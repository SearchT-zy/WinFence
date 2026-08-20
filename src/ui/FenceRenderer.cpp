// D2D 绘制实现（M10 v2 Apple 高级感：柔和投影 / 平滑渐变 / 发丝描边 + 内高光）。
#include "ui/FenceRenderer.h"

#include <d2d1helper.h>
#include <wrl/client.h>

#include <algorithm>

namespace winfence {

using Microsoft::WRL::ComPtr;

namespace {

// ---------- 网格几何（绘制/命中/最大滚动共用，杜绝算法漂移）----------
struct GridGeom {
    float padX = 10.0f, padY = 10.0f;
    float cellW = 72.0f, cellH = 80.0f;
    float iconSize = 44.0f;
    int   cols = 1;
};

GridGeom GridOf(float widthDip)
{
    GridGeom g;
    g.cols = std::max(1, (int)((widthDip - 2 * g.padX - 4.0f) / g.cellW));
    return g;
}

float TitleH(const Fence& f) { return f.style.titleBarHeightDip; }

float ScrollDip(const Fence& f, UINT dpi)
{
    return f.scrollOffset.y * 96.0f / (float)dpi;
}

// 按绘制顺序遍历可视单元格（滚动感知）；fn(cellRect, geom, meta)
template <typename F>
void ForEachDrawnCell(const Fence& f, const IconRegistry& icons,
                      float wDip, float hDip, float scrollDip, F&& fn)
{
    const GridGeom g = GridOf(wDip);
    const float titleH = TitleH(f);
    int drawn = 0;
    for (IconUid uid : f.items) {
        auto it = icons.find(uid);
        if (it == icons.end() || it->second.orphan) continue;
        const int col = drawn % g.cols;
        const int row = drawn / g.cols;
        D2D1_RECT_F cell{
            g.padX + col * g.cellW,
            titleH + g.padY + row * g.cellH - scrollDip,
            g.padX + (col + 1) * g.cellW,
            titleH + g.padY + (row + 1) * g.cellH - scrollDip};
        ++drawn;
        if (cell.bottom <= titleH) continue;            // 滚出可视区上方
        if (cell.top > hDip - g.padY) break;            // 滚出可视区下方
        fn(cell, g, it->second);
    }
}

size_t CountAlive(const Fence& f, const IconRegistry& icons)
{
    size_t n = 0;
    for (IconUid uid : f.items) {
        auto it = icons.find(uid);
        if (it != icons.end() && !it->second.orphan) ++n;
    }
    return n;
}

ComPtr<IDWriteTextFormat> MakeFormat(IDWriteFactory* dw, float size,
                                     DWRITE_FONT_WEIGHT weight, bool ellipsis,
                                     const wchar_t* family = L"Microsoft YaHei UI")
{
    ComPtr<IDWriteTextFormat> fmt;
    if (SUCCEEDED(dw->CreateTextFormat(
            family, nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"zh-CN", &fmt))) {
        if (ellipsis) {
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> sign;
            if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(fmt.Get(), &sign)))
                fmt->SetTrimming(&trim, sign.Get());
            else
                fmt->SetTrimming(&trim, nullptr);
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    return fmt;
}

} // namespace

void FenceRenderer::Draw(const Fence& f, const IconRegistry& icons, IconCache& cache,
                         ID2D1DeviceContext* ctx, IDWriteFactory* dwrite, UINT dpi,
                         bool acrylicActive, IconUid hoverUid, IconUid dragUid,
                         bool dropTarget, bool plusHover)
{
    if (!ctx || !dwrite || !dpi) return;

    const FenceStyle& st = f.style;
    const FLOAT kDipPerPx = 96.0f / (FLOAT)dpi;
    const FLOAT w = (FLOAT)(f.collapsed ? f.collapsedSizePx.cx : f.sizePx.cx) * kDipPerPx;
    const FLOAT h = (FLOAT)(f.collapsed ? f.collapsedSizePx.cy : f.sizePx.cy) * kDipPerPx;
    const FLOAT radius = st.cornerRadiusDip;
    const FLOAT titleH = TitleH(f);
    // M10 v3：面板窗口与面板同尺寸（投影由独立的阴影窗口渲染，无亚克力光晕），
    // 面板坐标系即窗口坐标系。

    ctx->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // ---- 富层次渐变底（上亮下暗，玻璃深度）----
    const FLOAT topA = acrylicActive ? 0.38f : st.opacity;
    const FLOAT botA = acrylicActive ? 0.56f : st.opacity * 0.96f;
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[4] = {
            {0.00f, D2D1::ColorF(0.20f, 0.235f, 0.325f, topA)},
            {0.28f, D2D1::ColorF(0.115f, 0.14f, 0.20f, topA * 0.97f)},
            {0.72f, D2D1::ColorF(0.07f, 0.085f, 0.13f, botA * 0.98f)},
            {1.00f, D2D1::ColorF(0.035f, 0.045f, 0.075f, botA)}};
        if (SUCCEEDED(ctx->CreateGradientStopCollection(gs, 4, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{D2D1::Point2F(0, 0),
                                                     D2D1::Point2F(0, h)};
            if (SUCCEEDED(ctx->CreateLinearGradientBrush(gp, stops.Get(), &bg))) {
                D2D1_ROUNDED_RECT rr =
                    D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius);
                ctx->FillRoundedRectangle(rr, bg.Get());
            }
        }
    }
    // 顶部内光晕（玻璃接光感，替代噪点）
    if (!f.collapsed)
        DrawTopGlow(ctx, D2D1::RectF(0, 0, w, h), radius, 0.9f);

    // ---- 玻璃高光：顶部一条柔光（模拟玻璃反射；折叠态不溢出）----
    {
        const float sheenBottom = std::min(titleH + 16.0f, h - 2.0f);
        if (sheenBottom > titleH + 2.0f) {
            ComPtr<ID2D1LinearGradientBrush> sheen;
            ComPtr<ID2D1GradientStopCollection> stops;
            D2D1_GRADIENT_STOP gs[3] = {
                {0.0f, D2D1::ColorF(1, 1, 1, 0.12f)},
                {0.55f, D2D1::ColorF(1, 1, 1, 0.04f)},
                {1.0f, D2D1::ColorF(1, 1, 1, 0.0f)}};
            if (SUCCEEDED(ctx->CreateGradientStopCollection(gs, 3, &stops))) {
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{
                    D2D1::Point2F(0, titleH), D2D1::Point2F(0, sheenBottom)};
                if (SUCCEEDED(ctx->CreateLinearGradientBrush(gp, stops.Get(), &sheen))) {
                    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                        D2D1::RectF(radius * 0.6f, titleH, w - radius * 0.6f,
                                    sheenBottom), 4, 4);
                    ctx->FillRoundedRectangle(rr, sheen.Get());
                }
            }
        }
    }

