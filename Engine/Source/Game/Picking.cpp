#include "Game/Picking.h"

#include "Game/Components.h"
#include "Game/Systems.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

using namespace DirectX;

Ray RayFromNdc(const CameraView& camera, float aspect, float ndcX, float ndcY)
{
    // The same two matrices the renderer builds every frame. Inverting their
    // PRODUCT undoes the whole camera in one step - there is no need to peel
    // off projection and view separately, and doing it in one inverse is
    // also one place to get wrong instead of two.
    const XMVECTOR eye     = XMLoadFloat3(&camera.position);
    const XMVECTOR forward = XMVector3Normalize(XMLoadFloat3(&camera.forward));
    const XMVECTOR up      = XMLoadFloat3(&camera.up);

    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(camera.fovY, aspect,
                                                   camera.nearZ, camera.farZ);
    const XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);

    // Two points that project to the SAME pixel, one on each clip plane.
    // The line through them is the ray.
    //
    // z = 0 is the near plane and z = 1 the far plane: D3D's depth range.
    // (OpenGL uses -1 for near, and copying that here is a classic port bug -
    // the ray would start behind the camera and still look almost right.)
    //
    // TransformCoord, not TransformNormal or Transform: it performs the
    // perspective divide by w. Skipping it leaves homogeneous coordinates
    // that are not positions at all.
    const XMVECTOR nearPoint = XMVector3TransformCoord(
        XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
    const XMVECTOR farPoint = XMVector3TransformCoord(
        XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);

    Ray ray;
    XMStoreFloat3(&ray.origin, nearPoint);
    XMStoreFloat3(&ray.direction, XMVectorSubtract(farPoint, nearPoint));
    return ray;
}

bool RayToLocalSpace(const Ray& worldRay, const XMFLOAT4X4& world, Ray& outLocalRay)
{
    const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);

    // A zero scale on any axis collapses the matrix and there is no inverse.
    // XMMatrixInverse hands back a determinant precisely so this is
    // detectable instead of producing quiet NaNs downstream.
    XMVECTOR determinant = XMVectorZero();
    const XMMATRIX inverse = XMMatrixInverse(&determinant, worldMatrix);
    if (XMVector4Equal(determinant, XMVectorZero()))
    {
        return false;
    }

    const XMVECTOR origin    = XMLoadFloat3(&worldRay.origin);
    const XMVECTOR direction = XMLoadFloat3(&worldRay.direction);

    // Coord for the origin (translation applies), Normal for the direction
    // (it must not). Getting these the wrong way round moves the ray by the
    // object's position twice.
    XMStoreFloat3(&outLocalRay.origin, XMVector3TransformCoord(origin, inverse));
    XMStoreFloat3(&outLocalRay.direction, XMVector3TransformNormal(direction, inverse));
    return true;
}

bool RayAabb(const Ray& ray, const Aabb& bounds, float& outDistance)
{
    if (bounds.IsEmpty())
    {
        return false;
    }

    // The slab method: the box is three pairs of parallel planes, and the
    // ray is inside the box exactly where all three intervals overlap.
    float tEnter = -FLT_MAX;
    float tExit  =  FLT_MAX;

    const float origin[3]    = { ray.origin.x,    ray.origin.y,    ray.origin.z };
    const float direction[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
    const float minimum[3]   = { bounds.min.x,    bounds.min.y,    bounds.min.z };
    const float maximum[3]   = { bounds.max.x,    bounds.max.y,    bounds.max.z };

    for (int axis = 0; axis < 3; ++axis)
    {
        constexpr float kParallelEpsilon = 1e-8f;
        if (std::fabs(direction[axis]) < kParallelEpsilon)
        {
            // Parallel to this pair of planes: a miss unless it already
            // lies between them.
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
            {
                return false;
            }
            continue;
        }

        const float inverseDirection = 1.0f / direction[axis];
        float tNear = (minimum[axis] - origin[axis]) * inverseDirection;
        float tFar  = (maximum[axis] - origin[axis]) * inverseDirection;
        if (tNear > tFar)
        {
            std::swap(tNear, tFar); // pointing down this axis
        }

        tEnter = std::max(tEnter, tNear);
        tExit  = std::min(tExit,  tFar);
        if (tEnter > tExit)
        {
            return false; // the intervals stopped overlapping
        }
    }

    if (tExit < 0.0f)
    {
        return false; // the whole box is behind the ray
    }

    // A negative entry with a positive exit means the ray STARTED inside.
    // Reporting 0 rather than the negative value keeps "nearest hit wins"
    // working when the camera is inside a large mesh.
    outDistance = tEnter < 0.0f ? 0.0f : tEnter;
    return true;
}

bool PickEntity(World& world, const ResourceManager& resources, const Ray& ray,
                Entity& outEntity)
{
    Entity nearest;
    float  nearestDistance = FLT_MAX;

    world.ForEach<MeshRenderer>([&](Entity entity, MeshRenderer& renderer) {
        const Transform* transform = world.Get<Transform>(entity);
        if (!transform || !renderer.mesh.IsValid())
        {
            return; // nothing drawn means nothing to click
        }

        XMFLOAT4X4 world4x4;
        XMStoreFloat4x4(&world4x4, WorldMatrixOf(*transform));

        Ray localRay;
        if (!RayToLocalSpace(ray, world4x4, localRay))
        {
            return; // degenerate transform - skip rather than guess
        }

        float distance = 0.0f;
        if (!RayAabb(localRay, resources.GetMesh(renderer.mesh).bounds, distance))
        {
            return;
        }

        // Clip to the same depth range the renderer draws.
        //
        // RayFromNdc builds its direction as (far point - near point), so
        // t = 0 is the near plane and t = 1 is the FAR plane. RayToLocalSpace
        // preserves t - transforming origin and direction by the same matrix
        // maps O + t*D to O' + t*D' - so the scale survives into local space.
        //
        // Without this an entity past farZ is still hit by the ray even
        // though the far plane clipped it out of the picture. Clicking empty
        // background would select something invisible.
        if (distance > 1.0f)
        {
            return;
        }

        // Comparable across objects because RayToLocalSpace leaves the
        // direction unnormalized: t stays in world-ray units.
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest         = entity;
        }
    });

    outEntity = nearest;
    return nearest.IsValid();
}

bool RayPlaneY(const Ray& ray, float planeY, XMFLOAT3& outPoint)
{
    // Solve origin.y + t * direction.y = planeY for t.
    constexpr float kParallelEpsilon = 1e-6f;
    if (std::fabs(ray.direction.y) < kParallelEpsilon)
    {
        // Looking along the floor. There is no crossing, and dividing would
        // produce an enormous t that lands the object at the horizon.
        return false;
    }

    const float t = (planeY - ray.origin.y) / ray.direction.y;
    if (t < 0.0f)
    {
        // The plane is behind the camera - looking up at the sky.
        return false;
    }

    outPoint.x = ray.origin.x + ray.direction.x * t;
    outPoint.y = planeY;
    outPoint.z = ray.origin.z + ray.direction.z * t;
    return true;
}
