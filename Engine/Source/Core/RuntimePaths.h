// RuntimePaths.h : explicit roots for deployable runtime content.
#pragma once

#include <filesystem>

struct RuntimePaths
{
    std::filesystem::path root;
    std::filesystem::path assetDir;
    std::filesystem::path shaderDir;

    static RuntimePaths FromRoot(const std::filesystem::path& root);

    std::filesystem::path SceneDir() const { return assetDir / L"Scenes"; }
    std::filesystem::path SkyboxDir() const { return assetDir / L"Skyboxes"; }
};

// Returns the directory containing the current process executable. The
// implementation grows its buffer instead of silently truncating at MAX_PATH.
std::filesystem::path GetExecutableDir();

// Player always uses this policy: no source-tree probing or working-directory
// dependency is permitted.
RuntimePaths MakePlayerRuntimePaths();

// Editor normally uses the same deployable layout. A development build may
// explicitly permit an Assets-only source-tree fallback; compiled shaders
// still always come from beside the executable.
RuntimePaths MakeEditorRuntimePaths(bool allowRepositoryAssetFallback);
