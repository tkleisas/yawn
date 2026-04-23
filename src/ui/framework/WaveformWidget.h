#pragma once
// fw::WaveformWidget — v1 wrapper around fw2::WaveformWidget.
//
// The real class lives in ui/framework/v2/WaveformWidget.h. This shim
// lets DetailPanelWidget (still v1) own an m_waveformWidget value member
// with unchanged addChild/layout/paint/onMouseDown/onMouseMove/onMouseUp/
// onScroll code paths. When DetailPanelWidget migrates, this shim goes.

#include "Widget.h"
#include "EventSystem.h"
#include "v2/WaveformWidget.h"
#include "v2/UIContext.h"
#include "v2/V1EventBridge.h"

#include "audio/Clip.h"
#include <cstdint>

namespace yawn {
namespace ui {
namespace fw {

class WaveformWidget : public Widget {
public:
    static constexpr float kOverviewH   = ::yawn::ui::fw2::WaveformWidget::kOverviewH;
    static constexpr float kOverviewGap = ::yawn::ui::fw2::WaveformWidget::kOverviewGap;
    static constexpr float kMinZoom     = ::yawn::ui::fw2::WaveformWidget::kMinZoom;
    static constexpr float kMaxZoom     = ::yawn::ui::fw2::WaveformWidget::kMaxZoom;
    static constexpr float kScrollSpeed = ::yawn::ui::fw2::WaveformWidget::kScrollSpeed;
    static constexpr float kZoomFactor  = ::yawn::ui::fw2::WaveformWidget::kZoomFactor;

    WaveformWidget() { setName("WaveformWidget"); }

    // ─── Pass-through configuration ──────────────────────────────────
    void setClip(const audio::Clip* c)        { m_impl.setClip(c); }
    const audio::Clip* clip() const           { return m_impl.clip(); }
    void setPlayPosition(int64_t p)            { m_impl.setPlayPosition(p); }
    void setPlaying(bool p)                    { m_impl.setPlaying(p); }
    void setSampleRate(int sr)                 { m_impl.setSampleRate(sr); }
    void setTransportBPM(double bpm)           { m_impl.setTransportBPM(bpm); }
    bool snapToGrid() const                    { return m_impl.snapToGrid(); }
    void setSnapToGrid(bool s)                 { m_impl.setSnapToGrid(s); }
    void toggleSnapToGrid()                    { m_impl.toggleSnapToGrid(); }
    void resetView()                           { m_impl.resetView(); }
    void fitToWidth()                          { m_impl.fitToWidth(); }
    void zoomIn()                              { m_impl.zoomIn(); }
    void zoomOut()                             { m_impl.zoomOut(); }

    ::yawn::ui::fw2::WaveformWidget& impl()    { return m_impl; }

    // ─── v1 Widget lifecycle (delegate to fw2) ───────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
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

    // ─── v1 event forwarders ────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        const auto& b = m_impl.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_impl.dispatchMouseDown(ev);
        if (handled || ::yawn::ui::fw2::Widget::capturedWidget()) {
            captureMouse();
        }
        return handled;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        const auto& b = m_impl.bounds();
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, b);
        return m_impl.dispatchMouseMove(ev);
    }

    bool onMouseUp(MouseEvent& e) override {
        const auto& b = m_impl.bounds();
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
        const bool handled = m_impl.dispatchMouseUp(ev);
        if (!::yawn::ui::fw2::Widget::capturedWidget()) {
            releaseMouse();
        }
        return handled;
    }

    bool onScroll(ScrollEvent& e) override {
        const auto& b = m_impl.bounds();
        if (e.x < b.x || e.x >= b.x + b.w) return false;
        if (e.y < b.y || e.y >= b.y + b.h) return false;
        auto ev = ::yawn::ui::fw2::toFw2Scroll(e, b);
        return m_impl.dispatchScroll(ev);
    }

private:
    ::yawn::ui::fw2::WaveformWidget m_impl;
};

} // namespace fw
} // namespace ui
} // namespace yawn
