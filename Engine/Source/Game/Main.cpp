// Main.cpp : entry point. Everything else lives behind Engine.
//
// Controls: WASD = move, Q/E = down/up, hold RIGHT MOUSE = look around,
//           Shift = move faster, V = toggle vsync, Esc = quit.

#include "Game/DemoGame.h"

#include <Windows.h>
#include <objbase.h>
#include <stdexcept>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // WIC (the texture loader) is a COM library, so COM must be running
    // before anything tries to decode an image.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return -1;
    }

    int exitCode = -1;
    try
    {
        DemoGame game(hInstance);
        exitCode = game.Run(nCmdShow);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Dx12Engine failed", MB_OK | MB_ICONERROR);
    }

    CoUninitialize();
    return exitCode;
}
