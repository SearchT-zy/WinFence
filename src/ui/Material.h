// 材质系统（M10 v2 · Apple 高级感）：柔和投影 / 发丝描边 / 玻璃面板通用画法。
// - SoftShadow：把圆角矩形形状高斯模糊后垫在面板下方，双层（大而淡的环境光
//   + 小而浓的接触阴影）模拟 macOS 的深度投影；按 (宽,高,圆角) 缓存模糊位图。
//   高斯模糊不可用时回退为多层同心描边（保证任何驱动下都有可见投影）。
// - DrawPanelBorder：1px 发丝白边 + 顶部内高光 + 底部内阴影，质感来自层次。
// 设计原则（v2）：不用噪点纹理（易显"马赛克"），一切用平滑渐变表达。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <windows.h>
#include <wrl/client.h>

#include <unordered_map>

namespace winfence {

// 面板四周预留的投影空间（DIP）。窗口必须比面板大 2×kShadowPadDip，
// 多余区域在 WM_NCHITTEST 返回 HTTRANSPARENT（点击穿透，投影不吃鼠标）。
inline constexpr float kShadowPadDip = 36.0f;

// ───────────────────────── 柔和投影 ─────────────────────────
class SoftShadow {
public:
    // body：窗口坐标系（DIP）下的面板矩形；radius：面板圆角。
    void Draw(ID2D1DeviceContext* ctx, const D2D1_RECT_F& body, float radius,
              float alphaScale = 1.0f)
    {
        const float w = body.right - body.left;
        const float h = body.bottom - body.top;
        if (w <= 8 || h <= 8 || !ctx) return;

        if (!effectsOk_) {   // 已确认效果不可用 → 描边回退
            DrawFallback(ctx, body, radius, alphaScale);
            return;
        }

        const uint32_t pad = 40;   // 模糊扩展边距：必须 ≥ 2.5×最大 blur，否则投影被位图硬边裁切
        const uint32_t bw = (uint32_t)(w + pad * 2);
        const uint32_t bh = (uint32_t)(h + pad * 2);
        const Key key{(uint32_t)(w * 2), (uint32_t)(h * 2), (uint32_t)(radius * 2)};

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bmp;
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            bmp = it->second;
        } else {
            Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> rt;
            if (FAILED(ctx->CreateCompatibleRenderTarget(
                    D2D1::SizeF((FLOAT)bw, (FLOAT)bh), &rt))) {
                effectsOk_ = false;
                DrawFallback(ctx, body, radius, alphaScale);
                return;
            }
            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF((FLOAT)pad, (FLOAT)pad,
                                              pad + w, pad + h),
                                  radius, radius),
                b.Get());
            rt->EndDraw();
            rt->GetBitmap(&bmp);
            cache_[key] = bmp;
            if (cache_.size() > 24) cache_.clear();   // 简单上限（按窗口数/尺寸数可控）
        }
        if (!bmp) return;

        // 双层：大而淡（环境光）+ 小而浓（接触阴影）→ 真实衰减
        // blur×2.5 + offsetY 必须 ≤ kShadowPadDip，否则投影在窗口边缘被硬切。
        const float layers[2][3] = {
            {10.0f, 0.18f, 7.0f},    // {blur, alpha, offsetY}
            {3.5f, 0.36f, 5.0f}};
        for (const auto& l : layers) {
            Microsoft::WRL::ComPtr<ID2D1Effect> fx;
            if (FAILED(ctx->CreateEffect(CLSID_D2D1GaussianBlur, &fx))) {
                effectsOk_ = false;
                DrawFallback(ctx, body, radius, alphaScale);
                return;
            }
            fx->SetInput(0, bmp.Get());
            fx->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, l[0]);
            ctx->PushLayer(
                D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                      D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                      D2D1::IdentityMatrix(), l[1] * alphaScale),
                nullptr);
            ctx->DrawImage(fx.Get(),
                           D2D1::Point2F(body.left - pad, body.top - pad + l[2]));
            ctx->PopLayer();
        }
    }

    void Reset() { cache_.clear(); }

