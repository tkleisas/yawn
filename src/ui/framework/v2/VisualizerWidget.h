#pragma once
// fw2::VisualizerWidget — Oscilloscope, spectrum, and tuner display.
//
// Migrated from v1 fw::VisualizerWidget. Pure paint widget: no events,
// no gestures. Callers feed samples via setData() each frame. Paint
// picks between oscilloscope / spectrum / tuner layouts depending on
// Mode. Matches AutomationEnvelopeWidget's structure (header-only, guard
// the Renderer.h include under YAWN_TEST_BUILD so the tests target can
// compile without OpenGL).

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
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class VisualizerWidget : public Widget {
public:
    enum class Mode { Oscilloscope, Spectrum, Tuner };

    explicit VisualizerWidget(Mode mode = Mode::Oscilloscope)
        : m_mode(mode) {
        setName("VisualizerWidget");
    }

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const    { return m_mode; }

    void setData(const float* data, int size) {
        if (data && size > 0)
            m_data.assign(data, data + size);
        else
            m_data.clear();
    }

    const std::vector<float>& data() const { return m_data; }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 120.0f});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        if (m_data.empty()) return;

        auto& r = *ctx.renderer;
        const float x = m_bounds.x, y = m_bounds.y;
        const float w = m_bounds.w, h = m_bounds.h;

        // Background
        r.drawRect(x, y, w, h, Color{18, 18, 22, 255});
        r.drawRect(x, y,            w, 1, Color{50, 50, 60, 255});
        r.drawRect(x, y + h - 1,    w, 1, Color{50, 50, 60, 255});
        r.drawRect(x, y,            1, h, Color{50, 50, 60, 255});
        r.drawRect(x + w - 1, y,    1, h, Color{50, 50, 60, 255});

        const float ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;

        if (m_mode == Mode::Oscilloscope)      renderOscilloscope(r, ix, iy, iw, ih);
        else if (m_mode == Mode::Spectrum)     renderSpectrum(r, ix, iy, iw, ih);
        else                                   renderTuner(ctx, ix, iy, iw, ih);
    }
#endif

private:
    Mode               m_mode = Mode::Oscilloscope;
    std::vector<float> m_data;

