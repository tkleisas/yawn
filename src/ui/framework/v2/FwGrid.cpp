#include "FwGrid.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Configuration
// ───────────────────────────────────────────────────────────────────

void FwGrid::setCellSize(float w, float h) {
    if (w == m_cellW && h == m_cellH) return;
    m_cellW = w;
    m_cellH = h;
    invalidate();
}

void FwGrid::setMaxRows(int rows) {
    rows = std::max(1, rows);
    if (rows == m_maxRows) return;
    m_maxRows = rows;
    invalidate();
}

void FwGrid::setGap(float gx, float gy) {
    if (gx == m_gapX && gy == m_gapY) return;
    m_gapX = gx;
    m_gapY = gy;
    invalidate();
}

void FwGrid::setGridPadding(float px) {
    if (px == m_gridPad) return;
    m_gridPad = px;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Measure / layout
// ───────────────────────────────────────────────────────────────────

Size FwGrid::onMeasure(Constraints c, UIContext& ctx) {
    (void)ctx;
    const int n = static_cast<int>(m_children.size());
    if (n == 0) {
        m_rows = 0;
        m_cols = 0;
        return c.constrain({0.0f, 0.0f});
    }

    m_rows = std::min(n, m_maxRows);
    m_cols = static_cast<int>(std::ceil(static_cast<float>(n)
                                         / static_cast<float>(m_maxRows)));

    const float w = m_gridPad * 2.0f
                  + m_cols * m_cellW
                  + std::max(0, m_cols - 1) * m_gapX;
    const float h = m_gridPad * 2.0f
                  + m_rows * m_cellH
                  + std::max(0, m_rows - 1) * m_gapY;
    return c.constrain({w, h});
}

void FwGrid::onLayout(Rect bounds, UIContext& ctx) {
    const int n = static_cast<int>(m_children.size());
    if (n == 0) { m_rows = 0; m_cols = 0; return; }

    m_rows = std::min(n, m_maxRows);
    m_cols = static_cast<int>(std::ceil(static_cast<float>(n)
                                         / static_cast<float>(m_maxRows)));

    for (int i = 0; i < n; ++i) {
        Widget* child = m_children[i];
        if (!child) continue;
        const int row = i / m_cols;
        const int col = i % m_cols;

        const float cx = bounds.x + m_gridPad + col * (m_cellW + m_gapX);
        const float cy = bounds.y + m_gridPad + row * (m_cellH + m_gapY);
        child->layout(Rect{cx, cy, m_cellW, m_cellH}, ctx);
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
