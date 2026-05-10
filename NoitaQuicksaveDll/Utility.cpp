#include "Utility.h"

namespace fs = std::filesystem;

namespace noitaqs
{
    std::wstring Widen(const char* value)
    {
        if (value == nullptr || *value == '\0')
            return L"";

        int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
        if (length <= 0)
            length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
        if (length <= 0)
            return L"(unprintable error)";

        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        UINT codePage = MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), length) > 0
            ? CP_UTF8
            : CP_ACP;

        if (codePage == CP_ACP)
            MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), length);

        return result;
    }

    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};

        int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return {};

        std::string result(static_cast<size_t>(length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
        return result;
    }

    fs::path GetModuleDirectory(HMODULE module)
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length == std::size(buffer))
            return fs::current_path();

        return fs::path(buffer).parent_path();
    }
}
