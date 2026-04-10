#pragma once
// Widget — Base class for all UI elements in the YAWN framework.
//
// Implements a two-pass layout system:
//   1. measure(constraints) → returns desired Size
//   2. layout(bounds)       → assigns position, recurses to children
//
// Events propagate through the widget tree:
//   capture (root→target) → target → bubble (target→root)
//
// Widgets form a tree via addChild/removeChild. The tree is owned externally
// (e.g., panels own their child widgets). Widget does NOT own children.

#include <algorithm>
#include <string>
#include <vector>

#include "Types.h"
#include "EventSystem.h"
#include "UIContext.h"

namespace yawn {
namespace ui {
namespace fw {

class Widget {
public:
    virtual ~Widget() = default;

    // ─── Tree management ────────────────────────────────────────────────

    void addChild(Widget* child) {
        if (!child || child == this) return;
        if (child->m_parent) child->m_parent->removeChild(child);
        m_children.push_back(child);
        child->m_parent = this;
    }

    void removeChild(Widget* child) {
        if (!child) return;
        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it != m_children.end()) {
            (*it)->m_parent = nullptr;
            m_children.erase(it);
        }
    }

    void removeAllChildren() {
        for (auto* c : m_children) c->m_parent = nullptr;
        m_children.clear();
    }

    Widget*                     parent()   const { return m_parent; }
    const std::vector<Widget*>& children() const { return m_children; }
    int                         childCount() const { return static_cast<int>(m_children.size()); }

    Widget* childAt(int index) const {
        if (index < 0 || index >= childCount()) return nullptr;
        return m_children[index];
    }

    // ─── Layout: two-pass system ────────────────────────────────────────

    // Pass 1: Measure — return desired size given constraints.
    // Override in subclasses. Default returns (0, 0).
    virtual Size measure(const Constraints& constraints, const UIContext& ctx) {
        (void)ctx;
        return constraints.constrain(Size::zero());
    }

