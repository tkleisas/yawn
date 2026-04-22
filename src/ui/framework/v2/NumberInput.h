#pragma once

// UI v2 — FwNumberInput.
//
// Compact horizontal numeric field. Drag vertically to change value
// (sensitivity-scaled); double-click (or keyboard Enter while focused)
// opens inline text editing. Enter commits, Escape cancels, Backspace
// erases one character at a time; digits, "-", and "." are accepted
// via takeTextInput. On focus loss the buffer auto-commits.
//
// v2 successor to v1 FwNumberInput. Used by TransportPanel for the
// BPM / time-signature num / time-signature den fields.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class FwNumberInput : public Widget {
public:
    using ValueCallback   = std::function<void(float)>;
    using DragEndCallback = std::function<void(float startValue, float endValue)>;
    using ValueFormatter  = std::function<std::string(float)>;

    FwNumberInput();

    // ─── Value / range ────────────────────────────────────────────
    void  setValue(float v, ValueChangeSource src = ValueChangeSource::Programmatic);
    float value() const              { return m_value; }

    void  setRange(float mn, float mx);
    float min() const                { return m_min; }
    float max() const                { return m_max; }

    // Optional integer snap — if step > 0, dragged values round to
    // the nearest multiple before clamping. 0 = continuous.
    void  setStep(float s)           { m_step = s; }

    // ─── Display ─────────────────────────────────────────────────
    // printf-style format string (default "%.1f"). Overridden by
    // setValueFormatter when present.
    void  setFormat(std::string fmt);
    const std::string& format() const { return m_format; }

    void  setValueFormatter(ValueFormatter fn) { m_formatter = std::move(fn); }
    std::string formattedValue() const;

    void  setSuffix(std::string s);
    const std::string& suffix() const { return m_suffix; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ValueCallback cb)    { m_onChange = std::move(cb); }
    void setOnDragEnd(DragEndCallback cb) { m_onDragEnd = std::move(cb); }

    // ─── Behaviour ────────────────────────────────────────────────
    // Pixels of vertical drag that sweep the full [min, max] range.
    // Default 200. Shift during drag engages fine (×0.1); Ctrl
    // engages coarse (×10).
    void  setPixelsPerFullRange(float px) { m_pixelsPerFullRange = px; }
    float pixelsPerFullRange() const      { return m_pixelsPerFullRange; }
    // Convenience matching the v1 API — `setSensitivity(s)` maps to
    // `setPixelsPerFullRange(200 / s)` (larger s = faster sweep).
    void  setSensitivity(float s)         { if (s > 0) m_pixelsPerFullRange = 200.0f / s; }

    void  setFineMode(bool shiftIsFine)   { m_shiftFine = shiftIsFine; }

    // ─── Inline edit mode ────────────────────────────────────────
    void beginEdit();
    void endEdit(bool commit);
    bool isEditing() const               { return m_editing; }
    const std::string& editBuffer() const { return m_editBuffer; }
    void takeTextInput(const std::string& text);

    // ─── Drag-state accessor (for paint-time sync guards) ────────
    bool isDragging() const              { return m_dragging; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    void onDragStart(const DragEvent&) override;
    void onDrag(const DragEvent&)      override;
    void onDragEnd(const DragEvent&)   override;
    void onDoubleClick(const ClickEvent&) override;
    bool onKeyDown(KeyEvent& e)        override;

private:
    void applyVerticalDelta(float logicalDy, uint16_t modifiers);
    void fireOnChange(ValueChangeSource src);

    static float clampVal(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Value / range
    float m_value        = 0.0f;
    float m_min          = 0.0f;
    float m_max          = 1000.0f;
    float m_step         = 0.0f;

    // Drag state
    float m_dragStartValue = 0.0f;
    bool  m_dragging       = false;

    // Display
    std::string    m_format = "%.1f";
    std::string    m_suffix;
    ValueFormatter m_formatter;

    // Behaviour
    float m_pixelsPerFullRange = 200.0f;
    bool  m_shiftFine          = true;

    // Callbacks
    ValueCallback   m_onChange;
    DragEndCallback m_onDragEnd;

    // Inline edit state
    bool        m_editing = false;
    std::string m_editBuffer;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
