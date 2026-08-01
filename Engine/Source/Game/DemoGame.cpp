#include "Game/DemoGame.h"

#include "Game/BuildWorld.h"
#include "Game/DebugUI.h"
#include "Game/Systems.h"

#include "Graphics/ImGuiLayer.h"

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
    ImGuiLayer* overlay = GetRenderer().Overlay();

    // Without this check, dragging a slider would also spin the camera.
    const bool uiHasMouse    = overlay && overlay->WantsMouse();
    const bool uiHasKeyboard = overlay && overlay->WantsKeyboard();

    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    GetWindow().ConsumeMouseDelta(mouseDeltaX, mouseDeltaY);
    if (uiHasMouse)
    {
        mouseDeltaX = 0.0f;
        mouseDeltaY = 0.0f;
    }

    // Systems, in order. Each one reads and writes components; none of them
    // owns state of its own.
    CameraSystem(m_world, mouseDeltaX, mouseDeltaY, uiHasKeyboard ? 0.0f : dt);
    SpinSystem(m_world, dt);
    LightOrbitSystem(m_world, TotalSeconds());

    // The panels read and WRITE components directly - editing a Transform in
    // the inspector is the same operation a system performs.
    bool vsyncToggled = false;
    DrawDebugUI(m_world, dt, CurrentFps(),
                GetRenderer().IsVSync(), GetRenderer().IsTearingSupported(),
                vsyncToggled);

    if (vsyncToggled || GetWindow().ConsumeKeyPress('V'))
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
