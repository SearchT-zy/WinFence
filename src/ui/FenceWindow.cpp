// 栅栏窗口实现（DESIGN.md §3.2/§3.3/§3.4/§3.6/§4.4/§4.5/§4.6）。
#include "ui/FenceWindow.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

#include "core/FenceService.h"
#include "persist/ConfigStore.h"
#include "ui/OrganizeHint.h"
#include "shell/DesktopAnchor.h"
#include "shell/MonitorUtil.h"
#include "ui/Compositor.h"
#include "ui/DropTarget.h"
#include "ui/FenceDrag.h"
#include "ui/IconCache.h"

namespace winfence {

namespace {

constexpr wchar_t kClassName[] = L"WinFenceFenceWnd";
constexpr UINT kTimerReanchorDebounce = 1;
constexpr UINT kTimerReanchorPeriodic = 2;
constexpr UINT kTimerCollapseAnim     = 3;
constexpr LONG kMinWidthPx  = 64;
constexpr LONG kMinHeightPx = 40;
constexpr DWORD kCollapseAnimMs = 150;   // §3.4 折叠动画时长

// 右键菜单命令
constexpr UINT kMenuOpenItem    = 100;   // 打开图标
constexpr UINT kMenuRemoveItem  = 101;   // 从此栅栏移除
constexpr UINT kMenuNewFence    = 200;
constexpr UINT kMenuRenameFence = 203;   // 重命名栅栏
constexpr UINT kMenuSettings    = 204;   // 设置…
constexpr UINT kMenuDeleteFence = 201;
constexpr UINT kMenuExit        = 202;

// 应用图标（resources/winfence.rc 中的 IDI_APP）
constexpr UINT kAppIconId = 101;

UINT g_wmTaskbarCreated = 0;

} // namespace

void FenceWindow::RegisterClass(HINSTANCE instance)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &FenceWindow::WndProc;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(kAppIconId),
                                         IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}

bool FenceWindow::Create(HINSTANCE instance, Fence& fence, Workspace& ws,
                         IconRegistry& icons, IconCache& cache, ConfigStore& store)
{
    fence_ = &fence;
    ws_ = &ws;
    icons_ = &icons;
    cache_ = &cache;
    store_ = &store;

    const SIZE sz = fence.collapsed ? fence.collapsedSizePx : fence.sizePx;
    auto mon = MonitorUtil::FromPoint(fence.posPx);
    RECT r{fence.posPx.x, fence.posPx.y,
           fence.posPx.x + sz.cx, fence.posPx.y + sz.cy};
    r = MonitorUtil::ClampToWorkArea(r, mon);

    hwnd_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, fence.title.c_str(), WS_POPUP | WS_THICKFRAME,   // THICKFRAME 才能缩放
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    if (!dpi_) dpi_ = mon.dpiX ? mon.dpiX : 96;

    if (!Compositor::Get().BindWindow(hwnd_, dpi_)) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    backdrop_ = DwmBackdrop::Detect();
    DwmBackdrop::Apply(hwnd_, backdrop_);

    // §3.2/§4.6：OLE 拖入（Drop → PathGuard → 栅栏归属）
    drop_ = new DropTarget([this](const std::vector<std::wstring>& paths) {
        const bool ok = FenceService::AddItems(*ws_, *icons_, fence_->id, paths);
        if (ok) {
            RequestRender();
            ScheduleSave();
            MaybeShowVirtualGroupingHint(hwnd_, *ws_, *store_);   // 首次拖入教育（一次）
        }
        return ok;
    });
    if (FAILED(RegisterDragDrop(hwnd_, drop_))) {
        drop_->Release();   // 注册失败则不交给 OLE 管理
        drop_ = nullptr;
    }

    DesktopAnchor::RegisterFence(hwnd_);
    SetTimer(hwnd_, kTimerReanchorPeriodic, 60000, nullptr);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    RequestRender();
    return true;
}

void FenceWindow::Destroy()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    fence_ = nullptr;
}

