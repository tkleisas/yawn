#pragma once
// Panel wrapper widgets — thin v1 Widget subclasses that delegate to
// fw2 panel classes. Each wrapper lives in the v1 rootLayout and
// forwards measure/layout/render + events to an inner fw2 widget using
// the v1↔fw2 event bridge.
//
// Currently wraps FwMenuBar + TransportPanel. As more panels migrate
// to fw2 this file grows — eventually once every panel is fw2 the
// rootLayout itself can migrate and these wrappers retire.

#include "Widget.h"
#include "v2/MenuBar.h"
#include "v2/UIContext.h"
#include "v2/V1EventBridge.h"
#include "v2/ContentGrid.h"

#include "ui/panels/TransportPanel.h"
#include "ui/panels/ReturnMasterPanel.h"
#include "ui/panels/MixerPanel.h"
#include "ui/panels/VisualParamsPanel.h"
#include "ui/panels/BrowserPanel.h"
#include "ui/panels/SessionPanel.h"

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
        // before; full width of the container. Drive the inner v2
        // bar's measure too so it builds its title strips (which
        // live in its internal state, not in the wrapper).
        constexpr float kBarHeight = 26.0f;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        Constraints fc = Constraints::loose(c.maxW, kBarHeight);
        m_bar.measure(fc, v2ctx);
        return c.constrain({c.maxW, kBarHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        // Re-measure so strips are up to date, then seat at bounds.
        Constraints fc = Constraints::loose(bounds.w, bounds.h);
        m_bar.measure(fc, v2ctx);
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

// ─── TransportPanelWrapper ───────────────────────────────────────────
//
// v1 shim around the fw2 TransportPanel so it can sit inside the v1
// rootLayout. Forwards measure / layout / render / mouse through the
// v1↔fw2 event bridge. On mouse-down that the inner panel handles
// (or that triggers fw2 widget capture), we captureMouse() on the v1
// side so follow-up moves and releases continue to reach us.

class TransportPanelWrapper : public Widget {
public:
    explicit TransportPanelWrapper(::yawn::ui::fw2::TransportPanel& panel)
        : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(
            c.maxW, ::yawn::ui::Theme::kTransportBarHeight);
        auto s = m_panel.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_panel.measure(fc, v2ctx);
        m_panel.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                        v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_panel.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_panel.dispatchMouseDown(ev);
        // If a fw2 child captured (number-input drag, button press),
        // take v1 capture too so follow-up moves / release reach us.
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_panel.dispatchMouseMove(ev);
        return false;   // don't consume — hover is non-exclusive
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        // Forward to any captured fw2 child. Widget::dispatchMouseUp
        // walks its own gesture SM; releasing v1 capture happens here
        // after fw2 has released too.
        if (auto* cap = ::yawn::ui::fw2::Widget::capturedWidget()) {
            const auto& cb = cap->bounds();
            auto cev = ::yawn::ui::fw2::toFw2Mouse(e, cb);
            cap->dispatchMouseUp(cev);
        }
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        (void)ev;
        return true;
    }

private:
    ::yawn::ui::fw2::TransportPanel& m_panel;
};

// ─── ReturnMasterPanelWrapper ────────────────────────────────────────
//
// v1 shim around the fw2 ReturnMasterPanel. Same pattern as
// TransportPanelWrapper: measure/layout/render forward to the inner
// fw2 widget using the v2 UIContext, and mouse events convert through
// the V1EventBridge before dispatchMouseDown/Up/Move.
//
// The panel lives inside ContentGrid's bottom-right slot — since
// ContentGrid is v1, it needs a v1 Widget handle.

class ReturnMasterPanelWrapper : public Widget {
public:
    explicit ReturnMasterPanelWrapper(::yawn::ui::fw2::ReturnMasterPanel& panel)
        : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(c.maxW, c.maxH);
        auto s = m_panel.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_panel.measure(fc, v2ctx);
        m_panel.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                        v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_panel.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_panel.dispatchMouseDown(ev);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_panel.dispatchMouseMove(ev);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        m_panel.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return true;
    }

    // Forward a couple of app-facing methods that App.cpp still calls
    // directly on the panel pointer.
    ::yawn::ui::fw2::ReturnMasterPanel& panel() { return m_panel; }

private:
    ::yawn::ui::fw2::ReturnMasterPanel& m_panel;
};

// ─── MixerPanelWrapper ──────────────────────────────────────────────
//
// v1 shim around the fw2 MixerPanel so it can sit inside the v1
// ContentGrid's bottom-left slot. Forwards measure/layout/render +
// mouse/scroll events through the v1↔fw2 bridge.

