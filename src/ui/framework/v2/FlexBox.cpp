#include "FlexBox.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
} // anon

void FlexBox::setDirection(Direction d) {
    if (d != m_direction) { m_direction = d; invalidate(); }
}
void FlexBox::setJustify(Justify j) {
    if (j != m_justify) { m_justify = j; invalidate(); }
}
void FlexBox::setAlign(Align a) {
    if (a != m_align) { m_align = a; invalidate(); }
}
void FlexBox::setGap(float g) {
    if (g != m_gap) { m_gap = g; invalidate(); }
}

Size FlexBox::onMeasure(Constraints constraints, UIContext& ctx) {
    m_childSizes.assign(m_children.size(), Size{0, 0});

    Constraints inner = constraints.deflate(m_padding);

    // Children are measured with the cross-axis constraint passed
    // through, and the main-axis unbounded so each reports its
    // intrinsic size.
    Constraints childC;
    if (m_direction == Direction::Row) {
        childC = {0, inner.minH, inner.maxW, inner.maxH};
    } else {
        childC = {inner.minW, 0, inner.maxW, inner.maxH};
    }

    float mainTotal = 0.0f;
    float crossMax  = 0.0f;
    int   visibleCount = 0;

    for (size_t i = 0; i < m_children.size(); ++i) {
        Widget* child = m_children[i];
        if (!child || !child->isVisible()) continue;
        ++visibleCount;
        Size cs = child->measure(childC, ctx);
        m_childSizes[i] = cs;
        if (m_direction == Direction::Row) {
            mainTotal += cs.w;
            crossMax   = std::max(crossMax, cs.h);
        } else {
            mainTotal += cs.h;
            crossMax   = std::max(crossMax, cs.w);
        }
    }

    const float gapTotal = visibleCount > 1 ? m_gap * (visibleCount - 1) : 0.0f;
    mainTotal += gapTotal;

    Size desired;
    if (m_direction == Direction::Row) {
        desired = {mainTotal + m_padding.horizontal(), crossMax + m_padding.vertical()};
    } else {
        desired = {crossMax + m_padding.horizontal(), mainTotal + m_padding.vertical()};
    }
    return constraints.constrain(desired);
}

void FlexBox::onLayout(Rect bounds, UIContext& ctx) {
    Rect inner = bounds.inset(m_padding);
    const float mainSize  = (m_direction == Direction::Row) ? inner.w : inner.h;
    const float crossSize = (m_direction == Direction::Row) ? inner.h : inner.w;

    // Ensure child sizes are populated. If onLayout is somehow called
    // without a prior onMeasure (unlikely under normal framework flow,
    // but defensive), recompute.
    if (m_childSizes.size() != m_children.size()) {
        Constraints c = Constraints::loose(inner.w, inner.h);
        (void)measure(c, ctx);
    }

    // Pass 1: totals.
    float totalFixed = 0.0f;
    float totalFlex  = 0.0f;
    int   visibleCount = 0;

    for (size_t i = 0; i < m_children.size(); ++i) {
        Widget* child = m_children[i];
        if (!child || !child->isVisible()) continue;
        ++visibleCount;
        const SizePolicy sp = child->sizePolicy();
        const float childMain = (m_direction == Direction::Row)
                                 ? m_childSizes[i].w : m_childSizes[i].h;
        if (sp.flexWeight > 0) totalFlex  += sp.flexWeight;
        else                    totalFixed += childMain;
    }

    const float gapTotal = visibleCount > 1 ? m_gap * (visibleCount - 1) : 0.0f;
    const float flexSpace = std::max(0.0f, mainSize - totalFixed - gapTotal);

    // Pass 2: main-axis size per child.
    std::vector<float> mainSizes(m_children.size(), 0.0f);
    for (size_t i = 0; i < m_children.size(); ++i) {
        Widget* child = m_children[i];
        if (!child || !child->isVisible()) continue;
        const SizePolicy sp = child->sizePolicy();
        const float childMain = (m_direction == Direction::Row)
                                 ? m_childSizes[i].w : m_childSizes[i].h;
        if (sp.flexWeight > 0 && totalFlex > 0) {
            const float allocated = (sp.flexWeight / totalFlex) * flexSpace;
            mainSizes[i] = clampf(allocated, sp.minSize, sp.maxSize);
        } else {
            mainSizes[i] = clampf(childMain, sp.minSize, sp.maxSize);
        }
    }

    // Total main-axis used (for justify spacing).
    float totalUsed = gapTotal;
    for (size_t i = 0; i < m_children.size(); ++i) {
        Widget* child = m_children[i];
        if (!child || !child->isVisible()) continue;
        totalUsed += mainSizes[i];
    }

    // Justify start position + per-gap delta.
    auto justifyStart = [&]() -> float {
        const float remaining = std::max(0.0f, mainSize - totalUsed);
        switch (m_justify) {
            case Justify::Start:        return 0.0f;
            case Justify::Center:       return remaining * 0.5f;
            case Justify::End:          return remaining;
            case Justify::SpaceBetween: return 0.0f;
            case Justify::SpaceAround:
                return visibleCount > 0 ? remaining / (visibleCount * 2.0f) : 0.0f;
        }
        return 0.0f;
    };
    auto justifyGap = [&]() -> float {
        const float remaining = std::max(0.0f, mainSize - totalUsed);
        switch (m_justify) {
            case Justify::Start:
            case Justify::Center:
            case Justify::End:
                return m_gap;
            case Justify::SpaceBetween:
                return visibleCount > 1 ? m_gap + remaining / (visibleCount - 1) : 0.0f;
            case Justify::SpaceAround:
                return visibleCount > 0 ? m_gap + remaining / visibleCount : 0.0f;
        }
        return m_gap;
    };

    float mainPos = justifyStart();
    const float gapStep = justifyGap();

    // Pass 3: position each visible child.
    for (size_t i = 0; i < m_children.size(); ++i) {
        Widget* child = m_children[i];
        if (!child || !child->isVisible()) continue;

        const float childMain  = mainSizes[i];
        const float childCross = (m_direction == Direction::Row)
                                  ? m_childSizes[i].h : m_childSizes[i].w;

        float crossPos = 0.0f;
        float crossExtent = childCross;
        switch (m_align) {
            case Align::Start:   crossPos = 0.0f; break;
            case Align::Center:  crossPos = (crossSize - childCross) * 0.5f; break;
            case Align::End:     crossPos = crossSize - childCross; break;
            case Align::Stretch: crossPos = 0.0f; crossExtent = crossSize; break;
        }

        Rect childBounds;
        if (m_direction == Direction::Row) {
            childBounds = {inner.x + mainPos, inner.y + crossPos, childMain, crossExtent};
        } else {
            childBounds = {inner.x + crossPos, inner.y + mainPos, crossExtent, childMain};
        }

        child->layout(childBounds, ctx);
        mainPos += childMain + gapStep;
    }
}

