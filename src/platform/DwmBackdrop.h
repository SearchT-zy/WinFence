// DWM 背景效果：亚克力探测与应用（DESIGN.md §0 / §4.5 —— 风险点②）。
// 失败链：DWMWA_SYSTEMBACKDROP_TYPE=DWMSBT_TRANSIENTWINDOW（Win11 22H2 22621+）
//         → 回退半透明纯色 + 噪点纹理。
// 未文档化 SetWindowCompositionAttribute(ACCENT_ENABLE_ACRYLICBLURBEHIND) 仅在
// 编译开关 WINFENCE_ENABLE_UNDOCUMENTED_ACCENT 后可用（默认 OFF，AV 误报风险）。
// 对照：MicaForEveryone（MIT，效果矩阵可参考）、TranslucentTB（GPL-3，只读思路）。
#pragma once
#include <windows.h>

namespace winfence {

enum class BackdropSupport { SystemBackdrop, Fallback, UndocumentedAccent };

class DwmBackdrop {
public:
    static BackdropSupport Detect();                 // 启动时探测 build 号
    static bool Apply(HWND hwnd, BackdropSupport s);
};

} // namespace winfence
