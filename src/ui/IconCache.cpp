// 位图级图标缓存实现（DESIGN.md §4.8）。
#include "ui/IconCache.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>

#include "platform/WinUtil.h"
#include "shell/IconExtractor.h"

namespace winfence {

namespace {

// HICON → ID2D1Bitmap：32bpp 顶向下 DIB + 预乘 Alpha（§4.8）
Microsoft::WRL::ComPtr<ID2D1Bitmap> HiconToBitmap(ID2D1DeviceContext* ctx, HICON icon)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1Bitmap> result;
    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii)) return result;

    HBITMAP source = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
    BITMAP bm{};
    if (!source || GetObjectW(source, sizeof(bm), &bm) == 0) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        return result;
    }
    const int w = bm.bmWidth;
    const int h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 顶向下，行序与 D2D 一致
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels((size_t)w * h * 4, 0);
    HDC hdc = CreateCompatibleDC(nullptr);
    int rows = GetDIBits(hdc, source, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);
    DeleteDC(hdc);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (rows != h || w <= 0) return result;

    D2D1_BITMAP_PROPERTIES props{};
    props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;   // 图标位图为预乘
    ctx->CreateBitmap(D2D1::SizeU((UINT)w, (UINT)h), pixels.data(),
                      (UINT)w * 4, props, &result);
    return result;
}

std::wstring ToLower(const std::wstring& s)
{
    std::wstring o = s;
    std::transform(o.begin(), o.end(), o.begin(), ::towlower);
    return o;
}

} // namespace

ID2D1Bitmap* IconCache::GetOrCreate(ID2D1DeviceContext* ctx, const std::wstring& path,
                                    uint64_t fileTime)
{
    if (!ctx || path.empty()) return nullptr;
    IconCacheKey key{ToLower(path), fileTime};

    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.bitmap.Get();

    uint8_t iconIndex = 0;
    HICON icon = IconExtractor::Extract(path, iconIndex);
    if (!icon) return nullptr;

    auto entry = HiconToBitmap(ctx, icon);
    DestroyIcon(icon);
    if (!entry) return nullptr;

    if (cache_.size() >= kMaxEntries) cache_.clear();   // 简化 LRU
    auto [inserted, ok] = cache_.emplace(std::move(key), Entry{entry});
    return ok ? inserted->second.bitmap.Get() : nullptr;
}

void IconCache::Clear()
{
    cache_.clear();
}

} // namespace winfence
