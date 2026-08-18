// 图标提取：SHGetFileInfo（SHGFI_ICON）取 HICON（DESIGN.md §4.8）。
// M2：UI 线程同步提取（桌面项少、本地快）；M3：Worker 线程 + 超时占位。
// 注意：方法名不能用 ExtractIcon —— shellapi.h 有同名宏会改写声明。
#pragma once
#include <windows.h>
#include <string>

namespace winfence {

class IconExtractor {
public:
    // 成功返回 HICON（调用方 DestroyIcon），失败返回 nullptr。
    static HICON Extract(const std::wstring& path, uint8_t& sysIconIndexOut);
};

} // namespace winfence
