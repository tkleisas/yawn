#include "ScrollView.h"
#include "Painter.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

ScrollView::ScrollView() {
    setFocusable(true);
    setRelayoutBoundary(true);   // content size changes don't bubble out
}

ScrollView::~ScrollView() {
    // Widget::~Widget() handles parent/child detachment — nothing
    // extra to do here.
}

// ───────────────────────────────────────────────────────────────────
// Content
// ───────────────────────────────────────────────────────────────────

void ScrollView::setContent(Widget* content) {
    if (content == m_content) return;
    if (m_content) removeChild(m_content);
    m_content = content;
    if (m_content) addChild(m_content);
    m_scrollX = 0.0f;
    m_scrollY = 0.0f;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Scroll offset
// ───────────────────────────────────────────────────────────────────

void ScrollView::setScrollOffset(Point offset, ValueChangeSource src) {
    const float prevX = m_scrollX;
    const float prevY = m_scrollY;
    m_scrollX = offset.x;
    m_scrollY = offset.y;
    clampScroll();
    if (m_scrollX == prevX && m_scrollY == prevY) return;
    // Offset change doesn't affect our own size — but content must be
    // repositioned, so force onLayout to re-run next paint.
    invalidate();
    fireOnScroll(src);
}

Point ScrollView::maxScrollOffset() const {
    const Size vp = viewportSize();
    return {std::max(0.0f, m_contentSize.w - vp.w),
            std::max(0.0f, m_contentSize.h - vp.h)};
}

void ScrollView::scrollBy(Point delta) {
    setScrollOffset({m_scrollX + delta.x, m_scrollY + delta.y},
                     ValueChangeSource::User);
}

void ScrollView::scrollToTop()    { setScrollOffset({m_scrollX, 0.0f}); }
void ScrollView::scrollToBottom() {
    const Point m = maxScrollOffset();
    setScrollOffset({m_scrollX, m.y});
}

Size ScrollView::viewportSize() const {
    const float t = scrollbarThickness();
    const float vw = m_bounds.w - (showVerticalBar() ? t : 0.0f);
    const float vh = m_bounds.h - (showHorizontalBar() ? t : 0.0f);
    return {std::max(0.0f, vw), std::max(0.0f, vh)};
}

void ScrollView::setHorizontalOverflow(ScrollOverflow o) {
    if (o == m_hOverflow) return;
    m_hOverflow = o;
    invalidate();
}

void ScrollView::setVerticalOverflow(ScrollOverflow o) {
    if (o == m_vOverflow) return;
    m_vOverflow = o;
    invalidate();
}

void ScrollView::setScrollbarThickness(float px) {
    if (px == m_scrollbarThickness) return;
    m_scrollbarThickness = px;
    invalidate();
}

float ScrollView::scrollbarThickness() const {
    return (m_scrollbarThickness > 0.0f)
        ? m_scrollbarThickness
        : theme().metrics.scrollbarThickness;
}

bool ScrollView::showVerticalBar() const {
    switch (m_vOverflow) {
        case ScrollOverflow::Always:
        case ScrollOverflow::Scroll: return true;
        case ScrollOverflow::Never:  return false;
        case ScrollOverflow::Auto:
            return m_contentSize.h > m_bounds.h + 0.5f;
    }
    return false;
}

bool ScrollView::showHorizontalBar() const {
    switch (m_hOverflow) {
        case ScrollOverflow::Always:
        case ScrollOverflow::Scroll: return true;
        case ScrollOverflow::Never:  return false;
        case ScrollOverflow::Auto:
            return m_contentSize.w > m_bounds.w + 0.5f;
    }
    return false;
}

void ScrollView::clampScroll() {
    const Point m = maxScrollOffset();
    m_scrollX = std::clamp(m_scrollX, 0.0f, m.x);
    m_scrollY = std::clamp(m_scrollY, 0.0f, m.y);
}

void ScrollView::fireOnScroll(ValueChangeSource src) {
    if (src == ValueChangeSource::Automation) return;
    if (m_onScroll) m_onScroll({m_scrollX, m_scrollY});
}

// ───────────────────────────────────────────────────────────────────
// Measure / layout
// ───────────────────────────────────────────────────────────────────

Size ScrollView::onMeasure(Constraints c, UIContext& ctx) {
    (void)ctx;
    // ScrollView fills whatever the parent gives it. We don't try to
    // report an intrinsic size — a scroll viewport with no height is
    // pointless, so callers are expected to give us a bounded
    // constraint.
    float w = (c.maxW > 0.0f && c.maxW < 1e8f) ? c.maxW : 400.0f;
    float h = (c.maxH > 0.0f && c.maxH < 1e8f) ? c.maxH : 300.0f;
    w = std::max(w, c.minW);
    h = std::max(h, c.minH);
    return {w, h};
}

void ScrollView::onLayout(Rect bounds, UIContext& ctx) {
    if (!m_content) {
        m_contentSize = {0.0f, 0.0f};
        return;
    }

    const float barT = scrollbarThickness();
    const bool  reservedV = (m_vOverflow == ScrollOverflow::Always ||
                              m_vOverflow == ScrollOverflow::Scroll);
    const bool  reservedH = (m_hOverflow == ScrollOverflow::Always ||
                              m_hOverflow == ScrollOverflow::Scroll);
    const bool  hScrollEnabled = (m_hOverflow != ScrollOverflow::Never);
    const bool  vScrollEnabled = (m_vOverflow != ScrollOverflow::Never);

    auto measureContent = [&](float viewportW, float viewportH) {
        Constraints cc;
        cc.minW = hScrollEnabled ? 0.0f : viewportW;
        cc.maxW = hScrollEnabled ? 1e8f : viewportW;
        cc.minH = vScrollEnabled ? 0.0f : viewportH;
        cc.maxH = vScrollEnabled ? 1e8f : viewportH;
        m_contentSize = m_content->measure(cc, ctx);
    };

    // First pass assumes reserved bars take space; Auto overflow
    // doesn't reserve yet — we'll discover.
    float vw = bounds.w - (reservedV ? barT : 0.0f);
    float vh = bounds.h - (reservedH ? barT : 0.0f);
    measureContent(vw, vh);

    // Auto policy may now enable bars — run a corrective pass once.
    const bool wantV = (m_vOverflow == ScrollOverflow::Auto &&
                         m_contentSize.h > vh + 0.5f);
    const bool wantH = (m_hOverflow == ScrollOverflow::Auto &&
                         m_contentSize.w > vw + 0.5f);
    if (wantV || wantH) {
        const float vw2 = bounds.w - ((reservedV || wantV) ? barT : 0.0f);
        const float vh2 = bounds.h - ((reservedH || wantH) ? barT : 0.0f);
        measureContent(vw2, vh2);
        vw = vw2;
        vh = vh2;
    }

    clampScroll();

    // Position content offset by -scrollOffset. Content bounds use
    // absolute coords like everything else.
    m_content->layout(Rect{bounds.x - m_scrollX,
                            bounds.y - m_scrollY,
                            m_contentSize.w, m_contentSize.h}, ctx);
}

// ───────────────────────────────────────────────────────────────────
// Render
// ───────────────────────────────────────────────────────────────────
//
// ScrollView's paint path is split: the painter registered at
// typeid(ScrollView) in Fw2Painters.cpp handles the renderer calls
// (background, clip push/pop, scrollbar indicators, AND calls
// content->render() in between). We override render() here so the
// default base class doesn't ALSO recurse children (which would paint
// the content a second time outside the clip).

void ScrollView::render(UIContext& ctx) {
    if (!m_visible) return;
    if (PaintFn fn = findPainter(typeid(*this))) {
        fn(*this, ctx);
    } else if (m_content) {
        // No registered painter (test harness / pre-wiring) — still
        // recurse so nested paintless widgets can do their bookkeeping.
        m_content->render(ctx);
    }
}

// ───────────────────────────────────────────────────────────────────
// Input
// ───────────────────────────────────────────────────────────────────

bool ScrollView::onScroll(ScrollEvent& e) {
    if (!m_enabled) return false;
    // Mouse wheel — dy positive = scroll up, negative = scroll down.
    // Shift+wheel scrolls horizontally. Ctrl+wheel passes through (for
    // zoom-capable children).
    if (e.modifiers & ModifierKey::Ctrl) return false;

    const bool shift = (e.modifiers & ModifierKey::Shift);
    float dx = 0.0f, dy = 0.0f;
    if (shift) dx = -e.dy * m_wheelMultiplier;
    else       { dx = -e.dx * m_wheelMultiplier;
                 dy = -e.dy * m_wheelMultiplier; }

    const Point before{m_scrollX, m_scrollY};
    scrollBy({dx, dy});
    const bool moved = (m_scrollX != before.x || m_scrollY != before.y);
    return moved;
}

bool ScrollView::onKeyDown(KeyEvent& e) {
    if (!m_enabled || e.consumed) return false;
    const float page = (m_pageStep > 0.0f)
        ? m_pageStep
        : std::max(40.0f, viewportSize().h - 40.0f);

    switch (e.key) {
        case Key::Up:       scrollBy({0, -m_arrowStep}); return true;
        case Key::Down:     scrollBy({0,  m_arrowStep}); return true;
        case Key::Left:     scrollBy({-m_arrowStep, 0}); return true;
        case Key::Right:    scrollBy({ m_arrowStep, 0}); return true;
        case Key::PageUp:   scrollBy({0, -page}); return true;
        case Key::PageDown: scrollBy({0,  page}); return true;
        case Key::Home:     setScrollOffset({0.0f, 0.0f}, ValueChangeSource::User); return true;
        case Key::End: {
            const Point m = maxScrollOffset();
            setScrollOffset(m, ValueChangeSource::User);
            return true;
        }
        default: return false;
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
