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
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/Checkbox.h"

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

    // Scroll indicators — subtle ▲ / ▼ overlays on the right edge of
    // the first / last row when more items exist in that direction.
    // Works alongside wheel scroll + hover auto-scroll so users can
    // see that the list extends past the viewport.
    {
        const int totalItems = static_cast<int>(items.size());
        const bool hasMoreAbove = startIdx > 0;
        const bool hasMoreBelow = startIdx + rows < totalItems;
        if (hasMoreAbove || hasMoreBelow) {
            const float arrowW = fontSize * 0.45f;
            const float arrowH = fontSize * 0.35f;
            const float cx     = b.x + b.w - textPad;
            const Color arrowC = p.textSecondary;
            if (hasMoreAbove) {
                const float cy = b.y + pad + ih * 0.5f;
                ctx.renderer->drawTriangle(
                    cx - arrowW * 0.5f, cy + arrowH * 0.5f,
                    cx + arrowW * 0.5f, cy + arrowH * 0.5f,
                    cx,                 cy - arrowH * 0.5f,
                    arrowC);
            }
            if (hasMoreBelow) {
                const float cy = b.y + b.h - pad - ih * 0.5f;
                ctx.renderer->drawTriangle(
                    cx - arrowW * 0.5f, cy - arrowH * 0.5f,
                    cx + arrowW * 0.5f, cy - arrowH * 0.5f,
                    cx,                 cy + arrowH * 0.5f,
                    arrowC);
            }
        }
    }

    ctx.renderer->popClip();
}

// ─── Tooltip ────────────────────────────────────────────────────────

static void paintTooltip(const Rect& b, const std::string& text, UIContext& ctx) {
    if (!ctx.renderer) return;
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // Subtle drop shadow so the bubble visually detaches from the
    // widget it's pointing at.
    ctx.renderer->drawRoundedRect(b.x + 1.5f, b.y + 2.0f, b.w, b.h,
                                   m.cornerRadius, p.dropShadow);
    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, m.cornerRadius, p.elevated);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

    if (ctx.textMetrics && !text.empty()) {
        const float fontSize = m.fontSizeSmall;
        const float padX     = m.baseUnit * 2.0f;
        const float padY     = m.baseUnit * 0.75f;
        const float lh       = ctx.textMetrics->lineHeight(fontSize);
        const float tx       = b.x + padX;
        // Same baseline nudge as Label/Button so the bubble's text sits
        // visually centred within padY..padY+lh.
        const float ty       = b.y + padY - lh * 0.15f;
        ctx.textMetrics->drawText(*ctx.renderer, text, tx, ty, fontSize, p.textPrimary);
    }
}

// ─── ContextMenu ────────────────────────────────────────────────────

