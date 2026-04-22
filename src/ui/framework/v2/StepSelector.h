#pragma once

// UI v2 — FwStepSelector.
//
// Integer spinner: a label, a numeric display, and ◀ / ▶ arrow regions
// on either side. Click the left arrow → value -= step (clamp or wrap);
// click the right → value += step. Scroll wheel also steps. No drag;
// keyboard navigation pending.
//
// This is the v2 successor to v1 FwStepSelector used by DeviceWidget
// for integer-domain parameters with value labels (e.g. waveform
// indices, filter types). Behaviour mirrors v1; the paint style is
// updated to match v2's theme.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class FwStepSelector : public Widget {
public:
    using ValueCallback  = std::function<void(int)>;
    using FormatCallback = std::function<std::string(int)>;

    FwStepSelector();

    // ─── Value / range ────────────────────────────────────────────
    void setRange(int mn, int mx);
    void setValue(int v, ValueChangeSource src = ValueChangeSource::Programmatic);
    int  value() const                { return m_value; }
    int  min()   const                { return m_min; }
    int  max()   const                { return m_max; }
    void setStep(int s)               { m_step = std::max(1, s); }
    void setWrap(bool w)              { m_wrap = w; }
    bool wrap() const                 { return m_wrap; }

    // ─── Content ──────────────────────────────────────────────────
    void setLabel(std::string l);
    const std::string& label() const  { return m_label; }

    // Display-string formatter. Default is decimal (`std::to_string`).
    // Set a formatter to map integer values to friendly text (e.g.
    // [0..3] → ["Saw", "Square", "Sine", "Noise"]).
    void setFormatter(FormatCallback fn) { m_formatter = std::move(fn); }
    std::string formattedValue() const;

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }

    // ─── Behaviour ────────────────────────────────────────────────
    // Width of each arrow hit-region in logical pixels. Default 16.
    // Arrow width is shared between paint geometry and hit-test so
    // any override here is picked up by both.
    void  setArrowWidth(float w)      { m_arrowWidth = w; invalidate(); }
    float arrowWidth() const          { return m_arrowWidth; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Gesture callbacks. Arrow clicks fire from onClick, which means
    // the whole click-gesture (down + up inside bounds) must complete
    // over one arrow — dragging off cancels, matching FwButton.
    void onClick(const ClickEvent& e) override;
    bool onScroll(ScrollEvent& e)     override;

private:
    // Which arrow was the initial press over (if any). Captured on
    // mouseDown, used by the click handler to pick the step direction.
    enum class ArrowRegion { None, Left, Right };
    ArrowRegion regionAt(float lx, float ly) const;

    // Apply ±step with clamp/wrap rules. Fires onChange with src=User.
    void step(int delta, ValueChangeSource src);

    // State
    int m_value = 0;
    int m_min   = 0;
    int m_max   = 127;
    int m_step  = 1;
    bool m_wrap = false;

    std::string     m_label;
    ValueCallback   m_onChange;
    FormatCallback  m_formatter;

    float m_arrowWidth = 16.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
