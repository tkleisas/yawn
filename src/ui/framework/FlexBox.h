#pragma once
// FlexBox — Flexbox-style layout container.
//
// Lays out children along a main axis (Row or Column) with:
//   - Direction: Row (horizontal) or Column (vertical)
//   - Justify: distribution of children along main axis
//   - Align: positioning of children along cross axis
//   - Gap: spacing between children
//   - Per-child SizePolicy: fixed or flex-weighted sizing
//
// Layout algorithm:
//   1. Measure all children
//   2. Allocate fixed children their preferred main-axis size
//   3. Distribute remaining space to flex children by weight
//   4. Apply justify/align to position each child

#include "Widget.h"

namespace yawn {
namespace ui {
namespace fw {

class FlexBox : public Widget {
public:
    FlexBox() = default;
    FlexBox(Direction dir) : m_direction(dir) {}

    // ─── Configuration ──────────────────────────────────────────────────

    Direction direction() const { return m_direction; }
    void setDirection(Direction d) { m_direction = d; }

    Justify justify() const { return m_justify; }
    void setJustify(Justify j) { m_justify = j; }

    Align align() const { return m_align; }
    void setAlign(Align a) { m_align = a; }

    float gap() const { return m_gap; }
    void setGap(float g) { m_gap = g; }

    // ─── Measure ────────────────────────────────────────────────────────

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        float mainTotal = 0;
        float crossMax  = 0;
        int visibleCount = 0;

        // Loosen the main-axis constraint so children report natural size.
        // The FlexBox itself will distribute space during layout().
        Constraints inner = constraints.deflate(m_padding);
        Constraints childConstraints;
        if (m_direction == Direction::Row) {
            childConstraints = {0, inner.minH, inner.maxW, inner.maxH};
        } else {
            childConstraints = {inner.minW, 0, inner.maxW, inner.maxH};
        }
        m_childSizes.resize(m_children.size());

        for (size_t i = 0; i < m_children.size(); ++i) {
            auto* child = m_children[i];
            if (!child->isVisible()) continue;
            ++visibleCount;

            Size childSize = child->measure(childConstraints, ctx);
            m_childSizes[i] = childSize;

            if (m_direction == Direction::Row) {
                mainTotal += childSize.w;
                crossMax = detail::cmax(crossMax, childSize.h);
            } else {
                mainTotal += childSize.h;
                crossMax = detail::cmax(crossMax, childSize.w);
            }
        }

        float gapTotal = visibleCount > 1 ? m_gap * (visibleCount - 1) : 0;
        mainTotal += gapTotal;

        Size desired;
        if (m_direction == Direction::Row) {
            desired = {mainTotal + m_padding.horizontal(), crossMax + m_padding.vertical()};
        } else {
            desired = {crossMax + m_padding.horizontal(), mainTotal + m_padding.vertical()};
        }

        return constraints.constrain(desired);
    }

    // ─── Layout ─────────────────────────────────────────────────────────

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;

        Rect inner = bounds.inset(m_padding);
        float mainSize  = (m_direction == Direction::Row) ? inner.w : inner.h;
        float crossSize = (m_direction == Direction::Row) ? inner.h : inner.w;

        // Ensure child sizes are measured
        if (m_childSizes.size() != m_children.size()) {
            Constraints c = Constraints::loose(inner.w, inner.h);
            measure(c, ctx);
        }

        // Pass 1: Compute total fixed, total flex weight
        float totalFixed = 0;
        float totalFlex  = 0;
        int visibleCount = 0;

        for (size_t i = 0; i < m_children.size(); ++i) {
            if (!m_children[i]->isVisible()) continue;
            ++visibleCount;
            auto sp = m_children[i]->sizePolicy();
            float childMain = (m_direction == Direction::Row) ? m_childSizes[i].w : m_childSizes[i].h;

            if (sp.flexWeight > 0) {
                totalFlex += sp.flexWeight;
            } else {
                totalFixed += childMain;
            }
        }

