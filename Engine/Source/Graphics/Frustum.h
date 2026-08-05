// Frustum.h : the geometry visibility is decided with.
//
// Deliberately free functions over plain data rather than methods on the
// renderer: what lands in which draw queue is the one part of culling that is
// worth testing without a device, and a pure function is what makes that
// possible.
#pragma once

#include "Graphics/Mesh.h"

#include <DirectXMath.h>
#include <cstdint>

// Six world-space planes, each stored as (nx, ny, nz, d) with the normal
// pointing INWARD, so `dot(n, p) + d >= 0` means "on the inside of that
// plane". A zeroed plane is the degenerate case and reads as "everything is
// inside", which is the direction culling is allowed to be wrong in.
struct Frustum
{
    enum PlaneIndex
    {
        Left,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        kPlaneCount
    };

    DirectX::XMFLOAT4 planes[kPlaneCount] = {};

    // Which planes a test must honour. Dropping a plane makes that side
    // unbounded - the mechanism ExtrudedPlaneMask uses to sweep a frustum
    // along a direction.
    static constexpr uint32_t kAllPlanes = (1u << kPlaneCount) - 1u;
};

// The six planes of whatever volume `viewProj` maps to the clip cube. Works
// for a perspective camera and an orthographic light alike, because it reads
// the matrix rather than the parameters it was built from.
Frustum ExtractFrustum(DirectX::FXMMATRIX viewProj);

// The world box around the local box's EIGHT corners.
//
// A local box pushed through a matrix is not a box - a rotated one has to be
// re-bounded. Eight corners is loose for a rotated mesh and never too small,
// and too small is the only error that can make something vanish.
//
// An empty local box stays empty, which callers read as "no usable bounds".
Aabb TransformAabb(const Aabb& localBounds, DirectX::FXMMATRIX world);

// True when the box may be inside. Only a box that is CERTAINLY outside one
// of the selected planes returns false: ties and near-ties count as visible,
// so a rounding error can cost a wasted draw but never a missing object.
bool IntersectsFrustum(const Frustum& frustum, const Aabb& worldBounds,
                       uint32_t planeMask = Frustum::kAllPlanes);

// The planes that still bound `frustum` after every point in it is swept
// along `sweepDirection` without limit.
//
// Sweeping a half-space whose inward normal points along the sweep leaves it
// unchanged; sweeping one that points against it covers all of space, so that
// plane drops out. Applying that per plane gives a volume that CONTAINS the
// true swept frustum - conservative, which is what a caster test needs.
//
// This is how an off-screen shadow caster is kept: sweep the camera frustum
// back toward the light, and everything whose shadow can land on screen is
// inside the result.
uint32_t ExtrudedPlaneMask(const Frustum& frustum,
                           DirectX::FXMVECTOR sweepDirection);
