// 中文设置弹窗实现（M7b：AI 智能分组区——唯一触发入口，仅手动点击）。
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
constexpr int kCtrlAiStatus = 112;
constexpr int kLblOpacity   = 1100;
constexpr int kLblRadius    = 1101;

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
    // 保存 AI 设置（provider/model 进配置；Key 进 DPAPI 文件）
    const int provSel = (int)SendMessageW(g_state.cmbAiProv, CB_GETCURSEL, 0, 0);
    g_state.ws->ai.provider = (provSel == 1) ? L"ollama" : L"deepseek";
    wchar_t model[128]{}, key[256]{};
    GetWindowTextW(g_state.edtAiModel, model, 128);
    GetWindowTextW(g_state.edtAiKey, key, 256);
    g_state.ws->ai.model = model;
    if (g_state.ws->ai.provider == L"deepseek" && key[0])
        SaveApiKey(key);

    EnableWindow(g_state.btnAiRun, FALSE);
    SetAiStatus(L"正在请求模型（最长 60 秒）…");

    // 线程捕获副本（UI 对象生命周期不可跨线程）
    const HWND dlg = g_state.dlg;
    Workspace wsCopy;
    wsCopy.ai = g_state.ws->ai;
    IconRegistry iconsCopy = *g_state.icons;

    std::thread([dlg, wsCopy = std::move(wsCopy), iconsCopy = std::move(iconsCopy)]() {
        auto* result = new AiJobResult(RunAiGrouping(wsCopy, iconsCopy));
        if (!PostMessageW(dlg, kMsgAiDone, result->ok ? 1 : 0, (LPARAM)result))
            delete result;   // 窗口已关：丢弃
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
    SetAiStatus(L"已生成，请预览确认");
    AiPreviewDialog::Show(g_state.inst, g_state.dlg, *g_state.ws, *g_state.icons,
                          *g_state.store, result->groups, [] {
                              if (g_state.onApplied) g_state.onApplied();   // 刷新栅栏/Dock
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

    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) {
            if (LOWORD(wp) == kCtrlApply) { ApplyAndSave(); return 0; }
            if (LOWORD(wp) == kCtrlAiRun) { StartAiGrouping(); return 0; }
            if (LOWORD(wp) == kCtrlAiReset) {
                if (g_state.ws->aiBackup.present) {
                    FenceService::ResetAiGrouping(*g_state.ws);
                    g_state.store->ScheduleSave();
                    if (g_state.onApplied) g_state.onApplied();
                    SetAiStatus(L"已恢复 AI 分组前的布局");
                } else {
                    SetAiStatus(L"没有可重置的 AI 分组");
                }
                return 0;
            }
        }
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == kCtrlAiProv) {
            // Ollama 本地无需 Key
            const int sel = (int)SendMessageW(g_state.cmbAiProv, CB_GETCURSEL, 0, 0);
            EnableWindow(g_state.edtAiKey, sel == 0);
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
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    g_state.ws    = &ws;
    g_state.icons = &icons;
    g_state.store = &store;
    g_state.inst  = instance;
    g_state.onApplied = std::move(onApplied);

    const int w = S(320), h = S(560);
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

    // ---- 样式区 ----
    HWND label = CreateWindowExW(0, L"STATIC", L"背景效果：", WS_CHILD | WS_VISIBLE,
                                 S(16), S(18), S(70), S(20),
                                 g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(label);
    g_state.combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                    S(92), S(16), S(200), S(100),
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
                                           S(16), S(84), S(282), S(30),
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
                                          S(16), S(148), S(282), S(30),
                                          g_state.dlg, (HMENU)(INT_PTR)kCtrlRadius, instance, nullptr);
    SendMessageW(g_state.trackRadius, TBM_SETRANGE, TRUE, MAKELPARAM(0, 24));
    SendMessageW(g_state.trackRadius, TBM_SETPOS, TRUE,
                 (int)ws.defaultStyle.cornerRadiusDip);

    // ---- 开关区 ----
    g_state.chkDock = CreateWindowExW(0, L"BUTTON", L"启用 Dock 栏（屏幕底部）",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      S(16), S(192), S(280), S(22),
                                      g_state.dlg, (HMENU)(INT_PTR)kCtrlDock, instance, nullptr);
    ApplyFont(g_state.chkDock);
    SendMessageW(g_state.chkDock, BM_SETCHECK,
                 ws.dock.visible ? BST_CHECKED : BST_UNCHECKED, 0);

    g_state.chkHideDesk = CreateWindowExW(
        0, L"BUTTON", L"隐藏桌面图标（清空桌面，需重启资源管理器）",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        S(16), S(220), S(290), S(22),
        g_state.dlg, (HMENU)(INT_PTR)kCtrlHideDesk, instance, nullptr);
    ApplyFont(g_state.chkHideDesk);
    SendMessageW(g_state.chkHideDesk, BM_SETCHECK,
                 IsHideDesktopIcons() ? BST_CHECKED : BST_UNCHECKED, 0);

    g_state.chkAutoRun = CreateWindowExW(0, L"BUTTON", L"开机自动启动 WinFence",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         S(16), S(248), S(280), S(22),
                                         g_state.dlg, (HMENU)(INT_PTR)kCtrlAutoRun, instance, nullptr);
    ApplyFont(g_state.chkAutoRun);
    SendMessageW(g_state.chkAutoRun, BM_SETCHECK,
                 IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    // ---- AI 分组区（M7b：仅手动触发，只上传文件名）----
    HWND aiLabel = CreateWindowExW(0, L"STATIC",
                                   L"AI 智能分组（仅手动触发 · 只上传文件名，不含路径与内容）",
                                   WS_CHILD | WS_VISIBLE,
                                   S(16), S(284), S(300), S(20),
                                   g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(aiLabel);

    HWND provLabel = CreateWindowExW(0, L"STATIC", L"服务：", WS_CHILD | WS_VISIBLE,
                                     S(16), S(310), S(40), S(20),
                                     g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(provLabel);
    g_state.cmbAiProv = CreateWindowExW(0, L"COMBOBOX", L"",
                                        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                        S(60), S(308), S(150), S(100),
                                        g_state.dlg, (HMENU)(INT_PTR)kCtrlAiProv, instance, nullptr);
    ApplyFont(g_state.cmbAiProv);
    ComboBox_AddString(g_state.cmbAiProv, L"DeepSeek 云端");
    ComboBox_AddString(g_state.cmbAiProv, L"Ollama 本地");
    ComboBox_SetCurSel(g_state.cmbAiProv, ws.ai.provider == L"ollama" ? 1 : 0);

    HWND modelLabel = CreateWindowExW(0, L"STATIC", L"模型：", WS_CHILD | WS_VISIBLE,
                                      S(16), S(338), S(40), S(20),
                                      g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(modelLabel);
    g_state.edtAiModel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                         ws.ai.model.c_str(),
                                         WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                         S(60), S(336), S(238), S(22),
                                         g_state.dlg, (HMENU)(INT_PTR)kCtrlAiModel, instance, nullptr);
    ApplyFont(g_state.edtAiModel);
    SendMessageW(g_state.edtAiModel, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"留空=默认（deepseek-chat / qwen2.5:7b）");

    HWND keyLabel = CreateWindowExW(0, L"STATIC", L"Key：", WS_CHILD | WS_VISIBLE,
                                    S(16), S(366), S(40), S(20),
                                    g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(keyLabel);
    g_state.edtAiKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                                       S(60), S(364), S(238), S(22),
                                       g_state.dlg, (HMENU)(INT_PTR)kCtrlAiKey, instance, nullptr);
    ApplyFont(g_state.edtAiKey);
    EnableWindow(g_state.edtAiKey, ws.ai.provider != L"ollama");

    g_state.btnAiRun = CreateWindowExW(0, L"BUTTON", L"AI 整理",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       S(16), S(396), S(90), S(28),
                                       g_state.dlg, (HMENU)(INT_PTR)kCtrlAiRun, instance, nullptr);
    ApplyFont(g_state.btnAiRun);
    g_state.btnAiReset = CreateWindowExW(0, L"BUTTON", L"重置 AI 分组",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         S(114), S(396), S(110), S(28),
                                         g_state.dlg, (HMENU)(INT_PTR)kCtrlAiReset, instance, nullptr);
    ApplyFont(g_state.btnAiReset);
    g_state.lblAiStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                          S(232), S(402), S(80), S(20),
                                          g_state.dlg, nullptr, instance, nullptr);
    ApplyFont(g_state.lblAiStatus);

    // ---- 应用按钮 ----
    HWND apply = CreateWindowExW(0, L"BUTTON", L"应用并保存",
                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 S(16), S(440), S(110), S(30),
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
