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
    // The two M1 optimizations, switchable independently. Off reproduces the
    // renderer as it was before each was added - culling off submits every
    // item to both passes (pre-M1.6), instancing off draws one item at a time
    // with its own b0 and material binds (pre-M1.5). The point of the pair is
    // that one binary produces all four rows of the same comparison.
    bool                  frustumCulling = true;
    bool                  instancing     = true;
    // Starting and maximum enemy count for an Arena Scene. 0 leaves the
    // arena's own defaults alone, which is what a shipped Player does.
    //
    // Separate from the benchmark's count because the two runs want opposite
    // things: the benchmark measures 600 frames and exits, while the M1
    // completion criterion is a person surviving five minutes among a
    // thousand of them. Without this the second is not reachable - the
    // arena's default ceiling is 100.
    uint32_t              enemyCount     = 0;
    // Starting and maximum player health, 0 meaning the arena's own default.
    // Raising it is what makes the loop observable at high enemy counts - at
    // 1000 enemies the default 100 is gone inside one second, which is faster
    // than the arena logs a single summary.
    uint32_t              playerHealth   = 0;
};

// argv includes the executable name. Supported options are
//     --scene <path>
//     --enemies <count>
//     --player-health <points>
//     --benchmark <100|500|1000|2000>
//     --benchmark-output <path>
//     --culling <on|off>
//     --instancing <on|off>
// Relative paths are rooted beside Player.exe, never at the process working
// directory. Benchmark mode selects Arena.scene unless --scene is explicit.
bool ParsePlayerStartup(int argc, const wchar_t* const* argv,
                        const RuntimePaths& runtimePaths,
                        PlayerStartup& outStartup, std::wstring& outError);
