// Engine.h : owns the window, the renderer, the clock, and the loop.
#pragma once

#include "Core/Window.h"
#include "Core/Timer.h"
#include "Core/RuntimePaths.h"
#include "Core/FrameStatistics.h"
#include "Graphics/Renderer.h"

#include <memory>
#include <string>

// A game derives from Engine and fills in the three hooks. Engine never
// learns what a Scene or a Camera is - that stays on the game side, which
// is what keeps the renderer reusable for the Phase 10 editor.
//
// Inheritance rather than callbacks: there is one game, and a virtual
// override is easier to follow than a std::function handed in from outside.
class Engine
{
public:
    Engine(HINSTANCE instance, const wchar_t* title, UINT width, UINT height,
           const RuntimePaths& runtimePaths);
    virtual ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // Runs until the window closes. Returns the WM_QUIT exit code.
    int Run(int cmdShow);

protected:
    // Called once, after the renderer exists so meshes can be created.
    virtual void OnInit() {}
    // Called once per frame with the seconds elapsed since the last one.
    virtual void OnUpdate(float dt) {}
    // Called once per frame; the game issues its own Render call here.
    virtual void OnRender() {}
    virtual void OnFrameSampleSummary(const FrameSampleSummary&,
                                      const Renderer::FrameStats&) {}

    Window&   GetWindow()   { return m_window; }
    Renderer& GetRenderer() { return *m_renderer; }
    float     TotalSeconds() const { return m_timer.TotalSeconds(); }

    // Whole-second average, so the UI and the title bar agree.
    int       CurrentFps() const { return m_lastFps; }

    // Arena gameplay can override this once enemies exist; keeping the host
    // count separate preserves Engine's independence from game components.
    virtual uint64_t MeasurementEnemyCount() { return 0; }
    virtual std::wstring MeasurementTitleStatus() { return {}; }

    void ConfigureFrameSampling(size_t warmupFrames, size_t sampleFrames)
    {
        m_frameSamples = FrameSampleCollector(warmupFrames, sampleFrames);
    }

private:
    void UpdateTitleFps(float dt);
    void RecordFrameSample(float dt);

    std::wstring m_title;

    Window m_window;
    // Held by pointer so construction can be wrapped in try/catch and
    // reported before the loop starts.
    std::unique_ptr<Renderer> m_renderer;
    Timer  m_timer;

    // FPS is averaged over one second.
    float m_fpsAccumulator = 0.0f;
    int   m_fpsFrames      = 0;
    int   m_lastFps        = 0;
    FrameSampleCollector m_frameSamples{ 120, 600 };
};
