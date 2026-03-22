#pragma once
// ContentGrid — 4-quadrant layout widget with draggable dividers.
//
// Manages four child panels in a 2×2 grid separated by a horizontal and
// a vertical divider.  Both dividers are draggable and their positions
// are stored as ratios (0–1) of the available space.

#include "ui/framework/Widget.h"
#include "ui/Renderer.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw {

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

    void setDividerRatios(float hRatio, float vRatio) {
        m_hRatio = std::clamp(hRatio, 0.1f, 0.9f);
        m_vRatio = std::clamp(vRatio, 0.2f, 0.95f);
    }

    float hDividerRatio() const { return m_hRatio; }
    float vDividerRatio() const { return m_vRatio; }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext& ctx) override {
        (void)ctx;
        return c.constrain({c.maxW, c.maxH});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;

        float x = bounds.x, y = bounds.y;
        float w = bounds.w, h = bounds.h;

        // Compute divider positions with min-size enforcement
        float leftW  = std::max(kMinLeft, m_vRatio * w);
        float rightW = w - leftW - kDividerSize;
        if (rightW < kMinRight) {
            rightW = kMinRight;
            leftW  = w - rightW - kDividerSize;
        }

        float topH    = std::max(kMinTop, m_hRatio * h);
        float bottomH = h - topH - kDividerSize;
        if (bottomH < kMinBottom) {
            bottomH = kMinBottom;
            topH    = h - bottomH - kDividerSize;
        }

        m_vDivX = x + leftW;
        m_hDivY = y + topH;

        // Layout children
        if (m_tl) {
            m_tl->measure(Constraints::tight(leftW, topH), ctx);
            m_tl->layout(Rect{x, y, leftW, topH}, ctx);
        }
        if (m_tr) {
            float rx = m_vDivX + kDividerSize;
            m_tr->measure(Constraints::tight(rightW, topH), ctx);
            m_tr->layout(Rect{rx, y, rightW, topH}, ctx);
        }
        if (m_bl) {
            float by = m_hDivY + kDividerSize;
            m_bl->measure(Constraints::tight(leftW, bottomH), ctx);
            m_bl->layout(Rect{x, by, leftW, bottomH}, ctx);
        }
        if (m_br) {
            float rx = m_vDivX + kDividerSize;
            float by = m_hDivY + kDividerSize;
            m_br->measure(Constraints::tight(rightW, bottomH), ctx);
            m_br->layout(Rect{rx, by, rightW, bottomH}, ctx);
        }
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
        auto& r = *ctx.renderer;
        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        // Paint children
        if (m_tl) m_tl->render(ctx);
        if (m_tr) m_tr->render(ctx);
        if (m_bl) m_bl->render(ctx);
        if (m_br) m_br->render(ctx);

        // Horizontal divider
        Color hCol = m_dragH ? kDividerActive : (m_hoverH ? kDividerHover : kDividerColor);
        r.drawRect(x, m_hDivY, w, kDividerSize, hCol);

        // Vertical divider
        Color vCol = m_dragV ? kDividerActive : (m_hoverV ? kDividerHover : kDividerColor);
        r.drawRect(m_vDivX, y, kDividerSize, h, vCol);

        // Intersection highlight
        if (m_dragH || m_dragV || m_hoverH || m_hoverV)
            r.drawRect(m_vDivX, m_hDivY, kDividerSize, kDividerSize, kDividerActive);
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float mx = e.x, my = e.y;

        bool onH = std::abs(my - m_hDivY) < kDividerHitZone && mx >= m_bounds.x && mx < m_bounds.x + m_bounds.w;
        bool onV = std::abs(mx - m_vDivX) < kDividerHitZone && my >= m_bounds.y && my < m_bounds.y + m_bounds.h;

        if (onH || onV) {
            m_dragH = onH;
            m_dragV = onV;
            m_dragStartMX = mx;
            m_dragStartMY = my;
            m_dragStartHRatio = m_hRatio;
            m_dragStartVRatio = m_vRatio;
            captureMouse();
            return true;
        }

        // Dispatch to children
        MouseEvent ce = e;
        if (m_tl && m_tl->bounds().contains(mx, my)) return m_tl->dispatchMouseDown(ce) != nullptr;
        if (m_tr && m_tr->bounds().contains(mx, my)) return m_tr->dispatchMouseDown(ce) != nullptr;
        if (m_bl && m_bl->bounds().contains(mx, my)) return m_bl->dispatchMouseDown(ce) != nullptr;
        if (m_br && m_br->bounds().contains(mx, my)) return m_br->dispatchMouseDown(ce) != nullptr;
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        float mx = e.x, my = e.y;

        if (m_dragH || m_dragV) {
            if (m_dragH) {
                float deltaY = my - m_dragStartMY;
                float newRatio = m_dragStartHRatio + deltaY / m_bounds.h;
                m_hRatio = std::clamp(newRatio, kMinTop / m_bounds.h,
                                      1.0f - (kMinBottom + kDividerSize) / m_bounds.h);
            }
            if (m_dragV) {
                float deltaX = mx - m_dragStartMX;
                float newRatio = m_dragStartVRatio + deltaX / m_bounds.w;
                m_vRatio = std::clamp(newRatio, kMinLeft / m_bounds.w,
                                      1.0f - (kMinRight + kDividerSize) / m_bounds.w);
            }
            return true;
        }

        // Hover detection
        m_hoverH = std::abs(my - m_hDivY) < kDividerHitZone && mx >= m_bounds.x && mx < m_bounds.x + m_bounds.w;
        m_hoverV = std::abs(mx - m_vDivX) < kDividerHitZone && my >= m_bounds.y && my < m_bounds.y + m_bounds.h;

        if (m_hoverH || m_hoverV) return true;

        // Forward to children
        if (Widget::capturedWidget()) return false; // let captured widget handle it
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_dragH || m_dragV) {
            m_dragH = false;
            m_dragV = false;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        float mx = e.x, my = e.y;
        ScrollEvent ce = e;
        if (m_tl && m_tl->bounds().contains(mx, my)) return m_tl->dispatchScroll(ce) != nullptr;
        if (m_tr && m_tr->bounds().contains(mx, my)) return m_tr->dispatchScroll(ce) != nullptr;
        if (m_bl && m_bl->bounds().contains(mx, my)) return m_bl->dispatchScroll(ce) != nullptr;
        if (m_br && m_br->bounds().contains(mx, my)) return m_br->dispatchScroll(ce) != nullptr;
        return false;
    }

    // Cursor state — the host (App) queries this to set SDL cursor shape
    bool wantsHorizontalResize() const { return m_hoverV || m_dragV; }
    bool wantsVerticalResize()   const { return m_hoverH || m_dragH; }
    bool isDraggingDivider()     const { return m_dragH || m_dragV; }

