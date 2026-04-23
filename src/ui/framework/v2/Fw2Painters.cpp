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
#include "ui/framework/v2/Crossfader.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/MenuBar.h"
#include "ui/framework/v2/Meter.h"
#include "ui/framework/v2/NumberInput.h"
#include "ui/framework/v2/Pan.h"
#include "ui/framework/v2/ScrollBar.h"
#include "ui/framework/v2/StepSelector.h"
#include "ui/framework/v2/TextInput.h"
#include "ui/framework/v2/ProgressBar.h"
#include "ui/framework/v2/RadioGroup.h"
#include "ui/framework/v2/ScrollView.h"
#include "ui/framework/v2/TabView.h"
#include "ui/framework/v2/SplitView.h"
#include "ui/framework/v2/ContentGrid.h"

#include <cmath>

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

// ─── Knob ───────────────────────────────────────────────────────────

namespace {

// Arc geometry: 300° sweep starting at 7 o'clock (angle 210° cw from
// top) and ending at 5 o'clock (angle 510° which wraps to 150°).
// Wrap-mode knobs use a 360° sweep starting at 12 o'clock instead
// (see kWrap*Deg) — visually signals "this parameter wraps around"
// and matches v1 FwKnob360's layout.
// Angle here is "degrees clockwise from top", so:
//   x = cx + r * sin(angle)
//   y = cy - r * cos(angle)
constexpr float kArcStartDeg  = 210.0f;
constexpr float kArcSweepDeg  = 300.0f;
constexpr float kWrapStartDeg = 0.0f;      // top (12 o'clock)
constexpr float kWrapSweepDeg = 360.0f;

inline void angleToPoint(float cx, float cy, float r, float deg,
                          float& outX, float& outY) {
    const float rad = deg * 3.14159265358979f / 180.0f;
    outX = cx + r * std::sin(rad);
    outY = cy - r * std::cos(rad);
}

// Stroke an arc as a series of wedge-quad triangles between the inner
// and outer radius, from degFrom to degTo. Segments ~10° each.
void fillArc(Renderer2D& r, float cx, float cy,
              float innerR, float outerR,
              float degFrom, float degTo,
              Color color) {
    if (outerR <= 0.0f || innerR >= outerR) return;
    if (degFrom == degTo) return;
    const float step = (degTo > degFrom) ? 4.0f : -4.0f;
    const int   nSegs = static_cast<int>(std::ceil(std::abs(degTo - degFrom) / 4.0f));
    if (nSegs <= 0) return;

    float prevIx, prevIy, prevOx, prevOy;
    angleToPoint(cx, cy, innerR, degFrom, prevIx, prevIy);
    angleToPoint(cx, cy, outerR, degFrom, prevOx, prevOy);
    for (int i = 1; i <= nSegs; ++i) {
        const float a = degFrom + step * static_cast<float>(i);
        float ix, iy, ox, oy;
        angleToPoint(cx, cy, innerR, a, ix, iy);
        angleToPoint(cx, cy, outerR, a, ox, oy);
        r.drawTriangle(prevIx, prevIy, prevOx, prevOy, ox, oy, color);
        r.drawTriangle(prevIx, prevIy, ox,     oy,     ix, iy, color);
        prevIx = ix; prevIy = iy;
        prevOx = ox; prevOy = oy;
    }
}

} // anon

