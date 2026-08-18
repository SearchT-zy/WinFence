// 桌面扫描实现：Shell 枚举 → DesktopSnapshot（DESIGN.md §2 / §3.1）。
#include "core/DesktopScanner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>

#include "platform/WinUtil.h"
#include "shell/ShellEnumerator.h"

namespace winfence {

namespace {

// 文件 ID + 修改时间（仅属性，不读内容；§3.5 重命名配对依据）
// fileId 必须用 BY_HANDLE_FILE_INFORMATION（WIN32_FIND_DATA 没有文件索引字段）
bool QueryFileInfo(const std::wstring& path, uint64_t& fileId, uint64_t& fileTime,
                   DWORD& attributes)
{
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(path.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    ULARGE_INTEGER u{};
    u.LowPart = fd.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fd.ftLastWriteTime.dwHighDateTime;
    fileTime = u.QuadPart;
    attributes = fd.dwFileAttributes;

    fileId = 0;
    HANDLE fh = CreateFileW(path.c_str(), 0,   // 仅属性，不读内容
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (fh != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION bh{};
        if (GetFileInformationByHandle(fh, &bh)) {
            fileId = ((uint64_t)bh.nFileIndexHigh << 32) | (uint64_t)bh.nFileIndexLow;
        }
        CloseHandle(fh);
    }
    return true;
}

bool IsBlockedName(const std::wstring& nameLower)
{
    return nameLower == L"desktop.ini" || nameLower == L"thumbs.db" ||
           nameLower == L"ntuser.dat";
}

} // namespace

DesktopSnapshot DesktopScanner::Scan()
{
    DesktopSnapshot snap;
    for (const auto& item : ShellEnumerator().EnumerateDesktop()) {
        if (item.source == IconSource::Namespace) continue;   // 命名空间项 M3 处理
        if (item.absolutePath.empty()) continue;

        std::wstring lower = item.absolutePath;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        size_t pos = lower.find_last_of(L"\\/");
        std::wstring name = (pos == std::wstring::npos) ? lower : lower.substr(pos + 1);
        if (IsBlockedName(name)) continue;

        DesktopFileRecord rec;
        rec.normalizedPath = item.absolutePath;   // 保留原始大小写显示，比较用 EqualNoCase
        rec.displayName = item.displayName;
        rec.kind = item.kind;
        rec.source = item.source;
        if (!QueryFileInfo(item.absolutePath, rec.fileId, rec.fileTime, rec.attributes))
            continue;
        // 隐藏/系统项默认不入栏（§4.10）
        if (rec.attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        snap.push_back(std::move(rec));
    }
    return snap;
}

} // namespace winfence
