// D2D 绘制实现（M4 视觉重绘：连续毛玻璃 + 极简标题行 + 悬停高亮）。
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
                                     DWRITE_FONT_WEIGHT weight, bool ellipsis)
{
    ComPtr<IDWriteTextFormat> fmt;
    if (SUCCEEDED(dw->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, weight,
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
                         bool dropTarget)
{
    if (!ctx || !dwrite || !dpi) return;

    const FenceStyle& st = f.style;
    const FLOAT kDipPerPx = 96.0f / (FLOAT)dpi;
    const FLOAT w = (FLOAT)(f.collapsed ? f.collapsedSizePx.cx : f.sizePx.cx) * kDipPerPx;
    const FLOAT h = (FLOAT)(f.collapsed ? f.collapsedSizePx.cy : f.sizePx.cy) * kDipPerPx;
    const FLOAT radius = st.cornerRadiusDip;
    const FLOAT titleH = TitleH(f);

    ctx->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // ---- 整块连续毛玻璃（无标题色带，视觉一体化）----
    // 微蓝深灰调；亚克力模式下低 alpha 让系统模糊主导质感
    const FLOAT baseAlpha = acrylicActive ? 0.42f : st.opacity;
    ComPtr<ID2D1SolidColorBrush> bg;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(
            D2D1::ColorF(0.10f, 0.11f, 0.15f, baseAlpha), &bg))) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius);
        ctx->FillRoundedRectangle(rr, bg.Get());
    }

    // ---- 标题行：左侧 3px 身份色条 + 小字标题 + 右侧计数 ----
    ComPtr<ID2D1SolidColorBrush> accent;
    ctx->CreateSolidColorBrush(
        D2D1::ColorF(st.accent.r, st.accent.g, st.accent.b, st.accent.a), &accent);
    ComPtr<ID2D1SolidColorBrush> title;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.92f), &title);
    ComPtr<ID2D1SolidColorBrush> dim;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.80f), &dim);
    ComPtr<ID2D1SolidColorBrush> faint;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.42f), &faint);
    ComPtr<ID2D1SolidColorBrush> hairline;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), &hairline);

    if (accent) {   // 身份色条：极小，仅作点缀
        D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(
            D2D1::RectF(12, titleH / 2 - 7, 15, titleH / 2 + 7), 1.5f, 1.5f);
        ctx->FillRoundedRectangle(bar, accent.Get());
    }
    if (auto fmt = MakeFormat(dwrite, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false)) {
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (title) ctx->DrawTextW(f.title.c_str(), (UINT32)f.title.size(), fmt.Get(),
                                  D2D1::RectF(22, 0, w - 84, titleH), title.Get());
        wchar_t badge[32];
        int n = swprintf_s(badge, L"%zu", CountAlive(f, icons));
        if (n > 0 && faint)
            ctx->DrawTextW(badge, (UINT32)n, fmt.Get(),
                           D2D1::RectF(w - 48, 0, w - 14, titleH), faint.Get());
    }
    // 标题栏「＋」新建栅栏按钮（与 FenceWindow 热区共用常量）
    if (accent) {
        const float cx = w - kPlusZoneRightDip + kPlusZoneWidthDip / 2;
        const float cy = titleH / 2;
        const float arm = 4.5f;
        ctx->DrawLine(D2D1::Point2F(cx - arm, cy), D2D1::Point2F(cx + arm, cy),
                      accent.Get(), 1.6f);
        ctx->DrawLine(D2D1::Point2F(cx, cy - arm), D2D1::Point2F(cx, cy + arm),
                      accent.Get(), 1.6f);
    }
    if (!f.collapsed && hairline) {   // 标题下细分割线（不抵边）
        ctx->DrawLine(D2D1::Point2F(10, titleH), D2D1::Point2F(w - 10, titleH),
                      hairline.Get(), 1.0f);
    }

    // ---- 图标网格：悬停高亮 + 图标 + 文件名 ----
    if (!f.collapsed) {
        const float scrollDip = ScrollDip(f, dpi);
        auto labelFmt = MakeFormat(dwrite, 10.5f, DWRITE_FONT_WEIGHT_NORMAL, true);
        ComPtr<ID2D1SolidColorBrush> hover;
        ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.09f), &hover);

        ctx->PushAxisAlignedClip(D2D1::RectF(0, titleH, w, h),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ForEachDrawnCell(f, icons, w, h, scrollDip,
            [&](const D2D1_RECT_F& cell, const GridGeom& g, const IconMeta& m) {
                // 悬停高亮（圆角内缩块）
                if (m.uid == hoverUid && hover) {
                    D2D1_ROUNDED_RECT hl = D2D1::RoundedRect(
                        D2D1::RectF(cell.left + 1, cell.top + 1,
                                    cell.right - 1, cell.bottom - 1), 6.0f, 6.0f);
                    ctx->FillRoundedRectangle(hl, hover.Get());
                }
                const float cx = (cell.left + cell.right) / 2;
                const bool isDragging = (m.uid == dragUid);
                const FLOAT iconAlpha = isDragging ? 0.35f : 1.0f;   // 拖拽中半透明
                if (ID2D1Bitmap* bmp = cache.GetOrCreate(ctx, m.sourcePath, m.fileTime)) {
                    const D2D1_SIZE_F s = bmp->GetSize();
                    if (s.width > 0 && s.height > 0) {
                        const float sc = std::min(g.iconSize / s.width,
                                                  g.iconSize / s.height);
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
                if (labelFmt && dim) {
                    ctx->DrawTextW(m.displayName.c_str(),
                                   (UINT32)m.displayName.size(), labelFmt.Get(),
                                   D2D1::RectF(cell.left, cell.top + 5 + g.iconSize + 3,
                                               cell.right, cell.bottom),
                                   isDragging ? faint.Get() : dim.Get());
                }
            });
        ctx->PopAxisAlignedClip();

        // 滚动条：仅细滑块（无底槽）
        const int maxPx = MaxScrollPx(f, icons, dpi);
        if (maxPx > 0) {
            const float trackTop = titleH + 4, trackBottom = h - 4;
            const float trackH = trackBottom - trackTop;
            const float viewDip = h - titleH;
            const float contentDip = viewDip + maxPx * kDipPerPx;
            const float thumbH = std::max(20.0f, trackH * viewDip / contentDip);
            const float thumbMax = trackH - thumbH;
            const float pos = (float)f.scrollOffset.y / maxPx;
            ComPtr<ID2D1SolidColorBrush> thumb;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(1, 1, 1, 0.28f), &thumb))) {
                D2D1_ROUNDED_RECT tb = D2D1::RoundedRect(
                    D2D1::RectF(w - 5, trackTop + thumbMax * pos,
                                w - 2, trackTop + thumbMax * pos + thumbH),
                    1.5f, 1.5f);
                ctx->FillRoundedRectangle(tb, thumb.Get());
            }
        }
    }

    // ---- 边框：常态细线；拖拽悬停目标 = 身份色加亮描边 ----
    if (dropTarget && accent) {
        D2D1_ROUNDED_RECT glow = D2D1::RoundedRect(
            D2D1::RectF(0, 0, w, h), radius, radius);
        ctx->DrawRoundedRectangle(glow, accent.Get(),
                                  std::max(2.0f, 2.0f * dpi / 96.0f));
    } else {
        ComPtr<ID2D1SolidColorBrush> border;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(
                D2D1::ColorF(st.border.r, st.border.g, st.border.b,
                             std::min(st.border.a, 0.25f)), &border))) {
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius);
            ctx->DrawRoundedRectangle(rr, border.Get(),
                                      std::max(1.0f, (FLOAT)dpi / 96.0f));
        }
    }
}

bool FenceRenderer::ItemAt(const Fence& f, const IconRegistry& icons, UINT dpi,
                           POINT ptPx, IconUid& outUid)
{
    if (f.collapsed || !dpi) return false;
    const FLOAT kDipPerPx = 96.0f / (FLOAT)dpi;
    const FLOAT w = (FLOAT)f.sizePx.cx * kDipPerPx;
    const FLOAT h = (FLOAT)f.sizePx.cy * kDipPerPx;
    const FLOAT x = (FLOAT)ptPx.x * kDipPerPx;
    const FLOAT y = (FLOAT)ptPx.y * kDipPerPx + ScrollDip(f, dpi);

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
