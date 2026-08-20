// 设置面板（M8 整窗 D2D 自绘 → M10 Apple 质感）：
// 零 Win32 子控件——胶囊分段按钮 / 发光滑条 / 开关 / 自绘输入框（含光标与 Ctrl+V），
// M10：窗口四周投影留白 + 柔和投影 + 发丝描边 + 磨砂噪点 + 实时预览卡 + 强调色预设。
#include "ui/SettingsDialog.h"

#include <d2d1helper.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <thread>

#include "ai/AiGrouping.h"
#include "core/FenceService.h"
#include "persist/ConfigStore.h"
#include "platform/DesktopIcons.h"
#include "ui/AiPreviewDialog.h"
#include "ui/Compositor.h"
#include "ui/Material.h"

namespace winfence {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"WinFenceSettingsWnd";
constexpr UINT kMsgAiDone = WM_APP + 7;
constexpr UINT kTimerCaret = 1;

constexpr float kW = 340.0f, kH = 660.0f;   // 逻辑尺寸（DIP）

// 强调色预设（M9）：霓虹青 / 电光蓝 / 霓虹紫 / 翠绿 / 琥珀 / 玫红
struct AccentPreset { const wchar_t* name; ColorF c; };
constexpr AccentPreset kAccents[] = {
    {L"霓虹青", {0.243f, 0.788f, 0.961f, 1.0f}},
    {L"电光蓝", {0.357f, 0.549f, 1.000f, 1.0f}},
    {L"霓虹紫", {0.655f, 0.545f, 0.980f, 1.0f}},
    {L"翠绿",   {0.204f, 0.827f, 0.600f, 1.0f}},
    {L"琥珀",   {0.984f, 0.749f, 0.141f, 1.0f}},
    {L"玫红",   {0.984f, 0.443f, 0.522f, 1.0f}},
};
constexpr int kAccentCount = (int)(sizeof(kAccents) / sizeof(kAccents[0]));

// ---- 命中目标 ----
enum CtrlId {
    idNone = 0,
    idBackdrop0, idBackdrop1, idBackdrop2,
    idOpacity, idRadius,
    idAccent0 = 10, idAccent1, idAccent2, idAccent3, idAccent4, idAccent5,
    idDock, idHideDesk, idAutoRun,
    idProv0, idProv1,
    idModel, idKey,
    idAiRun, idAiReset, idApply, idClose,
};

struct Layout {
    D2D1_RECT_F preview;
    D2D1_RECT_F backdrop[3];
    D2D1_RECT_F sliderOp, sliderRd;
    D2D1_RECT_F swatches[kAccentCount];
    D2D1_RECT_F togDock, togHide, togAuto;
    D2D1_RECT_F prov[2];
    D2D1_RECT_F fieldModel, fieldKey;
    D2D1_RECT_F btnAiRun, btnAiReset, btnApply, btnClose;
};

const Layout& L()
{
    static Layout ly = [] {
        Layout l{};
        const float pad = 18.0f;
        l.preview = {pad, 46, kW - pad, 118};
        float x = pad;
        const float widths[3] = {86.0f, 82.0f, 54.0f};
        for (int i = 0; i < 3; ++i) {
            l.backdrop[i] = {x, 146, x + widths[i], 172};
            x += widths[i] + 8;
        }
        l.sliderOp = {pad, 192, kW - pad, 212};
        l.sliderRd = {pad, 230, kW - pad, 250};
        for (int i = 0; i < kAccentCount; ++i) {   // 色板圆心 x = 40 + i*30
            const float cx = 40.0f + i * 30.0f;
            l.swatches[i] = {cx - 12, 272, cx + 12, 296};
        }
        l.togDock  = {kW - pad - 40, 326, kW - pad, 348};
        l.togHide  = {kW - pad - 40, 356, kW - pad, 378};
        l.togAuto  = {kW - pad - 40, 386, kW - pad, 408};
        l.prov[0] = {pad, 438, pad + 118, 464};
        l.prov[1] = {pad + 126, 438, pad + 126 + 118, 464};
        l.fieldModel = {pad, 482, kW - pad, 508};
        l.fieldKey   = {pad, 526, kW - pad, 552};
        l.btnAiRun   = {pad, 560, pad + 90, 590};
        l.btnAiReset = {pad + 98, 560, pad + 98 + 104, 590};
        l.btnApply   = {kW - pad - 120, 618, kW - pad, 652};
        l.btnClose   = {kW - 34, 12, kW - 14, 32};
        return l;
    }();
    return ly;
}

bool InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

CtrlId HitTest(float x, float y)
{
    const Layout& l = L();
    for (int i = 0; i < 3; ++i) if (InRect(l.backdrop[i], x, y)) return (CtrlId)(idBackdrop0 + i);
    if (InRect(l.sliderOp, x, y)) return idOpacity;
    if (InRect(l.sliderRd, x, y)) return idRadius;
    for (int i = 0; i < kAccentCount; ++i)
        if (InRect(l.swatches[i], x, y)) return (CtrlId)(idAccent0 + i);
    if (InRect(l.togDock, x, y))  return idDock;
    if (InRect(l.togHide, x, y))  return idHideDesk;
    if (InRect(l.togAuto, x, y))  return idAutoRun;
    for (int i = 0; i < 2; ++i) if (InRect(l.prov[i], x, y)) return (CtrlId)(idProv0 + i);
    if (InRect(l.fieldModel, x, y)) return idModel;
    if (InRect(l.fieldKey, x, y))   return idKey;
    if (InRect(l.btnAiRun, x, y))   return idAiRun;
    if (InRect(l.btnAiReset, x, y)) return idAiReset;
    if (InRect(l.btnApply, x, y))   return idApply;
    if (InRect(l.btnClose, x, y))   return idClose;
    return idNone;
}

// ---- 开机自启：HKCU Run 键 ----
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

struct PanelState {
    HWND dlg = nullptr;
    HINSTANCE inst = nullptr;
    UINT dpi = 96;
    Workspace* ws = nullptr;
    const IconRegistry* icons = nullptr;
    ConfigStore* store = nullptr;
    std::function<void()> onApplied;

