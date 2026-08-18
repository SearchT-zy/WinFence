// 中文设置弹窗实现（MVP 第 5 项 + M5 自启 + M6 Dock/隐藏桌面图标）。
// comctl32 v6（manifest 已声明）：TrackBar / ComboBox / Button。
#include "ui/SettingsDialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <string>

#include "persist/ConfigStore.h"
#include "platform/DesktopIcons.h"

namespace winfence {

namespace {

constexpr wchar_t kClass[] = L"WinFenceSettingsWnd";
constexpr int kCtrlCombo    = 100;
constexpr int kCtrlOpacity  = 101;
constexpr int kCtrlRadius   = 102;
constexpr int kCtrlApply    = 103;
constexpr int kCtrlAutoRun  = 104;
constexpr int kCtrlDock     = 105;
constexpr int kCtrlHideDesk = 106;
constexpr int kLblOpacity   = 110;
constexpr int kLblRadius    = 111;

// ---- 开机自启：HKCU Run 键（官方途径，无需管理员）----
constexpr wchar_t kRunKey[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"WinFence";

bool IsAutostartEnabled()
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    const LSTATUS st = RegQueryValueExW(k, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

void SetAutostart(bool enable)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t path[MAX_PATH * 2]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring quoted = L"\"";
        quoted += path;
        quoted += L"\"";
        RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                       (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunValue);
    }
    RegCloseKey(k);
}

struct DialogState {
    HWND dlg = nullptr;
    HWND combo = nullptr;
    HWND trackOpacity = nullptr;
    HWND trackRadius = nullptr;
    HWND lblOpacity = nullptr;
    HWND lblRadius = nullptr;
    HWND chkAutoRun = nullptr;
    HWND chkDock = nullptr;
    HWND chkHideDesk = nullptr;
    Workspace* ws = nullptr;
    ConfigStore* store = nullptr;
    std::function<void()> onApplied;
    UINT dpi = 96;
};

DialogState g_state;

int S(int dip) { return (int)(dip * g_state.dpi / 96.0f + 0.5f); }

void ApplyFont(HWND ctrl)
{
    SendMessageW(ctrl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

void UpdateValueLabels()
{
    const int opacity = 20 + (int)SendMessageW(g_state.trackOpacity, TBM_GETPOS, 0, 0);
    const int radius  = (int)SendMessageW(g_state.trackRadius, TBM_GETPOS, 0, 0);
    wchar_t buf[64];
    swprintf_s(buf, L"透明度：%d%%", opacity);
    SetWindowTextW(g_state.lblOpacity, buf);
    swprintf_s(buf, L"圆角：%d", radius);
    SetWindowTextW(g_state.lblRadius, buf);
}

void ApplyAndSave()
{
    const int comboSel = (int)SendMessageW(g_state.combo, CB_GETCURSEL, 0, 0);
    const int opacity  = 20 + (int)SendMessageW(g_state.trackOpacity, TBM_GETPOS, 0, 0);
    const int radius   = (int)SendMessageW(g_state.trackRadius, TBM_GETPOS, 0, 0);

    BackdropType backdrop = BackdropType::Acrylic;
    if (comboSel == 1) backdrop = BackdropType::Translucent;
    if (comboSel == 2) backdrop = BackdropType::None;

    FenceStyle st = g_state.ws->defaultStyle;
    st.backdrop = backdrop;
    st.opacity  = opacity / 100.0f;
    st.cornerRadiusDip = (float)radius;
    g_state.ws->defaultStyle = st;
    for (auto& f : g_state.ws->fences) f.style = st;   // 应用到全部现有栅栏

    SetAutostart(SendMessageW(g_state.chkAutoRun, BM_GETCHECK, 0, 0) == BST_CHECKED);

    g_state.ws->dock.visible =
        (SendMessageW(g_state.chkDock, BM_GETCHECK, 0, 0) == BST_CHECKED);

    const bool hideDesk =
        (SendMessageW(g_state.chkHideDesk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (IsHideDesktopIcons() != hideDesk)
        SetHideDesktopIcons(hideDesk);   // 状态变化时才重启 Explorer

    g_state.store->ScheduleSave();
    if (g_state.onApplied) g_state.onApplied();
    DestroyWindow(g_state.dlg);
}

LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_HSCROLL:   // TrackBar 拖动 → 实时数值
        if ((HWND)lp == g_state.trackOpacity || (HWND)lp == g_state.trackRadius)
            UpdateValueLabels();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == kCtrlApply && HIWORD(wp) == BN_CLICKED) {
            ApplyAndSave();
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {   // Esc 关闭
            DestroyWindow(h);
            return 0;
        }
        break;

    case WM_DPICHANGED: {
        g_state.dpi = HIWORD(wp);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }

    case WM_DESTROY:
        g_state.dlg = nullptr;
        return 0;

    default:
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

void SettingsDialog::ShowSingle(HINSTANCE instance, Workspace& ws, ConfigStore& store,
                                std::function<void()> onApplied)
{
    if (g_state.dlg) {   // 单实例：已打开则前置
        SetForegroundWindow(g_state.dlg);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &DlgProc;
        wc.hInstance     = instance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    g_state.ws    = &ws;
    g_state.store = &store;
    g_state.onApplied = std::move(onApplied);

    const int w = S(310), h = S(330);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top + (wa.bottom - wa.top - h) / 2;

    g_state.dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"WinFence 设置",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  x, y, w, h, nullptr, nullptr, instance, nullptr);
    if (!g_state.dlg) return;
    g_state.dpi = GetDpiForWindow(g_state.dlg);
    if (!g_state.dpi) g_state.dpi = 96;

    // ---- 控件布局（全部按 DPI 折算）----
    HWND label = CreateWindowExW(0, L"STATIC", L"背景效果：", WS_CHILD | WS_VISIBLE,
                                 S(16), S(18), S(70), S(20),
                                 g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(label);
    g_state.combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                    S(92), S(16), S(190), S(100),
                                    g_state.dlg, (HMENU)(INT_PTR)kCtrlCombo, instance, nullptr);
    ApplyFont(g_state.combo);
    ComboBox_AddString(g_state.combo, L"亚克力（Win11 毛玻璃）");
    ComboBox_AddString(g_state.combo, L"半透明");
    ComboBox_AddString(g_state.combo, L"无");

    g_state.lblOpacity = CreateWindowExW(0, L"STATIC", L"透明度：", WS_CHILD | WS_VISIBLE,
                                         S(16), S(60), S(120), S(20),
                                         g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(g_state.lblOpacity);
    g_state.trackOpacity = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                           WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                           S(16), S(84), S(270), S(30),
                                           g_state.dlg, (HMENU)(INT_PTR)kCtrlOpacity, instance, nullptr);
    SendMessageW(g_state.trackOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(0, 80));
    SendMessageW(g_state.trackOpacity, TBM_SETPOS, TRUE,
                 (int)(ws.defaultStyle.opacity * 100) - 20);

    g_state.lblRadius = CreateWindowExW(0, L"STATIC", L"圆角：", WS_CHILD | WS_VISIBLE,
                                        S(16), S(124), S(120), S(20),
                                        g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(g_state.lblRadius);
    g_state.trackRadius = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                          WS_CHILD | WS_VISIBLE | TBS_HORZ,
                                          S(16), S(148), S(270), S(30),
                                          g_state.dlg, (HMENU)(INT_PTR)kCtrlRadius, instance, nullptr);
    SendMessageW(g_state.trackRadius, TBM_SETRANGE, TRUE, MAKELPARAM(0, 24));
    SendMessageW(g_state.trackRadius, TBM_SETPOS, TRUE,
                 (int)ws.defaultStyle.cornerRadiusDip);

    g_state.chkDock = CreateWindowExW(0, L"BUTTON", L"启用 Dock 栏（屏幕底部）",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      S(16), S(192), S(270), S(22),
                                      g_state.dlg, (HMENU)(INT_PTR)kCtrlDock, instance, nullptr);
    ApplyFont(g_state.chkDock);
    SendMessageW(g_state.chkDock, BM_SETCHECK,
                 ws.dock.visible ? BST_CHECKED : BST_UNCHECKED, 0);

    g_state.chkHideDesk = CreateWindowExW(
        0, L"BUTTON", L"隐藏桌面图标（清空桌面，需重启资源管理器）",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        S(16), S(220), S(280), S(22),
        g_state.dlg, (HMENU)(INT_PTR)kCtrlHideDesk, instance, nullptr);
    ApplyFont(g_state.chkHideDesk);
    SendMessageW(g_state.chkHideDesk, BM_SETCHECK,
                 IsHideDesktopIcons() ? BST_CHECKED : BST_UNCHECKED, 0);

    g_state.chkAutoRun = CreateWindowExW(0, L"BUTTON", L"开机自动启动 WinFence",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         S(16), S(248), S(270), S(22),
                                         g_state.dlg, (HMENU)(INT_PTR)kCtrlAutoRun, instance, nullptr);
    ApplyFont(g_state.chkAutoRun);
    SendMessageW(g_state.chkAutoRun, BM_SETCHECK,
                 IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    HWND apply = CreateWindowExW(0, L"BUTTON", L"应用并保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 S(16), S(280), S(110), S(28),
                                 g_state.dlg, (HMENU)(INT_PTR)kCtrlApply, instance, nullptr);
    ApplyFont(apply);

    int sel = 0;
    if (ws.defaultStyle.backdrop == BackdropType::Translucent) sel = 1;
    if (ws.defaultStyle.backdrop == BackdropType::None)         sel = 2;
    ComboBox_SetCurSel(g_state.combo, sel);
    UpdateValueLabels();

    ShowWindow(g_state.dlg, SW_SHOWNORMAL);
    UpdateWindow(g_state.dlg);
}

void SettingsDialog::CloseIfOpen()
{
    if (g_state.dlg) DestroyWindow(g_state.dlg);
}

} // namespace winfence
