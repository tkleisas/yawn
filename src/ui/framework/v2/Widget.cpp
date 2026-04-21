#include "Widget.h"
#include "Painter.h"

#include <algorithm>
#include <typeinfo>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
// Single global capture slot — matches v1's model.
Widget* g_captured = nullptr;
} // anon

Widget::~Widget() {
    if (g_captured == this) g_captured = nullptr;
    // Orphan children — the framework is not RAII-owning them.
    for (auto* c : m_children) {
        if (c && c->m_parent == this) c->m_parent = nullptr;
    }
    if (m_parent) {
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this),
                        siblings.end());
        m_parent = nullptr;
    }
}

// ─── Tree ────────────────────────────────────────────────────────────

void Widget::addChild(Widget* child) {
    if (!child || child == this) return;
    if (child->m_parent) child->m_parent->removeChild(child);
    m_children.push_back(child);
    child->m_parent = this;
    invalidate();
}

void Widget::removeChild(Widget* child) {
    if (!child) return;
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parent = nullptr;
        m_children.erase(it);
        invalidate();
    }
}

void Widget::removeAllChildren() {
    if (m_children.empty()) return;
    for (auto* c : m_children) c->m_parent = nullptr;
    m_children.clear();
    invalidate();
}

// ─── Geometry ────────────────────────────────────────────────────────

Rect Widget::globalBounds() const {
    Rect gb = m_bounds;
    for (Widget* p = m_parent; p; p = p->m_parent) {
        gb.x += p->m_bounds.x;
        gb.y += p->m_bounds.y;
    }
    return gb;
}

Point Widget::toLocal(float sx, float sy) const {
    Rect gb = globalBounds();
    return {sx - gb.x, sy - gb.y};
}

Point Widget::toGlobal(float lx, float ly) const {
    Rect gb = globalBounds();
    return {gb.x + lx, gb.y + ly};
}

bool Widget::hitTestGlobal(float sx, float sy) const {
    return globalBounds().contains(sx, sy);
}

// ─── Cache & invalidation ────────────────────────────────────────────

Size Widget::measure(Constraints c, UIContext& ctx) {
    const int epoch = ctx.epoch();
    if (m_measure.globalEpoch == epoch
     && m_measure.lastLocalSeen == m_measure.localVersion
     && m_measure.lastConstraints == c) {
        return m_measure.lastResult;
    }
    Size s = onMeasure(c, ctx);
    ++m_measureCallCount;
    m_measure.lastConstraints = c;
    m_measure.lastResult      = s;
    m_measure.globalEpoch     = epoch;
    m_measure.lastLocalSeen   = m_measure.localVersion;
    return s;
}

void Widget::layout(Rect bounds, UIContext& ctx) {
    if (m_layout.globalEpoch == ctx.epoch()
     && m_layout.lastBounds == bounds
     && m_layout.measureVersion == m_measure.localVersion) {
        // Bounds are stored from last time; nothing to recompute.
        return;
    }
    m_bounds = bounds;
    onLayout(bounds, ctx);
    m_layout.lastBounds      = bounds;
    m_layout.globalEpoch     = ctx.epoch();
    m_layout.measureVersion  = m_measure.localVersion;
}

void Widget::onLayout(Rect bounds, UIContext& ctx) {
    // Default: children share the same bounds (useful for Stack-like
    // subclasses but otherwise a sane fallback).
    (void)bounds; (void)ctx;
    // Subclasses override to position children.
}

bool Widget::measureCacheValid(const Constraints& c, int epoch) const {
    return m_measure.globalEpoch == epoch
        && m_measure.lastLocalSeen == m_measure.localVersion
        && m_measure.lastConstraints == c;
}

void Widget::invalidate() {
    ++m_measure.localVersion;
    m_layout.measureVersion = -1;   // force re-layout on next call

    // Bubble up until first relayout boundary.
    for (Widget* p = m_parent; p; p = p->m_parent) {
        if (p->isRelayoutBoundary()) break;
        ++p->m_measure.localVersion;
        p->m_layout.measureVersion = -1;
    }
}

// ─── Boundaries ──────────────────────────────────────────────────────

bool Widget::isRelayoutBoundary() const {
    if (m_explicitBoundary.isSet) return m_explicitBoundary.value;
    // Auto-detect: fixed-size widgets (flexWeight=0) are boundaries.
    // A flexWeight=0 widget's size doesn't stretch to fill parent,
    // so its own invalidation doesn't need to tell the parent.
    // Note: this is a heuristic. Widgets whose measured size still
    // depends on their children (FlexBox with flex=0, for instance)
    // should explicitly setRelayoutBoundary(false).
    return m_sizePolicy.flexWeight == 0.0f;
}

