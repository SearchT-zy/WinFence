// 图标提取实现（DESIGN.md §4.8；M6 质量升级：256px JUMBO 消除马赛克）。
#include "shell/IconExtractor.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <commoncontrols.h>

#ifndef SHIL_JUMBO
#define SHIL_JUMBO 0x4
#endif

namespace winfence {

HICON IconExtractor::Extract(const std::wstring& path, uint8_t& sysIconIndexOut)
{
    // 系统图标索引（同时用于缓存键）
    SHFILEINFOW sfi{};
    DWORD_PTR ok = SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                                  SHGFI_SYSICONINDEX);
    sysIconIndexOut = ok ? (uint8_t)(sfi.iIcon & 0xFF) : 0;

    // 首选：系统图像列表 256px JUMBO —— 缩到任何显示尺寸都清晰（M6 修"马赛克"）
    if (ok) {
        IImageList* list = nullptr;
        if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&list))) && list) {
            HICON big = nullptr;
            if (SUCCEEDED(list->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &big)) && big) {
                list->Release();
                return big;
            }
            list->Release();
        }
    }

    // 回退：32px（旧路径，死链/异常场景）
    sfi = {};
    ok = SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_LARGEICON);
    return (ok && sfi.hIcon) ? sfi.hIcon : nullptr;
}

} // namespace winfence
