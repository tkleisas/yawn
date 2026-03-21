#pragma once
// FwGrid — Grid layout container.
//
// Lays out children in a fixed-size cell grid with:
//   - Configurable cell dimensions (width × height)
//   - Maximum rows (fills columns left-to-right, then wraps to next row)
//   - Gap spacing between cells
//   - Internal padding
//
// Layout algorithm (row-major):
//   1. cols = ceil(childCount / maxRows)
//   2. rows = min(childCount, maxRows)
//   3. For child i: row = i / cols, col = i % cols
//   4. Position = padding + (col * (cellW + gapX), row * (cellH + gapY))

#include "Widget.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw {

class FwGrid : public Widget {
public:
    FwGrid() = default;

    // ─── Configuration ──────────────────────────────────────────────────

    void setCellSize(float w, float h) { m_cellW = w; m_cellH = h; }
    void setMaxRows(int r) { m_maxRows = r; }
    void setGap(float gx, float gy) { m_gapX = gx; m_gapY = gy; }
    void setPadding(float p) { m_gridPad = p; }

    float cellWidth()  const { return m_cellW; }
    float cellHeight() const { return m_cellH; }
    int   maxRows()    const { return m_maxRows; }

    // ─── Measure ────────────────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext& ctx) override {
        (void)ctx;
        int n = static_cast<int>(m_children.size());
        if (n == 0) return c.constrain(Size::zero());

        m_rows = std::min(n, m_maxRows);
        m_cols = static_cast<int>(std::ceil(static_cast<float>(n) / m_maxRows));

        float totalW = m_gridPad * 2 + m_cols * m_cellW + std::max(0, m_cols - 1) * m_gapX;
        float totalH = m_gridPad * 2 + m_rows * m_cellH + std::max(0, m_rows - 1) * m_gapY;

        return c.constrain({totalW, totalH});
    }

    // ─── Layout ─────────────────────────────────────────────────────────

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;

        int n = static_cast<int>(m_children.size());
        if (n == 0) return;

        m_rows = std::min(n, m_maxRows);
        m_cols = static_cast<int>(std::ceil(static_cast<float>(n) / m_maxRows));

        for (int i = 0; i < n; ++i) {
            int row = i / m_cols;
            int col = i % m_cols;

            float cellX = m_bounds.x + m_gridPad + col * (m_cellW + m_gapX);
            float cellY = m_bounds.y + m_gridPad + row * (m_cellH + m_gapY);

            m_children[i]->layout({cellX, cellY, m_cellW, m_cellH}, ctx);
        }
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    // Override render to skip Widget's default child iteration — paint()
    // handles children with bounds clipping.
    void render(UIContext& ctx) override {
        if (!m_visible) return;
        paint(ctx);
    }

    void paint(UIContext& ctx) override {
        for (auto* child : children()) {
            if (!child->isVisible()) continue;
            auto& cb = child->bounds();
            if (cb.x + cb.w < m_bounds.x || cb.x > m_bounds.x + m_bounds.w) continue;
            if (cb.y + cb.h < m_bounds.y || cb.y > m_bounds.y + m_bounds.h) continue;
            child->render(ctx);
        }
    }

private:
    float m_cellW   = 64.0f;   // 48 knob + 16 spacing
    float m_cellH   = 70.0f;   // 48 knob + 22 label space
    int   m_maxRows = 2;
    float m_gapX    = 0.0f;
    float m_gapY    = 0.0f;
    float m_gridPad = 8.0f;    // uniform internal padding

    // Computed in measure/layout
    int m_cols = 0;
    int m_rows = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
