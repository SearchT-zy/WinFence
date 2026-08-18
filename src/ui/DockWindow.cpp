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
constexpr float kIconHov  = 58.0f;   // 悬停放大
constexpr float kBarH     = 64.0f;   // 条高
constexpr float kRadius   = 16.0f;

constexpr UINT kMenuOpen   = 400;
constexpr UINT kMenuRemove = 401;
constexpr UINT kMenuHide   = 402;

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
    const int wPx = (int)n * cellPx + 2 * marginPx;
    const int hPx = MonitorUtil::DipToPx(kBarH, dpi_);

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
    const FLOAT kDip = 96.0f / (FLOAT)dpi_;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const FLOAT w = (FLOAT)(rc.right - rc.left) * kDip;
    const FLOAT h = (FLOAT)(rc.bottom - rc.top) * kDip;
    const IconUid dragUid = FenceDrag::Get().active ? FenceDrag::Get().uid : 0;

    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));

    // ---- 黑色半透明圆角条 ----
    ComPtr<ID2D1SolidColorBrush> bar;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(
            D2D1::ColorF(0.05f, 0.05f, 0.07f, 0.55f), &bar))) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h),
                                                 kRadius, kRadius);
        ctx->FillRoundedRectangle(rr, bar.Get());
    }
    ComPtr<ID2D1SolidColorBrush> border;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(
            D2D1::ColorF(1, 1, 1, 0.10f), &border))) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h),
                                                 kRadius, kRadius);
        ctx->DrawRoundedRectangle(rr, border.Get(), 1.0f);
    }

    // ---- 图标序列（悬停放大上浮 + 名称气泡；拖拽中半透明）----
    // 可视索引（跳过 orphan/缺失）与 hoverIndex_（items 索引）换算
    auto VisualIndexOf = [&](size_t itemIdx) -> int {
        int vi = -1;
        int seen = 0;
        for (size_t i = 0; i < ws_->dock.items.size(); ++i) {
            auto it = icons_->find(ws_->dock.items[i]);
            if (it == icons_->end() || it->second.orphan) continue;
            if (i == itemIdx) return seen;
            ++seen;
        }
        return vi;
    };

    int vi = 0;
    for (size_t i = 0; i < ws_->dock.items.size(); ++i) {
        auto it = icons_->find(ws_->dock.items[i]);
        if (it == icons_->end() || it->second.orphan) continue;
        const IconMeta& m = it->second;
        const float cx = kMargin + vi * kCell + kCell / 2;
        const bool hovered = ((int)i == hoverIndex_);
        const float size = hovered ? kIconHov : kIconBase;
        const float top  = hovered ? (h - size - 6) : (h - size - 10);
        const FLOAT alpha = (m.uid == dragUid) ? 0.35f : 1.0f;

        if (ID2D1Bitmap* bmp = cache_->GetOrCreate(ctx, m.sourcePath, m.fileTime)) {
            const D2D1_SIZE_F s = bmp->GetSize();
            if (s.width > 0 && s.height > 0) {
                const float sc = std::min(size / s.width, size / s.height);
                const float iw = s.width * sc, ih = s.height * sc;
                D2D1_RECT_F dest{cx - iw / 2, top, cx + iw / 2, top + ih};
                ctx->DrawBitmap(bmp, &dest, alpha,
                                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                nullptr, nullptr);
            }
        }
        // 名称气泡（悬停时，显示在条上方）
        if (hovered) {
            ComPtr<ID2D1SolidColorBrush> chip, text;
            ComPtr<IDWriteTextFormat> fmt;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(0.05f, 0.05f, 0.07f, 0.85f), &chip)) &&
                SUCCEEDED(ctx->CreateSolidColorBrush(
                    D2D1::ColorF(1, 1, 1, 0.95f), &text)) &&
                SUCCEEDED(Compositor::Get().DWrite()->CreateTextFormat(
                    L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    11.5f, L"zh-CN", &fmt))) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                D2D1_ROUNDED_RECT chipRect =
                    D2D1::RoundedRect(D2D1::RectF(cx - 86, -34, cx + 86, -6), 6, 6);
                ctx->FillRoundedRectangle(chipRect, chip.Get());
                ctx->DrawTextW(m.displayName.c_str(),
                               (UINT32)m.displayName.size(), fmt.Get(),
                               D2D1::RectF(cx - 84, -33, cx + 84, -7), text.Get());
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
            ctx->DrawTextW(L"把图标拖到这里", 7, fmt.Get(),
                           D2D1::RectF(0, 0, w, h), hint.Get());
        }
    }

    Compositor::Get().EndDraw(hwnd_);
    (void)VisualIndexOf;
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
        const int idx = (pt.x - marginPx) / cellPx;

        if (dragging_) {   // 栏内活体排序
            if (idx >= 0 && idx < (int)ws_->dock.items.size() && idx != pressIndex_) {
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

        // 悬停放大
        if (!mouseTracking_) {
            TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0};
            if (TrackMouseEvent(&tme)) mouseTracking_ = true;
        }
        const int newHover = (idx >= 0 && idx < (int)ws_->dock.items.size()) ? idx : -1;
        if (newHover != hoverIndex_) {
            hoverIndex_ = newHover;
            RequestRender();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        mouseTracking_ = false;
        if (hoverIndex_ != -1) {
            hoverIndex_ = -1;
            RequestRender();
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int cellPx   = MonitorUtil::DipToPx(kCell, dpi_);
        const int marginPx = MonitorUtil::DipToPx(kMargin, dpi_);
        const int idx = (pt.x - marginPx) / cellPx;
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
    const int cellPx   = MonitorUtil::DipToPx(kCell, dpi_);
    const int marginPx = MonitorUtil::DipToPx(kMargin, dpi_);
    const int idx = (client.x - marginPx) / cellPx;
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