// ─── Mouse dispatch ─────────────────────────────────────────────────
//
// Own-dispatch container — find the visible child whose laid-out bounds
// contain the press, dispatch into it. Move/up forward to whatever
// descendant currently holds capture (knob drag, fader drag, scrollbar
// drag, panel resize handle…), guarded by a containment check so a
// captured widget OUTSIDE this FlexBox subtree (e.g. another panel or
// an overlay) doesn't get double-fed.

bool FlexBox::onMouseDown(MouseEvent& e) {
    // Iterate front-to-back so the topmost child wins (last addChild).
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        Widget* child = *it;
        if (!child || !child->isVisible() || !child->isEnabled()) continue;
        const Rect& cb = child->bounds();
        if (e.x < cb.x || e.x >= cb.x + cb.w) continue;
        if (e.y < cb.y || e.y >= cb.y + cb.h) continue;
        MouseEvent ce = e;
        ce.lx = e.x - cb.x;
        ce.ly = e.y - cb.y;
        if (child->dispatchMouseDown(ce)) return true;
    }
    return false;
}

namespace {
// True if `cap` is inside `flex`'s subtree (any descendant depth).
// Used to gate move/up forwarding so we don't double-route a capture
// that belongs to an entirely different parent (e.g. an overlay).
bool isCapturedDescendant(const Widget* flex, const Widget* cap) {
    for (const Widget* p = cap; p; p = p->parent())
        if (p == flex) return true;
    return false;
}
} // anon

bool FlexBox::onMouseMove(MouseMoveEvent& e) {
    if (Widget* cap = capturedWidget()) {
        if (isCapturedDescendant(this, cap)) {
            const Rect& cb = cap->bounds();
            MouseMoveEvent ce = e;
            ce.lx = e.x - cb.x;
            ce.ly = e.y - cb.y;
            cap->dispatchMouseMove(ce);
            return true;
        }
    }
    // Hover propagation: forward to the child currently under the
    // pointer so it can update its hover state.
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        Widget* child = *it;
        if (!child || !child->isVisible()) continue;
        const Rect& cb = child->bounds();
        if (e.x < cb.x || e.x >= cb.x + cb.w) continue;
        if (e.y < cb.y || e.y >= cb.y + cb.h) continue;
        MouseMoveEvent ce = e;
        ce.lx = e.x - cb.x;
        ce.ly = e.y - cb.y;
        child->dispatchMouseMove(ce);
        return false;   // hover is non-exclusive
    }
    return false;
}

bool FlexBox::onMouseUp(MouseEvent& e) {
    if (Widget* cap = capturedWidget()) {
        if (isCapturedDescendant(this, cap)) {
            const Rect& cb = cap->bounds();
            MouseEvent ce = e;
            ce.lx = e.x - cb.x;
            ce.ly = e.y - cb.y;
            cap->dispatchMouseUp(ce);
            return true;
        }
    }
    // No capture in our subtree — try to deliver to whichever child
    // contains the release (typical click flow on a stateless widget
    // that didn't capture on press, e.g. a Button that fires onClick
    // from its gesture SM).
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        Widget* child = *it;
        if (!child || !child->isVisible()) continue;
        const Rect& cb = child->bounds();
        if (e.x < cb.x || e.x >= cb.x + cb.w) continue;
        if (e.y < cb.y || e.y >= cb.y + cb.h) continue;
        MouseEvent ce = e;
        ce.lx = e.x - cb.x;
        ce.ly = e.y - cb.y;
        if (child->dispatchMouseUp(ce)) return true;
    }
    return false;
}

bool FlexBox::onScroll(ScrollEvent& e) {
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        Widget* child = *it;
        if (!child || !child->isVisible()) continue;
        const Rect& cb = child->bounds();
        if (e.x < cb.x || e.x >= cb.x + cb.w) continue;
        if (e.y < cb.y || e.y >= cb.y + cb.h) continue;
        ScrollEvent ce = e;
        ce.lx = e.x - cb.x;
        ce.ly = e.y - cb.y;
        if (child->dispatchScroll(ce)) return true;
    }
    return false;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
