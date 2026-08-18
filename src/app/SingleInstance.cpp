// 单实例互斥体实现（DESIGN.md §3.1 / §4.10）。
#include "app/SingleInstance.h"
#include <windows.h>

namespace winfence {

SingleInstance::SingleInstance(std::wstring name)
{
    HANDLE handle = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!handle) {           // 创建失败按"已有实例"处理，保守退出
        primary_ = false;
        return;
    }
    handle_  = handle;
    primary_ = (GetLastError() != ERROR_ALREADY_EXISTS);
}

SingleInstance::~SingleInstance()
{
    if (handle_) {
        ReleaseMutex(static_cast<HANDLE>(handle_));
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

void SingleInstance::ActivateExisting()
{
    // 里程碑 2 接管：找到已有实例的窗口并打开设置页。当前仅投递空消息探测。
    HWND hwnd = FindWindowW(L"WinFenceFenceWnd", nullptr);
    if (hwnd) PostMessageW(hwnd, WM_NULL, 0, 0);
}

} // namespace winfence
