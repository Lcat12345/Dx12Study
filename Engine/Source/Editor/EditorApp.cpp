#include "Editor/EditorApp.h"

#include "Game/Arena.h"
#include "Game/BuildWorld.h"
#include "Editor/DebugUI.h"
#include "Game/Systems.h"

namespace
{
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine Editor";
    constexpr UINT    kClientWidth   = 1280;
    constexpr UINT    kClientHeight  = 720;
}

EditorApp::EditorApp(HINSTANCE instance, const RuntimePaths& runtimePaths)
    : Engine(instance, kWindowTitle, kClientWidth, kClientHeight, runtimePaths)
    , m_packageBuilder(runtimePaths)
{
    Renderer& renderer = GetRenderer();
    // The overlay caches the heap and binds it itself, so the editor - not
    // RenderFrame - decides when it may be replaced. See
    // SyncOverlayToDescriptorHeap.
    renderer.SetHostManagesDescriptorHeapGrowth(true);
    m_overlay = std::make_unique<ImGuiLayer>(
        GetWindow().Handle(), renderer.Device(), renderer.CommandQueue(),
        renderer.ShaderVisibleDescriptors(), renderer.OutputFormat(),
        DXGI_FORMAT_UNKNOWN, renderer.FramesInFlight());

    // A first run has no imgui.ini, so nothing would place the panels and they
    // would open overlapping. Any later run has a saved arrangement that is
    // the user's, not ours, and must be left alone. Only the layer can tell
    // the two apart, and only before ImGui loads the file.
    m_applyDefaultLayout = !m_overlay->HadSavedLayout();
    // The overlay was just built against the heap as it stands, so the first
    // frame has nothing to rebuild.
    m_descriptorHeapGeneration = renderer.ShaderVisibleDescriptors().Generation();

    GetWindow().SetMessageHook(
        [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            return ImGuiLayer::HandleMessage(hwnd, message, wParam, lParam);
        });
}

// Keep the overlay's view of the descriptor heap current.
//
// Two things can replace it: this call, which grows it on purpose at the one
// moment of the frame where doing so is safe for ImGui, and RenderFrame's own
// backstop, which may have grown it after a texture load late in the previous
// frame. Comparing the generation covers both without caring which happened.
void EditorApp::SyncOverlayToDescriptorHeap()
{
    GetRenderer().EnsureShaderVisibleDescriptorCapacity();

    const uint64_t generation =
        GetRenderer().ShaderVisibleDescriptors().Generation();
    if (generation == m_descriptorHeapGeneration)
    {
        return;
    }
    m_descriptorHeapGeneration = generation;

    // The growth itself already drained the GPU. Drain again anyway for the
    // case where RenderFrame's backstop grew the heap late in the previous
    // frame: the overlay may have recorded draws against the old heap, and
    // those must be finished before its handles are moved off it.
    GetRenderer().WaitForGpu();
    m_overlay->RebindDescriptorHeap();
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
    // Before NewFrame, deliberately. The DX12 backend caches the SRV heap
    // pointer and binds it itself, so the heap has to settle for this frame
    // before any ImGui work starts - growing it halfway through would leave
    // the overlay drawing from a heap nobody is publishing into any more.
    SyncOverlayToDescriptorHeap();
    m_overlay->NewFrame();
    const FrameContext hostFrame = CaptureHostFrame(dt);
    const FrameContext frame = MakeEditorFrameContext(
        hostFrame, m_viewportHovered, m_overlay->WantsTextInput());

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
    RunPlaySystems(m_world, m_play, &GetRenderer().Resources());
    m_play.EndFrame();
    GetActiveCameraView(m_world, m_camera);
}

void EditorApp::RunAlways(const FrameContext& frame)
{
    m_packageBuilder.Poll();

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
    // Consumed by this frame and cleared, so the layout is rebuilt exactly
    // once. m_applyDefaultLayout starts true only when there was no imgui.ini
    // to honour; View > Reset layout sets it again below.
    ui.applyDefaultLayout = m_applyDefaultLayout;
    m_applyDefaultLayout  = false;

    DrawDebugUI(m_world, GetRenderer().Resources(), *m_assets, m_editor,
                m_packageBuilder, ui);
    m_viewportHovered = ui.viewportHovered;

    // Deferred to the NEXT frame on purpose: this frame's panels have already
    // been laid out by the time the menu item ran, and rebuilding the dock
    // tree under them leaves the windows referencing nodes that no longer
    // exist.
    if (ui.resetLayoutRequested)
    {
        m_applyDefaultLayout = true;
    }

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
            InitializeArenaIfPresent(m_world, GetRenderer().Resources());
            GetActiveCameraView(m_world, m_camera);
        }
    }
    else if (m_editor.StopPlay(m_world, GetRenderer().Resources(), m_play))
    {
        m_camera = GetEditorCameraView(m_editorCamera);
    }
}

uint64_t EditorApp::MeasurementEnemyCount()
{
    return GetArenaStatus(m_world).currentEnemyCount;
}

std::wstring EditorApp::MeasurementTitleStatus()
{
    return ArenaTitleStatus(m_world);
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
