#pragma once

// UI v2 — FlexBox.
//
// 1D layout container — rows or columns of children with distribution
// (justify) and cross-axis alignment (align). Ported from v1's FlexBox
// to the v2 Widget base with its caching model. Child measure/layout
// go through the cached wrappers, so repeated layout of a stable tree
// is near-free.
//
// See docs/widgets/flex_box.md for the full spec.

#include "Widget.h"

#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// Direction / Justify / Align share semantics with v1 — reuse the enums.
using ::yawn::ui::fw::Direction;
using ::yawn::ui::fw::Justify;
using ::yawn::ui::fw::Align;

class FlexBox : public Widget {
public:
    FlexBox() {
        setAutoCaptureOnUnhandledPress(false);
        // Container widget — its measured size depends on children, so
        // child invalidations MUST bubble through. The default auto-
        // boundary heuristic ("flexWeight=0 ⇒ boundary") is wrong for
        // containers; force it off. Symptom this fixes: a child whose
        // visibility flips from off to on doesn't appear / appears at
        // (0,0) because the FlexBox's measure cache stayed valid.
        setRelayoutBoundary(false);
    }
    explicit FlexBox(Direction dir) : m_direction(dir) {
        setAutoCaptureOnUnhandledPress(false);
        setRelayoutBoundary(false);
    }

    // ─── Configuration ────────────────────────────────────────────
    Direction direction() const     { return m_direction; }
    void setDirection(Direction d);

    Justify justify() const         { return m_justify; }
    void setJustify(Justify j);

    Align align() const             { return m_align; }
    void setAlign(Align a);

    float gap() const               { return m_gap; }
    void  setGap(float g);

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

    // ─── Mouse dispatch (own-dispatch container) ──────────────────
    // FlexBox is a hierarchy container — clicks inside route to the
    // child whose bounds contain the point, drag-tracking forwards to
    // the captured descendant. Same pattern as ContentGrid; opt out of
    // the gesture SM auto-capture (set in ctor) so dead-space clicks
    // don't end up captured by the FlexBox itself.
    bool onMouseDown(MouseEvent& e)     override;
    bool onMouseMove(MouseMoveEvent& e) override;
    bool onMouseUp(MouseEvent& e)       override;
    bool onScroll(ScrollEvent& e)       override;

private:
    Direction m_direction = Direction::Row;
    Justify   m_justify   = Justify::Start;
    Align     m_align     = Align::Stretch;
    float     m_gap       = 0.0f;

    // Per-measure child sizes, cached between onMeasure and onLayout
    // within the same frame. Child measure is itself cached at the
    // child's own MeasureCache, so re-measuring during onLayout is
    // cheap even if we didn't cache these sizes here — but storing
    // them avoids re-looking-up the same Constraints.
    std::vector<Size> m_childSizes;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
