#pragma once
// fw::SnapScrollContainer — v1 wrapper around fw2::SnapScrollContainer.
//
// DetailPanelWidget (still v1) keeps owning m_scroll as a value member
// and calls addChild/layout/paint/onMouseDown/handleScroll on it. This
// shim forwards each call to the fw2 impl, stashing the v1 UIContext
// beforehand so fw2 can dispatch v1 calls back to our v1 children.
// Retires when DetailPanelWidget migrates to fw2 and can hold fw2
// children directly.

#include "Widget.h"
#include "EventSystem.h"
#include "UIContext.h"
#include "v2/SnapScrollContainer.h"
#include "v2/UIContext.h"
#include "v2/V1EventBridge.h"

#include <utility>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

class SnapScrollContainer : public Widget {
public:
    SnapScrollContainer() { setName("SnapScrollContainer"); }

    static constexpr float kScrollBtnW  = ::yawn::ui::fw2::SnapScrollContainer::kScrollBtnW;
    static constexpr float kScrollStep  = ::yawn::ui::fw2::SnapScrollContainer::kScrollStep;
    static constexpr float kScrollSpeed = ::yawn::ui::fw2::SnapScrollContainer::kScrollSpeed;

    // ─── Children (non-owning) ───────────────────────────────────────

    void addChild(Widget* child) {
        m_impl.addV1Child(child);
    }
    void removeAllChildren() {
        m_impl.removeAllV1Children();
    }

    // ─── Snap / scroll state pass-through ────────────────────────────

    void setSnapPoints(std::vector<float> p) { m_impl.setSnapPoints(std::move(p)); }
    void scrollLeft()                          { m_impl.scrollLeft(); }
    void scrollRight()                         { m_impl.scrollRight(); }
    void handleScroll(float dx, float dy)      { m_impl.handleScroll(dx, dy); }
    float scrollX() const                      { return m_impl.scrollX(); }
    void setScrollX(float x)                   { m_impl.setScrollX(x); }
    bool canScroll() const                     { return m_impl.canScroll(); }
    void setShowScrollButtons(bool s)          { m_impl.setShowScrollButtons(s); }
    float gap() const                          { return m_impl.gap(); }
    void setGap(float g)                       { m_impl.setGap(g); }

    ::yawn::ui::fw2::SnapScrollContainer& impl() { return m_impl; }

    // ─── Lifecycle delegation ────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext& ctx) override {
        m_impl.setV1Ctx(&ctx);
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        ::yawn::ui::fw2::Constraints fc{c.minW, c.minH, c.maxW, c.maxH};
        auto s = m_impl.measure(fc, v2ctx);
        m_impl.setV1Ctx(nullptr);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
        m_impl.setV1Ctx(&ctx);
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_impl.measure(fc, v2ctx);
        m_impl.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                       v2ctx);
        m_impl.setV1Ctx(nullptr);
    }

    void paint(UIContext& ctx) override {
        m_impl.setV1Ctx(&ctx);
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_impl.render(v2ctx);
        m_impl.setV1Ctx(nullptr);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_impl.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        return m_impl.dispatchMouseDown(ev);
    }

private:
    ::yawn::ui::fw2::SnapScrollContainer m_impl;
};

} // namespace fw
} // namespace ui
} // namespace yawn