static void paintKnob(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& k = static_cast<FwKnob&>(w);
    const Rect& b = k.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const Color accent = k.accentColor().value_or(p.accent);

    // Layout: knob disc centred horizontally; label + value below.
    // The requested diameter comes from setDiameter() or a theme
    // default, but we MUST stay within the laid-out bounds — callers
    // pack knobs into tight grids (device params, mixer sends) and
    // the disc + label + value rows together have to fit inside `b`.
    // Shrink the disc if needed so the label/value rows don't spill
    // into the next cell.
    const float fontSizeLbl = m.fontSize;
    const float fontSizeVal = m.fontSizeSmall;
    const float lhLbl = ctx.textMetrics ? ctx.textMetrics->lineHeight(fontSizeLbl)
                                          : fontSizeLbl * 1.2f;
    const float lhVal = ctx.textMetrics ? ctx.textMetrics->lineHeight(fontSizeVal)
                                          : fontSizeVal * 1.2f;
    const float gap   = m.baseUnit * 0.5f;
    float belowDisc = 0.0f;
    if (k.showLabel() && !k.label().empty()) belowDisc += gap + lhLbl;
    if (k.showValue())                        belowDisc += gap + lhVal;

    float discDiameter = (k.diameter() > 0.0f) ? k.diameter()
                                                  : (m.controlHeight * 1.6f);
    // Fit inside bounds: width, AND remaining height after label/value.
    const float maxDiscH = std::max(8.0f, b.h - belowDisc);
    discDiameter = std::min(discDiameter, std::min(b.w, maxDiscH));
    const float discR        = discDiameter * 0.5f;
    const float cx           = b.x + b.w * 0.5f;
    const float cy           = b.y + discR;

    // Arc thickness scales with diameter but floors at a sensible
    // minimum so tiny compact knobs don't disappear.
    const float arcThickness = std::max(2.0f, discDiameter * 0.08f);
    const float outerR       = discR;
    const float innerR       = outerR - arcThickness;

    // Pick arc geometry. Wrap-mode knobs use a full 360° sweep from
    // 12 o'clock — no dead-zone gap at the bottom — so the user sees
    // "this parameter has no endpoints".
    const bool  wrap         = k.wrapMode();
    const float arcStartDeg  = wrap ? kWrapStartDeg : kArcStartDeg;
    const float arcSweepDeg  = wrap ? kWrapSweepDeg : kArcSweepDeg;

    // Background arc (full sweep).
    fillArc(*ctx.renderer, cx, cy, innerR, outerR,
             arcStartDeg, arcStartDeg + arcSweepDeg,
             k.isEnabled() ? p.controlBg : Color{40, 40, 45, 255});

    // Value → normalized t ∈ [0, 1].
    const float range = k.max() - k.min();
    const float t = (range > 0.0f) ? (k.value() - k.min()) / range : 0.0f;

    // Filled arc — from min (or centre in bipolar) to current value.
    // Wrap mode has no natural "start" — rather than shade an arbitrary
    // slice we skip the fill and rely on the indicator line to show
    // position. (Phase-like params read more naturally as a pointer
    // than as a proportion.)
    if (!wrap) {
        float fillFromDeg, fillToDeg;
        if (k.bipolar()) {
            const float mid = arcStartDeg + arcSweepDeg * 0.5f;
            const float cur = arcStartDeg + arcSweepDeg * t;
            fillFromDeg = std::min(mid, cur);
            fillToDeg   = std::max(mid, cur);
        } else {
            fillFromDeg = arcStartDeg;
            fillToDeg   = arcStartDeg + arcSweepDeg * t;
        }
        const Color fillColor = k.isEnabled() ? accent : p.textDim;
        fillArc(*ctx.renderer, cx, cy, innerR, outerR,
                 fillFromDeg, fillToDeg, fillColor);
    }

    // Inner disc — elevated fill inside the arc ring.
    ctx.renderer->drawFilledCircle(cx, cy, innerR - 1.0f, p.elevated, 32);

    // Detent tick marks — short notches at each detent's position on
    // the outer edge of the ring so users can see where the snap
    // zones are before they drag into one.
    if (!k.detents().empty() && range > 0.0f) {
        const Color tickCol = k.isEnabled() ? p.textSecondary : p.textDim;
        for (const auto& d : k.detents()) {
            float dt = (d.value - k.min()) / range;
            if (dt < 0.0f || dt > 1.0f) continue;
            const float dDeg = arcStartDeg + arcSweepDeg * dt;
            float tx0, ty0, tx1, ty1;
            angleToPoint(cx, cy, outerR,        dDeg, tx0, ty0);
            angleToPoint(cx, cy, outerR + 3.0f, dDeg, tx1, ty1);
            ctx.renderer->drawLine(tx0, ty0, tx1, ty1, tickCol, 1.0f);
        }
    }

    // Indicator line — from just inside the arc ring toward the
    // centre, pointing at current value.
    const float indicatorR0 = innerR * 0.35f;
    const float indicatorR1 = innerR * 0.9f;
    const float indDeg      = arcStartDeg + arcSweepDeg * t;
    float ix0, iy0, ix1, iy1;
    angleToPoint(cx, cy, indicatorR0, indDeg, ix0, iy0);
    angleToPoint(cx, cy, indicatorR1, indDeg, ix1, iy1);
    const Color indCol = k.isEnabled() ? p.textPrimary : p.textDim;
    ctx.renderer->drawLine(ix0, iy0, ix1, iy1, indCol,
                            std::max(1.5f, arcThickness * 0.4f));

    // Modulation overlay — thin secondary indicator at the modulated
    // value, drawn inside the ring.
    if (k.modulatedValue()) {
        const float modT = (range > 0.0f)
            ? (*k.modulatedValue() - k.min()) / range : 0.0f;
        const float modDeg = arcStartDeg + arcSweepDeg * modT;
        const Color modCol = k.modulationColor().value_or(p.modulation);

        // Thin filled arc from primary to modulated position —
        // visualizes the "excursion" the modulator is adding.
        fillArc(*ctx.renderer, cx, cy,
                 outerR + 1.0f, outerR + 1.0f + arcThickness * 0.6f,
                 indDeg, modDeg, modCol);

        // Small dot marker at the modulated position.
        float dotX, dotY;
        angleToPoint(cx, cy, outerR + arcThickness * 0.4f + 1.0f, modDeg,
                      dotX, dotY);
        ctx.renderer->drawFilledCircle(dotX, dotY, arcThickness * 0.5f, modCol, 12);
    }

    // Label + value text below the disc.
    float textY = b.y + discDiameter + m.baseUnit * 0.5f;
    if (k.showLabel() && !k.label().empty() && ctx.textMetrics) {
        const float fontSize = m.fontSize;
        const float lh       = ctx.textMetrics->lineHeight(fontSize);
        const float tw       = ctx.textMetrics->textWidth(k.label(), fontSize);
        const float tx       = b.x + (b.w - tw) * 0.5f;
        const Color tc = k.isEnabled() ? p.textPrimary : p.textDim;
        ctx.textMetrics->drawText(*ctx.renderer, k.label(), tx,
                                   textY - lh * 0.15f, fontSize, tc);
        textY += lh + m.baseUnit * 0.25f;
    }
    if (k.showValue() && ctx.textMetrics) {
        const float fontSize = m.fontSizeSmall;
        const float lh       = ctx.textMetrics->lineHeight(fontSize);
        // When editing, show the live buffer (possibly empty) so the
        // user sees their typed characters; otherwise the formatted
        // value. An editing knob also gets an accent-coloured rect
        // behind the text so it's obviously a text field now.
        const bool editing   = k.isEditing();
        const std::string vs = editing ? k.editBuffer() : k.formattedValue();
        const float tw       = ctx.textMetrics->textWidth(vs, fontSize);
        const float tx       = b.x + (b.w - tw) * 0.5f;
        const float ty       = textY - lh * 0.15f;
        if (editing) {
            // Subtle outlined field, accent-coloured. Gives users a
            // visual confirmation that the knob has become editable.
            const float padX = m.baseUnit * 0.75f;
            const float padY = m.baseUnit * 0.1f;
            const float fieldW = std::max(tw, discDiameter * 0.6f) + padX * 2.0f;
            const float fieldX = b.x + (b.w - fieldW) * 0.5f;
            const float fieldY = ty - padY;
            const float fieldH = lh + padY * 2.0f;
            ctx.renderer->drawRect(fieldX, fieldY, fieldW, fieldH, p.elevated);
            ctx.renderer->drawRectOutline(fieldX, fieldY, fieldW, fieldH, accent,
                                           1.0f);
        }
        const Color tc = editing
            ? p.textPrimary
            : (k.isEnabled() ? p.textSecondary : p.textDim);
        ctx.textMetrics->drawText(*ctx.renderer, vs, tx, ty, fontSize, tc);

        if (editing) {
            // Caret — a 1px vertical bar at the end of the buffer.
            // Static (no blink) for simplicity; users see it as "the
            // typing cursor" regardless.
            const float caretX = tx + tw + 1.0f;
            const float caretY0 = ty;
            const float caretY1 = ty + lh * 0.9f;
            ctx.renderer->drawLine(caretX, caretY0, caretX, caretY1, accent,
                                    1.5f);
        }
    }
}

