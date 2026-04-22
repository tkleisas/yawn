#pragma once

// UI v2 — FwKnob.
//
// Circular value control — the synth/effect parameter workhorse.
// Value-handling mirrors FwFader verbatim (range, step, default,
// ValueChangeSource, DPI-normalized drag, Shift/Ctrl scaling). Paint
// is a 300° arc with a 30° gap at the bottom, indicator line pointing
// from the centre toward the current value.
//
// Bipolar mode (setBipolar(true)) centres the fill on 12 o'clock so
// a symmetric range like [-1, 1] shows the fill extending left of
// centre for negative values and right for positive.
//
// Modulation overlay — setModulatedValue gives the widget a second
// value to show alongside the primary. Common use: primary = user-set
// param value, modulated = LFO-output value going into the audio
// engine. The modulated position draws as a thin secondary indicator
// in a distinct colour so at a glance you can tell "this knob is at
// 0.5 but being pushed to 0.7 right now".
//
// Modes supported by this one class (flags rather than subclasses so
// the DeviceWidget factory can pivot behaviour without swapping types):
//   • addDetent(value, snapRangeFrac) — rolled-in v1 FwDentedKnob. The
//     raw drag position is snapped to the nearest detent when within
//     snapRangeFrac × (max − min) of it. Use for default-value "home
//     feel" on bipolar params (e.g. pan at 0).
//   • setWrapMode(true) — rolled-in v1 FwKnob360. Arc becomes a full
//     360° sweep starting at 12 o'clock; the drag math uses modular
//     arithmetic so rolling past max wraps to min (phase parameters).
//
// Spec-matching items deferred to later:
//   • Accessibility attrs (aria-valuenow etc.).
//
// See docs/widgets/knob.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class FwKnob : public Widget {
public:
    using ValueCallback      = std::function<void(float)>;
    using DragEndCallback    = std::function<void(float startValue, float endValue)>;
    using ClickCallback      = std::function<void()>;
    using RightClickCallback = std::function<void(Point screen)>;
    using ValueFormatter     = std::function<std::string(float)>;

    FwKnob();

    // ─── Value / range ────────────────────────────────────────────
    void setRange(float min, float max);
    void setValue(float v, ValueChangeSource src = ValueChangeSource::Programmatic);
    void setDefaultValue(float v) { m_defaultValue = v; }
    void setStep(float s)         { m_step = s; }
    float value() const           { return m_value; }
    float min()   const           { return m_min; }
    float max()   const           { return m_max; }
    float step()  const           { return m_step; }

    // ─── Bipolar mode ─────────────────────────────────────────────
    // Fill starts at 12 o'clock and extends left (negative) or right
    // (positive). Use for symmetric ranges like pan (-1..+1) or
    // detune cents (-100..+100).
    void setBipolar(bool b);
    bool bipolar() const { return m_bipolar; }

    // ─── Modulation overlay ──────────────────────────────────────
    // Secondary "live" value visualized alongside the primary. Pass
    // std::nullopt to clear. Update cadence is the caller's choice;
    // widget doesn't throttle.
    void setModulatedValue(std::optional<float> v);
    const std::optional<float>& modulatedValue() const { return m_modulatedValue; }

    // Override modulation indicator colour (defaults to
    // palette.modulation).
    void setModulationColor(Color c)           { m_modulationColor = c; }
    void clearModulationColor()                { m_modulationColor.reset(); }
    const std::optional<Color>& modulationColor() const { return m_modulationColor; }

    // ─── Content / display ────────────────────────────────────────
    void setLabel(std::string l);
    const std::string& label() const { return m_label; }
    void setShowLabel(bool b);
    bool showLabel() const       { return m_showLabel; }
    void setShowValue(bool b);
    bool showValue() const       { return m_showValue; }

    // Value formatter — converts float to display string (e.g.
    // "0.75" → "75 %"). Null means "use default 2-decimal format".
    void setValueFormatter(ValueFormatter fn) { m_valueFormatter = std::move(fn); }
    std::string formattedValue() const;

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ValueCallback cb)          { m_onChange = std::move(cb); }
    void setOnDragEnd(DragEndCallback cb)       { m_onDragEnd = std::move(cb); }
    void setOnClick(ClickCallback cb)           { m_onClick = std::move(cb); }
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setAccentColor(Color c) { m_accentOverride = c; }
    void clearAccentColor()      { m_accentOverride.reset(); }
    const std::optional<Color>& accentColor() const { return m_accentOverride; }

    // Knob disc diameter in logical pixels. 0 = auto (use theme
    // metrics). Manual override via setDiameter(px).
    void setDiameter(float px);
    float diameter() const { return m_diameter; }

    // ─── Behaviour ────────────────────────────────────────────────
    void setPixelsPerFullRange(float px) { m_pixelsPerFullRange = px; }
    float pixelsPerFullRange() const     { return m_pixelsPerFullRange; }

    void setFineMode(bool shiftIsFine)   { m_shiftFine = shiftIsFine; }

    // ─── Detents (rolled-in FwDentedKnob behaviour) ───────────────
    // A detent is a "sticky" value the knob snaps to while the raw
    // drag position lies within snapRangeFrac × range of it. Drag
    // far enough past and the knob releases from the detent. Visible
    // effect is a short pause on the value near the detent.
    //
    // Common pattern: bipolar pan (-1..1) gets a detent at 0 so the
    // user can drag through "centre" with a felt bump.
    struct Detent {
        float value;
        float snapRangeFrac;
    };
    void addDetent(float value, float snapRangeFrac = 0.03f);
    void clearDetents()                       { m_detents.clear(); }
    const std::vector<Detent>& detents() const { return m_detents; }

    // ─── Wrap mode (rolled-in FwKnob360 behaviour) ───────────────
    // When true: arc paints a full 360° sweep starting at 12 o'clock,
    // and drag past either end wraps around (modular arithmetic).
    // Use for phase-like parameters with no natural endpoints.
    void setWrapMode(bool w);
    bool wrapMode() const                 { return m_wrapMode; }

    // ─── Inline edit mode ────────────────────────────────────────
    // Double-click opens a text buffer pre-filled with the current
    // value. Host forwards text input + key events while editing;
    // Enter commits (parses float, clamps, fires onChange with
    // ValueChangeSource::User), Escape cancels. While editing, the
    // painter shows the buffer instead of the formatted value.
    void beginEdit();
    void endEdit(bool commit);
    bool isEditing() const              { return m_editing; }
    const std::string& editBuffer() const { return m_editBuffer; }
    // Host feeds text input (SDL_EVENT_TEXT_INPUT → this) while
    // isEditing() is true. Accepts digits, ±, and one decimal point.
    void takeTextInput(const std::string& text);

    // True between drag-start (threshold crossed after mouseDown) and
    // drag-end (mouseUp while dragging). Hosts that paint-time sync
    // their backing value to the knob must guard that sync on this
    // being false — an async engine lagging behind the user's drag
    // would otherwise rubber-band the knob to the stale value.
    bool isDragging() const             { return m_dragging; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Gesture callbacks
    void onDragStart(const DragEvent&) override;
    void onDrag(const DragEvent&)      override;
    void onDragEnd(const DragEvent&)   override;
    void onClick(const ClickEvent&)      override;
    void onDoubleClick(const ClickEvent&) override;
    void onRightClick(const ClickEvent&) override;

    // Raw callback for wheel.
    bool onScroll(ScrollEvent& e)      override;

    // Keyboard — Enter/Escape/Backspace while editing.
    bool onKeyDown(KeyEvent& e)        override;

private:
    // Apply a logical-pixel vertical delta to the value. Positive dy
    // (mouse moved DOWN) DECREASES value; negative (UP) INCREASES.
    // Modifier-aware when m_shiftFine: Shift = ×0.1, Ctrl = ×10.
    void applyVerticalDelta(float logicalDy, uint16_t modifiers);

    // Fire onChange unless src == Automation.
    void fireOnChange(ValueChangeSource src);

    // Clamp helper.
    static float clampVal(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Value / range
    float m_value        = 0.5f;
    float m_min          = 0.0f;
    float m_max          = 1.0f;
    float m_step         = 0.01f;
    std::optional<float> m_defaultValue;

    // Drag state
    float m_dragStartValue = 0.0f;
    bool  m_dragging       = false;
    // When detents are present, m_rawValue tracks the un-snapped drag
    // position so crossing a detent's threshold releases cleanly. Kept
    // in sync with m_value on every setValue() / drag-start so the
    // snap-threshold check always starts from the on-screen value.
    float m_rawValue       = 0.0f;

    // Appearance
    float                 m_diameter  = 0.0f;   // 0 = auto
    bool                  m_bipolar   = false;
    std::optional<Color>  m_accentOverride;

    // Modulation overlay
    std::optional<float>  m_modulatedValue;
    std::optional<Color>  m_modulationColor;

    // Content
    std::string           m_label;
    bool                  m_showLabel = true;
    bool                  m_showValue = true;

    // Behaviour
    float m_pixelsPerFullRange = 200.0f;
    bool  m_shiftFine          = true;
    bool  m_wrapMode           = false;
    std::vector<Detent> m_detents;

    // Callbacks
    ValueCallback      m_onChange;
    DragEndCallback    m_onDragEnd;
    ClickCallback      m_onClick;
    RightClickCallback m_onRightClick;
    ValueFormatter     m_valueFormatter;

    // Inline edit state
    bool        m_editing = false;
    std::string m_editBuffer;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
