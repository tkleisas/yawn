#pragma once

// UI v2 — Stack.
//
// Z-ordered overlap container. All children occupy the same rect;
// paint order is the insertion order (first = bottom); per-child
// Alignment specifies where the child sits inside the stack if it's
// smaller than the full bounds.
//
// Stack is for INLINE overlapping — widgets layered within a single
// rect. For floating overlays / modals use LayerStack instead.
//
// See docs/widgets/stack.md for the full spec.

#include "Widget.h"

#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Alignment — per-child placement within the stack's rect.
// ───────────────────────────────────────────────────────────────────
struct StackAlignment {
    enum class HAlign { Left, Center, Right, Fill };
    enum class VAlign { Top,  Center, Bottom, Fill };
    HAlign h = HAlign::Fill;
    VAlign v = VAlign::Fill;

    constexpr bool operator==(const StackAlignment& o) const {
        return h == o.h && v == o.v;
    }
};

namespace StackAlign {
using HA = StackAlignment::HAlign;
using VA = StackAlignment::VAlign;
constexpr StackAlignment Fill        {HA::Fill,   VA::Fill};
constexpr StackAlignment Center      {HA::Center, VA::Center};
constexpr StackAlignment TopLeft     {HA::Left,   VA::Top};
constexpr StackAlignment TopCenter   {HA::Center, VA::Top};
constexpr StackAlignment TopRight    {HA::Right,  VA::Top};
constexpr StackAlignment MidLeft     {HA::Left,   VA::Center};
constexpr StackAlignment MidRight    {HA::Right,  VA::Center};
constexpr StackAlignment BottomLeft  {HA::Left,   VA::Bottom};
constexpr StackAlignment BottomCenter{HA::Center, VA::Bottom};
constexpr StackAlignment BottomRight {HA::Right,  VA::Bottom};
} // namespace StackAlign

// ───────────────────────────────────────────────────────────────────
// Stack widget
// ───────────────────────────────────────────────────────────────────
class Stack : public Widget {
public:
    Stack() = default;

    // Alternate addChild that records an alignment spec. The base-
    // class Widget::addChild still works (defaults to Fill/Fill); this
    // overload exists so callers can inline the alignment.
    void addChild(Widget* child, StackAlignment align);
    using Widget::addChild;   // keep the single-arg version available

    // Mutate alignment / insets post-insertion.
    void setAlignment(Widget* child, StackAlignment align);
    StackAlignment alignmentOf(Widget* child) const;

    void setInsets(Widget* child, Insets insets);
    Insets insetsOf(Widget* child) const;

    // Preferred size override. When > 0, the stack size is driven by
    // this rather than the "largest child" rule.
    void setPreferredSize(Size s);
    Size preferredSize() const { return m_preferredSize; }

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

private:
    struct PerChild {
        Widget*        child = nullptr;
        StackAlignment align = StackAlign::Fill;
        Insets         insets{};
    };

    PerChild*       findEntry(Widget* child);
    const PerChild* findEntry(Widget* child) const;
    PerChild&       findOrInsertEntry(Widget* child);

    // Constraints + positioning for a single child given the stack's
    // usable rect (bounds minus child insets).
    void layoutOneChild(const PerChild& e, Rect bounds, UIContext& ctx);

    std::vector<PerChild> m_entries;
    Size m_preferredSize{0, 0};
};

} // namespace fw2
} // namespace ui
} // namespace yawn