    // UI 状态
    int backdropSel = 0;
    int opacity = 65;          // 20..100
    int radius = 10;           // 0..24
    int accentSel = 0;         // kAccents 索引
    int aiProvSel = 0;
    std::wstring modelBuf, keyBuf;
    std::wstring status;
    CtrlId hover = idNone;
    CtrlId focusField = idNone;   // idModel / idKey
    CtrlId dragging = idNone;
    bool caretOn = true;
    bool busy = false;             // AI 请求中
};
PanelState g;

constexpr float kPad = kShadowPadDip;   // M10：投影留白（内容原点偏移）
constexpr float kWinW = kW + 2 * kPad, kWinH = kH + 2 * kPad;   // 实际窗口尺寸

// 窗口客户区像素 → 内容坐标（DIP，已扣投影留白）
float DipX(LONG px) { return px * 96.0f / (float)g.dpi - kPad; }
float DipY(LONG py) { return py * 96.0f / (float)g.dpi - kPad; }

// ---- 实时生效：样式改动刷到所有栅栏 ----
void LiveStyleApply()
{
    FenceStyle st = g.ws->defaultStyle;
    st.opacity = g.opacity / 100.0f;
    st.cornerRadiusDip = (float)g.radius;
    g.ws->defaultStyle = st;
    for (auto& f : g.ws->fences) f.style = st;
    if (g.onApplied) g.onApplied();
}

float SliderT(CtrlId id, float x)
{
    const Layout& l = L();
    const D2D1_RECT_F& r = (id == idOpacity) ? l.sliderOp : l.sliderRd;
    float t = (x - r.left) / (r.right - r.left);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t;
}

void PasteClipboard(std::wstring& buf)
{
    if (!OpenClipboard(g.dlg)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            for (const wchar_t* q = p; *q && buf.size() < 200; ++q)
                if (*q >= 0x20 && *q < 0x7F) buf.push_back(*q);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

void DoApplyAndClose()
{
    g.ws->ai.provider = (g.aiProvSel == 1) ? L"ollama" : L"deepseek";
    g.ws->ai.model = g.modelBuf;
    if (g.aiProvSel == 0 && !g.keyBuf.empty()) SaveApiKey(g.keyBuf);
    g.store->ScheduleSave();
    g.store->FlushNow();
    DestroyWindow(g.dlg);
}

void StartAiGrouping()
{
    if (g.busy) return;
    g.ws->ai.provider = (g.aiProvSel == 1) ? L"ollama" : L"deepseek";
    g.ws->ai.model = g.modelBuf;
    if (g.aiProvSel == 0 && !g.keyBuf.empty()) SaveApiKey(g.keyBuf);
    g.busy = true;
    g.status = L"请求中…";

    const HWND dlg = g.dlg;
    Workspace wsCopy;
    wsCopy.ai = g.ws->ai;
    IconRegistry iconsCopy = *g.icons;
    std::thread([dlg, wsCopy = std::move(wsCopy), iconsCopy = std::move(iconsCopy)]() {
        auto* result = new AiJobResult(RunAiGrouping(wsCopy, iconsCopy));
        if (!PostMessageW(dlg, kMsgAiDone, result->ok ? 1 : 0, (LPARAM)result))
            delete result;
    }).detach();
}

void OnClick(CtrlId id)
{
    switch (id) {
    case idBackdrop0: case idBackdrop1: case idBackdrop2:
        g.backdropSel = id - idBackdrop0;
        g.ws->defaultStyle.backdrop = (BackdropType)g.backdropSel;
        LiveStyleApply();
        break;
    case idAccent0: case idAccent1: case idAccent2:
    case idAccent3: case idAccent4: case idAccent5:   // M9：强调色预设即时全局换色
        g.accentSel = id - idAccent0;
        g.ws->defaultStyle.accent = kAccents[g.accentSel].c;
        LiveStyleApply();
        break;
    case idDock:
        g.ws->dock.visible = !g.ws->dock.visible;
        if (g.onApplied) g.onApplied();
        break;
    case idHideDesk:
        SetHideDesktopIcons(!IsHideDesktopIcons());
        break;
    case idAutoRun:
        SetAutostart(!IsAutostartEnabled());
        break;
    case idProv0: g.aiProvSel = 0; break;
    case idProv1: g.aiProvSel = 1; break;
    case idModel: g.focusField = idModel; break;
    case idKey:   g.focusField = idKey; break;
    case idAiRun:  StartAiGrouping(); break;
    case idAiReset:
        if (g.ws->aiBackup.present) {
            FenceService::ResetAiGrouping(*g.ws);
            g.store->ScheduleSave();
            if (g.onApplied) g.onApplied();
            g.status = L"已重置";
        } else {
            g.status = L"无可重置";
        }
        break;
    case idApply: DoApplyAndClose(); break;
    case idClose: DestroyWindow(g.dlg); break;
    default: break;
    }
}

// ══════════════════ 绘制 ══════════════════

ComPtr<IDWriteTextFormat> Fmt(float size, DWRITE_FONT_WEIGHT wgt,
                              const wchar_t* family = L"Microsoft YaHei UI")
{
    ComPtr<IDWriteTextFormat> f;
    Compositor::Get().DWrite()->CreateTextFormat(
        family, nullptr, wgt, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"zh-CN", &f);
    return f;
}

float TextW(const std::wstring& s, IDWriteTextFormat* f)
{
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(Compositor::Get().DWrite()->CreateTextLayout(
            s.c_str(), (UINT32)s.size(), f, 2000, 60, &l)))
        return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.width;
}

void Pill(ID2D1DeviceContext* c, const D2D1_RECT_F& r, const wchar_t* text,
          bool selected, bool hovered, ID2D1SolidColorBrush* accent)
{
    ComPtr<ID2D1SolidColorBrush> fill, txt, ln, hi;
    const auto& a = accent->GetColor();
    const float h = r.bottom - r.top;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, h / 2, h / 2);
    if (selected) {   // M10：纵向 accent 渐变（上亮下暗）+ 顶部内高光
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(a.r, a.g, a.b, 0.95f)},
            {1.0f, D2D1::ColorF(a.r, a.g, a.b, 0.72f)}};
        if (SUCCEEDED(c->CreateGradientStopCollection(gs, 2, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{D2D1::Point2F(0, r.top),
                                                     D2D1::Point2F(0, r.bottom)};
            if (SUCCEEDED(c->CreateLinearGradientBrush(gp, stops.Get(), &bg)))
                c->FillRoundedRectangle(rr, bg.Get());
        }
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.35f), &hi);
        c->DrawLine(D2D1::Point2F(r.left + h / 2, r.top + 1.5f),
                    D2D1::Point2F(r.right - h / 2, r.top + 1.5f), hi.Get(), 1.0f);
    } else {
        c->CreateSolidColorBrush(
            D2D1::ColorF(1, 1, 1, hovered ? 0.10f : 0.05f), &fill);
        c->FillRoundedRectangle(rr, fill.Get());
    }
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, selected ? 1.0f : 0.75f), &txt);
    c->CreateSolidColorBrush(
        D2D1::ColorF(a.r, a.g, a.b, selected ? 0.9f : 0.30f), &ln);
    c->DrawRoundedRectangle(rr, ln.Get(), 1.0f);
    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        c->DrawTextW(text, (UINT32)wcslen(text), f.Get(), r, txt.Get());
    }
}

