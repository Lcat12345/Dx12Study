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
    InitializeEditorCamera(m_world, m_editorCamera);
    m_camera = GetEditorCameraView(m_editorCamera);

    // Scans Assets/ once here; the panel's Refresh button does it again.
    m_assets = std::make_unique<AssetBrowser>(GetRenderer().Resources());
}

void DemoGame::OnUpdate(float dt)
{
    ImGuiLayer* overlay = GetRenderer().Overlay();
    const FrameContext hostFrame = CaptureHostFrame(dt);
    const FrameContext frame = MakeEditorFrameContext(
        hostFrame, m_viewportHovered, overlay && overlay->WantsKeyboard());

    if (m_editor.runMode == RunMode::Edit)
    {
        RunEditorOnly(frame);
    }
    else
    {
        RunPlayOnly(frame);
    }
    RunAlways(frame);
}

FrameContext DemoGame::CaptureHostFrame(float dt)
{
    FrameContext frame;
    frame.deltaSeconds = dt;
    frame.renderAspect = GetRenderer().SceneAspectRatio();

    GetWindow().ConsumeMouseDelta(frame.input.mouseDeltaX, frame.input.mouseDeltaY);
    for (int key = 0; key < InputContext::kKeyCount; ++key)
    {
        frame.input.keyDown[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        frame.input.keyPressed[key] = GetWindow().ConsumeKeyPress(key);
    }
    return frame;
}

void DemoGame::RunEditorOnly(const FrameContext& frame)
{
    RunEditorSystems(m_editorCamera, frame);
    m_camera = GetEditorCameraView(m_editorCamera);
}

void DemoGame::RunPlayOnly(const FrameContext& frame)
{
    m_play.BeginFrame(frame);
    RunPlaySystems(m_world, m_play);
    m_play.EndFrame();

    // Play presentation is scene-owned. Failure leaves the last valid game
    // camera in place instead of silently falling back to EditorCamera.
    GetActiveCameraView(m_world, m_camera);
}

void DemoGame::RunAlways(const FrameContext& frame)
{
    // The panels read and WRITE components directly - editing a Transform in
    // the inspector is the same operation a system performs.
    DebugUIContext ui;
    ui.dt               = frame.deltaSeconds;
    ui.fps              = CurrentFps();
    ui.vsync            = GetRenderer().IsVSync();
    ui.tearingSupported = GetRenderer().IsTearingSupported();
    ui.msaaEnabled      = GetRenderer().IsMsaaEnabled();
    ui.msaa4xSupported  = GetRenderer().Is4xMsaaSupported();
    ui.sceneTexture     = GetRenderer().SceneTextureId();
    ui.shadowTexture    = GetRenderer().ShadowTextureId();
    ui.shadowMapSize    = GetRenderer().ShadowMapSize();
    const DirectX::XMFLOAT3 shadowCenter = GetRenderer().ShadowSceneCenter();
    ui.shadowCenter[0]  = shadowCenter.x;
    ui.shadowCenter[1]  = shadowCenter.y;
    ui.shadowCenter[2]  = shadowCenter.z;
    ui.shadowRadius     = GetRenderer().ShadowSceneRadius();
    ui.debugMessages    = GetRenderer().DebugMessageCount();
    ui.hasDebugLayer    = GetRenderer().HasDebugLayer();
    ui.maxDrawItems     = GetRenderer().MaxDrawItems();
    // Last frame's list: OnRender rebuilds it after this runs.
    ui.drawItemCount    = unsigned(m_drawItems.size());
    ui.sceneAspect      = frame.renderAspect;
    ui.viewportCamera   = &m_camera;
    ui.runMode          = m_editor.runMode;
    ui.playElapsed      = m_play.ElapsedSeconds();

    DrawDebugUI(m_world, GetRenderer().Resources(), *m_assets, m_editor, ui);

    m_viewportHovered = ui.viewportHovered;

    // A request, not an immediate resize - the renderer applies it once it
    // can prove the GPU is done with the old texture.
    if (ui.viewportWidth > 0 && ui.viewportHeight > 0)
    {
        GetRenderer().SetSceneViewportSize(ui.viewportWidth, ui.viewportHeight);
    }

    if (ui.vsyncToggled || frame.input.WasPressed('V'))
    {
        GetRenderer().SetVSync(!GetRenderer().IsVSync());
    }
    if (ui.msaaToggled)
    {
        GetRenderer().SetMsaaEnabled(!GetRenderer().IsMsaaEnabled());
    }

    if (ui.runModeChangeRequested)
    {
        SetRunMode(ui.requestedRunMode);
    }
}

void DemoGame::SetRunMode(RunMode mode)
{
    if (mode == m_editor.runMode)
    {
        return;
    }

    if (mode == RunMode::Play)
    {
        if (m_editor.EnterPlay(m_world, GetRenderer().Resources(), m_play))
        {
            // The request is handled after this frame's system selection, so
            // select the Play camera now rather than showing one Edit-camera
            // frame under an already-Play toolbar.
            GetActiveCameraView(m_world, m_camera);
        }
    }
    else
    {
        if (m_editor.StopPlay(m_world, GetRenderer().Resources(), m_play))
        {
            m_camera = GetEditorCameraView(m_editorCamera);
        }
    }
}

void DemoGame::OnRender()
{
    // The one place the two halves meet: the world is flattened into plain
    // arrays, and the renderer takes it from there.
    BuildRenderData(m_world, GetRenderer().Resources(), m_drawItems, m_lighting);
    GetRenderer().Render(m_camera, m_lighting, m_drawItems);
}
