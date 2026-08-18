// 应用生命周期实现（DESIGN.md §3.1 / §3.5）。
// M3 启动序列：OleInit → Compositor → ConfigStore::Load → Scan → ReconcileOnStartup
//   → FenceWindow×N → 隐藏管理窗口(托盘宿主/事件路由) → Watcher::Start → 消息循环。
#include "app/App.h"

#include <ole2.h>
#include <shellapi.h>

#include "core/FenceService.h"
#include "core/Reconciler.h"
#include "platform/PathGuard.h"
#include "platform/WinUtil.h"
#include "shell/DesktopAnchor.h"
#include "shell/MonitorUtil.h"
#include "ui/Compositor.h"
#include "ui/SettingsDialog.h"

namespace winfence {

namespace {

constexpr wchar_t kAppWndClass[] = L"WinFenceAppWnd";
constexpr UINT kTrayId = 1;
constexpr UINT kMenuSettings = 300;
constexpr UINT kMenuNewFence = 301;
constexpr UINT kMenuExit     = 302;

} // namespace

int App::Run(HINSTANCE instance)
{
    instance_ = instance;
    if (FAILED(OleInitialize(nullptr))) return 1;   // §4.6：拖拽必须 OLE

    if (!Compositor::Get().Init()) {
        MessageBoxW(nullptr, L"图形设备初始化失败（D3D / DComp / D2D）。",
                    L"WinFence", MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    // ---- 配置加载（失败链：json → bak → 默认布局，§3.1）----
    store_.SetModel(&ws_, &icons_);
    if (!store_.Load(ws_, icons_)) {
        CreateDefaultLayout();
    }

    // ---- 桌面扫描 + 启动对账（§3.1）----
    Reconciler::ReconcileOnStartup(ws_, icons_, DesktopScanner().Scan());
    store_.ScheduleSave();   // 对账会改模型（新文件注册/orphan），标脏待落盘

    FenceWindow::RegisterClass(instance);

    // ---- 隐藏管理窗口：托盘宿主 + 文件事件路由 ----
    if (!EnsureAppWindow()) {
        MessageBoxW(nullptr, L"管理窗口创建失败，文件监控与托盘不可用。",
                    L"WinFence", MB_OK | MB_ICONWARNING);
    } else {
        AddTrayIcon();
    }

    for (auto& fence : ws_.fences) SpawnFenceWindow(fence);
    DesktopAnchor::AnchorAll();
    if (windows_.empty()) {   // 配置里没有任何栅栏 → 补一个默认
        CreateDefaultLayout();
        for (auto& fence : ws_.fences) SpawnFenceWindow(fence);
        DesktopAnchor::AnchorAll();
    }

    // ---- Dock（M6：可见时创建，常驻顶层）----
    if (ws_.dock.visible) SpawnDock();

    // ---- 文件监控（用户桌面 + 公共桌面，§3.5）----
    if (appHwnd_) {
        watcher_.Start(PathGuard::UserDesktopDir(), PathGuard::PublicDesktopDir(),
                       appHwnd_);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    watcher_.Stop();          // §3.7：先停监控再落盘
    store_.FlushNow();
    Shutdown();
    return (int)msg.wParam;
}

bool App::SpawnFenceWindow(Fence& fence)
{
    auto win = std::make_unique<FenceWindow>();
    win->SetActionHandler([this](AppAction a, FenceId id) { HandleAction(a, id); });
    if (!win->Create(instance_, fence, ws_, icons_, iconCache_, store_))
        return false;
    windows_.push_back(std::move(win));
    return true;
}

void App::HandleAction(AppAction action, FenceId subject)
{
    switch (action) {
    case AppAction::NewFence: {
        const FenceId id = FenceService::CreateFence(ws_, L"新建栅栏");
        Fence* nf = nullptr;
        for (auto& f : ws_.fences)
            if (f.id == id) nf = &f;
        if (nf) {
            const auto mon = MonitorUtil::Primary();
            const UINT dpi = mon.dpiX ? mon.dpiX : 96;
            const LONG off = 24 + (LONG)(ws_.fences.size() % 8) * 32;
            nf->monitorDevice = mon.device;
            nf->posPx = {mon.workArea.left + MonitorUtil::DipToPx((float)off, dpi),
                         mon.workArea.top + MonitorUtil::DipToPx(24.0f, dpi)};
            nf->sizePx = {MonitorUtil::DipToPx(280.0f, dpi),
                          MonitorUtil::DipToPx(320.0f, dpi)};
            nf->collapsedSizePx = {nf->sizePx.cx, MonitorUtil::DipToPx(40.0f, dpi)};
            SpawnFenceWindow(*nf);
        }
        DesktopAnchor::AnchorAll();
        store_.ScheduleSave();
        break;
    }

    case AppAction::DeleteFence:
        for (auto it = windows_.begin(); it != windows_.end();) {
            if ((*it)->fenceId() == subject) {
                (*it)->Destroy();
                it = windows_.erase(it);
            } else {
                ++it;
            }
        }
        for (size_t i = 0; i < ws_.fences.size(); ) {   // 移除模型
            if (ws_.fences[i].id == subject) ws_.fences.erase(ws_.fences.begin() + (long)i);
            else ++i;
        }
        DesktopAnchor::AnchorAll();
        store_.ScheduleSave();
        if (windows_.empty()) PostQuitMessage(0);   // 最后一个栅栏关闭才退出
        break;

    case AppAction::Exit:
        PostQuitMessage(0);
        break;
    }
}

// ============ 文件事件（Watcher → 管理窗口 → 这里，§3.5）============

void App::HandleFileEvents(DesktopWatcher::EventBatch* batch)
{
    if (!batch) return;
    DesktopSnapshot overflowRescan;
    const bool changed =
        Reconciler::ApplyEvents(ws_, icons_, *batch, overflowRescan);
    delete batch;
    if (changed) {
        RefreshAllFences();
        store_.ScheduleSave();
    }
}

void App::RefreshAllFences()
{
    for (auto& w : windows_) w->RequestRender();
    if (dock_) dock_->RequestRender();
}

bool App::SpawnDock()
{
    DockWindow::RegisterClass(instance_);
    auto dock = std::make_unique<DockWindow>();
    if (!dock->Create(instance_, ws_, icons_, iconCache_, store_))
        return false;
    dock_ = std::move(dock);
    return true;
}

void App::SyncDockVisibility()
{
    if (ws_.dock.visible && !dock_) {
        SpawnDock();
    } else if (!ws_.dock.visible && dock_) {
        dock_.reset();
    }
    if (dock_) {
        dock_->Relayout();
        dock_->RequestRender();
    }
}

// ============ 隐藏管理窗口（托盘宿主 + 事件路由）============

bool App::EnsureAppWindow()
{
    if (appHwnd_) return true;
    wmTaskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &App::AppWndProc;
    wc.hInstance     = instance_;
    wc.lpszClassName = kAppWndClass;
    ATOM atom = RegisterClassExW(&wc);

    appHwnd_ = CreateWindowExW(0, kAppWndClass, L"WinFenceApp", WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!appHwnd_) {
        (void)atom;   // 注册成功但创建失败（罕见），交由调用方提示
        return false;
    }
    // 全局热键：Ctrl+Alt+N 新建栅栏（M7a：桌面空白处右键属 Explorer，热键补位）
    RegisterHotKey(appHwnd_, 1, MOD_CONTROL | MOD_ALT, 'N');
    return true;
}

LRESULT CALLBACK App::AppWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->appHwnd_ = h;   // 创建期间 DefWindowProc 需要 this 里的句柄
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (self) return self->HandleAppMsg(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::HandleAppMsg(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_HOTKEY && wp == 1) {    // Ctrl+Alt+N → 新建栅栏
        HandleAction(AppAction::NewFence, 0);
        return 0;
    }
    if (msg == kMsgFileEvents) {          // Watcher 批量事件（所有权转移）
        HandleFileEvents(reinterpret_cast<DesktopWatcher::EventBatch*>(wp));
        return 0;
    }
    if (msg == kMsgTrayIcon) {            // 托盘回调
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP || lp == WM_CONTEXTMENU)
            ShowTrayMenu();
        return 0;
    }
    if (wmTaskbarCreated_ && msg == wmTaskbarCreated_) {   // Explorer 重启
        AddTrayIcon();                     // §4.10：托盘图标重挂
        DesktopAnchor::HandleTaskbarCreated();
        RefreshAllFences();
        return 0;
    }
    if (msg == WM_COMMAND) {              // 托盘菜单命令
        switch (LOWORD(wp)) {
        case kMenuSettings:
            SettingsDialog::ShowSingle(instance_, ws_, icons_, store_, [this]() {
                RefreshAllFences();
                SyncDockVisibility();
            });
            break;
        case kMenuNewFence:
            HandleAction(AppAction::NewFence, 0);
            break;
        case kMenuExit:
            PostQuitMessage(0);
            break;
        default:
            break;
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        UnregisterHotKey(appHwnd_, 1);
        RemoveTrayIcon();
        return 0;
    }
    return DefWindowProcW(appHwnd_, msg, wp, lp);
}

void App::AddTrayIcon()
{
    if (!appHwnd_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = appHwnd_;
    nid.uID              = kTrayId;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kMsgTrayIcon;
    nid.hIcon            = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                             MAKEINTRESOURCEW(101),   // IDI_APP
                                             IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wcscpy_s(nid.szTip, L"WinFence - 桌面图标栅栏");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void App::RemoveTrayIcon()
{
    if (!appHwnd_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = appHwnd_;
    nid.uID    = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void App::ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"设置…");
    AppendMenuW(menu, MF_STRING, kMenuNewFence, L"新建栅栏 (Ctrl+Alt+N)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"退出 WinFence");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(appHwnd_);   // 保证菜单可点击外部消失
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, appHwnd_, nullptr);
    DestroyMenu(menu);
}

void App::CreateDefaultLayout()
{
    const auto mon = MonitorUtil::Primary();
    const UINT dpi = mon.dpiX ? mon.dpiX : 96;

    Fence f;
    f.id = ws_.nextFenceId++;
    f.title = L"新建栅栏";
    f.monitorDevice = mon.device;
    f.posPx = {mon.workArea.left + MonitorUtil::DipToPx(24.0f, dpi),
               mon.workArea.top + MonitorUtil::DipToPx(24.0f, dpi)};
    f.sizePx = {MonitorUtil::DipToPx(280.0f, dpi),
                MonitorUtil::DipToPx(320.0f, dpi)};
    f.collapsedSizePx = {f.sizePx.cx, MonitorUtil::DipToPx(40.0f, dpi)};
    ws_.fences.push_back(f);
}

void App::Shutdown()
{
    SettingsDialog::CloseIfOpen();
    if (appHwnd_) {
        DestroyWindow(appHwnd_);   // → RemoveTrayIcon
        appHwnd_ = nullptr;
    }
    windows_.clear();
    Compositor::Get().Shutdown();
    OleUninitialize();
}

} // namespace winfence
