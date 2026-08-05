#include "Editor/PackageBuilder.h"

#include "Core/TextEncoding.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace
{
    std::filesystem::path FindEngineDir(const RuntimePaths& paths)
    {
        std::filesystem::path candidate = paths.root;
        for (int depth = 0; depth < 10; ++depth)
        {
            for (const std::filesystem::path& possible :
                 { candidate, candidate / L"Engine" })
            {
                if (std::filesystem::is_regular_file(possible / L"Player.vcxproj") &&
                    std::filesystem::is_regular_file(possible / L"BuildSettings.props") &&
                    std::filesystem::is_regular_file(
                        possible / L"Tools" / L"PackagePlayer.ps1"))
                {
                    return std::filesystem::absolute(possible).lexically_normal();
                }
            }
            if (candidate.parent_path() == candidate)
            {
                break;
            }
            candidate = candidate.parent_path();
        }
        return {};
    }

    std::wstring QuoteArgument(const std::filesystem::path& path)
    {
        // Windows paths cannot contain a quote. Keeping this helper path-only
        // makes CreateProcess quoting exact without pretending to be a full
        // command-line encoder for arbitrary user text.
        return L"\"" + path.wstring() + L"\"";
    }

    bool IsBelow(const std::filesystem::path& child,
                 const std::filesystem::path& parent)
    {
        std::error_code error;
        const std::wstring childText =
            std::filesystem::absolute(child, error).lexically_normal().wstring();
        if (error)
        {
            return false;
        }
        std::wstring parentText =
            std::filesystem::absolute(parent, error).lexically_normal().wstring();
        if (error)
        {
            return false;
        }
        if (!parentText.empty() && parentText.back() != L'\\')
        {
            parentText.push_back(L'\\');
        }
        return childText.size() > parentText.size() &&
               _wcsnicmp(childText.c_str(), parentText.c_str(), parentText.size()) == 0;
    }

    std::string LastLogLines(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return {};
        }
        std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        constexpr std::size_t kTailBytes = 3000;
        if (contents.size() > kTailBytes)
        {
            contents.erase(0, contents.size() - kTailBytes);
            const std::size_t firstLine = contents.find('\n');
            if (firstLine != std::string::npos)
            {
                contents.erase(0, firstLine + 1);
            }
        }
        while (!contents.empty() &&
               (contents.back() == '\r' || contents.back() == '\n'))
        {
            contents.pop_back();
        }
        return contents;
    }
}

PackageBuilder::PackageBuilder(const RuntimePaths& runtimePaths)
    : m_runtimePaths(runtimePaths)
    , m_engineDir(FindEngineDir(runtimePaths))
{
    if (m_engineDir.empty())
    {
        m_status = "Player packaging is unavailable: Player.vcxproj was not found";
        return;
    }

    m_outputRoot = m_engineDir.parent_path() / L"Output";
    m_defaultPackageDir =
        m_outputRoot / L"x64" / L"Release" / L"PlayerPackage";
    m_status = "Ready to package";
}

PackageBuilder::~PackageBuilder()
{
    // The build is a separate process and is intentionally allowed to finish
    // if the editor closes. Closing our handle does not terminate it.
    if (m_process)
    {
        CloseHandle(m_process);
    }
}

bool PackageBuilder::Start(const std::filesystem::path& scenePath,
                           const std::filesystem::path& packageDir)
{
    if (IsRunning())
    {
        m_status = "A Player package is already being built";
        return false;
    }
    if (!IsAvailable())
    {
        return false;
    }
    std::error_code pathError;
    if (!std::filesystem::is_regular_file(scenePath, pathError) || pathError ||
        !IsBelow(scenePath, m_runtimePaths.SceneDir()))
    {
        m_status = "Choose a .scene file under the editor's Assets/Scenes folder";
        return false;
    }

    const std::filesystem::path normalizedPackage =
        std::filesystem::absolute(packageDir, pathError).lexically_normal();
    if (pathError)
    {
        m_status = "The package folder path is invalid: " + pathError.message();
        return false;
    }
    if (!IsBelow(normalizedPackage, m_outputRoot))
    {
        m_status = "The package folder must be inside the repository Output folder";
        return false;
    }

    std::error_code directoryError;
    const std::filesystem::path logDir = m_outputRoot / L"PackageLogs";
    std::filesystem::create_directories(logDir, directoryError);
    if (directoryError)
    {
        m_status = "Could not create the package log folder: " +
                   directoryError.message();
        return false;
    }

    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_logPath = logDir / (L"PackagePlayer-" + std::to_wstring(stamp) + L".log");

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    const HANDLE log = CreateFileW(
        m_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
    {
        m_status = "Could not create the package log";
        return false;
    }

    const std::filesystem::path script =
        m_engineDir / L"Tools" / L"PackagePlayer.ps1";
    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
        QuoteArgument(script) + L" -EngineDir " + QuoteArgument(m_engineDir) +
        L" -AssetDir " + QuoteArgument(m_runtimePaths.assetDir) +
        L" -ScenePath " + QuoteArgument(scenePath) +
        L" -PackageDir " + QuoteArgument(normalizedPackage);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = log;
    startup.hStdError = log;

    PROCESS_INFORMATION process = {};
    const BOOL created = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
        m_engineDir.c_str(), &startup, &process);
    CloseHandle(log);
    if (!created)
    {
        m_status = "Could not start PowerShell for Player packaging (error " +
                   std::to_string(GetLastError()) + ")";
        return false;
    }

    CloseHandle(process.hThread);
    m_process = process.hProcess;
    m_packageDir = normalizedPackage;
    m_status = "Building Release x64 Player package...";
    return true;
}

void PackageBuilder::Poll()
{
    if (!m_process)
    {
        return;
    }

    DWORD exitCode = STILL_ACTIVE;
    if (!GetExitCodeProcess(m_process, &exitCode))
    {
        exitCode = GetLastError();
    }
    if (exitCode != STILL_ACTIVE)
    {
        Finish(exitCode);
    }
}

void PackageBuilder::Finish(DWORD exitCode)
{
    CloseHandle(m_process);
    m_process = nullptr;
    if (exitCode == 0)
    {
        m_status = "Package ready: " + ToUtf8(m_packageDir.wstring());
        return;
    }

    const std::string tail = LastLogLines(m_logPath);
    m_status = "Packaging failed (exit " + std::to_string(exitCode) + ")";
    if (!tail.empty())
    {
        m_status += ":\n" + tail;
    }
}
