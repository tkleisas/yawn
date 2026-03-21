#pragma once
// Viewport — Scrollable container that clips its content.
//
// Contains a single content widget that can be larger than the viewport.
// Handles scroll events to translate the visible region.
// Clipping is applied during render via UIContext.

#include "Widget.h"

namespace yawn {
namespace ui {
namespace fw {

class Viewport : public Widget {
public:
    Viewport() = default;

    // ─── Content ────────────────────────────────────────────────────────

    void setContent(Widget* content) {
        removeAllChildren();
        if (content) addChild(content);
    }

    Widget* content() const { return m_children.empty() ? nullptr : m_children[0]; }

    // ─── Scroll ─────────────────────────────────────────────────────────

    float scrollX() const { return m_scrollX; }
    float scrollY() const { return m_scrollY; }

    void setScrollX(float x) { m_scrollX = x; clampScroll(); }
    void setScrollY(float y) { m_scrollY = y; clampScroll(); }
    void setScroll(float x, float y) { m_scrollX = x; m_scrollY = y; clampScroll(); }

    void scrollBy(float dx, float dy) {
        m_scrollX += dx;
        m_scrollY += dy;
        clampScroll();
    }

    // Enable/disable scroll axes
    bool scrollHorizontal() const { return m_scrollH; }
    bool scrollVertical()   const { return m_scrollV; }
    void setScrollHorizontal(bool h) { m_scrollH = h; }
    void setScrollVertical(bool v)   { m_scrollV = v; }

    // Scroll speed multiplier
    float scrollSpeed() const { return m_scrollSpeed; }
    void setScrollSpeed(float s) { m_scrollSpeed = s; }

    // ─── Content size ───────────────────────────────────────────────────

    Size contentSize() const { return m_contentSize; }

    // Visible region in content coordinates
    Rect visibleRegion() const {
        return {m_scrollX, m_scrollY, m_bounds.w, m_bounds.h};
    }

    // Max scroll values
    float maxScrollX() const { return detail::cmax(0.0f, m_contentSize.w - m_bounds.w); }
    float maxScrollY() const { return detail::cmax(0.0f, m_contentSize.h - m_bounds.h); }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        // Viewport takes whatever size is offered (or its preferred size)
        // Content is measured unconstrained to determine its natural size
        if (auto* c = content()) {
            m_contentSize = c->measure(Constraints::unbounded(), ctx);
        }
        return constraints.constrain(m_contentSize);
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;

        if (auto* c = content()) {
            // Content is laid out at its natural size, starting at (0,0) in local coords
            // The scroll offset is applied during rendering/event dispatch
            float cw = detail::cmax(bounds.w, m_contentSize.w);
            float ch = detail::cmax(bounds.h, m_contentSize.h);
            c->layout(Rect{0, 0, cw, ch}, ctx);
        }

        clampScroll();
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void render(UIContext& ctx) override {
        if (!m_visible) return;
        paint(ctx);

        // Content renders at offset (-scrollX, -scrollY).
        // The concrete rendering layer (App.cpp) should call pushClip()
        // before rendering the viewport and popClip() after.
        // Here we just propagate to children with the scroll-adjusted layout.
        for (auto* child : m_children) {
            child->render(ctx);
        }
    }

    // Returns the clip rect in global coordinates for the render layer.
    Rect clipRect() const { return globalBounds(); }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onScroll(ScrollEvent& e) override {
        float dx = m_scrollH ? -e.dx * m_scrollSpeed : 0;
        float dy = m_scrollV ? -e.dy * m_scrollSpeed : 0;

        if (dx == 0 && dy == 0) return false;

        float oldX = m_scrollX, oldY = m_scrollY;
        scrollBy(dx, dy);

        // Only consume if we actually scrolled
        if (m_scrollX != oldX || m_scrollY != oldY) {
            e.consume();
            return true;
        }
        return false;
    }

    // Override dispatch to translate mouse coordinates by scroll offset
    Widget* dispatchMouseDown(MouseEvent& e) override {
        if (!m_visible || !m_enabled) return nullptr;
        Point local = toLocal(e.x, e.y);
        if (!hitTest(local.x, local.y)) return nullptr;

        // Translate to content coordinates
        if (auto* c = content()) {
            MouseEvent contentEvent = e;
            contentEvent.x = e.x + m_scrollX;
            contentEvent.y = e.y + m_scrollY;

            Widget* hit = c->dispatchMouseDown(contentEvent);
            if (hit) {
                e.consume();
                return hit;
            }
        }

        // Self handles
        e.lx = local.x;
        e.ly = local.y;
        if (onMouseDown(e)) return this;
        return nullptr;
    }

    Widget* dispatchScroll(ScrollEvent& e) override {
        if (!m_visible || !m_enabled) return nullptr;
        Point local = toLocal(e.x, e.y);
        if (!hitTest(local.x, local.y)) return nullptr;

        e.lx = local.x;
        e.ly = local.y;

        // Let children handle first (content might have inner scrollables)
        if (auto* c = content()) {
            ScrollEvent contentEvent = e;
            contentEvent.x = e.x + m_scrollX;
            contentEvent.y = e.y + m_scrollY;
            Widget* hit = c->dispatchScroll(contentEvent);
            if (hit) {
                e.consume();
                return hit;
            }
        }

        // Self handles scroll
        if (onScroll(e)) return this;
        return nullptr;
    }

private:
    void clampScroll() {
        if (!m_scrollH) m_scrollX = 0;
        if (!m_scrollV) m_scrollY = 0;
        m_scrollX = detail::cclamp(m_scrollX, 0.0f, maxScrollX());
        m_scrollY = detail::cclamp(m_scrollY, 0.0f, maxScrollY());
    }

    float m_scrollX = 0;
    float m_scrollY = 0;
    bool  m_scrollH = true;
    bool  m_scrollV = true;
    float m_scrollSpeed = 30.0f;
    Size  m_contentSize{};
};

} // namespace fw
} // namespace ui
} // namespace yawn
