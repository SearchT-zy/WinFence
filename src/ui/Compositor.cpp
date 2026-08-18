// 合成器实现（DESIGN.md §4.5）。
#include "ui/Compositor.h"
#include <algorithm>

namespace winfence {

using Microsoft::WRL::ComPtr;

Compositor& Compositor::Get()
{
    static Compositor instance;
    return instance;
}

bool Compositor::Init()
{
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr)) {
        // 无独显/驱动异常时回退 WARP 软渲染
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
        if (FAILED(hr)) return false;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3d_.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory_)))) return false;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory),
                                 reinterpret_cast<void**>(d2dFactory_.GetAddressOf())))) {
        return false;
    }
    ComPtr<ID2D1Factory1> d2dFactory1;   // CreateDevice 在 ID2D1Factory1（d2d1_1.h）
    if (FAILED(d2dFactory_.As(&d2dFactory1))) return false;
    if (FAILED(d2dFactory1->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) return false;
    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        return false;
    }
    return true;
}

void Compositor::Shutdown()
{
    surfaces_.clear();
    dwrite_.Reset();
    dcomp_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    factory_.Reset();
    d3d_.Reset();
}

bool Compositor::BindWindow(HWND hwnd, UINT dpi)
{
    if (!hwnd || !dcomp_ || !factory_) return false;
    RECT rc{};
    GetClientRect(hwnd, &rc);

    Surface s;
    s.dpi = dpi ? dpi : 96;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width       = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    desc.Height      = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));
    desc.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling     = DXGI_SCALING_STRETCH;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;   // §4.5 每像素透明的前提
    if (FAILED(factory_->CreateSwapChainForComposition(d3d_.Get(), &desc, nullptr,
                                                       &s.swapchain))) {
        return false;
    }
    if (FAILED(dcomp_->CreateTargetForHwnd(hwnd, TRUE, &s.target))) return false;
    if (FAILED(dcomp_->CreateVisual(&s.visual))) return false;
    s.visual->SetContent(s.swapchain.Get());
    s.target->SetRoot(s.visual.Get());
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &s.ctx))) {
        return false;
    }
    surfaces_[hwnd] = std::move(s);
    dcomp_->Commit();
    return true;
}

void Compositor::UnbindWindow(HWND hwnd)
{
    auto it = surfaces_.find(hwnd);
    if (it == surfaces_.end()) return;
    if (it->second.target) it->second.target->SetRoot(nullptr);
    if (dcomp_) dcomp_->Commit();
    surfaces_.erase(it);
}

void Compositor::Resize(HWND hwnd, UINT w, UINT h)
{
    auto it = surfaces_.find(hwnd);
    if (it == surfaces_.end()) return;
    Surface& s = it->second;
    s.ctx->SetTarget(nullptr);
    s.bitmap.Reset();
    s.swapchain->ResizeBuffers(0, std::max<UINT>(1, w), std::max<UINT>(1, h),
                               DXGI_FORMAT_UNKNOWN, 0);
}

void Compositor::SetDpi(HWND hwnd, UINT dpi)
{
    auto it = surfaces_.find(hwnd);
    if (it == surfaces_.end() || it->second.dpi == dpi) return;
    it->second.dpi = dpi;
    it->second.bitmap.Reset();   // 位图按 DPI 烘焙，失效重建
}

ID2D1DeviceContext* Compositor::BeginDraw(HWND hwnd)
{
    auto it = surfaces_.find(hwnd);
    if (it == surfaces_.end()) return nullptr;
    Surface& s = it->second;
    if (!s.bitmap) {
        ComPtr<IDXGISurface> surface;
        if (FAILED(s.swapchain->GetBuffer(0, IID_PPV_ARGS(&surface))) || !surface) return nullptr;
        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = static_cast<FLOAT>(s.dpi);
        props.dpiY = static_cast<FLOAT>(s.dpi);
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if (FAILED(s.ctx->CreateBitmapFromDxgiSurface(surface.Get(), props, &s.bitmap))) {
            return nullptr;
        }
    }
    s.ctx->SetTarget(s.bitmap.Get());
    s.ctx->SetDpi(static_cast<FLOAT>(s.dpi), static_cast<FLOAT>(s.dpi));
    s.ctx->BeginDraw();
    return s.ctx.Get();
}

void Compositor::EndDraw(HWND hwnd)
{
    auto it = surfaces_.find(hwnd);
    if (it == surfaces_.end()) return;
    Surface& s = it->second;
    s.ctx->EndDraw();
    s.swapchain->Present(0, 0);
    if (dcomp_) dcomp_->Commit();
}

} // namespace winfence
