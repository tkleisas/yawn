#pragma once
// fw2::ConvReverbDisplayPanel — minimal display panel for the
// Convolution Reverb device.
//
// Shows the loaded IR's filename + a "Load IR..." button + a tiny
// waveform thumbnail so the user can see what got loaded. Button
// click fires onLoadIRRequest, which DetailPanelWidget wires to an
// App-side handler that opens an SDL file dialog, reads the WAV via
// libsndfile, and calls ConvolutionReverb::loadIRMono.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/GroupedKnobBody.h"   // CustomDeviceBody base
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

// Same CustomDeviceBody trick as SplineEQDisplayPanel — keeps the
// device strip from being sized by the per-param knob count.
class ConvReverbDisplayPanel : public CustomDeviceBody {
public:
    ConvReverbDisplayPanel() {
        setName("ConvReverbDisplay");
        setAutoCaptureOnUnhandledPress(false);
    }

    float preferredBodyWidth() const override { return 240.0f; }
    void  updateParamValue(int, float) override {}
    void  setOnParamChange(std::function<void(int, float)> cb) override {
        m_onParamChange = std::move(cb);
    }

    // Filename shown above the button. Empty string = "(no IR
    // loaded)" placeholder.
    void setIRName(const std::string& s) { m_irName = s; }
    void setIRWaveform(const float* data, int length) {
        m_waveData = data; m_waveLen = length;
    }

    // Click-fires callbacks.
    void setOnLoadRequest(std::function<void()> cb) {
        m_onLoadRequest = std::move(cb);
    }
    void setOnClearRequest(std::function<void()> cb) {
        m_onClearRequest = std::move(cb);
    }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({preferredBodyWidth(), c.maxH});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
    bool onMouseDown(MouseEvent&) override { return false; }
#else
    void render(UIContext& ctx) override {
        if (!isVisible() || !ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{18, 18, 24, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        const float pad = 6.0f;
        const float fs  = 9.0f * (48.0f / 26.0f);          // ≈ 16.6 px
        const float titleFs = 11.0f * (48.0f / 26.0f);     // ≈ 20 px

        // Title row: "IMPULSE RESPONSE"
        if (tm)
            tm->drawText(r, "IR", m_bounds.x + pad, m_bounds.y + 4,
                          titleFs, Color{180, 180, 200, 255});

        // Filename area below the title.
        const float nameY = m_bounds.y + 4 + titleFs + 4;
        if (tm) {
            const std::string display = m_irName.empty()
                ? std::string("(no IR loaded)") : m_irName;
            tm->drawText(r, display, m_bounds.x + pad, nameY, fs,
                          m_irName.empty()
                            ? Color{120, 120, 140, 220}
                            : Color{220, 220, 235, 255});
        }

        // Waveform thumbnail strip below the filename.
        const float waveY = nameY + fs + 4;
        const float waveH = std::max(20.0f,
            m_bounds.y + m_bounds.h - waveY - (kBtnH + pad * 2));
        const float waveX = m_bounds.x + pad;
        const float waveW = m_bounds.w - pad * 2;
        if (waveW > 4 && waveH > 4) {
            r.drawRect(waveX, waveY, waveW, waveH,
                       Color{12, 12, 16, 255});
            if (m_waveData && m_waveLen > 1 && waveW > 2) {
                const float midY = waveY + waveH * 0.5f;
                const float halfH = waveH * 0.5f - 1.0f;
                const Color waveCol{120, 200, 255, 220};
                const int cols = static_cast<int>(waveW);
                for (int i = 0; i < cols; ++i) {
                    const int s0 = (i     * m_waveLen) / cols;
                    const int s1 = ((i+1) * m_waveLen) / cols;
                    float minV = 0.0f, maxV = 0.0f;
                    for (int s = s0; s < s1; ++s) {
                        const float v = m_waveData[s];
                        if (v < minV) minV = v;
                        if (v > maxV) maxV = v;
                    }
                    const float top = midY - maxV * halfH;
                    const float bot = midY - minV * halfH;
                    r.drawRect(waveX + i, top, 1,
                                std::max(1.0f, bot - top), waveCol);
                }
            }
        }

        // Button row at the bottom: [Load IR…] [Clear]
        const float btnY = m_bounds.y + m_bounds.h - kBtnH - pad;
        m_loadBtnRect  = { m_bounds.x + pad,  btnY,
                            m_bounds.w * 0.62f, kBtnH };
        m_clearBtnRect = { m_loadBtnRect.x + m_loadBtnRect.w + 4, btnY,
                            m_bounds.w - m_loadBtnRect.w - pad * 2 - 4, kBtnH };
        drawButton(r, tm, m_loadBtnRect,  "Load IR…",
                   m_loadBtnHover);
        drawButton(r, tm, m_clearBtnRect, "Clear",
                   m_clearBtnHover);
    }

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        if (hit(m_loadBtnRect, e)) {
            if (m_onLoadRequest) m_onLoadRequest();
            return true;
        }
        if (hit(m_clearBtnRect, e)) {
            if (m_onClearRequest) m_onClearRequest();
            return true;
        }
        return false;
    }
    bool onMouseMove(MouseMoveEvent& e) override {
        m_loadBtnHover  = hitMove(m_loadBtnRect,  e);
        m_clearBtnHover = hitMove(m_clearBtnRect, e);
        return false;
    }
#endif

private:
#ifndef YAWN_TEST_BUILD
    static constexpr float kBtnH = 22.0f;

    static bool hit(const Rect& r, const MouseEvent& e) {
        return e.x >= r.x && e.x < r.x + r.w &&
               e.y >= r.y && e.y < r.y + r.h;
    }
    static bool hitMove(const Rect& r, const MouseMoveEvent& e) {
        return e.x >= r.x && e.x < r.x + r.w &&
               e.y >= r.y && e.y < r.y + r.h;
    }

    void drawButton(::yawn::ui::Renderer2D& r, TextMetrics* tm,
                    const Rect& rc, const char* label, bool hovered) {
        r.drawRect(rc.x, rc.y, rc.w, rc.h,
                    hovered ? Color{55, 55, 70, 255}
                             : Color{40, 40, 50, 255});
        r.drawRectOutline(rc.x, rc.y, rc.w, rc.h,
                           Color{90, 90, 110, 255});
        if (tm) {
            const float fs = 9.0f * (48.0f / 26.0f);
            const float tw = tm->textWidth(label, fs);
            tm->drawText(r, label,
                          rc.x + (rc.w - tw) * 0.5f,
                          rc.y + (rc.h - fs) * 0.5f - 1.0f,
                          fs, Color{220, 220, 230, 255});
        }
    }
#endif

    std::string m_irName;
    const float* m_waveData = nullptr;
    int          m_waveLen  = 0;

    std::function<void(int, float)> m_onParamChange;
    std::function<void()>           m_onLoadRequest;
    std::function<void()>           m_onClearRequest;

    Rect  m_loadBtnRect{}, m_clearBtnRect{};
    bool  m_loadBtnHover = false, m_clearBtnHover = false;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
