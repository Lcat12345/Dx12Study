#include "Game/DemoGame.h"

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
    // The renderer already exists, so its device can build the meshes.
    BuildScene(GetRenderer().Device(), m_scene);
}

void DemoGame::OnUpdate(float dt)
{
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    GetWindow().ConsumeMouseDelta(mouseDeltaX, mouseDeltaY);
    UpdateCamera(m_camera, mouseDeltaX, mouseDeltaY, dt);

    // Toggling vsync is how we measure: with it on, the frame rate is pinned
    // to the refresh rate and says nothing about how much work a frame does.
    if (GetWindow().ConsumeKeyPress('V'))
    {
        GetRenderer().SetVSync(!GetRenderer().IsVSync());
    }
}

void DemoGame::OnRender()
{
    GetRenderer().Render(m_scene, m_camera, TotalSeconds());
}
