// 一次性教育提示实现（M7a）。
#include "ui/OrganizeHint.h"

#include "persist/ConfigStore.h"
#include "platform/DesktopIcons.h"

namespace winfence {

void MaybeShowVirtualGroupingHint(HWND owner, Workspace& ws, ConfigStore& store)
{
    if (ws.hintHideIconsDismissed || IsHideDesktopIcons()) return;

    const int ret = MessageBoxW(
        owner,
        L"图标已收入栅栏。\n\n"
        L"WinFence 采用「虚拟收纳」：桌面上的原文件保持原位，不会被移动或删除，"
        L"所以桌面上仍能看到原图标。\n\n"
        L"是否现在隐藏全部桌面图标，获得干净的桌面？"
        L"（可在 设置 中随时恢复；栅栏和 Dock 不受影响）",
        L"WinFence 提示", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND);

    if (ret == IDYES)
        SetHideDesktopIcons(true);   // 状态变化时内部会平滑重启 Explorer

    ws.hintHideIconsDismissed = true;   // 无论选什么都只问一次
    store.ScheduleSave();
}

} // namespace winfence