// ─── Step Selector ──────────────────────────────────────────────────

static void paintStepSelector(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& s = static_cast<FwStepSelector&>(w);
    const Rect& b = s.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const float fontSize  = m.fontSize;
    const float smallFS   = m.fontSizeSmall;

    // Label (above display, dim).
    float displayY = b.y;
    float displayH = b.h;
    if (!s.label().empty() && ctx.textMetrics) {
        const float lh = ctx.textMetrics->lineHeight(smallFS);
        const float tw = ctx.textMetrics->textWidth(s.label(), smallFS);
        const float tx = b.x + (b.w - tw) * 0.5f;
        ctx.textMetrics->drawText(*ctx.renderer, s.label(),
                                   tx, b.y + 1.0f, smallFS, p.textSecondary);
        const float consumed = lh + m.baseUnit * 0.25f;
        displayY += consumed;
        displayH -= consumed;
    }

    // Display background.
    ctx.renderer->drawRect(b.x, displayY, b.w, displayH,
                            s.isEnabled() ? p.controlBg : Color{40, 40, 45, 255});
    ctx.renderer->drawRectOutline(b.x, displayY, b.w, displayH, p.border, 1.0f);

    // Value text centred.
    if (ctx.textMetrics) {
        const std::string val = s.formattedValue();
        const float lh = ctx.textMetrics->lineHeight(fontSize);
        const float tw = ctx.textMetrics->textWidth(val, fontSize);
        const float tx = b.x + (b.w - tw) * 0.5f;
        const float ty = displayY + (displayH - lh) * 0.5f - lh * 0.15f;
        const Color tc = s.isEnabled() ? p.textPrimary : p.textDim;
        ctx.textMetrics->drawText(*ctx.renderer, val, tx, ty, fontSize, tc);
    }

    // Arrow glyphs — small filled triangles drawn into the arrow
    // strips. Dimmer when the value is at an endpoint (and no wrap).
    const float arrowW = s.arrowWidth();
    const bool  atMin  = !s.wrap() && s.value() <= s.min();
    const bool  atMax  = !s.wrap() && s.value() >= s.max();
    const Color lc = (s.isEnabled() && !atMin) ? p.textPrimary : p.textDim;
    const Color rc = (s.isEnabled() && !atMax) ? p.textPrimary : p.textDim;

    const float cyA = displayY + displayH * 0.5f;
    const float triH = displayH * 0.25f;
    const float triW = arrowW * 0.35f;
    // Left arrow: ◀
    const float lx = b.x + arrowW * 0.5f;
    ctx.renderer->drawTriangle(lx + triW * 0.5f, cyA - triH,
                                lx + triW * 0.5f, cyA + triH,
                                lx - triW * 0.5f, cyA, lc);
    // Right arrow: ▶
    const float rx = b.x + b.w - arrowW * 0.5f;
    ctx.renderer->drawTriangle(rx - triW * 0.5f, cyA - triH,
                                rx - triW * 0.5f, cyA + triH,
                                rx + triW * 0.5f, cyA, rc);
}

// ─── Pan ────────────────────────────────────────────────────────────

