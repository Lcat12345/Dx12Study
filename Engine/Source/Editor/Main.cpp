#include "Editor/EditorApp.h"
#include "Core/ProcessLog.h"
#include "Core/TextEncoding.h"

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

    ProcessLog::Initialize("Editor");
    ProcessLog::Info("application_start");

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult))
    {
        ProcessLog::Error("COM initialization failed");
        ProcessLog::Info("application_stop exit_code=-1");
        ProcessLog::Shutdown();
        return -1;
    }

    int exitCode = -1;
    try
    {
#if defined(EDITOR_REPOSITORY_ASSET_FALLBACK)
        constexpr bool kAllowRepositoryAssetFallback = true;
#else
        constexpr bool kAllowRepositoryAssetFallback = false;
#endif
        const RuntimePaths paths =
            MakeEditorRuntimePaths(kAllowRepositoryAssetFallback);
        const std::wstring pathLog = L"Dx12Engine Editor runtime root: " +
                                     paths.root.wstring() + L"\nAssets: " +
                                     paths.assetDir.wstring() + L"\nShaders: " +
                                     paths.shaderDir.wstring() + L"\n";
        OutputDebugStringW(pathLog.c_str());
        ProcessLog::Info("runtime_paths root=\"" +
                         ToUtf8(paths.root.wstring()) + "\"");

        EditorApp app(hInstance, paths);
        exitCode = app.Run(nCmdShow);
    }
    catch (const std::exception& error)
    {
        ProcessLog::Error(std::string("unhandled_exception message=\"") +
                          error.what() + "\"");
        MessageBoxA(nullptr, error.what(), "Dx12Engine Editor failed",
                    MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        ProcessLog::Error("unhandled_exception message=\"unknown exception\"");
        MessageBoxA(nullptr, "unknown exception", "Dx12Engine Editor failed",
                    MB_OK | MB_ICONERROR);
    }

    CoUninitialize();
    ProcessLog::Info("application_stop exit_code=" + std::to_string(exitCode));
    ProcessLog::Shutdown();
    return exitCode;
}
