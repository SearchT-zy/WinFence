// WinFence 入口（里程碑 1：可见栅栏窗口垂直切片）。
// 完整启动序列见 docs/DESIGN.md §3.1。
#include <windows.h>

#include "app/App.h"
#include "app/SingleInstance.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // manifest 已声明 PMv2；此处运行时兜底（§4.1）
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    winfence::SingleInstance single(L"Local\\WinFence.Singleton");
    if (!single.IsPrimary()) {
        MessageBoxW(nullptr, L"WinFence 已在运行。", L"WinFence",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    winfence::App app;
    return app.Run(instance);
}
