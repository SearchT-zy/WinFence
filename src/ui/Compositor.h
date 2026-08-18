// 合成器：D3D11 / DXGI / DComp / D2D / DWrite 设备与交换链管理（DESIGN.md §4.5）。
// 每个栅栏一个 DComp target + 交换链（DXGI_ALPHA_MODE_PREMULTIPLIED）。
// 设备丢失（DXGI_ERROR_DEVICE_REMOVED）→ 全链重建（里程碑 2 完善）。
// 坑：WS_EX_NOREDIRECTIONBITMAP 窗口没有 WM_PAINT，忘建 DComp 目标 = 纯透明假死。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <unordered_map>

namespace winfence {

class Compositor {
public:
    static Compositor& Get();

    bool Init();      // 进程级设备创建（§3.1 步骤 4）
    void Shutdown();

    bool BindWindow(HWND hwnd, UINT dpi);   // 交换链 + DComp target + 独立 D2D 上下文
    void UnbindWindow(HWND hwnd);

    void Resize(HWND hwnd, UINT w, UINT h); // WM_SIZE：ResizeBuffers + 失效位图
    void SetDpi(HWND hwnd, UINT dpi);       // WM_DPICHANGED：重建带新 DPI 的位图

    // 按需渲染：BeginDraw 返回已 SetTarget/SetDpi 并 BeginDraw 的上下文
    ID2D1DeviceContext* BeginDraw(HWND hwnd);
    void EndDraw(HWND hwnd);                // EndDraw + Present + DComp Commit

    IDWriteFactory* DWrite() const { return dwrite_.Get(); }

private:
    struct Surface {
        UINT dpi = 96;
        Microsoft::WRL::ComPtr<IDXGISwapChain1>      swapchain;
        Microsoft::WRL::ComPtr<IDCompositionTarget>  target;
        Microsoft::WRL::ComPtr<IDCompositionVisual>  visual;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext>   ctx;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1>         bitmap;   // 失效即重建（Resize/SetDpi）
    };

    Microsoft::WRL::ComPtr<ID3D11Device>        d3d_;
    Microsoft::WRL::ComPtr<IDXGIFactory2>       factory_;
    Microsoft::WRL::ComPtr<ID2D1Factory>        d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device>         d2dDevice_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_;
    Microsoft::WRL::ComPtr<IDWriteFactory>      dwrite_;
    std::unordered_map<HWND, Surface>           surfaces_;   // UI 线程独占
};

} // namespace winfence
