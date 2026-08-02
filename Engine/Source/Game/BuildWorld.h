// BuildWorld.h : populates the demo scene as entities.
#pragma once

#include "Core/World.h"
#include "Graphics/ResourceManager.h"

// Creates every entity in the demo: geometry, the camera, and the lights.
// Nothing here is special-cased by the engine - a light and a cube are both
// just entities that happen to carry different components.
void BuildWorld(ResourceManager& resources, World& world);

// What File > New gives you: not an empty world, but the smallest one you
// can actually work in. With no camera the viewport shows nothing and
// placing refuses, so a blank slate has to come with a viewpoint, something
// to light it, and an ambient term.
void BuildEmptyScene(World& world);
