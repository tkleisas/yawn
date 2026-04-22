#include "ScrollBar.h"
#include "UIContext.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

FwScrollBar::FwScrollBar() {
    // Flexible width (stretches to fill), fixed thin height.
    setSizePolicy(SizePolicy::flex(1.0f));
    setRelayoutBoundary(true);
    setFocusable(false);    // no keyboard focus — scrolling is reactive
}

// ───────────────────────────────────────────────────────────────────
// State
// ───────────────────────────────────────────────────────────────────

void FwScrollBar::setContentSize(float size) {
    if (size == m_contentSize) return;
    m_contentSize = std::max(0.0f, size);
    m_scrollPos = clampVal(m_scrollPos, 0.0f, maxScroll());
    invalidate();
}

void FwScrollBar::setScrollPos(float pos) {
    const float clamped = clampVal(pos, 0.0f, maxScroll());
    if (clamped == m_scrollPos) return;
    m_scrollPos = clamped;
}

float FwScrollBar::maxScroll() const {
    return std::max(0.0f, m_contentSize - bounds().w);
}

// ───────────────────────────────────────────────────────────────────
// Thumb geometry
// ───────────────────────────────────────────────────────────────────

float FwScrollBar::thumbWidth() const {
    const float w = bounds().w;
    if (w <= 0.0f || m_contentSize <= w) return 0.0f;
    return std::max(m_minThumbW, w * (w / m_contentSize));
}

float FwScrollBar::thumbX() const {
    const Rect& b = bounds();
    const float ms = maxScroll();
    if (ms <= 0.0f) return b.x;
    const float tw = thumbWidth();
    const float frac = m_scrollPos / ms;
    return b.x + frac * (b.w - tw);
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwScrollBar::onMeasure(Constraints c, UIContext& /*ctx*/) {
    constexpr float kDefaultHeight = 12.0f;
    float w = 120.0f;                             // prefers flex; floor small
    float h = kDefaultHeight;
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

// ───────────────────────────────────────────────────────────────────
// Gesture handling
// ───────────────────────────────────────────────────────────────────

bool FwScrollBar::onMouseDown(MouseEvent& e) {
    if (!m_enabled) return false;
    if (e.button != MouseButton::Left) return false;
    if (m_contentSize <= bounds().w) return false;   // nothing to scroll

    const float tx = thumbX();
    const float tw = thumbWidth();

    if (e.x >= tx && e.x < tx + tw) {
        // Thumb grab — let the gesture SM drive the drag from here.
        // Return false so dispatchMouseDown arms the SM + captures.
        m_dragStartX      = e.x;
        m_dragStartScroll = m_scrollPos;
        return false;
    }

    // Click on the track outside the thumb — jump the thumb so its
    // LEFT edge lands at the click (matches common scrollbar UX: a
    // page-up / page-down feel when the jump crosses a viewport).
    const float ms = maxScroll();
    const float frac = (e.x - bounds().x) / std::max(1.0f, bounds().w);
    const float newPos = clampVal(frac * ms, 0.0f, ms);
    if (newPos != m_scrollPos) {
        m_scrollPos = newPos;
        if (m_onScroll) m_onScroll(m_scrollPos);
    }
    // Still return false so the SM can track a further drag from this
    // click (grab-and-go from track position).
    m_dragStartX      = e.x;
    m_dragStartScroll = m_scrollPos;
    return false;
}

void FwScrollBar::onDragStart(const DragEvent& /*e*/) {
    m_dragging = true;
    // m_dragStartX / m_dragStartScroll were seeded in onMouseDown so
    // we have the correct pre-drag state regardless of whether the
    // user started from the thumb or the track.
}

void FwScrollBar::onDrag(const DragEvent& e) {
    if (!m_enabled) return;
    if (m_contentSize <= bounds().w) return;
    // Map thumb travel (along the scrollbar's width minus thumb) back
    // to a content delta. Use current-vs-startScreen rather than the
    // per-tick dx so accumulated rounding doesn't drift.
    const float dx   = e.currentScreen.x - m_dragStartX;
    const float tw   = thumbWidth();
    const float travel = std::max(1.0f, bounds().w - tw);
    const float delta  = dx * (maxScroll() / travel);
    const float newPos = clampVal(m_dragStartScroll + delta, 0.0f, maxScroll());
    if (newPos != m_scrollPos) {
        m_scrollPos = newPos;
        if (m_onScroll) m_onScroll(m_scrollPos);
    }
}

void FwScrollBar::onDragEnd(const DragEvent& /*e*/) {
    m_dragging = false;
}

bool FwScrollBar::onScroll(ScrollEvent& e) {
    if (!m_enabled) return false;
    if (m_contentSize <= bounds().w) return false;
    const float ms = maxScroll();
    const float newPos = clampVal(m_scrollPos - e.dx * 30.0f, 0.0f, ms);
    if (newPos != m_scrollPos) {
        m_scrollPos = newPos;
        if (m_onScroll) m_onScroll(m_scrollPos);
    }
    return true;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
