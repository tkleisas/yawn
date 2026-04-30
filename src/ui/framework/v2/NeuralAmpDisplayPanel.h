#pragma once
// fw2::NeuralAmpDisplayPanel — minimal display panel for the
// Neural Amp device. Mirrors ConvReverbDisplayPanel (filename header
// + Load / Clear buttons) but for .nam model files. Same
// CustomDeviceBody pattern so the device strip sizes to the panel
// rather than the per-param knob count.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/GroupedKnobBody.h"
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

class NeuralAmpDisplayPanel : public CustomDeviceBody {
public:
    NeuralAmpDisplayPanel() {
        setName("NeuralAmpDisplay");
        setAutoCaptureOnUnhandledPress(false);
    }

    float preferredBodyWidth() const override { return 240.0f; }
    void  updateParamValue(int, float) override {}
    void  setOnParamChange(std::function<void(int, float)> cb) override {
        m_onParamChange = std::move(cb);
    }

    void setModelName(const std::string& s) {
        if (m_modelName == s) return;
        m_modelName = s;
        // Reset marquee scroll state when the name changes — otherwise
        // the new (shorter) string can land mid-animation and look
        // garbled. Cheap, fires only on actual change.
        m_marqueeFrame = 0.0f;
    }
    void setLoadedFlag(bool loaded)         { m_loaded = loaded; }
    void setOnLoadRequest(std::function<void()> cb)  { m_onLoadRequest  = std::move(cb); }
    void setOnClearRequest(std::function<void()> cb) { m_onClearRequest = std::move(cb); }

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
        const float fs  = 9.0f * (48.0f / 26.0f);
        const float titleFs = 11.0f * (48.0f / 26.0f);

        if (tm)
            tm->drawText(r, "NAM", m_bounds.x + pad, m_bounds.y + 4,
                          titleFs, Color{180, 180, 200, 255});

        // ── Filename row ──
        // Marquee-scroll long filenames so users with long
        // pelennor-style "<Capturer> <Amp> <Channel> <Settings>.nam"
        // names can read the whole thing without the panel
        // overflowing. Behaviour:
        //   * Text fits → render normally, no animation
        //   * Text overflows → scroll left at ~40 px/sec with a
        //     ~1 s pause at start + end. Render is clipped to the
        //     panel-internal width so nothing escapes the bounds
        //     even mid-scroll.
        // Frame counter is incremented inside render() so the
        // animation runs at the UI frame rate (typically 60 Hz).
        const float nameY = m_bounds.y + 4 + titleFs + 4;
        if (tm) {
            const std::string display = m_modelName.empty()
                ? std::string("(no model loaded)") : m_modelName;
            const Color textC = m_modelName.empty()
                ? Color{120, 120, 140, 220}
                : Color{220, 220, 235, 255};
            const float availW = m_bounds.w - pad * 2;
            const float textW  = tm->textWidth(display, fs);

            r.pushClip(m_bounds.x + pad, nameY,
                        availW, fs + 4);
            float drawX = m_bounds.x + pad;
            if (textW > availW) {
                // Compute marquee offset from frame counter.
                // Phases (in frames at ~60 Hz):
                //   * pause   — kPauseFrames at offset = 0
                //   * scroll  — linear ramp 0 → maxOffset over kScrollFrames
                //   * pause   — kPauseFrames at offset = maxOffset
                //   * snap    — instant return to 0 (start of next loop)
                // 0.66 px/frame ≈ 40 px/sec scroll speed; readable
                // without feeling sluggish on the long pelennor names.
                constexpr float kPauseFrames = 60.0f;
                constexpr float kPxPerFrame  = 0.66f;
                const float maxOffset  = textW - availW + 12.0f;
                const float scrollFr   = maxOffset / kPxPerFrame;
                const float totalFr    = scrollFr + 2.0f * kPauseFrames;
                const float t = std::fmod(m_marqueeFrame, totalFr);
                float offset;
                if (t < kPauseFrames)                       offset = 0.0f;
                else if (t < kPauseFrames + scrollFr)
                    offset = (t - kPauseFrames) * kPxPerFrame;
                else                                         offset = maxOffset;
                drawX -= offset;
                m_marqueeFrame += 1.0f;
            } else {
                m_marqueeFrame = 0.0f;
            }
            tm->drawText(r, display, drawX, nameY, fs, textC);
            r.popClip();
        }

        // "Loaded" / "Not loaded" status indicator.
        if (tm) {
            const char* status = m_loaded ? "● ready" : "○ idle";
            const Color sc = m_loaded ? Color{ 80, 220, 100, 255}
                                       : Color{160, 160, 170, 200};
            tm->drawText(r, status, m_bounds.x + pad, nameY + fs + 4, fs, sc);
        }

        // Button row at the bottom.
        const float btnY = m_bounds.y + m_bounds.h - kBtnH - pad;
        m_loadBtnRect  = { m_bounds.x + pad, btnY,
                           m_bounds.w * 0.62f, kBtnH };
        m_clearBtnRect = { m_loadBtnRect.x + m_loadBtnRect.w + 4, btnY,
                           m_bounds.w - m_loadBtnRect.w - pad * 2 - 4, kBtnH };
        drawButton(r, tm, m_loadBtnRect,  "Load .nam…",
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

    std::string m_modelName;
    bool        m_loaded = false;
    std::function<void(int, float)> m_onParamChange;
    std::function<void()>           m_onLoadRequest;
    std::function<void()>           m_onClearRequest;

    Rect m_loadBtnRect{}, m_clearBtnRect{};
    bool m_loadBtnHover = false, m_clearBtnHover = false;

    // Marquee animation state for long filenames. Reset to 0 on
    // model-name change (setModelName) so a freshly loaded short
    // name doesn't inherit a stale offset from the previous long
    // one. render() is non-const so no mutable needed.
    float m_marqueeFrame = 0.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