void Toggle(ID2D1DeviceContext* c, const D2D1_RECT_F& r, bool on, bool hovered,
            ID2D1SolidColorBrush* accent)
{
    ComPtr<ID2D1SolidColorBrush> track, knob, halo;
    c->CreateSolidColorBrush(
        on ? D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                          accent->GetColor().b, 0.9f)
           : D2D1::ColorF(1, 1, 1, 0.14f), &track);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &knob);
    const float h = r.bottom - r.top;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, h / 2, h / 2);
    if (hovered) {   // M9：悬停外发光环
        c->CreateSolidColorBrush(
            D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                         accent->GetColor().b, 0.30f), &halo);
        D2D1_ROUNDED_RECT hl = D2D1::RoundedRect(
            D2D1::RectF(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2),
            h / 2 + 2, h / 2 + 2);
        c->DrawRoundedRectangle(hl, halo.Get(), 2.5f);
    }
    c->FillRoundedRectangle(rr, track.Get());
    // 轨道内阴影（顶部 1px 深色，凹陷感）
    {
        ComPtr<ID2D1SolidColorBrush> inset;
        c->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.25f), &inset);
        c->DrawLine(D2D1::Point2F(r.left + h / 2, r.top + 1),
                    D2D1::Point2F(r.right - h / 2, r.top + 1), inset.Get(), 1.0f);
    }
    const float d = h - 6;
    const float kx = on ? r.right - d - 3 : r.left + 3;
    // 旋钮投影（M10：轻浮起感）
    {
        ComPtr<ID2D1SolidColorBrush> ksh;
        c->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.30f), &ksh);
        c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + d / 2, r.top + h / 2 + 1.5f),
                                     d / 2 + 1, d / 2 + 1), ksh.Get());
    }
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + d / 2, r.top + h / 2),
                                 d / 2, d / 2), knob.Get());
    // 旋钮顶部高光
    {
        ComPtr<ID2D1SolidColorBrush> hi;
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.35f), &hi);
        c->DrawLine(D2D1::Point2F(kx + d / 2 - d / 4, r.top + h / 2 - d / 3),
                    D2D1::Point2F(kx + d / 2 + d / 4, r.top + h / 2 - d / 3),
                    hi.Get(), 1.0f);
    }
}

