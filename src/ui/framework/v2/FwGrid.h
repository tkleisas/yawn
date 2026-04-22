#pragma once

// UI v2 — FwGrid.
//
// Uniform-cell grid container. Fixed cell size, row-major placement;
// children wrap to a new row once maxRows children have been placed in
// the current column (spec's "cap rows, grow cols" convention matches
// v1 for knob banks).
//
// See docs/widgets/fw_grid.md for the full spec.

#include "Widget.h"

namespace yawn {
namespace ui {
namespace fw2 {

class FwGrid : public Widget {
public:
    FwGrid() = default;

    // ─── Configuration ──────────────────────────────────────────────
    void  setCellSize(float w, float h);
    float cellWidth()  const { return m_cellW; }
    float cellHeight() const { return m_cellH; }

    void setMaxRows(int rows);
    int  maxRows() const   { return m_maxRows; }

    void  setGap(float gx, float gy);
    float gapX() const     { return m_gapX; }
    float gapY() const     { return m_gapY; }

    void  setGridPadding(float px);
    float gridPadding() const { return m_gridPad; }

    // ─── Post-layout accessors ────────────────────────────────────
    int currentRows() const { return m_rows; }
    int currentCols() const { return m_cols; }

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

private:
    // Cell defaults match v1: 48 px knob + 16 px spacing horizontally,
    // 48 px knob + 22 px label space vertically.
    float m_cellW   = 64.0f;
    float m_cellH   = 70.0f;
    int   m_maxRows = 2;
    float m_gapX    = 0.0f;
    float m_gapY    = 0.0f;
    float m_gridPad = 8.0f;

    // Computed in measure / layout.
    int m_cols = 0;
    int m_rows = 0;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
