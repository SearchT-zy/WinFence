// Dock 栏（M6 + M10）：屏幕底部居中圆角条（macOS 式）。
// M10 Apple 质感：柔和投影 / 发丝描边 / 抛物线悬停放大（邻图标联动）/
// squircle 圆角图标遮罩 + 玻璃反光 / 倒影 / 描边名称气泡。
// 常驻顶层（WS_EX_TOPMOST）——Dock 的预期行为，与栅栏（桌面层）不同。
#pragma once
#include "core/Model.h"
#include "ui/Material.h"

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
    int bubbleIndex_ = -1;          // 气泡显示项（动画回退期仍显示）
    float hoverT_ = 0.0f;           // 气泡/光晕动画插值 0..1
    float mouseXDip_ = -1.0e9f;     // 鼠标 X（面板 DIP 坐标；-1e9 = 离开）→ 抛物线放大
    bool animating_ = false;
    bool mouseTracking_ = false;

    // 拖拽手势（与栅栏同方案：鼠标捕获 + FenceDrag 共享状态）
    bool    dragging_ = false;
    int     pressIndex_ = -1;
    POINT   pressPt_{};
    std::vector<IconUid> orderBackup_;

    IDropTarget* drop_ = nullptr;

    SoftShadow shadow_;             // 投影缓存（按条尺寸/圆角）
};

} // namespace winfence
