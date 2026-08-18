// 栅栏间虚拟拖拽共享状态（§3.3：内部拖动 = 纯模型操作，非 OLE，不涉文件）。
// 单例 + 仅 UI 线程读写（§1.3 单写线程模型），跨窗口经消息通信。
#pragma once
#include "core/Model.h"
#include <windows.h>

namespace winfence {

// 栅栏窗口间内部消息
inline constexpr UINT kMsgFenceRefresh   = WM_APP + 5;   // 请求重绘（拖拽高亮变化）
inline constexpr UINT kMsgFenceDropItem  = WM_APP + 6;   // wp=源fenceId lp=uid，跨栏落子

// 拖拽合法目标窗口类名（栅栏 / Dock）
inline constexpr wchar_t kFenceWndClass[] = L"WinFenceFenceWnd";
inline constexpr wchar_t kDockWndClass[]  = L"WinFenceDockWnd";

class FenceDrag {
public:
    static FenceDrag& Get() { static FenceDrag s; return s; }

    bool    active = false;   // 拖拽进行中
    HWND    source = nullptr; // 拖拽源栅栏窗口
    IconUid uid    = 0;       // 被拖图标
    HWND    hover  = nullptr; // 当前悬停目标窗口（== source 时为栏内排序）
};

} // namespace winfence
