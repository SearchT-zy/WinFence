// 文件监控：ReadDirectoryChangesW ×2（用户/公共桌面）+ IOCP + 200ms 静默防抖。
// 细节与坑见 DESIGN.md §3.5 / §4.7。
// 铁律：本线程绝不直接改模型，只 PostMessage(kMsgFileEvents) 回 UI 线程（§1.3）。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace winfence {

// UI 线程通信消息（发给 App 的隐藏管理窗口）
inline constexpr UINT kMsgFileEvents = WM_APP + 1;   // wParam=EventBatch*（接收方 delete）
inline constexpr UINT kMsgTrayIcon   = WM_APP + 3;   // 托盘回调（App 窗口）

enum class FileEventKind { Added, Removed, RenamedFrom, RenamedTo, Modified, Overflow };

struct FileEvent {
    FileEventKind kind;
    std::wstring  path;             // 事件原始路径（用户/公共桌面之一）
    uint64_t      timestampMs = 0;
};

class DesktopWatcher {
public:
    using EventBatch = std::vector<FileEvent>;

    // 两个目录各开一个浅层监控句柄（不递归，防 junction 循环，§4.7）。
    // target：接收 kMsgFileEvents 的窗口（App 隐藏管理窗口）。
    bool Start(const std::wstring& userDesktop, const std::wstring& publicDesktop,
               HWND target);
    void Stop();   // 信号 + 线程汇合（§3.7）；可重入

private:
    DWORD Run();
    static DWORD WINAPI ThreadProc(LPVOID self);

    HANDLE thread_    = nullptr;
    HANDLE iocp_      = nullptr;
    HANDLE stopEvent_ = nullptr;
    HWND   target_    = nullptr;
    std::wstring dirs_[2];
};

} // namespace winfence
