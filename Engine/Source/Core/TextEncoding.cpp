#include "Core/TextEncoding.h"

#include <Windows.h>

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()),
                                         nullptr, 0);
    std::wstring result(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()),
                        result.data(), size);
    return result;
}
