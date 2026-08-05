// ProcessLog.h : one UTF-8 file log per Editor or Player process.
#pragma once

#include <filesystem>
#include <string_view>

namespace ProcessLog
{
    // Opens <exe>/Logs/<application>-<UTC timestamp>-<pid>.log. Tests may
    // replace the directory through DX12ENGINE_LOG_DIR. Returns false when
    // the directory or file cannot be created; logging then remains a no-op.
    bool Initialize(std::string_view application);
    void Shutdown();

    void Info(std::string_view message);
    void Error(std::string_view message);

    bool IsOpen();
    const std::filesystem::path& Path();
}
