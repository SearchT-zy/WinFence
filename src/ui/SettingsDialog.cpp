// 中文设置弹窗实现（M7c 视觉重做：微软雅黑 + 分组框 + 白底净色 + 统一栅格）。
// AI 请求在工作线程执行（WinHTTP 同步 60s 超时），结果经 kMsgAiDone 回投 UI 线程。
#include "ui/SettingsDialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <thread>

#include "ai/AiGrouping.h"
#include "core/FenceService.h"
#include "persist/ConfigStore.h"
#include "platform/DesktopIcons.h"
#include "ui/AiPreviewDialog.h"

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
constexpr int kCtrlAiProv   = 107;
constexpr int kCtrlAiModel  = 108;
constexpr int kCtrlAiKey    = 109;
constexpr int kCtrlAiRun    = 110;
constexpr int kCtrlAiReset  = 111;
constexpr int kCtrlAiStatus = 113;
constexpr int kGrpLook      = 120;
constexpr int kGrpDesk      = 121;
constexpr int kGrpAi        = 122;

constexpr UINT kMsgAiDone = WM_APP + 7;   // AI 工作线程 → 设置窗口

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
    HWND cmbAiProv = nullptr;
    HWND edtAiModel = nullptr;
    HWND edtAiKey = nullptr;
    HWND btnAiRun = nullptr;
    HWND btnAiReset = nullptr;
    HWND lblAiStatus = nullptr;
    HFONT font = nullptr;
    HBRUSH whiteBrush = nullptr;
    HINSTANCE inst = nullptr;
    Workspace* ws = nullptr;
    const IconRegistry* icons = nullptr;
    ConfigStore* store = nullptr;
    std::function<void()> onApplied;
    UINT dpi = 96;
};

DialogState g_state;

int S(int dip) { return (int)(dip * g_state.dpi / 96.0f + 0.5f); }

void ApplyFont(HWND ctrl)
{
    if (g_state.font) SendMessageW(ctrl, WM_SETFONT, (WPARAM)g_state.font, TRUE);
}

HWND MkCtrl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y,
            int w, int h, int id = 0)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h), g_state.dlg,
                             id ? (HMENU)(INT_PTR)id : nullptr, g_state.inst, nullptr);
    ApplyFont(c);
    return c;
}

void UpdateValueLabels()
{
    const int opacity = 20 + (int)SendMessageW(g_state.trackOpacity, TBM_GETPOS, 0, 0);
    const int radius  = (int)SendMessageW(g_state.trackRadius, TBM_GETPOS, 0, 0);
    wchar_t buf[64];
    swprintf_s(buf, L"透明度 %d%%", opacity);
    SetWindowTextW(g_state.lblOpacity, buf);
    swprintf_s(buf, L"圆角 %d", radius);
    SetWindowTextW(g_state.lblRadius, buf);
}