void Slider(ID2D1DeviceContext* c, const D2D1_RECT_F& r, float t,
            ID2D1SolidColorBrush* accent)
{
    const float cy = (r.top + r.bottom) / 2;
    ComPtr<ID2D1SolidColorBrush> base, glow, shade;
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), &base);
    c->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.22f), &shade);
    c->CreateSolidColorBrush(
        D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                     accent->GetColor().b, 0.9f), &glow);
    D2D1_RECT_F track{r.left, cy - 2, r.right, cy + 2};
    c->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), base.Get());
    c->DrawLine(D2D1::Point2F(r.left + 2, cy + 1.5f),
                D2D1::Point2F(r.right - 2, cy + 1.5f), shade.Get(), 1.0f);   // 轨道内阴影
    const float x = r.left + (r.right - r.left) * t;
    D2D1_RECT_F fill{r.left, cy - 2, x, cy + 2};
    if (x > r.left) c->FillRoundedRectangle(D2D1::RoundedRect(fill, 2, 2), glow.Get());
    // 滑块：投影 + accent 光环 + 白芯（M10）
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy + 1.5f), 9, 9), shade.Get());
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 8, 8), glow.Get());
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 3.5f, 3.5f),
                   Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>(
                       [&] { ComPtr<ID2D1SolidColorBrush> w;
                             c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &w);
                             return w; }()).Get());
}

void TechButton(ID2D1DeviceContext* c, const D2D1_RECT_F& r, const wchar_t* text,
                bool filled, bool hovered, bool enabled, ID2D1SolidColorBrush* accent)
{
    ComPtr<ID2D1SolidColorBrush> fill, txt, ln, hi;
    const auto& a = accent->GetColor();
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6, 6);
    if (filled) {   // M10：纵向 accent 渐变 + 顶部内高光
        ComPtr<ID2D1GradientStopCollection> stops;
        const float topA = enabled ? (hovered ? 1.0f : 0.92f) : 0.30f;
        const float botA = enabled ? (hovered ? 0.86f : 0.76f) : 0.24f;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(a.r, a.g, a.b, topA)},
            {1.0f, D2D1::ColorF(a.r, a.g, a.b, botA)}};
        if (SUCCEEDED(c->CreateGradientStopCollection(gs, 2, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{D2D1::Point2F(0, r.top),
                                                     D2D1::Point2F(0, r.bottom)};
            if (SUCCEEDED(c->CreateLinearGradientBrush(gp, stops.Get(), &bg)))
                c->FillRoundedRectangle(rr, bg.Get());
        }
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, enabled ? 0.30f : 0.12f), &hi);
        c->DrawLine(D2D1::Point2F(r.left + 6, r.top + 1.5f),
                    D2D1::Point2F(r.right - 6, r.top + 1.5f), hi.Get(), 1.0f);
    } else {
        c->CreateSolidColorBrush(
            D2D1::ColorF(1, 1, 1, hovered ? 0.12f : 0.05f), &fill);
        c->FillRoundedRectangle(rr, fill.Get());
    }
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, enabled ? 0.95f : 0.5f), &txt);
    c->CreateSolidColorBrush(D2D1::ColorF(a.r, a.g, a.b, filled ? 1.0f : 0.45f), &ln);
    c->DrawRoundedRectangle(rr, ln.Get(), 1.2f);
    if (auto f = Fmt(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        c->DrawTextW(text, (UINT32)wcslen(text), f.Get(), r, txt.Get());
    }
}

void Field(ID2D1DeviceContext* c, const D2D1_RECT_F& r, const std::wstring& text,
           bool focused, const wchar_t* cue, CtrlId id)
{
    ComPtr<ID2D1SolidColorBrush> bg, ln, txt, cueB, caret;
    c->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.85f), &bg);
    c->CreateSolidColorBrush(D2D1::ColorF(g.ws->defaultStyle.accent.r,
                                          g.ws->defaultStyle.accent.g,
                                          g.ws->defaultStyle.accent.b,
                                          focused ? 0.8f : 0.18f), &ln);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.88f), &txt);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.32f), &cueB);
    c->CreateSolidColorBrush(D2D1::ColorF(g.ws->defaultStyle.accent.r,
                                          g.ws->defaultStyle.accent.g,
                                          g.ws->defaultStyle.accent.b, 1.0f), &caret);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6, 6);
    c->FillRoundedRectangle(rr, bg.Get());
    c->DrawRoundedRectangle(rr, ln.Get(), 1.2f);
    // 顶部内阴影（M10：凹陷感）
    {
        ComPtr<ID2D1SolidColorBrush> inset;
        c->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.35f), &inset);
        c->DrawLine(D2D1::Point2F(r.left + 6, r.top + 1),
                    D2D1::Point2F(r.right - 6, r.top + 1), inset.Get(), 1.0f);
    }
    const D2D1_RECT_F tr{r.left + 10, r.top, r.right - 8, r.bottom};
    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas")) {
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (text.empty() && !focused) {
            c->DrawTextW(cue, (UINT32)wcslen(cue), f.Get(), tr, cueB.Get());
        } else {
            // 密码形态：Key 全圆点
            std::wstring shown;
            if (id == idKey) shown.assign(text.size(), L'*');
            else shown = text;
            c->DrawTextW(shown.c_str(), (UINT32)shown.size(), f.Get(), tr, txt.Get());
            if (focused && g.caretOn) {
                const float cx = r.left + 10 + TextW(shown, f.Get()) + 2;
                const float cy = (r.top + r.bottom) / 2;
                c->DrawLine(D2D1::Point2F(cx, cy - 8), D2D1::Point2F(cx, cy + 8),
                            caret.Get(), 1.6f);
            }
        }
    }
}

