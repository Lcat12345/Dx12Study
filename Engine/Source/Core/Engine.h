// Engine.h : owns the window, the renderer, the clock, and the loop.
#pragma once

#include "Core/Window.h"
#include "Core/Timer.h"
#include "Graphics/Renderer.h"

#include <memory>

// A game derives from Engine and fills in the three hooks. Engine never
// learns what a Scene or a Camera is - that stays on the game side, which
// is what keeps the renderer reusable for the Phase 10 editor.
//
// Inheritance rather than callbacks: there is one game, and a virtual
// override is easier to follow than a std::function handed in from outside.
class Engine
{
public:
    Engine(HINSTANCE instance, const wchar_t* title, UINT width, UINT height);
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

    Window&   GetWindow()   { return m_window; }
    Renderer& GetRenderer() { return *m_renderer; }
    float     TotalSeconds() const { return m_timer.TotalSeconds(); }

private:
    void UpdateTitleFps(float dt);

    const wchar_t* m_title = nullptr;

    Window m_window;
    // Held by pointer so construction can be wrapped in try/catch and
    // reported before the loop starts.
    std::unique_ptr<Renderer> m_renderer;
    Timer  m_timer;

    // Title-bar FPS is averaged over one second.
    float m_fpsAccumulator = 0.0f;
    int   m_fpsFrames      = 0;
};
