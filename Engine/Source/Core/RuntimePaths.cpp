#include "Core/RuntimePaths.h"

#include <Windows.h>

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <vector>

RuntimePaths RuntimePaths::FromRoot(const std::filesystem::path& rootPath)
{
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(rootPath);
    return { absoluteRoot, absoluteRoot / L"Assets", absoluteRoot / L"Shaders" };
}

std::filesystem::path GetExecutableDir()
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(), "GetModuleFileNameW");
        }
        if (length < buffer.size() - 1)
        {
            return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
        }
        if (buffer.size() >= 32768)
        {
            throw std::runtime_error("Executable path exceeds the Windows path limit");
        }
        buffer.resize((std::min)(buffer.size() * 2, size_t(32768)));
    }
}

RuntimePaths MakePlayerRuntimePaths()
{
    return RuntimePaths::FromRoot(GetExecutableDir());
}

RuntimePaths MakeEditorRuntimePaths(bool allowRepositoryAssetFallback)
{
    RuntimePaths paths = RuntimePaths::FromRoot(GetExecutableDir());
    if (std::filesystem::is_directory(paths.assetDir) ||
        !allowRepositoryAssetFallback)
    {
        return paths;
    }

    std::filesystem::path candidate = paths.root;
    for (int depth = 0; depth < 8; ++depth)
    {
        if (std::filesystem::is_regular_file(candidate / L"Dx12Engine.slnx"))
        {
            const std::filesystem::path sourceAssets = candidate / L"Engine" / L"Assets";
            if (std::filesystem::is_directory(sourceAssets))
            {
                paths.assetDir = sourceAssets;
            }
            return paths;
        }
        if (candidate.parent_path() == candidate)
        {
            break;
        }
        candidate = candidate.parent_path();
    }
    return paths;
}
