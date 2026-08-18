// 桌面层锚定：定位 SHELLDLL_DefView、Z 序锚定与重锚（DESIGN.md §3.6 / §4.4 —— 风险点①）。
// 实现前先用 Spy++ 验证插入位置；对照 Rainmeter "On Desktop"（GPL-2，只读思路，docs/CREDITS.md）。
// 禁止：发送未文档化消息 0x052C；SetParent 到 WorkerW（§4.4）。
#pragma once
#include <windows.h>

namespace winfence {

class DesktopAnchor {
public:
    // FindWindowW(L"Progman") → FindWindowExW 向下找 SHELLDLL_DefView，纯查询；
    // 找不到时回退枚举 WorkerW 下的 DefView（动态壁纸场景）。
    static HWND FindIconLayer();

    // 栅栏 HWND 注册（AnchorAll 作用对象）；UI 线程调用
    static void RegisterFence(HWND hwnd);
    static void UnregisterFence(HWND hwnd);

    // 把所有已注册栅栏插到图标层之上、普通应用窗口之下：
    // SetWindowPos(fence, GetWindow(defView, GW_HWNDPREV) 或 HWND_BOTTOM,
    //               0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)
    static void AnchorAll();

    // Explorer 重启（TaskbarCreated 广播）→ 重找图标层并重锚
    static void HandleTaskbarCreated() { AnchorAll(); }
};

} // namespace winfence