    // Pass 2: Layout — assign bounds and recursively lay out children.
    // Override in containers (FlexBox, etc.) to position children.
    virtual void layout(const Rect& bounds, const UIContext& ctx) {
        (void)ctx;
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    // Render this widget and its children. Children are rendered after the
    // parent (painter's algorithm: parent is background, children on top).
    virtual void render(UIContext& ctx) {
        if (!m_visible) return;
        paint(ctx);
        for (auto* child : m_children) {
            child->render(ctx);
        }
    }

    // Override to draw this widget's own visuals (before children).
    virtual void paint(UIContext& ctx) { (void)ctx; }

    // Override to draw overlays on top of everything (e.g. dropdown popup lists).
    // Called by the parent panel after all regular paint calls and after popClip.
    virtual void paintOverlay(UIContext& ctx) { (void)ctx; }

    // ─── Events ─────────────────────────────────────────────────────────
    // Return true to consume the event (stops propagation).
    // Default implementations do nothing and return false.

    virtual bool onMouseDown(MouseEvent& e)     { (void)e; return false; }
    virtual bool onMouseUp(MouseEvent& e)       { (void)e; return false; }
    virtual bool onMouseMove(MouseMoveEvent& e)  { (void)e; return false; }
    virtual bool onScroll(ScrollEvent& e)        { (void)e; return false; }
    virtual bool onKeyDown(KeyEvent& e)          { (void)e; return false; }
    virtual bool onKeyUp(KeyEvent& e)            { (void)e; return false; }
    virtual bool onTextInput(TextInputEvent& e)  { (void)e; return false; }
    virtual void onFocusChanged(FocusEvent& e)   { (void)e; }
    virtual void onMouseEnter()                  {}
    virtual void onMouseLeave()                  {}

    // ─── Geometry ───────────────────────────────────────────────────────

    const Rect& bounds() const { return m_bounds; }
    void setBoundsX(float x) { m_bounds.x = x; }

    // Bounds in screen (global) coordinates.
    Rect globalBounds() const {
        Rect gb = m_bounds;
        for (Widget* p = m_parent; p; p = p->m_parent) {
            gb.x += p->m_bounds.x;
            gb.y += p->m_bounds.y;
        }
        return gb;
    }

    // Hit test in local coordinates (relative to this widget's bounds).
    bool hitTest(float localX, float localY) const {
        return localX >= 0 && localX < m_bounds.w &&
               localY >= 0 && localY < m_bounds.h;
    }

    // Hit test in global (screen) coordinates.
    bool hitTestGlobal(float screenX, float screenY) const {
        Rect gb = globalBounds();
        return gb.contains(screenX, screenY);
    }

    // Convert screen coordinates to local coordinates.
    Point toLocal(float screenX, float screenY) const {
        Rect gb = globalBounds();
        return {screenX - gb.x, screenY - gb.y};
    }

    // Convert local coordinates to screen coordinates.
    Point toGlobal(float localX, float localY) const {
        Rect gb = globalBounds();
        return {gb.x + localX, gb.y + localY};
    }

    // ─── State ──────────────────────────────────────────────────────────

    bool visible() const { return m_visible; }
    bool isVisible() const { return m_visible; }
    bool isEnabled() const { return m_enabled; }
    bool isFocused() const { return m_focused; }
    bool isHovered() const { return m_hovered; }

    void setVisible(bool v) { m_visible = v; }
    void setEnabled(bool e) { m_enabled = e; }
    void setFocused(bool f) { m_focused = f; }
    void setHovered(bool h) { m_hovered = h; }

    // ─── Mouse capture ──────────────────────────────────────────────────
    // When a widget captures the mouse, it receives all mouse move and
    // mouse up events regardless of position (e.g. during fader drag).

    static Widget*& capturedWidget() {
        static Widget* s_captured = nullptr;
        return s_captured;
    }

    void captureMouse() { capturedWidget() = this; }
    void releaseMouse() { if (capturedWidget() == this) capturedWidget() = nullptr; }
    bool hasMouseCapture() const { return capturedWidget() == this; }

    // Size policy for flex layout — how this widget participates when it's
    // a child of a FlexBox.
    SizePolicy sizePolicy() const { return m_sizePolicy; }
    void setSizePolicy(SizePolicy sp) { m_sizePolicy = sp; }

    // Optional padding inside this widget's bounds.
    Insets padding() const { return m_padding; }
    void setPadding(Insets p) { m_padding = p; }

    // Debug name.
    const std::string& name() const { return m_name; }
    void setName(const std::string& n) { m_name = n; }

    // ─── Event dispatch (called by framework, not user code) ────────────

    // Dispatch a mouse event through the tree. Finds the target via hit-test,
    // then runs capture → target → bubble phases.
    // Returns the widget that consumed the event (or nullptr).
    virtual Widget* dispatchMouseDown(MouseEvent& e) {
        if (!m_visible || !m_enabled) return nullptr;

        // Convert to local coords for hit test
        Point local = toLocal(e.x, e.y);
        if (!hitTest(local.x, local.y)) return nullptr;

        // Set local coordinates on the event
        e.lx = local.x;
        e.ly = local.y;

        // Capture phase: give this widget first shot
        e.phase = EventPhase::Capture;
        if (onMouseDown(e) || e.consumed) return this;

        // Dispatch to children (back-to-front = topmost first)
        for (int i = childCount() - 1; i >= 0; --i) {
            Widget* hit = m_children[i]->dispatchMouseDown(e);
            if (hit) return hit;
        }

        // Target/bubble: this widget is the deepest hit
        e.phase = EventPhase::Target;
        e.lx = local.x;
        e.ly = local.y;
        if (onMouseDown(e) || e.consumed) return this;

        return nullptr;
    }

    virtual Widget* dispatchMouseUp(MouseEvent& e) {
        if (!m_visible || !m_enabled) return nullptr;

        // Root-level capture: forward directly to captured widget
        if (!m_parent && capturedWidget()) {
            Widget* cap = capturedWidget();
            Point local = cap->toLocal(e.x, e.y);
            e.lx = local.x;
            e.ly = local.y;
            e.phase = EventPhase::Target;
            bool handled = cap->onMouseUp(e);
            cap->releaseMouse();
            return handled ? cap : nullptr;
        }

        Point local = toLocal(e.x, e.y);
        e.lx = local.x;
        e.ly = local.y;
        e.phase = EventPhase::Target;
        if (onMouseUp(e) || e.consumed) return this;
        return nullptr;
    }

    virtual Widget* dispatchMouseMove(MouseMoveEvent& e) {
        if (!m_visible || !m_enabled) return nullptr;

        // Root-level capture: forward directly to captured widget
        if (!m_parent && capturedWidget()) {
            Widget* cap = capturedWidget();
            Point local = cap->toLocal(e.x, e.y);
            e.lx = local.x;
            e.ly = local.y;
            return cap->onMouseMove(e) ? cap : nullptr;
        }

        Point local = toLocal(e.x, e.y);
        if (!hitTest(local.x, local.y)) return nullptr;

        e.lx = local.x;
        e.ly = local.y;

        // Dispatch to children first (topmost)
        for (int i = childCount() - 1; i >= 0; --i) {
            Widget* hit = m_children[i]->dispatchMouseMove(e);
            if (hit) return hit;
        }

        e.lx = local.x;
        e.ly = local.y;
        if (onMouseMove(e) || e.consumed) return this;
        return nullptr;
    }

    virtual Widget* dispatchScroll(ScrollEvent& e) {
        if (!m_visible || !m_enabled) return nullptr;
        Point local = toLocal(e.x, e.y);
        if (!hitTest(local.x, local.y)) return nullptr;

        e.lx = local.x;
        e.ly = local.y;

        // Children first
        for (int i = childCount() - 1; i >= 0; --i) {
            Widget* hit = m_children[i]->dispatchScroll(e);
            if (hit) return hit;
        }

        e.lx = local.x;
        e.ly = local.y;
        if (onScroll(e) || e.consumed) return this;
        return nullptr;
    }

    virtual bool dispatchKeyDown(KeyEvent& e) {
        if (!m_visible || !m_enabled) return false;
        return onKeyDown(e);
    }

    virtual bool dispatchKeyUp(KeyEvent& e) {
        if (!m_visible || !m_enabled) return false;
        return onKeyUp(e);
    }

    virtual bool dispatchTextInput(TextInputEvent& e) {
        if (!m_visible || !m_enabled) return false;
        return onTextInput(e);
    }

    // ─── Find child at point ────────────────────────────────────────────

    Widget* findWidgetAt(float screenX, float screenY) {
        if (!m_visible || !m_enabled) return nullptr;
        Point local = toLocal(screenX, screenY);
        if (!hitTest(local.x, local.y)) return nullptr;

        // Children back-to-front
        for (int i = childCount() - 1; i >= 0; --i) {
            Widget* hit = m_children[i]->findWidgetAt(screenX, screenY);
            if (hit) return hit;
        }
        return this;
    }

protected:
    Rect        m_bounds{};
    Widget*     m_parent = nullptr;
    std::vector<Widget*> m_children;

    bool m_visible = true;
    bool m_enabled = true;
    bool m_focused = false;
    bool m_hovered = false;

    SizePolicy  m_sizePolicy = SizePolicy::fixed();
    Insets      m_padding{};
    std::string m_name;
};

} // namespace fw
} // namespace ui
} // namespace yawn