void SetAiStatus(const wchar_t* text)
{
    if (g_state.lblAiStatus) SetWindowTextW(g_state.lblAiStatus, text);
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
    for (auto& f : g_state.ws->fences) f.style = st;

    SetAutostart(SendMessageW(g_state.chkAutoRun, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_state.ws->dock.visible =
        (SendMessageW(g_state.chkDock, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool hideDesk =
        (SendMessageW(g_state.chkHideDesk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (IsHideDesktopIcons() != hideDesk)
        SetHideDesktopIcons(hideDesk);

    g_state.store->ScheduleSave();
    if (g_state.onApplied) g_state.onApplied();
    DestroyWindow(g_state.dlg);
}

// ---- AI 整理：唯一入口（手动点击，§6.6）。工作线程跑网络，结果回投 ----
void StartAiGrouping()
{
    const int provSel = (int)SendMessageW(g_state.cmbAiProv, CB_GETCURSEL, 0, 0);
    g_state.ws->ai.provider = (provSel == 1) ? L"ollama" : L"deepseek";
    wchar_t model[128]{}, key[256]{};
    GetWindowTextW(g_state.edtAiModel, model, 128);
    GetWindowTextW(g_state.edtAiKey, key, 256);
    g_state.ws->ai.model = model;
    if (g_state.ws->ai.provider == L"deepseek" && key[0])
        SaveApiKey(key);

    EnableWindow(g_state.btnAiRun, FALSE);
    SetAiStatus(L"请求中…");

    const HWND dlg = g_state.dlg;
    Workspace wsCopy;
    wsCopy.ai = g_state.ws->ai;
    IconRegistry iconsCopy = *g_state.icons;

    std::thread([dlg, wsCopy = std::move(wsCopy), iconsCopy = std::move(iconsCopy)]() {
        auto* result = new AiJobResult(RunAiGrouping(wsCopy, iconsCopy));
        if (!PostMessageW(dlg, kMsgAiDone, result->ok ? 1 : 0, (LPARAM)result))
            delete result;
    }).detach();
}

void OnAiDone(WPARAM wp, LPARAM lp)
{
    EnableWindow(g_state.btnAiRun, TRUE);
    std::unique_ptr<AiJobResult> result((AiJobResult*)lp);
    if (!result) return;
    if (!result->ok) {
        SetAiStatus(L"失败");
        MessageBoxW(g_state.dlg, result->errZh.c_str(), L"AI 整理失败",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    SetAiStatus(L"已生成");
    AiPreviewDialog::Show(g_state.inst, g_state.dlg, *g_state.ws, *g_state.icons,
                          *g_state.store, result->groups, [] {
                              if (g_state.onApplied) g_state.onApplied();
                          });
}

LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_HSCROLL:
        if ((HWND)lp == g_state.trackOpacity || (HWND)lp == g_state.trackRadius)
            UpdateValueLabels();
        return 0;

    case WM_CTLCOLORSTATIC:   // 白底 + 深灰文字（含分组框/标签）
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        SetTextColor((HDC)wp, RGB(56, 56, 60));
        if (g_state.whiteBrush) return (LRESULT)g_state.whiteBrush;
        break;

    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) {
            if (LOWORD(wp) == kCtrlApply) { ApplyAndSave(); return 0; }
            if (LOWORD(wp) == kCtrlAiRun) { StartAiGrouping(); return 0; }
            if (LOWORD(wp) == kCtrlAiReset) {
                if (g_state.ws->aiBackup.present) {
                    FenceService::ResetAiGrouping(*g_state.ws);
                    g_state.store->ScheduleSave();
                    if (g_state.onApplied) g_state.onApplied();
                    SetAiStatus(L"已重置");
                } else {
                    SetAiStatus(L"无可重置");
                }
                return 0;
            }
        }
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == kCtrlAiProv) {
            const int sel = (int)SendMessageW(g_state.cmbAiProv, CB_GETCURSEL, 0, 0);
            EnableWindow(g_state.edtAiKey, sel == 0);   // Ollama 本地无需 Key
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        break;

    case kMsgAiDone:
        OnAiDone(wp, lp);
        return 0;

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

void SettingsDialog::ShowSingle(HINSTANCE instance, Workspace& ws,
                                const IconRegistry& icons, ConfigStore& store,
                                std::function<void()> onApplied)
{
    if (g_state.dlg) {
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
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);   // 净白底
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    g_state.ws    = &ws;
    g_state.icons = &icons;
    g_state.store = &store;
    g_state.inst  = instance;
    g_state.onApplied = std::move(onApplied);
    if (!g_state.font)
        g_state.font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                   DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!g_state.whiteBrush)
        g_state.whiteBrush = CreateSolidBrush(RGB(255, 255, 255));

    const int w = S(340), h = S(500);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_state.dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"WinFence 设置",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  wa.left + (wa.right - wa.left - w) / 2,
                                  wa.top + (wa.bottom - wa.top - h) / 2,
                                  w, h, nullptr, nullptr, instance, nullptr);
    if (!g_state.dlg) return;
    g_state.dpi = GetDpiForWindow(g_state.dlg);
    if (!g_state.dpi) g_state.dpi = 96;
    SendMessageW(g_state.dlg, WM_SETFONT, (WPARAM)g_state.font, TRUE);

    // ════ 分组一：外观 ════
    MkCtrl(L"BUTTON", L"外观", BS_GROUPBOX, 12, 8, 304, 150, kGrpLook);
    MkCtrl(L"STATIC", L"背景效果", SS_LEFT, 26, 32, 56, 18);
    g_state.combo = MkCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 88, 28, 212, 100, kCtrlCombo);
    ComboBox_AddString(g_state.combo, L"亚克力（Win11 毛玻璃）");
    ComboBox_AddString(g_state.combo, L"半透明");
    ComboBox_AddString(g_state.combo, L"无");

    g_state.lblOpacity = MkCtrl(L"STATIC", L"透明度", SS_LEFT, 26, 68, 80, 18);
    g_state.trackOpacity = MkCtrl(L"BUTTON", L"", TBS_HORZ, 26, 88, 274, 26, kCtrlOpacity);
    SendMessageW(g_state.trackOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(0, 80));
    SendMessageW(g_state.trackOpacity, TBM_SETPOS, TRUE,
                 (int)(ws.defaultStyle.opacity * 100) - 20);

    g_state.lblRadius = MkCtrl(L"STATIC", L"圆角", SS_LEFT, 26, 116, 80, 18);
    g_state.trackRadius = MkCtrl(L"BUTTON", L"", TBS_HORZ, 26, 134, 274, 26, kCtrlRadius);
    SendMessageW(g_state.trackRadius, TBM_SETRANGE, TRUE, MAKELPARAM(0, 24));
    SendMessageW(g_state.trackRadius, TBM_SETPOS, TRUE,
                 (int)ws.defaultStyle.cornerRadiusDip);

    // ════ 分组二：桌面 ════
    MkCtrl(L"BUTTON", L"桌面", BS_GROUPBOX, 12, 164, 304, 96, kGrpDesk);
    g_state.chkDock = MkCtrl(L"BUTTON", L"启用 Dock 栏（屏幕底部）",
                             BS_AUTOCHECKBOX, 26, 186, 272, 20, kCtrlDock);
    SendMessageW(g_state.chkDock, BM_SETCHECK,
                 ws.dock.visible ? BST_CHECKED : BST_UNCHECKED, 0);
    g_state.chkHideDesk = MkCtrl(L"BUTTON", L"隐藏桌面图标（需重启资源管理器）",
                                 BS_AUTOCHECKBOX, 26, 210, 278, 20, kCtrlHideDesk);
    SendMessageW(g_state.chkHideDesk, BM_SETCHECK,
                 IsHideDesktopIcons() ? BST_CHECKED : BST_UNCHECKED, 0);
    g_state.chkAutoRun = MkCtrl(L"BUTTON", L"开机自动启动 WinFence",
                                BS_AUTOCHECKBOX, 26, 234, 272, 20, kCtrlAutoRun);
    SendMessageW(g_state.chkAutoRun, BM_SETCHECK,
                 IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    // ════ 分组三：AI 智能分组 ════
    MkCtrl(L"BUTTON", L"AI 智能分组（仅手动触发 · 只上传文件名）",
           BS_GROUPBOX, 12, 266, 304, 150, kGrpAi);
    MkCtrl(L"STATIC", L"服务", SS_LEFT, 26, 290, 32, 18);
    g_state.cmbAiProv = MkCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 62, 286, 110, 100, kCtrlAiProv);
    ComboBox_AddString(g_state.cmbAiProv, L"DeepSeek 云端");
    ComboBox_AddString(g_state.cmbAiProv, L"Ollama 本地");
    ComboBox_SetCurSel(g_state.cmbAiProv, ws.ai.provider == L"ollama" ? 1 : 0);

    MkCtrl(L"STATIC", L"模型", SS_LEFT, 180, 290, 32, 18);
    g_state.edtAiModel = MkCtrl(L"EDIT", ws.ai.model.c_str(),
                                ES_AUTOHSCROLL | WS_BORDER, 216, 286, 84, 22, kCtrlAiModel);
    SendMessageW(g_state.edtAiModel, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"留空=默认");

    MkCtrl(L"STATIC", L"API Key", SS_LEFT, 26, 318, 48, 18);
    g_state.edtAiKey = MkCtrl(L"EDIT", L"",
                              ES_AUTOHSCROLL | ES_PASSWORD | WS_BORDER,
                              80, 314, 220, 22, kCtrlAiKey);
    EnableWindow(g_state.edtAiKey, ws.ai.provider != L"ollama");

    g_state.btnAiRun = MkCtrl(L"BUTTON", L"AI 整理", BS_PUSHBUTTON,
                              26, 348, 86, 28, kCtrlAiRun);
    g_state.btnAiReset = MkCtrl(L"BUTTON", L"重置 AI 分组", BS_PUSHBUTTON,
                                120, 348, 104, 28, kCtrlAiReset);
    g_state.lblAiStatus = MkCtrl(L"STATIC", L"", SS_LEFT, 232, 354, 76, 18, kCtrlAiStatus);

    MkCtrl(L"STATIC",
           L"整理完成后先预览确认再应用；应用前自动备份，可一键重置。",
           SS_LEFT, 26, 384, 280, 18);

    // ════ 应用按钮（默认按钮样式，右下） ════
    HWND apply = MkCtrl(L"BUTTON", L"应用并保存", BS_DEFPUSHBUTTON,
                        198, 426, 118, 32, kCtrlApply);

    int sel = 0;
    if (ws.defaultStyle.backdrop == BackdropType::Translucent) sel = 1;
    if (ws.defaultStyle.backdrop == BackdropType::None)         sel = 2;
    ComboBox_SetCurSel(g_state.combo, sel);
    UpdateValueLabels();

    ShowWindow(g_state.dlg, SW_SHOWNORMAL);
    UpdateWindow(g_state.dlg);
    (void)apply;
}

void SettingsDialog::CloseIfOpen()
{
    if (g_state.dlg) DestroyWindow(g_state.dlg);
}

} // namespace winfence