static void paintContextMenuLevels(
    const std::vector<ContextMenuManager::Level>& levels,
    UIContext& ctx) {
    if (!ctx.renderer || levels.empty()) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    for (const auto& L : levels) {
        const Rect& b = L.bounds;

        // Drop shadow + body + border.
        ctx.renderer->drawRoundedRect(b.x + 2.0f, b.y + 3.0f, b.w, b.h,
                                       m.cornerRadius, p.dropShadow);
        ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h,
                                       m.cornerRadius, p.elevated);
        ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

        // Clip items to body so long labels don't bleed.
        ctx.renderer->pushClip(b.x, b.y, b.w, b.h);

        const float padX       = m.baseUnit * 2.0f;
        const float padY       = m.baseUnit * 0.5f;
        const float leftMarker = m.fontSize;          // space for check/radio indicator
        const float arrowW     = m.fontSize * 0.75f;
        const float rowH       = ContextMenuManager::rowHeight();
        const float sepH       = ContextMenuManager::separatorRowHeight();

        float rowY = b.y + padY;
        for (int i = 0; i < static_cast<int>(L.entries.size()); ++i) {
            const MenuEntry& e = L.entries[i];

            if (e.kind == MenuEntryKind::Separator) {
                const float ly = rowY + sepH * 0.5f;
                ctx.renderer->drawLine(b.x + padX, ly,
                                        b.x + b.w - padX, ly,
                                        p.borderSubtle, 1.0f);
                rowY += sepH;
                continue;
            }

            const bool selectable = (e.kind != MenuEntryKind::Header) && e.enabled;
            const bool highlighted = (i == L.highlighted) && selectable;

            // Row background on hover/highlight.
            if (highlighted) {
                ctx.renderer->drawRect(b.x + 1.0f, rowY,
                                        b.w - 2.0f, rowH,
                                        p.accent.withAlpha(80));
            }

            // Check / radio indicator on the left gutter.
            if ((e.kind == MenuEntryKind::Checkable || e.kind == MenuEntryKind::Radio) &&
                e.checked) {
                // Draw a small filled circle for Radio, checkmark-ish
                // rect for Checkable. Simplified; we can upgrade to
                // glyph rendering later.
                const float cx = b.x + padX + leftMarker * 0.5f;
                const float cy = rowY + rowH * 0.5f;
                if (e.kind == MenuEntryKind::Radio) {
                    ctx.renderer->drawFilledCircle(cx, cy, 3.0f, p.accent, 12);
                } else {
                    ctx.renderer->drawRect(cx - 3.0f, cy - 3.0f, 6.0f, 6.0f, p.accent);
                }
            }

            // Label.
            if (ctx.textMetrics && !e.label.empty()) {
                const float fontSize = m.fontSize;
                const float lh = ctx.textMetrics->lineHeight(fontSize);
                const float tx = b.x + padX + leftMarker;
                const float ty = rowY + (rowH - lh) * 0.5f - lh * 0.15f;
                Color col = e.enabled ? p.textPrimary : p.textDim;
                if (e.kind == MenuEntryKind::Header) col = p.textSecondary;
                ctx.textMetrics->drawText(*ctx.renderer, e.label, tx, ty, fontSize, col);
            }

            // Shortcut text, right-aligned (with room for a submenu arrow).
            if (ctx.textMetrics && !e.shortcut.empty()) {
                const float fontSize = m.fontSize;
                const float lh = ctx.textMetrics->lineHeight(fontSize);
                const float sw = ctx.textMetrics->textWidth(e.shortcut, fontSize);
                const float tx = b.x + b.w - padX - arrowW - sw;
                const float ty = rowY + (rowH - lh) * 0.5f - lh * 0.15f;
                ctx.textMetrics->drawText(*ctx.renderer, e.shortcut, tx, ty,
                                           fontSize, p.textSecondary);
            }

            // Submenu chevron ▸.
            if (e.kind == MenuEntryKind::Submenu) {
                const float cx = b.x + b.w - padX - arrowW * 0.5f;
                const float cy = rowY + rowH * 0.5f;
                const float w  = arrowW * 0.55f;
                const float h  = arrowW * 0.5f;
                Color c = e.enabled ? p.textPrimary : p.textDim;
                // Triangle pointing right.
                ctx.renderer->drawTriangle(
                    cx - w * 0.5f, cy - h * 0.5f,
                    cx - w * 0.5f, cy + h * 0.5f,
                    cx + w * 0.5f, cy,
                    c);
            }

            rowY += rowH;
        }

        ctx.renderer->popClip();
    }
}

// ─── Dialog ─────────────────────────────────────────────────────────

