#pragma once
// fw::DeviceWidget — v1 wrapper around fw2::DeviceWidget.
//
// The real class lives in ui/framework/v2/DeviceWidget.h. This shim
// lets DetailPanelWidget (still v1) keep owning m_deviceWidgets with
// unchanged lifecycle/event code. Two v1-only holdouts remain its own
// responsibility: the `customPanel` (a v1 Widget* passed in via
// setCustomPanel) and the `customBody` (a v1 CustomDeviceBody* from
// InstrumentDisplayWidget). The fw2 impl computes where they go
// during its own layout pass; the wrapper drives their v1 lifecycle
// calls at those rects.
//
// When InstrumentDisplayWidget migrates to fw2, fold custom panels
// into the fw2 class and delete this wrapper.

#include "Widget.h"
#include "EventSystem.h"
#include "DeviceHeaderWidget.h"        // v1 wrapper; re-exposes ToggleCallback etc.
#include "v2/DeviceWidget.h"
#include "v2/UIContext.h"
#include "v2/V1EventBridge.h"
#include "InstrumentDisplayWidget.h"   // v1 CustomDeviceBody

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

class DeviceWidget : public Widget {
public:
    // ─── Type aliases (pass-through to fw2) ─────────────────────────
    using RemoveCallback          = ::yawn::ui::fw2::DeviceWidget::RemoveCallback;
    using ParamChangeCallback     = ::yawn::ui::fw2::DeviceWidget::ParamChangeCallback;
    using ParamTouchCallback      = ::yawn::ui::fw2::DeviceWidget::ParamTouchCallback;
    using ParamRightClickCallback = ::yawn::ui::fw2::DeviceWidget::ParamRightClickCallback;
    using CCLabelCallback         = ::yawn::ui::fw2::DeviceWidget::CCLabelCallback;
    using ParamInfo               = ::yawn::ui::fw2::DeviceWidget::ParamInfo;
    using ParamSlot               = ::yawn::ui::fw2::DeviceWidget::ParamSlot;

    // Layout constants (re-exported for DetailPanelWidget)
    static constexpr float kKnobSize       = ::yawn::ui::fw2::DeviceWidget::kKnobSize;
    static constexpr float kKnobSpacing    = ::yawn::ui::fw2::DeviceWidget::kKnobSpacing;
    static constexpr int   kMaxKnobRows    = ::yawn::ui::fw2::DeviceWidget::kMaxKnobRows;
    static constexpr float kCollapsedW     = ::yawn::ui::fw2::DeviceWidget::kCollapsedW;
    static constexpr float kMinExpandedW   = ::yawn::ui::fw2::DeviceWidget::kMinExpandedW;
    static constexpr float kVisualizerMinW = ::yawn::ui::fw2::DeviceWidget::kVisualizerMinW;
    static constexpr float kDeviceHeaderH  = ::yawn::ui::fw2::DeviceWidget::kDeviceHeaderH;

    DeviceWidget() { setName("DeviceWidget"); }

    ~DeviceWidget() {
        if (m_customPanel) delete m_customPanel;
        delete m_customBody;
    }

    DeviceWidget(const DeviceWidget&) = delete;
    DeviceWidget& operator=(const DeviceWidget&) = delete;

    // ─── Configuration pass-through ─────────────────────────────────

    void setDeviceName(const std::string& n)                { m_impl.setDeviceName(n); }
    void setDeviceType(DeviceHeaderWidget::DeviceType t)    { m_impl.setDeviceType(t); }
    void setRemovable(bool r)                                { m_impl.setRemovable(r); }
    void setVisualizer(bool isViz, const char* vt = "oscilloscope") {
        m_impl.setVisualizer(isViz, vt);
    }

    void setCustomPanel(Widget* panel, float height, float minWidth = 0) {
        if (m_customPanel) { delete m_customPanel; }
        m_customPanel = panel;
        m_impl.setCustomPanelRef(panel, height, minWidth);
    }
    Widget* customPanel() const { return m_customPanel; }

