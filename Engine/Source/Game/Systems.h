// Systems.h : pure logic. Systems own no state; they read and write
// components on a World.
#pragma once

#include "Core/World.h"
#include "Graphics/RenderData.h"

#include <vector>

// Advances every entity that has both a Spin and a Transform.
void SpinSystem(World& world, float dt);

// Applies mouse look and the held movement keys to the active camera's
// Transform. Frame-rate independence comes from dt, as before.
void CameraSystem(World& world, float mouseDeltaX, float mouseDeltaY, float dt);

// Moves point lights that have a Spin, so the orbiting light is now scene
// data rather than a hardcoded sine in the renderer.
void LightOrbitSystem(World& world, float totalSeconds);

// --- the render boundary ---
// Walks the world once and flattens it into what the renderer accepts.
// Nothing below this line knows about entities.
void BuildRenderData(World& world,
                     std::vector<DrawItem>& outItems,
                     CameraView& outCamera,
                     LightingData& outLighting);
