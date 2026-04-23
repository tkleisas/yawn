#pragma once
// fw2::AutomationEnvelopeWidget — Editable breakpoint envelope.
//
// Draws a list of (time, value) points as draggable circles joined by
// line segments. Click empty area to add a point, drag a point to move
// it, right-click a point to remove it. Used by BrowserPanel's Clip
// tab (visual-clip automation) and DetailPanelWidget's AudioClip view.
//
// Migrated from v1 fw::AutomationEnvelopeWidget (in the old
// InstrumentDisplayWidget.h) to fw2 so the now-fw2 BrowserPanel can
// drop its setV1Context bridge. Paint helpers use ctx.renderer +
// ctx.textMetrics; mouse + move events are the fw2 flavour.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"
// Renderer.h pulls GL via glad; include only in non-test builds where
// render() actually calls into Renderer2D methods.
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class AutomationEnvelopeWidget : public Widget {
public:
    AutomationEnvelopeWidget() { setName("AutoEnvelope"); }

    void setTimeRange(double start, double end) { m_timeStart = start; m_timeEnd = end; }
    void setValueRange(float minV, float maxV)  { m_valueMin = minV; m_valueMax = maxV; }

    void setPoints(const std::vector<std::pair<double,float>>& pts) { m_points = pts; }

    // Callbacks for user interaction.
    using PointCallback  = std::function<void(int idx, double time, float value)>;
    using AddCallback    = std::function<void(double time, float value)>;
    using RemoveCallback = std::function<void(int idx)>;

    void setOnPointMove(PointCallback cb)   { m_onPointMove   = std::move(cb); }
    void setOnPointAdd(AddCallback cb)       { m_onPointAdd    = std::move(cb); }
    void setOnPointRemove(RemoveCallback cb) { m_onPointRemove = std::move(cb); }

    // When true, draws with transparent background for overlay on a
    // waveform / other backing art.
    void setOverlayMode(bool overlay) { m_overlayMode = overlay; }

    int dragIndex() const { return m_dragIdx; }

    // ─── fw2 Widget overrides ───────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({200.0f, 60.0f});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        const float x = m_bounds.x, y = m_bounds.y;
        const float w = m_bounds.w, h = m_bounds.h;

        // Background
        if (m_overlayMode) {
            r.drawRect(x, y, w, h, Color{10, 10, 15, 40});
        } else {
            r.drawRect(x, y, w, h, Color{20, 20, 25, 255});
            r.drawRectOutline(x, y, w, h, Color{50, 50, 55, 255});
        }

        // Vertical grid lines — skipped in overlay mode (the backing
        // widget usually draws its own).
        if (!m_overlayMode) {
            double timeSpan = m_timeEnd - m_timeStart;
            if (timeSpan > 0) {
                double step;
                if (timeSpan > 16)      step = 4.0;
                else if (timeSpan > 8)  step = 2.0;
                else if (timeSpan > 4)  step = 1.0;
                else                    step = 0.5;

                double t = std::ceil(m_timeStart / step) * step;
                while (t < m_timeEnd) {
                    float gx = timeToX(t);
                    r.drawRect(gx, y + 1, 1.0f, h - 2, Color{35, 35, 40, 255});
                    t += step;
                }
            }
        }

        // Centre line.
        float midY = valueToY((m_valueMin + m_valueMax) * 0.5f);
        r.drawRect(x + 1, midY, w - 2, 1.0f,
                   Color{40, 40, 48, m_overlayMode ? uint8_t{60} : uint8_t{255}});

        if (m_points.empty()) {
            if (ctx.textMetrics) {
                const float fs = theme().metrics.fontSizeSmall;
                ctx.textMetrics->drawText(r, "Click to add points",
                                           x + w * 0.3f, y + h * 0.4f, fs,
                                           Color{80, 80, 90, 255});
            }
            return;
        }

        // Connecting lines between points.
        for (size_t i = 0; i + 1 < m_points.size(); ++i) {
            float x0 = timeToX(m_points[i].first);
            float y0 = valueToY(m_points[i].second);
            float x1 = timeToX(m_points[i + 1].first);
            float y1 = valueToY(m_points[i + 1].second);
            r.drawLine(x0, y0, x1, y1, Color{100, 180, 255, 200});
        }

        // Edge extensions (horizontal lines from the first/last point
        // out to the widget edges).
        if (!m_points.empty()) {
            float firstX = timeToX(m_points.front().first);
            float firstY = valueToY(m_points.front().second);
            if (firstX > x + 1)
                r.drawLine(x + 1, firstY, firstX, firstY,
                           Color{100, 180, 255, 100});
            float lastX = timeToX(m_points.back().first);
            float lastY = valueToY(m_points.back().second);
            if (lastX < x + w - 1)
                r.drawLine(lastX, lastY, x + w - 1, lastY,
                           Color{100, 180, 255, 100});
        }

        // Breakpoint handles.
        for (size_t i = 0; i < m_points.size(); ++i) {
            float px = timeToX(m_points[i].first);
            float py = valueToY(m_points[i].second);
            Color col = (static_cast<int>(i) == m_dragIdx)
                ? Color{255, 220, 80, 255} : Color{100, 180, 255, 255};
            r.drawFilledCircle(px, py, kPointRadius,         col, 12);
            r.drawFilledCircle(px, py, kPointRadius - 1.5f,  Color{30, 30, 40, 255}, 12);
            r.drawFilledCircle(px, py, kPointRadius - 2.5f,  col, 12);
        }
    }
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button == MouseButton::Right) {
            int nearest = findNearestPoint(e.x, e.y);
            if (nearest >= 0 && m_onPointRemove) {
                m_onPointRemove(nearest);
                return true;
            }
            return false;
        }
        int hit = findNearestPoint(e.x, e.y);
        if (hit >= 0) {
            m_dragIdx = hit;
            captureMouse();
            return true;
        }
        // Empty area — add a point.
        double t = xToTime(e.x);
        float  v = yToValue(e.y);
        if (m_onPointAdd) m_onPointAdd(t, v);
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_dragIdx >= 0 && m_dragIdx < static_cast<int>(m_points.size())) {
            double t = std::clamp(xToTime(e.x), m_timeStart, m_timeEnd);
            float  v = std::clamp(yToValue(e.y), m_valueMin, m_valueMax);
            m_points[m_dragIdx] = {t, v};
            if (m_onPointMove) m_onPointMove(m_dragIdx, t, v);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_dragIdx >= 0) {
            m_dragIdx = -1;
            releaseMouse();
            return true;
        }
        return false;
    }

