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

    // Scans Assets/ once here; the panel's Refresh button does it again.
    m_assets = std::make_unique<AssetBrowser>(GetRenderer().Resources());
}

void DemoGame::OnUpdate(float dt)
{
    ImGuiLayer* overlay = GetRenderer().Overlay();

    // The scene lives inside a window now, so "is the UI using the mouse" is
    // the wrong question - the UI is ALWAYS using it. The right one is
    // whether the cursor is over the scene viewport.
    const bool cameraHasMouse = m_viewportHovered;
    // Keyboard still asks ImGui: a text field must swallow WASD wherever it
    // is, and unlike the mouse there is no "over the viewport" to test.
    const bool uiHasKeyboard  = overlay && overlay->WantsKeyboard();

    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    // Consumed unconditionally: leaving the delta in the window would apply
    // it in one lump the moment the cursor re-enters the viewport.
    GetWindow().ConsumeMouseDelta(mouseDeltaX, mouseDeltaY);
    if (!cameraHasMouse)
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
    DebugUIContext ui;
    ui.dt               = dt;
    ui.fps              = CurrentFps();
    ui.vsync            = GetRenderer().IsVSync();
    ui.tearingSupported = GetRenderer().IsTearingSupported();
    ui.sceneTexture     = GetRenderer().SceneTextureId();
    ui.maxDrawItems     = GetRenderer().MaxDrawItems();
    // Last frame's list: OnRender rebuilds it after this runs.
    ui.drawItemCount    = unsigned(m_drawItems.size());
    ui.sceneAspect      = GetRenderer().SceneAspectRatio();

    DrawDebugUI(m_world, GetRenderer().Resources(), *m_assets, ui);

    m_viewportHovered = ui.viewportHovered;

    // A request, not an immediate resize - the renderer applies it once it
    // can prove the GPU is done with the old texture.
    if (ui.viewportWidth > 0 && ui.viewportHeight > 0)
    {
        GetRenderer().SetSceneViewportSize(ui.viewportWidth, ui.viewportHeight);
    }

    if (ui.vsyncToggled || GetWindow().ConsumeKeyPress('V'))
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
