// D2D 绘制：连续毛玻璃容器 / 极简标题行 / 图标网格 / 悬停高亮 / 滚动条
// 视觉语言（M10 Apple 质感）：柔和投影 + 发丝描边 + 内高光 + 磨砂噪点，
// 面板四周预留 kShadowPadDip 投影空间（窗口比面板大，命中测试负责穿透）。
#pragma once
#include "core/Model.h"
#include "ui/IconCache.h"
#include "ui/Material.h"

#include <d2d1_1.h>
#include <dwrite.h>

namespace winfence {

// 标题栏「＋」新建按钮热区（DIP，从右缘起）：与 FenceWindow 命中测试共用
inline constexpr float kPlusZoneRightDip = 76.0f;
inline constexpr float kPlusZoneWidthDip = 22.0f;

class FenceRenderer {
public:
    // 在 Compositor 提供的设备上下文上绘制一个栅栏（ctx 已 SetDpi，坐标 DIP）
    // hoverUid：当前悬停的图标（高亮其单元格），0 = 无
    // dragUid：拖拽中的图标（源窗口中半透明），0 = 无；dropTarget：拖拽悬停本栏（描边高亮）
    // plusHover：鼠标悬停在标题栏「＋」按钮上（M9 视觉：按钮发光反馈）
    void Draw(const Fence& fence, const IconRegistry& icons, IconCache& cache,
              ID2D1DeviceContext* ctx, IDWriteFactory* dwrite, UINT dpi,
              bool acrylicActive, IconUid hoverUid = 0, IconUid dragUid = 0,
              bool dropTarget = false, bool plusHover = false);

    // 图标命中测试：ptPx 为窗口客户区物理像素，padPx 为面板在窗口内的
    // 投影留白（双层窗口方案下恒为 0）；与绘制同序（跳过 orphan/缺失项）
    static bool ItemAt(const Fence& fence, const IconRegistry& icons, UINT dpi,
                       LONG padPx, POINT ptPx, IconUid& outUid);

    // 内容区最大滚动量（物理像素，0 = 内容未溢出）
    static int MaxScrollPx(const Fence& fence, const IconRegistry& icons, UINT dpi);
};

} // namespace winfence
