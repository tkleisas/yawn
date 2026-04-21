#pragma once

// UI v2 — FwFader.
//
// Vertical linear value control. See docs/widgets/fader.md for the
// full spec. This implementation delivers the core behavior; missing
// (documented for follow-up): inline text entry via FwNumberInput +
// LayerStack (requires DropDown overlay infrastructure), modulation
// overlay, paint (rendered by a separate v2 paint path that isn't
// wired to v1's renderer yet).

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>

namespace yawn {
namespace ui {
namespace fw2 {

struct FaderMetrics {
    float trackWidth   = 4.0f;
    float handleWidth  = 20.0f;
    float handleHeight = 10.0f;
    float minWidth     = 20.0f;
    float minHeight    = 80.0f;
    float preferredHeight = 120.0f;
};

class FwFader : public Widget {
public:
    using ValueCallback = std::function<void(float)>;
    using DragEndCallback = std::function<void(float startValue, float endValue)>;
    using ClickCallback = std::function<void()>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwFader();

    // ─── Value / range ────────────────────────────────────────────
    void setRange(float min, float max);
    void setValue(float v, ValueChangeSource src = ValueChangeSource::Programmatic);
    void setDefaultValue(float v)    { m_defaultValue = v; }
    void setStep(float s)            { m_step = s; }
    float value() const              { return m_value; }
    float min()   const              { return m_min; }
    float max()   const              { return m_max; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ValueCallback cb)            { m_onChange = std::move(cb); }
    void setOnDragEnd(DragEndCallback cb)         { m_onDragEnd = std::move(cb); }
    void setOnClick(ClickCallback cb)             { m_onClick = std::move(cb); }
    void setOnRightClick(RightClickCallback cb)   { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setTrackColor(Color c)           { m_trackColorOverride = c; }
    void clearTrackColor()                { m_trackColorOverride.reset(); }
    void setVisualMetrics(FaderMetrics m);

    // ─── Behavior ────────────────────────────────────────────────
    // pixelsPerFullRange: logical pixels of vertical drag to sweep
    // the full [min, max] range. Default 200. Shift during drag
    // engages fine mode (×0.1); Ctrl engages coarse (×10).
    void setPixelsPerFullRange(float px)  { m_pixelsPerFullRange = px; }
    float pixelsPerFullRange() const      { return m_pixelsPerFullRange; }

    void setFineMode(bool shiftIsFine)    { m_shiftFine = shiftIsFine; }
    // When true, Shift during drag = fine; Ctrl = coarse. When false,
    // modifiers are ignored by the Fader (useful if caller wants to
    // reserve modifiers for other purposes).

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect b, UIContext& ctx) override;

    // Gesture callbacks
    void onDragStart(const DragEvent&) override;
    void onDrag(const DragEvent&) override;
    void onDragEnd(const DragEvent&) override;
    void onClick(const ClickEvent&) override;
    void onRightClick(const ClickEvent&) override;

    // Hover feedback
    bool onMouseMove(MouseMoveEvent& e) override;

private:
    // Apply a logical-pixel vertical delta to the value. Positive dy
    // (mouse moved DOWN) decreases value; negative dy (moved UP)
    // increases. Modifier-aware: Shift = ×0.1, Ctrl = ×10.
    void applyVerticalDelta(float logicalDy, uint16_t modifiers);

    // Fire onChange unless src == Automation.
    void fireOnChange(ValueChangeSource src);

    // Clamp helper
    static float clampVal(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Value / range
    float m_value       = 0.7f;
    float m_min         = 0.0f;
    float m_max         = 1.0f;
    float m_step        = 0.01f;
    std::optional<float> m_defaultValue;

    // Drag state
    float m_dragStartValue = 0.0f;

    // Appearance
    FaderMetrics m_metrics;
    std::optional<Color> m_trackColorOverride;

    // Behavior
    float m_pixelsPerFullRange = 200.0f;
    bool  m_shiftFine          = true;

    // Callbacks
    ValueCallback      m_onChange;
    DragEndCallback    m_onDragEnd;
    ClickCallback      m_onClick;
    RightClickCallback m_onRightClick;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