static void paintPan(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& p = static_cast<FwPan&>(w);
    const Rect& b = p.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& pal = theme().palette;

    // Track — thin horizontal strip, vertically centred.
    const float trackH = std::min(b.h, 4.0f);
    const float trackY = b.y + (b.h - trackH) * 0.5f;
    ctx.renderer->drawRect(b.x, trackY, b.w, trackH,
                            p.isEnabled() ? pal.controlBg : Color{40, 40, 45, 255});

    // Center tick — a small vertical nub at the midpoint so users can
    // see the "0 pan" position at a glance.
    const float cx = b.x + b.w * 0.5f;
    ctx.renderer->drawRect(cx - 0.5f, b.y + b.h * 0.2f, 1.0f, b.h * 0.6f,
                            pal.border);

    // Filled portion from center to thumb.
    const float t = p.value();                          // -1..+1
    const float thumbCx = cx + t * (b.w * 0.5f - 4.0f); // keep thumb inside
    const Color fillCol = p.thumbColor().value_or(pal.accent);
    if (t >= 0.0f) {
        ctx.renderer->drawRect(cx, trackY, thumbCx - cx, trackH,
                                p.isEnabled() ? fillCol : pal.textDim);
    } else {
        ctx.renderer->drawRect(thumbCx, trackY, cx - thumbCx, trackH,
                                p.isEnabled() ? fillCol : pal.textDim);
    }

    // Thumb — small square centred on the current value.
    const float thumbW = 6.0f;
    const float thumbH = std::min(b.h - 2.0f, 12.0f);
    ctx.renderer->drawRect(thumbCx - thumbW * 0.5f,
                            b.y + (b.h - thumbH) * 0.5f,
                            thumbW, thumbH,
                            p.isEnabled() ? fillCol : pal.textDim);
    ctx.renderer->drawRectOutline(thumbCx - thumbW * 0.5f,
                                   b.y + (b.h - thumbH) * 0.5f,
                                   thumbW, thumbH, pal.border, 1.0f);
}

// ─── Crossfader ────────────────────────────────────────────────────

static void paintCrossfader(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& xf = static_cast<FwCrossfader&>(w);
    const Rect& b = xf.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    const float pad    = xf.trackPadding();
    const float trackX = b.x + pad;
    const float trackW = std::max(1.0f, b.w - pad * 2.0f);

    // End labels — "A" left, "B" right, in the track-padding area.
    if (ctx.textMetrics) {
        const float fs = m.fontSize;
        const float lh = ctx.textMetrics->lineHeight(fs);
        const float ty = b.y + (b.h - lh) * 0.5f - lh * 0.15f;

        const Color aCol = xf.isEnabled() ? xf.colorA() : p.textDim;
        const Color bCol = xf.isEnabled() ? xf.colorB() : p.textDim;

        const std::string& la = xf.labelA();
        const std::string& lb = xf.labelB();
        if (!la.empty()) {
            const float tw = ctx.textMetrics->textWidth(la, fs);
            ctx.textMetrics->drawText(*ctx.renderer, la,
                                       b.x + (pad - tw) * 0.5f, ty, fs, aCol);
        }
        if (!lb.empty()) {
            const float tw = ctx.textMetrics->textWidth(lb, fs);
            ctx.textMetrics->drawText(*ctx.renderer, lb,
                                       b.x + b.w - pad + (pad - tw) * 0.5f,
                                       ty, fs, bCol);
        }
    }

    // Track.
    const float trackH = std::min(b.h, 6.0f);
    const float trackY = b.y + (b.h - trackH) * 0.5f;
    ctx.renderer->drawRect(trackX, trackY, trackW, trackH, p.controlBg);

    // Filled halves. frac = 1.0 (all A) at left → handle at trackX.
    const float frac = xf.value() / 100.0f;           // 1 = full A
    const float handleX = trackX + (1.0f - frac) * trackW;
    const Color colA = xf.isEnabled() ? xf.colorA() : p.textDim;
    const Color colB = xf.isEnabled() ? xf.colorB() : p.textDim;
    // Left half — A side
    ctx.renderer->drawRect(trackX, trackY, handleX - trackX, trackH, colA);
    // Right half — B side
    ctx.renderer->drawRect(handleX, trackY,
                            trackX + trackW - handleX, trackH, colB);

    // Handle — small vertical bar at the current position.
    const float handleW = 6.0f;
    const float handleH = std::min(b.h - 2.0f, 16.0f);
    ctx.renderer->drawRect(handleX - handleW * 0.5f,
                            b.y + (b.h - handleH) * 0.5f,
                            handleW, handleH, p.textPrimary);
    ctx.renderer->drawRectOutline(handleX - handleW * 0.5f,
                                   b.y + (b.h - handleH) * 0.5f,
                                   handleW, handleH, p.border, 1.0f);
}

// ─── ScrollBar ─────────────────────────────────────────────────────

static void paintScrollBar(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& sb = static_cast<FwScrollBar&>(w);
    const Rect& b = sb.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;

    // Track.
    ctx.renderer->drawRect(b.x, b.y, b.w, b.h, p.controlBg);

    // Thumb. Hidden when contentSize <= viewport (nothing to scroll).
    const float tw = sb.thumbWidth();
    if (tw <= 0.0f) return;
    const float tx = sb.thumbX();

    Color thumbCol = p.controlActive;
    if (!sb.isEnabled())   thumbCol = p.textDim;
    else if (sb.isDragging()) thumbCol = p.accent;
    else if (sb.isHovered())  thumbCol = p.controlHover;

    // Small vertical inset so the thumb doesn't touch the track edges.
    const float inset = 1.0f;
    ctx.renderer->drawRect(tx, b.y + inset, tw, b.h - inset * 2.0f, thumbCol);
}

// ─── Meter ──────────────────────────────────────────────────────────

