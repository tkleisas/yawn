#include "Stack.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

void Stack::addChild(Widget* child, StackAlignment align) {
    if (!child) return;
    Widget::addChild(child);
    PerChild& e = findOrInsertEntry(child);
    e.align = align;
    invalidate();
}

void Stack::setAlignment(Widget* child, StackAlignment align) {
    PerChild* e = findEntry(child);
    if (!e) return;
    if (e->align == align) return;
    e->align = align;
    invalidate();
}

StackAlignment Stack::alignmentOf(Widget* child) const {
    const PerChild* e = findEntry(child);
    return e ? e->align : StackAlign::Fill;
}

void Stack::setInsets(Widget* child, Insets insets) {
    PerChild* e = findEntry(child);
    if (!e) return;
    // Compare without relying on Insets having operator==.
    if (e->insets.top == insets.top && e->insets.right == insets.right &&
        e->insets.bottom == insets.bottom && e->insets.left == insets.left) return;
    e->insets = insets;
    invalidate();
}

Insets Stack::insetsOf(Widget* child) const {
    const PerChild* e = findEntry(child);
    return e ? e->insets : Insets{};
}

void Stack::setPreferredSize(Size s) {
    if (s.w == m_preferredSize.w && s.h == m_preferredSize.h) return;
    m_preferredSize = s;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Measure / layout
// ───────────────────────────────────────────────────────────────────

Size Stack::onMeasure(Constraints c, UIContext& ctx) {
    if (m_preferredSize.w > 0.0f && m_preferredSize.h > 0.0f) {
        return c.constrain(m_preferredSize);
    }

    float w = 0.0f, h = 0.0f;
    for (Widget* child : m_children) {
        if (!child || !child->isVisible()) continue;
        const PerChild* e = findEntry(child);
        const StackAlignment a = e ? e->align : StackAlign::Fill;

        // For Fill children measure with the stack's constraint; for
        // non-Fill children measure loose so we get their intrinsic
        // size contribution.
        Constraints cc = c;
        if (a.h != StackAlignment::HAlign::Fill) cc.minW = 0.0f;
        if (a.v != StackAlignment::VAlign::Fill) cc.minH = 0.0f;
        Size cs = child->measure(cc, ctx);
        w = std::max(w, cs.w);
        h = std::max(h, cs.h);
    }
    return c.constrain({w, h});
}

void Stack::onLayout(Rect bounds, UIContext& ctx) {
    for (Widget* child : m_children) {
        if (!child || !child->isVisible()) continue;
        PerChild* e = findEntry(child);
        const PerChild fallback{child, StackAlign::Fill, Insets{}};
        const PerChild& entry = e ? *e : fallback;
        layoutOneChild(entry, bounds, ctx);
    }
}

void Stack::layoutOneChild(const PerChild& e, Rect bounds, UIContext& ctx) {
    if (!e.child) return;

    // Apply per-child insets to the stack bounds → "container" rect.
    Rect container{
        bounds.x + e.insets.left,
        bounds.y + e.insets.top,
        std::max(0.0f, bounds.w - e.insets.left - e.insets.right),
        std::max(0.0f, bounds.h - e.insets.top  - e.insets.bottom),
    };

    const bool fillH = (e.align.h == StackAlignment::HAlign::Fill);
    const bool fillV = (e.align.v == StackAlignment::VAlign::Fill);

    Constraints cc;
    cc.minW = fillH ? container.w : 0.0f;
    cc.maxW = container.w;
    cc.minH = fillV ? container.h : 0.0f;
    cc.maxH = container.h;
    Size cs = e.child->measure(cc, ctx);

    cs.w = std::min(cs.w, container.w);
    cs.h = std::min(cs.h, container.h);

    float x = container.x;
    if (e.align.h == StackAlignment::HAlign::Center)
        x += (container.w - cs.w) * 0.5f;
    else if (e.align.h == StackAlignment::HAlign::Right)
        x += container.w - cs.w;

    float y = container.y;
    if (e.align.v == StackAlignment::VAlign::Center)
        y += (container.h - cs.h) * 0.5f;
    else if (e.align.v == StackAlignment::VAlign::Bottom)
        y += container.h - cs.h;

    const float finalW = fillH ? container.w : cs.w;
    const float finalH = fillV ? container.h : cs.h;
    e.child->layout(Rect{x, y, finalW, finalH}, ctx);
}

// ───────────────────────────────────────────────────────────────────
// Per-child entry lookup
// ───────────────────────────────────────────────────────────────────

Stack::PerChild* Stack::findEntry(Widget* child) {
    for (auto& e : m_entries) if (e.child == child) return &e;
    return nullptr;
}

const Stack::PerChild* Stack::findEntry(Widget* child) const {
    for (auto& e : m_entries) if (e.child == child) return &e;
    return nullptr;
}

Stack::PerChild& Stack::findOrInsertEntry(Widget* child) {
    if (PerChild* e = findEntry(child)) return *e;
    m_entries.push_back({child, StackAlign::Fill, Insets{}});
    return m_entries.back();
}

} // namespace fw2
} // namespace ui
} // namespace yawn
