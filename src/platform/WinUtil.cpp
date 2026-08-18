// 通用工具实现（DESIGN.md §4.3）。
#include "platform/WinUtil.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>

namespace winfence {

std::string ToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], n,
                        nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

int CompareNoCase(const std::wstring& a, const std::wstring& b)
{
    // CompareStringOrdinal 返回 CSTR_LESS_THAN(1)/CSTR_EQUAL(2)/CSTR_GREATER_THAN(3)
    return CompareStringOrdinal(a.c_str(), (int)a.size(),
                                b.c_str(), (int)b.size(), TRUE) - 2;
}

bool EqualNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    return CompareStringOrdinal(a.c_str(), (int)a.size(),
                                b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

std::wstring HresultToMessage(long hr)
{
    wchar_t buf[512]{};
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, (DWORD)hr, 0, buf, 512, nullptr);
    if (n == 0) swprintf_s(buf, L"错误码 0x%08X", (unsigned)hr);
    return buf;
}

} // namespace winfence
