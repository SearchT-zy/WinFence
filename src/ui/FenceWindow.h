// 栅栏窗口：每个 Fence 一个 HWND（DESIGN.md §3.2/§3.3/§3.4/§4.4/§4.5）。
// M10 v3 双层窗口：面板窗口（亚克力玻璃 + 内容，与面板同尺寸）+
// 阴影窗口（垫在面板正下方，渲染柔和投影）——亚克力与投影并存，无模糊光晕。
#pragma once
#include "core/Model.h"
#include "platform/DwmBackdrop.h"
#include "ui/FenceRenderer.h"
#include "ui/Material.h"

#include <windows.h>

#include <functional>

namespace winfence {

class ConfigStore;
class IconCache;

// 窗口 → App 的动作回调（创建/删除栅栏涉及窗口生命周期，归 App 管）
enum class AppAction { NewFence, DeleteFence, Settings, Exit };
using ActionHandler = std::function<void(AppAction, FenceId)>;

class FenceWindow {
public:
    static void RegisterClass(HINSTANCE instance);

    bool Create(HINSTANCE instance, Fence& fence, Workspace& ws, IconRegistry& icons,
                IconCache& cache, ConfigStore& store);
    void Destroy();
    void RequestRender();
    void ScheduleSave();                       // 模型变更 → 防抖保存

    HWND hwnd() const { return hwnd_; }
    FenceId fenceId() const { return fence_ ? fence_->id : 0; }
    void SetActionHandler(ActionHandler h) { onAction_ = std::move(h); }
    void SyncShadow();   // M10 v3：阴影窗口跟随面板（位置/尺寸/Z 序），重锚后调用

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ShadowWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleShadowMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnNcHitTest(LPARAM lp);
    void ToggleCollapse();
    void StartCollapseAnim();
    void ClampOntoScreen();
    void ShowContextMenu(POINT screenPt);
    void OpenItem(IconUid uid);
    void StartRename();     // 标题栏内联重命名
    void CommitRename(bool save);
    void CreateShadow();    // M10 v3：投影窗口
    void RenderShadow();

    HWND hwnd_ = nullptr;
    Fence* fence_ = nullptr;         // App 保证生命周期
    Workspace* ws_ = nullptr;
    IconRegistry* icons_ = nullptr;
    IconCache* cache_ = nullptr;
    ConfigStore* store_ = nullptr;
    UINT dpi_ = 96;
    LONG padPx_ = 0;                 // M10 v3：双层窗口方案下面板留白恒为 0
    BackdropSupport backdrop_ = BackdropSupport::Fallback;
    FenceRenderer renderer_;
    ActionHandler onAction_;
    IDropTarget* drop_ = nullptr;   // RegisterDragDrop 持引用；Destroy 时 Revoke

    // M10 v3：投影窗口（垫在面板正下方，无 DWM 背景 → 无光晕）
    HWND shadowHwnd_ = nullptr;
    UINT shadowDpi_ = 96;
    SoftShadow shadow_;

    // 折叠动画状态（§3.4）
    bool  animating_ = false;
    LONG  animFrom_ = 0;
    LONG  animTo_ = 0;
    DWORD animStart_ = 0;

    // 模态移动/缩放循环守卫：仅在真实用户拖动结束时才把尺寸落模型，
    // 防止动画期间的 SetWindowPos 引发幻影 WM_EXITSIZEMOVE 覆盖折叠尺寸
    bool  inModalLoop_ = false;

    // 图标悬停状态（渲染高亮用）
    IconUid hoverUid_ = 0;
    bool    mouseTracking_ = false;
    bool    plusHover_ = false;   // 标题栏「＋」按钮悬停（M9 发光反馈）

    // 栏内/跨栏拖拽手势（鼠标捕获方案，§3.3）
    bool    dragging_ = false;
    IconUid pressUid_ = 0;
    POINT   pressPt_{};
    std::vector<IconUid> orderBackup_;   // 拖拽起始顺序（取消时还原）

    // 标题内联重命名
    HWND    renameEdit_ = nullptr;
    WNDPROC renameOldProc_ = nullptr;
    static LRESULT CALLBACK RenameEditProc(HWND, UINT, WPARAM, LPARAM);
};

} // namespace winfence
