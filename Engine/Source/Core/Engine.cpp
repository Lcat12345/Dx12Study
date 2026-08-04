#include "Core/Engine.h"

#include <cstdio>

Engine::Engine(HINSTANCE instance, const wchar_t* title, UINT width, UINT height)
    : m_title(title)
    , m_window(instance, title, width, height)
{
    m_renderer = std::make_unique<Renderer>(m_window.Handle(),
                                            m_window.ClientWidth(),
                                            m_window.ClientHeight());

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
        UpdateTitleFps(dt);
    }

    // wParam of WM_QUIT carries the exit code passed to PostQuitMessage.
    return static_cast<int>(msg.wParam);
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
                  m_title, m_fpsFrames,
                  1000.0f * m_fpsAccumulator / float(m_fpsFrames),
                  m_window.ClientWidth(), m_window.ClientHeight(), sync,
                  stats.meshLoads, stats.meshRequests,
                  stats.textureLoads, stats.textureRequests,
                  stats.shaderCompiles, stats.shaderRequests);
#else
    std::swprintf(title, _countof(title),
                  L"%ls   %d fps   %.2f ms   %ux%u   [%ls]  V=toggle",
                  m_title, m_fpsFrames,
                  1000.0f * m_fpsAccumulator / float(m_fpsFrames),
                  m_window.ClientWidth(), m_window.ClientHeight(), sync);
#endif
    m_window.SetTitle(title);

    m_fpsAccumulator = 0.0f;
    m_fpsFrames      = 0;
}
