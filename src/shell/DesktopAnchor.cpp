// 桌面层锚定实现（DESIGN.md §3.6 / §4.4 —— 风险点①）。
// 纯查询定位图标层，不发送未文档化消息 0x052C，不 SetParent 到 WorkerW。
#include "shell/DesktopAnchor.h"
#include <algorithm>
#include <vector>

namespace winfence {

namespace {
// 栅栏 HWND 注册表（UI 线程独占访问，§1.3 单写线程模型）
std::vector<HWND> g_fenceHwnds;
}

HWND DesktopAnchor::FindIconLayer()
{
    // 路径 1：常规 —— Progman → SHELLDLL_DefView（图标列表的宿主）
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        HWND defView = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) return defView;
    }
    // 路径 2：动态壁纸等场景下 DefView 可能挂在某个 WorkerW 下，纯枚举查找
    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
        HWND defView = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) return defView;
    }
    return nullptr;
}

void DesktopAnchor::RegisterFence(HWND hwnd)
{
    if (hwnd) g_fenceHwnds.push_back(hwnd);
}

void DesktopAnchor::UnregisterFence(HWND hwnd)
{
    g_fenceHwnds.erase(std::remove(g_fenceHwnds.begin(), g_fenceHwnds.end(), hwnd),
                       g_fenceHwnds.end());
}

void DesktopAnchor::AnchorAll()
{
    if (g_fenceHwnds.empty()) return;
    HWND defView = FindIconLayer();
    HWND insertAfter = HWND_BOTTOM;
    if (defView) {
        // 图标层正上方的窗口：把栅栏插到它下面 = 恰好在图标层之上（§3.6 配方）
        HWND above = GetWindow(defView, GW_HWNDPREV);
        if (above) insertAfter = above;
        // above 为空（罕见：图标层之上没有任何窗口）时退 HWND_BOTTOM，
        // 由 60s 周期重锚与 TaskbarCreated 事件纠正。
    }
    for (HWND fence : g_fenceHwnds) {
        if (!fence) continue;
        SetWindowPos(fence, insertAfter, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

} // namespace winfence
