// AI 分组预览窗口实现（M7b）。
#include "ui/AiPreviewDialog.h"

#include <commctrl.h>

#include <string>

#include "core/FenceService.h"
#include "persist/ConfigStore.h"

namespace winfence {

namespace {

constexpr wchar_t kClass[] = L"WinFenceAiPreviewWnd";
constexpr int kBtnApply  = 300;
constexpr int kBtnCancel = 301;

struct PreviewState {
    HWND dlg = nullptr;
    HWND edit = nullptr;
    Workspace* ws = nullptr;
    const IconRegistry* icons = nullptr;
    ConfigStore* store = nullptr;
    AiPreviewDialog::Plan plan;
    std::function<void()> onApplied;
};

PreviewState g_pv;

void Apply()
{
    FenceService::ApplyGroupPlan(*g_pv.ws, g_pv.plan);
    g_pv.store->ScheduleSave();
    if (g_pv.onApplied) g_pv.onApplied();
    DestroyWindow(g_pv.dlg);
}

LRESULT CALLBACK PvProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == kBtnApply && HIWORD(wp) == BN_CLICKED) {
            Apply();
            return 0;
        }
        if (LOWORD(wp) == kBtnCancel || LOWORD(wp) == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_DESTROY:
        g_pv.dlg = nullptr;
        g_pv.edit = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

void AiPreviewDialog::Show(HINSTANCE instance, HWND owner, Workspace& ws,
                           const IconRegistry& icons, ConfigStore& store,
                           const Plan& plan, std::function<void()> onApplied)
{
    if (g_pv.dlg) {   // 单实例
        SetForegroundWindow(g_pv.dlg);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &PvProc;
        wc.hInstance     = instance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    g_pv.ws    = &ws;
    g_pv.icons = &icons;
    g_pv.store = &store;
    g_pv.plan  = plan;
    g_pv.onApplied = std::move(onApplied);

    const int w = 380, h = 430;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_pv.dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass,
                               L"AI 分组预览（应用前会自动备份，可一键重置）",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               wa.left + (wa.right - wa.left - w) / 2,
                               wa.top + (wa.bottom - wa.top - h) / 2,
                               w, h, owner, nullptr, instance, nullptr);
    if (!g_pv.dlg) return;

    // 组列表（只读多行文本）
    std::wstring text;
    size_t assigned = 0;
    for (const auto& [title, uids] : plan) {
        text += L"【" + title + L"】(" + std::to_wstring(uids.size()) + L")\r\n  ";
        for (size_t i = 0; i < uids.size(); ++i) {
            auto it = icons.find(uids[i]);
            text += (it != icons.end()) ? it->second.displayName : L"?";
            if (i + 1 < uids.size()) text += L" · ";
        }
        text += L"\r\n\r\n";
        assigned += uids.size();
    }
    size_t total = 0;
    for (const auto& [uid, m] : icons)
        if (!m.orphan && !m.sourcePath.empty()) ++total;
    text += L"（共分配 " + std::to_wstring(assigned) + L" / " +
            std::to_wstring(total) + L" 个图标，未分配的保持原状）";

    g_pv.edit = CreateWindowExW(0, L"EDIT", text.c_str(),
                                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                                    WS_VSCROLL,
                                12, 12, w - 40, h - 100,
                                g_pv.dlg, nullptr, instance, nullptr);
    SendMessageW(g_pv.edit, WM_SETFONT,
                 (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    HWND apply = CreateWindowExW(0, L"BUTTON", L"应用分组",
                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 12, h - 76, 110, 30,
                                 g_pv.dlg, (HMENU)(INT_PTR)kBtnApply, instance, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"放弃",
                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  132, h - 76, 90, 30,
                                  g_pv.dlg, (HMENU)(INT_PTR)kBtnCancel, instance, nullptr);
    SendMessageW(apply, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(cancel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    ShowWindow(g_pv.dlg, SW_SHOWNORMAL);
    UpdateWindow(g_pv.dlg);
}

} // namespace winfence
