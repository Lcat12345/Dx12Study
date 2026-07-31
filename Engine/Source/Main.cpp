// Main.cpp : window, input, timing, and the game loop.
// Nothing D3D12 lives here - see Renderer.cpp.
//
// Controls: WASD = move, Q/E = down/up, hold RIGHT MOUSE = look around,
//           Shift = move faster, Esc = quit.

#include "Renderer.h"
#include "Scene.h"
#include "Camera.h"

#include <Windows.h>
#include <objbase.h>
#include <stdexcept>
#include <cstdio>

namespace
{
    constexpr wchar_t kClassName[]   = L"Dx12EngineWndClass";
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine";

    UINT g_clientWidth  = 1280;
    UINT g_clientHeight = 720;
    HWND g_hWnd         = nullptr;

    // Mouse look is active only while the right button is held, so the
    // cursor stays usable for anything else.
    bool  g_lookActive  = false;
    POINT g_lastMouse   = {};
    float g_mouseDeltaX = 0.0f;
    float g_mouseDeltaY = 0.0f;

    bool g_resizePending = false;
    bool g_minimized     = false;

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
        {
            g_minimized = (wParam == SIZE_MINIMIZED);
            const UINT width  = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            if (!g_minimized && width > 0 && height > 0)
            {
                g_clientWidth  = width;
                g_clientHeight = height;
                // Don't resize here: WM_SIZE arrives continuously while the
                // user drags. Flag it and handle it once in the main loop.
                g_resizePending = true;
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
            g_lookActive = true;
            GetCursorPos(&g_lastMouse);
            SetCapture(hWnd); // keep receiving moves outside the window
            return 0;

        case WM_RBUTTONUP:
            g_lookActive = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE:
            if (g_lookActive)
            {
                POINT current;
                GetCursorPos(&current);
                g_mouseDeltaX += float(current.x - g_lastMouse.x);
                g_mouseDeltaY += float(current.y - g_lastMouse.y);
                g_lastMouse = current;
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    HWND CreateMainWindow(HINSTANCE hInstance)
    {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW; // repaint on resize
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc))
        {
            return nullptr;
        }

        // CreateWindow takes the OUTER size (title bar + borders included),
        // but we want the CLIENT area to be exactly the size we asked for.
        RECT windowRect = { 0, 0, LONG(g_clientWidth), LONG(g_clientHeight) };
        const DWORD style = WS_OVERLAPPEDWINDOW;
        AdjustWindowRect(&windowRect, style, FALSE);

        return CreateWindowExW(
            0, kClassName, kWindowTitle, style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr, nullptr, hInstance, nullptr);
    }

    // High-resolution timing. GetTickCount64 only has ~15 ms resolution -
    // useless when a frame takes 6 ms. QueryPerformanceCounter ticks
    // millions of times per second.
    class Timer
    {
    public:
        Timer()
        {
            QueryPerformanceFrequency(&m_frequency);
            QueryPerformanceCounter(&m_start);
            m_previous = m_start;
        }

        // Seconds since the previous call.
        float Tick()
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const float dt = float(double(now.QuadPart - m_previous.QuadPart) /
                                   double(m_frequency.QuadPart));
            m_previous = now;
            return dt;
        }

        float TotalSeconds() const
        {
            return float(double(m_previous.QuadPart - m_start.QuadPart) /
                         double(m_frequency.QuadPart));
        }

    private:
        LARGE_INTEGER m_frequency = {};
        LARGE_INTEGER m_start     = {};
        LARGE_INTEGER m_previous  = {};
    };

    // Averages over a whole second - a per-frame number would be unreadable.
    void UpdateTitleFps(float dt)
    {
        static float accumulated = 0.0f;
        static int   frames      = 0;

        accumulated += dt;
        ++frames;
        if (accumulated >= 1.0f)
        {
            // %ls, not %s: in the wide printf family %s means a NARROW
            // string under ISO rules.
            wchar_t title[128];
            std::swprintf(title, _countof(title),
                          L"%ls   %d fps   %.2f ms   %ux%u",
                          kWindowTitle, frames, 1000.0f * accumulated / float(frames),
                          g_clientWidth, g_clientHeight);
            SetWindowTextW(g_hWnd, title);
            accumulated = 0.0f;
            frames      = 0;
        }
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // WIC (the texture loader) is a COM library, so COM must be running.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return -1;
    }

    g_hWnd = CreateMainWindow(hInstance);
    if (!g_hWnd)
    {
        CoUninitialize();
        return -1;
    }

    Scene  scene;
    Camera camera;

    try
    {
        Renderer::Initialize(g_hWnd, g_clientWidth, g_clientHeight);
        BuildScene(Renderer::GetDevice(), scene);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Initialization failed",
                    MB_OK | MB_ICONERROR);
        CoUninitialize();
        return -1;
    }

    ShowWindow(g_hWnd, nCmdShow);

    Timer timer;

    // Game-style loop. PeekMessage returns immediately even when the queue
    // is empty, so the 'else' path runs every iteration - unlike GetMessage,
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

        if (g_minimized)
        {
            Sleep(16); // nothing to draw - don't spin the CPU
            continue;
        }

        if (g_resizePending)
        {
            g_resizePending = false;
            Renderer::Resize(g_clientWidth, g_clientHeight);
        }

        const float dt = timer.Tick();

        UpdateCamera(camera, g_mouseDeltaX, g_mouseDeltaY, dt);
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;

        Renderer::Render(scene, camera, timer.TotalSeconds());
        UpdateTitleFps(dt);
    }

    Renderer::Shutdown();
    CoUninitialize();

    // wParam of WM_QUIT carries the exit code passed to PostQuitMessage.
    return static_cast<int>(msg.wParam);
}
