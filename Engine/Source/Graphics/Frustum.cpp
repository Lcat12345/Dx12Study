#include "Graphics/Frustum.h"

#include <cfloat>
#include <cmath>

using namespace DirectX;

namespace
{
    // Slack on every plane test, in world units.
    //
    // The exact test already treats a box touching a plane as visible; this
    // widens that by more than a float's worth of error at the scales the
    // arena works at. Culling is an optimisation, so it must lose an argument
    // with rounding by drawing something twice rather than not at all.
    constexpr float kPlaneTolerance = 1e-4f;

    // Below this the plane's normal carries no direction - a matrix row that
    // cancelled out. Zeroed rather than normalised, and read as "inside".
    constexpr float kMinPlaneLength = 1e-12f;
}

// The clip volume is -w <= x <= w, -w <= y <= w, 0 <= z <= w (D3D, not GL).
// Each of those six inequalities is a plane once the clip coordinate is
// written back as a dot product with a COLUMN of the view-projection: with
// row vectors, clip.x is dot(p, column 0) and clip.w is dot(p, column 3), so
// `x + w >= 0` is the plane column0 + column3. Transposing turns the columns
// into rows this code can index.
Frustum ExtractFrustum(FXMMATRIX viewProj)
{
    const XMMATRIX columns = XMMatrixTranspose(viewProj);
    const XMVECTOR raw[Frustum::kPlaneCount] = {
        XMVectorAdd(columns.r[3], columns.r[0]),      // -w <= x
        XMVectorSubtract(columns.r[3], columns.r[0]), //  x <= w
        XMVectorAdd(columns.r[3], columns.r[1]),      // -w <= y
        XMVectorSubtract(columns.r[3], columns.r[1]), //  y <= w
        columns.r[2],                                 //  0 <= z
        XMVectorSubtract(columns.r[3], columns.r[2]), //  z <= w
    };

    Frustum frustum;
    for (int index = 0; index < Frustum::kPlaneCount; ++index)
    {
        // Normalising the xyz part is what makes `dot(n, p) + d` a DISTANCE,
        // which is what the box's projected radius below is compared against.
        const float length = XMVectorGetX(XMVector3Length(raw[index]));
        const XMVECTOR plane = (std::isfinite(length) && length > kMinPlaneLength)
                             ? XMVectorScale(raw[index], 1.0f / length)
                             : XMVectorZero();
        XMStoreFloat4(&frustum.planes[index], plane);
    }
    return frustum;
}

Aabb TransformAabb(const Aabb& localBounds, FXMMATRIX world)
{
    if (localBounds.IsEmpty())
    {
        return localBounds;
    }

    XMVECTOR minCorner = XMVectorReplicate( FLT_MAX);
    XMVECTOR maxCorner = XMVectorReplicate(-FLT_MAX);
    for (int corner = 0; corner < 8; ++corner)
    {
        // Bit 0/1/2 pick min or max on x/y/z - the eight combinations.
        const XMVECTOR localPoint = XMVectorSet(
            (corner & 1) ? localBounds.max.x : localBounds.min.x,
            (corner & 2) ? localBounds.max.y : localBounds.min.y,
            (corner & 4) ? localBounds.max.z : localBounds.min.z,
            1.0f);
        const XMVECTOR worldPoint = XMVector3TransformCoord(localPoint, world);
        minCorner = XMVectorMin(minCorner, worldPoint);
        maxCorner = XMVectorMax(maxCorner, worldPoint);
    }

    Aabb result;
    XMStoreFloat3(&result.min, minCorner);
    XMStoreFloat3(&result.max, maxCorner);

    // A transform holding NaN or infinity produces corners that compare
    // false against everything, leaving an inverted box. Report that as
    // empty so the caller falls back to "assume visible" instead of testing
    // garbage.
    const bool finite =
        std::isfinite(result.min.x) && std::isfinite(result.min.y) &&
        std::isfinite(result.min.z) && std::isfinite(result.max.x) &&
        std::isfinite(result.max.y) && std::isfinite(result.max.z);
    return finite ? result : Aabb{ { 1.0f, 1.0f, 1.0f }, { -1.0f, -1.0f, -1.0f } };
}

bool IntersectsFrustum(const Frustum& frustum, const Aabb& worldBounds,
                       uint32_t planeMask)
{
    if (worldBounds.IsEmpty())
    {
        return true; // nothing to test against - never a reason to cull
    }

    const XMFLOAT3 centerValue  = worldBounds.Center();
    const XMFLOAT3 extentsValue = worldBounds.Extents();
    const XMVECTOR center  = XMLoadFloat3(&centerValue);
    const XMVECTOR extents = XMLoadFloat3(&extentsValue);

    for (int index = 0; index < Frustum::kPlaneCount; ++index)
    {
        if ((planeMask & (1u << index)) == 0)
        {
            continue; // this side is unbounded for this test
        }

        const XMVECTOR plane = XMLoadFloat4(&frustum.planes[index]);
        // How far the box reaches toward the plane from its centre. Using
        // |n| against the extents gives the box's radius along the plane's
        // own axis, which is exact for an axis-aligned box and needs no
        // per-corner loop.
        const float radius = XMVectorGetX(
            XMVector3Dot(XMVectorAbs(plane), extents));
        const float distance = XMVectorGetX(XMVector3Dot(plane, center)) +
                               XMVectorGetW(plane);
        if (distance + radius < -kPlaneTolerance)
        {
            return false; // wholly on the outside of one plane - done
        }
    }
    return true;
}

uint32_t ExtrudedPlaneMask(const Frustum& frustum, FXMVECTOR sweepDirection)
{
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(sweepDirection));
    if (!std::isfinite(lengthSq) || lengthSq < kMinPlaneLength)
    {
        return Frustum::kAllPlanes; // no direction to sweep along
    }
    const XMVECTOR direction = XMVector3Normalize(sweepDirection);

    uint32_t mask = 0;
    for (int index = 0; index < Frustum::kPlaneCount; ++index)
    {
        // XMVector3Dot reads xyz only, so the plane's distance term takes no
        // part in this - only which way its normal faces.
        const XMVECTOR plane = XMLoadFloat4(&frustum.planes[index]);
        // Sweeping along the inward normal can only push points further
        // inside, so the plane still bounds the result and stays in the mask.
        if (XMVectorGetX(XMVector3Dot(plane, direction)) >= 0.0f)
        {
            mask |= (1u << index);
        }
    }
    return mask;
}
