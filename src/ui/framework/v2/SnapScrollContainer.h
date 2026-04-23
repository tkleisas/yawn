#pragma once
// fw2::SnapScrollContainer — horizontal scroll container with side
// scroll buttons (`<` / `>`) and smooth animation.
//
// Holds fw2 children added via addChild(). The container clips its
// children to the viewport and animates m_scrollX toward m_targetX.
// Only scroll-button clicks are handled here; other clicks fall
// through to the parent so it can route them to the child widgets.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <utility>

namespace yawn {
namespace ui {
namespace fw2 {

class SnapScrollContainer : public Widget {
public:
    SnapScrollContainer() { setName("SnapScrollContainer"); }

    // addChild / removeAllChildren / children() come from fw2::Widget.

    // ─── Snap points ─────────────────────────────────────────────────

    void setSnapPoints(std::vector<float> points) {
        m_snapPoints = std::move(points);
        std::sort(m_snapPoints.begin(), m_snapPoints.end());
        m_currentSnap = 0;
        clampTarget();
        m_scrollX = m_targetX;
    }

    // ─── Navigation ──────────────────────────────────────────────────

    void scrollLeft()  { m_targetX -= kScrollStep; clampTarget(); }
    void scrollRight() { m_targetX += kScrollStep; clampTarget(); }

    void handleScroll(float dx, float /*dy*/) {
        if      (dx > 0) scrollRight();
        else if (dx < 0) scrollLeft();
    }

    // ─── Scroll position ─────────────────────────────────────────────

    float scrollX() const { return m_scrollX; }
    void setScrollX(float x) {
        m_targetX = x;
        m_scrollX = x;
        clampTarget();
        m_scrollX = m_targetX;
    }

    bool canScroll() const { return m_contentWidth > m_bounds.w; }

    void setShowScrollButtons(bool s) { m_showButtons = s; }

    float gap() const     { return m_gap; }
    void setGap(float g)  { m_gap = g; }

    static constexpr float kScrollBtnW  = 28.0f;
    static constexpr float kScrollStep  = 256.0f;
    static constexpr float kScrollSpeed = 0.2f;  // lerp factor per frame

