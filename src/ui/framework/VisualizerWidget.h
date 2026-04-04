#pragma once
// VisualizerWidget — Oscilloscope, spectrum, and tuner display widget.
//
// Wraps the oscilloscope/spectrum/tuner rendering from DetailPanelWidget
// into a proper fw::Widget with measure/layout/paint lifecycle.

#include "Widget.h"
#include "../Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

class VisualizerWidget : public Widget {
public:
    enum class Mode { Oscilloscope, Spectrum, Tuner };

    explicit VisualizerWidget(Mode mode = Mode::Oscilloscope)
        : m_mode(mode) {
        setName("VisualizerWidget");
    }

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }

    // Feed data to display. Widget copies the data internally.
    void setData(const float* data, int size) {
        if (data && size > 0)
            m_data.assign(data, data + size);
        else
            m_data.clear();
    }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 120.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (m_data.empty()) return;

        auto& renderer = *ctx.renderer;
        float x = m_bounds.x;
        float y = m_bounds.y;
        float w = m_bounds.w;
        float h = m_bounds.h;

        // Background
        renderer.drawRect(x, y, w, h, Color{18, 18, 22, 255});
        renderer.drawRect(x, y, w, 1, Color{50, 50, 60, 255});
        renderer.drawRect(x, y + h - 1, w, 1, Color{50, 50, 60, 255});
        renderer.drawRect(x, y, 1, h, Color{50, 50, 60, 255});
        renderer.drawRect(x + w - 1, y, 1, h, Color{50, 50, 60, 255});

        // Inset the drawing area past the outline
        float ix = x + 2;
        float iy = y + 2;
        float iw = w - 4;
        float ih = h - 4;

        if (m_mode == Mode::Oscilloscope)
            renderOscilloscope(renderer, ix, iy, iw, ih);
        else if (m_mode == Mode::Spectrum)
            renderSpectrum(renderer, ix, iy, iw, ih);
        else
            renderTuner(ctx, ix, iy, iw, ih);
#endif
    }

