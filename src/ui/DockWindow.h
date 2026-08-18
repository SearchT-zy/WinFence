// Dock 栏（M6）：屏幕底部居中的黑色半透明圆角条（macOS 式）。
// 悬停图标放大 + 名称气泡；单击启动；支持拖入（OLE/内部）、拖出至栅栏、栏内排序。
// 常驻顶层（WS_EX_TOPMOST）——Dock 的预期行为，与栅栏（桌面层）不同。
#pragma once
#include "core/Model.h"

#include <windows.h>
#include <oleidl.h>   // IDropTarget

#include <vector>

namespace winfence {

class ConfigStore;
class IconCache;

class DockWindow {
public:
    static void RegisterClass(HINSTANCE instance);

    bool Create(HINSTANCE instance, Workspace& ws, IconRegistry& icons,
                IconCache& cache, ConfigStore& store);
    void Destroy();
    void RequestRender();
    void Relayout();        // 项目数变化 → 重算尺寸并保持底部居中
    void ScheduleSave();

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Draw();
    void OpenItem(IconUid uid);
    void ShowContextMenu(POINT screenPt);

    HWND hwnd_ = nullptr;
    Workspace* ws_ = nullptr;
    IconRegistry* icons_ = nullptr;
    IconCache* cache_ = nullptr;
    ConfigStore* store_ = nullptr;
    UINT dpi_ = 96;

    int hoverIndex_ = -1;           // 悬停放大项
    bool mouseTracking_ = false;

    // 拖拽手势（与栅栏同方案：鼠标捕获 + FenceDrag 共享状态）
    bool    dragging_ = false;
    int     pressIndex_ = -1;
    POINT   pressPt_{};
    std::vector<IconUid> orderBackup_;

    IDropTarget* drop_ = nullptr;
};

} // namespace winfence
