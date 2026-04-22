#pragma once

// UI v2 — FwPan.
//
// Horizontal pan control for bipolar [-1, +1] values. Thin track with
// a thumb drawn on top; the filled portion of the track extends from
// the midpoint toward the current value. Drag horizontally to change;
// right-click resets to 0. Used in mixer strips for per-track /
// per-return pan.
//
// Compact — no label, no value readout (the hosting panel draws those
// when it wants to). Sized for a strip: ~60×16 default.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>

namespace yawn {
namespace ui {
namespace fw2 {

class FwPan : public Widget {
public:
    using ValueCallback      = std::function<void(float)>;
    using DragEndCallback    = std::function<void(float startValue, float endValue)>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwPan();

    // ─── Value (always in [-1, +1]) ──────────────────────────────
    void  setValue(float v, ValueChangeSource src = ValueChangeSource::Programmatic);
    float value() const              { return m_value; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ValueCallback cb)          { m_onChange = std::move(cb); }
    void setOnDragEnd(DragEndCallback cb)       { m_onDragEnd = std::move(cb); }
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setThumbColor(Color c)         { m_thumbColorOverride = c; }
    void clearThumbColor()              { m_thumbColorOverride.reset(); }
    const std::optional<Color>& thumbColor() const { return m_thumbColorOverride; }

    // ─── Drag-state accessor (for paint-time sync guards) ────────
    bool isDragging() const             { return m_dragging; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    void onDragStart(const DragEvent&) override;
    void onDrag(const DragEvent&)      override;
    void onDragEnd(const DragEvent&)   override;
    void onRightClick(const ClickEvent& e) override;

private:
    static float clampVal(float v) {
        return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }

    // State
    float m_value          = 0.0f;
    float m_dragStartValue = 0.0f;
    bool  m_dragging       = false;

    std::optional<Color> m_thumbColorOverride;

    // Callbacks
    ValueCallback      m_onChange;
    DragEndCallback    m_onDragEnd;
    RightClickCallback m_onRightClick;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