        float gapTotal = visibleCount > 1 ? m_gap * (visibleCount - 1) : 0;
        float flexSpace = detail::cmax(0.0f, mainSize - totalFixed - gapTotal);

        // Pass 2: Assign main-axis sizes
        std::vector<float> mainSizes(m_children.size(), 0);
        for (size_t i = 0; i < m_children.size(); ++i) {
            if (!m_children[i]->isVisible()) continue;
            auto sp = m_children[i]->sizePolicy();
            float childMain = (m_direction == Direction::Row) ? m_childSizes[i].w : m_childSizes[i].h;

            if (sp.flexWeight > 0 && totalFlex > 0) {
                float allocated = (sp.flexWeight / totalFlex) * flexSpace;
                mainSizes[i] = detail::cclamp(allocated, sp.minSize, sp.maxSize);
            } else {
                mainSizes[i] = detail::cclamp(childMain, sp.minSize, sp.maxSize);
            }
        }

        // Compute total main-axis used space (for justify)
        float totalUsed = 0;
        for (size_t i = 0; i < m_children.size(); ++i) {
            if (!m_children[i]->isVisible()) continue;
            totalUsed += mainSizes[i];
        }
        totalUsed += gapTotal;

        // Pass 3: Position children (justify + align)
        float mainPos = computeJustifyStart(mainSize, totalUsed, visibleCount);

        for (size_t i = 0; i < m_children.size(); ++i) {
            if (!m_children[i]->isVisible()) continue;
            float childMain  = mainSizes[i];
            float childCross = (m_direction == Direction::Row) ? m_childSizes[i].h : m_childSizes[i].w;

            // Apply align (cross axis)
            float crossPos = 0;
            float crossExtent = childCross;
            switch (m_align) {
                case Align::Start:   crossPos = 0; break;
                case Align::Center:  crossPos = (crossSize - childCross) * 0.5f; break;
                case Align::End:     crossPos = crossSize - childCross; break;
                case Align::Stretch: crossPos = 0; crossExtent = crossSize; break;
            }

            Rect childBounds;
            if (m_direction == Direction::Row) {
                childBounds = {inner.x + mainPos, inner.y + crossPos, childMain, crossExtent};
            } else {
                childBounds = {inner.x + crossPos, inner.y + mainPos, crossExtent, childMain};
            }

            m_children[i]->layout(childBounds, ctx);
            mainPos += childMain + computeJustifyGap(mainSize, totalUsed, visibleCount);
        }
    }

private:
    float computeJustifyStart(float mainSize, float totalUsed, int count) const {
        float remaining = detail::cmax(0.0f, mainSize - totalUsed);
        switch (m_justify) {
            case Justify::Start:        return 0;
            case Justify::Center:       return remaining * 0.5f;
            case Justify::End:          return remaining;
            case Justify::SpaceBetween: return 0;
            case Justify::SpaceAround:  return count > 0 ? remaining / (count * 2.0f) : 0;
        }
        return 0;
    }

    float computeJustifyGap(float mainSize, float totalUsed, int count) const {
        float remaining = detail::cmax(0.0f, mainSize - totalUsed);
        switch (m_justify) {
            case Justify::Start:
            case Justify::Center:
            case Justify::End:
                return m_gap;
            case Justify::SpaceBetween:
                return count > 1 ? remaining / (count - 1) + m_gap : m_gap;
            case Justify::SpaceAround:
                return count > 0 ? remaining / count + m_gap : m_gap;
        }
        return m_gap;
    }

    Direction m_direction = Direction::Column;
    Justify   m_justify   = Justify::Start;
    Align     m_align     = Align::Stretch;
    float     m_gap       = 0;

    std::vector<Size> m_childSizes;  // Cached from measure pass
};

} // namespace fw
} // namespace ui
} // namespace yawn