static void paintMeter(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& m = static_cast<FwMeter&>(w);
    const Rect& b = m.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& pal = theme().palette;

    // Two bars, side-by-side, with a 2 px gap.
    const float halfW = b.w * 0.5f - 1.0f;
    const Color bg{20, 20, 22, 255};
    ctx.renderer->drawRect(b.x, b.y, halfW, b.h, bg);
    ctx.renderer->drawRect(b.x + halfW + 2.0f, b.y, halfW, b.h, bg);

    // Peak heights (bottom-up fill).
    const float hL = FwMeter::dbToHeight(m.peakL()) * b.h;
    const float hR = FwMeter::dbToHeight(m.peakR()) * b.h;
    ctx.renderer->drawRect(b.x, b.y + b.h - hL, halfW, hL,
                            m.isEnabled() ? FwMeter::meterColor(m.peakL())
                                          : pal.textDim);
    ctx.renderer->drawRect(b.x + halfW + 2.0f, b.y + b.h - hR, halfW, hR,
                            m.isEnabled() ? FwMeter::meterColor(m.peakR())
                                          : pal.textDim);
}

// ─── NumberInput ───────────────────────────────────────────────────

static void paintNumberInput(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& n = static_cast<FwNumberInput&>(w);
    const Rect& b = n.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // Background — accent-tinted while editing so the text-entry
    // state is obviously different from the drag-to-change state.
    Color bg = n.isEnabled() ? p.controlBg : Color{40, 40, 45, 255};
    if (n.isEditing())        bg = p.elevated;
    else if (n.isPressed())   bg = p.controlActive;
    else if (n.isHovered())   bg = p.controlHover;
    ctx.renderer->drawRect(b.x, b.y, b.w, b.h, bg);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h,
                                   n.isEditing() ? p.accent : p.border,
                                   n.isEditing() ? 1.5f : 1.0f);

    // Text — edit buffer while typing, formatted value otherwise.
    if (!ctx.textMetrics) return;
    const float fs = m.fontSize;
    const float lh = ctx.textMetrics->lineHeight(fs);
    const bool editing = n.isEditing();
    const std::string display =
        editing ? n.editBuffer() : n.formattedValue();
    const float tw = ctx.textMetrics->textWidth(display, fs);
    const float tx = b.x + (b.w - tw) * 0.5f;
    const float ty = b.y + (b.h - lh) * 0.5f - lh * 0.15f;
    const Color tc = n.isEnabled() ? p.textPrimary : p.textDim;
    ctx.textMetrics->drawText(*ctx.renderer, display, tx, ty, fs, tc);

    if (editing) {
        // Caret — thin vertical bar at end of buffer.
        const float caretX  = tx + tw + 1.0f;
        const float caretY0 = ty;
        const float caretY1 = ty + lh * 0.9f;
        ctx.renderer->drawLine(caretX, caretY0, caretX, caretY1, p.accent,
                                1.5f);
    }
}

// ─── TextInput ──────────────────────────────────────────────────────

static void paintTextInput(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& t = static_cast<FwTextInput&>(w);
    const Rect& b = t.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;

    // Background: elevated while editing, control-bg otherwise.
    const bool  editing = t.isEditing();
    Color bg = t.isEnabled() ? p.controlBg : Color{40, 40, 45, 255};
    if (editing)              bg = p.elevated;
    else if (t.isHovered())   bg = p.controlHover;
    ctx.renderer->drawRect(b.x, b.y, b.w, b.h, bg);
    ctx.renderer->drawRectOutline(b.x, b.y, b.w, b.h,
                                   editing ? p.accent : p.border,
                                   editing ? 1.5f : 1.0f);

    // Text / placeholder.
    if (!ctx.textMetrics) return;
    const float fs     = m.fontSize;
    const float lh     = ctx.textMetrics->lineHeight(fs);
    const float padX   = m.baseUnit;
    const float tx0    = b.x + padX;
    // Text top (v1 Font::drawText interprets y as the glyph-bounding-
    // box top). The -0.15 lh shim counteracts stbtt's baseline offset
    // so text visually centres.
    const float ty     = b.y + (b.h - lh) * 0.5f - lh * 0.15f;

    const bool showPlaceholder = !editing && t.text().empty() && !t.placeholder().empty();
    const std::string& display = showPlaceholder ? t.placeholder() : t.text();
    const Color tc =
        showPlaceholder ? p.textDim :
        (t.isEnabled() ? p.textPrimary : p.textDim);

    // Clip so long text doesn't escape the box to the right.
    ctx.renderer->pushClip(b.x + padX * 0.5f, b.y, b.w - padX, b.h);
    ctx.textMetrics->drawText(*ctx.renderer, display, tx0, ty, fs, tc);

    // Caret at cursor position while editing. Size + centre it to
    // match the glyph cap-to-baseline range (≈ 0.7 lh), vertically
    // centred inside the field — looks balanced against the text
    // regardless of the shim above.
    if (editing) {
        // Measure up to the cursor byte offset. If the cursor sits in
        // the middle of a multi-byte char (shouldn't under the SM now
        // that Backspace/arrows are UTF-8-aware, but be defensive),
        // back it up to the prior codepoint boundary.
        int c = t.cursor();
        while (c > 0 &&
                (static_cast<unsigned char>(t.text()[c]) & 0xC0) == 0x80)
            --c;
        const std::string pre = t.text().substr(0, c);
        const float caretX = tx0 + ctx.textMetrics->textWidth(pre, fs);
        const float caretH = lh * 0.75f;
        const float caretY = b.y + (b.h - caretH) * 0.5f;
        ctx.renderer->drawLine(caretX, caretY, caretX, caretY + caretH,
                                p.accent, 1.5f);
    }
    ctx.renderer->popClip();
}

