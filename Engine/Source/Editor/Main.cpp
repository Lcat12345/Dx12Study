#include "Editor/EditorApp.h"

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

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
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

        EditorApp app(hInstance, paths);
        exitCode = app.Run(nCmdShow);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "Dx12Engine Editor failed",
                    MB_OK | MB_ICONERROR);
    }

    CoUninitialize();
    return exitCode;
}
