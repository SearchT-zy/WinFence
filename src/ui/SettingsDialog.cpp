// 设置面板（M8 整窗 D2D 自绘 · 科技风）：
// 零 Win32 子控件——胶囊分段按钮 / 发光滑条 / 开关 / 自绘输入框（含光标与 Ctrl+V），
// 所有改动实时生效（样式/开关即时反馈，霓虹青主题与栅栏同一视觉语言）。
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

namespace winfence {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"WinFenceSettingsWnd";
constexpr UINT kMsgAiDone = WM_APP + 7;
constexpr UINT kTimerCaret = 1;

constexpr float kW = 340.0f, kH = 560.0f;   // 逻辑尺寸（DIP）

// ---- 命中目标 ----
enum CtrlId {
    idNone = 0,
    idBackdrop0, idBackdrop1, idBackdrop2,
    idOpacity, idRadius,
    idDock, idHideDesk, idAutoRun,
    idProv0, idProv1,
    idModel, idKey,
    idAiRun, idAiReset, idApply, idClose,
};

struct Layout {
    D2D1_RECT_F backdrop[3];
    D2D1_RECT_F sliderOp, sliderRd;
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
        float x = pad;
        const float widths[3] = {86.0f, 82.0f, 54.0f};
        for (int i = 0; i < 3; ++i) {
            l.backdrop[i] = {x, 72, x + widths[i], 98};
            x += widths[i] + 8;
        }
        l.sliderOp = {pad, 124, kW - pad, 144};
        l.sliderRd = {pad, 164, kW - pad, 184};
        l.togDock  = {kW - pad - 40, 212, kW - pad, 234};
        l.togHide  = {kW - pad - 40, 242, kW - pad, 264};
        l.togAuto  = {kW - pad - 40, 272, kW - pad, 294};
        l.prov[0] = {pad, 324, pad + 118, 350};
        l.prov[1] = {pad + 126, 324, pad + 126 + 118, 350};
        l.fieldModel = {pad, 374, kW - pad, 400};
        l.fieldKey   = {pad, 422, kW - pad, 448};
        l.btnAiRun   = {pad, 458, pad + 90, 488};
        l.btnAiReset = {pad + 98, 458, pad + 98 + 104, 488};
        l.btnApply   = {kW - pad - 120, 514, kW - pad, 548};
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

float DipX(LONG px) { return px * 96.0f / (float)g.dpi; }
float DipY(LONG py) { return py * 96.0f / (float)g.dpi; }

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
    ComPtr<ID2D1SolidColorBrush> fill, txt, ln;
    c->CreateSolidColorBrush(
        selected ? D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                                accent->GetColor().b, 0.85f)
                 : D2D1::ColorF(1, 1, 1, hovered ? 0.10f : 0.05f),
        &fill);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, selected ? 1.0f : 0.75f), &txt);
    c->CreateSolidColorBrush(
        D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                     accent->GetColor().b, selected ? 0.9f : 0.30f), &ln);
    const float h = r.bottom - r.top;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, h / 2, h / 2);
    c->FillRoundedRectangle(rr, fill.Get());
    c->DrawRoundedRectangle(rr, ln.Get(), 1.0f);
    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        c->DrawTextW(text, (UINT32)wcslen(text), f.Get(), r, txt.Get());
    }
}

void Toggle(ID2D1DeviceContext* c, const D2D1_RECT_F& r, bool on,
            ID2D1SolidColorBrush* accent)
{
    ComPtr<ID2D1SolidColorBrush> track, knob;
    c->CreateSolidColorBrush(
        on ? D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                          accent->GetColor().b, 0.9f)
           : D2D1::ColorF(1, 1, 1, 0.14f), &track);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &knob);
    const float h = r.bottom - r.top;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, h / 2, h / 2);
    c->FillRoundedRectangle(rr, track.Get());
    const float d = h - 6;
    const float kx = on ? r.right - d - 3 : r.left + 3;
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + d / 2, r.top + h / 2),
                                 d / 2, d / 2), knob.Get());
}