private:
    Mode m_mode = Mode::Oscilloscope;
    std::vector<float> m_data;

    void renderOscilloscope([[maybe_unused]] Renderer2D& renderer,
                            [[maybe_unused]] float x, [[maybe_unused]] float y,
                            [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        int dataSize = static_cast<int>(m_data.size());
        const float* data = m_data.data();

        float midY = y + h * 0.5f;
        renderer.drawRect(x, midY, w, 1, Color{40, 40, 50, 255});
        renderer.drawRect(x, midY - h * 0.25f, w, 1, Color{30, 30, 38, 255});
        renderer.drawRect(x, midY + h * 0.25f, w, 1, Color{30, 30, 38, 255});

        Color waveColor{80, 220, 120, 255};
        float xStep = w / static_cast<float>(dataSize);
        for (int i = 0; i < dataSize; ++i) {
            float sample = std::clamp(data[i], -1.0f, 1.0f);
            float px = x + i * xStep;
            float py = midY - sample * (h * 0.45f);
            renderer.drawRect(px, py - 1, std::max(xStep, 1.0f), 2, waveColor);
        }
#endif
    }

    void renderSpectrum([[maybe_unused]] Renderer2D& renderer,
                        [[maybe_unused]] float x, [[maybe_unused]] float y,
                        [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        int dataSize = static_cast<int>(m_data.size());
        const float* data = m_data.data();

        for (int db = 1; db <= 3; ++db) {
            float gy = y + h * (1.0f - static_cast<float>(96 - db * 24) / 96.0f);
            renderer.drawRect(x, gy, w, 1, Color{30, 30, 38, 255});
        }
        int numBars = std::min(static_cast<int>(w), dataSize);
        float barW = std::max(w / static_cast<float>(numBars), 1.0f);
        for (int i = 0; i < numBars; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(numBars);
            float logIdx = std::pow(t, 2.0f) * (dataSize - 1);
            int idx = std::clamp(static_cast<int>(logIdx), 0, dataSize - 1);
            float mag = data[idx];
            float barH = mag * (h - 4);
            if (barH < 1.0f) continue;
            float bx = x + i * barW;
            float by = y + h - barH - 2;
            uint8_t r = static_cast<uint8_t>(60 + t * 180);
            uint8_t g = static_cast<uint8_t>(200 - t * 160);
            uint8_t b = 220;
            renderer.drawRect(bx, by, std::max(barW - 1.0f, 1.0f), barH,
                              Color{r, g, b, 230});
        }
#endif
    }

    void renderTuner([[maybe_unused]] UIContext& ctx,
                     [[maybe_unused]] float x, [[maybe_unused]] float y,
                     [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        // Display data layout: [frequency, cents, confidence, midiNote]
        float frequency  = (m_data.size() > 0) ? m_data[0] : 0.0f;
        float cents      = (m_data.size() > 1) ? m_data[1] : 0.0f;
        float confidence = (m_data.size() > 2) ? m_data[2] : 0.0f;
        int   midiNote   = (m_data.size() > 3) ? static_cast<int>(m_data[3]) : -1;

        bool hasPitch = frequency > 0.0f && confidence > 0.3f;
        float centerX = x + w * 0.5f;

        // ─── Note name (large, centered) ───────────────────────────────
        float noteScale = Theme::kFontSize / f.pixelHeight() * 1.8f;
        float smallScale = Theme::kSmallFontSize / f.pixelHeight() * 0.8f;

        if (hasPitch && midiNote >= 0 && midiNote <= 127) {
            static const char* noteNames[] = {
                "C", "C#", "D", "D#", "E", "F",
                "F#", "G", "G#", "A", "A#", "B"
            };
            const char* nn = noteNames[midiNote % 12];
            int octave = (midiNote / 12) - 1;
            char noteBuf[8];
            std::snprintf(noteBuf, sizeof(noteBuf), "%s%d", nn, octave);

            // Color: green when in tune, yellow when close, red when far
            float absCents = std::abs(cents);
            Color noteCol;
            if (absCents < 5.0f)
                noteCol = Color{80, 255, 120, 255};    // green — in tune
            else if (absCents < 15.0f)
                noteCol = Color{255, 230, 80, 255};    // yellow — close
            else
                noteCol = Color{255, 100, 80, 255};    // red — off

            float tw = f.textWidth(noteBuf, noteScale);
            float noteY = y + h * 0.08f;
            f.drawText(r, noteBuf, centerX - tw * 0.5f, noteY, noteScale, noteCol);
        } else {
            // No signal or low confidence
            const char* dash = "--";
            float tw = f.textWidth(dash, noteScale);
            f.drawText(r, dash, centerX - tw * 0.5f, y + h * 0.08f, noteScale,
                        Color{80, 80, 90, 255});
        }

        // ─── Needle gauge ──────────────────────────────────────────────
        float gaugeY = y + h * 0.50f;
        float gaugeW = w * 0.85f;
        float gaugeX = x + (w - gaugeW) * 0.5f;
        float gaugeH = 4.0f;

        // Background track
        r.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, Color{40, 40, 50, 255});

        // Center tick (in-tune marker)
        float centerTickX = gaugeX + gaugeW * 0.5f;
        r.drawRect(centerTickX - 1, gaugeY - 6, 2, gaugeH + 12, Color{80, 255, 120, 180});

        // Tick marks every 10 cents
        for (int c = -50; c <= 50; c += 10) {
            if (c == 0) continue;
            float frac = (c + 50.0f) / 100.0f;
            float tickX = gaugeX + frac * gaugeW;
            float tickH = (c % 25 == 0) ? 8.0f : 5.0f;
            r.drawRect(tickX, gaugeY - tickH * 0.5f + gaugeH * 0.5f,
                        1, tickH, Color{60, 60, 70, 200});
        }

        // Cent labels at edges
        {
            const char* lblL = "-50";
            const char* lblR = "+50";
            float lblScale = Theme::kSmallFontSize / f.pixelHeight() * 0.65f;
            f.drawText(r, lblL, gaugeX - 2, gaugeY + gaugeH + 3, lblScale,
                        Color{80, 80, 90, 200});
            float rwt = f.textWidth(lblR, lblScale);
            f.drawText(r, lblR, gaugeX + gaugeW - rwt + 2, gaugeY + gaugeH + 3,
                        lblScale, Color{80, 80, 90, 200});
        }

        // Needle (triangle indicator)
        if (hasPitch) {
            float clampedCents = std::clamp(cents, -50.0f, 50.0f);
            float needleFrac = (clampedCents + 50.0f) / 100.0f;
            float needleX = gaugeX + needleFrac * gaugeW;

            float absCents = std::abs(cents);
            Color needleCol;
            if (absCents < 5.0f)
                needleCol = Color{80, 255, 120, 255};
            else if (absCents < 15.0f)
                needleCol = Color{255, 230, 80, 255};
            else
                needleCol = Color{255, 100, 80, 255};

            // Upward-pointing triangle needle
            float triW = 8.0f, triH = 10.0f;
            r.drawTriangle(needleX - triW * 0.5f, gaugeY - 4 - triH,
                           needleX + triW * 0.5f, gaugeY - 4 - triH,
                           needleX,               gaugeY - 3, needleCol);

            // Cents text below gauge
            char centsBuf[16];
            if (cents >= 0.0f)
                std::snprintf(centsBuf, sizeof(centsBuf), "+%.1f ct", cents);
            else
                std::snprintf(centsBuf, sizeof(centsBuf), "%.1f ct", cents);
            float ctw = f.textWidth(centsBuf, smallScale);
            f.drawText(r, centsBuf, centerX - ctw * 0.5f, gaugeY + gaugeH + 16,
                        smallScale, Color{160, 160, 170, 220});
        }

        // ─── Frequency display (bottom) ────────────────────────────────
        float bottomY = y + h - 18;
        if (hasPitch) {
            char hzBuf[24];
            std::snprintf(hzBuf, sizeof(hzBuf), "%.1f Hz", frequency);
            float htw = f.textWidth(hzBuf, smallScale);
            f.drawText(r, hzBuf, centerX - htw * 0.5f, bottomY,
                        smallScale, Color{120, 120, 140, 200});
        }

        // ─── Confidence bar (bottom-right corner) ──────────────────────
        if (hasPitch) {
            float barW2 = 40.0f, barH2 = 4.0f;
            float barX2 = x + w - barW2 - 4;
            float barY2 = bottomY + 4;
            r.drawRect(barX2, barY2, barW2, barH2, Color{30, 30, 38, 255});
            float fillW = confidence * barW2;
            Color confCol = (confidence > 0.7f) ? Color{80, 200, 120, 200}
                                                 : Color{200, 160, 60, 200};
            r.drawRect(barX2, barY2, fillW, barH2, confCol);
        }
#endif
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
