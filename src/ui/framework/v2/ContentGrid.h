#pragma once
// fw2::ContentGrid — 2×2 panel layout with linked draggable dividers.
//
// Mirrors the v1 ContentGrid as a proper fw2::Widget:
//   • Children are fw2::Widget* — panels slot in directly, no v1 wrappers.
//   • Horizontal divider spans the full width (shared hRatio for both columns).
//   • Vertical divider spans the full height  (shared vRatio for both rows).
//   • Cursor hints: wantsHorizontalResize() / wantsVerticalResize().
//   • setTopLeft() lets App swap session ↔ arrangement view at runtime.
//
// Layout (with divider names matching the v1 original):
//   ┌──────────────┬──────────┐
//   │  topLeft     │ topRight │  topH  = hRatio × totalH
//   ├──────────────┼──────────┤
//   │  bottomLeft  │ botRight │  botH  = (1-hRatio) × totalH - kDiv
//   └──────────────┴──────────┘
//      leftW = vRatio × totalW     rightW = rest

#include "Widget.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yawn {
namespace ui {
namespace fw2 {

class ContentGrid : public Widget {
public:
    ContentGrid() = default;

    void setChildren(Widget* topLeft, Widget* topRight,
                     Widget* bottomLeft, Widget* bottomRight) {
        m_tl = topLeft;
        m_tr = topRight;
        m_bl = bottomLeft;
        m_br = bottomRight;
    }

    // Swap just the top-left child (session ↔ arrangement view switch).
    // Calls invalidate() so the next layout pass repositions the new child.
    void setTopLeft(Widget* w) { m_tl = w; invalidate(); }

    void setDividerRatios(float hRatio, float vRatio) {
        m_hRatio = std::clamp(hRatio, 0.1f, 0.9f);
        m_vRatio = std::clamp(vRatio, 0.1f, 0.9f);
    }
    float hDividerRatio() const { return m_hRatio; }
    float vDividerRatio() const { return m_vRatio; }

    // ─── Cursor hints (queried by App for SDL cursor shape) ─────────────
    bool wantsHorizontalResize() const { return m_hoverV || m_dragV; }
    bool wantsVerticalResize()   const { return m_hoverH || m_dragH; }
    bool isDraggingDivider()     const { return m_dragH || m_dragV; }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        m_bounds = bounds;
        recomputeDividers();

        float x = bounds.x, y = bounds.y;

        float leftW  = m_vDivX - x;
        float rightW = (x + bounds.w) - (m_vDivX + kDividerSize);
        float topH   = m_hDivY - y;
        float botH   = (y + bounds.h) - (m_hDivY + kDividerSize);
        float rx     = m_vDivX + kDividerSize;
        float by     = m_hDivY + kDividerSize;

        auto layout1 = [&](Widget* w, float wx, float wy, float ww, float wh) {
            if (!w) return;
            w->measure(Constraints::tight(ww, wh), ctx);
            w->layout(Rect{wx, wy, ww, wh}, ctx);
        };

        layout1(m_tl, x,  y,  leftW,  topH);
        layout1(m_tr, rx, y,  rightW, topH);
        layout1(m_bl, x,  by, leftW,  botH);
        layout1(m_br, rx, by, rightW, botH);
    }

    // ─── Render ─────────────────────────────────────────────────────────

