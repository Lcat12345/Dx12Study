#include "Player/PlayerApp.h"

#include "Game/Arena.h"
#include "Game/Scene.h"
#include "Game/Systems.h"
#include "Core/ProcessLog.h"
#include "Core/TextEncoding.h"

#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr UINT    kClientWidth   = 1280;
    constexpr UINT    kClientHeight  = 720;

    std::string TsvField(std::string_view value)
    {
        std::string sanitized(value);
        for (char& character : sanitized)
        {
            if (character == '\t' || character == '\r' || character == '\n')
            {
                character = ' ';
            }
        }
        return sanitized;
    }
}

PlayerApp::PlayerApp(HINSTANCE instance, const RuntimePaths& runtimePaths,
                     PlayerStartup startup, const wchar_t* windowTitle)
    : Engine(instance, windowTitle, kClientWidth, kClientHeight, runtimePaths)
    , m_startup(std::move(startup))
{
    if (m_startup.benchmark.enabled)
    {
        ConfigureFrameSampling(m_startup.benchmark.warmupFrames,
                               m_startup.benchmark.sampleFrames);
    }
}

void PlayerApp::OnInit()
{
    GetRenderer().SetFrustumCullingEnabled(m_startup.frustumCulling);

    std::string error;
    if (!LoadScene(m_startup.scenePath, GetRenderer().Resources(), m_world, error))
    {
        throw std::runtime_error("could not load Player Scene '" +
                                 ToUtf8(m_startup.scenePath.wstring()) + "': " + error);
    }
    if (!GetActiveCameraView(m_world, m_camera))
    {
        throw std::runtime_error("Player Scene has no ActiveCamera: " +
                                 ToUtf8(m_startup.scenePath.wstring()));
    }

    ArenaConfig arenaConfig;
    if (m_startup.benchmark.enabled)
    {
        arenaConfig.initialEnemyCount = m_startup.benchmark.enemyCount;
        arenaConfig.maxEnemies        = m_startup.benchmark.enemyCount;
        GetRenderer().SetVSync(false);
    }
    const bool initializedArena =
        InitializeArenaIfPresent(m_world, GetRenderer().Resources(), arenaConfig);
    if (m_startup.benchmark.enabled && !initializedArena)
    {
        throw std::runtime_error("benchmark mode requires an Arena scene");
    }
    m_play.Begin();
}

uint64_t PlayerApp::MeasurementEnemyCount()
{
    return GetArenaStatus(m_world).currentEnemyCount;
}

std::wstring PlayerApp::MeasurementTitleStatus()
{
    return ArenaTitleStatus(m_world);
}

void PlayerApp::OnUpdate(float dt)
{
    const FrameContext frame = MakePlayerFrameContext(CaptureHostFrame(dt));
    m_play.BeginFrame(frame);
    RunPlaySystems(m_world, m_play, &GetRenderer().Resources());
    m_play.EndFrame();

    if (!GetActiveCameraView(m_world, m_camera))
    {
        throw std::runtime_error("Player lost its active camera");
    }
    if (frame.input.WasPressed('V'))
    {
        GetRenderer().SetVSync(!GetRenderer().IsVSync());
    }
    if (frame.input.WasPressed('M'))
    {
        GetRenderer().SetMsaaEnabled(!GetRenderer().IsMsaaEnabled());
    }
}

