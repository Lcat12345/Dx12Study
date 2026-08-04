#include "Player/PlayerStartup.h"

#include <string_view>

#ifndef PLAYER_DEFAULT_SCENE
#define PLAYER_DEFAULT_SCENE L"Assets\\Scenes\\Demo.scene"
#endif

bool ParsePlayerStartup(int argc, const wchar_t* const* argv,
                        const RuntimePaths& runtimePaths,
                        PlayerStartup& outStartup, std::wstring& outError)
{
    std::filesystem::path requested = PLAYER_DEFAULT_SCENE;
    bool                  sawScene  = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view option = argv[index] ? argv[index] : L"";
        if (option != L"--scene")
        {
            outError = L"unknown Player option: " + std::wstring(option) +
                       L"\nUsage: Player.exe [--scene <path>]";
            return false;
        }
        if (sawScene)
        {
            outError = L"--scene may be specified only once";
            return false;
        }
        if (++index >= argc || !argv[index] || argv[index][0] == L'\0')
        {
            outError = L"--scene requires a path\nUsage: Player.exe [--scene <path>]";
            return false;
        }

        requested = argv[index];
        sawScene  = true;
    }

    outStartup.scenePath = (requested.is_absolute()
                                ? requested
                                : runtimePaths.root / requested).lexically_normal();
    outError.clear();
    return true;
}