    // ---- 标题行：左侧 3px 身份色条 + 折叠 chevron + 小字标题 + 右侧计数 ----
    ComPtr<ID2D1SolidColorBrush> accent;
    ctx->CreateSolidColorBrush(
        D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, st.accent.a), &accent);
    ComPtr<ID2D1SolidColorBrush> title;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.92f), &title);
    ComPtr<ID2D1SolidColorBrush> dim;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.80f), &dim);
    ComPtr<ID2D1SolidColorBrush> faint;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.42f), &faint);

    if (accent) {   // 身份色条：极小，仅作点缀
        D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(
            D2D1::RectF(12, titleH / 2 - 7, 15, titleH / 2 + 7), 1.5f, 1.5f);
        ctx->FillRoundedRectangle(bar, accent.Get());
    }
    // 折叠 chevron：展开 ▾ / 折叠 ▸（双击标题切换的视觉提示，M9）
    if (faint) {
        const float cy = titleH / 2;
        const float cx = 21.0f;
        const float arm = 3.5f;
        if (f.collapsed) {   // 右三角
            ctx->DrawLine(D2D1::Point2F(cx - 1.5f, cy - arm), D2D1::Point2F(cx + 2.5f, cy),
                          faint.Get(), 1.4f);
            ctx->DrawLine(D2D1::Point2F(cx + 2.5f, cy), D2D1::Point2F(cx - 1.5f, cy + arm),
                          faint.Get(), 1.4f);
        } else {             // 下三角
            ctx->DrawLine(D2D1::Point2F(cx - arm, cy - 1.5f), D2D1::Point2F(cx, cy + 2.5f),
                          faint.Get(), 1.4f);
            ctx->DrawLine(D2D1::Point2F(cx + arm, cy - 1.5f), D2D1::Point2F(cx, cy + 2.5f),
                          faint.Get(), 1.4f);
        }
    }
    if (auto fmt = MakeFormat(dwrite, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false)) {
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (title) ctx->DrawTextW(f.title.c_str(), (UINT32)f.title.size(), fmt.Get(),
                                  D2D1::RectF(30, 0, w - 84, titleH), title.Get());
    }
    // 计数：等宽字体（科技感 HUD 数字）
    if (auto mono = MakeFormat(dwrite, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, false,
                               L"Consolas")) {
        mono->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        wchar_t badge[32];
        int n = swprintf_s(badge, L"%zu", CountAlive(f, icons));
        if (n > 0 && accent)
            ctx->DrawTextW(badge, (UINT32)n, mono.Get(),
                           D2D1::RectF(w - 48, 0, w - 14, titleH), accent.Get());
    }
    // 标题栏「＋」新建栅栏按钮（与 FenceWindow 热区共用常量；M9：悬停发光反馈）
    if (accent) {
        const float cx = w - kPlusZoneRightDip + kPlusZoneWidthDip / 2;
        const float cy = titleH / 2;
        const float arm = 4.5f;
        if (plusHover) {   // 悬停：外圈圆环 + 加粗加号
            D2D1_ELLIPSE ring{D2D1::Point2F(cx, cy), 8.5f, 8.5f};
            ctx->DrawEllipse(ring, accent.Get(), 1.4f);
            ctx->DrawLine(D2D1::Point2F(cx - arm, cy), D2D1::Point2F(cx + arm, cy),
                          accent.Get(), 2.2f);
            ctx->DrawLine(D2D1::Point2F(cx, cy - arm), D2D1::Point2F(cx, cy + arm),
                          accent.Get(), 2.2f);
        } else {
            ctx->DrawLine(D2D1::Point2F(cx - arm, cy), D2D1::Point2F(cx + arm, cy),
                          accent.Get(), 1.6f);
            ctx->DrawLine(D2D1::Point2F(cx, cy - arm), D2D1::Point2F(cx, cy + arm),
                          accent.Get(), 1.6f);
        }
    }
    if (!f.collapsed) {   // 标题下分隔线：accent 微光（M10 收敛浓度）
        ComPtr<ID2D1SolidColorBrush> sep;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(
                D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, 0.20f), &sep)))
            ctx->DrawLine(D2D1::Point2F(10, titleH), D2D1::Point2F(w - 10, titleH),
                          sep.Get(), 1.0f);
    }

    // ---- 图标网格：悬停发光高亮 + 图标轻微放大 + 文件名 ----
    if (!f.collapsed) {
        const float scrollDip = ScrollDip(f, dpi);
        auto labelFmt = MakeFormat(dwrite, 10.5f, DWRITE_FONT_WEIGHT_NORMAL, true);
        ComPtr<ID2D1SolidColorBrush> hoverFill, hoverGlow, hoverLn;
        ctx->CreateSolidColorBrush(
            D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, 0.12f), &hoverFill);
        ctx->CreateSolidColorBrush(
            D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, 0.20f), &hoverGlow);
        ctx->CreateSolidColorBrush(
            D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, 0.45f), &hoverLn);

        ctx->PushAxisAlignedClip(D2D1::RectF(0, titleH, w, h),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ForEachDrawnCell(f, icons, w, h, scrollDip,
            [&](const D2D1_RECT_F& cell, const GridGeom& g, const IconMeta& m) {
                const bool isHover = (m.uid == hoverUid);
                // 悬停：accent 色内缩块 + 外发光 + 细描边（M9）
                if (isHover) {
                    D2D1_ROUNDED_RECT hl = D2D1::RoundedRect(
                        D2D1::RectF(cell.left + 1, cell.top + 1,
                                    cell.right - 1, cell.bottom - 1), 7.0f, 7.0f);
                    if (hoverGlow) ctx->DrawRoundedRectangle(hl, hoverGlow.Get(), 4.0f);
                    if (hoverFill) ctx->FillRoundedRectangle(hl, hoverFill.Get());
                    if (hoverLn) ctx->DrawRoundedRectangle(hl, hoverLn.Get(), 1.0f);
                }
                const float cx = (cell.left + cell.right) / 2;
                const bool isDragging = (m.uid == dragUid);
                const FLOAT iconAlpha = isDragging ? 0.35f : 1.0f;   // 拖拽中半透明
                const float drawSize = isHover ? g.iconSize * 1.07f : g.iconSize;
                if (ID2D1Bitmap* bmp = cache.GetOrCreate(ctx, m.sourcePath, m.fileTime)) {
                    const D2D1_SIZE_F s = bmp->GetSize();
                    if (s.width > 0 && s.height > 0) {
                        const float sc = std::min(drawSize / s.width,
                                                  drawSize / s.height);
                        const float iw = s.width * sc, ih = s.height * sc;
                        D2D1_RECT_F dest{cx - iw / 2, cell.top + 5,
                                         cx + iw / 2, cell.top + 5 + ih};
                        ctx->DrawBitmap(bmp, &dest, iconAlpha,
                                        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                        nullptr, nullptr);
                    }
                } else {
                    if (dim) {   // 提取失败占位（§4.8）
                        D2D1_ELLIPSE e{D2D1::Point2F(cx, cell.top + 5 + g.iconSize / 2),
                                       g.iconSize / 2, g.iconSize / 2};
                        ctx->DrawEllipse(e, dim.Get());
                    }
                }
                if (labelFmt) {
                    ctx->DrawTextW(m.displayName.c_str(),
                                   (UINT32)m.displayName.size(), labelFmt.Get(),
                                   D2D1::RectF(cell.left, cell.top + 5 + g.iconSize + 3,
                                               cell.right, cell.bottom),
                                   isDragging ? faint.Get()
                                              : (isHover ? title.Get() : dim.Get()));
                }
            });
        ctx->PopAxisAlignedClip();

        // 滚动条：极淡底槽 + accent 色发光滑块（M9）
        const int maxPx = MaxScrollPx(f, icons, dpi);
        if (maxPx > 0) {
            const float trackTop = titleH + 4, trackBottom = h - 4;
            const float trackH = trackBottom - trackTop;
            const float viewDip = h - titleH;
            const float contentDip = viewDip + maxPx * kDipPerPx;
            const float thumbH = std::max(20.0f, trackH * viewDip / contentDip);
            const float thumbMax = trackH - thumbH;
            const float pos = (float)f.scrollOffset.y / maxPx;
            ComPtr<ID2D1SolidColorBrush> track, thumb;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(1, 1, 1, 0.05f), &track)) &&
                SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, 0.45f),
                    &thumb))) {
                D2D1_ROUNDED_RECT tr = D2D1::RoundedRect(
                    D2D1::RectF(w - 5, trackTop, w - 2, trackBottom), 1.5f, 1.5f);
                ctx->FillRoundedRectangle(tr, track.Get());
                D2D1_ROUNDED_RECT tb = D2D1::RoundedRect(
                    D2D1::RectF(w - 5, trackTop + thumbMax * pos,
                                w - 2, trackTop + thumbMax * pos + thumbH),
                    1.5f, 1.5f);
                ctx->FillRoundedRectangle(tb, thumb.Get());
            }
        }
    }

    // ---- Apple 式描边（发丝白边 + 顶部内高光 + 底部内阴影；拖放目标时 accent 高亮）----
    DrawPanelBorder(ctx, D2D1::RectF(0, 0, w, h), radius,
                    st.accent.r, st.accent.g, st.accent.b, dropTarget);
}