void Slider(ID2D1DeviceContext* c, const D2D1_RECT_F& r, float t,
            ID2D1SolidColorBrush* accent)
{
    const float cy = (r.top + r.bottom) / 2;
    ComPtr<ID2D1SolidColorBrush> base, glow;
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), &base);
    c->CreateSolidColorBrush(
        D2D1::ColorF(accent->GetColor().r, accent->GetColor().g,
                     accent->GetColor().b, 0.9f), &glow);
    D2D1_RECT_F track{r.left, cy - 2, r.right, cy + 2};
    c->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), base.Get());
    const float x = r.left + (r.right - r.left) * t;
    D2D1_RECT_F fill{r.left, cy - 2, x, cy + 2};
    if (x > r.left) c->FillRoundedRectangle(D2D1::RoundedRect(fill, 2, 2), glow.Get());
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 8, 8), glow.Get());   // 发光滑块
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 3.5f, 3.5f),
                   Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>(
                       [&] { ComPtr<ID2D1SolidColorBrush> w;
                             c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &w);
                             return w; }()).Get());
}

void TechButton(ID2D1DeviceContext* c, const D2D1_RECT_F& r, const wchar_t* text,
                bool filled, bool hovered, bool enabled, ID2D1SolidColorBrush* accent)
{
    ComPtr<ID2D1SolidColorBrush> fill, txt, ln;
    const auto& a = accent->GetColor();
    c->CreateSolidColorBrush(
        filled ? D2D1::ColorF(a.r, a.g, a.b, enabled ? (hovered ? 1.0f : 0.85f) : 0.3f)
               : D2D1::ColorF(1, 1, 1, hovered ? 0.12f : 0.05f), &fill);
    c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, enabled ? 0.95f : 0.5f), &txt);
    c->CreateSolidColorBrush(D2D1::ColorF(a.r, a.g, a.b, filled ? 1.0f : 0.45f), &ln);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6, 6);
    c->FillRoundedRectangle(rr, fill.Get());
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

    // ---- 背景：深蓝黑渐变（近实心，保证可读性）----
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.12f, 0.15f, 0.21f, 0.98f)},
            {1.0f, D2D1::ColorF(0.045f, 0.055f, 0.095f, 0.98f)}};
        if (SUCCEEDED(c->CreateGradientStopCollection(gs, 2, &stops))) {
            ComPtr<ID2D1LinearGradientBrush> bg;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{D2D1::Point2F(0, 0),
                                                     D2D1::Point2F(0, kH)};
            if (SUCCEEDED(c->CreateLinearGradientBrush(gp, stops.Get(), &bg)))
                c->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(0, 0, kW, kH), 12, 12), bg.Get());
        }
    }
    // 外发光 + 霓虹描边 + HUD 括号
    {
        ComPtr<ID2D1SolidColorBrush> glow, neon;
        c->CreateSolidColorBrush(D2D1::ColorF(accent->GetColor().r,
            accent->GetColor().g, accent->GetColor().b, 0.15f), &glow);
        c->CreateSolidColorBrush(D2D1::ColorF(accent->GetColor().r,
            accent->GetColor().g, accent->GetColor().b, 0.65f), &neon);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, kW, kH), 12, 12);
        if (glow) c->DrawRoundedRectangle(rr, glow.Get(), 5.0f);
        if (neon) c->DrawRoundedRectangle(rr, neon.Get(), 1.5f);
        if (neon) {
            const float inset = 5, arm = 12;
            struct C { float x, y, dx, dy; };
            const C cs[4] = {{inset, inset, 1, 1}, {kW - inset, inset, -1, 1},
                             {inset, kH - inset, 1, -1}, {kW - inset, kH - inset, -1, -1}};
            for (const auto& k : cs) {
                c->DrawLine(D2D1::Point2F(k.x, k.y),
                            D2D1::Point2F(k.x + k.dx * arm, k.y), neon.Get(), 2.0f);
                c->DrawLine(D2D1::Point2F(k.x, k.y),
                            D2D1::Point2F(k.x, k.y + k.dy * arm), neon.Get(), 2.0f);
            }
        }
    }

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

    // ---- 外观 ----
    SectionLabel(c, 52, L"外观", accent.Get());
    Pill(c, l.backdrop[0], L"亚克力", g.backdropSel == 0, g.hover == idBackdrop0, accent.Get());
    Pill(c, l.backdrop[1], L"半透明", g.backdropSel == 1, g.hover == idBackdrop1, accent.Get());
    Pill(c, l.backdrop[2], L"无",     g.backdropSel == 2, g.hover == idBackdrop2, accent.Get());

    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        wchar_t v[32];
        swprintf_s(v, L"%d%%", g.opacity);
        c->DrawTextW(L"透明度", 3, f.Get(), D2D1::RectF(30, 108, 200, 126), dim.Get());
        c->DrawTextW(v, (UINT32)wcslen(v), Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas").Get(),
                     D2D1::RectF(kW - 60, 108, kW - 18, 126), accent.Get());
        swprintf_s(v, L"%d", g.radius);
        c->DrawTextW(L"圆角", 2, f.Get(), D2D1::RectF(30, 148, 200, 166), dim.Get());
        c->DrawTextW(v, (UINT32)wcslen(v), Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas").Get(),
                     D2D1::RectF(kW - 60, 148, kW - 18, 166), accent.Get());
    }
    Slider(c, l.sliderOp, (g.opacity - 20) / 80.0f, accent.Get());
    Slider(c, l.sliderRd, g.radius / 24.0f, accent.Get());

    // ---- 桌面 ----
    SectionLabel(c, 198, L"桌面", accent.Get());
    if (auto f = Fmt(13.0f, DWRITE_FONT_WEIGHT_NORMAL)) {
        c->DrawTextW(L"启用 Dock 栏（屏幕底部）", 14, f.Get(),
                     D2D1::RectF(30, 212, 260, 234), txt.Get());
        c->DrawTextW(L"隐藏桌面图标", 6, f.Get(),
                     D2D1::RectF(30, 242, 260, 264), txt.Get());
        c->DrawTextW(L"开机自动启动", 6, f.Get(),
                     D2D1::RectF(30, 272, 260, 294), txt.Get());
    }
    Toggle(c, l.togDock, g.ws->dock.visible, accent.Get());
    Toggle(c, l.togHide, IsHideDesktopIcons(), accent.Get());
    Toggle(c, l.togAuto, IsAutostartEnabled(), accent.Get());

    // ---- AI ----
    SectionLabel(c, 306, L"AI 智能分组（仅手动触发 · 只上传文件名）", accent.Get());
    Pill(c, l.prov[0], L"DeepSeek 云端", g.aiProvSel == 0, g.hover == idProv0, accent.Get());
    Pill(c, l.prov[1], L"Ollama 本地",  g.aiProvSel == 1, g.hover == idProv1, accent.Get());
    if (auto f = Fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        c->DrawTextW(L"模型", 2, f.Get(), D2D1::RectF(30, 356, 200, 372), dim.Get());
        c->DrawTextW(L"API Key", 7, f.Get(), D2D1::RectF(30, 404, 200, 420), dim.Get());
    }
    Field(c, l.fieldModel, g.modelBuf, g.focusField == idModel,
          L"留空=默认", idModel);
    Field(c, l.fieldKey, g.keyBuf, g.focusField == idKey,
          g.aiProvSel == 1 ? L"本地无需" : L"sk-...", idKey);
    TechButton(c, l.btnAiRun, L"AI 整理", true, g.hover == idAiRun, !g.busy, accent.Get());
    TechButton(c, l.btnAiReset, L"重置", false, g.hover == idAiReset, true, accent.Get());
    if (auto f = Fmt(11.5f, DWRITE_FONT_WEIGHT_NORMAL)) {
        c->DrawTextW(g.status.c_str(), (UINT32)g.status.size(), f.Get(),
                     D2D1::RectF(212, 462, kW - 18, 484), dim.Get());
        c->DrawTextW(L"整理完成后先预览确认再应用；应用前自动备份",
                     22, f.Get(), D2D1::RectF(30, 496, kW - 18, 512), dim.Get());
    }
    TechButton(c, l.btnApply, L"保存并关闭", true, g.hover == idApply, true, accent.Get());

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
    case WM_NCHITTEST: {   // 四角圆角外穿透 + 整窗可拖动（标题区）
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        const float x = DipX(p.x), y = DipY(p.y);
        const float rad = 12, inset = 4;
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
    g.aiProvSel = (ws.ai.provider == L"ollama") ? 1 : 0;
    g.modelBuf = ws.ai.model;

    g.dlg = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
                            kClass, L"WinFence 设置", WS_POPUP,
                            100, 100, (int)kW, (int)kH,
                            nullptr, nullptr, instance, &g);
    if (!g.dlg) return;
    g.dpi = GetDpiForWindow(g.dlg);
    if (!g.dpi) g.dpi = 96;

    if (!Compositor::Get().BindWindow(g.dlg, g.dpi)) {
        DestroyWindow(g.dlg);
        return;
    }

    // DPI 精确定位 + 居中
    const int wPx = (int)(kW * g.dpi / 96), hPx = (int)(kH * g.dpi / 96);
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
