#include "Player/PlayerStartup.h"

#include <array>
#include <string_view>

// Normally supplied by the build (DefaultPlayerScene in BuildSettings.props).
// This fallback only covers a host that compiles this file on its own - the
// test project does - so it has to name the same Scene the build does.
#ifndef PLAYER_DEFAULT_SCENE
#define PLAYER_DEFAULT_SCENE L"Assets\\Scenes\\Arena.scene"
#endif

bool ParsePlayerStartup(int argc, const wchar_t* const* argv,
                        const RuntimePaths& runtimePaths,
                        PlayerStartup& outStartup, std::wstring& outError)
{
    constexpr std::wstring_view usage =
        L"Usage: Player.exe [--scene <path>] [--enemies <count>] "
        L"[--player-health <points>] "
        L"[--benchmark <100|500|1000|2000>] [--benchmark-output <path>] "
        L"[--culling <on|off>] [--instancing <on|off>]";
    // Past this the object constant buffer alone would run to tens of
    // megabytes per frame set. Far beyond anything M1 asks for, and a typo in
    // a count should be an error rather than a minute of allocation.
    constexpr uint32_t    kMaxEnemies = 100000;
    // Contact damage is 8 a hit, so this is "effectively unkillable" without
    // becoming a number the title bar cannot show.
    constexpr uint32_t    kMaxPlayerHealth = 1000000;
    std::filesystem::path requested = PLAYER_DEFAULT_SCENE;
    std::filesystem::path benchmarkOutput;
    bool                  sawScene           = false;
    bool                  sawBenchmark       = false;
    bool                  sawBenchmarkOutput = false;
    bool                  sawCulling         = false;
    bool                  sawInstancing      = false;
    bool                  sawEnemies         = false;
    bool                  sawPlayerHealth    = false;
    bool                  frustumCulling     = true;
    bool                  instancing         = true;
    uint32_t              benchmarkEnemies  = 0;
    uint32_t              enemyCount        = 0;
    uint32_t              playerHealth      = 0;

    // The two optimization switches differ only in which flag they set, so
    // the shape of an on/off option is written once. Returns whether the
    // option was RECOGNIZED; a recognized option that failed to parse leaves
    // its reason in outError and raises this flag.
    // Same shape for the two count options. Parsed by hand rather than with
    // stoul because "12abc", "-4" and "0" must all be errors rather than a
    // silently truncated 12, an unsigned wrap, or a player that starts dead.
    bool countFailed = false;
    auto parseCountOption = [&](std::wstring_view option, std::wstring_view name,
                                int& index, uint32_t maximum, bool& sawOption,
                                uint32_t& outValue) {
        if (option != name)
        {
            return false;
        }
        if (sawOption)
        {
            outError = std::wstring(name) + L" may be specified only once";
            countFailed = true;
            return true;
        }
        if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
        {
            outError = std::wstring(name) + L" requires a count\n" +
                       std::wstring(usage);
            countFailed = true;
            return true;
        }

        const std::wstring_view value = argv[index];
        uint64_t parsed = 0;
        bool digitsOnly = !value.empty();
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                digitsOnly = false;
                break;
            }
            parsed = parsed * 10u + uint64_t(character - L'0');
            if (parsed > maximum)
            {
                break;
            }
        }
        if (!digitsOnly || parsed == 0 || parsed > maximum)
        {
            outError = std::wstring(name) + L" must be a count from 1 to " +
                       std::to_wstring(maximum);
            countFailed = true;
            return true;
        }

        outValue  = uint32_t(parsed);
        sawOption = true;
        return true;
    };

    bool switchFailed = false;
    auto parseOnOffSwitch = [&](std::wstring_view option, std::wstring_view name,
                                int& index, bool& sawOption, bool& outValue) {
        if (option != name)
        {
            return false;
        }
        if (sawOption)
        {
            outError = std::wstring(name) + L" may be specified only once";
            switchFailed = true;
            return true;
        }
        if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
        {
            outError = std::wstring(name) + L" requires on or off\n" +
                       std::wstring(usage);
            switchFailed = true;
            return true;
        }

        const std::wstring_view value = argv[index];
        if (value != L"on" && value != L"off")
        {
            outError = std::wstring(name) + L" must be on or off";
            switchFailed = true;
            return true;
        }
        outValue  = value == L"on";
        sawOption = true;
        return true;
    };

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

        if (parseCountOption(option, L"--enemies", index, kMaxEnemies,
                             sawEnemies, enemyCount) ||
            parseCountOption(option, L"--player-health", index, kMaxPlayerHealth,
                             sawPlayerHealth, playerHealth))
        {
            if (countFailed)
            {
                return false;
            }
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

        if (parseOnOffSwitch(option, L"--culling", index, sawCulling,
                             frustumCulling) ||
            parseOnOffSwitch(option, L"--instancing", index, sawInstancing,
                             instancing))
        {
            if (switchFailed)
            {
                return false;
            }
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
    // Both choose the arena's population, so accepting both would mean
    // deciding silently which one loses.
    if (sawEnemies && sawBenchmark)
    {
        outError = L"--enemies and --benchmark both set the enemy count; "
                   L"use one of them";
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
    outStartup.instancing     = instancing;
    outStartup.enemyCount     = enemyCount;
    outStartup.playerHealth   = playerHealth;
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
