// PlayerStartup.h : command-line selection of the runtime's initial Scene.
#pragma once

#include "Core/RuntimePaths.h"

#include <filesystem>
#include <string>

struct PlayerStartup
{
    std::filesystem::path scenePath;
};

// argv includes the executable name. The only supported option is
//     --scene <path>
// Relative paths are rooted beside Player.exe, never at the process working
// directory. With no option the build's DefaultPlayerScene is selected.
bool ParsePlayerStartup(int argc, const wchar_t* const* argv,
                        const RuntimePaths& runtimePaths,
                        PlayerStartup& outStartup, std::wstring& outError);