// ─── MenuBar ────────────────────────────────────────────────────────

static void paintMenuBar(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& mb = static_cast<FwMenuBar&>(w);
    const Rect& b = mb.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const float fs = m.fontSize;

    // Bar background + bottom separator.
    ctx.renderer->drawRect(b.x, b.y, b.w, b.h, Color{38, 38, 42, 255});
    ctx.renderer->drawRect(b.x, b.y + b.h - 1.0f, b.w, 1.0f,
                            Color{55, 55, 60, 255});

    // Per-title strips. Highlight open (accent) / hovered (soft).
    const int openIdx  = mb.openIndex();
    const int hoverIdx = mb.hoverIndex();
    for (int i = 0; i < static_cast<int>(mb.titleStrips().size()); ++i) {
        const auto& s = mb.titleStrips()[i];
        const float tx = b.x + s.x;
        const bool active = (i == openIdx);
        const bool hover  = (i == hoverIdx) && !active;
        if (active || hover) {
            const Color bg = active ? Color{60, 60, 65, 255}
                                    : Color{50, 50, 55, 255};
            ctx.renderer->drawRect(tx, b.y, s.w, b.h - 1.0f, bg);
        }
        // Centre the label within its strip.
        if (ctx.textMetrics) {
            const float lh = ctx.textMetrics->lineHeight(fs);
            const float tw = ctx.textMetrics->textWidth(s.label, fs);
            const float cx = tx + (s.w - tw) * 0.5f;
            const float cy = b.y + (b.h - lh) * 0.5f - lh * 0.15f;
            ctx.textMetrics->drawText(*ctx.renderer, s.label, cx, cy, fs,
                                       p.textPrimary);
        }
    }
}

// ─── ProgressBar ────────────────────────────────────────────────────

static void paintProgressBar(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& pb = static_cast<FwProgressBar&>(w);
    const Rect& b = pb.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const float radius = m.cornerRadius;

    // Track: background rounded rect.
    Color trackBg = p.controlBg;
    if (!pb.isEnabled()) trackBg = Color{50, 50, 53, 255};
    ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, radius, trackBg);

    if (pb.hasError()) {
        // Full-width fill in error color.
        ctx.renderer->drawRoundedRect(b.x, b.y, b.w, b.h, radius, p.error);
        return;
    }

    // Determine fill color: accent by default, complete-color when at
    // 100%, caller override wins over theme defaults.
    const bool complete = pb.isDeterminate() && pb.value() >= 0.999f;
    Color fillColor;
    if (complete) {
        fillColor = pb.completeColor().value_or(p.success);
    } else {
        fillColor = pb.accentColor().value_or(p.accent);
    }
    if (!pb.isEnabled()) fillColor = Color{100, 100, 100, 255};

    // Apply complete-fade alpha.
    if (complete) {
        const float a = pb.completeAlpha();
        fillColor.a = static_cast<uint8_t>(
            std::clamp(static_cast<float>(fillColor.a) * a, 0.0f, 255.0f));
    }

    if (pb.isDeterminate()) {
        const float v = std::clamp(pb.value(), 0.0f, 1.0f);
        if (pb.orientation() == ProgressOrientation::Horizontal) {
            const float fillW = b.w * v;
            if (fillW > 0.0f)
                ctx.renderer->drawRoundedRect(b.x, b.y, fillW, b.h,
                                               radius, fillColor);
        } else {
            // Vertical: grow bottom-up.
            const float fillH = b.h * v;
            if (fillH > 0.0f)
                ctx.renderer->drawRoundedRect(b.x, b.y + b.h - fillH,
                                               b.w, fillH, radius, fillColor);
        }
        return;
    }

    // Indeterminate: sweep. Phase in [0,1] drives position; we
    // bounce-map to create a ping-pong so the sweep runs back and
    // forth across the track.
    const float phase = pb.sweepPhase();
    const float bounced = 1.0f - std::fabs(phase * 2.0f - 1.0f);   // triangle wave in [0,1]
    const float sweepFrac = 0.25f;   // sweep width as fraction of track
    if (pb.orientation() == ProgressOrientation::Horizontal) {
        const float sweepW = b.w * sweepFrac;
        const float startX = b.x + (b.w - sweepW) * bounced;
        ctx.renderer->drawRoundedRect(startX, b.y, sweepW, b.h,
                                       radius, fillColor);
    } else {
        const float sweepH = b.h * sweepFrac;
        const float startY = b.y + (b.h - sweepH) * bounced;
        ctx.renderer->drawRoundedRect(b.x, startY, b.w, sweepH,
                                       radius, fillColor);
    }
}

// ─── RadioButton ────────────────────────────────────────────────────

