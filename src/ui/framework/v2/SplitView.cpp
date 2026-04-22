#include "SplitView.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

SplitView::SplitView() {
    setFocusable(false);
    setRelayoutBoundary(true);   // child resize doesn't bubble past us
}

SplitView::SplitView(SplitOrientation o) : SplitView() {
    m_orientation = o;
}

// ───────────────────────────────────────────────────────────────────
// Panes
// ───────────────────────────────────────────────────────────────────

void SplitView::setFirst(Widget* pane) {
    if (m_first == pane) return;
    if (m_first) removeChild(m_first);
    m_first = pane;
    if (m_first) addChild(m_first);
    invalidate();
}

void SplitView::setSecond(Widget* pane) {
    if (m_second == pane) return;
    if (m_second) removeChild(m_second);
    m_second = pane;
    if (m_second) addChild(m_second);
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Orientation
// ───────────────────────────────────────────────────────────────────

void SplitView::setOrientation(SplitOrientation o) {
    if (o == m_orientation) return;
    m_orientation = o;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Position / mode
// ───────────────────────────────────────────────────────────────────

void SplitView::setRatio(float r) {
    m_mode  = SplitMode::Ratio;
    m_ratio = std::clamp(r, 0.0f, 1.0f);
    invalidate();
}

void SplitView::setFirstPaneSize(float px) {
    m_mode    = SplitMode::FixedStart;
    m_fixedPx = std::max(0.0f, px);
    invalidate();
}

void SplitView::setSecondPaneSize(float px) {
    m_mode    = SplitMode::FixedEnd;
    m_fixedPx = std::max(0.0f, px);
    invalidate();
}

float SplitView::dividerRatio() const {
    const float avail = (m_orientation == SplitOrientation::Horizontal)
        ? m_bounds.w - dividerThickness()
        : m_bounds.h - dividerThickness();
    if (avail <= 0.0f) return m_ratio;
    return std::clamp(m_actualFirstSize / avail, 0.0f, 1.0f);
}

float SplitView::firstPaneSize()  const { return m_actualFirstSize; }
float SplitView::secondPaneSize() const {
    const float avail = (m_orientation == SplitOrientation::Horizontal)
        ? m_bounds.w - dividerThickness()
        : m_bounds.h - dividerThickness();
    return std::max(0.0f, avail - m_actualFirstSize);
}

// ───────────────────────────────────────────────────────────────────
// Constraints
// ───────────────────────────────────────────────────────────────────

void SplitView::setConstraints(SplitPane pane, SplitConstraints c) {
    c.minSize = std::max(0.0f, c.minSize);
    c.maxSize = std::max(c.minSize, c.maxSize);
    if (pane == SplitPane::First)   m_firstC  = c;
    else                             m_secondC = c;
    invalidate();
}

SplitConstraints SplitView::constraints(SplitPane pane) const {
    return (pane == SplitPane::First) ? m_firstC : m_secondC;
}

// ───────────────────────────────────────────────────────────────────
// Divider thickness
// ───────────────────────────────────────────────────────────────────

void SplitView::setDividerThickness(float px) {
    if (px == m_dividerThickness) return;
    m_dividerThickness = px;
    invalidate();
}

float SplitView::dividerThickness() const {
    return (m_dividerThickness > 0.0f)
        ? m_dividerThickness
        : theme().metrics.splitterThickness;
}

// ───────────────────────────────────────────────────────────────────
// Layout
// ───────────────────────────────────────────────────────────────────

Size SplitView::onMeasure(Constraints c, UIContext& ctx) {
    (void)ctx;
    float w = (c.maxW > 0.0f && c.maxW < 1e8f) ? c.maxW : 400.0f;
    float h = (c.maxH > 0.0f && c.maxH < 1e8f) ? c.maxH : 300.0f;
    w = std::max(w, c.minW);
    h = std::max(h, c.minH);
    return {w, h};
}

float SplitView::computeFirstSize(float available) const {
    float s = 0.0f;
    switch (m_mode) {
        case SplitMode::Ratio:      s = m_ratio * available;        break;
        case SplitMode::FixedStart: s = m_fixedPx;                  break;
        case SplitMode::FixedEnd:   s = available - m_fixedPx;      break;
    }
    // First must fit in [firstMin, firstMax].
    s = std::clamp(s, m_firstC.minSize, m_firstC.maxSize);
    // Second must meet its own min: firstSize ≤ available - secondMin.
    s = std::min(s, std::max(0.0f, available - m_secondC.minSize));
    // If that pushed us below firstMin, firstMin wins (second shrinks
    // below its own min — mins are a "best effort" contract when the
    // total space is too small to honour both).
    s = std::max(s, std::min(m_firstC.minSize, available));
    return s;
}

void SplitView::onLayout(Rect bounds, UIContext& ctx) {
    const float thickness = dividerThickness();
    const bool  horiz     = (m_orientation == SplitOrientation::Horizontal);
    const float totalMain = horiz ? bounds.w : bounds.h;
    const float available = std::max(0.0f, totalMain - thickness);

    float firstSize = computeFirstSize(available);
    m_actualFirstSize = firstSize;

    Rect fRect, dRect, sRect;
    if (horiz) {
        fRect = Rect{bounds.x,                         bounds.y, firstSize,             bounds.h};
        dRect = Rect{bounds.x + firstSize,             bounds.y, thickness,              bounds.h};
        sRect = Rect{bounds.x + firstSize + thickness, bounds.y, available - firstSize,  bounds.h};
    } else {
        fRect = Rect{bounds.x, bounds.y,                         bounds.w, firstSize};
        dRect = Rect{bounds.x, bounds.y + firstSize,             bounds.w, thickness};
        sRect = Rect{bounds.x, bounds.y + firstSize + thickness, bounds.w, available - firstSize};
    }
    m_divider = dRect;

    if (m_first)  m_first->layout(fRect, ctx);
    if (m_second) m_second->layout(sRect, ctx);
}

// ───────────────────────────────────────────────────────────────────
// Drag handling
// ───────────────────────────────────────────────────────────────────

void SplitView::commitFirstSize(float firstSize, float available) {
    firstSize = std::clamp(firstSize, m_firstC.minSize, m_firstC.maxSize);
    firstSize = std::min(firstSize, std::max(0.0f, available - m_secondC.minSize));
    firstSize = std::max(firstSize, 0.0f);

    switch (m_mode) {
        case SplitMode::Ratio:
            m_ratio = (available > 0.0f) ? (firstSize / available) : 0.5f;
            break;
        case SplitMode::FixedStart:
            m_fixedPx = firstSize;
            break;
        case SplitMode::FixedEnd:
            m_fixedPx = std::max(0.0f, available - firstSize);
            break;
    }
    invalidate();
}

void SplitView::fireMoved() {
    if (!m_onMoved) return;
    switch (m_mode) {
        case SplitMode::Ratio:      m_onMoved(m_ratio); break;
        case SplitMode::FixedStart: m_onMoved(m_fixedPx); break;
        case SplitMode::FixedEnd:   m_onMoved(m_fixedPx); break;
    }
}

bool SplitView::onMouseDown(MouseEvent& e) {
    if (!m_enabled) return false;
    if (e.button != MouseButton::Left) return false;
    // Only take over when the press lands on the divider — otherwise
    // let the gesture SM run but flag that we're not in a resize
    // interaction (prevents unrelated drags from nudging panes).
    if (m_divider.contains(e.x, e.y)) {
        m_pressedOnDivider   = true;
        m_dragStartFirstSize = m_actualFirstSize;
        // Capture so follow-up moves + release are routed here even if
        // the pointer leaves the divider strip — the gesture SM does
        // this for us when we return false, but we explicitly return
        // true so the divider press doesn't propagate to child hit-
        // tests (panes under the divider hit-test aren't there, but
        // this is belt-and-braces).
    } else {
        m_pressedOnDivider = false;
    }
    return false;   // let the SM capture + run drag detection
}

void SplitView::onDragStart(const DragEvent& /*e*/) {
    if (!m_pressedOnDivider) return;
    m_dragging = true;
}

void SplitView::onDrag(const DragEvent& e) {
    if (!m_pressedOnDivider) return;
    const bool  horiz     = (m_orientation == SplitOrientation::Horizontal);
    const float thickness = dividerThickness();
    const float totalMain = horiz ? m_bounds.w : m_bounds.h;
    const float available = std::max(0.0f, totalMain - thickness);
    const float delta     = horiz ? e.cumDx : e.cumDy;
    commitFirstSize(m_dragStartFirstSize + delta, available);
    fireMoved();
}

void SplitView::onDragEnd(const DragEvent& /*e*/) {
    m_pressedOnDivider = false;
    m_dragging         = false;
    fireMoved();   // final value
}

bool SplitView::onMouseMove(MouseMoveEvent& e) {
    // Hover tracking — painter uses m_hoverDivider to brighten the
    // track. We don't consume so children still see move events.
    const bool over = m_divider.contains(e.x, e.y);
    if (over != m_hoverDivider) m_hoverDivider = over;
    return false;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