    // ─── fw2 lifecycle ───────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext& ctx) override {
        float totalW = 0;
        float maxH   = 0;
        for (auto* child : children()) {
            auto cs = child->measure(Constraints::unbounded(), ctx);
            totalW += cs.w;
            maxH = std::max(maxH, cs.h);
        }
        if (children().size() > 1)
            totalW += static_cast<float>(children().size() - 1) * m_gap;
        m_contentWidth = totalW;
        return c.constrain({c.maxW, maxH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);

        float vpX = bounds.x;
        float vpW = bounds.w;
        if (canScroll() && m_showButtons) {
            vpX += kScrollBtnW;
            vpW -= 2.0f * kScrollBtnW;
        }

        clampTarget();
        clampScroll();

        float offset = 0;
        for (auto* child : children()) {
            auto cs = child->measure(Constraints::unbounded(), ctx);
            const float childX = vpX - m_scrollX + offset;
            child->layout({childX, bounds.y, cs.w, bounds.h}, ctx);
            offset += cs.w + m_gap;
        }
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        // Animate scroll toward target. Re-lay each child via layout
        // so nested sub-widgets (device header / knob grid / custom
        // body) reposition with the container — setBoundsX alone would
        // only move the device's root bounds and leave its internals
        // stale.
        if (std::fabs(m_scrollX - m_targetX) > 0.5f) {
            m_scrollX += (m_targetX - m_scrollX) * kScrollSpeed;
            float vpX = m_bounds.x;
            if (canScroll() && m_showButtons)
                vpX += kScrollBtnW;
            float offset = 0;
            for (auto* child : children()) {
                const auto& cb = child->bounds();
                const float childX = vpX - m_scrollX + offset;
                child->layout({childX, m_bounds.y, cb.w, m_bounds.h}, ctx);
                offset += cb.w + m_gap;
            }
        } else {
            m_scrollX = m_targetX;
        }

        float vpX = m_bounds.x;
        float vpW = m_bounds.w;
        if (canScroll() && m_showButtons) {
            vpX += kScrollBtnW;
            vpW -= 2.0f * kScrollBtnW;
        }

        r.pushClip(vpX, m_bounds.y, vpW, m_bounds.h);
        for (auto* child : children())
            child->render(ctx);
        r.popClip();

        if (canScroll() && m_showButtons) {
            const bool atLeft  = m_targetX <= 0.5f;
            const bool atRight = m_targetX >= maxScroll() - 0.5f;

            const Color normalBg{50, 50, 60, 255};
            const Color limitBg{35, 35, 40, 255};

            const Color lbg   = atLeft ? limitBg : normalBg;
            r.drawRect(m_bounds.x, m_bounds.y, kScrollBtnW, m_bounds.h, lbg);

            const Color lText = atLeft
                ? ::yawn::ui::Theme::textDim
                : ::yawn::ui::Theme::textPrimary;
            const float fs   = std::min(m_bounds.h * 0.5f, 18.0f)
                                * (48.0f / 26.0f);  // v1→fw2 pixel conversion
            auto* tm = ctx.textMetrics;
            if (tm) {
                const float tw = tm->textWidth("<", fs);
                const float tx = m_bounds.x + (kScrollBtnW - tw) * 0.5f;
                const float ty = m_bounds.y + m_bounds.h * 0.2f;
                tm->drawText(r, "<", tx, ty, fs, lText);
            }

            const float rbX   = m_bounds.x + m_bounds.w - kScrollBtnW;
            const Color rbg   = atRight ? limitBg : normalBg;
            r.drawRect(rbX, m_bounds.y, kScrollBtnW, m_bounds.h, rbg);

            const Color rText = atRight
                ? ::yawn::ui::Theme::textDim
                : ::yawn::ui::Theme::textPrimary;
            if (tm) {
                const float tw = tm->textWidth(">", fs);
                const float tx = rbX + (kScrollBtnW - tw) * 0.5f;
                const float ty = m_bounds.y + m_bounds.h * 0.2f;
                tm->drawText(r, ">", tx, ty, fs, rText);
            }
        }
    }
#endif

    // ─── Events ──────────────────────────────────────────────────────
    // Only handle scroll-button clicks. Override dispatchMouseDown so
    // the base class's gesture-SM auto-capture doesn't fire when the
    // click missed a button — the parent needs non-button clicks to
    // fall through so it can route them to the child widgets.
    bool dispatchMouseDown(MouseEvent& e) override {
        if (!isVisible() || !isEnabled()) return false;
        return onMouseDown(e) || e.consumed;
    }

    bool onMouseDown(MouseEvent& e) override {
        if (!canScroll() || !m_showButtons) return false;

        const float lx = e.x - m_bounds.x;
        const float ly = e.y - m_bounds.y;

        if (lx >= 0 && lx < kScrollBtnW && ly >= 0 && ly < m_bounds.h) {
            scrollLeft();
            e.consumed = true;
            return true;
        }
        if (lx >= m_bounds.w - kScrollBtnW && lx < m_bounds.w
         && ly >= 0 && ly < m_bounds.h) {
            scrollRight();
            e.consumed = true;
            return true;
        }
        return false;
    }

private:
    std::vector<float> m_snapPoints;
    float m_scrollX       = 0;
    float m_targetX       = 0;
    float m_contentWidth  = 0;
    bool  m_showButtons   = true;
    int   m_currentSnap   = 0;
    float m_gap           = 0;

    void clampScroll() { m_scrollX = std::clamp(m_scrollX, 0.0f, maxScroll()); }
    void clampTarget() { m_targetX = std::clamp(m_targetX, 0.0f, maxScroll()); }

    float maxScroll() const {
        return std::max(0.0f, m_contentWidth - viewportWidth());
    }
    float viewportWidth() const {
        if (canScroll() && m_showButtons)
            return m_bounds.w - 2.0f * kScrollBtnW;
        return m_bounds.w;
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