static void paintRadioButton(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& rb = static_cast<FwRadioButton&>(w);
    const Rect& b = rb.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;
    const ThemeMetrics& m = theme().metrics;
    const Color accent = rb.accentColor().value_or(p.accent);

    const float circleSize = m.controlHeight * 0.6f;
    const float cx = b.x + circleSize * 0.5f;
    const float cy = b.y + b.h * 0.5f;
    const float r  = circleSize * 0.5f;

    // Outer circle fill.
    Color outerBg = p.controlBg;
    if (!rb.isEnabled())      outerBg = Color{50, 50, 53, 255};
    else if (rb.isPressed())  outerBg = p.controlActive;
    else if (rb.isHovered())  outerBg = p.controlHover;
    ctx.renderer->drawFilledCircle(cx, cy, r, outerBg, 20);

    // Border — accent when selected, normal border otherwise. Simulate
    // stroke by overdrawing a slightly smaller background circle.
    const Color borderColor = rb.isSelected() ? accent : p.border;
    ctx.renderer->drawFilledCircle(cx, cy, r,      borderColor, 20);
    ctx.renderer->drawFilledCircle(cx, cy, r - 1.2f, outerBg,   20);

    // Inner dot when selected.
    if (rb.isSelected()) {
        const Color dot = rb.isEnabled() ? accent : p.textDim;
        ctx.renderer->drawFilledCircle(cx, cy, r * 0.5f, dot, 16);
    }

    // Label right of the circle.
    if (ctx.textMetrics && !rb.label().empty()) {
        const float fontSize = m.fontSize;
        const float gap      = m.baseUnit;
        const float lh       = ctx.textMetrics->lineHeight(fontSize);
        const float tx       = b.x + circleSize + gap;
        const float ty       = b.y + (b.h - lh) * 0.5f - lh * 0.15f;
        const Color tc       = rb.isEnabled() ? p.textPrimary : p.textDim;
        ctx.textMetrics->drawText(*ctx.renderer, rb.label(), tx, ty, fontSize, tc);
    }
}

// ─── ScrollView ─────────────────────────────────────────────────────

static void paintScrollView(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& sv = static_cast<ScrollView&>(w);
    const Rect& b = sv.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p  = theme().palette;
    const ThemeMetrics& tm = theme().metrics;
    const float barT = sv.scrollbarThickness();
    const Size  vp   = sv.viewportSize();

    // Optional solid background.
    if (sv.backgroundColor()) {
        ctx.renderer->drawRect(b.x, b.y, b.w, b.h, *sv.backgroundColor());
    }

    // Clip to the viewport, render the content, unclip.
    ctx.renderer->pushClip(b.x, b.y, vp.w, vp.h);
    if (Widget* content = sv.content()) content->render(ctx);
    ctx.renderer->popClip();

    // Vertical bar along right edge.
    if (sv.showVerticalBar()) {
        const float trackX = b.x + b.w - barT;
        const float trackY = b.y;
        const float trackH = vp.h;
        ctx.renderer->drawRect(trackX, trackY, barT, trackH, p.controlBg);

        const Size cs = sv.contentSize();
        if (cs.h > vp.h && vp.h > 0.0f) {
            const float thumbH = std::max(20.0f, (vp.h / cs.h) * trackH);
            const Point mx = sv.maxScrollOffset();
            const float ratio = (mx.y > 0.0f) ? (sv.scrollOffset().y / mx.y) : 0.0f;
            const float thumbY = trackY + (trackH - thumbH) * ratio;
            ctx.renderer->drawRoundedRect(trackX + 2.0f, thumbY,
                                           barT - 4.0f, thumbH,
                                           tm.cornerRadius, p.border);
        }
    }
    // Horizontal bar along bottom edge.
    if (sv.showHorizontalBar()) {
        const float trackX = b.x;
        const float trackY = b.y + b.h - barT;
        const float trackW = vp.w;
        ctx.renderer->drawRect(trackX, trackY, trackW, barT, p.controlBg);

        const Size cs = sv.contentSize();
        if (cs.w > vp.w && vp.w > 0.0f) {
            const float thumbW = std::max(20.0f, (vp.w / cs.w) * trackW);
            const Point mx = sv.maxScrollOffset();
            const float ratio = (mx.x > 0.0f) ? (sv.scrollOffset().x / mx.x) : 0.0f;
            const float thumbX = trackX + (trackW - thumbW) * ratio;
            ctx.renderer->drawRoundedRect(thumbX, trackY + 2.0f,
                                           thumbW, barT - 4.0f,
                                           tm.cornerRadius, p.border);
        }
    }
}

// ─── TabView ────────────────────────────────────────────────────────

static void paintTabView(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& tv = static_cast<TabView&>(w);
    const Rect& b = tv.bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    const ThemePalette& p  = theme().palette;
    const ThemeMetrics& tm = theme().metrics;

    // Content area background — same as the parent panel. Painted
    // first so the active-tab content draws on top.
    const Rect content = tv.contentAreaRect();
    ctx.renderer->drawRect(content.x, content.y, content.w, content.h,
                            p.panelBg);

    // Strip background + bottom divider.
    const float stripH = tv.tabStripHeight();
    ctx.renderer->drawRect(b.x, b.y, b.w, stripH, p.panelBg);
    ctx.renderer->drawRect(b.x, b.y + stripH - 1.0f, b.w, 1.0f, p.border);

    // Each tab button.
    const int active = tv.activeTabIndex();
    const int hover  = tv.hoverIndex();
    const float fontSize = tm.fontSize;

    for (int i = 0; i < tv.tabCount(); ++i) {
        const auto& t = tv.tabs()[i];
        const Rect& sr = t.stripRect;
        if (sr.w <= 0.0f) continue;

        const bool isActive = (i == active);
        const bool isHover  = (i == hover) && !isActive;

        Color bg;
        if (isActive)     bg = p.surface;
        else if (isHover) bg = p.controlHover;
        else              bg = p.controlBg;
        ctx.renderer->drawRect(sr.x, sr.y, sr.w, sr.h, bg);

        // Right-edge separator between inactive tabs.
        if (!isActive) {
            ctx.renderer->drawRect(sr.x + sr.w - 1.0f, sr.y + 4.0f,
                                    1.0f, sr.h - 8.0f, p.borderSubtle);
        }

        // Label centered.
        if (ctx.textMetrics && !t.label.empty()) {
            const float lh = ctx.textMetrics->lineHeight(fontSize);
            const float tw = ctx.textMetrics->textWidth(t.label, fontSize);
            const float tx = sr.x + (sr.w - tw) * 0.5f;
            const float ty = sr.y + (sr.h - lh) * 0.5f - lh * 0.15f;
            const Color tc = isActive ? p.textPrimary : p.textSecondary;
            ctx.textMetrics->drawText(*ctx.renderer, t.label, tx, ty,
                                       fontSize, tc);
        }

        // Active-tab indicator bar — accent colored, 2 px at bottom.
        if (isActive) {
            ctx.renderer->drawRect(sr.x, sr.y + sr.h - 2.0f,
                                    sr.w, 2.0f, p.accent);
        }
    }
}

