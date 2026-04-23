#pragma once
// V1WidgetAdapter — fw2::Widget shim that embeds a v1 (fw::Widget) panel
// inside a fw2 container (e.g. fw2::ContentGrid).
//
// Purpose: ArrangementPanel is still v1; ContentGrid is now fw2. Rather
// than migrating ArrangementPanel immediately we wrap it here so the grid
// can treat it like any other fw2 child.
//
// The adapter:
//   • delegates measure / layout to the v1 widget using a stored UIContext ref
//   • calls v1 Widget::paint() from its own render() override
//   • translates fw2 mouse/scroll events to v1 via V1EventBridge
//
// Once ArrangementPanel is fully migrated to fw2 this class can be deleted.

#include "Widget.h"
#include "V1EventBridge.h"
#include "ui/framework/Widget.h"       // fw::Widget
#include "ui/framework/UIContext.h"    // fw::UIContext
#include "ui/framework/EventSystem.h"  // fw::Rect, Constraints, ScrollEvent

namespace yawn {
namespace ui {
namespace fw2 {

class V1WidgetAdapter : public Widget {
public:
    V1WidgetAdapter() = default;

    // The v1 UIContext must remain valid for the adapter's lifetime.
    void setV1Widget(::yawn::ui::fw::Widget* w) { m_v1w = w; }
    void setV1Context(const ::yawn::ui::fw::UIContext* ctx) { m_ctx = ctx; }

    // ─── fw2 Widget interface ────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        if (!m_v1w || !m_ctx) return c.constrain({c.maxW, c.maxH});
        ::yawn::ui::fw::Constraints vc =
            ::yawn::ui::fw::Constraints::loose(c.maxW, c.maxH);
        auto s = m_v1w->measure(vc, *m_ctx);
        return c.constrain({s.w, s.h});
    }

    void onLayout(Rect bounds, UIContext&) override {
        m_bounds = bounds;
        if (!m_v1w || !m_ctx) return;
        ::yawn::ui::fw::Constraints vc =
            ::yawn::ui::fw::Constraints::tight(bounds.w, bounds.h);
        m_v1w->measure(vc, *m_ctx);
        m_v1w->layout(::yawn::ui::fw::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                       const_cast<::yawn::ui::fw::UIContext&>(*m_ctx));
    }

    void render(UIContext&) override {
        if (!m_v1w || !m_ctx || !m_v1w->visible()) return;
        // Call the v1 paint path. Both rendering paths ultimately write to the
        // same underlying SDL/GL buffer, so mixing them in one frame is safe.
        m_v1w->paint(const_cast<::yawn::ui::fw::UIContext&>(*m_ctx));
    }

    // ─── Mouse events ────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_v1w || !m_v1w->visible()) return false;
        auto ve = toFw1Mouse(e);
        ve.clickCount = e.clickCount;   // preserve SDL double-click info
        return m_v1w->dispatchMouseDown(ve) != nullptr;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_v1w) return false;
        auto ve = toFw1MouseMove(e);
        m_v1w->dispatchMouseMove(ve);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (!m_v1w) return false;
        auto ve = toFw1Mouse(e);
        m_v1w->dispatchMouseUp(ve);
        return true;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_v1w || !m_v1w->visible()) return false;
        ::yawn::ui::fw::ScrollEvent se{};
        se.x = e.x; se.y = e.y;
        se.dx = e.dx; se.dy = e.dy;
        return m_v1w->dispatchScroll(se) != nullptr;
    }

    // Passthrough for methods App.cpp may call directly on the wrapped panel.
    ::yawn::ui::fw::Widget* v1Widget() const { return m_v1w; }

private:
    ::yawn::ui::fw::Widget*          m_v1w = nullptr;
    const ::yawn::ui::fw::UIContext* m_ctx = nullptr;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