void Widget::setRelayoutBoundary(bool b) { m_explicitBoundary.set(b); }

// ─── State setters ───────────────────────────────────────────────────

void Widget::setVisible(bool v) {
    if (m_visible != v) {
        m_visible = v;
        invalidate();
    }
}

void Widget::setEnabled(bool v) {
    if (m_enabled != v) {
        m_enabled = v;
        // Enabled state affects paint only; not layout.
        // No measure invalidation needed.
    }
}

void Widget::setSizePolicy(SizePolicy sp) {
    if (!(sp.flexWeight == m_sizePolicy.flexWeight
       && sp.minSize    == m_sizePolicy.minSize
       && sp.maxSize    == m_sizePolicy.maxSize)) {
        m_sizePolicy = sp;
        invalidate();
    }
}

void Widget::setPadding(Insets p) {
    if (p != m_padding) {
        m_padding = p;
        invalidate();
    }
}

// ─── Mouse capture ───────────────────────────────────────────────────

void Widget::captureMouse()      { g_captured = this; }
void Widget::releaseMouse()      { if (g_captured == this) g_captured = nullptr; }
bool Widget::hasMouseCapture() const { return g_captured == this; }
Widget* Widget::capturedWidget()     { return g_captured; }

// ─── Rendering ───────────────────────────────────────────────────────

void Widget::render(UIContext& ctx) {
    if (!m_visible) return;
    // Painter registry lookup — main exe registers per-type paint
    // functions at startup. If no painter is registered (e.g. in tests
    // or for pure-container widgets like FlexBox), fall back to the
    // virtual paint() which defaults to a no-op.
    if (PaintFn fn = findPainter(typeid(*this))) fn(*this, ctx);
    else paint(ctx);
    for (auto* child : m_children) {
        if (child) child->render(ctx);
    }
}

// ─── Find widget at ──────────────────────────────────────────────────

Widget* Widget::findWidgetAt(float sx, float sy) {
    if (!m_visible || !m_enabled) return nullptr;
    Point local = toLocal(sx, sy);
    if (!hitTest(local.x, local.y)) return nullptr;
    // Children (back-to-front = topmost first)
    for (int i = static_cast<int>(m_children.size()) - 1; i >= 0; --i) {
        Widget* hit = m_children[i]->findWidgetAt(sx, sy);
        if (hit) return hit;
    }
    return this;
}

// ─── Event dispatch with gesture state machine ───────────────────────

namespace {
bool shouldFireDoubleClick(const Widget::GestureState& gs,
                            const MouseEvent& release) {
    if (gs.lastClickEndMs == 0) return false;
    const uint64_t dt = release.timestampMs - gs.lastClickEndMs;
    if (dt > gesture::kDoubleClickWindowMs) return false;
    Point p{release.x, release.y};
    if (p.distanceTo(gs.lastClickScreen)
        > gesture::kDoubleClickPosToleranceLogical) return false;
    return true;
}

ClickEvent makeClickEvent(const MouseEvent& e, int count) {
    ClickEvent ce;
    ce.local  = {e.lx, e.ly};
    ce.screen = {e.x, e.y};
    ce.button = e.button;
    ce.modifiers = e.modifiers;
    ce.clickCount = count;
    ce.timestampMs = e.timestampMs;
    return ce;
}

DragEvent makeDragEventStart(const Widget::GestureState& gs,
                              const MouseMoveEvent& e, UIContext& ctx) {
    DragEvent de;
    de.startLocal   = gs.pressLocal;
    de.startScreen  = gs.pressScreen;
    de.currentLocal = {e.lx, e.ly};
    de.currentScreen = {e.x, e.y};
    de.dx = (e.x - gs.pressScreen.x) / ctx.dpiScale();
    de.dy = (e.y - gs.pressScreen.y) / ctx.dpiScale();
    de.cumDx = de.dx;
    de.cumDy = de.dy;
    de.button = gs.pressButton;
    de.modifiers = gs.pressMods;
    de.timestampMs = e.timestampMs;
    return de;
}

DragEvent makeDragEventContinue(const Widget::GestureState& gs,
                                 const MouseMoveEvent& e, UIContext& ctx) {
    DragEvent de;
    de.startLocal   = gs.pressLocal;
    de.startScreen  = gs.pressScreen;
    de.currentLocal = {e.lx, e.ly};
    de.currentScreen = {e.x, e.y};
    de.dx = (e.x - gs.lastMoveScreen.x) / ctx.dpiScale();
    de.dy = (e.y - gs.lastMoveScreen.y) / ctx.dpiScale();
    de.cumDx = (e.x - gs.pressScreen.x) / ctx.dpiScale();
    de.cumDy = (e.y - gs.pressScreen.y) / ctx.dpiScale();
    de.button = gs.pressButton;
    de.modifiers = gs.pressMods;
    de.timestampMs = e.timestampMs;
    return de;
}

DragEvent makeDragEventEnd(const Widget::GestureState& gs,
                            const MouseEvent& e, UIContext& ctx) {
    DragEvent de;
    de.startLocal   = gs.pressLocal;
    de.startScreen  = gs.pressScreen;
    de.currentLocal = {e.lx, e.ly};
    de.currentScreen = {e.x, e.y};
    de.dx = 0;
    de.dy = 0;
    de.cumDx = (e.x - gs.pressScreen.x) / ctx.dpiScale();
    de.cumDy = (e.y - gs.pressScreen.y) / ctx.dpiScale();
    de.button = gs.pressButton;
    de.modifiers = gs.pressMods;
    de.timestampMs = e.timestampMs;
    return de;
}
} // anon