static void paintDialog(const DialogManager::State& s, UIContext& ctx) {
    if (!ctx.renderer) return;
    const Rect& b = s.bounds;
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // Drop shadow + body.
    ctx.renderer->drawRoundedRect(b.x + 2.0f, b.y + 4.0f, b.w, b.h,
                                   m.cornerRadius, p.dropShadow);
    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h,
                                   m.cornerRadius, p.surface);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

    // Clip text to body interior.
    ctx.renderer->pushClip(b.x, b.y, b.w, b.h);

    const float padX     = m.baseUnit * 4.0f;
    const float padY     = m.baseUnit * 3.0f;
    const float rowGap   = m.baseUnit * 2.0f;
    const float fontSize = m.fontSize;
    const float titleSz  = m.fontSizeLarge;

    float y = b.y + padY;

    // Title bar.
    if (!s.spec.title.empty() && ctx.textMetrics) {
        const float lh = ctx.textMetrics->lineHeight(titleSz);
        ctx.textMetrics->drawText(*ctx.renderer, s.spec.title,
                                   b.x + padX, y - lh * 0.15f,
                                   titleSz, p.textPrimary);
        y += lh + rowGap;
    }

    // Message — one line per '\n'.
    if (!s.spec.message.empty() && ctx.textMetrics) {
        const float lh = ctx.textMetrics->lineHeight(fontSize);
        std::string line;
        for (size_t i = 0; i <= s.spec.message.size(); ++i) {
            const bool eol = (i == s.spec.message.size() || s.spec.message[i] == '\n');
            if (!eol) { line.push_back(s.spec.message[i]); continue; }
            ctx.textMetrics->drawText(*ctx.renderer, line,
                                       b.x + padX, y - lh * 0.15f,
                                       fontSize, p.textSecondary);
            y += lh;
            line.clear();
        }
    }

    // Buttons — rects already computed by layoutBody.
    for (size_t i = 0; i < s.spec.buttons.size(); ++i) {
        const DialogButton& btn = s.spec.buttons[i];
        const Rect& r = s.buttonRects[i];
        const bool hovered = (static_cast<int>(i) == s.hoveredButton);
        const bool pressed = (static_cast<int>(i) == s.pressedButton);

        Color bg = p.controlBg;
        if (btn.primary) {
            bg = p.accent;
            if (pressed)      bg = p.accentActive;
            else if (hovered) bg = p.accentHover;
        } else {
            if (pressed)      bg = p.controlActive;
            else if (hovered) bg = p.controlHover;
        }

        ctx.renderer->drawRoundedRect(r.x, r.y, r.w, r.h, m.cornerRadius, bg);
        ctx.renderer->drawRectOutline(r.x, r.y, r.w, r.h, p.border, m.borderWidth);

        if (ctx.textMetrics && !btn.label.empty()) {
            const float lh = ctx.textMetrics->lineHeight(fontSize);
            const float tw = ctx.textMetrics->textWidth(btn.label, fontSize);
            const float tx = r.x + (r.w - tw) * 0.5f;
            const float ty = r.y + (r.h - lh) * 0.5f - lh * 0.15f;
            const Color tc = btn.primary ? p.textOnAccent : p.textPrimary;
            ctx.textMetrics->drawText(*ctx.renderer, btn.label, tx, ty, fontSize, tc);
        }
    }

    ctx.renderer->popClip();
}

// ─── Toggle ─────────────────────────────────────────────────────────

static void paintToggle(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& tog = static_cast<FwToggle&>(w);
    const Rect& b = tog.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const Color accent = tog.accentColor().value_or(p.accent);

    if (tog.variant() == ToggleVariant::Switch) {
        // Pill track + knob.
        const float trackH = b.h * 0.55f;
        const float trackY = b.y + (b.h - trackH) * 0.5f;
        const float radius = trackH * 0.5f;

        Color trackCol = tog.state() ? accent : p.controlBg;
        if (!tog.isEnabled()) trackCol = Color{50, 50, 53, 255};
        else if (tog.isPressed())  trackCol = tog.state() ? p.accentActive : p.controlActive;
        else if (tog.isHovered())  trackCol = tog.state() ? p.accentHover  : p.controlHover;

        ctx.renderer->drawRoundedRect(b.x, trackY, b.w, trackH, radius, trackCol);
        ctx.renderer->drawRectOutline(b.x, trackY, b.w, trackH, p.border, m.borderWidth);

        // Knob — circle on left (off) or right (on). Slight inset so
        // it doesn't kiss the outline.
        const float knobR   = trackH * 0.45f;
        const float knobCY  = trackY + trackH * 0.5f;
        const float inset   = trackH * 0.1f;
        const float knobCX  = tog.state()
            ? (b.x + b.w - inset - knobR)
            : (b.x + inset + knobR);
        const Color knobCol = tog.isEnabled() ? p.elevated : p.textDim;
        ctx.renderer->drawFilledCircle(knobCX, knobCY, knobR, knobCol, 16);
        return;
    }

    // Button variant — rectangular fill.
    Color bg = tog.state() ? accent : p.controlBg;
    if (!tog.isEnabled()) bg = Color{50, 50, 53, 255};
    else if (tog.isPressed()) bg = tog.state() ? p.accentActive : p.controlActive;
    else if (tog.isHovered()) bg = tog.state() ? p.accentHover  : p.controlHover;

    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, m.cornerRadius, bg);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h, p.border, m.borderWidth);

    if (ctx.textMetrics && !tog.label().empty()) {
        const float fontSize = m.fontSize;
        const float tw = ctx.textMetrics->textWidth(tog.label(), fontSize);
        const float lh = ctx.textMetrics->lineHeight(fontSize);
        const float tx = b.x + (b.w - tw) * 0.5f;
        const float ty = b.y + (b.h - lh) * 0.5f - lh * 0.15f;
        Color textColor = p.textPrimary;
        if (!tog.isEnabled())       textColor = p.textDim;
        else if (tog.state())       textColor = p.textOnAccent;
        ctx.textMetrics->drawText(*ctx.renderer, tog.label(), tx, ty, fontSize, textColor);
    }
}

