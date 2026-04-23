#pragma once
// fw::VisualizerWidget — v1 wrapper around fw2::VisualizerWidget.
//
// The real implementation lives in ui/framework/v2/VisualizerWidget.h.
// This v1 shim exists so DeviceWidget (still v1) can own a
// VisualizerWidget child with its existing addChild/remove/layout/paint
// code path. When DeviceWidget migrates to fw2, it can drop this header
// and reach the fw2 class directly, and this file can be deleted.

#include "Widget.h"
#include "v2/VisualizerWidget.h"
#include "v2/UIContext.h"

namespace yawn {
namespace ui {
namespace fw {

class VisualizerWidget : public Widget {
public:
    using Mode = ::yawn::ui::fw2::VisualizerWidget::Mode;

    explicit VisualizerWidget(Mode mode = Mode::Oscilloscope) : m_impl(mode) {
        setName("VisualizerWidget");
    }

    void setMode(Mode m)                     { m_impl.setMode(m); }
    Mode mode() const                        { return m_impl.mode(); }
    void setData(const float* data, int sz)  { m_impl.setData(data, sz); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 120.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
        // Forward to the fw2 widget so its m_bounds match when render
        // runs. Constraints are tight to the bounds.
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        ::yawn::ui::fw2::Constraints c{bounds.w, bounds.w, bounds.h, bounds.h};
        m_impl.measure(c, v2ctx);
        m_impl.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                       v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_impl.render(v2ctx);
    }

    ::yawn::ui::fw2::VisualizerWidget& impl() { return m_impl; }

private:
    ::yawn::ui::fw2::VisualizerWidget m_impl;
};

} // namespace fw
} // namespace ui
} // namespace yawn