// ─── SplitView ──────────────────────────────────────────────────────

static void paintSplitView(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& sv = static_cast<SplitView&>(w);
    const Rect d = sv.dividerRect();
    if (d.w <= 0.0f || d.h <= 0.0f) return;

    const ThemePalette& p = theme().palette;

    // Divider fill — accent while dragging, hover tint on mouseover,
    // border color at rest. Caller override wins over theme defaults.
    Color fill = p.border;
    if (sv.isDragging())         fill = p.accent;
    else if (sv.isDividerHover()) fill = p.controlHover;
    if (sv.dividerColor())       fill = *sv.dividerColor();

    ctx.renderer->drawRect(d.x, d.y, d.w, d.h, fill);

    // Grip dots — 3 small circles along the cross-axis centre. Helps
    // users spot the divider as interactive without reading a
    // separate label.
    const bool horiz = (sv.orientation() == SplitOrientation::Horizontal);
    const float cx = d.x + d.w * 0.5f;
    const float cy = d.y + d.h * 0.5f;
    const float spacing = 6.0f;
    const float r       = 1.3f;
    const Color dotCol  = p.textSecondary;
    for (int i = -1; i <= 1; ++i) {
        if (horiz) {
            ctx.renderer->drawFilledCircle(cx, cy + i * spacing, r, dotCol, 8);
        } else {
            ctx.renderer->drawFilledCircle(cx + i * spacing, cy, r, dotCol, 8);
        }
    }
}

// ─── ContentGrid ────────────────────────────────────────────────────
// Paints just the two divider bars (+ intersection) on top of children.
// Children have already been rendered by ContentGrid::render() before
// Widget::render() calls this painter.

static void paintContentGrid(Widget& w, UIContext& ctx) {
    if (!ctx.renderer) return;
    auto& cg = static_cast<ContentGrid&>(w);
    const auto& d  = cg.paintData();
    const auto& p  = theme().palette;

    const bool anyH = d.hoverH || d.dragH;
    const bool anyV = d.hoverV || d.dragV;

    Color hCol = d.dragH ? p.accent
               : d.hoverH ? Color{80, 80, 90, 255}
               : Color{55, 55, 60, 255};
    Color vCol = d.dragV ? p.accent
               : d.hoverV ? Color{80, 80, 90, 255}
               : Color{55, 55, 60, 255};

    // Horizontal divider (full width)
    ctx.renderer->drawRect(d.bx, d.hDivY, d.bw, d.divSize, hCol);
    // Vertical divider (full height)
    ctx.renderer->drawRect(d.vDivX, d.by, d.divSize, d.bh, vCol);
    // Intersection highlight when either is active
    if (anyH || anyV)
        ctx.renderer->drawRect(d.vDivX, d.hDivY, d.divSize, d.divSize, p.accent);
}

// ─── Registration ──────────────────────────────────────────────────

void registerAllFw2Painters() {
    registerPainter(typeid(Label),      &paintLabel);
    registerPainter(typeid(FwButton),   &paintButton);
    registerPainter(typeid(FwFader),    &paintFader);
    registerPainter(typeid(FwDropDown), &paintDropDownButton);
    registerPainter(typeid(FwToggle),   &paintToggle);
    registerPainter(typeid(FwCheckbox), &paintCheckbox);
    registerPainter(typeid(FwKnob),     &paintKnob);
    registerPainter(typeid(FwStepSelector), &paintStepSelector);
    registerPainter(typeid(FwPan),      &paintPan);
    registerPainter(typeid(FwCrossfader), &paintCrossfader);
    registerPainter(typeid(FwScrollBar), &paintScrollBar);
    registerPainter(typeid(FwMeter),    &paintMeter);
    registerPainter(typeid(FwNumberInput), &paintNumberInput);
    registerPainter(typeid(FwTextInput), &paintTextInput);
    registerPainter(typeid(FwMenuBar),  &paintMenuBar);
    registerPainter(typeid(FwProgressBar), &paintProgressBar);
    registerPainter(typeid(FwRadioButton), &paintRadioButton);
    registerPainter(typeid(ScrollView),    &paintScrollView);
    registerPainter(typeid(TabView),       &paintTabView);
    registerPainter(typeid(SplitView),     &paintSplitView);
    registerPainter(typeid(ContentGrid),   &paintContentGrid);
    // FwRadioGroup has no painter — it overrides render() to paint
    // its buttons directly. Stack is pure layout, also painter-less.
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
