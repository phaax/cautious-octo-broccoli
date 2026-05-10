#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <filesystem>
#include <string>

namespace noitaqs
{
    std::wstring Widen(const char* value);
    std::string ToUtf8(const std::wstring& value);
    std::filesystem::path GetModuleDirectory(HMODULE module);
}
