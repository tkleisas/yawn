#pragma once
// ConstraintBox — Constraint-based layout container.
//
// Positions a single child within its bounds using anchor constraints.
// Each edge (top, right, bottom, left) can be anchored with an offset.
// Unanchored edges fall back to the child's measured size.
//
// Example: anchor left=10, right=10 → child fills width minus 20px margins.
// Example: anchor top=10, no bottom → child at top with measured height.

#include "Widget.h"
#include <optional>

namespace yawn {
namespace ui {
namespace fw {

struct EdgeConstraint {
    std::optional<float> top;
    std::optional<float> right;
    std::optional<float> bottom;
    std::optional<float> left;

    bool centerH = false;
    bool centerV = false;

    static EdgeConstraint anchored(std::optional<float> t, std::optional<float> r,
                                   std::optional<float> b, std::optional<float> l) {
        return {t, r, b, l, false, false};
    }
    static EdgeConstraint centered(bool h = true, bool v = true) {
        return {{}, {}, {}, {}, h, v};
    }
};

class ConstraintBox : public Widget {
public:
    ConstraintBox() = default;

    // Set constraints for a child widget.
    void setConstraints(Widget* child, const EdgeConstraint& ec) {
        for (auto& e : m_constraints) {
            if (e.widget == child) { e.constraint = ec; return; }
        }
        m_constraints.push_back({child, ec});
    }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        // ConstraintBox takes the full offered size
        for (auto* child : m_children) {
            if (!child->isVisible()) continue;
            child->measure(constraints, ctx);
        }
        return {constraints.maxW, constraints.maxH};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        Rect inner = bounds.inset(m_padding);

        for (auto* child : m_children) {
            if (!child->isVisible()) continue;

            Size childSize = child->measure(
                Constraints::loose(inner.w, inner.h), ctx);

            EdgeConstraint ec;
            for (const auto& e : m_constraints) {
                if (e.widget == child) { ec = e.constraint; break; }
            }

            float cx, cy, cw, ch;

            // Horizontal
            if (ec.centerH) {
                cw = childSize.w;
                cx = inner.x + (inner.w - cw) * 0.5f;
            } else if (ec.left.has_value() && ec.right.has_value()) {
                cx = inner.x + *ec.left;
                cw = inner.w - *ec.left - *ec.right;
            } else if (ec.left.has_value()) {
                cx = inner.x + *ec.left;
                cw = childSize.w;
            } else if (ec.right.has_value()) {
                cw = childSize.w;
                cx = inner.x + inner.w - *ec.right - cw;
            } else {
                cx = inner.x;
                cw = childSize.w;
            }

            // Vertical
            if (ec.centerV) {
                ch = childSize.h;
                cy = inner.y + (inner.h - ch) * 0.5f;
            } else if (ec.top.has_value() && ec.bottom.has_value()) {
                cy = inner.y + *ec.top;
                ch = inner.h - *ec.top - *ec.bottom;
            } else if (ec.top.has_value()) {
                cy = inner.y + *ec.top;
                ch = childSize.h;
            } else if (ec.bottom.has_value()) {
                ch = childSize.h;
                cy = inner.y + inner.h - *ec.bottom - ch;
            } else {
                cy = inner.y;
                ch = childSize.h;
            }

            child->layout(Rect{cx, cy, detail::cmax(0.0f, cw), detail::cmax(0.0f, ch)}, ctx);
        }
    }

private:
    struct ChildConstraint {
        Widget*        widget = nullptr;
        EdgeConstraint constraint;
    };
    std::vector<ChildConstraint> m_constraints;
};

} // namespace fw
} // namespace ui
} // namespace yawn
