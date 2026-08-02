// Picking.h : turning a click in the viewport into a point in the world.
//
// The reverse of what the vertex shader does. Rendering takes a world
// position through view and projection to a pixel; this takes a pixel back
// out to a ray. Everything that follows from a mouse in a 3D view - placing,
// selecting, dragging a gizmo - starts here.
#pragma once

#include "Core/World.h"
#include "Graphics/Mesh.h"
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

// Moves a ray into an object's local space.
//
// The alternative - growing a world-space box around every rotated mesh -
// is both looser and has to be redone whenever anything moves. One inverse
// per candidate keeps the tight local box.
//
// The direction is transformed as a VECTOR (no translation) and is NOT
// renormalized: leaving its length alone keeps the parameter t on the same
// scale as the world ray, so hits from different objects stay comparable.
//
// False when the matrix cannot be inverted - a zero or near-zero scale on
// some axis. A scene file can hold one even though the Inspector clamps.
bool RayToLocalSpace(const Ray& worldRay, const DirectX::XMFLOAT4X4& world,
                     Ray& outLocalRay);

// Slab test. outDistance is the entry distance, or 0 when the ray starts
// INSIDE the box - otherwise standing inside a large mesh would make it
// unselectable.
bool RayAabb(const Ray& ray, const Aabb& bounds, float& outDistance);

// The nearest live entity whose mesh bounds the ray hits.
//
// This is the one function here that knows entities exist - Graphics/ still
// does not. It reads mesh bounds through the ResourceManager, the same way
// the renderer reads vertex buffers.
//
// Bounds only, not triangles: a box test picks a mesh from its silhouette
// closely enough for an editor, and per-triangle precision costs a spatial
// structure nobody needs yet.
bool PickEntity(World& world, const ResourceManager& resources, const Ray& ray,
                Entity& outEntity);
