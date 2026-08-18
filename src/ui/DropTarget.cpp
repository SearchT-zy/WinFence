// OLE 拖放目标实现（DESIGN.md §3.2 / §4.6 / §4.9）。
#include "ui/DropTarget.h"

#include <shellapi.h>

#include "platform/PathGuard.h"

namespace winfence {

DropTarget::DropTarget(Sink sink) : sink_(std::move(sink))
{
}

ULONG STDMETHODCALLTYPE DropTarget::AddRef()
{
    return ++refs_;
}

ULONG STDMETHODCALLTYPE DropTarget::Release()
{
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
}

HRESULT STDMETHODCALLTYPE DropTarget::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

bool DropTarget::HasHDrop(IDataObject* obj) const
{
    if (!obj) return false;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return SUCCEEDED(obj->QueryGetData(&fmt));
}

HRESULT STDMETHODCALLTYPE DropTarget::DragEnter(IDataObject* obj, DWORD, POINTL,
                                                DWORD* effect)
{
    if (effect) *effect = HasHDrop(obj) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragOver(DWORD, POINTL, DWORD* effect)
{
    if (effect) *effect = DROPEFFECT_COPY;   // 只读收纳，永远"复制"语义
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::Drop(IDataObject* obj, DWORD, POINTL,
                                           DWORD* effect)
{
    if (effect) *effect = DROPEFFECT_NONE;
    if (!obj || !HasHDrop(obj) || !sink_) return S_OK;

    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(obj->GetData(&fmt, &medium)) || !medium.hGlobal) return S_OK;

    std::vector<std::wstring> accepted;
    HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    if (drop) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        wchar_t buf[MAX_PATH * 2];
        for (UINT i = 0; i < count; ++i) {
            if (DragQueryFileW(drop, i, buf, MAX_PATH * 2) > 0) {
                // ★ 安全闸门（§4.9）：存在性/桌面前缀/junction 逃逸/黑名单
                if (PathGuard::ValidateDesktopItem(buf) == PathVerdict::Ok)
                    accepted.push_back(buf);
            }
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);

    if (!accepted.empty())
        sink_(accepted);   // 栅栏/Dock 自行决定归属与后续（渲染/保存）
    return S_OK;
}

} // namespace winfence
