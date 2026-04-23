#pragma once
// fw2::LFODisplayWidget — LFO waveform preview with animated phase dot.
// Migrated from v1 fw::LFODisplayWidget. Used as a customPanel on the
// MIDI LFO device strip — shows the shape, depth, phase offset, and a
// live dot tracking the current phase.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yawn {
namespace ui {
namespace fw2 {

class LFODisplayWidget : public Widget {
public:
    LFODisplayWidget() { setName("LFODisplay"); }

    // 0=Sine, 1=Triangle, 2=Saw, 3=Square, 4=S&H
    void setShape(int s)           { m_shape = std::clamp(s, 0, 4); }
    void setDepth(float d)         { m_depth = std::clamp(d, 0.0f, 1.0f); }
    void setBias(float b)          { m_bias = std::clamp(b, -1.0f, 1.0f); }
    void setPhaseOffset(float p)   { m_phaseOffset = std::clamp(p, 0.0f, 1.0f); }
    void setCurrentValue(float v)  { m_currentValue = std::clamp(v, -1.0f, 1.0f); }
    void setCurrentPhase(double p) { m_currentPhase = p - std::floor(p); }
    void setLinked(bool linked)    { m_linked = linked; }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 52.0f});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        const float wx = m_bounds.x + 4;
        const float wy = m_bounds.y + 10;
        const float ww = m_bounds.w - 8;
        const float wh = m_bounds.h - 14;
        if (ww < 10 || wh < 6) return;

        const float midY = wy + wh / 2;
        // Bias line — shifts where the waveform is centred. Depth=0
        // with non-zero bias shows a flat line at the bias level.
        // y-axis range is -2..+2 so that depth=1 + bias=±1 (peak value
        // ±2) still fits inside the widget without clipping.
        const float fullAmp = wh / 4;
        const float biasY   = midY - m_bias * fullAmp;
        // Zero-reference line (dim)
        r.drawRect(wx, midY, ww, 1, Color{35, 35, 45, 255});
        // Bias baseline (brighter if off-centre)
        if (std::abs(m_bias) > 0.001f)
            r.drawRect(wx, biasY, ww, 1, Color{90, 110, 130, 180});

        const uint8_t alpha = static_cast<uint8_t>(100 + 155 * m_depth);
        const Color waveCol{80, 180, 255, alpha};
        const float amp = fullAmp * std::max(m_depth, 0.15f);

        // Clip so the waveform + bias offset can't draw into the label
        // row above nor spill past the widget bottom when amp + |bias|
        // exceeds the drawing area.
        r.pushClip(wx, wy, ww, wh);

        const int steps = static_cast<int>(ww);
        float prevX = wx, prevY = biasY;
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            float phase = t + m_phaseOffset;
            phase = phase - std::floor(phase);
            const float val = computeShape(phase);
            const float curX = wx + static_cast<float>(i);
            const float curY = biasY - val * amp;
            if (i > 0)
                r.drawLine(prevX, prevY, curX, curY, waveCol, 1.5f);
            prevX = curX;
            prevY = curY;
        }

        // Animated phase dot. The waveform at pixel t renders value
        // for phase (t + offset); the dot should sit at pixel
        // currentPhase so it rides the drawn curve — NOT at
        // (currentPhase + offset), which would apply the phase shift
        // twice.
        const float dotX = wx + static_cast<float>(m_currentPhase) * ww;
        float dotPhase   = static_cast<float>(m_currentPhase) + m_phaseOffset;
        dotPhase         = dotPhase - std::floor(dotPhase);
        const float dotVal = computeShape(dotPhase);
        const float dotY   = biasY - dotVal * amp;
        const Color dotCol{255, 200, 60, 255};
        r.drawRect(dotX - 3, dotY - 3, 6, 6, dotCol);

        r.popClip();

        auto* tm = ctx.textMetrics;
        const float lblFs = 7.0f * (48.0f / 26.0f);  // ≈ 12.9 px
        static const char* shapeNames[] = {"Sin", "Tri", "Saw", "Sqr", "S&H"};
        const char* sn = (m_shape >= 0 && m_shape <= 4) ? shapeNames[m_shape] : "?";
        if (tm)
            tm->drawText(r, sn, m_bounds.x + 3, m_bounds.y + 1, lblFs,
                          Color{180, 180, 200, 200});

        if (m_linked && tm) {
            const float tw = tm->textWidth("Link", lblFs);
            tm->drawText(r, "Link",
                          m_bounds.x + m_bounds.w - tw - 3, m_bounds.y + 1,
                          lblFs, Color{100, 220, 140, 200});
        }
    }
#endif

private:
    float computeShape(float phase) const {
        switch (m_shape) {
            case 0: return std::sin(phase * 6.283185307f);
            case 1:
                if (phase < 0.25f) return phase * 4.0f;
                if (phase < 0.75f) return 2.0f - phase * 4.0f;
                return phase * 4.0f - 4.0f;
            case 2: return 2.0f * phase - 1.0f;
            case 3: return (phase < 0.5f) ? 1.0f : -1.0f;
            case 4: {
                const int seg = static_cast<int>(phase * 8.0f);
                const uint32_t hash = static_cast<uint32_t>(seg) * 1664525u + 1013904223u;
                return (static_cast<float>(hash & 0xFFFF) / 32767.5f) - 1.0f;
            }
            default: return 0.0f;
        }
    }

    int    m_shape        = 0;
    float  m_depth        = 0.5f;
    float  m_bias         = 0.0f;
    float  m_phaseOffset  = 0.0f;
    float  m_currentValue = 0.0f;
    double m_currentPhase = 0.0;
    bool   m_linked       = false;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
