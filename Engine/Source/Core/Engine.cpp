#include "Core/Engine.h"
#include "Core/ProcessLog.h"
#include "Core/TextEncoding.h"

#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>

Engine::Engine(HINSTANCE instance, const wchar_t* title, UINT width, UINT height,
               const RuntimePaths& runtimePaths)
    : m_title(title)
    , m_window(instance, title, width, height)
{
    m_renderer = std::make_unique<Renderer>(m_window.Handle(),
                                            m_window.ClientWidth(),
                                            m_window.ClientHeight(),
                                            runtimePaths);

}

Engine::~Engine() = default;

int Engine::Run(int cmdShow)
{
    OnInit();
    m_window.Show(cmdShow);

    // Game-style loop. PeekMessage returns immediately even when the queue
    // is empty, so the work below runs every iteration - unlike GetMessage,
    // which would block until the user does something.
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        if (m_window.IsMinimized())
        {
            Sleep(16); // nothing to draw - don't spin the CPU
            continue;
        }

        // WM_SIZE fires continuously while dragging, so it only sets a flag
        // and the resize happens once here.
        if (m_window.ConsumeResizePending())
        {
            m_renderer->Resize(m_window.ClientWidth(), m_window.ClientHeight());
        }

        const float dt = m_timer.Tick();

        OnUpdate(dt);
        OnRender();
        RecordFrameSample(dt);
        UpdateTitleFps(dt);
    }

    // wParam of WM_QUIT carries the exit code passed to PostQuitMessage.
    return static_cast<int>(msg.wParam);
}

void Engine::RecordFrameSample(float dt)
{
    const std::optional<FrameSampleSummary> summary =
        m_frameSamples.AddSample(double(dt) * 1000.0);
    if (!summary)
    {
        return;
    }

    const Renderer::FrameStats& frame = m_renderer->LastFrameStats();
    std::ostringstream row;
    row.imbue(std::locale::classic());
    row << std::fixed << std::setprecision(3)
        << "frame_summary"
        << " warmup=" << m_frameSamples.WarmupFrames()
        << " samples=" << summary->sampleCount
        << " median_ms=" << summary->medianMilliseconds
        << " p95_ms=" << summary->p95Milliseconds
        << " max_ms=" << summary->maxMilliseconds
        << " vsync=" << (m_renderer->IsVSync() ? "on" : "off")
        << " msaa=" << m_renderer->MsaaSampleCount() << 'x'
        << " width=" << frame.renderWidth
        << " height=" << frame.renderHeight
        << " adapter=\"" << ToUtf8(m_renderer->AdapterName()) << '"'
        << " enemies=" << MeasurementEnemyCount()
        << " draws=" << frame.drawCalls
        << " root_cbv_binds=" << frame.rootCbvBinds
        << " main_visible=" << frame.mainVisible
        << " shadow_visible=" << frame.shadowVisible
        << " object_capacity=" << frame.objectCapacity
        << " full_gpu_waits=" << frame.fullGpuWaits
        << " srv_used=" << frame.srvUsed
        << " srv_capacity=" << frame.srvCapacity;
    ProcessLog::Info(row.str());
    OnFrameSampleSummary(*summary, frame);
}

// Averages over a whole second - a per-frame number would be unreadable.
void Engine::UpdateTitleFps(float dt)
{
    m_fpsAccumulator += dt;
    ++m_fpsFrames;
    if (m_fpsAccumulator < 1.0f)
    {
        return;
    }
    m_lastFps = m_fpsFrames;

    const wchar_t* sync = m_renderer->IsVSync()
                              ? L"vsync"
                              : (m_renderer->IsTearingSupported() ? L"uncapped"
                                                                  : L"uncapped(no tearing)");
    // %ls, not %s: in the wide printf family %s means a NARROW string under
    // ISO rules.
    wchar_t title[220];
#if defined(_DEBUG)
    // Loads vs requests: the gap is the resource cache doing its job.
    const ResourceManager::Stats& stats = m_renderer->Resources().GetStats();
    std::swprintf(title, _countof(title),
                  L"%ls   %d fps   %.2f ms   %ux%u   [%ls]  V=toggle"
                  L"   | mesh %u/%u  tex %u/%u  shader %u/%u (loads/requests)",
                  m_title.c_str(), m_fpsFrames,
                  1000.0f * m_fpsAccumulator / float(m_fpsFrames),
                  m_window.ClientWidth(), m_window.ClientHeight(), sync,
                  stats.meshLoads, stats.meshRequests,
                  stats.textureLoads, stats.textureRequests,
                  stats.shaderLoads, stats.shaderRequests);
#else
    std::swprintf(title, _countof(title),
                  L"%ls   %d fps   %.2f ms   %ux%u   [%ls]  V=toggle",
                  m_title.c_str(), m_fpsFrames,
                  1000.0f * m_fpsAccumulator / float(m_fpsFrames),
                  m_window.ClientWidth(), m_window.ClientHeight(), sync);
#endif
    const std::wstring fullTitle = std::wstring(title) + MeasurementTitleStatus();
    m_window.SetTitle(fullTitle.c_str());

    m_fpsAccumulator = 0.0f;
    m_fpsFrames      = 0;
}
