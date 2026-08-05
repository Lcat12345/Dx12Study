#include "Player/PlayerApp.h"
#include "Player/PlayerStartup.h"
#include "Core/ProcessLog.h"
#include "Core/TextEncoding.h"

#include <Windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    void WriteRuntimePathLogIfRequested(const std::wstring& pathLog)
    {
        constexpr wchar_t variable[] = L"DX12ENGINE_RUNTIME_PATH_LOG";
        const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
        if (required <= 1)
        {
            return;
        }

        std::vector<wchar_t> buffer(required);
        if (GetEnvironmentVariableW(variable, buffer.data(), required) == 0)
        {
            return;
        }

        std::ofstream stream(std::filesystem::path(buffer.data()),
                             std::ios::binary | std::ios::trunc);
        if (stream)
        {
            stream << ToUtf8(pathLog);
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

    ProcessLog::Initialize("Player");
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
    PlayerStartup startup;
    bool suppressErrorDialog = false;
    try
    {
        const RuntimePaths paths = MakePlayerRuntimePaths();
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (!arguments)
        {
            throw std::runtime_error("CommandLineToArgvW failed");
        }

        std::wstring startupError;
        const std::vector<const wchar_t*> argumentViews(arguments,
                                                        arguments + argumentCount);
        const bool parsed = ParsePlayerStartup(
            argumentCount, argumentViews.data(), paths, startup, startupError);
        LocalFree(arguments);
        if (!parsed)
        {
            exitCode = 2;
            ProcessLog::Error("startup_error message=\"" +
                              ToUtf8(startupError) + "\"");
            MessageBoxW(nullptr, startupError.c_str(), L"Dx12Engine Player arguments",
                        MB_OK | MB_ICONERROR);
        }
        else
        {
            suppressErrorDialog = startup.benchmark.enabled;
            const std::wstring pathLog = L"Dx12Engine Player runtime root: " +
                                         paths.root.wstring() + L"\nAssets: " +
                                         paths.assetDir.wstring() + L"\nShaders: " +
                                         paths.shaderDir.wstring() + L"\nScene: " +
                                         startup.scenePath.wstring() + L"\n";
            OutputDebugStringW(pathLog.c_str());
            WriteRuntimePathLogIfRequested(pathLog);
            ProcessLog::Info("runtime_paths root=\"" +
                             ToUtf8(paths.root.wstring()) + "\" scene=\"" +
                             ToUtf8(startup.scenePath.wstring()) + "\"");

            const std::wstring title = L"Dx12Engine Player - " +
                                       startup.scenePath.filename().wstring();
            PlayerApp app(hInstance, paths, std::move(startup), title.c_str());
            exitCode = app.Run(nCmdShow);
        }
    }
    catch (const std::exception& error)
    {
        ProcessLog::Error(std::string("unhandled_exception message=\"") +
                          error.what() + "\"");
        if (!suppressErrorDialog)
        {
            MessageBoxA(nullptr, error.what(), "Dx12Engine Player failed",
                        MB_OK | MB_ICONERROR);
        }
    }
    catch (...)
    {
        ProcessLog::Error("unhandled_exception message=\"unknown exception\"");
        if (!suppressErrorDialog)
        {
            MessageBoxA(nullptr, "unknown exception", "Dx12Engine Player failed",
                        MB_OK | MB_ICONERROR);
        }
    }

    CoUninitialize();
    ProcessLog::Info("application_stop exit_code=" + std::to_string(exitCode));
    ProcessLog::Shutdown();
    return exitCode;
}
