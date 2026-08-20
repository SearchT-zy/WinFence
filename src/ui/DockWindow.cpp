// Dock 栏实现（M6）。
#include "ui/DockWindow.h"

#include <d2d1helper.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>

#include "core/FenceService.h"
#include "persist/ConfigStore.h"
#include "shell/MonitorUtil.h"
#include "ui/OrganizeHint.h"
#include "ui/Compositor.h"
#include "ui/DropTarget.h"
#include "ui/FenceDrag.h"
#include "ui/IconCache.h"

namespace winfence {

namespace {

constexpr wchar_t kDockClass[] = L"WinFenceDockWnd";

// 几何（DIP）
constexpr float kMargin   = 10.0f;   // 条内边距
constexpr float kCell     = 60.0f;   // 单元宽
constexpr float kIconBase = 44.0f;   // 常态图标
constexpr float kBarH     = 76.0f;   // 条高（留出倒影区）
constexpr float kRadius   = 18.0f;
constexpr float kBubbleH  = 34.0f;   // 条上方气泡区（窗口加高，命中测试穿透）
constexpr float kReflGap  = 3.0f;    // 图标底 → 倒影顶
constexpr float kReflH    = 16.0f;   // 倒影高度
constexpr float kBottomPad = 8.0f;   // 倒影底 → 条底
constexpr float kPadX     = kShadowPadDip;    // M10：左右投影留白
constexpr float kPadBottom = 24.0f;  // M10：条下投影留白
constexpr float kBarTop() { return kBubbleH; }          // 条顶在窗口内的 y
constexpr float kBarLeft() { return kPadX; }            // 条左在窗口内的 x

constexpr UINT kMenuOpen   = 400;
constexpr UINT kMenuRemove = 401;
constexpr UINT kMenuHide   = 402;
constexpr UINT kTimerAnim  = 5;

} // namespace

void DockWindow::RegisterClass(HINSTANCE instance)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &DockWindow::WndProc;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_HAND);
    wc.hIcon         = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101),
                                         IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.lpszClassName = kDockClass;
    RegisterClassExW(&wc);
}

bool DockWindow::Create(HINSTANCE instance, Workspace& ws, IconRegistry& icons,
                        IconCache& cache, ConfigStore& store)
{
    ws_    = &ws;
    icons_ = &icons;
    cache_ = &cache;
    store_ = &store;

    // 先按最小尺寸创建，Relayout 再精确定位
    hwnd_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kDockClass, L"WinFenceDock", WS_POPUP,
        0, 0, 100, 100, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    if (!dpi_) dpi_ = 96;

    if (!Compositor::Get().BindWindow(hwnd_, dpi_)) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    // OLE 拖入（Explorer/桌面 → Dock）
    drop_ = new DropTarget([this](const std::vector<std::wstring>& paths) {
        auto uids = FenceService::RegisterIcons(*ws_, *icons_, paths);
        bool added = false;
        for (IconUid uid : uids) {
            FenceService::DetachFromAll(*ws_, uid);   // 去重（从别处摘除）
            if (ws_->dock.items.size() >= 100) break;
            ws_->dock.items.push_back(uid);
            added = true;
        }
        if (added) {
            Relayout();
            RequestRender();
            ScheduleSave();
            MaybeShowVirtualGroupingHint(hwnd_, *ws_, *store_);   // 首次拖入教育（一次）
        }
        return added;
    });
    if (FAILED(RegisterDragDrop(hwnd_, drop_))) {
        drop_->Release();
        drop_ = nullptr;
    }

    Relayout();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    RequestRender();
    return true;
}

void DockWindow::Destroy()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    ws_ = nullptr;
}