    void render(UIContext& ctx) override {
        // Render children first, then overlay the divider bars.
        if (m_tl && m_tl->isVisible()) m_tl->render(ctx);
        if (m_tr && m_tr->isVisible()) m_tr->render(ctx);
        if (m_bl && m_bl->isVisible()) m_bl->render(ctx);
        if (m_br && m_br->isVisible()) m_br->render(ctx);

        // Let the registered painter draw the dividers.
        Widget::render(ctx);
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float mx = e.x, my = e.y;

        bool onH = std::fabs(my - m_hDivY) < kHitZone
                && mx >= m_bounds.x && mx < m_bounds.x + m_bounds.w;
        bool onV = std::fabs(mx - m_vDivX) < kHitZone
                && my >= m_bounds.y && my < m_bounds.y + m_bounds.h;

        if (onH || onV) {
            m_dragH = onH;
            m_dragV = onV;
            m_dragStartMX    = mx;
            m_dragStartMY    = my;
            m_dragStartHRatio = m_hRatio;
            m_dragStartVRatio = m_vRatio;
            return true;  // gesture SM takes capture
        }

        // Dispatch to the child that contains the click.
        for (Widget* w : {m_tl, m_tr, m_bl, m_br}) {
            if (w && w->isVisible() && w->bounds().contains(mx, my))
                return w->dispatchMouseDown(e);
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        float mx = e.x, my = e.y;

        if (m_dragH) {
            float delta = my - m_dragStartMY;
            float newRatio = m_dragStartHRatio + delta / m_bounds.h;
            m_hRatio = std::clamp(newRatio,
                                  kMinTop    / m_bounds.h,
                                  1.0f - (kMinBottom + kDividerSize) / m_bounds.h);
            reflowChildren();
            return true;
        }
        if (m_dragV) {
            float delta = mx - m_dragStartMX;
            float newRatio = m_dragStartVRatio + delta / m_bounds.w;
            m_vRatio = std::clamp(newRatio,
                                  kMinLeft  / m_bounds.w,
                                  1.0f - (kMinRight + kDividerSize) / m_bounds.w);
            reflowChildren();
            return true;
        }

        // Something captured fw2 mouse (panel scrollbar drag, fader drag,
        // knob drag, envelope point drag, …). Forward the move so the
        // captured widget sees its drag continue. ContentGrid isn't in
        // a fw2 parent tree so no one else routes this for us. The
        // captured widget can be a deep descendant of any of the four
        // quadrants — check by visual containment rather than by
        // direct-child identity, otherwise controls inside mixer /
        // browser / return panels (faders, knobs, dropdowns) never
        // receive drag updates.
        if (Widget* cap = capturedWidget()) {
            const Rect& cb = cap->bounds();
            bool inside = cb.x >= m_bounds.x
                       && cb.x + cb.w <= m_bounds.x + m_bounds.w
                       && cb.y >= m_bounds.y
                       && cb.y + cb.h <= m_bounds.y + m_bounds.h;
            if (inside) {
                MouseMoveEvent ce = e;
                ce.lx = e.x - cb.x;
                ce.ly = e.y - cb.y;
                cap->dispatchMouseMove(ce);
                return true;
            }
        }

        m_hoverH = std::fabs(my - m_hDivY) < kHitZone
                && mx >= m_bounds.x && mx < m_bounds.x + m_bounds.w;
        m_hoverV = std::fabs(mx - m_vDivX) < kHitZone
                && my >= m_bounds.y && my < m_bounds.y + m_bounds.h;
        return m_hoverH || m_hoverV;
    }

    bool onMouseUp(MouseEvent& e) override {
        bool wasDragging = m_dragH || m_dragV;
        m_dragH = m_dragV = false;
        // Forward mouseUp to the captured widget (descendant at any
        // depth) so its gesture SM can end its drag + release capture.
        // Same bounds-check rationale as onMouseMove above.
        if (Widget* cap = capturedWidget()) {
            const Rect& cb = cap->bounds();
            bool inside = cb.x >= m_bounds.x
                       && cb.x + cb.w <= m_bounds.x + m_bounds.w
                       && cb.y >= m_bounds.y
                       && cb.y + cb.h <= m_bounds.y + m_bounds.h;
            if (inside) {
                MouseEvent ce = e;
                ce.lx = e.x - cb.x;
                ce.ly = e.y - cb.y;
                cap->dispatchMouseUp(ce);
                return true;
            }
        }
        return wasDragging;
    }

    bool onScroll(ScrollEvent& e) override {
        float mx = e.x, my = e.y;
        for (Widget* w : {m_tl, m_tr, m_bl, m_br}) {
            if (w && w->isVisible() && w->bounds().contains(mx, my))
                return w->dispatchScroll(e);
        }
        return false;
    }

    // ─── Painter data (read by Fw2Painters.cpp) ─────────────────────────

    struct PaintData {
        float vDivX   = 0, hDivY   = 0;
        float bx      = 0, by      = 0, bw = 0, bh = 0;
        bool  hoverH  = false, hoverV  = false;
        bool  dragH   = false, dragV   = false;
        float divSize = 4.f;
    };

    PaintData paintData() const {
        return {m_vDivX, m_hDivY,
                m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                m_hoverH, m_hoverV, m_dragH, m_dragV,
                kDividerSize};
    }

private:
    Widget* m_tl = nullptr;
    Widget* m_tr = nullptr;
    Widget* m_bl = nullptr;
    Widget* m_br = nullptr;

    float m_hRatio = 0.6f;
    float m_vRatio = 0.70f;

    float m_vDivX = 0;
    float m_hDivY = 0;

    bool  m_dragH = false, m_dragV = false;
    float m_dragStartMX = 0, m_dragStartMY = 0;
    float m_dragStartHRatio = 0, m_dragStartVRatio = 0;

    bool m_hoverH = false, m_hoverV = false;

    static constexpr float kDividerSize = 4.0f;
    static constexpr float kHitZone    = 6.0f;
    static constexpr float kMinLeft    = 200.0f;
    static constexpr float kMinRight   = 150.0f;
    static constexpr float kMinTop     = 100.0f;
    static constexpr float kMinBottom  = 80.0f;

    void recomputeDividers() {
        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        float leftW = std::max(kMinLeft, m_vRatio * w);
        float rightW = w - leftW - kDividerSize;
        if (rightW < kMinRight) {
            rightW = kMinRight;
            leftW  = w - rightW - kDividerSize;
        }

        float topH   = std::max(kMinTop, m_hRatio * h);
        float botH   = h - topH - kDividerSize;
        if (botH < kMinBottom) {
            botH = kMinBottom;
            topH = h - botH - kDividerSize;
        }

        m_vDivX = x + leftW;
        m_hDivY = y + topH;
    }

    // Re-layout children mid-drag when the divider ratio changes.
    // The fw2 layout cache on a child keys on (bounds, measure-version,
    // epoch): if we only invalidate *ourselves* the child still thinks
    // its previous bounds are valid until something bumps its own
    // measure version. invalidate() on each child sidesteps the cache
    // and forces its layout to recompute on the next pass driven by
    // this reflow call.
    void reflowChildren() {
        recomputeDividers();

        float x = m_bounds.x, y = m_bounds.y;
        float leftW  = m_vDivX - x;
        float rightW = (x + m_bounds.w) - (m_vDivX + kDividerSize);
        float topH   = m_hDivY - y;
        float botH   = (y + m_bounds.h) - (m_hDivY + kDividerSize);
        float rx     = m_vDivX + kDividerSize;
        float by     = m_hDivY + kDividerSize;

        UIContext& ctx = UIContext::global();
        auto relayout = [&](Widget* w, float wx, float wy, float ww, float wh) {
            if (!w) return;
            w->invalidate();                                   // force child layout miss
            w->measure(Constraints::tight(ww, wh), ctx);
            w->layout(Rect{wx, wy, ww, wh}, ctx);
        };
        relayout(m_tl, x,  y,  leftW,  topH);
        relayout(m_tr, rx, y,  rightW, topH);
        relayout(m_bl, x,  by, leftW,  botH);
        relayout(m_br, rx, by, rightW, botH);
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
