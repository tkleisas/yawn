// UI v2 — paint functions for Label, FwButton, FwFader.
//
// Lives in the main executable (not yawn_core) because it touches both
// v1 Renderer2D and v1 Font directly — both pull glad/gl.h. The
// corresponding widget *logic* is in yawn_core and stays render-less;
// these paint functions are looked up by widget typeid through the
// Painter registry at render time.
//
// registerAllFw2Painters() is called once from App::setupUI (after the
// renderer + font adapter are wired) to populate the registry.

#include "ui/framework/v2/Painter.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/Label.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Fader.h"

#include "ui/Renderer.h"
#include "ui/Theme.h"

namespace yawn {
namespace ui {
namespace fw2 {

namespace {

// ─── Helpers ────────────────────────────────────────────────────────

Color textColorFromToken(TextColorToken tok, const ThemePalette& p) {
    switch (tok) {
        case TextColorToken::Primary:   return p.textPrimary;
        case TextColorToken::Secondary: return p.textSecondary;
        case TextColorToken::Dim:       return p.textDim;
    }
    return p.textPrimary;
}

// ─── Label ──────────────────────────────────────────────────────────

void paintLabel(Widget& w, UIContext& ctx) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& lbl = static_cast<Label&>(w);
    const Rect& b = lbl.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& pal = theme().palette;
    const ThemeMetrics& met = theme().metrics;

    const float fontSize = met.fontSize * lbl.fontScale();
    const std::string shown = lbl.effectiveText(b.w, ctx);
    const float tw = ctx.textMetrics->textWidth(shown, fontSize);
    const float lh = ctx.textMetrics->lineHeight(fontSize);

    // Horizontal alignment within bounds.
    float tx = b.x;
    switch (lbl.hAlign()) {
        case TextAlign::Start:  tx = b.x; break;
        case TextAlign::Center: tx = b.x + (b.w - tw) * 0.5f; break;
        case TextAlign::End:    tx = b.x + b.w - tw; break;
    }
    // Vertical alignment — the -0.15 * lh nudge matches v1's baseline
    // offset so v1 and v2 labels look visually identical during the
    // transition period.
    float ty = b.y;
    switch (lbl.vAlign()) {
        case VerticalAlign::Top:    ty = b.y; break;
        case VerticalAlign::Middle: ty = b.y + (b.h - lh) * 0.5f - lh * 0.15f; break;
        case VerticalAlign::Bottom: ty = b.y + b.h - lh; break;
    }

    const Color color = lbl.colorOverride().value_or(
        textColorFromToken(lbl.colorToken(), pal));

    // Clip truncation mode draws the full text inside a scissor so it
    // hard-cuts at widget bounds; None + Ellipsis already returned an
    // appropriately-sized string from effectiveText().
    const bool needsClip = (lbl.truncation() == Truncation::Clip);
    if (needsClip) ctx.renderer->pushClip(b.x, b.y, b.w, b.h);

    ctx.textMetrics->drawText(*ctx.renderer, shown, tx, ty, fontSize, color);

    if (needsClip) ctx.renderer->popClip();
}

// ─── Button ─────────────────────────────────────────────────────────

void paintButton(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& btn = static_cast<FwButton&>(w);
    const Rect& b = btn.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // State-driven background color.
    Color bg = p.controlBg;
    if (!btn.isEnabled())         bg = Color{50, 50, 53, 255};
    else if (btn.isHighlighted()) bg = btn.accentColor().value_or(p.accent);
    else if (btn.isPressed())     bg = p.controlActive;
    else if (btn.isHovered())     bg = p.controlHover;

    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, m.cornerRadius, bg);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

    // Label centered.
    if (ctx.textMetrics && !btn.label().empty()) {
        const float fontSize = m.fontSize;
        const float tw = ctx.textMetrics->textWidth(btn.label(), fontSize);
        const float lh = ctx.textMetrics->lineHeight(fontSize);
        const float tx = b.x + (b.w - tw) * 0.5f;
        const float ty = b.y + (b.h - lh) * 0.5f - lh * 0.15f;

        Color textColor = p.textPrimary;
        if (!btn.isEnabled())         textColor = p.textDim;
        else if (btn.isHighlighted()) textColor = p.textOnAccent;

        ctx.textMetrics->drawText(*ctx.renderer, btn.label(), tx, ty, fontSize, textColor);
    }
}

// ─── Fader ──────────────────────────────────────────────────────────

void paintFader(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& f = static_cast<FwFader&>(w);
    const Rect& b = f.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const FaderMetrics& fm = f.visualMetrics();

    // Track — thin vertical bar centered horizontally, spans full height.
    const float trackX = b.x + (b.w - fm.trackWidth) * 0.5f;
    const Color trackBg = f.trackColor().value_or(p.controlBg);
    ctx.renderer->drawRect(trackX, b.y, fm.trackWidth, b.h, trackBg);

    // Value fill — colored portion of track from bottom up to handle.
    const float range = f.max() - f.min();
    const float norm  = (range > 0.0f) ? (f.value() - f.min()) / range : 0.0f;
    const float fillH = (b.h - fm.handleHeight) * norm + fm.handleHeight * 0.5f;
    const float fillY = b.y + b.h - fillH;
    Color fillColor = p.accent;
    if (!f.isEnabled())      fillColor = p.textDim;
    else if (f.isPressed())  fillColor = p.accentActive;
    else if (f.isHovered())  fillColor = p.accentHover;
    ctx.renderer->drawRect(trackX, fillY, fm.trackWidth, fillH, fillColor);

    // Handle — rect centered on fill top.
    const float handleX = b.x + (b.w - fm.handleWidth) * 0.5f;
    const float handleY = b.y + (b.h - fm.handleHeight) * (1.0f - norm);
    ctx.renderer->drawRect(handleX, handleY, fm.handleWidth, fm.handleHeight, fillColor);
    ctx.renderer->drawRectOutline(handleX, handleY, fm.handleWidth, fm.handleHeight,
                                   p.border, 1.0f);
}

} // anon

// ─── Registration ──────────────────────────────────────────────────

void registerAllFw2Painters() {
    registerPainter(typeid(Label),    &paintLabel);
    registerPainter(typeid(FwButton), &paintButton);
    registerPainter(typeid(FwFader),  &paintFader);
    // FlexBox has no paint — it's a pure layout container; its
    // children paint themselves via Widget::render recursion.
}

// ─── Modal scrim ────────────────────────────────────────────────────

void paintModalScrim(UIContext& ctx, Rect viewport) {
    if (!ctx.renderer) return;
    ctx.renderer->drawRect(viewport.x, viewport.y, viewport.w, viewport.h,
                            theme().palette.scrim);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