private:
    // 回退：多层同心描边（外扩 + 二次衰减），无任何效果依赖
    void DrawFallback(ID2D1DeviceContext* ctx, const D2D1_RECT_F& body,
                      float radius, float alphaScale)
    {
        const int n = 8;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
        for (int i = n - 1; i >= 0; --i) {
            const float t = (float)i / (float)(n - 1);          // 0=最外
            const float grow = t * 24.0f;
            const float a = (1.0f - t) * (1.0f - t) * 0.34f * alphaScale;
            if (a <= 0.004f) continue;
            b->SetColor(D2D1::ColorF(0, 0, 0, a));
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                D2D1::RectF(body.left - grow, body.top - grow + 6.0f,
                            body.right + grow, body.bottom + grow + 6.0f),
                radius + grow, radius + grow);
            ctx->DrawRoundedRectangle(rr, b.Get(), 3.0f + t * 4.0f);
        }
    }

    struct Key {
        uint32_t w, h, r;
        bool operator==(const Key& o) const
        {
            return w == o.w && h == o.h && r == o.r;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const
        {
            return (size_t)k.w * 73856093u ^ (size_t)k.h * 19349663u ^
                   (size_t)k.r * 83492791u;
        }
    };
    std::unordered_map<Key, Microsoft::WRL::ComPtr<ID2D1Bitmap>, KeyHash> cache_;
    bool effectsOk_ = true;
};

// ───────────────────────── 面板描边（Apple 式发丝边 + 内高光）─────────────────────────
// 质感来自层次而非荧光：1px 发丝白边 + 顶部内高光 + 底部内阴影。
// accent 只在内容里做点缀；dropHighlight 时描边提亮为 accent 色。
inline void DrawPanelBorder(ID2D1DeviceContext* ctx, const D2D1_RECT_F& r,
                            float radius, float accentR, float accentG,
                            float accentB, bool dropHighlight)
{
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hair, top, bottom, accent;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), &hair);
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.22f), &top);
    ctx->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.18f), &bottom);
    ctx->CreateSolidColorBrush(
        D2D1::ColorF(accentR, accentG, accentB, dropHighlight ? 0.75f : 0.34f),
        &accent);

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
    if (hair) ctx->DrawRoundedRectangle(rr, hair.Get(), 1.0f);

    // 顶部内高光（1px 内缩）
    if (top) {
        D2D1_ROUNDED_RECT hi = D2D1::RoundedRect(
            D2D1::RectF(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1),
            std::max(1.0f, radius - 1), std::max(1.0f, radius - 1));
        ctx->DrawRoundedRectangle(hi, top.Get(), 1.0f);
    }
    // 底部内阴影（1px）
    if (bottom) {
        D2D1_ROUNDED_RECT lo = D2D1::RoundedRect(
            D2D1::RectF(r.left + 1, r.bottom - 1.5f, r.right - 1, r.bottom - 0.5f),
            0.5f, 0.5f);
        ctx->DrawRoundedRectangle(lo, bottom.Get(), 1.0f);
    }
    // accent 发丝（拖放目标时）
    if (accent && dropHighlight) {
        D2D1_ROUNDED_RECT ar = D2D1::RoundedRect(
            D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f),
            radius, radius);
        ctx->DrawRoundedRectangle(ar, accent.Get(), 1.2f);
    }
}

// ───────────────────────── 顶部内光晕（玻璃接光感）─────────────────────────
// 面板顶部一条大半径径向光斑：平滑的"光照"质感，无噪点。
inline void DrawTopGlow(ID2D1DeviceContext* ctx, const D2D1_RECT_F& body,
                        float radius, float strength = 1.0f)
{
    const float w = body.right - body.left;
    const float h = body.bottom - body.top;
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
    D2D1_GRADIENT_STOP gs[2] = {
        {0.0f, D2D1::ColorF(1, 1, 1, 0.10f * strength)},
        {1.0f, D2D1::ColorF(1, 1, 1, 0.0f)}};
    if (FAILED(ctx->CreateGradientStopCollection(gs, 2, &stops))) return;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> glow;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp{
        D2D1::Point2F(body.left + w / 2, body.top - h * 0.15f),
        D2D1::Point2F(body.left + w / 2, body.top),
        w * 0.75f, h * 0.95f};
    if (FAILED(ctx->CreateRadialGradientBrush(rp, stops.Get(), &glow))) return;
    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    ctx->GetFactory(&fac);
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
    if (!fac || FAILED(fac->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(body, radius, radius), &geo)))
        return;
    ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get(),
                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                   nullptr);
    ctx->FillRectangle(body, glow.Get());
    ctx->PopLayer();
}

} // namespace winfence
