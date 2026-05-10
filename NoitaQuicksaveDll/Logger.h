#pragma once

#include <filesystem>
#include <string>

namespace noitaqs
{
    void InitializeLogging(const std::filesystem::path& logPath);
    void ShutdownLogging();
    void Log(const std::wstring& message);
}
