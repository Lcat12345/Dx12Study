#include "Player/PlayerStartup.h"

#include <array>
#include <string_view>

#ifndef PLAYER_DEFAULT_SCENE
#define PLAYER_DEFAULT_SCENE L"Assets\\Scenes\\Demo.scene"
#endif

bool ParsePlayerStartup(int argc, const wchar_t* const* argv,
                        const RuntimePaths& runtimePaths,
                        PlayerStartup& outStartup, std::wstring& outError)
{
    constexpr std::wstring_view usage =
        L"Usage: Player.exe [--scene <path>] "
        L"[--benchmark <100|500|1000|2000>] [--benchmark-output <path>] "
        L"[--culling <on|off>]";
    std::filesystem::path requested = PLAYER_DEFAULT_SCENE;
    std::filesystem::path benchmarkOutput;
    bool                  sawScene           = false;
    bool                  sawBenchmark       = false;
    bool                  sawBenchmarkOutput = false;
    bool                  sawCulling         = false;
    bool                  frustumCulling     = true;
    uint32_t              benchmarkEnemies  = 0;

    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view option = argv[index] ? argv[index] : L"";
        if (option == L"--scene")
        {
            if (sawScene)
            {
                outError = L"--scene may be specified only once";
                return false;
            }
            if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
            {
                outError = L"--scene requires a path\n" + std::wstring(usage);
                return false;
            }

            requested = argv[index];
            sawScene  = true;
            continue;
        }

        if (option == L"--benchmark")
        {
            if (sawBenchmark)
            {
                outError = L"--benchmark may be specified only once";
                return false;
            }
            if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
            {
                outError = L"--benchmark requires an enemy count\n" +
                           std::wstring(usage);
                return false;
            }

            const std::wstring_view value = argv[index];
            const std::array<std::wstring_view, 4> supported = {
                L"100", L"500", L"1000", L"2000"
            };
            bool valid = false;
            for (const std::wstring_view candidate : supported)
            {
                if (value == candidate)
                {
                    valid = true;
                    break;
                }
            }
            if (!valid)
            {
                outError = L"--benchmark enemy count must be one of "
                           L"100, 500, 1000, or 2000";
                return false;
            }

            benchmarkEnemies = static_cast<uint32_t>(std::stoul(std::wstring(value)));
            sawBenchmark = true;
            continue;
        }

        if (option == L"--benchmark-output")
        {
            if (sawBenchmarkOutput)
            {
                outError = L"--benchmark-output may be specified only once";
                return false;
            }
            if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
            {
                outError = L"--benchmark-output requires a path\n" +
                           std::wstring(usage);
                return false;
            }
            benchmarkOutput = argv[index];
            sawBenchmarkOutput = true;
            continue;
        }

        if (option == L"--culling")
        {
            if (sawCulling)
            {
                outError = L"--culling may be specified only once";
                return false;
            }
            if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
            {
                outError = L"--culling requires on or off\n" + std::wstring(usage);
                return false;
            }

            const std::wstring_view value = argv[index];
            if (value != L"on" && value != L"off")
            {
                outError = L"--culling must be on or off";
                return false;
            }
            frustumCulling = value == L"on";
            sawCulling = true;
            continue;
        }

        outError = L"unknown Player option: " + std::wstring(option) +
                   L"\n" + std::wstring(usage);
        return false;
    }

    if (sawBenchmarkOutput && !sawBenchmark)
    {
        outError = L"--benchmark-output requires --benchmark";
        return false;
    }
    if (sawBenchmark && !sawScene)
    {
        requested = L"Assets\\Scenes\\Arena.scene";
    }

    outStartup.scenePath = (requested.is_absolute()
                                ? requested
                                : runtimePaths.root / requested).lexically_normal();
    outStartup.frustumCulling = frustumCulling;
    outStartup.benchmark = {};
    if (sawBenchmark)
    {
        outStartup.benchmark.enabled    = true;
        outStartup.benchmark.enemyCount = benchmarkEnemies;
        if (benchmarkOutput.empty())
        {
            benchmarkOutput = L"Logs\\Player-benchmark.tsv";
        }
        outStartup.benchmark.outputPath =
            (benchmarkOutput.is_absolute()
                 ? benchmarkOutput
                 : runtimePaths.root / benchmarkOutput).lexically_normal();
    }
    outError.clear();
    return true;
}