void DockWindow::Relayout()
{
    if (!hwnd_ || !ws_) return;
    const size_t n = std::max<size_t>(1, ws_->dock.items.size());
    const int cellPx   = MonitorUtil::DipToPx(kCell, dpi_);
    const int marginPx = MonitorUtil::DipToPx(kMargin, dpi_);
    const int padXPx   = MonitorUtil::DipToPx(kPadX, dpi_);
    const int barWPx   = (int)n * cellPx + 2 * marginPx;
    const int wPx = barWPx + 2 * padXPx;   // M10：窗口含左右投影留白
    const int hPx = MonitorUtil::DipToPx(kBubbleH + kBarH + kPadBottom, dpi_);

    const auto mon = MonitorUtil::Primary();
    const int x = mon.workArea.left + (mon.workArea.right - mon.workArea.left - wPx) / 2;
    const int y = mon.workArea.bottom - hPx - MonitorUtil::DipToPx(8, dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, wPx, hPx,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DockWindow::RequestRender()
{
    Draw();
}

void DockWindow::ScheduleSave()
{
    if (store_) store_->ScheduleSave();
}

void DockWindow::Draw()
{
    if (!hwnd_ || !ws_ || !icons_ || !cache_) return;
    ID2D1DeviceContext* ctx = Compositor::Get().BeginDraw(hwnd_);
    if (!ctx) return;

    using Microsoft::WRL::ComPtr;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const FLOAT w = (FLOAT)(rc.right - rc.left) * (96.0f / (FLOAT)dpi_);
    const IconUid dragUid = FenceDrag::Get().active ? FenceDrag::Get().uid : 0;

    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));

    // ---- M10：条区几何（窗口含左右投影留白 + 上方气泡区 + 下方投影留白）----
    const D2D1_COLOR_F accentC = D2D1::ColorF(ws_->defaultStyle.accent.r,
                                              ws_->defaultStyle.accent.g,
                                              ws_->defaultStyle.accent.b, 1.0f);
    const FLOAT barLeft = kBarLeft();
    const FLOAT barTop = kBarTop(), barBottom = kBarTop() + kBarH;
    const FLOAT barW = w - 2 * kPadX;
    const D2D1_RECT_F bar{barLeft, barTop, barLeft + barW, barBottom};

    // 柔和投影（M10）
    shadow_.Draw(ctx, bar, kRadius);

    // 磨砂渐变底
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[3] = {
            {0.0f, D2D1::ColorF(0.15f, 0.18f, 0.26f, 0.66f)},
            {0.55f, D2D1::ColorF(0.09f, 0.11f, 0.16f, 0.64f)},
            {1.0f, D2D1::ColorF(0.04f, 0.05f, 0.09f, 0.62f)}};
        if (SUCCEEDED(ctx->CreateGradientStopCollection(gs, 3, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{D2D1::Point2F(0, barTop),
                                                     D2D1::Point2F(0, barBottom)};
            if (SUCCEEDED(ctx->CreateLinearGradientBrush(gp, stops.Get(), &bg))) {
                D2D1_ROUNDED_RECT rr =
                    D2D1::RoundedRect(bar, kRadius, kRadius);
                ctx->FillRoundedRectangle(rr, bg.Get());
            }
        }
    }
    // 顶部内光晕（玻璃接光感）
    DrawTopGlow(ctx, bar, kRadius, 0.7f);
    // Apple 式描边（发丝白边 + 顶部内高光）
    DrawPanelBorder(ctx, bar, kRadius, accentC.r, accentC.g, accentC.b, false);
    // ---- 图标序列（M10：抛物线放大 + squircle 遮罩 + 玻璃反光 + 倒影）----
    const float t = hoverT_;                     // 气泡/光晕动画插值 0..1
    const int hoverItem = (hoverIndex_ >= 0) ? hoverIndex_ : bubbleIndex_;
    const bool mouseInside = mouseXDip_ > -1.0e8f;
    const float iconBaseBottom = barBottom - kBottomPad - kReflH - kReflGap;

    auto CenterCrop = [](const D2D1_SIZE_F& s) -> D2D1_RECT_F {
        const float side = std::min(s.width, s.height);
        return D2D1::RectF((s.width - side) / 2, (s.height - side) / 2,
                           (s.width + side) / 2, (s.height + side) / 2);
    };

    int vi = 0;
    for (size_t i = 0; i < ws_->dock.items.size(); ++i) {
        auto it = icons_->find(ws_->dock.items[i]);
        if (it == icons_->end() || it->second.orphan) continue;
        const IconMeta& m = it->second;
        const float cx = kMargin + vi * kCell + kCell / 2;   // 条本地 x
        const float cxWin = barLeft + cx;                    // 窗口 x
        const bool isHover = ((int)i == hoverItem);

        // macOS 式抛物线放大：与鼠标距离成高斯衰减，邻图标联动
        float mag = 1.0f;
        if (mouseInside) {
            const float dx = cx - mouseXDip_;
            mag = 1.0f + 0.45f * expf(-(dx * dx) / (2.0f * 44.0f * 44.0f));
        }
        const float size = kIconBase * mag;
        const float lift = (mag - 1.0f) * 16.0f;
        const float iconBottom = iconBaseBottom - lift;
        const float top = iconBottom - size;
        const D2D1_RECT_F iconRect{cxWin - size / 2, top, cxWin + size / 2, iconBottom};
        const float cr = size * 0.22f;   // squircle 圆角
        const FLOAT alpha = (m.uid == dragUid) ? 0.35f : 1.0f;

        if (ID2D1Bitmap* bmp = cache_->GetOrCreate(ctx, m.sourcePath, m.fileTime)) {
            const D2D1_SIZE_F s = bmp->GetSize();
            if (s.width > 0 && s.height > 0) {
                const D2D1_RECT_F src = CenterCrop(s);   // 中心裁剪成方形

                // 悬停光晕（随气泡动画淡入）
                if (isHover && t > 0.02f) {
                    ComPtr<ID2D1GradientStopCollection> stops;
                    D2D1_GRADIENT_STOP gs[2] = {
                        {0.0f, D2D1::ColorF(accentC.r, accentC.g, accentC.b,
                                            0.28f * t)},
                        {1.0f, D2D1::ColorF(accentC.r, accentC.g, accentC.b, 0.0f)}};
                    if (SUCCEEDED(ctx->CreateGradientStopCollection(gs, 2, &stops))) {
                        ComPtr<ID2D1RadialGradientBrush> glow;
                        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp{
                            D2D1::Point2F(cxWin, iconBottom + 2),
                            D2D1::Point2F(cxWin, iconBottom + 2),
                            size * 0.60f, size * 0.20f};
                        if (SUCCEEDED(ctx->CreateRadialGradientBrush(
                                rp, stops.Get(), &glow))) {
                            ctx->FillEllipse(
                                D2D1::Ellipse(D2D1::Point2F(cxWin, iconBottom + 2),
                                              size * 0.60f, size * 0.20f),
                                glow.Get());
                        }
                    }
                }

                // squircle 遮罩 + 图标 + 顶部玻璃反光
                ComPtr<ID2D1RoundedRectangleGeometry> geo;
                ID2D1Factory* fac = nullptr;
                ctx->GetFactory(&fac);
                if (fac && SUCCEEDED(fac->CreateRoundedRectangleGeometry(
                               D2D1::RoundedRect(iconRect, cr, cr), &geo))) {
                    ctx->PushLayer(
                        D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get(),
                                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                        nullptr);
                    ctx->DrawBitmap(bmp, iconRect, alpha,
                                    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                    &src, nullptr);
                    // 玻璃反光（上半圆角渐变）
                    ComPtr<ID2D1GradientStopCollection> stops;
                    D2D1_GRADIENT_STOP gs[2] = {
                        {0.0f, D2D1::ColorF(1, 1, 1, 0.20f)},
                        {1.0f, D2D1::ColorF(1, 1, 1, 0.0f)}};
                    if (SUCCEEDED(ctx->CreateGradientStopCollection(
                            gs, 2, &stops))) {
                        ComPtr<ID2D1LinearGradientBrush> shine;
                        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{
                            D2D1::Point2F(0, top),
                            D2D1::Point2F(0, top + size * 0.5f)};
                        if (SUCCEEDED(ctx->CreateLinearGradientBrush(
                                gp, stops.Get(), &shine)))
                            ctx->FillRectangle(
                                D2D1::RectF(cxWin - size / 2, top,
                                            cxWin + size / 2, top + size * 0.5f),
                                shine.Get());
                    }
                    ctx->PopLayer();
                    // squircle 描边
                    ComPtr<ID2D1SolidColorBrush> edge;
                    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.14f), &edge);
                    ctx->DrawRoundedRectangle(
                        D2D1::RoundedRect(iconRect, cr, cr), edge.Get(), 1.0f);
                }

                // 倒影：中心裁剪方形镜像 + 渐变遮罩沉入条底（随放大增强）
                const float reflH = kReflH * (0.7f + 0.5f * mag);
                const float reflTop = iconBaseBottom + kReflGap;
                const float midY = reflTop + reflH / 2;
                D2D1_RECT_F reflDest{cxWin - reflH / 2, reflTop,
                                     cxWin + reflH / 2, reflTop + reflH};
                ctx->SetTransform(D2D1::Matrix3x2F::Scale(1.0f, -1.0f,
                    D2D1::Point2F(cxWin, midY)));
                ctx->DrawBitmap(bmp, &reflDest, alpha * 0.30f,
                                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                &src, nullptr);
                ctx->SetTransform(D2D1::Matrix3x2F::Identity());
                {
                    ComPtr<ID2D1GradientStopCollection> stops;
                    D2D1_GRADIENT_STOP gs[2] = {
                        {0.0f, D2D1::ColorF(0.05f, 0.065f, 0.11f, 0.0f)},
                        {1.0f, D2D1::ColorF(0.05f, 0.065f, 0.11f, 0.66f)}};
                    if (SUCCEEDED(ctx->CreateGradientStopCollection(gs, 2, &stops))) {
                        ComPtr<ID2D1LinearGradientBrush> mask;
                        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{
                            D2D1::Point2F(0, reflTop), D2D1::Point2F(0, reflTop + reflH)};
                        if (SUCCEEDED(ctx->CreateLinearGradientBrush(
                                gp, stops.Get(), &mask)))
                            ctx->FillRectangle(
                                D2D1::RectF(cxWin - reflH / 2 - 2, reflTop,
                                            cxWin + reflH / 2 + 2, reflTop + reflH),
                                mask.Get());
                    }
                }
            }
        }
        // 名称气泡（悬停时，条上方；发丝描边 + 尾角，随动画淡入/淡出）
        if (isHover && t > 0.02f) {
            ComPtr<ID2D1SolidColorBrush> chip, text, line;
            ComPtr<IDWriteTextFormat> fmt;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(0.04f, 0.045f, 0.07f, 0.90f * t), &chip)) &&
                SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(1, 1, 1, 0.95f * t), &text)) &&
                SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(1, 1, 1, 0.12f * t), &line)) &&
                SUCCEEDED(Compositor::Get().DWrite()->CreateTextFormat(
                    L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    11.5f, L"zh-CN", &fmt))) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                D2D1_ROUNDED_RECT chipRect =
                    D2D1::RoundedRect(D2D1::RectF(cxWin - 86, 2, cxWin + 86, 26), 8, 8);
                ctx->FillRoundedRectangle(chipRect, chip.Get());
                ctx->DrawRoundedRectangle(chipRect, line.Get(), 1.0f);
                // 尾角：气泡底部中央的小菱形，指向图标
                ctx->SetTransform(D2D1::Matrix3x2F::Rotation(
                    45.0f, D2D1::Point2F(cxWin, 26)));
                ctx->FillRectangle(D2D1::RectF(cxWin - 3.4f, 26 - 3.4f,
                                               cxWin + 3.4f, 26 + 3.4f), chip.Get());
                ctx->SetTransform(D2D1::Matrix3x2F::Identity());
                ctx->DrawTextW(m.displayName.c_str(),
                               (UINT32)m.displayName.size(), fmt.Get(),
                               D2D1::RectF(cxWin - 84, 3, cxWin + 84, 25), text.Get());
            }
        }
        ++vi;
    }

    // 空态提示
    if (vi == 0) {
        ComPtr<ID2D1SolidColorBrush> hint;
        ComPtr<IDWriteTextFormat> fmt;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.4f), &hint)) &&
            SUCCEEDED(Compositor::Get().DWrite()->CreateTextFormat(
                L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                11.5f, L"zh-CN", &fmt))) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ctx->DrawTextW(L"把图标拖到这里", 7, fmt.Get(), bar, hint.Get());
        }
    }

    Compositor::Get().EndDraw(hwnd_);
}

