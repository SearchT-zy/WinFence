// 路径安全闸门实现（DESIGN.md §4.9，硬约束 4）。
// 全程只读；唯一职责是判断"路径是否为可收纳的桌面项"。
#include "platform/PathGuard.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include <algorithm>

#include "platform/WinUtil.h"

namespace winfence {

namespace {

// 两个合法桌面根（规范小写、尾部带分隔符），首次调用时初始化
std::wstring g_userDesktop;
std::wstring g_publicDesktop;

void InitDesktopDirs()
{
    if (!g_userDesktop.empty()) return;
    const GUID guids[] = {FOLDERID_Desktop, FOLDERID_PublicDesktop};
    std::wstring* outs[] = {&g_userDesktop, &g_publicDesktop};
    for (int i = 0; i < 2; ++i) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(guids[i], KF_FLAG_DEFAULT, nullptr, &p)) && p) {
            std::wstring s = p;
            CoTaskMemFree(p);
            if (!s.empty() && s.back() != L'\\') s += L'\\';
            std::transform(s.begin(), s.end(), s.begin(), ::towlower);
            *outs[i] = s;
        }
    }
}

// 解析符号链接/junction 后的最终路径（防逃逸，§4.9 第 2 条）
bool GetFinalPath(const std::wstring& path, std::wstring& out)
{
    HANDLE h = CreateFileW(path.c_str(), 0,   // 0 = 仅属性，不读内容
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    wchar_t buf[1024];
    DWORD n = GetFinalPathNameByHandleW(h, buf, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= 1024) return false;
    out = buf;
    if (out.rfind(L"\\\\?\\", 0) == 0) out = out.substr(4);   // 去掉 \\?\ 前缀
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    std::transform(out.begin(), out.end(), out.begin(), ::towlower);
    return true;
}

bool IsBlockedName(const std::wstring& path)
{
    // 提取文件名（小写）
    size_t pos = path.find_last_of(L"\\/");
    std::wstring name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    return name == L"desktop.ini" || name == L"thumbs.db" || name == L"ntuser.dat";
}

} // namespace

bool PathGuard::IsUnderDesktop(const std::wstring& normalizedLowerPath)
{
    InitDesktopDirs();
    return (!g_userDesktop.empty() &&
            normalizedLowerPath.rfind(g_userDesktop, 0) == 0) ||
           (!g_publicDesktop.empty() &&
            normalizedLowerPath.rfind(g_publicDesktop, 0) == 0);
}

std::wstring PathGuard::UserDesktopDir()
{
    InitDesktopDirs();
    return g_userDesktop;
}

std::wstring PathGuard::PublicDesktopDir()
{
    InitDesktopDirs();
    return g_publicDesktop;
}

PathVerdict PathGuard::ValidateDesktopItem(const std::wstring& path)
{
    if (path.empty() || path.find(L"..") != std::wstring::npos)
        return PathVerdict::OutsideDesktop;

    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return PathVerdict::NotExist;
    if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
        return PathVerdict::HiddenSystem;
    if (IsBlockedName(path)) return PathVerdict::BlockedName;

    // 符号链接解析后必须仍位于桌面目录之下（防 junction 逃逸）
    std::wstring finalLower;
    if (!GetFinalPath(path, finalLower))
        return PathVerdict::NotExist;
    if (!IsUnderDesktop(finalLower))
        return PathVerdict::JunctionEscape;

    return PathVerdict::Ok;
}

} // namespace winfence
