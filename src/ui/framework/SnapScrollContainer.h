#pragma once
// SnapScrollContainer — Horizontal scroll container with smooth scrolling.
//
// Lays children out side-by-side. When their total width exceeds the
// container, scroll buttons (< >) appear on left and right edges.
// Scrolling is animated with exponential ease-out.

#include "Widget.h"
#include "../Theme.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

namespace yawn {
namespace ui {
namespace fw {

class SnapScrollContainer : public Widget {
public:
    SnapScrollContainer() = default;

    // ─── Snap points ────────────────────────────────────────────────────

    // Set snap points (device boundary x-positions in content space).
    // Call after children change.
    void setSnapPoints(std::vector<float> points) {
        m_snapPoints = std::move(points);
        std::sort(m_snapPoints.begin(), m_snapPoints.end());
        m_currentSnap = 0;
        clampTarget();
        m_scrollX = m_targetX;  // no animation on rebuild
    }

    // ─── Navigation ─────────────────────────────────────────────────────

    void scrollLeft() {
        m_targetX -= kScrollStep;
        clampTarget();
    }

    void scrollRight() {
        m_targetX += kScrollStep;
        clampTarget();
    }

    // ─── Scroll position ────────────────────────────────────────────────

    float scrollX() const { return m_scrollX; }

    void setScrollX(float x) {
        m_targetX = x;
        m_scrollX = x;
        clampTarget();
        m_scrollX = m_targetX;
    }

    // Whether scroll buttons are needed (content wider than container)
    bool canScroll() const {
        return m_contentWidth > m_bounds.w;
    }

    void setShowScrollButtons(bool show) { m_showButtons = show; }

    static constexpr float kScrollBtnW  = 28.0f;
    static constexpr float kScrollStep  = 256.0f;
    static constexpr float kScrollSpeed = 0.2f;   // lerp factor per frame

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext& ctx) override {
        float totalW = 0;
        float maxH = 0;
        for (auto* child : m_children) {
            Size cs = child->measure(Constraints::unbounded(), ctx);
            totalW += cs.w;
            maxH = detail::cmax(maxH, cs.h);
        }
        // Add gaps between children
        if (m_children.size() > 1)
            totalW += static_cast<float>(m_children.size() - 1) * m_gap;

        m_contentWidth = totalW;
        return c.constrain(Size{c.maxW, maxH});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;

        float vpX = bounds.x;
        float vpW = bounds.w;

        // Reserve space for scroll buttons when content overflows
        if (canScroll() && m_showButtons) {
            vpX += kScrollBtnW;
            vpW -= 2.0f * kScrollBtnW;
        }

        clampTarget();
        clampScroll();

        // Lay out children horizontally within the viewport
        float offset = 0;
        for (auto* child : m_children) {
            Size cs = child->measure(Constraints::unbounded(), ctx);
            float childX = vpX - m_scrollX + offset;
            child->layout(Rect{childX, bounds.y, cs.w, bounds.h}, ctx);
            offset += cs.w + m_gap;
        }
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void render(UIContext& ctx) override {
        if (!m_visible) return;
        paint(ctx);
    }

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;

        // Animate scroll position toward target
        if (std::fabs(m_scrollX - m_targetX) > 0.5f) {
            m_scrollX += (m_targetX - m_scrollX) * kScrollSpeed;
            // Re-layout children at the new scroll position
            float vpX = m_bounds.x;
            float vpW = m_bounds.w;
            if (canScroll() && m_showButtons) {
                vpX += kScrollBtnW;
                vpW -= 2.0f * kScrollBtnW;
            }
            float offset = 0;
            for (auto* child : m_children) {
                auto& cb = child->bounds();
                float childX = vpX - m_scrollX + offset;
                child->setBoundsX(childX);
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

        // Clip children to the viewport area
        ctx.renderer->pushClip(vpX, m_bounds.y, vpW, m_bounds.h);
        for (auto* child : m_children) {
            child->render(ctx);
        }
        ctx.renderer->popClip();

        // Draw scroll buttons when content overflows
        if (canScroll() && m_showButtons) {
            bool atLeft  = m_targetX <= 0.5f;
            bool atRight = m_targetX >= maxScroll() - 0.5f;

            Color normalBg{50, 50, 60, 255};
            Color limitBg{35, 35, 40, 255};

            // Left button
            Color lbg = atLeft ? limitBg : normalBg;
            ctx.renderer->drawRect(m_bounds.x, m_bounds.y,
                                   kScrollBtnW, m_bounds.h, lbg);

            Color lText = atLeft ? Theme::textDim : Theme::textPrimary;
            float scale = detail::cmin(m_bounds.h * 0.5f, 18.0f) / Theme::kFontSize;
            float tw = ctx.font->textWidth("<", scale);
            float tx = m_bounds.x + (kScrollBtnW - tw) * 0.5f;
            float ty = m_bounds.y + m_bounds.h * 0.2f;
            ctx.font->drawText(*ctx.renderer, "<", tx, ty, scale, lText);

            // Right button
            float rbX = m_bounds.x + m_bounds.w - kScrollBtnW;
            Color rbg = atRight ? limitBg : normalBg;
            ctx.renderer->drawRect(rbX, m_bounds.y,
                                   kScrollBtnW, m_bounds.h, rbg);

            Color rText = atRight ? Theme::textDim : Theme::textPrimary;
            tw = ctx.font->textWidth(">", scale);
            tx = rbX + (kScrollBtnW - tw) * 0.5f;
            ctx.font->drawText(*ctx.renderer, ">", tx, ty, scale, rText);
        }
#else
        (void)ctx;
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!canScroll() || !m_showButtons) return false;

        Point local = toLocal(e.x, e.y);

        // Left scroll button
        if (local.x >= 0 && local.x < kScrollBtnW &&
            local.y >= 0 && local.y < m_bounds.h) {
            scrollLeft();
            e.consume();
            return true;
        }

        // Right scroll button
        if (local.x >= m_bounds.w - kScrollBtnW && local.x < m_bounds.w &&
            local.y >= 0 && local.y < m_bounds.h) {
            scrollRight();
            e.consume();
            return true;
        }

        return false;
    }

    // Scroll wheel handler (call from App)
    void handleScroll(float dx, float /*dy*/) {
        if (dx > 0)
            scrollRight();
        else if (dx < 0)
            scrollLeft();
    }

    // ─── Configuration ──────────────────────────────────────────────────

    float gap() const { return m_gap; }
    void setGap(float g) { m_gap = g; }

private:
    std::vector<float> m_snapPoints;
    float m_scrollX = 0;
    float m_targetX = 0;
    float m_contentWidth = 0;
    bool  m_showButtons = true;
    int   m_currentSnap = 0;
    float m_gap = 0;

    void clampScroll() {
        m_scrollX = detail::cclamp(m_scrollX, 0.0f, maxScroll());
    }

    void clampTarget() {
        m_targetX = detail::cclamp(m_targetX, 0.0f, maxScroll());
    }

    float maxScroll() const {
        return detail::cmax(0.0f, m_contentWidth - viewportWidth());
    }

    float viewportWidth() const {
        if (canScroll() && m_showButtons)
            return m_bounds.w - 2.0f * kScrollBtnW;
        return m_bounds.w;
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