bool FenceRenderer::ItemAt(const Fence& f, const IconRegistry& icons, UINT dpi,
                           LONG padPx, POINT ptPx, IconUid& outUid)
{
    if (f.collapsed || !dpi) return false;
    const FLOAT kDipPerPx = 96.0f / (FLOAT)dpi;
    const FLOAT w = (FLOAT)f.sizePx.cx * kDipPerPx;
    const FLOAT h = (FLOAT)f.sizePx.cy * kDipPerPx;
    const FLOAT x = (FLOAT)(ptPx.x - padPx) * kDipPerPx;
    const FLOAT y = (FLOAT)(ptPx.y - padPx) * kDipPerPx + ScrollDip(f, dpi);

    bool hit = false;
    ForEachDrawnCell(f, icons, w, h, 0,
        [&](const D2D1_RECT_F& cell, const GridGeom&, const IconMeta& m) {
            if (!hit && x >= cell.left && x < cell.right && y >= cell.top && y < cell.bottom) {
                outUid = m.uid;
                hit = true;
            }
        });
    return hit;
}

int FenceRenderer::MaxScrollPx(const Fence& f, const IconRegistry& icons, UINT dpi)
{
    if (f.collapsed || !dpi) return 0;
    const FLOAT kDipPerPx = 96.0f / (FLOAT)dpi;
    const FLOAT w = (FLOAT)f.sizePx.cx * kDipPerPx;
    const FLOAT h = (FLOAT)f.sizePx.cy * kDipPerPx;
    const GridGeom g = GridOf(w);
    const size_t alive = CountAlive(f, icons);
    if (alive == 0) return 0;
    const float rows = (float)((alive + g.cols - 1) / g.cols);
    const float contentDip = g.padY * 2 + rows * g.cellH;
    const float viewDip = h - TitleH(f);
    const float maxDip = contentDip - viewDip;
    return maxDip > 0 ? (int)(maxDip * dpi / 96.0f + 0.5f) : 0;
}

} // namespace winfence