LRESULT CALLBACK DockWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    DockWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<DockWindow*>(
            reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    } else {
        self = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (self) return self->Handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT DockWindow::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCREATE:
        break;

    case WM_NCHITTEST: {   // M10：条区（含气泡区上/左右投影/下投影）之外 → 点击穿透
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &p);
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const int padXPx   = MonitorUtil::DipToPx((int)kPadX, dpi_);
        const int barTopPx = MonitorUtil::DipToPx((int)kBubbleH, dpi_);
        const int barBottomPx = MonitorUtil::DipToPx((int)(kBubbleH + kBarH), dpi_);
        if (p.x < padXPx || p.x >= rc.right - padXPx ||
            p.y < barTopPx || p.y >= barBottomPx)
            return HTTRANSPARENT;
        return HTCLIENT;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        EndPaint(hwnd_, &ps);
        RequestRender();
        return 0;
    }

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            Compositor::Get().Resize(hwnd_, LOWORD(lp), HIWORD(lp));
            RequestRender();
        }
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        Compositor::Get().SetDpi(hwnd_, dpi_);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int cellPx   = MonitorUtil::DipToPx(kCell, dpi_);
        const int marginPx = MonitorUtil::DipToPx(kMargin, dpi_);
        const int padXPx   = MonitorUtil::DipToPx(kPadX, dpi_);
        const int bx = pt.x - padXPx;   // M10：条本地坐标
        const int idx = (bx - marginPx) / cellPx;
        const float barTopPx = (float)MonitorUtil::DipToPx((int)kBubbleH, dpi_);
        const float barHPx = (float)MonitorUtil::DipToPx((int)kBarH, dpi_);
        const bool inBar = pt.y >= barTopPx && pt.y < barTopPx + barHPx;

        if (dragging_) {   // 栏内活体排序
            if (inBar && idx >= 0 && idx < (int)ws_->dock.items.size() &&
                idx != pressIndex_) {
                auto& items = ws_->dock.items;
                const IconUid uid = FenceDrag::Get().uid;
                auto it = std::find(items.begin(), items.end(), uid);
                if (it != items.end()) {
                    items.erase(it);
                    items.insert(items.begin() + idx, uid);
                    pressIndex_ = idx;
                    RequestRender();
                }
            }
            auto& d = FenceDrag::Get();
            POINT screen{};
            GetCursorPos(&screen);
            HWND target = WindowFromPoint(screen);
            if (target != d.hover) {
                if (d.hover) PostMessageW(d.hover, kMsgFenceRefresh, 0, 0);
                d.hover = target;
                if (d.hover) PostMessageW(d.hover, kMsgFenceRefresh, 0, 0);
                RequestRender();
            }
            return 0;
        }
        if (pressIndex_ >= 0) {   // 越阈 → 进入拖拽
            const int thr = std::max(4, MonitorUtil::DipToPx(4.0f, dpi_));
            const int dx = pt.x - pressPt_.x, dy = pt.y - pressPt_.y;
            if (dx * dx + dy * dy > thr * thr) {
                dragging_ = true;
                orderBackup_ = ws_->dock.items;
                auto& d = FenceDrag::Get();
                d.active = true;
                d.source = hwnd_;
                d.uid    = ws_->dock.items[(size_t)pressIndex_];
                d.hover  = hwnd_;
            }
            return 0;
        }

        // 抛物线放大：记录鼠标条本地 X（离开置 -1e9）
        mouseXDip_ = bx * (96.0f / (FLOAT)dpi_);
        if (!mouseTracking_) {
            TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0};
            if (TrackMouseEvent(&tme)) mouseTracking_ = true;
        }
        const int newHover = (inBar && idx >= 0 && idx < (int)ws_->dock.items.size())
                                 ? idx : -1;
        if (newHover != hoverIndex_) {
            hoverIndex_ = newHover;
            if (newHover >= 0) bubbleIndex_ = newHover;
            if (!animating_) {
                animating_ = true;
                SetTimer(hwnd_, kTimerAnim, 15, nullptr);
            }
        }
        RequestRender();   // 放大随鼠标连续变化，每帧重绘
        return 0;
    }

    case WM_MOUSELEAVE:
        mouseTracking_ = false;
        mouseXDip_ = -1.0e9f;   // 收起放大
        if (hoverIndex_ != -1) {
            hoverIndex_ = -1;   // bubbleIndex_ 保留 → 动画淡出后再清
            if (!animating_) {
                animating_ = true;
                SetTimer(hwnd_, kTimerAnim, 15, nullptr);
            }
        }
        RequestRender();
        return 0;

    case WM_TIMER:   // M9：悬停缩放/淡出动画（15ms 步进）
        if (wp == kTimerAnim) {
            const float target = (hoverIndex_ >= 0) ? 1.0f : 0.0f;
            const float step = 0.22f;
            if (hoverT_ < target) hoverT_ = std::min(target, hoverT_ + step);
            else if (hoverT_ > target) hoverT_ = std::max(target, hoverT_ - step);
            RequestRender();
            if (hoverT_ == target) {
                KillTimer(hwnd_, kTimerAnim);
                animating_ = false;
                if (hoverIndex_ < 0) bubbleIndex_ = -1;
            }
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int barTopPx = MonitorUtil::DipToPx((int)kBubbleH, dpi_);
        const int barBottomPx = MonitorUtil::DipToPx((int)(kBubbleH + kBarH), dpi_);
        if (pt.y < barTopPx || pt.y >= barBottomPx) return 0;   // 条外（理论上被穿透）
        const int cellPx   = MonitorUtil::DipToPx(kCell, dpi_);
        const int marginPx = MonitorUtil::DipToPx(kMargin, dpi_);
        const int padXPx   = MonitorUtil::DipToPx(kPadX, dpi_);
        const int idx = (pt.x - padXPx - marginPx) / cellPx;
        if (idx >= 0 && idx < (int)ws_->dock.items.size()) {
            pressIndex_ = idx;
            pressPt_ = pt;
            SetCapture(hwnd_);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (dragging_) {
            ReleaseCapture();
            auto& d = FenceDrag::Get();
            POINT screen{};
            GetCursorPos(&screen);
            HWND target = WindowFromPoint(screen);
            wchar_t cls[64]{};
            GetClassNameW(target, cls, 64);
            const bool isFence = (target && wcscmp(cls, L"WinFenceFenceWnd") == 0);
            if (isFence && target != hwnd_) {
                SendMessageW(target, kMsgFenceDropItem, 0, (LPARAM)d.uid);
            } else if (target != hwnd_) {
                ws_->dock.items = orderBackup_;   // 拖出取消 → 还原
            }
            if (d.hover && d.hover != hwnd_) PostMessageW(d.hover, kMsgFenceRefresh, 0, 0);
            d.active = false; d.source = nullptr; d.uid = 0; d.hover = nullptr;
            dragging_ = false;
            pressIndex_ = -1;
            Relayout();
            RequestRender();
            ScheduleSave();
            return 0;
        }
        if (pressIndex_ >= 0) {   // 单击 → 启动（Dock 语义，区别于栅栏双击）
            const IconUid uid = ws_->dock.items[(size_t)pressIndex_];
            ReleaseCapture();
            pressIndex_ = -1;
            OpenItem(uid);
            return 0;
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (dragging_) {
            ws_->dock.items = orderBackup_;
            auto& d = FenceDrag::Get();
            if (d.hover && d.hover != hwnd_) PostMessageW(d.hover, kMsgFenceRefresh, 0, 0);
            d.active = false; d.source = nullptr; d.uid = 0; d.hover = nullptr;
            dragging_ = false;
            pressIndex_ = -1;
            RequestRender();
        }
        return 0;

    case WM_CONTEXTMENU:
        ShowContextMenu(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kTimerAnim);
        if (drop_) { RevokeDragDrop(hwnd_); drop_ = nullptr; }
        Compositor::Get().UnbindWindow(hwnd_);
        return 0;

    default:
        break;
    }

    if (msg == kMsgFenceRefresh) {
        RequestRender();
        return 0;
    }
    if (msg == kMsgFenceDropItem) {   // 栅栏 → Dock
        if (!FenceDrag::Get().active) return 0;
        const IconUid uid = (IconUid)lp;
        FenceService::DetachFromAll(*ws_, uid);
        if (ws_->dock.items.size() < 100) ws_->dock.items.push_back(uid);
        Relayout();
        RequestRender();
        ScheduleSave();
        return 1;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void DockWindow::OpenItem(IconUid uid)
{
    auto it = icons_->find(uid);
    if (it == icons_->end() || it->second.orphan) return;
    if (it->second.sourcePath.empty()) return;
    ShellExecuteW(hwnd_, L"open", it->second.sourcePath.c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}

void DockWindow::ShowContextMenu(POINT screenPt)
{
    POINT client = screenPt;
    ScreenToClient(hwnd_, &client);
    const int barTopPx = MonitorUtil::DipToPx((int)kBubbleH, dpi_);
    const int barBottomPx = MonitorUtil::DipToPx((int)(kBubbleH + kBarH), dpi_);
    const int cellPx   = MonitorUtil::DipToPx(kCell, dpi_);
    const int marginPx = MonitorUtil::DipToPx(kMargin, dpi_);
    const int padXPx   = MonitorUtil::DipToPx(kPadX, dpi_);
    const int idx = (client.y >= barTopPx && client.y < barBottomPx)
                        ? (client.x - padXPx - marginPx) / cellPx : -1;
    IconUid uid = 0;
    if (idx >= 0 && idx < (int)ws_->dock.items.size())
        uid = ws_->dock.items[(size_t)idx];

    HMENU menu = CreatePopupMenu();
    if (uid) {
        AppendMenuW(menu, MF_STRING, kMenuOpen, L"打开");
        AppendMenuW(menu, MF_STRING, kMenuRemove, L"从 Dock 移除");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kMenuHide, L"隐藏 Dock（设置中可再开启）");

    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                    screenPt.x, screenPt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case kMenuOpen:
        OpenItem(uid);
        break;
    case kMenuRemove:
        ws_->dock.items.erase(
            std::remove(ws_->dock.items.begin(), ws_->dock.items.end(), uid),
            ws_->dock.items.end());
        Relayout();
        RequestRender();
        ScheduleSave();
        break;
    case kMenuHide:
        ws_->dock.visible = false;
        ShowWindow(hwnd_, SW_HIDE);
        ScheduleSave();
        break;
    default:
        break;
    }
}

} // namespace winfence
