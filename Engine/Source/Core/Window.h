// Window.h : the Win32 window and the raw input state it collects.
#pragma once

#include <Windows.h>

// Owns one window and the input it observes. Knows nothing about D3D12 -
// the renderer only ever receives a HWND and a size from here.
class Window
{
public:
    Window(HINSTANCE instance, const wchar_t* title, UINT width, UINT height);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    void Show(int cmdShow);
    void SetTitle(const wchar_t* title);

    HWND Handle()       const { return m_hwnd; }
    UINT ClientWidth()  const { return m_clientWidth; }
    UINT ClientHeight() const { return m_clientHeight; }
    bool IsMinimized()  const { return m_minimized; }

    // True once per resize that still needs handling; clears the flag.
    // WM_SIZE arrives continuously while the user drags, so the loop
    // collapses them into a single resize per frame.
    bool ConsumeResizePending();

    // Mouse movement accumulated since the last call, in pixels. Reading
    // clears it, so a frame that skips this does not double-apply later.
    void ConsumeMouseDelta(float& outX, float& outY);

    // True if the key went down since the last call - an edge, not a state.
    // Held-key movement uses GetAsyncKeyState instead (see Camera).
    bool ConsumeKeyPress(int virtualKey);

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND      m_hwnd     = nullptr;
    HINSTANCE m_instance = nullptr;

    UINT m_clientWidth  = 0;
    UINT m_clientHeight = 0;

    bool m_minimized     = false;
    bool m_resizePending = false;

    // Mouse look is active only while the right button is held, so the
    // cursor stays usable for anything else.
    bool  m_lookActive  = false;
    POINT m_lastMouse   = {};
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;

    bool m_keyPressed[256] = {};
};
