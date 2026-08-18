// 桌面图标显隐实现（M6）。
#include "platform/DesktopIcons.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace winfence {

namespace {

constexpr wchar_t kAdvKey[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr wchar_t kHideVal[]  = L"HideIcons";

void RestartExplorer()
{
    // 结束并重新拉起 Explorer（重读 Advanced 设置）。任务栏会消失约 1 秒。
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, (LPWSTR)L"taskkill /f /im explorer.exe",
                       nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    Sleep(300);
    // Win10/11：Explorer 被杀后通常自愈；保险起见检测并手动拉起
    if (!FindWindowW(L"Shell_TrayWnd", nullptr)) {
        if (CreateProcessW(nullptr, (LPWSTR)L"explorer.exe",
                           nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

} // namespace

bool IsHideDesktopIcons()
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAdvKey, 0, KEY_QUERY_VALUE, &k)
        != ERROR_SUCCESS)
        return false;
    DWORD v = 0, size = sizeof(v), type = 0;
    const LSTATUS st = RegQueryValueExW(k, kHideVal, nullptr, &type,
                                        (BYTE*)&v, &size);
    RegCloseKey(k);
    return st == ERROR_SUCCESS && type == REG_DWORD && v == 1;
}

void SetHideDesktopIcons(bool hide)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAdvKey, 0, KEY_SET_VALUE, &k)
        != ERROR_SUCCESS)
        return;
    const bool changed = (IsHideDesktopIcons() != hide);
    const DWORD v = hide ? 1u : 0u;
    RegSetValueExW(k, kHideVal, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
    if (changed) RestartExplorer();   // 仅状态实际变化时才重启 Explorer
}

} // namespace winfence
