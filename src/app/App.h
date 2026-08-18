// 应用生命周期编排（DESIGN.md §3.1 / §3.7）。
// M3：隐藏管理窗口（托盘宿主 + 文件事件接收）+ DesktopWatcher + Reconciler。
#pragma once
#include <windows.h>

#include <memory>
#include <vector>

#include "core/DesktopScanner.h"
#include "core/DesktopWatcher.h"
#include "core/Model.h"
#include "persist/ConfigStore.h"
#include "ui/DockWindow.h"
#include "ui/FenceWindow.h"
#include "ui/IconCache.h"

namespace winfence {

class App {
public:
    int Run(HINSTANCE instance);

private:
    bool SpawnFenceWindow(Fence& fence);
    bool SpawnDock();          // M6：Dock 可见时创建
    void SyncDockVisibility(); // 设置开关后同步创建/销毁
    void HandleAction(AppAction action, FenceId subject);
    void HandleFileEvents(DesktopWatcher::EventBatch* batch);
    void RefreshAllFences();

    bool EnsureAppWindow();          // 隐藏管理窗口（托盘 + 事件路由）
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    void CreateDefaultLayout();
    void Shutdown();

    static LRESULT CALLBACK AppWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleAppMsg(UINT msg, WPARAM wp, LPARAM lp);

    Workspace ws_;
    IconRegistry icons_;
    IconCache iconCache_;
    ConfigStore store_;
    DesktopWatcher watcher_;
    std::vector<std::unique_ptr<FenceWindow>> windows_;
    std::unique_ptr<DockWindow> dock_;
    HINSTANCE instance_ = nullptr;
    HWND appHwnd_ = nullptr;
    UINT wmTaskbarCreated_ = 0;
};

} // namespace winfence