// ─── Checkbox ───────────────────────────────────────────────────────

static void paintCheckbox(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& cb = static_cast<FwCheckbox&>(w);
    const Rect& b = cb.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const Color accent = cb.accentColor().value_or(p.accent);

    const float boxSize = m.controlHeight * 0.6f;
    const float boxX    = b.x;
    const float boxY    = b.y + (b.h - boxSize) * 0.5f;
    const CheckState s  = cb.state();

    // Box background + border.
    Color boxBg = p.controlBg;
    if (!cb.isEnabled())        boxBg = Color{50, 50, 53, 255};
    else if (s == CheckState::On || s == CheckState::Indeterminate) boxBg = accent;
    else if (cb.isPressed())    boxBg = p.controlActive;
    else if (cb.isHovered())    boxBg = p.controlHover;

    ctx.renderer->drawRoundedRect(boxX, boxY, boxSize, boxSize,
                                   m.cornerRadius * 0.5f, boxBg);
    ctx.renderer->drawRectOutline(boxX, boxY, boxSize, boxSize,
                                   p.border, m.borderWidth);

    // Glyph — checkmark (On) or horizontal bar (Indeterminate).
    if (s == CheckState::On) {
        // Simple two-segment checkmark. Stroke from (0.22, 0.52) →
        // (0.45, 0.72) → (0.78, 0.32) in unit coordinates within the
        // box.
        const Color tc = cb.isEnabled() ? p.textOnAccent : p.textDim;
        const float x0 = boxX + boxSize * 0.22f, y0 = boxY + boxSize * 0.52f;
        const float x1 = boxX + boxSize * 0.45f, y1 = boxY + boxSize * 0.72f;
        const float x2 = boxX + boxSize * 0.78f, y2 = boxY + boxSize * 0.32f;
        ctx.renderer->drawLine(x0, y0, x1, y1, tc, 1.5f);
        ctx.renderer->drawLine(x1, y1, x2, y2, tc, 1.5f);
    } else if (s == CheckState::Indeterminate) {
        const Color tc = cb.isEnabled() ? p.textOnAccent : p.textDim;
        const float barW = boxSize * 0.55f;
        const float barH = std::max(2.0f, boxSize * 0.12f);
        const float barX = boxX + (boxSize - barW) * 0.5f;
        const float barY = boxY + (boxSize - barH) * 0.5f;
        ctx.renderer->drawRect(barX, barY, barW, barH, tc);
    }

    // Label right of box.
    if (ctx.textMetrics && !cb.label().empty()) {
        const float fontSize = m.fontSize;
        const float gap      = m.baseUnit;
        const float lh       = ctx.textMetrics->lineHeight(fontSize);
        const float tx       = boxX + boxSize + gap;
        const float ty       = b.y + (b.h - lh) * 0.5f - lh * 0.15f;
        const Color tc = cb.isEnabled() ? p.textPrimary : p.textDim;
        ctx.textMetrics->drawText(*ctx.renderer, cb.label(), tx, ty, fontSize, tc);
    }
}

// ─── Registration ──────────────────────────────────────────────────

void registerAllFw2Painters() {
    registerPainter(typeid(Label),      &paintLabel);
    registerPainter(typeid(FwButton),   &paintButton);
    registerPainter(typeid(FwFader),    &paintFader);
    registerPainter(typeid(FwDropDown), &paintDropDownButton);
    registerPainter(typeid(FwToggle),   &paintToggle);
    registerPainter(typeid(FwCheckbox), &paintCheckbox);
    // DropDown's popup is NOT a registered widget painter — it's a
    // static hook on the class because the popup is an OverlayEntry
    // closure, not a Widget subtree.
    FwDropDown::setPopupPainter(&paintDropDownPopup);
    // TooltipManager uses the same pattern — the overlay entry's paint
    // closure dispatches through this function pointer.
    TooltipManager::setPainter(&paintTooltip);
    // ContextMenuManager: same pattern — one function paints all open
    // levels (root + submenus).
    ContextMenuManager::setPainter(&paintContextMenuLevels);
    // DialogManager: paints the modal body (scrim is already drawn by
    // App via paintModalScrim when hasModalActive()).
    DialogManager::setPainter(&paintDialog);
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