    void setCustomBody(CustomDeviceBody* body) {
        delete m_customBody;
        m_customBody = body;
        m_impl.setCustomBodyRef(body);
        // NOTE: callers wire body->setOnParamChange / setOnParamTouch
        // directly (e.g. DetailPanelWidget uses a DeviceRef lambda for
        // per-device routing). Do NOT overwrite those here.
    }
    CustomDeviceBody* customBody() const { return m_customBody; }

    // ─── State ──────────────────────────────────────────────────────

    bool isExpanded() const                   { return m_impl.isExpanded(); }
    void setExpanded(bool e)                  { m_impl.setExpanded(e); }
    bool isBypassed() const                   { return m_impl.isBypassed(); }
    void setBypassed(bool b)                  { m_impl.setBypassed(b); }

    // ─── Parameters ─────────────────────────────────────────────────

    void setParameters(const std::vector<ParamInfo>& params) { m_impl.setParameters(params); }

    void updateParamValue(int index, float value) {
        if (m_customBody) { m_customBody->updateParamValue(index, value); return; }
        m_impl.updateParamValue(index, value);
    }

    void setVisualizerData(const float* data, int size) { m_impl.setVisualizerData(data, size); }

    // ─── Callbacks ──────────────────────────────────────────────────

    void setOnRemove(RemoveCallback cb)                       { m_impl.setOnRemove(std::move(cb)); }
    void setOnParamChange(ParamChangeCallback cb)              {
        m_onParamChange = cb;
        m_impl.setOnParamChange(std::move(cb));
    }
    void setOnParamTouch(ParamTouchCallback cb)                {
        m_onParamTouch = cb;
        m_impl.setOnParamTouch(std::move(cb));
    }
    void setOnParamRightClick(ParamRightClickCallback cb)      { m_impl.setOnParamRightClick(std::move(cb)); }
    void setCCLabelCallback(CCLabelCallback cb)                { m_impl.setCCLabelCallback(std::move(cb)); }
    void setOnBypassToggle(DeviceHeaderWidget::ToggleCallback cb) { m_impl.setOnBypassToggle(std::move(cb)); }
    void setOnExpandToggle(DeviceHeaderWidget::ToggleCallback cb) { m_impl.setOnExpandToggle(std::move(cb)); }
    void setOnDragStart(DeviceHeaderWidget::ActionCallback cb)    { m_impl.setOnDragStart(std::move(cb)); }
    void setOnPresetClick(DeviceHeaderWidget::PresetClickCallback cb) { m_impl.setOnPresetClick(std::move(cb)); }
    void setPresetName(const std::string& name)                { m_impl.setPresetName(name); }

    // ─── Knob edit forwarding ───────────────────────────────────────

    bool hasEditingKnob() const {
        if (m_customBody) return m_customBody->hasEditingKnob();
        return m_impl.hasEditingKnob();
    }

    bool forwardKeyDown(int key) {
        if (m_customBody) return m_customBody->forwardKeyDown(key);
        return m_impl.forwardKeyDown(key);
    }

    bool forwardTextInput(const char* text) {
        if (m_customBody) return m_customBody->forwardTextInput(text);
        return m_impl.forwardTextInput(text);
    }

    void cancelEditingKnobs() {
        if (m_customBody) { m_customBody->cancelEditingKnobs(); return; }
        m_impl.cancelEditingKnobs();
    }

    // ─── Width calculation ──────────────────────────────────────────

    float preferredWidth() const {
        if (!m_impl.isExpanded()) return kCollapsedW;
        if (m_customBody) {
            const float w = m_customBody->preferredBodyWidth() + 16.0f;
            return std::max(kMinExpandedW, w);
        }
        return m_impl.preferredWidth();
    }

    // ─── v1 Widget lifecycle (delegate to fw2 + v1 custom children) ─