private:
    Widget* m_tl = nullptr;
    Widget* m_tr = nullptr;
    Widget* m_bl = nullptr;
    Widget* m_br = nullptr;

    float m_hRatio = 0.6f;   // horizontal divider position (top height fraction)
    float m_vRatio = 0.70f;  // vertical divider position (left width fraction)

    float m_vDivX = 0;       // computed vertical divider X
    float m_hDivY = 0;       // computed horizontal divider Y

    // Drag state
    bool  m_dragH = false, m_dragV = false;
    float m_dragStartMX = 0, m_dragStartMY = 0;
    float m_dragStartHRatio = 0, m_dragStartVRatio = 0;

    // Hover state
    bool m_hoverH = false, m_hoverV = false;

    // Constants
    static constexpr float kDividerSize    = 4.0f;
    static constexpr float kDividerHitZone = 6.0f;
    static constexpr float kMinLeft   = 200.0f;
    static constexpr float kMinRight  = 150.0f;
    static constexpr float kMinTop    = 100.0f;
    static constexpr float kMinBottom = 80.0f;

    static constexpr Color kDividerColor  {55, 55, 60};
    static constexpr Color kDividerHover  {80, 80, 90};
    static constexpr Color kDividerActive {100, 120, 160};
};

} // namespace fw
} // namespace ui
} // namespace yawn