private:
    static constexpr float kPointRadius = 4.0f;
    static constexpr float kHitRadius   = 8.0f;

    double m_timeStart = 0.0, m_timeEnd = 4.0;
    float  m_valueMin = 0.0f, m_valueMax = 1.0f;
    std::vector<std::pair<double,float>> m_points;
    int  m_dragIdx      = -1;
    bool m_overlayMode  = false;

    PointCallback  m_onPointMove;
    AddCallback    m_onPointAdd;
    RemoveCallback m_onPointRemove;

    float timeToX(double t) const {
        double span = m_timeEnd - m_timeStart;
        if (span <= 0) return m_bounds.x;
        float frac = static_cast<float>((t - m_timeStart) / span);
        return m_bounds.x + frac * m_bounds.w;
    }
    float valueToY(float v) const {
        float span = m_valueMax - m_valueMin;
        if (span <= 0) return m_bounds.y + m_bounds.h * 0.5f;
        float frac = (v - m_valueMin) / span;
        return m_bounds.y + m_bounds.h - frac * m_bounds.h;  // higher value → higher on screen
    }
    double xToTime(float px) const {
        double span = m_timeEnd - m_timeStart;
        float  frac = (px - m_bounds.x) / std::max(1.0f, m_bounds.w);
        return m_timeStart + frac * span;
    }
    float yToValue(float py) const {
        float span = m_valueMax - m_valueMin;
        float frac = 1.0f - (py - m_bounds.y) / std::max(1.0f, m_bounds.h);
        return m_valueMin + frac * span;
    }

    int findNearestPoint(float mx, float my) const {
        int best = -1;
        float bestDist = kHitRadius * kHitRadius;
        for (size_t i = 0; i < m_points.size(); ++i) {
            float px = timeToX(m_points[i].first);
            float py = valueToY(m_points[i].second);
            float dx = mx - px, dy = my - py;
            float d2 = dx * dx + dy * dy;
            if (d2 < bestDist) { bestDist = d2; best = static_cast<int>(i); }
        }
        return best;
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
