#include "Game/DemoGame.h"

#include "Game/BuildWorld.h"
#include "Game/Systems.h"

namespace
{
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine";
    constexpr UINT    kClientWidth   = 1280;
    constexpr UINT    kClientHeight  = 720;
}

DemoGame::DemoGame(HINSTANCE instance)
    : Engine(instance, kWindowTitle, kClientWidth, kClientHeight)
{
}

void DemoGame::OnInit()
{
    // The renderer already exists, so its resource manager can load assets.
    BuildWorld(GetRenderer().Resources(), m_world);
}

void DemoGame::OnUpdate(float dt)
{
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    GetWindow().ConsumeMouseDelta(mouseDeltaX, mouseDeltaY);

    // Systems, in order. Each one reads and writes components; none of them
    // owns state of its own.
    CameraSystem(m_world, mouseDeltaX, mouseDeltaY, dt);
    SpinSystem(m_world, dt);
    LightOrbitSystem(m_world, TotalSeconds());

    // Toggling vsync is how we measure: with it on, the frame rate is pinned
    // to the refresh rate and says nothing about how much work a frame does.
    if (GetWindow().ConsumeKeyPress('V'))
    {
        GetRenderer().SetVSync(!GetRenderer().IsVSync());
    }
}

void DemoGame::OnRender()
{
    // The one place the two halves meet: the world is flattened into plain
    // arrays, and the renderer takes it from there.
    BuildRenderData(m_world, m_drawItems, m_camera, m_lighting);
    GetRenderer().Render(m_camera, m_lighting, m_drawItems);
}
