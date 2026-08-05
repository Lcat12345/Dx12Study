#include "Core/ProcessLog.h"

#include "Core/RuntimePaths.h"
#include "Core/TextEncoding.h"

#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    std::ofstream         g_stream;
    std::filesystem::path g_path;
    std::string           g_application;

    std::filesystem::path EnvironmentLogDirectory()
    {
        constexpr wchar_t variable[] = L"DX12ENGINE_LOG_DIR";
        const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
        if (required <= 1)
        {
            return {};
        }

        std::vector<wchar_t> buffer(required);
        if (GetEnvironmentVariableW(variable, buffer.data(), required) == 0)
        {
            return {};
        }
        return std::filesystem::path(buffer.data());
    }

    std::string Timestamp()
    {
        SYSTEMTIME utc = {};
        GetSystemTime(&utc);
        char text[32];
        std::snprintf(text, sizeof(text),
                      "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                      unsigned(utc.wYear), unsigned(utc.wMonth),
                      unsigned(utc.wDay), unsigned(utc.wHour),
                      unsigned(utc.wMinute), unsigned(utc.wSecond),
                      unsigned(utc.wMilliseconds));
        return text;
    }

    void Write(std::string_view level, std::string_view message)
    {
        if (!g_stream)
        {
            return;
        }
        g_stream << Timestamp() << " [" << level << "] [" << g_application
                 << "] " << message << '\n';
        // A crash must not strand the most useful line in an iostream buffer.
        g_stream.flush();
    }
}

bool ProcessLog::Initialize(std::string_view application)
{
    Shutdown();
    g_application.assign(application);

    std::filesystem::path directory = EnvironmentLogDirectory();
    if (directory.empty())
    {
        try
        {
            directory = GetExecutableDir() / L"Logs";
        }
        catch (...)
        {
            return false;
        }
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return false;
    }

    SYSTEMTIME utc = {};
    GetSystemTime(&utc);
    wchar_t filename[128];
    std::swprintf(filename, _countof(filename),
                  L"%hs-%04u%02u%02u-%02u%02u%02u-%03u-%lu.log",
                  g_application.c_str(), unsigned(utc.wYear),
                  unsigned(utc.wMonth), unsigned(utc.wDay),
                  unsigned(utc.wHour), unsigned(utc.wMinute),
                  unsigned(utc.wSecond), unsigned(utc.wMilliseconds),
                  GetCurrentProcessId());
    g_path = directory / filename;
    g_stream.open(g_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!g_stream)
    {
        g_path.clear();
        return false;
    }
    return true;
}

void ProcessLog::Shutdown()
{
    if (g_stream.is_open())
    {
        g_stream.flush();
        g_stream.close();
    }
    g_stream.clear();
    g_path.clear();
    g_application.clear();
}

void ProcessLog::Info(std::string_view message)
{
    Write("INFO", message);
}

void ProcessLog::Error(std::string_view message)
{
    Write("ERROR", message);
}

bool ProcessLog::IsOpen()
{
    return g_stream.is_open();
}

const std::filesystem::path& ProcessLog::Path()
{
    return g_path;
}
