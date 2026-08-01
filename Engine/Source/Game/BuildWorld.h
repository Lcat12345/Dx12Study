// BuildWorld.h : populates the demo scene as entities.
#pragma once

#include "Core/World.h"
#include "Graphics/ResourceManager.h"

// Creates every entity in the demo: geometry, the camera, and the lights.
// Nothing here is special-cased by the engine - a light and a cube are both
// just entities that happen to carry different components.
void BuildWorld(ResourceManager& resources, World& world);