void SectionLabel(ID2D1DeviceContext* c, float y, const wchar_t* text,
                  ID2D1SolidColorBrush* accent)
{
    ComPtr<ID2D1SolidColorBrush> txt, dot;
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.55f), &txt);
    if (auto f = Fmt(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)) {
        c->DrawTextW(text, (UINT32)wcslen(text), f.Get(),
                     D2D1::RectF(30, y, 300, y + 18), txt.Get());
    }
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(20, y + 9), 3, 3), accent);
}

// ---- M9：实时预览卡（mini 栅栏 + mini Dock，样式改动即时反馈）----
void DrawPreview(ID2D1DeviceContext* c, const D2D1_RECT_F& card)
{
    const FenceStyle& st = g.ws->defaultStyle;
    ComPtr<ID2D1SolidColorBrush> cardBg, cardLn, labelTxt, chip, chipDim;
    c->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.03f, 0.055f, 0.85f), &cardBg);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.08f), &cardLn);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.45f), &labelTxt);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.55f), &chip);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.20f), &chipDim);

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(card, 10, 10);
    c->FillRoundedRectangle(rr, cardBg.Get());
    c->DrawRoundedRectangle(rr, cardLn.Get(), 1.0f);
    if (auto f = Fmt(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD)) {
        c->DrawTextW(L"实时预览", 4, f.Get(),
                     D2D1::RectF(card.left + 10, card.top + 4, card.right - 10,
                                 card.top + 18), labelTxt.Get());
    }

    // ---- mini 栅栏 ----
    const D2D1_RECT_F fenceR{card.left + 10, card.top + 24,
                             card.left + 138, card.bottom - 8};
    const float fr = std::min(8.0f, st.cornerRadiusDip * 0.6f);
    const float bodyAlpha = (st.backdrop == BackdropType::None) ? 0.06f
                          : (st.backdrop == BackdropType::Acrylic) ? 0.50f
                          : st.opacity * 0.85f;
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.13f, 0.16f, 0.23f, bodyAlpha)},
            {1.0f, D2D1::ColorF(0.05f, 0.065f, 0.11f, bodyAlpha * 0.96f)}};
        if (SUCCEEDED(c->CreateGradientStopCollection(gs, 2, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{
                D2D1::Point2F(0, fenceR.top), D2D1::Point2F(0, fenceR.bottom)};
            if (SUCCEEDED(c->CreateLinearGradientBrush(gp, stops.Get(), &bg)))
                c->FillRoundedRectangle(D2D1::RoundedRect(fenceR, fr, fr), bg.Get());
        }
    }
    {
        ComPtr<ID2D1SolidColorBrush> neon, hair, hi;
        c->CreateSolidColorBrush(D2D1::ColorF(st.accent.r, st.accent.g,
                                              st.accent.b, 0.30f), &neon);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.12f), &hair);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), &hi);
        c->DrawRoundedRectangle(D2D1::RoundedRect(fenceR, fr, fr), hair.Get(), 1.0f);
        c->DrawRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(fenceR.left + 1, fenceR.top + 1, fenceR.right - 1,
                        fenceR.bottom - 1),
            std::max(1.0f, fr - 1), std::max(1.0f, fr - 1)), hi.Get(), 1.0f);
        // 标题条 + 身份色点 + 3 个图标占位块
        c->DrawLine(D2D1::Point2F(fenceR.left + 4, fenceR.top + 12),
                    D2D1::Point2F(fenceR.right - 4, fenceR.top + 12), neon.Get(), 0.8f);
        c->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(fenceR.left + 6, fenceR.top + 5,
                                          fenceR.left + 9, fenceR.top + 11),
                              1.5f, 1.5f), neon.Get());
        const float bx = fenceR.left + 14;
        const float by = fenceR.top + 24;
        for (int i = 0; i < 3; ++i) {
            const float x = bx + i * 26;
            c->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x, by, x + 14, by + 14), 4, 4),
                (i == 1) ? chip.Get() : chipDim.Get());
        }
    }

    // ---- mini Dock ----
    const D2D1_RECT_F dockR{card.right - 134, card.top + 38,
                            card.right - 10, card.bottom - 8};
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.13f, 0.16f, 0.23f, 0.75f)},
            {1.0f, D2D1::ColorF(0.05f, 0.065f, 0.11f, 0.75f)}};
        if (SUCCEEDED(c->CreateGradientStopCollection(gs, 2, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{
                D2D1::Point2F(0, dockR.top), D2D1::Point2F(0, dockR.bottom)};
            if (SUCCEEDED(c->CreateLinearGradientBrush(gp, stops.Get(), &bg)))
                c->FillRoundedRectangle(
                    D2D1::RoundedRect(dockR, 10, 10), bg.Get());
        }
    }
    {
        ComPtr<ID2D1SolidColorBrush> neon, hair;
        c->CreateSolidColorBrush(D2D1::ColorF(st.accent.r, st.accent.g,
                                              st.accent.b, 0.30f), &neon);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.12f), &hair);
        c->DrawRoundedRectangle(D2D1::RoundedRect(dockR, 10, 10), hair.Get(), 1.0f);
        const float cy = (dockR.top + dockR.bottom) / 2;
        for (int i = 0; i < 3; ++i) {
            const float x = dockR.left + 16 + i * 20;
            c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy - 2), 5, 5),
                           (i == 1) ? chip.Get() : chipDim.Get());
        }
    }
}

