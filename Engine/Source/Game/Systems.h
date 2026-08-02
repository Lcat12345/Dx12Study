// Systems.h : pure logic. Systems own no state; they read and write
// components on a World.
#pragma once

#include "Core/World.h"
#include "Game/Components.h"
#include "Graphics/RenderData.h"

#include <DirectXMath.h>
#include <vector>

// Scale, then rotate, then translate - the order every other piece of code
// has to agree on. Shared rather than reimplemented because picking has to
// invert exactly the matrix the renderer drew with.
DirectX::XMMATRIX WorldMatrixOf(const Transform& transform);

// Advances every entity that has both a Spin and a Transform.
void SpinSystem(World& world, float dt);

// Applies mouse look and the held movement keys to the active camera's
// Transform. Frame-rate independence comes from dt, as before.
void CameraSystem(World& world, float mouseDeltaX, float mouseDeltaY, float dt);

// Moves point lights that have a Spin, so the orbiting light is now scene
// data rather than a hardcoded sine in the renderer.
void LightOrbitSystem(World& world, float totalSeconds);

// Fills outCamera from the entity tagged ActiveCamera. False when there is
// none, or when it has no Transform to sit in.
//
// Split out of BuildRenderData because picking needs exactly the same camera
// the renderer drew with - reconstructing it separately is how a click ends
// up landing somewhere the user did not point at.
//
// On success it overwrites EVERY field, so the result never depends on what
// outCamera happened to contain. On failure it writes nothing, and the two
// callers then diverge deliberately: the renderer keeps drawing from the
// last good view, while picking refuses to place anything. Both are the
// safe answer to "there is no camera" for their own job.
bool GetActiveCameraView(World& world, CameraView& outCamera);

// --- the render boundary ---
// Walks the world once and flattens it into what the renderer accepts.
// Nothing below this line knows about entities.
void BuildRenderData(World& world,
                     std::vector<DrawItem>& outItems,
                     CameraView& outCamera,
                     LightingData& outLighting);
