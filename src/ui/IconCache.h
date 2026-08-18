// 位图级图标缓存（DESIGN.md §4.8）：路径+文件时间 → ID2D1Bitmap。
// 放在 ui 层而非 core：位图对象依赖 D2D，core 不碰图形 API（分层约定 §1.1）。
// 全部窗口的 D2D 上下文同属 Compositor 的 ID2D1Device，位图可跨窗口使用。
// HICON→位图：GetDIBits 32bpp 顶向下 + 预乘 Alpha（§4.8 丢 Alpha = 黑底坑）。
#pragma once
#include "core/Model.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>

namespace winfence {

struct IconCacheKey {
    std::wstring pathLower;   // 忽略大小写的路径
    uint64_t     fileTime;    // 文件 mtime，图标可能随文件变化
    bool operator==(const IconCacheKey& o) const
    {
        return fileTime == o.fileTime && pathLower == o.pathLower;
    }
};

struct IconCacheKeyHash {
    size_t operator()(const IconCacheKey& k) const
    {
        return std::hash<std::wstring>()(k.pathLower) * 31 +
               std::hash<uint64_t>()(k.fileTime);
    }
};

class IconCache {
public:
    // 命中返回缓存位图；未命中则同步提取（SHGetFileInfo，M2 简化）并缓存。
    // 返回 nullptr 表示提取失败（渲染方画占位）。
    ID2D1Bitmap* GetOrCreate(ID2D1DeviceContext* ctx, const std::wstring& path,
                             uint64_t fileTime);

    void Clear();   // 设备丢失/全量刷新时调用（M3）

private:
    struct Entry { Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap; };
    std::unordered_map<IconCacheKey, Entry, IconCacheKeyHash> cache_;
    static constexpr size_t kMaxEntries = 500;   // LRU 简化：超限全清（§4.8）
};

} // namespace winfence