// ---- M9：强调色预设色板 ----
void DrawSwatches(ID2D1DeviceContext* c, const Layout& l)
{
    for (int i = 0; i < kAccentCount; ++i) {
        const D2D1_RECT_F& r = l.swatches[i];
        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        const float rad = (g.hover == (CtrlId)(idAccent0 + i)) ? 10.5f : 9.0f;
        const bool sel = (g.accentSel == i);
        ComPtr<ID2D1SolidColorBrush> dot, ring, glow;
        c->CreateSolidColorBrush(
            D2D1::ColorF(kAccents[i].c.r, kAccents[i].c.g, kAccents[i].c.b, 1.0f), &dot);
        c->CreateSolidColorBrush(
            D2D1::ColorF(kAccents[i].c.r, kAccents[i].c.g, kAccents[i].c.b, 0.25f), &glow);
        if (sel || g.hover == (CtrlId)(idAccent0 + i))
            c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 15, 15), glow.Get());
        c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), dot.Get());
        if (sel) {
            c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &ring);
            c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad + 2.5f, rad + 2.5f),
                           ring.Get(), 1.4f);
        }
    }
}

void Render()
{
    ID2D1DeviceContext* c = Compositor::Get().BeginDraw(g.dlg);
    if (!c) return;
    const Layout& l = L();
    ComPtr<ID2D1SolidColorBrush> accent;
    c->CreateSolidColorBrush(
        D2D1::ColorF(g.ws->defaultStyle.accent.r, g.ws->defaultStyle.accent.g,
                     g.ws->defaultStyle.accent.b, 1.0f), &accent);

    c->Clear(D2D1::ColorF(0, 0, 0, 0));

    // ---- M10：柔和投影（窗口坐标系）----
    {
        static SoftShadow shadow;
        shadow.Draw(c, D2D1::RectF(kPad, kPad, kPad + kW, kPad + kH), 12);
    }
    // 内容全部画在平移后的内容坐标系
    c->SetTransform(D2D1::Matrix3x2F::Translation(kPad, kPad));

    // ---- 背景：深蓝黑渐变（近实心，保证可读性）----
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.12f, 0.15f, 0.21f, 0.985f)},
            {1.0f, D2D1::ColorF(0.045f, 0.055f, 0.095f, 0.985f)}};
        if (SUCCEEDED(c->CreateGradientStopCollection(gs, 2, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{D2D1::Point2F(0, 0),
                                                     D2D1::Point2F(0, kH)};
            if (SUCCEEDED(c->CreateLinearGradientBrush(gp, stops.Get(), &bg)))
                c->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(0, 0, kW, kH), 12, 12), bg.Get());
        }
    }
    // 顶部内光晕（玻璃接光感）
    DrawTopGlow(c, D2D1::RectF(0, 0, kW, kH), 12, 0.8f);
    // Apple 式描边（发丝白边 + 顶部内高光 + 底部内阴影）
    DrawPanelBorder(c, D2D1::RectF(0, 0, kW, kH), 12,
                    g.ws->defaultStyle.accent.r, g.ws->defaultStyle.accent.g,
                    g.ws->defaultStyle.accent.b, false);

    ComPtr<ID2D1SolidColorBrush> txt, dim;
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.92f), &txt);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.55f), &dim);

    // ---- 标题 + 关闭 × ----
    if (auto f = Fmt(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)) {
        c->DrawTextW(L"WinFence 设置", 13, f.Get(), D2D1::RectF(24, 12, 260, 34), txt.Get());
    }
    if (accent) c->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(18, 16, 21, 30), 1.5f, 1.5f), accent.Get());
    {
        ComPtr<ID2D1SolidColorBrush> x;
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, g.hover == idClose ? 0.95f : 0.5f), &x);
        const float cx = (l.btnClose.left + l.btnClose.right) / 2;
        const float cy = (l.btnClose.top + l.btnClose.bottom) / 2;
        if (x) {
            c->DrawLine(D2D1::Point2F(cx - 5, cy - 5), D2D1::Point2F(cx + 5, cy + 5), x.Get(), 1.6f);
            c->DrawLine(D2D1::Point2F(cx + 5, cy - 5), D2D1::Point2F(cx - 5, cy + 5), x.Get(), 1.6f);
        }
    }

    // ---- 实时预览卡（M9）----
    DrawPreview(c, l.preview);

    // ---- 外观 ----
    SectionLabel(c, 132, L"外观", accent.Get());
    Pill(c, l.backdrop[0], L"亚克力", g.backdropSel == 0, g.hover == idBackdrop0, accent.Get());
    Pill(c, l.backdrop[1], L"半透明", g.backdropSel == 1, g.hover == idBackdrop1, accent.Get());
    Pill(c, l.backdrop[2], L"无",     g.backdropSel == 2, g.hover == idBackdrop2, accent.Get());

    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        wchar_t v[32];
        swprintf_s(v, L"%d%%", g.opacity);
        c->DrawTextW(L"透明度", 3, f.Get(), D2D1::RectF(30, 178, 200, 196), dim.Get());
        c->DrawTextW(v, (UINT32)wcslen(v), Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas").Get(),
                     D2D1::RectF(kW - 60, 178, kW - 18, 196), accent.Get());
        swprintf_s(v, L"%d", g.radius);
        c->DrawTextW(L"圆角", 2, f.Get(), D2D1::RectF(30, 216, 200, 234), dim.Get());
        c->DrawTextW(v, (UINT32)wcslen(v), Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas").Get(),
                     D2D1::RectF(kW - 60, 216, kW - 18, 234), accent.Get());
        c->DrawTextW(L"强调色", 3, f.Get(), D2D1::RectF(30, 258, 200, 276), dim.Get());
        if (g.accentSel >= 0 && g.accentSel < kAccentCount)
            c->DrawTextW(kAccents[g.accentSel].name,
                         (UINT32)wcslen(kAccents[g.accentSel].name),
                         Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas").Get(),
                         D2D1::RectF(kW - 90, 258, kW - 18, 276), accent.Get());
    }
    Slider(c, l.sliderOp, (g.opacity - 20) / 80.0f, accent.Get());
    Slider(c, l.sliderRd, g.radius / 24.0f, accent.Get());
    DrawSwatches(c, l);

    // ---- 桌面 ----
    SectionLabel(c, 312, L"桌面", accent.Get());
    if (auto f = Fmt(13.0f, DWRITE_FONT_WEIGHT_NORMAL)) {
        c->DrawTextW(L"启用 Dock 栏（屏幕底部）", 14, f.Get(),
                     D2D1::RectF(30, 326, 260, 348), txt.Get());
        c->DrawTextW(L"隐藏桌面图标", 6, f.Get(),
                     D2D1::RectF(30, 356, 260, 378), txt.Get());
        c->DrawTextW(L"开机自动启动", 6, f.Get(),
                     D2D1::RectF(30, 386, 260, 408), txt.Get());
    }
    Toggle(c, l.togDock, g.ws->dock.visible, g.hover == idDock, accent.Get());
    Toggle(c, l.togHide, IsHideDesktopIcons(), g.hover == idHideDesk, accent.Get());
    Toggle(c, l.togAuto, IsAutostartEnabled(), g.hover == idAutoRun, accent.Get());

    // ---- AI ----
    SectionLabel(c, 424, L"AI 智能分组（仅手动触发 · 只上传文件名）", accent.Get());
    Pill(c, l.prov[0], L"DeepSeek 云端", g.aiProvSel == 0, g.hover == idProv0, accent.Get());
    Pill(c, l.prov[1], L"Ollama 本地",  g.aiProvSel == 1, g.hover == idProv1, accent.Get());
    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        c->DrawTextW(L"模型", 2, f.Get(), D2D1::RectF(30, 466, 200, 482), dim.Get());
        c->DrawTextW(L"API Key", 7, f.Get(), D2D1::RectF(30, 510, 200, 526), dim.Get());
    }
    Field(c, l.fieldModel, g.modelBuf, g.focusField == idModel,
          L"留空=默认", idModel);
    Field(c, l.fieldKey, g.keyBuf, g.focusField == idKey,
          g.aiProvSel == 1 ? L"本地无需" : L"sk-...", idKey);
    TechButton(c, l.btnAiRun, L"AI 整理", true, g.hover == idAiRun, !g.busy, accent.Get());
    TechButton(c, l.btnAiReset, L"重置", false, g.hover == idAiReset, true, accent.Get());
    if (auto f = Fmt(11.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        c->DrawTextW(g.status.c_str(), (UINT32)g.status.size(), f.Get(),
                     D2D1::RectF(228, 564, kW - 18, 590), dim.Get());
        c->DrawTextW(L"整理完成后先预览确认再应用；应用前自动备份",
                     22, f.Get(), D2D1::RectF(30, 596, kW - 18, 612), dim.Get());
    }
    TechButton(c, l.btnApply, L"保存并关闭", true, g.hover == idApply, true, accent.Get());

    c->SetTransform(D2D1::Matrix3x2F::Identity());
    Compositor::Get().EndDraw(g.dlg);
}

