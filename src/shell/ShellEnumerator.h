// Shell 命名空间枚举：PIDL → 路径 / 显示名（DESIGN.md §4.3 / §4.10）。
// 显示名用 SIGDN_PARENTRELATIVEEDITING（.lnk 不带扩展名），解析名用
// SIGDN_DESKTOPABSOLUTEEDITING；命名空间项（回收站等）无文件路径，以 PIDL 为身份。
// 坑：SHGetPathFromIDList 默认 MAX_PATH 缓冲 —— 传自备 WCHAR[MAX_PATH*2]。
// 参考 explorerplusplus（GPL-3，只读思路）：PIDL 生命周期 / ILFree 纪律。
#pragma once
#include "core/Model.h"
#include <string>
#include <vector>

namespace winfence {

class ShellEnumerator {
public:
    // 枚举桌面命名空间（含两个物理目录的合并视图 + 命名空间项），喂给 DesktopScanner。
    // 返回的每项含 displayName / absolutePath(可空) / pidl(序列化)。
    struct Item {
        std::wstring displayName;
        std::wstring absolutePath;
        std::vector<uint8_t> pidl;
        IconKind kind = IconKind::File;
        IconSource source = IconSource::UserDesktop;
    };
    std::vector<Item> EnumerateDesktop();
};

} // namespace winfence
