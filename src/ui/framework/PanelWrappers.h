#pragma once
// Panel wrapper widgets — thin Widget subclasses that delegate to existing
// panel classes for rendering and events.  Only MenuBarWrapper remains;
// other panels have been migrated to full fw::Widget subclasses.

#include "Widget.h"
#include "v2/MenuBar.h"
#include "v2/UIContext.h"
#include "v2/V1EventBridge.h"

namespace yawn {
namespace ui {
namespace fw {

// ─── MenuBarWrapper ──────────────────────────────────────────────────────
//
// Thin v1-widget shim around the v2 FwMenuBar so the menu bar can live
// inside the v1 rootLayout's flex-box layout. The wrapper:
//   • measures / lays out the v2 widget within the cell the rootLayout
//     allocates
//   • renders the v2 widget through the fw2 UIContext on paint
//   • routes v1 mouseDown / mouseMove to the v2 widget via the
//     existing V1EventBridge helpers
//
// The v2 MenuBar's popup is owned by fw2::ContextMenuManager + painted
// via LayerStack, so it sits on top of all other panels automatically —
// no "render last" hack is needed here.
class MenuBarWrapper : public Widget {
public:
    explicit MenuBarWrapper(::yawn::ui::fw2::FwMenuBar& bar) : m_bar(bar) {}

    Size measure(const Constraints& c, const UIContext&) override {
        // Match v1 MenuBar height so layout cells stay the same as
        // before; full width of the container.
        constexpr float kBarHeight = 26.0f;
        return c.constrain({c.maxW, kBarHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_bar.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                     v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_bar.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_bar.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        return m_bar.dispatchMouseDown(ev);
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        // Always forward so hover-switch between titles tracks the
        // pointer even outside the bar (releases hover on leave).
        const auto& b = m_bar.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_bar.dispatchMouseMove(ev);
        return false;   // don't consume; other hover-aware widgets still see it
    }

private:
    ::yawn::ui::fw2::FwMenuBar& m_bar;
};

} // namespace fw
} // namespace ui
} // namespace yawn
