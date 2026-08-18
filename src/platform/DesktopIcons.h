// 桌面图标显隐（M6）：HKCU\...\Explorer\Advanced\HideIcons。
// 该设置官方仅在被 Explorer 重读时生效 —— 写注册表后平滑重启 Explorer
//（桌面闪一下属预期；与各类"隐藏桌面图标"工具的通行做法一致）。
#pragma once

namespace winfence {

bool IsHideDesktopIcons();
void SetHideDesktopIcons(bool hide);   // true=隐藏桌面图标

} // namespace winfence