    Size measure(const Constraints& c, const UIContext&) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        ::yawn::ui::fw2::Constraints fc{c.minW, c.minH, c.maxW, c.maxH};
        auto s = m_impl.measure(fc, v2ctx);
        // fw2::DeviceWidget can't know the v1 customBody's preferred
        // width (it's a v1-only type); substitute ours here so the
        // wrapper advertises the real size to its parent.
        const float w = std::min(preferredWidth(), c.maxW);
        return c.constrain({w, s.h});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        auto fc = ::yawn::ui::fw2::Constraints::tight(bounds.w, bounds.h);
        m_impl.measure(fc, v2ctx);
        m_impl.layout(::yawn::ui::fw2::Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                       v2ctx);

        if (m_customPanel) {
            const auto r = m_impl.customPanelLayoutRect();
            m_customPanel->layout({r.x, r.y, r.w, r.h}, ctx);
        }
        if (m_customBody) {
            const auto r = m_impl.customBodyLayoutRect();
            m_customBody->layout({r.x, r.y, r.w, r.h}, ctx);
        }
    }

    void paint(UIContext& ctx) override {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_impl.render(v2ctx);
        if (m_impl.isExpanded()) {
            if (m_customPanel) m_customPanel->paint(ctx);
            if (m_customBody)  m_customBody->paint(ctx);
        }
    }

    // ─── v1 event forwarders ────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_impl.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;

        // customBody gets first dibs on its own area — forwarding to
        // fw2 first lets fw2's gesture state machine auto-capture the
        // DeviceWidget (because our fw2 onMouseDown returns false over
        // the customBody region), which would starve the v1 body for
        // the rest of the drag.
        if (m_customBody && m_customBody->bounds().contains(e.x, e.y)) {
            if (m_customBody->onMouseDown(e)) {
                captureMouse();
                m_draggingCustomBody = true;
                return true;
            }
        }

        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_impl.dispatchMouseDown(ev);
        // Only capture if fw2 actually has a drag target (gesture SM
        // captured during dispatch). For one-shot actions — button
        // clicks, header drag-start callbacks, preset menu triggers —
        // we return handled without capturing so DetailPanelWidget can
        // receive follow-up moves for its own drag-reorder state and
        // pop-up menus can take focus.
        if (::yawn::ui::fw2::Widget::capturedWidget())
            captureMouse();
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        // If customBody is mid-drag, route directly to it — fw2
        // capture never involved, so dispatchMouseMove would return
        // false.
        if (m_draggingCustomBody && m_customBody)
            return m_customBody->onMouseMove(e);

        const auto& b = m_impl.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        if (m_impl.dispatchMouseMove(ev)) return true;
        if (m_customBody) return m_customBody->onMouseMove(e);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_draggingCustomBody && m_customBody) {
            m_customBody->onMouseUp(e);
            m_draggingCustomBody = false;
            releaseMouse();
            return true;
        }

        const auto& b = m_impl.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_impl.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) releaseMouse();
        if (handled) return true;
        if (m_customBody) return m_customBody->onMouseUp(e);
        return false;
    }

    // ─── Value formatting (expose for tests / external use) ─────────

    static std::string formatValue(float value, const std::string& unit, bool isBoolean) {
        return ::yawn::ui::fw2::DeviceWidget::formatValue(value, unit, isBoolean);
    }

    ::yawn::ui::fw2::DeviceWidget& impl() { return m_impl; }

private:
    ::yawn::ui::fw2::DeviceWidget m_impl;

    // v1-only children (this wrapper owns them)
    Widget*           m_customPanel = nullptr;
    CustomDeviceBody* m_customBody  = nullptr;

    // Shadow copies of param callbacks so we can forward through custom
    // body wiring.
    ParamChangeCallback m_onParamChange;
    ParamTouchCallback  m_onParamTouch;

    // Tracks whether the current drag landed on the v1 customBody so
    // follow-up move/up calls route there (v1 customBody doesn't use
    // the fw2 gesture SM, so fw2 capture state wouldn't tell us).
    bool m_draggingCustomBody = false;
};

} // namespace fw
} // namespace ui
} // namespace yawn
