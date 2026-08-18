// 交互控制：命中分区、标题栏拖动、双击折叠、右键菜单、内容滚动（DESIGN.md §3.3/§3.4）。
// 标题栏拖动用 WM_NCLBUTTONDOWN + HTCAPTION 走系统移动循环（免费获得 DPI/多屏正确性）。
// 双击折叠依赖窗口类 CS_DBLCLKS（§3.4）。
// MVP 不做 OLE 拖出到 Shell（§0 安全决策）；出栏 = 拖到其他栅栏 / 未分组区 / 右键移除。
#pragma once
#include "core/Model.h"

namespace winfence {

enum class HitZone { None, TitleBar, Content, Icon, ResizeBorder, RoundedCornerOut };

class Interaction {
public:
    HitZone HitTest(const Fence& fence, POINT ptPx) const;
    // 双击标题栏 → FenceService::ToggleCollapse
    // 右键菜单：从此栅栏移除 / 重命名 / 设置…
};

} // namespace winfence
