#pragma once

// UI v2 — FwCrossfader.
//
// A/B crossfader — a horizontal slider where the value represents
// "% A" (0..100). Drag to change; right-click resets to default;
// clicking anywhere on the track jumps the handle to that position.
// Two end labels ("A" / "B") and two accent colours (one per side)
// let callers tint the fill per audio rail they represent.
//
// Used by the Follow-Action panel for the A/B chance split.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class FwCrossfader : public Widget {
public:
    using ValueCallback      = std::function<void(float)>;
    using DragEndCallback    = std::function<void(float startValue, float endValue)>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwCrossfader();

    // ─── Value (always 0..100, interpreted as "% A") ─────────────
    void  setValue(float v, ValueChangeSource src = ValueChangeSource::Programmatic);
    float value()  const              { return m_value; }        // % A
    float valueB() const              { return 100.0f - m_value; }
    void  setDefaultValue(float d)    { m_defaultValue = d; }

    // ─── Content ──────────────────────────────────────────────────
    void setLabels(std::string a, std::string b);
    const std::string& labelA() const { return m_labelA; }
    const std::string& labelB() const { return m_labelB; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ValueCallback cb)          { m_onChange = std::move(cb); }
    void setOnDragEnd(DragEndCallback cb)       { m_onDragEnd = std::move(cb); }
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setColors(Color a, Color b)      { m_colorA = a; m_colorB = b; }
    Color colorA() const                  { return m_colorA; }
    Color colorB() const                  { return m_colorB; }

    // Padding each end of the track reserves for the A/B labels.
    // Defaults to 20 px. Callers can trim for tight strips.
    void  setTrackPadding(float px)       { m_trackPad = px; invalidate(); }
    float trackPadding() const            { return m_trackPad; }

    // ─── Drag-state accessor ─────────────────────────────────────
    bool isDragging() const              { return m_dragging; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // v1 behaviour: clicking anywhere jumps the handle to that x.
    // We do that by running our own snap-to-click in onMouseDown
    // (returning false so the base's gesture SM still arms for
    // subsequent drag / click / right-click dispatch).
    bool onMouseDown(MouseEvent& e) override;

    void onDragStart(const DragEvent&) override;
    void onDrag(const DragEvent&)      override;
    void onDragEnd(const DragEvent&)   override;
    void onRightClick(const ClickEvent&) override;

private:
    static float clampVal(float v) {
        return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
    }

    // Map a widget-local x coordinate to a value in [0, 100].
    // The crossfader reads left→right as A=100% → A=0%, matching v1.
    float valueForLocalX(float lx) const;

    // State
    float m_value          = 100.0f;    // % A
    float m_defaultValue   = 100.0f;
    float m_dragStartValue = 0.0f;
    bool  m_dragging       = false;

    // Content
    std::string m_labelA = "A";
    std::string m_labelB = "B";

    // Appearance
    Color m_colorA{90, 180, 255, 255};  // blue
    Color m_colorB{255, 160, 40, 255};  // orange
    float m_trackPad = 20.0f;

    // Callbacks
    ValueCallback      m_onChange;
    DragEndCallback    m_onDragEnd;
    RightClickCallback m_onRightClick;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
