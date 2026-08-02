#include "Game/Picking.h"

#include <cmath>

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
