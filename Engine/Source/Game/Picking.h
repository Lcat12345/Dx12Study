// Picking.h : turning a click in the viewport into a point in the world.
//
// The reverse of what the vertex shader does. Rendering takes a world
// position through view and projection to a pixel; this takes a pixel back
// out to a ray. Everything that follows from a mouse in a 3D view - placing,
// selecting, dragging a gizmo - starts here.
#pragma once

#include "Graphics/RenderData.h"

#include <DirectXMath.h>

// A ray in world space. The direction is deliberately NOT normalized: it is
// "near plane to far plane", and every use here is a ratio where its length
// cancels out.
struct Ray
{
    DirectX::XMFLOAT3 origin    = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 direction = { 0.0f, 0.0f, 1.0f };
};

// ndcX/ndcY are D3D clip space: both in [-1, 1] with Y pointing UP.
//
// The aspect ratio must be the one the image was actually rendered with -
// the scene render target's, not the window's. Feed it the wrong one and the
// ray misses horizontally by exactly the ratio between them, which is why
// this is the same argument the renderer builds its projection from.
Ray RayFromNdc(const CameraView& camera, float aspect, float ndcX, float ndcY);

// Where the ray crosses the horizontal plane y = planeY.
// False when the ray runs parallel to the plane, or when the crossing is
// behind the camera - both are "the user clicked the sky".
bool RayPlaneY(const Ray& ray, float planeY, DirectX::XMFLOAT3& outPoint);