bool Widget::dispatchMouseDown(MouseEvent& e) {
    if (!m_visible || !m_enabled) return false;

    // Raw callback first — gives subclasses a chance to handle the
    // low-level event before the gesture state machine kicks in.
    if (onMouseDown(e) || e.consumed) return true;

    // Start gesture state machine.
    m_gesture.pressed    = true;
    m_gesture.dragging   = false;
    m_gesture.pressLocal = {e.lx, e.ly};
    m_gesture.pressScreen = {e.x, e.y};
    m_gesture.lastMoveScreen = m_gesture.pressScreen;
    m_gesture.pressTimeMs = e.timestampMs;
    m_gesture.pressButton = e.button;
    m_gesture.pressMods   = e.modifiers;

    // Capture so we receive follow-up moves + release regardless of
    // pointer position.
    captureMouse();
    return true;
}

bool Widget::dispatchMouseUp(MouseEvent& e) {
    if (!m_visible) return false;

    // If there's no pending press, just forward the raw event.
    if (!m_gesture.pressed) return onMouseUp(e) || e.consumed;

    // End-of-interaction: determine click vs drag-end.
    if (m_gesture.dragging) {
        DragEvent de = makeDragEventEnd(m_gesture, e, UIContext::global());
        onDragEnd(de);
    } else {
        if (m_gesture.pressButton == MouseButton::Right) {
            onRightClick(makeClickEvent(e, 1));
        } else {
            const bool isDouble = shouldFireDoubleClick(m_gesture, e);
            onClick(makeClickEvent(e, 1));
            if (isDouble) {
                onDoubleClick(makeClickEvent(e, 2));
                m_gesture.lastClickEndMs = 0;   // prevent triple
            } else {
                m_gesture.lastClickEndMs = e.timestampMs;
                m_gesture.lastClickScreen = {e.x, e.y};
            }
        }
    }
    m_gesture.pressed  = false;
    m_gesture.dragging = false;
    if (hasMouseCapture()) releaseMouse();

    // Let subclasses observe the post-gesture raw event too.
    onMouseUp(e);
    return true;
}

bool Widget::dispatchMouseMove(MouseMoveEvent& e) {
    if (!m_visible) return false;

    // Raw callback first.
    if (onMouseMove(e) || e.consumed) return true;

    // Gesture: if pressed, check for drag threshold crossing.
    if (m_gesture.pressed) {
        UIContext& ctx = UIContext::global();
        if (!m_gesture.dragging) {
            float dx = (e.x - m_gesture.pressScreen.x) / ctx.dpiScale();
            float dy = (e.y - m_gesture.pressScreen.y) / ctx.dpiScale();
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= gesture::kClickDragThresholdPx) {
                m_gesture.dragging = true;
                DragEvent de = makeDragEventStart(m_gesture, e, ctx);
                onDragStart(de);
            }
        }
        if (m_gesture.dragging) {
            DragEvent de = makeDragEventContinue(m_gesture, e, ctx);
            onDrag(de);
            m_gesture.lastMoveScreen = {e.x, e.y};
        }
        return true;
    }
    return false;
}

bool Widget::dispatchScroll(ScrollEvent& e) {
    if (!m_visible || !m_enabled) return false;
    return onScroll(e);
}

bool Widget::dispatchKeyDown(KeyEvent& e) {
    if (!m_visible || !m_enabled) return false;
    return onKeyDown(e);
}

bool Widget::dispatchKeyUp(KeyEvent& e) {
    if (!m_visible || !m_enabled) return false;
    return onKeyUp(e);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
