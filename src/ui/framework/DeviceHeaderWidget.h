#pragma once
// fw::DeviceHeaderWidget — v1 wrapper around fw2::DeviceHeaderWidget.
//
// The real class lives in ui/framework/v2/DeviceHeaderWidget.h. This
// shim lets DeviceWidget (still v1) keep owning an m_header value member
// with unchanged addChild/layout/paint/onMouseDown/onMouseUp code paths.
// Removed when DeviceWidget migrates to fw2.

#include "Widget.h"
#include "EventSystem.h"
#include "v2/DeviceHeaderWidget.h"
#include "v2/UIContext.h"
#include "v2/V1EventBridge.h"

#include <functional>
#include <string>
#include <utility>

namespace yawn {
namespace ui {
namespace fw {

class DeviceHeaderWidget : public Widget {
public:
    using ActionCallback      = ::yawn::ui::fw2::DeviceHeaderWidget::ActionCallback;
    using ToggleCallback      = ::yawn::ui::fw2::DeviceHeaderWidget::ToggleCallback;
    using PresetClickCallback = ::yawn::ui::fw2::DeviceHeaderWidget::PresetClickCallback;
    using DeviceType          = ::yawn::ui::fw2::DeviceHeaderWidget::DeviceType;

    static constexpr float kHeaderH = ::yawn::ui::fw2::DeviceHeaderWidget::kHeaderH;
    static constexpr float kBtnSize = ::yawn::ui::fw2::DeviceHeaderWidget::kBtnSize;
    static constexpr float kBypassW = ::yawn::ui::fw2::DeviceHeaderWidget::kBypassW;
    static constexpr float kStripeH = ::yawn::ui::fw2::DeviceHeaderWidget::kStripeH;

    DeviceHeaderWidget() { setName("DeviceHeaderWidget"); }

    // ─── Configuration (pass-through) ───────────────────────────────────
    void setDeviceName(const std::string& n) { m_impl.setDeviceName(n); }
    void setDeviceType(DeviceType t)          { m_impl.setDeviceType(t); }
    void setBypassed(bool b)                   { m_impl.setBypassed(b); }
    bool isBypassed() const                    { return m_impl.isBypassed(); }
    void setExpanded(bool e)                   { m_impl.setExpanded(e); }
    bool isExpanded() const                    { return m_impl.isExpanded(); }
    void setRemovable(bool r)                  { m_impl.setRemovable(r); }
    void setPresetName(const std::string& n)   { m_impl.setPresetName(n); }

    void setOnExpandToggle(ToggleCallback cb)      { m_impl.setOnExpandToggle(std::move(cb)); }
    void setOnBypassToggle(ToggleCallback cb)      { m_impl.setOnBypassToggle(std::move(cb)); }
    void setOnRemove(ActionCallback cb)             { m_impl.setOnRemove(std::move(cb)); }
    void setOnDragStart(ActionCallback cb)          { m_impl.setOnDragStart(std::move(cb)); }
    void setOnPresetClick(PresetClickCallback cb)   { m_impl.setOnPresetClick(std::move(cb)); }

    Color deviceColor() const { return m_impl.deviceColor(); }

    ::yawn::ui::fw2::DeviceHeaderWidget& impl() { return m_impl; }

    // ─── Lifecycle (delegate to fw2 via v1 interface) ───────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, kHeaderH});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        ::yawn::ui::fw2::Constraints fc{bounds.w, bounds.w, bounds.h, bounds.h};
        m_impl.measure(fc, v2ctx);
        m_impl.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                       v2ctx);
    }

    void paint(UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_impl.render(v2ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_impl.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        return m_impl.dispatchMouseDown(ev);
    }

private:
    ::yawn::ui::fw2::DeviceHeaderWidget m_impl;
};

} // namespace fw
} // namespace ui
} // namespace yawn
