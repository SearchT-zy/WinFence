// Shell 命名空间枚举实现（DESIGN.md §4.3 / §4.10）。
// IShellFolder::EnumObjects —— 与 Explorer 桌面视图一致的枚举。
#include "shell/ShellEnumerator.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shellapi.h>
#include <shlwapi.h>     // StrRetToBufW
#include <wrl/client.h>  // ComPtr

#include <algorithm>

#include "platform/WinUtil.h"

namespace winfence {

using Microsoft::WRL::ComPtr;

namespace {

std::vector<uint8_t> PidlToBytes(PCIDLIST_ABSOLUTE pidl)
{
    std::vector<uint8_t> v;
    if (!pidl) return v;
    UINT n = ILGetSize(pidl);
    v.resize(n);
    if (n) memcpy(v.data(), pidl, n);
    return v;
}

bool IsBelowLower(const std::wstring& dirLower, const std::wstring& path)
{
    if (dirLower.empty()) return false;
    if (path.size() <= dirLower.size()) return false;
    return CompareStringOrdinal(path.c_str(), (int)dirLower.size(),
                                dirLower.c_str(), (int)dirLower.size(),
                                TRUE) == CSTR_EQUAL;
}

} // namespace

std::vector<ShellEnumerator::Item> ShellEnumerator::EnumerateDesktop()
{
    std::vector<Item> items;

    // 两个物理桌面目录（小写、带尾分隔符）用于归类
    std::wstring userDir, publicDir;
    {
        const GUID guids[] = {FOLDERID_Desktop, FOLDERID_PublicDesktop};
        std::wstring* outs[] = {&userDir, &publicDir};
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

    ComPtr<IShellFolder> desktop;   // shlobj 提供 Microsoft::WRL::ComPtr
    if (FAILED(SHGetDesktopFolder(&desktop)) || !desktop) return items;

    ComPtr<IEnumIDList> enumerator;
    if (FAILED(desktop->EnumObjects(nullptr,
                                    SHCONTF_FOLDERS | SHCONTF_NONFOLDERS,
                                    &enumerator)) || !enumerator) {
        return items;
    }

    PITEMID_CHILD pidl = nullptr;
    while (enumerator->Next(1, &pidl, nullptr) == S_OK && pidl) {
        Item item;
        item.pidl = PidlToBytes(pidl);   // 桌面相对 PIDL

        STRRET str{};
        // 显示名：Explorer 风格（.lnk 不带扩展名）
        if (SUCCEEDED(desktop->GetDisplayNameOf(
                pidl, (SHGDNF)SIGDN_PARENTRELATIVEEDITING, &str))) {
            wchar_t buf[512];
            if (SUCCEEDED(StrRetToBufW(&str, pidl, buf, 512))) item.displayName = buf;
        }
        // 解析名：绝对文件系统路径（命名空间项为空）。
        // SIGDN 值可安全转换为 SHGDNF 传入（现代用法，头文件签名遗留不一致）
        if (SUCCEEDED(desktop->GetDisplayNameOf(
                pidl, (SHGDNF)SIGDN_DESKTOPABSOLUTEEDITING, &str))) {
            wchar_t buf[MAX_PATH * 2];   // §4.3：自备双倍缓冲
            if (SUCCEEDED(StrRetToBufW(&str, pidl, buf, MAX_PATH * 2)))
                item.absolutePath = buf;
        }

        if (!item.absolutePath.empty()) {
            if (IsBelowLower(userDir, item.absolutePath))
                item.source = IconSource::UserDesktop;
            else if (IsBelowLower(publicDir, item.absolutePath))
                item.source = IconSource::PublicDesktop;
            else
                item.source = IconSource::Namespace;   // 不在两个目录下的特殊项
        } else {
            item.source = IconSource::Namespace;
            item.kind = IconKind::VirtualNamespace;
        }

        if (item.kind != IconKind::VirtualNamespace) {
            std::wstring lower = item.absolutePath;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            const DWORD attr = GetFileAttributesW(item.absolutePath.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                item.kind = IconKind::Folder;
            else if (lower.size() >= 4 &&
                     (lower.compare(lower.size() - 4, 4, L".lnk") == 0 ||
                      lower.compare(lower.size() - 4, 4, L".url") == 0))
                item.kind = IconKind::Shortcut;
            else
                item.kind = IconKind::File;
        }

        items.push_back(std::move(item));
        CoTaskMemFree(pidl);
        pidl = nullptr;
    }
    return items;
}

} // namespace winfence
