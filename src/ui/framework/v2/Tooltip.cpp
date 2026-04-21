#include "Tooltip.h"

#include "Theme.h"
#include "UIContext.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Painter hook storage
// ───────────────────────────────────────────────────────────────────

namespace {
TooltipManager::PaintFn& painterSlot() {
    static TooltipManager::PaintFn fn = nullptr;
    return fn;
}
// Fallback when ctx.textMetrics is null (tests).
constexpr float kFallbackPxPerChar = 8.0f;

int utf8CodepointCount(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

float measureText(const std::string& s, UIContext& ctx, float fontSize) {
    if (s.empty()) return 0.0f;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(s, fontSize);
    return static_cast<float>(utf8CodepointCount(s)) * kFallbackPxPerChar;
}
float measureLineHeight(UIContext& ctx, float fontSize) {
    if (ctx.textMetrics) return ctx.textMetrics->lineHeight(fontSize);
    return fontSize * 1.2f;
}
} // anon

void    TooltipManager::setPainter(PaintFn fn) { painterSlot() = fn; }
TooltipManager::PaintFn TooltipManager::painter()      { return painterSlot(); }

// ───────────────────────────────────────────────────────────────────
// Singleton
// ───────────────────────────────────────────────────────────────────

TooltipManager& TooltipManager::instance() {
    static TooltipManager g;
    return g;
}

// ───────────────────────────────────────────────────────────────────
// Registration
// ───────────────────────────────────────────────────────────────────

void TooltipManager::attach(Widget* w, std::string text) {
    if (!w) return;
    if (text.empty()) { detach(w); return; }
    m_tooltips[w] = std::move(text);
    // If the user attached text to the already-hovered widget, refresh
    // the visible bubble (common case: text depends on state).
    if (m_hover == w && m_handle.active()) {
        m_currentText = m_tooltips[w];
    }
}

void TooltipManager::detach(Widget* w) {
    if (!w) return;
    m_tooltips.erase(w);
    if (m_hover == w) {
        m_hover      = nullptr;
        m_hoverTimer = 0.0f;
        hideOverlay();
    }
}

bool TooltipManager::isAttached(Widget* w) const {
    return m_tooltips.find(w) != m_tooltips.end();
}

void TooltipManager::clearAll() {
    m_tooltips.clear();
    m_hover      = nullptr;
    m_hoverTimer = 0.0f;
    hideOverlay();
}

// ───────────────────────────────────────────────────────────────────
// Pointer tracking
// ───────────────────────────────────────────────────────────────────

void TooltipManager::onPointerMoved(float sx, float sy) {
    // Find the first attached widget whose global bounds contain the
    // pointer and which is currently visible. Iteration order matches
    // unordered_map's — tooltip attachment is 1:1 per widget so the
    // order is irrelevant unless two attached widgets overlap, which
    // is rare (and if it happens, the first win is acceptable).
    Widget* found = nullptr;
    for (auto& kv : m_tooltips) {
        Widget* w = kv.first;
        if (!w || !w->isVisible()) continue;
        Rect gb = w->globalBounds();
        if (gb.contains(sx, sy)) { found = w; break; }
    }

    if (found == m_hover) return;   // still on the same target (or still none)

    // Target changed — reset timer and hide any visible bubble. Next
    // tick will start counting down against the new target.
    m_hover      = found;
    m_hoverTimer = 0.0f;
    hideOverlay();
}

void TooltipManager::onPointerLeft() {
    m_hover      = nullptr;
    m_hoverTimer = 0.0f;
    hideOverlay();
}

// ───────────────────────────────────────────────────────────────────
// Frame tick — advance timer, show when expired
// ───────────────────────────────────────────────────────────────────

void TooltipManager::tick(float dtSec) {
    if (!m_hover) return;           // nothing to count for
    if (m_handle.active()) return;  // already visible

    m_hoverTimer += dtSec;
    if (m_hoverTimer < m_showDelay) return;

    // Show if we can.
    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;    // no stack → can't push

    auto it = m_tooltips.find(m_hover);
    if (it == m_tooltips.end() || it->second.empty()) return;
    showFor(m_hover, ctx);
}

// ───────────────────────────────────────────────────────────────────
// Geometry — 4-direction fallback + viewport clamp
// ───────────────────────────────────────────────────────────────────

namespace {
Rect computeTooltipBounds(UIContext& ctx, const Rect& anchor,
                           const std::string& text) {
    const ThemeMetrics& m = theme().metrics;
    const float fontSize  = m.fontSizeSmall;
    const float padX      = m.baseUnit * 2.0f;   // 8 px horiz
    const float padY      = m.baseUnit * 0.75f;  // 3 px vert
    const float gap       = m.baseUnit;          // 4 px between anchor + bubble

    const float textW = measureText(text, ctx, fontSize);
    const float lineH = measureLineHeight(ctx, fontSize);

    const float popupW = textW + padX * 2.0f;
    const float popupH = lineH + padY * 2.0f;

    // Four candidate placements — centred on the anchor for below/above,
    // vertically centred for right/left.
    const float cx = anchor.x + (anchor.w - popupW) * 0.5f;
    const float cy = anchor.y + (anchor.h - popupH) * 0.5f;

    const Rect below{cx,                      anchor.y + anchor.h + gap, popupW, popupH};
    const Rect above{cx,                      anchor.y - popupH - gap,   popupW, popupH};
    const Rect right{anchor.x + anchor.w + gap, cy,                       popupW, popupH};
    const Rect left {anchor.x - popupW - gap,   cy,                       popupW, popupH};

    const Rect& v = ctx.viewport;
    auto fitsIn = [&](const Rect& r) {
        return r.x >= v.x && r.y >= v.y &&
               r.x + r.w <= v.x + v.w &&
               r.y + r.h <= v.y + v.h;
    };

    Rect chosen;
    if      (fitsIn(below)) chosen = below;
    else if (fitsIn(above)) chosen = above;
    else if (fitsIn(right)) chosen = right;
    else if (fitsIn(left))  chosen = left;
    else                    chosen = below;   // last-resort: force clamp

    // Clamp so the bubble never pokes past a viewport edge.
    chosen.x = std::max(chosen.x, v.x);
    chosen.y = std::max(chosen.y, v.y);
    if (chosen.x + chosen.w > v.x + v.w) chosen.x = v.x + v.w - chosen.w;
    if (chosen.y + chosen.h > v.y + v.h) chosen.y = v.y + v.h - chosen.h;

    return chosen;
}
} // anon

// ───────────────────────────────────────────────────────────────────
// Show / hide
// ───────────────────────────────────────────────────────────────────

void TooltipManager::showFor(Widget* w, UIContext& ctx) {
    m_currentText   = m_tooltips[w];
    m_currentBounds = computeTooltipBounds(ctx, w->globalBounds(), m_currentText);

    OverlayEntry entry;
    entry.debugName             = "Tooltip";
    entry.bounds                = m_currentBounds;
    entry.modal                 = false;
    entry.dismissOnOutsideClick = false;   // clicks never dismiss tooltips
    // Tooltips don't participate in hit-test at all — pointer just
    // phases through. Spec: "no hit-test".
    entry.customHitTest = [](float, float) { return false; };
    entry.paint = [this](UIContext& c) {
        if (PaintFn fn = painterSlot()) {
            fn(m_currentBounds, m_currentText, c);
        }
    };

    m_handle = ctx.layerStack->push(OverlayLayer::Tooltip, std::move(entry));
}

void TooltipManager::hideOverlay() {
    if (m_handle.active()) m_handle.remove();
}

} // namespace fw2
} // namespace ui
} // namespace yawn
