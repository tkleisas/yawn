#pragma once

// UI v2 — SplitView.
//
// Two-pane resizable splitter. A draggable divider sits between a
// "first" and "second" child; the user resizes by dragging it, or the
// caller programmatically sets a ratio / fixed pane size.
//
// This first-pass implementation ships the spec's core contract:
//   • Horizontal / vertical orientation
//   • Three split modes: Ratio, FixedStart, FixedEnd
//   • Per-pane min/max constraints
//   • Mouse drag on divider (via gesture SM drag callbacks)
//   • onDividerMoved callback
//
// Deferred (per spec Open Questions): collapse/expand animation,
// double-click-to-collapse, keyboard arrow-key resize, snap points,
// grip dots, right-click context menu.
//
// See docs/widgets/split_view.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>

namespace yawn {
namespace ui {
namespace fw2 {

enum class SplitOrientation {
    Horizontal,  // vertical divider, left | right
    Vertical,    // horizontal divider, top / bottom
};

enum class SplitPane {
    First,       // left or top
    Second,      // right or bottom
};

enum class SplitMode {
    Ratio,       // ratio of first pane to total main-axis size
    FixedStart,  // first pane has a fixed absolute size
    FixedEnd,    // second pane has a fixed absolute size
};

struct SplitConstraints {
    float minSize = 40.0f;
    float maxSize = 1.0e9f;   // effectively unbounded
};

class SplitView : public Widget {
public:
    using PositionCallback = std::function<void(float)>;

    SplitView();
    explicit SplitView(SplitOrientation o);

    // ─── Panes ────────────────────────────────────────────────────
    // Caller keeps ownership. Passing nullptr removes the pane.
    void    setFirst(Widget* pane);
    void    setSecond(Widget* pane);
    Widget* first()  const { return m_first;  }
    Widget* second() const { return m_second; }

    // ─── Orientation ──────────────────────────────────────────────
    void setOrientation(SplitOrientation o);
    SplitOrientation orientation() const { return m_orientation; }

    // ─── Divider position / mode ─────────────────────────────────
    // Choose one. Each setter switches the mode + stores the value.
    void setRatio(float r);
    void setFirstPaneSize(float px);
    void setSecondPaneSize(float px);

    SplitMode splitMode() const { return m_mode; }
    float dividerRatio()   const;   // always returns a ratio (0..1)
    float firstPaneSize()  const;   // derived from last layout
    float secondPaneSize() const;

    // ─── Pane constraints ────────────────────────────────────────
    void setConstraints(SplitPane pane, SplitConstraints c);
    SplitConstraints constraints(SplitPane pane) const;

    // ─── Divider appearance ──────────────────────────────────────
    void  setDividerThickness(float px);
    float dividerThickness() const;

    void setDividerColor(Color c)       { m_dividerColorOverride = c; }
    void clearDividerColor()            { m_dividerColorOverride.reset(); }
    const std::optional<Color>& dividerColor() const { return m_dividerColorOverride; }

    // ─── Callbacks ───────────────────────────────────────────────
    // Fires during drag (continuous) with whatever the underlying
    // storage is (ratio for Ratio mode, px for Fixed*). Callers can
    // cross-check via splitMode().
    void setOnDividerMoved(PositionCallback cb) { m_onMoved = std::move(cb); }

    // ─── Paint-side accessors (for Fw2Painters) ──────────────────
    Rect dividerRect()    const { return m_divider; }
    bool isDragging()     const { return m_dragging; }
    bool isDividerHover() const { return m_hoverDivider; }

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

    // Gesture SM — drag-start inside divider starts the resize.
    bool onMouseDown(MouseEvent& e)    override;
    void onDragStart(const DragEvent& e) override;
    void onDrag(const DragEvent& e)      override;
    void onDragEnd(const DragEvent& e)   override;
    bool onMouseMove(MouseMoveEvent& e) override;

private:
    // Compute current first-pane size along the main axis, given the
    // available space (main-axis minus divider thickness). Clamps to
    // both panes' constraints.
    float computeFirstSize(float available) const;
    void  commitFirstSize(float firstSize, float available);
    void  fireMoved();

    // State
    Widget* m_first  = nullptr;
    Widget* m_second = nullptr;

    SplitOrientation m_orientation = SplitOrientation::Horizontal;
    SplitMode        m_mode        = SplitMode::Ratio;
    float            m_ratio       = 0.5f;   // Ratio mode
    float            m_fixedPx     = 200.0f; // FixedStart / FixedEnd modes

    SplitConstraints m_firstC{};
    SplitConstraints m_secondC{};

    float                 m_dividerThickness = 0.0f;   // 0 = theme default
    std::optional<Color>  m_dividerColorOverride;

    // Layout output / drag state
    Rect    m_divider{};
    float   m_actualFirstSize = 0.0f;
    bool    m_pressedOnDivider = false;
    bool    m_dragging         = false;
    bool    m_hoverDivider     = false;
    float   m_dragStartFirstSize = 0.0f;

    PositionCallback m_onMoved;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
