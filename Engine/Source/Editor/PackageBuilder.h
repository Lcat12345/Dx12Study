// PackageBuilder.h : asynchronous bridge from the Editor to the Player build.
#pragma once

#include "Core/RuntimePaths.h"

#include <Windows.h>

#include <filesystem>
#include <string>

class PackageBuilder
{
public:
    explicit PackageBuilder(const RuntimePaths& runtimePaths);
    ~PackageBuilder();

    PackageBuilder(const PackageBuilder&) = delete;
    PackageBuilder& operator=(const PackageBuilder&) = delete;

    bool IsAvailable() const { return !m_engineDir.empty(); }
    bool IsRunning() const { return m_process != nullptr; }

    const std::filesystem::path& EngineDir() const { return m_engineDir; }
    const std::filesystem::path& DefaultPackageDir() const
    {
        return m_defaultPackageDir;
    }
    const std::filesystem::path& PackageDir() const { return m_packageDir; }
    const std::filesystem::path& LogPath() const { return m_logPath; }
    const std::string& Status() const { return m_status; }

    // Starts a Release x64 Player build without blocking the editor frame.
    // scenePath must live below the runtime Assets/Scenes directory and the
    // package directory must live below the repository Output directory.
    bool Start(const std::filesystem::path& scenePath,
               const std::filesystem::path& packageDir);

    // Cheap, non-blocking process check. Call once per editor frame.
    void Poll();

private:
    void Finish(DWORD exitCode);

    RuntimePaths          m_runtimePaths;
    std::filesystem::path m_engineDir;
    std::filesystem::path m_outputRoot;
    std::filesystem::path m_defaultPackageDir;
    std::filesystem::path m_packageDir;
    std::filesystem::path m_logPath;
    std::string           m_status;
    HANDLE                m_process = nullptr;
};
