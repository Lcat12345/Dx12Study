#include "Player/PlayerApp.h"
#include "Player/PlayerStartup.h"

#include <Windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <stdexcept>
#include <vector>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return -1;
    }

    int exitCode = -1;
    try
    {
        const RuntimePaths paths = MakePlayerRuntimePaths();
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (!arguments)
        {
            throw std::runtime_error("CommandLineToArgvW failed");
        }

        PlayerStartup startup;
        std::wstring startupError;
        const std::vector<const wchar_t*> argumentViews(arguments,
                                                        arguments + argumentCount);
        const bool parsed = ParsePlayerStartup(
            argumentCount, argumentViews.data(), paths, startup, startupError);
        LocalFree(arguments);
        if (!parsed)
        {
            MessageBoxW(nullptr, startupError.c_str(), L"Dx12Engine Player arguments",
                        MB_OK | MB_ICONERROR);
            CoUninitialize();
            return 2;
        }

        const std::wstring pathLog = L"Dx12Engine Player runtime root: " +
                                     paths.root.wstring() + L"\nAssets: " +
                                     paths.assetDir.wstring() + L"\nShaders: " +
                                     paths.shaderDir.wstring() + L"\nScene: " +
                                     startup.scenePath.wstring() + L"\n";
        OutputDebugStringW(pathLog.c_str());

        const std::wstring title = L"Dx12Engine Player - " +
                                   startup.scenePath.filename().wstring();
        PlayerApp app(hInstance, paths, startup.scenePath, title.c_str());
        exitCode = app.Run(nCmdShow);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "Dx12Engine Player failed",
                    MB_OK | MB_ICONERROR);
    }

    CoUninitialize();
    return exitCode;
}
