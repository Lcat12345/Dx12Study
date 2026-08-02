// TextEncoding.h : the one place wide and UTF-8 meet.
//
// Windows paths are UTF-16 (std::filesystem::path::value_type is wchar_t);
// ImGui, the scene file format and every error message we build are UTF-8.
//
// NEVER go through path::string(). It converts to the process's ANSI code
// page and THROWS std::system_error the moment a character does not fit -
// a Korean folder name on an English Windows, an emoji, anything. Use
// ToUtf8(path.wstring()), which is a real conversion that cannot fail.
//
// Deliberately its own header rather than part of Common.h: the loaders
// need string conversion without dragging in d3d12.h.
#pragma once

#include <string>

std::string  ToUtf8(const std::wstring& text);
std::wstring ToWide(const std::string& text);
