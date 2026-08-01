// DemoGame.h : the game side - what is in the world and how it is driven.
#pragma once

#include "Core/Engine.h"
#include "Core/World.h"
#include "Graphics/RenderData.h"

#include <vector>

// Everything specific to this demo lives here; Engine and Renderer stay
// unaware of entities.
class DemoGame : public Engine
{
public:
    DemoGame(HINSTANCE instance);

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    World m_world;

    // Rebuilt every frame by the render system. Kept as a member so the
    // vector's storage is reused instead of reallocated 5000 times a second.
    std::vector<DrawItem> m_drawItems;
    CameraView            m_camera;
    LightingData          m_lighting;

    // Set by last frame's Scene panel. The systems run before the UI is
    // built, so this is necessarily one frame old - which is invisible at
    // frame rate, and the alternative (building the UI first) would let a
    // panel edit be overwritten by a system in the same frame.
    bool m_viewportHovered = false;
};
