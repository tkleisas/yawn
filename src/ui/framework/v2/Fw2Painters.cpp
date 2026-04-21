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
#include "ui/framework/v2/DropDown.h"

#include "ui/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>

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

// ─── DropDown (closed button + open popup) ──────────────────────────

static void paintDropDownButton(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& dd = static_cast<FwDropDown&>(w);
    const Rect& b = dd.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // State → background.
    Color bg = p.controlBg;
    if (!dd.isEnabled())      bg = Color{50, 50, 53, 255};
    else if (dd.isOpen())     bg = p.controlActive;
    else if (dd.isPressed())  bg = p.controlActive;
    else if (dd.isHovered())  bg = p.controlHover;

    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, m.cornerRadius, bg);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

    const float fontSize = m.fontSize;
    const float pad      = m.baseUnit * 2.0f;

    // Label or placeholder.
    const std::string shown =
        (dd.selectedIndex() >= 0) ? dd.selectedLabel() : dd.placeholder();
    Color textColor = (dd.selectedIndex() >= 0) ? p.textPrimary : p.textSecondary;
    if (!dd.isEnabled()) textColor = p.textDim;

    if (ctx.textMetrics && !shown.empty()) {
        const float lh = ctx.textMetrics->lineHeight(fontSize);
        const float tx = b.x + pad;
        const float ty = b.y + (b.h - lh) * 0.5f - lh * 0.15f;
        // Clip label to the area left of the glyph so long items
        // don't run into the ▾ / ▴.
        ctx.renderer->pushClip(b.x, b.y,
                                b.w - pad * 1.5f - fontSize, b.h);
        ctx.textMetrics->drawText(*ctx.renderer, shown, tx, ty, fontSize, textColor);
        ctx.renderer->popClip();
    }

    // Dropdown glyph: ▾ when closed / opening down, ▴ when opening up.
    // The geometry is a small centered triangle drawn with drawTriangle.
    if (ctx.renderer) {
        const float gx = b.x + b.w - pad - fontSize * 0.5f;
        const float gy = b.y + b.h * 0.5f;
        const float gw = fontSize * 0.35f;   // half-base
        const float gh = fontSize * 0.28f;   // tip offset
        Color gc = dd.isEnabled() ? p.textSecondary : p.textDim;
        if (dd.isOpen() && dd.popupOpensUpward()) {
            // ▴ — tip up
            ctx.renderer->drawTriangle(gx - gw, gy + gh * 0.5f,
                                        gx + gw, gy + gh * 0.5f,
                                        gx,      gy - gh * 0.5f,
                                        gc);
        } else {
            // ▾ — tip down
            ctx.renderer->drawTriangle(gx - gw, gy - gh * 0.5f,
                                        gx + gw, gy - gh * 0.5f,
                                        gx,      gy + gh * 0.5f,
                                        gc);
        }
    }
}

static void paintDropDownPopup(const FwDropDown& dd, UIContext& ctx) {
    if (!ctx.renderer) return;
    const Rect& b = dd.popupBounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // Drop shadow — rendered as a slightly-offset, semi-transparent
    // rectangle below the popup body.
    ctx.renderer->drawRoundedRect(b.x + 2.0f, b.y + 3.0f, b.w, b.h,
                                   m.cornerRadius, p.dropShadow);
    // Popup body.
    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, m.cornerRadius, p.elevated);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

    // Items — scissor to popup body so overlong text doesn't bleed.
    ctx.renderer->pushClip(b.x, b.y, b.w, b.h);

    const float ih       = dd.itemHeight();
    const float pad      = m.baseUnit * 0.5f;
    const float textPad  = m.baseUnit * 2.0f;
    const float fontSize = m.fontSize;

    const auto& items    = dd.items();
    const int   rows     = std::min(static_cast<int>(items.size()),
                                     dd.maxVisibleItems());
    const int   startIdx = dd.scrollOffset();
    const int   selIdx   = dd.selectedIndex();
    const int   hlIdx    = dd.highlightedIndex();
    const Color accent   = dd.accentColor().value_or(p.accent);

    for (int row = 0; row < rows; ++row) {
        const int idx = startIdx + row;
        if (idx < 0 || idx >= static_cast<int>(items.size())) break;
        const auto& it = items[idx];
        const float ry = b.y + pad + row * ih;

        if (it.separator) {
            // Divider row: thin line centered vertically.
            const float ly = ry + ih * 0.5f;
            ctx.renderer->drawLine(b.x + textPad, ly,
                                    b.x + b.w - textPad, ly,
                                    p.borderSubtle, 1.0f);
            continue;
        }

        // Hover / highlight background.
        if (idx == hlIdx && it.enabled) {
            ctx.renderer->drawRect(b.x + 1.0f, ry,
                                    b.w - 2.0f, ih,
                                    accent.withAlpha(80));
        }
        // Selected indicator — left stripe.
        if (idx == selIdx) {
            ctx.renderer->drawRect(b.x + 1.0f, ry, 3.0f, ih, accent);
        }

        // Label.
        if (ctx.textMetrics && !it.label.empty()) {
            const float lh = ctx.textMetrics->lineHeight(fontSize);
            const float tx = b.x + textPad;
            const float ty = ry + (ih - lh) * 0.5f - lh * 0.15f;
            Color col = it.enabled ? p.textPrimary : p.textDim;
            ctx.textMetrics->drawText(*ctx.renderer, it.label, tx, ty, fontSize, col);
        }
    }

    ctx.renderer->popClip();
}

// ─── Registration ──────────────────────────────────────────────────

void registerAllFw2Painters() {
    registerPainter(typeid(Label),      &paintLabel);
    registerPainter(typeid(FwButton),   &paintButton);
    registerPainter(typeid(FwFader),    &paintFader);
    registerPainter(typeid(FwDropDown), &paintDropDownButton);
    // DropDown's popup is NOT a registered widget painter — it's a
    // static hook on the class because the popup is an OverlayEntry
    // closure, not a Widget subtree.
    FwDropDown::setPopupPainter(&paintDropDownPopup);
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
