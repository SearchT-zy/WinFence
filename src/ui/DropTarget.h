// OLE 拖放目标：IDropTarget 实现，只读接收 CF_HDROP（DESIGN.md §3.2 / §4.6）。
// M6：目标无关化——路径经 PathGuard 校验后交给 sink 回调（栅栏/Dock 各自决定归属）。
// 铁律：
//   - 只认 CF_HDROP；DragQueryFileW 枚举路径（中文无损）
//   - Drop 后路径一律过 PathGuard::ValidateDesktopItem（§4.9 安全闸门）
//   - 我们不执行任何文件写操作；DragOver 只返回效果不改模型
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <oleidl.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace winfence {

class DropTarget : public IDropTarget {
public:
    // paths：通过安全校验的路径；返回是否有项被接受（决定是否触发视觉更新）
    using Sink = std::function<bool(const std::vector<std::wstring>& paths)>;

    explicit DropTarget(Sink sink);

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD keys, POINTL pt,
                                        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD keys, POINTL pt,
                                   DWORD* effect) override;

private:
    bool HasHDrop(IDataObject* obj) const;

    std::atomic<ULONG> refs_{1};
    Sink sink_;
};

} // namespace winfence