class MixerPanelWrapper : public Widget {
public:
    explicit MixerPanelWrapper(::yawn::ui::fw2::MixerPanel& panel)
        : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(
            c.maxW, ::yawn::ui::fw2::MixerPanel::kMixerHeight);
        auto s = m_panel.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_panel.measure(fc, v2ctx);
        m_panel.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                        v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_panel.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_panel.dispatchMouseDown(ev);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_panel.dispatchMouseMove(ev);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        m_panel.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return true;
    }

    // Forward wheel scroll — App.cpp still calls this directly for
    // the mixer's horizontal pan (see the MOUSE_WHEEL branch).
    bool onScroll(ScrollEvent& e) override {
        ::yawn::ui::fw2::ScrollEvent se{};
        se.x = e.x;
        se.y = e.y;
        se.dx = e.dx;
        se.dy = e.dy;
        return m_panel.dispatchScroll(se);
    }

    ::yawn::ui::fw2::MixerPanel& panel() { return m_panel; }

private:
    ::yawn::ui::fw2::MixerPanel& m_panel;
};

// ─── VisualParamsPanelWrapper ────────────────────────────────────────
//
// v1 shim around the fw2 VisualParamsPanel. Sits directly inside
// m_rootLayout (not inside ContentGrid).

class VisualParamsPanelWrapper : public Widget {
public:
    explicit VisualParamsPanelWrapper(::yawn::ui::fw2::VisualParamsPanel& panel)
        : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(c.maxW, c.maxH);
        auto s = m_panel.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_panel.measure(fc, v2ctx);
        m_panel.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                        v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_panel.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_panel.dispatchMouseDown(ev);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_panel.dispatchMouseMove(ev);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        m_panel.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return true;
    }

    ::yawn::ui::fw2::VisualParamsPanel& panel() { return m_panel; }

private:
    ::yawn::ui::fw2::VisualParamsPanel& m_panel;
};

// ─── BrowserPanelWrapper ────────────────────────────────────────────
//
// v1 shim around the fw2 BrowserPanel for ContentGrid's top-right slot.

class BrowserPanelWrapper : public Widget {
public:
    explicit BrowserPanelWrapper(::yawn::ui::fw2::BrowserPanel& panel)
        : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(c.maxW, c.maxH);
        auto s = m_panel.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_panel.measure(fc, v2ctx);
        m_panel.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                        v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_panel.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_panel.dispatchMouseDown(ev);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_panel.dispatchMouseMove(ev);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        m_panel.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return true;
    }

    bool onScroll(ScrollEvent& e) override {
        ::yawn::ui::fw2::ScrollEvent se{};
        se.x = e.x;
        se.y = e.y;
        se.dx = e.dx;
        se.dy = e.dy;
        return m_panel.dispatchScroll(se);
    }

    ::yawn::ui::fw2::BrowserPanel& panel() { return m_panel; }

private:
    ::yawn::ui::fw2::BrowserPanel& m_panel;
};

// ─── SessionPanelWrapper ────────────────────────────────────────────
//
// v1 shim for fw2 SessionPanel (ContentGrid top-left slot).
// Passes the v1 MouseEvent's clickCount through to the panel's
// onMouseDownWithClicks so double-click-to-rename still works.

class SessionPanelWrapper : public Widget {
public:
    explicit SessionPanelWrapper(::yawn::ui::fw2::SessionPanel& panel)
        : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(c.maxW, c.maxH);
        auto s = m_panel.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_panel.measure(fc, v2ctx);
        m_panel.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                        v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_panel.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_panel.onMouseDownWithClicks(ev, e.clickCount);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_panel.dispatchMouseMove(ev);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_panel.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        m_panel.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return true;
    }

    ::yawn::ui::fw2::SessionPanel& panel() { return m_panel; }

private:
    ::yawn::ui::fw2::SessionPanel& m_panel;
};

// ─── ContentGridWrapper ─────────────────────────────────────────────
//
// v1 shim around the fw2::ContentGrid so the grid can live inside the
// v1 rootLayout. Forwards measure/layout/render and all mouse/scroll
// events through the v1↔fw2 bridge exactly like the panel wrappers above.
//
// Cursor-hint queries (wantsHorizontalResize etc.) are delegated to the
// inner fw2::ContentGrid — App.cpp may call them directly on the fw2
// grid pointer it owns alongside this wrapper.

class ContentGridWrapper : public Widget {
public:
    explicit ContentGridWrapper(::yawn::ui::fw2::ContentGrid& grid)
        : m_grid(grid) {}

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::loose(c.maxW, c.maxH);
        auto s = m_grid.measure(fc, v2ctx);
        return c.constrain({s.w, s.h});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_grid.measure(fc, v2ctx);
        m_grid.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                       v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_grid.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_grid.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        ev.clickCount = e.clickCount;   // preserve SDL double-click count
        const bool handled = m_grid.dispatchMouseDown(ev);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_grid.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        m_grid.dispatchMouseMove(ev);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_grid.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        m_grid.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return true;
    }

    bool onScroll(ScrollEvent& e) override {
        ::yawn::ui::fw2::ScrollEvent se{};
        se.x = e.x; se.y = e.y;
        se.dx = e.dx; se.dy = e.dy;
        return m_grid.dispatchScroll(se);
    }

    ::yawn::ui::fw2::ContentGrid& grid() { return m_grid; }

private:
    ::yawn::ui::fw2::ContentGrid& m_grid;
};

} // namespace fw
} // namespace ui
} // namespace yawn
