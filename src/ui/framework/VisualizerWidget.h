#pragma once
// VisualizerWidget — Oscilloscope and spectrum display widget.
//
// Wraps the oscilloscope/spectrum rendering from DetailPanelWidget into a
// proper fw::Widget with measure/layout/paint lifecycle.

#include "Widget.h"
#include "../Theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

class VisualizerWidget : public Widget {
public:
    enum class Mode { Oscilloscope, Spectrum };

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
        else
            renderSpectrum(renderer, ix, iy, iw, ih);
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
};

} // namespace fw
} // namespace ui
} // namespace yawn
