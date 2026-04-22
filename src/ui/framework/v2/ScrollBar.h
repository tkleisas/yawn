#pragma once

// UI v2 — FwScrollBar.
//
// Horizontal scrollbar with a draggable thumb. Click on the thumb to
// drag; click elsewhere on the track to jump there; wheel scrolls.
// Thumb size reflects the ratio of viewport width to content width;
// minimum 20 px so tiny thumbs stay grabbable.
//
// This is the v2 replacement for v1 ScrollBar in MixerPanel +
// PianoRollPanel, where it drives the horizontal content scroll of
// each panel's track / note area.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>

namespace yawn {
namespace ui {
namespace fw2 {

class FwScrollBar : public Widget {
public:
    using ScrollCallback = std::function<void(float)>;   // passes new scrollPos

    FwScrollBar();

    // ─── Content / position ──────────────────────────────────────
    void  setContentSize(float size);
    float contentSize() const       { return m_contentSize; }

    void  setScrollPos(float pos);
    float scrollPos() const         { return m_scrollPos; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnScroll(ScrollCallback cb) { m_onScroll = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    // Minimum thumb width in pixels — floors so a thumb on a tiny
    // viewport stays draggable. Default 20 px.
    void  setMinThumbWidth(float w) { m_minThumbW = w; invalidate(); }
    float minThumbWidth() const     { return m_minThumbW; }

    // ─── State accessors (for painter / host) ───────────────────
    bool isDragging() const         { return m_dragging; }

    // Computed thumb metrics (widget-local coordinates). Callers can
    // read these for hit-testing without duplicating the math.
    float thumbWidth() const;
    float thumbX() const;

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Click on thumb starts drag; click on track jumps handle
    // directly. Wheel nudges scrollPos by a few content pixels.
    bool onMouseDown(MouseEvent& e) override;

    void onDrag(const DragEvent&)    override;
    void onDragStart(const DragEvent&) override;
    void onDragEnd(const DragEvent&) override;
    bool onScroll(ScrollEvent& e)    override;

private:
    static float clampVal(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    float maxScroll() const;

    // State
    float m_contentSize = 0.0f;
    float m_scrollPos   = 0.0f;

    // Drag state
    bool  m_dragging         = false;
    float m_dragStartX       = 0.0f;   // screen x where drag began
    float m_dragStartScroll  = 0.0f;   // scroll pos at drag start

    // Appearance
    float m_minThumbW = 20.0f;

    // Callbacks
    ScrollCallback m_onScroll;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
