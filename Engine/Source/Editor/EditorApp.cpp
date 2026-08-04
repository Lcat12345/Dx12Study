#include "Editor/EditorApp.h"

#include "Game/BuildWorld.h"
#include "Game/DebugUI.h"
#include "Game/Systems.h"

namespace
{
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine Editor";
    constexpr UINT    kClientWidth   = 1280;
    constexpr UINT    kClientHeight  = 720;
}

EditorApp::EditorApp(HINSTANCE instance, const RuntimePaths& runtimePaths)
    : Engine(instance, kWindowTitle, kClientWidth, kClientHeight, runtimePaths)
{
    Renderer& renderer = GetRenderer();
    m_overlay = std::make_unique<ImGuiLayer>(
        GetWindow().Handle(), renderer.Device(), renderer.CommandQueue(),
        renderer.ShaderVisibleDescriptors(), renderer.OutputFormat(),
        DXGI_FORMAT_UNKNOWN, renderer.FramesInFlight());

    GetWindow().SetMessageHook(
        [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            return ImGuiLayer::HandleMessage(hwnd, message, wParam, lParam);
        });
}

void EditorApp::OnInit()
{
    BuildWorld(GetRenderer().Resources(), m_world);
    InitializeEditorCamera(m_world, m_editorCamera);
    m_camera = GetEditorCameraView(m_editorCamera);
    m_assets = std::make_unique<AssetBrowser>(GetRenderer().Resources());
}

void EditorApp::OnUpdate(float dt)
{
    m_overlay->NewFrame();
    const FrameContext hostFrame = CaptureHostFrame(dt);
    const FrameContext frame = MakeEditorFrameContext(
        hostFrame, m_viewportHovered, m_overlay->WantsKeyboard());

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

FrameContext EditorApp::CaptureHostFrame(float dt)
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

void EditorApp::RunEditorOnly(const FrameContext& frame)
{
    RunEditorSystems(m_editorCamera, frame);
    m_camera = GetEditorCameraView(m_editorCamera);
}

void EditorApp::RunPlayOnly(const FrameContext& frame)
{
    m_play.BeginFrame(frame);
    RunPlaySystems(m_world, m_play);
    m_play.EndFrame();
    GetActiveCameraView(m_world, m_camera);
}

void EditorApp::RunAlways(const FrameContext& frame)
{
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
    ui.drawItemCount    = unsigned(m_drawItems.size());
    ui.sceneAspect      = frame.renderAspect;
    ui.viewportCamera   = &m_camera;
    ui.runMode          = m_editor.runMode;
    ui.playElapsed      = m_play.ElapsedSeconds();

    DrawDebugUI(m_world, GetRenderer().Resources(), *m_assets, m_editor, ui);
    m_viewportHovered = ui.viewportHovered;

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

void EditorApp::SetRunMode(RunMode mode)
{
    if (mode == m_editor.runMode)
    {
        return;
    }

    if (mode == RunMode::Play)
    {
        if (m_editor.EnterPlay(m_world, GetRenderer().Resources(), m_play))
        {
            GetActiveCameraView(m_world, m_camera);
        }
    }
    else if (m_editor.StopPlay(m_world, GetRenderer().Resources(), m_play))
    {
        m_camera = GetEditorCameraView(m_editorCamera);
    }
}

void EditorApp::OnRender()
{
    BuildRenderData(m_world, GetRenderer().Resources(), m_drawItems, m_lighting);
    GetRenderer().RenderFrame(
        m_camera, m_lighting, m_drawItems,
        Renderer::SceneOutput::OffscreenTexture,
        [this](ID3D12GraphicsCommandList* commandList) {
            m_overlay->Render(commandList);
        });
}

EditorApp::~EditorApp()
{
    GetRenderer().WaitForGpu();
    GetWindow().SetMessageHook({});
    m_overlay.reset();
}
