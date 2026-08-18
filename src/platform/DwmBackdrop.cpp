// DWM 背景效果实现（DESIGN.md §4.5 —— 风险点②）。
#include "platform/DwmBackdrop.h"
#include <dwmapi.h>

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

namespace winfence {

BackdropSupport DwmBackdrop::Detect()
{
    // 探测法（不依赖版本 API）：对隐藏探针窗口试设 SYSTEMBACKDROP 属性，
    // 旧系统（< Win11 22H2）返回 E_INVALIDARG → 走回退路径。
    HWND probe = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                                 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!probe) return BackdropSupport::Fallback;
    INT value = DWMSBT_TRANSIENTWINDOW;
    HRESULT hr = DwmSetWindowAttribute(probe, DWMWA_SYSTEMBACKDROP_TYPE,
                                       &value, sizeof(value));
    DestroyWindow(probe);
    return SUCCEEDED(hr) ? BackdropSupport::SystemBackdrop : BackdropSupport::Fallback;
}

bool DwmBackdrop::Apply(HWND hwnd, BackdropSupport support)
{
    if (!hwnd) return false;
    if (support == BackdropSupport::SystemBackdrop) {
        INT value = DWMSBT_TRANSIENTWINDOW;
        return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                               &value, sizeof(value)));
    }
    // Fallback：不设系统背景，半透明视觉由 FenceRenderer 的填充承担（§4.5）
    return true;
}

} // namespace winfence
