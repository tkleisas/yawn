#pragma once
// fw2::FilterDisplayWidget — filter frequency response curve.
// Migrated from v1 fw::FilterDisplayWidget. Used as a customPanel on
// the Filter audio effect strip — shows the approximate magnitude
// response for LP / HP / BP / Notch given cutoff + resonance.

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
#include <cstdio>

namespace yawn {
namespace ui {
namespace fw2 {

class FilterDisplayWidget : public Widget {
public:
    FilterDisplayWidget() { setName("FilterDisplay"); }

    void setCutoff(float hz) { m_cutoff = std::clamp(hz, 20.0f, 20000.0f); }
    void setResonance(float q) { m_resonance = std::clamp(q, 0.0f, 1.0f); }
    // 0=LP, 1=HP, 2=BP, 3=Notch
    void setFilterType(int type) { m_type = std::clamp(type, 0, 3); }

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

        const float lblFs = 7.0f * (48.0f / 26.0f);  // ≈ 12.9 px
        static const char* typeNames[] = {"LP", "HP", "BP", "Notch"};
        auto* tm = ctx.textMetrics;
        if (tm)
            tm->drawText(r, typeNames[m_type], m_bounds.x + 3, m_bounds.y + 1,
                          lblFs, Color{180, 180, 200, 200});

        const float px = m_bounds.x + 4;
        const float py = m_bounds.y + 11;
        const float pw = m_bounds.w - 8;
        const float ph = m_bounds.h - 14;
        if (pw < 10 || ph < 6) return;

        // Log-scale frequency: 20Hz→20kHz mapped to 0..1
        const float cutoffNorm = std::log(m_cutoff / 20.0f) / std::log(1000.0f);

        r.pushClip(px, py, pw, ph);

        const Color lineCol{255, 160, 40, 220};
        const int steps = static_cast<int>(pw);
        float prevX = px, prevY = py + ph;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            const float dist = t - cutoffNorm;
            const float slope = 7.0f + m_resonance * 12.0f;
            float mag = 0.0f;

            switch (m_type) {
                case 0: // LP
                    mag = 1.0f / (1.0f + std::exp(dist * slope));
                    if (m_resonance > 0.05f)
                        mag += m_resonance * 0.6f * std::exp(-dist * dist * 60.0f);
                    break;
                case 1: // HP
                    mag = 1.0f / (1.0f + std::exp(-dist * slope));
                    if (m_resonance > 0.05f)
                        mag += m_resonance * 0.6f * std::exp(-dist * dist * 60.0f);
                    break;
                case 2: { // BP
                    float bw = 0.12f - m_resonance * 0.08f;
                    if (bw < 0.02f) bw = 0.02f;
                    mag = std::exp(-dist * dist / (2 * bw * bw));
                    break;
                }
                case 3: { // Notch
                    float bw = 0.12f - m_resonance * 0.08f;
                    if (bw < 0.02f) bw = 0.02f;
                    mag = 1.0f - std::exp(-dist * dist / (2 * bw * bw));
                    break;
                }
            }
            mag = std::clamp(mag, 0.0f, 1.3f);
            const float curX = px + static_cast<float>(i);
            const float curY = py + ph * (1.0f - mag * 0.75f);
            if (i > 0)
                r.drawLine(prevX, prevY, curX, curY, lineCol, 1.5f);
            prevX = curX;
            prevY = curY;
        }

        const float cutoffX = px + cutoffNorm * pw;
        if (cutoffX >= px && cutoffX <= px + pw)
            r.drawRect(cutoffX, py, 1, ph, Color{255, 160, 40, 60});

        r.popClip();

        char hzBuf[16];
        if (m_cutoff >= 1000.0f) std::snprintf(hzBuf, sizeof(hzBuf), "%.1fk", m_cutoff / 1000.0f);
        else                      std::snprintf(hzBuf, sizeof(hzBuf), "%.0f",  m_cutoff);
        if (tm) {
            const float tw = tm->textWidth(hzBuf, lblFs);
            tm->drawText(r, hzBuf,
                          m_bounds.x + m_bounds.w - tw - 3, m_bounds.y + 1,
                          lblFs, Color{255, 160, 40, 200});
        }
    }
#endif

private:
    float m_cutoff    = 5000.0f;
    float m_resonance = 0.0f;
    int   m_type      = 0;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