FrameContext PlayerApp::CaptureHostFrame(float dt)
{
    FrameContext frame;
    frame.deltaSeconds = dt;
    const UINT height = GetWindow().ClientHeight();
    frame.renderAspect = height == 0
        ? 1.0f
        : float(GetWindow().ClientWidth()) / float(height);

    GetWindow().ConsumeMouseDelta(frame.input.mouseDeltaX, frame.input.mouseDeltaY);
    for (int key = 0; key < InputContext::kKeyCount; ++key)
    {
        frame.input.keyDown[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        frame.input.keyPressed[key] = GetWindow().ConsumeKeyPress(key);
    }
    return frame;
}

void PlayerApp::OnRender()
{
    BuildRenderData(m_world, GetRenderer().Resources(), m_drawItems, m_lighting);
    try
    {
        GetRenderer().RenderFrame(m_camera, m_lighting, m_drawItems,
                                  Renderer::SceneOutput::SwapChain);
    }
    catch (const std::exception& exception)
    {
        if (m_startup.benchmark.enabled)
        {
            const Renderer::FrameStats frame = GetRenderer().LastFrameStats();
            std::ostringstream failure;
            failure << "benchmark_failure"
                    << " requested_enemies=" << m_startup.benchmark.enemyCount
                    << " current_enemies=" << MeasurementEnemyCount()
                    << " draw_items=" << m_drawItems.size()
                    << " object_capacity=" << GetRenderer().MaxDrawItems()
                    << " srv_used="
                    << GetRenderer().ShaderVisibleDescriptors().UsedCount()
                    << " srv_capacity="
                    << GetRenderer().ShaderVisibleDescriptors().Capacity()
                    << " error=\"" << exception.what() << '"';
            ProcessLog::Error(failure.str());
            WriteBenchmarkRow("failed", nullptr, frame, exception.what());
        }
        throw;
    }
}

void PlayerApp::OnFrameSampleSummary(const FrameSampleSummary& summary,
                                     const Renderer::FrameStats& frame)
{
    if (!m_startup.benchmark.enabled)
    {
        return;
    }
    if (!WriteBenchmarkRow("success", &summary, frame))
    {
        throw std::runtime_error("could not write benchmark TSV report");
    }
    PostQuitMessage(0);
}

bool PlayerApp::WriteBenchmarkRow(std::string_view status,
                                  const FrameSampleSummary* summary,
                                  const Renderer::FrameStats& frame,
                                  std::string_view error)
{
    const std::filesystem::path& path = m_startup.benchmark.outputPath;
    std::error_code filesystemError;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, filesystemError);
    }
    if (filesystemError)
    {
        ProcessLog::Error("benchmark_report_error path=\"" +
                          ToUtf8(path.wstring()) + "\" message=\"" +
                          filesystemError.message() + "\"");
        return false;
    }

    bool writeHeader = true;
    const bool pathExists = std::filesystem::exists(path, filesystemError);
    if (!filesystemError && pathExists)
    {
        writeHeader = std::filesystem::file_size(path, filesystemError) == 0;
    }
    if (filesystemError)
    {
        ProcessLog::Error("benchmark_report_error path=\"" +
                          ToUtf8(path.wstring()) + "\" message=\"" +
                          filesystemError.message() + "\"");
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::app);
    if (!stream)
    {
        ProcessLog::Error("benchmark_report_error path=\"" +
                          ToUtf8(path.wstring()) + "\" message=\"open failed\"");
        return false;
    }
    if (writeHeader)
    {
        stream << "status\tenemies\twarmup_frames\tsample_frames"
                  "\tmedian_ms\tp95_ms\tmax_ms\tvsync\tmsaa\tculling"
                  "\twidth\theight\tadapter\tdraw_calls\troot_cbv_binds"
                  "\tmain_visible\tshadow_visible\tmain_culled\tshadow_culled"
                  "\tunbounded\tdraw_items\tobject_capacity\tinstance_capacity"
                  "\tsrv_used\tsrv_capacity\terror\n";
    }

    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(3)
           << status << '\t'
           << m_startup.benchmark.enemyCount << '\t'
           << m_startup.benchmark.warmupFrames << '\t'
           << (summary ? summary->sampleCount : 0) << '\t'
           << (summary ? summary->medianMilliseconds : 0.0) << '\t'
           << (summary ? summary->p95Milliseconds : 0.0) << '\t'
           << (summary ? summary->maxMilliseconds : 0.0) << '\t'
           << (GetRenderer().IsVSync() ? "on" : "off") << '\t'
           << GetRenderer().MsaaSampleCount() << 'x' << '\t'
           << (GetRenderer().IsFrustumCullingEnabled() ? "on" : "off") << '\t'
           << frame.renderWidth << '\t' << frame.renderHeight << '\t'
           << TsvField(ToUtf8(GetRenderer().AdapterName())) << '\t'
           << frame.drawCalls << '\t' << frame.rootCbvBinds << '\t'
           << frame.mainVisible << '\t' << frame.shadowVisible << '\t'
           << frame.mainCulled << '\t' << frame.shadowCulled << '\t'
           << frame.unboundedItems << '\t'
           << m_drawItems.size() << '\t' << GetRenderer().MaxDrawItems() << '\t'
           << frame.instanceCapacity << '\t'
           << GetRenderer().ShaderVisibleDescriptors().UsedCount() << '\t'
           << GetRenderer().ShaderVisibleDescriptors().Capacity() << '\t'
           << TsvField(error) << '\n';
    stream.flush();
    if (!stream)
    {
        ProcessLog::Error("benchmark_report_error path=\"" +
                          ToUtf8(path.wstring()) + "\" message=\"write failed\"");
        return false;
    }
    return true;
}