#ifndef YAWN_TEST_BUILD
    void renderOscilloscope(Renderer2D& r, float x, float y, float w, float h) {
        const int    dataSize = static_cast<int>(m_data.size());
        const float* data     = m_data.data();

        const float midY = y + h * 0.5f;
        r.drawRect(x, midY,             w, 1, Color{40, 40, 50, 255});
        r.drawRect(x, midY - h * 0.25f, w, 1, Color{30, 30, 38, 255});
        r.drawRect(x, midY + h * 0.25f, w, 1, Color{30, 30, 38, 255});

        const Color waveColor{80, 220, 120, 255};
        const float xStep = w / static_cast<float>(dataSize);
        for (int i = 0; i < dataSize; ++i) {
            const float sample = std::clamp(data[i], -1.0f, 1.0f);
            const float px = x + i * xStep;
            const float py = midY - sample * (h * 0.45f);
            r.drawRect(px, py - 1, std::max(xStep, 1.0f), 2, waveColor);
        }
    }

    void renderSpectrum(Renderer2D& r, float x, float y, float w, float h) {
        const int    dataSize = static_cast<int>(m_data.size());
        const float* data     = m_data.data();

        for (int db = 1; db <= 3; ++db) {
            float gy = y + h * (1.0f - static_cast<float>(96 - db * 24) / 96.0f);
            r.drawRect(x, gy, w, 1, Color{30, 30, 38, 255});
        }
        const int   numBars = std::min(static_cast<int>(w), dataSize);
        const float barW    = std::max(w / static_cast<float>(numBars), 1.0f);
        for (int i = 0; i < numBars; ++i) {
            const float t      = static_cast<float>(i) / static_cast<float>(numBars);
            const float logIdx = std::pow(t, 2.0f) * (dataSize - 1);
            const int   idx    = std::clamp(static_cast<int>(logIdx), 0, dataSize - 1);
            const float mag    = data[idx];
            const float barH   = mag * (h - 4);
            if (barH < 1.0f) continue;
            const float bx = x + i * barW;
            const float by = y + h - barH - 2;
            const uint8_t rc = static_cast<uint8_t>(60 + t * 180);
            const uint8_t gc = static_cast<uint8_t>(200 - t * 160);
            const uint8_t bc = 220;
            r.drawRect(bx, by, std::max(barW - 1.0f, 1.0f), barH,
                       Color{rc, gc, bc, 230});
        }
    }

    void renderTuner(UIContext& ctx, float x, float y, float w, float h) {
        auto& r = *ctx.renderer;

        // Display data: [frequency, cents, confidence, midiNote]
        const float frequency  = (m_data.size() > 0) ? m_data[0] : 0.0f;
        const float cents      = (m_data.size() > 1) ? m_data[1] : 0.0f;
        const float confidence = (m_data.size() > 2) ? m_data[2] : 0.0f;
        const int   midiNote   = (m_data.size() > 3) ? static_cast<int>(m_data[3]) : -1;

        const bool  hasPitch = frequency > 0.0f && confidence > 0.3f;
        const float centerX  = x + w * 0.5f;

        // Note name (large, centered)
        const float noteFs  = theme().metrics.fontSize * 1.8f;
        const float smallFs = theme().metrics.fontSizeSmall * 0.8f;

        const auto colorForCents = [](float absCents) -> Color {
            if (absCents < 5.0f)  return Color{ 80, 255, 120, 255};
            if (absCents < 15.0f) return Color{255, 230,  80, 255};
            return                       Color{255, 100,  80, 255};
        };

        if (hasPitch && midiNote >= 0 && midiNote <= 127) {
            static const char* noteNames[] = {
                "C", "C#", "D", "D#", "E", "F",
                "F#", "G", "G#", "A", "A#", "B"
            };
            const char* nn = noteNames[midiNote % 12];
            const int octave = (midiNote / 12) - 1;
            char noteBuf[8];
            std::snprintf(noteBuf, sizeof(noteBuf), "%s%d", nn, octave);

            const Color noteCol = colorForCents(std::abs(cents));
            if (ctx.textMetrics) {
                const float tw = ctx.textMetrics->textWidth(noteBuf, noteFs);
                ctx.textMetrics->drawText(r, noteBuf,
                                           centerX - tw * 0.5f, y + h * 0.08f,
                                           noteFs, noteCol);
            }
        } else {
            const char* dash = "--";
            if (ctx.textMetrics) {
                const float tw = ctx.textMetrics->textWidth(dash, noteFs);
                ctx.textMetrics->drawText(r, dash,
                                           centerX - tw * 0.5f, y + h * 0.08f,
                                           noteFs, Color{80, 80, 90, 255});
            }
        }

        // Needle gauge
        const float gaugeY = y + h * 0.50f;
        const float gaugeW = w * 0.85f;
        const float gaugeX = x + (w - gaugeW) * 0.5f;
        const float gaugeH = 4.0f;

        r.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, Color{40, 40, 50, 255});

        const float centerTickX = gaugeX + gaugeW * 0.5f;
        r.drawRect(centerTickX - 1, gaugeY - 6, 2, gaugeH + 12,
                   Color{80, 255, 120, 180});

        for (int c = -50; c <= 50; c += 10) {
            if (c == 0) continue;
            const float frac = (c + 50.0f) / 100.0f;
            const float tickX = gaugeX + frac * gaugeW;
            const float tickH = (c % 25 == 0) ? 8.0f : 5.0f;
            r.drawRect(tickX, gaugeY - tickH * 0.5f + gaugeH * 0.5f,
                       1, tickH, Color{60, 60, 70, 200});
        }

        // Cent labels
        if (ctx.textMetrics) {
            const float lblFs = theme().metrics.fontSizeSmall * 0.65f;
            ctx.textMetrics->drawText(r, "-50",
                                       gaugeX - 2, gaugeY + gaugeH + 3,
                                       lblFs, Color{80, 80, 90, 200});
            const float rw = ctx.textMetrics->textWidth("+50", lblFs);
            ctx.textMetrics->drawText(r, "+50",
                                       gaugeX + gaugeW - rw + 2,
                                       gaugeY + gaugeH + 3,
                                       lblFs, Color{80, 80, 90, 200});
        }

        // Needle triangle
        if (hasPitch) {
            const float clampedCents = std::clamp(cents, -50.0f, 50.0f);
            const float needleFrac = (clampedCents + 50.0f) / 100.0f;
            const float needleX    = gaugeX + needleFrac * gaugeW;

            const Color needleCol = colorForCents(std::abs(cents));
            const float triW = 8.0f, triH = 10.0f;
            r.drawTriangle(needleX - triW * 0.5f, gaugeY - 4 - triH,
                           needleX + triW * 0.5f, gaugeY - 4 - triH,
                           needleX,               gaugeY - 3, needleCol);

            char centsBuf[16];
            if (cents >= 0.0f)
                std::snprintf(centsBuf, sizeof(centsBuf), "+%.1f ct", cents);
            else
                std::snprintf(centsBuf, sizeof(centsBuf), "%.1f ct", cents);
            if (ctx.textMetrics) {
                const float ctw = ctx.textMetrics->textWidth(centsBuf, smallFs);
                ctx.textMetrics->drawText(r, centsBuf,
                                           centerX - ctw * 0.5f,
                                           gaugeY + gaugeH + 16,
                                           smallFs, Color{160, 160, 170, 220});
            }
        }

        // Frequency readout
        const float bottomY = y + h - 18;
        if (hasPitch && ctx.textMetrics) {
            char hzBuf[24];
            std::snprintf(hzBuf, sizeof(hzBuf), "%.1f Hz", frequency);
            const float htw = ctx.textMetrics->textWidth(hzBuf, smallFs);
            ctx.textMetrics->drawText(r, hzBuf,
                                       centerX - htw * 0.5f, bottomY,
                                       smallFs, Color{120, 120, 140, 200});
        }

        // Confidence bar
        if (hasPitch) {
            const float barW2 = 40.0f, barH2 = 4.0f;
            const float barX2 = x + w - barW2 - 4;
            const float barY2 = bottomY + 4;
            r.drawRect(barX2, barY2, barW2, barH2, Color{30, 30, 38, 255});
            const float fillW = confidence * barW2;
            const Color confCol = (confidence > 0.7f)
                ? Color{ 80, 200, 120, 200}
                : Color{200, 160,  60, 200};
            r.drawRect(barX2, barY2, fillW, barH2, confCol);
        }
    }
#endif
};

} // namespace fw2
} // namespace ui
} // namespace yawn
