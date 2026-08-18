// 路径安全闸门：所有涉桌面文件路径操作的强制唯一入口（DESIGN.md §4.9，硬约束 4）。
// 校验链：
//   1) GetFileAttributesW 存在且可读
//   2) GetFinalPathNameByHandleW 解析符号链接后做前缀比较（防 junction 逃逸）
//      → 必须位于 用户桌面 / 公共桌面 的规范化路径之下；拒绝 ".."
//   3) 排除 desktop.ini / thumbs.db / 隐藏系统项
//   4) 任一项不通过 → 拒绝并给出原因（中文），绝不猜测
// 全程只读：本项目代码库不得出现对桌面项的 DeleteFileW/MoveFileW/RemoveDirectory
// （CI grep 禁词审查）；唯一写操作对象是自己的 config.json。
#pragma once
#include <string>

namespace winfence {

enum class PathVerdict { Ok, NotExist, OutsideDesktop, JunctionEscape, BlockedName, HiddenSystem };

class PathGuard {
public:
    static PathVerdict ValidateDesktopItem(const std::wstring& path);
    static bool        IsUnderDesktop(const std::wstring& normalizedPath);

    // 两个合法桌面根（带尾反斜杠；供监控启动等使用）
    static std::wstring UserDesktopDir();
    static std::wstring PublicDesktopDir();
};

} // namespace winfence
