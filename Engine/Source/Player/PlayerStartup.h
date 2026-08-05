// PlayerStartup.h : command-line selection of the runtime's initial Scene.
#pragma once

#include "Core/RuntimePaths.h"

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <string>

struct PlayerBenchmark
{
    bool                  enabled          = false;
    uint32_t              enemyCount       = 0;
    size_t                warmupFrames     = 120;
    size_t                sampleFrames     = 600;
    std::filesystem::path outputPath;
};

struct PlayerStartup
{
    std::filesystem::path scenePath;
    PlayerBenchmark       benchmark;
    // Main/shadow frustum culling. Off submits every item to both passes,
    // which is what the renderer did before M1.6 - the point of the switch is
    // that one binary produces both rows of an on/off comparison.
    bool                  frustumCulling = true;
};

// argv includes the executable name. Supported options are
//     --scene <path>
//     --benchmark <100|500|1000|2000>
//     --benchmark-output <path>
//     --culling <on|off>
// Relative paths are rooted beside Player.exe, never at the process working
// directory. Benchmark mode selects Arena.scene unless --scene is explicit.
bool ParsePlayerStartup(int argc, const wchar_t* const* argv,
                        const RuntimePaths& runtimePaths,
                        PlayerStartup& outStartup, std::wstring& outError);