void FenceWindow::RequestRender()
{
    if (!hwnd_ || !fence_ || !icons_ || !cache_) return;
    const auto& drag = FenceDrag::Get();
    const IconUid dragUid = (drag.active && drag.source == hwnd_) ? drag.uid : 0;
    const bool dropTarget = drag.active && drag.hover == hwnd_ && drag.source != hwnd_;
    ID2D1DeviceContext* ctx = Compositor::Get().BeginDraw(hwnd_);
    if (!ctx) return;
    renderer_.Draw(*fence_, *icons_, *cache_, ctx, Compositor::Get().DWrite(), dpi_,
                   backdrop_ == BackdropSupport::SystemBackdrop, hoverUid_,
                   dragUid, dropTarget);
    Compositor::Get().EndDraw(hwnd_);
}

void FenceWindow::ScheduleSave()
{
    if (store_) store_->ScheduleSave();
}

LRESULT CALLBACK FenceWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    FenceWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<FenceWindow*>(
            reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    } else {
        self = reinterpret_cast<FenceWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (self) return self->Handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT FenceWindow::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCREATE:
        break;

    case WM_NCCALCSIZE:   // 无边框但可缩放：客户区覆盖整个窗口（消除 THICKFRAME 边框内缩）
        if (wp) return 0;
        break;

    case WM_NCHITTEST:
        return OnNcHitTest(lp);

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
        const UINT newDpi = HIWORD(wp);
        const FLOAT ratio = dpi_ ? (FLOAT)newDpi / (FLOAT)dpi_ : 1.0f;
        dpi_ = newDpi;
        Compositor::Get().SetDpi(hwnd_, dpi_);
        fence_->sizePx = {
            (LONG)(fence_->sizePx.cx * ratio), (LONG)(fence_->sizePx.cy * ratio)};
        fence_->collapsedSizePx = {
            (LONG)(fence_->collapsedSizePx.cx * ratio),
            (LONG)(fence_->collapsedSizePx.cy * ratio)};
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        ScheduleSave();
        return 0;
    }

    case WM_NCLBUTTONDBLCLK:
        ToggleCollapse();
        return 0;

    case WM_LBUTTONDBLCLK: {   // 双击图标 → 打开（§3.3）
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        IconUid uid = 0;
        if (FenceRenderer::ItemAt(*fence_, *icons_, dpi_, pt, uid)) OpenItem(uid);
        return 0;
    }

    case WM_LBUTTONDOWN: {   // 按下图标 → 预备拖拽（§3.3）
        if (fence_->collapsed) break;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        IconUid uid = 0;
        if (FenceRenderer::ItemAt(*fence_, *icons_, dpi_, pt, uid)) {
            pressUid_ = uid;
            pressPt_  = pt;
            SetCapture(hwnd_);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        // ---- 拖拽进行中：栏内活体排序 + 跨栏目标探测 ----
        if (dragging_) {
            IconUid hit = 0;
            auto& items = fence_->items;
            const IconUid dragUid = FenceDrag::Get().uid;
            if (FenceRenderer::ItemAt(*fence_, *icons_, dpi_, pt, hit) &&
                hit != dragUid) {
                auto it = std::find(items.begin(), items.end(), dragUid);
                auto jt = std::find(items.begin(), items.end(), hit);
                if (it != items.end() && jt != items.end()) {
                    const size_t oldIdx = (size_t)(it - items.begin());
                    const size_t hitIdx = (size_t)(jt - items.begin());
                    items.erase(it);
                    // 向后拖 → 放到 hit 前；向前拖 → 放到 hit 后
                    jt = std::find(items.begin(), items.end(), hit);
                    const size_t insertPos = (hitIdx > oldIdx)
                        ? (size_t)(jt - items.begin())          // hit 前
                        : (size_t)(jt - items.begin()) + 1;     // hit 后
                    items.insert(items.begin() + (long)insertPos, dragUid);
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

        // ---- 未拖拽：越过阈值 → 进入拖拽 ----
        if (pressUid_) {
            const int threshold = std::max(4, MonitorUtil::DipToPx(4.0f, dpi_));
            const int dx = pt.x - pressPt_.x, dy = pt.y - pressPt_.y;
            if (dx * dx + dy * dy > threshold * threshold) {
                dragging_ = true;
                orderBackup_ = fence_->items;
                auto& d = FenceDrag::Get();
                d.active = true;
                d.source = hwnd_;
                d.uid    = pressUid_;
                d.hover  = hwnd_;
            }
            return 0;
        }

        // ---- 普通悬停高亮 ----
        if (!mouseTracking_) {
            TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0};
            if (TrackMouseEvent(&tme)) mouseTracking_ = true;
        }
        if (fence_->collapsed) return 0;
        IconUid uid = 0;
        FenceRenderer::ItemAt(*fence_, *icons_, dpi_, pt, uid);
        if (uid != hoverUid_) {
            hoverUid_ = uid;
            RequestRender();
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        // 标题栏「＋」：新建栅栏（热区为 HTCLIENT，见 OnNcHitTest）
        if (!dragging_ && pressUid_ == 0) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            const int titleH = MonitorUtil::DipToPx(fence_->style.titleBarHeightDip, dpi_);
            const int zoneW = MonitorUtil::DipToPx(kPlusZoneWidthDip, dpi_);
            const int zoneR = MonitorUtil::DipToPx(kPlusZoneRightDip, dpi_);
            if (pt.y < titleH && pt.x >= rc.right - zoneR &&
                pt.x < rc.right - zoneR + zoneW) {
                if (onAction_) onAction_(AppAction::NewFence, 0);
                return 0;
            }
        }
        if (dragging_) {   // 落子：跨栏/Dock 移动 / 栏内保留新序 / 栏外取消还原
            ReleaseCapture();
            auto& d = FenceDrag::Get();
            POINT screen{};
            GetCursorPos(&screen);
            HWND target = WindowFromPoint(screen);
            wchar_t cls[64]{};
            GetClassNameW(target, cls, 64);
            const bool isFence = (target && wcscmp(cls, kFenceWndClass) == 0);
            const bool isDock  = (target && wcscmp(cls, kDockWndClass) == 0);
            const bool validTarget = (isFence || isDock) && target != hwnd_;
            bool moved = false;
            if (validTarget) {
                moved = (SendMessageW(target, kMsgFenceDropItem,
                                      (WPARAM)fence_->id, (LPARAM)pressUid_) != 0);
            }
            if (!moved && !validTarget)
                fence_->items = orderBackup_;   // 拖到外面 = 取消，还原起始顺序
            if (d.hover && d.hover != hwnd_) PostMessageW(d.hover, kMsgFenceRefresh, 0, 0);
            d.active = false; d.source = nullptr; d.uid = 0; d.hover = nullptr;
            dragging_ = false;
            pressUid_ = 0;
            RequestRender();
            ScheduleSave();
            return 0;
        }
        if (pressUid_) {   // 原地单击（未成拖拽）
            ReleaseCapture();
            pressUid_ = 0;
        }
        return 0;
    }

    case WM_CAPTURECHANGED:   // 捕获被抢（Esc/系统菜单等）→ 优雅取消
        if (dragging_) {
            fence_->items = orderBackup_;
            auto& d = FenceDrag::Get();
            if (d.hover && d.hover != hwnd_) PostMessageW(d.hover, kMsgFenceRefresh, 0, 0);
            d.active = false; d.source = nullptr; d.uid = 0; d.hover = nullptr;
            dragging_ = false;
            pressUid_ = 0;
            RequestRender();
        }
        return 0;

    case WM_MOUSELEAVE:
        mouseTracking_ = false;
        if (hoverUid_ != 0) {
            hoverUid_ = 0;
            RequestRender();
        }
        return 0;

    case WM_MOUSEWHEEL: {   // 内容区滚动（一格一单元）
        if (fence_->collapsed) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const int step = MonitorUtil::DipToPx(84.0f, dpi_);   // cellH 一行
        const int maxScroll = FenceRenderer::MaxScrollPx(*fence_, *icons_, dpi_);
        if (maxScroll <= 0) return 0;
        int newY = fence_->scrollOffset.y + (delta > 0 ? -step : step);
        if (newY < 0) newY = 0;
        if (newY > maxScroll) newY = maxScroll;
        if (newY != fence_->scrollOffset.y) {
            fence_->scrollOffset.y = newY;
            RequestRender();
            ScheduleSave();
        }
        return 0;
    }

    case WM_SIZING: {
        auto* r = reinterpret_cast<RECT*>(lp);
        if (r->right - r->left < kMinWidthPx) {
            if (wp == WMSZ_LEFT || wp == WMSZ_TOPLEFT || wp == WMSZ_BOTTOMLEFT)
                r->left = r->right - kMinWidthPx;
            else
                r->right = r->left + kMinWidthPx;
        }
        if (r->bottom - r->top < kMinHeightPx) {
            if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT)
                r->top = r->bottom - kMinHeightPx;
            else
                r->bottom = r->top + kMinHeightPx;
        }
        return TRUE;
    }

    case WM_EXITSIZEMOVE: {
        RECT wr{};
        GetWindowRect(hwnd_, &wr);
        const SIZE cur{wr.right - wr.left, wr.bottom - wr.top};
        if (inModalLoop_) {   // 仅真实用户拖动结束才落模型（防幻影写）
            fence_->posPx = {wr.left, wr.top};
            if (fence_->collapsed) fence_->collapsedSizePx = cur;
            else                   fence_->sizePx = cur;
            ScheduleSave();
        }
        inModalLoop_ = false;
        return 0;
    }

    case WM_WINDOWPOSCHANGED: {
        auto* wpos = reinterpret_cast<WINDOWPOS*>(lp);
        if (!(wpos->flags & SWP_NOZORDER))
            SetTimer(hwnd_, kTimerReanchorDebounce, 200, nullptr);
        break;
    }

    case WM_TIMER:
        if (wp == kTimerReanchorDebounce) {
            KillTimer(hwnd_, kTimerReanchorDebounce);
            DesktopAnchor::AnchorAll();
        } else if (wp == kTimerReanchorPeriodic) {
            DesktopAnchor::AnchorAll();
            ClampOntoScreen();
            RequestRender();
        } else if (wp == kTimerCollapseAnim) {
            const float t = std::min(1.0f, (float)(GetTickCount() - animStart_)
                                              / (float)kCollapseAnimMs);
            const float e = t * t * (3.0f - 2.0f * t);   // smoothstep
            const LONG cy = (LONG)(animFrom_ + (animTo_ - animFrom_) * e);
            RECT wr{};
            GetWindowRect(hwnd_, &wr);
            SetWindowPos(hwnd_, nullptr, 0, 0, wr.right - wr.left, cy,
                         SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
            RequestRender();
            if (t >= 1.0f) {
                KillTimer(hwnd_, kTimerCollapseAnim);
                animating_ = false;
            }
        }
        return 0;

    case WM_DISPLAYCHANGE:
        ClampOntoScreen();
        DesktopAnchor::AnchorAll();
        RequestRender();
        return 0;

    case WM_CONTEXTMENU:   // 鼠标右键 / Shift+F10，坐标为屏幕坐标
        ShowContextMenu(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;

    case WM_CLOSE:   // 关闭单个栅栏 = 删除该栅栏（窗口生命周期归 App 管）
        if (onAction_) onAction_(AppAction::DeleteFence, fence_->id);
        return 0;

    case WM_GETMINMAXINFO: {   // 系统级最小尺寸（与 WM_SIZING 约束一致）
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = kMinWidthPx;
        mmi->ptMinTrackSize.y = kMinHeightPx;
        return 0;
    }

    case WM_ENTERSIZEMOVE:   // 真实用户拖动/缩放开始
        inModalLoop_ = true;
        break;

    case WM_QUERYENDSESSION:   // §4.10：关机前同步落盘（防抖定时器来不及）
        store_->FlushNow();
        return TRUE;

    case WM_ENDSESSION:
        if (wp) store_->FlushNow();
        return 0;

    case WM_DESTROY:
        if (drop_) { RevokeDragDrop(hwnd_); drop_ = nullptr; }   // §4.6 顺序铁律
        KillTimer(hwnd_, kTimerReanchorDebounce);
        KillTimer(hwnd_, kTimerReanchorPeriodic);
        KillTimer(hwnd_, kTimerCollapseAnim);
        DesktopAnchor::UnregisterFence(hwnd_);
        Compositor::Get().UnbindWindow(hwnd_);
        return 0;   // 退出时机由 App 决定（最后一个栅栏关闭 / 菜单退出）

    default:
        break;
    }

    // ---- 栅栏间拖拽消息（ui/FenceDrag.h）----
    if (msg == kMsgFenceRefresh) {
        RequestRender();
        return 0;
    }
    if (msg == kMsgFenceDropItem) {   // lp=uid；通用语义：从所有归属处摘除后入本栏（§3.3）
        if (!FenceDrag::Get().active) return 0;
        const IconUid uid = (IconUid)lp;
        FenceService::DetachFromAll(*ws_, uid);
        fence_->items.push_back(uid);
        RequestRender();
        ScheduleSave();
        return 1;
    }

    if (g_wmTaskbarCreated && msg == g_wmTaskbarCreated) {
        DesktopAnchor::HandleTaskbarCreated();
        RequestRender();
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT FenceWindow::OnNcHitTest(LPARAM lp)
{
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &p);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right, h = rc.bottom;

    const int titleH = MonitorUtil::DipToPx(fence_->style.titleBarHeightDip, dpi_);
    const int radius = MonitorUtil::DipToPx(fence_->style.cornerRadiusDip, dpi_);
    const int border = std::max(5, MonitorUtil::DipToPx(7.0f, dpi_));   // 缩放手柄宽

    // ---- 标题栏「＋」新建按钮热区（优先于标题拖动）----
    {
        const int zoneW = MonitorUtil::DipToPx(kPlusZoneWidthDip, dpi_);
        const int zoneR = MonitorUtil::DipToPx(kPlusZoneRightDip, dpi_);
        if (p.y < titleH && p.x >= w - zoneR && p.x < w - zoneR + zoneW)
            return HTCLIENT;
    }

    // ---- 八方向缩放（THICKFRAME 命中测试，系统自动给大小光标）----
    const bool atL = p.x < border, atR = p.x >= w - border;
    const bool atT = p.y < border, atB = p.y >= h - border;
    if (atT && atL) return HTTOPLEFT;
    if (atT && atR) return HTTOPRIGHT;
    if (atB && atL) return HTBOTTOMLEFT;
    if (atB && atR) return HTBOTTOMRIGHT;
    if (atL) return HTLEFT;
    if (atR) return HTRIGHT;
    if (atT) return HTTOP;
    if (atB) return HTBOTTOM;

    // ---- 圆角外的角 → 点击穿透到桌面 ----
    auto cornerOutside = [&](int cx, int cy) {
        const double dx = p.x - cx, dy = p.y - cy;
        return dx * dx + dy * dy > (double)radius * radius;
    };
    if (radius > 0) {
        if (p.x < radius && p.y < radius && cornerOutside(radius, radius))
            return HTTRANSPARENT;
        if (p.x >= w - radius && p.y < radius && cornerOutside(w - radius, radius))
            return HTTRANSPARENT;
        if (p.x < radius && p.y >= h - radius && cornerOutside(radius, h - radius))
            return HTTRANSPARENT;
        if (p.x >= w - radius && p.y >= h - radius && cornerOutside(w - radius, h - radius))
            return HTTRANSPARENT;
    }

    if (p.y < titleH) return HTCAPTION;
    if (fence_->collapsed) return HTTRANSPARENT;
    return HTCLIENT;
}

void FenceWindow::ToggleCollapse()
{
    fence_->collapsed = !fence_->collapsed;
    StartCollapseAnim();
    RequestRender();
    ScheduleSave();
}

void FenceWindow::StartCollapseAnim()
{
    if (animating_) return;
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    animFrom_ = wr.bottom - wr.top;
    animTo_ = fence_->collapsed ? fence_->collapsedSizePx.cy : fence_->sizePx.cy;
    if (animFrom_ == animTo_) return;   // 无需动画
    animStart_ = GetTickCount();
    animating_ = true;
    SetTimer(hwnd_, kTimerCollapseAnim, 15, nullptr);
}

void FenceWindow::ClampOntoScreen()
{
    if (!hwnd_) return;
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    const POINT center{(wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2};
    const auto m = MonitorUtil::FromPoint(center);
    const RECT clamped = MonitorUtil::ClampToWorkArea(wr, m);
    if (clamped.left != wr.left || clamped.top != wr.top) {
        SetWindowPos(hwnd_, nullptr, clamped.left, clamped.top, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        fence_->posPx = {clamped.left, clamped.top};
    }
}

void FenceWindow::ShowContextMenu(POINT screenPt)
{
    // 命中图标则追加图标级菜单项
    POINT client = screenPt;
    ScreenToClient(hwnd_, &client);
    IconUid uid = 0;
    const bool onItem =
        !fence_->collapsed &&
        FenceRenderer::ItemAt(*fence_, *icons_, dpi_, client, uid);

    HMENU menu = CreatePopupMenu();
    if (onItem) {
        AppendMenuW(menu, MF_STRING, kMenuOpenItem, L"打开");
        AppendMenuW(menu, MF_STRING, kMenuRemoveItem, L"从此栅栏移除");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kMenuNewFence, L"新建栅栏");
    AppendMenuW(menu, MF_STRING, kMenuRenameFence, L"重命名栅栏");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"设置…");
    AppendMenuW(menu, MF_STRING, kMenuDeleteFence, L"删除此栅栏");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"退出 WinFence");

    // 菜单要能点击外部消失，需先提到前台（NOACTIVATE 窗口的常规做法）
    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                    screenPt.x, screenPt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case kMenuOpenItem:
        OpenItem(uid);
        break;
    case kMenuRemoveItem:   // §3.3：仅解除归属，不动文件
        FenceService::RemoveItem(*ws_, uid);
        RequestRender();
        ScheduleSave();
        break;
    case kMenuNewFence:
        if (onAction_) onAction_(AppAction::NewFence, 0);
        break;
    case kMenuRenameFence:
        StartRename();
        break;
    case kMenuSettings:
        if (onAction_) onAction_(AppAction::Settings, 0);
        break;
    case kMenuDeleteFence:
        if (onAction_) onAction_(AppAction::DeleteFence, fence_->id);
        break;
    case kMenuExit:
        if (onAction_) onAction_(AppAction::Exit, 0);
        break;
    default:
        break;
    }
}

void FenceWindow::OpenItem(IconUid uid)
{
    auto it = icons_->find(uid);
    if (it == icons_->end() || it->second.orphan) return;
    const std::wstring& path = it->second.sourcePath;
    if (path.empty()) return;   // 命名空间项 PIDL 打开 M5 接入
    ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ============ 标题栏内联重命名 ============

LRESULT CALLBACK FenceWindow::RenameEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<FenceWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        if (self) self->CommitRename(true);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        if (self) self->CommitRename(false);
        return 0;
    }
    if (msg == WM_KILLFOCUS && self && self->renameEdit_ == h) {
        self->CommitRename(true);   // 失焦即提交
        return 0;
    }
    return CallWindowProcW(self ? self->renameOldProc_ : nullptr, h, msg, wp, lp);
}

void FenceWindow::StartRename()
{
    if (renameEdit_ || !hwnd_ || !fence_) return;
    const int titleH = MonitorUtil::DipToPx(fence_->style.titleBarHeightDip, dpi_);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    renameEdit_ = CreateWindowExW(0, L"EDIT", fence_->title.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                  22, 3, rc.right - 90, titleH - 6,
                                  hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!renameEdit_) return;
    SetWindowLongPtrW(renameEdit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    renameOldProc_ = (WNDPROC)SetWindowLongPtrW(renameEdit_, GWLP_WNDPROC,
                                                (LONG_PTR)&FenceWindow::RenameEditProc);
    SendMessageW(renameEdit_, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(renameEdit_, EM_SETSEL, 0, -1);
    // 编辑框需要键盘焦点：短暂前置本窗口（折叠/重锚逻辑会自行恢复层级）
    SetForegroundWindow(hwnd_);
    SetFocus(renameEdit_);
}

void FenceWindow::CommitRename(bool save)
{
    if (!renameEdit_) return;
    if (save && fence_) {
        wchar_t buf[128];
        int n = GetWindowTextW(renameEdit_, buf, 128);
        if (n > 0) {
            fence_->title = buf;
            SetWindowTextW(hwnd_, buf);
            ScheduleSave();
        }
    }
    HWND edit = renameEdit_;
    renameEdit_ = nullptr;   // 先清，防 KILLFOCUS 重入
    DestroyWindow(edit);
    RequestRender();
}

} // namespace winfence