// ══════════════════ 窗口过程 ══════════════════

LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCREATE: {
        auto* self = (PanelState*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        break;
    }
    case WM_NCHITTEST: {   // M10：投影留白/四角圆角外穿透 + 标题区整窗拖动
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        const float x = DipX(p.x), y = DipY(p.y);
        if (x < 0 || y < 0 || x >= kW || y >= kH) return HTTRANSPARENT;   // 留白
        const float rad = 12;
        auto outside = [&](float cx, float cy) {
            const float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy > rad * rad;
        };
        if (x < rad && y < rad && outside(rad, rad)) return HTTRANSPARENT;
        if (x >= kW - rad && y < rad && outside(kW - rad, rad)) return HTTRANSPARENT;
        if (x < rad && y >= kH - rad && outside(rad, kH - rad)) return HTTRANSPARENT;
        if (x >= kW - rad && y >= kH - rad && outside(kW - rad, kH - rad)) return HTTRANSPARENT;
        if (y < 40 && HitTest(x, y) == idNone) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        Render();
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const float x = DipX(p.x), y = DipY(p.y);
        if (g.dragging == idOpacity || g.dragging == idRadius) {
            const float t = SliderT(g.dragging, x);
            if (g.dragging == idOpacity) g.opacity = 20 + (int)(t * 80 + 0.5f);
            else                          g.radius  = (int)(t * 24 + 0.5f);
            LiveStyleApply();
            Render();
            return 0;
        }
        const CtrlId hv = HitTest(x, y);
        if (hv != g.hover) {
            g.hover = hv;
            Render();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.hover = idNone;
        Render();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const CtrlId id = HitTest(DipX(p.x), DipY(p.y));
        if (id == idOpacity || id == idRadius) {
            g.dragging = id;
            SetCapture(h);
            const float t = SliderT(id, DipX(p.x));
            if (id == idOpacity) g.opacity = 20 + (int)(t * 80 + 0.5f);
            else                 g.radius  = (int)(t * 24 + 0.5f);
            LiveStyleApply();
            Render();
            return 0;
        }
        OnClick(id);
        Render();
        return 0;
    }
    case WM_LBUTTONUP:
        if (g.dragging) {
            ReleaseCapture();
            g.dragging = idNone;
            g.store->ScheduleSave();
        }
        return 0;
    case WM_CHAR:
        if (wp == VK_ESCAPE) {
            DestroyWindow(h);
            return 0;
        }
        if (g.focusField == idModel || g.focusField == idKey) {
            std::wstring& buf = (g.focusField == idModel) ? g.modelBuf : g.keyBuf;
            if (wp == 0x08) {                      // Backspace
                if (!buf.empty()) buf.pop_back();
            } else if (wp == 0x16) {               // Ctrl+V
                PasteClipboard(buf);
            } else if (wp >= 0x20 && wp < 0x7F && buf.size() < 200) {
                buf.push_back((wchar_t)wp);
            }
            g.caretOn = true;
            Render();
        }
        return 0;
    case WM_TIMER:
        if (wp == kTimerCaret && (g.focusField == idModel || g.focusField == idKey)) {
            g.caretOn = !g.caretOn;
            Render();
        }
        return 0;
    case kMsgAiDone: {
        g.busy = false;
        std::unique_ptr<AiJobResult> result((AiJobResult*)lp);
        if (!result) return 0;
        if (!result->ok) {
            g.status = L"失败";
            Render();
            MessageBoxW(h, result->errZh.c_str(), L"AI 整理失败",
                        MB_OK | MB_ICONWARNING);
            return 0;
        }
        g.status = L"已生成";
        Render();
        AiPreviewDialog::Show(g.inst, h, *g.ws, *g.icons, *g.store,
                              result->groups, [] { if (g.onApplied) g.onApplied(); });
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, kTimerCaret);
        g.dlg = nullptr;
        g.focusField = idNone;
        g.store->ScheduleSave();
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
    if (g.dlg) {
        SetForegroundWindow(g.dlg);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &DlgProc;
        wc.hInstance     = instance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    g = PanelState{};   // 重置后重新绑定
    g.ws    = &ws;
    g.icons = &icons;
    g.store = &store;
    g.inst  = instance;
    g.onApplied = std::move(onApplied);
    g.backdropSel = (int)ws.defaultStyle.backdrop;
    g.opacity = (int)(ws.defaultStyle.opacity * 100 + 0.5f);
    g.radius  = (int)ws.defaultStyle.cornerRadiusDip;
    g.accentSel = 0;   // 按当前强调色匹配最近预设
    {
        float best = 1e9f;
        for (int i = 0; i < kAccentCount; ++i) {
            const float dr = kAccents[i].c.r - ws.defaultStyle.accent.r;
            const float dg = kAccents[i].c.g - ws.defaultStyle.accent.g;
            const float db = kAccents[i].c.b - ws.defaultStyle.accent.b;
            const float d = dr * dr + dg * dg + db * db;
            if (d < best) { best = d; g.accentSel = i; }
        }
    }
    g.aiProvSel = (ws.ai.provider == L"ollama") ? 1 : 0;
    g.modelBuf = ws.ai.model;

    g.dlg = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
                            kClass, L"WinFence 设置", WS_POPUP,
                            100, 100, (int)kWinW, (int)kWinH,
                            nullptr, nullptr, instance, &g);
    if (!g.dlg) return;
    g.dpi = GetDpiForWindow(g.dlg);
    if (!g.dpi) g.dpi = 96;

    if (!Compositor::Get().BindWindow(g.dlg, g.dpi)) {
        DestroyWindow(g.dlg);
        return;
    }

    // DPI 精确定位 + 居中（含投影留白）
    const int wPx = (int)(kWinW * g.dpi / 96), hPx = (int)(kWinH * g.dpi / 96);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g.dlg, nullptr,
                 wa.left + (wa.right - wa.left - wPx) / 2,
                 wa.top + (wa.bottom - wa.top - hPx) / 2,
                 wPx, hPx, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    SetTimer(g.dlg, kTimerCaret, 530, nullptr);
    SetForegroundWindow(g.dlg);
    Render();
}

void SettingsDialog::CloseIfOpen()
{
    if (g.dlg) DestroyWindow(g.dlg);
}

} // namespace winfence
