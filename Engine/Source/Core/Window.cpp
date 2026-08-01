#include "Core/Window.h"

#include <stdexcept>

namespace
{
    constexpr wchar_t kClassName[] = L"Dx12EngineWndClass";
}

Window::Window(HINSTANCE instance, const wchar_t* title, UINT width, UINT height)
    : m_instance(instance)
    , m_clientWidth(width)
    , m_clientHeight(height)
{
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW; // repaint on resize
    wc.lpfnWndProc   = WndProcThunk;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;

    // Registering twice is harmless here (one window), but ignore the
    // "already registered" case so a second Window would still work.
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        throw std::runtime_error("RegisterClassEx failed");
    }

    // CreateWindow takes the OUTER size (title bar + borders included), but
    // we want the CLIENT area to be exactly the size we asked for.
    RECT rect = { 0, 0, LONG(width), LONG(height) };
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rect, style, FALSE);

    // Passing `this` as the creation parameter is how the static thunk finds
    // the instance for the very first messages (see WM_NCCREATE below).
    m_hwnd = CreateWindowExW(0, kClassName, title, style,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top,
                             nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        throw std::runtime_error("CreateWindowEx failed");
    }
}

Window::~Window()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void Window::Show(int cmdShow)
{
    ShowWindow(m_hwnd, cmdShow);
}

void Window::SetTitle(const wchar_t* title)
{
    SetWindowTextW(m_hwnd, title);
}

// A window procedure is a plain C callback, so it cannot be a member
// function. The standard bridge: stash `this` in the window's user data on
// the first message, then forward everything to the instance.
LRESULT CALLBACK Window::WndProcThunk(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* window = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->m_hwnd = hwnd; // CreateWindowEx has not returned yet
    }

    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window)
    {
        return window->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Window::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
    {
        m_minimized = (wParam == SIZE_MINIMIZED);
        const UINT width  = LOWORD(lParam);
        const UINT height = HIWORD(lParam);
        if (!m_minimized && width > 0 && height > 0)
        {
            m_clientWidth  = width;
            m_clientHeight = height;
            m_resizePending = true;
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
        m_lookActive = true;
        GetCursorPos(&m_lastMouse);
        SetCapture(m_hwnd); // keep receiving moves outside the window
        return 0;

    case WM_RBUTTONUP:
        m_lookActive = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (m_lookActive)
        {
            POINT current;
            GetCursorPos(&current);
            m_mouseDeltaX += float(current.x - m_lastMouse.x);
            m_mouseDeltaY += float(current.y - m_lastMouse.y);
            m_lastMouse = current;
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam < 256)
        {
            m_keyPressed[wParam] = true;
        }
        if (wParam == VK_ESCAPE)
        {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr; // already gone - do not DestroyWindow it again
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, message, wParam, lParam);
}

bool Window::ConsumeResizePending()
{
    const bool pending = m_resizePending;
    m_resizePending = false;
    return pending;
}

void Window::ConsumeMouseDelta(float& outX, float& outY)
{
    outX = m_mouseDeltaX;
    outY = m_mouseDeltaY;
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
}

bool Window::ConsumeKeyPress(int virtualKey)
{
    if (virtualKey < 0 || virtualKey >= 256)
    {
        return false;
    }
    const bool pressed = m_keyPressed[virtualKey];
    m_keyPressed[virtualKey] = false;
    return pressed;
}
