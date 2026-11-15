#pragma once

// PolivoksPanel — the YAWN device editor for the Polivoks 1982 instrument.
//
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 bokontep
//
// A CustomDeviceBody: a live Polivoks-styled display on the left and grouped
// knob / switch / step-selector sections on the right. The controls mirror the
// original panel modules: PROGRAM, MASTER, VCO I/II, MIXER, FILTER, FILT ENV,
// AMP, LFO and MORE. The display previews the two oscillator waves, the filter,
// the LFO and both envelopes.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/GroupedKnobBody.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/StepSelector.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/Theme.h"
#include "instruments/PolivoksSynth.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// ─────────────────────────────────────────────────────────────────────
// PolivoksDisplayWidget — live module display.
// ─────────────────────────────────────────────────────────────────────
class PolivoksDisplayWidget : public Widget
{
public:
    struct State
    {
        int   vco1Wave = 1, vco1Oct = 0;
        float vco1Level = 0.5f;
        int   vco2Wave = 1, vco2Oct = 2;
        float vco2Level = 0.0f, vco2Detune = 0.0f;
        float xmod = 0.0f, noise = 0.0f;
        float cutoffHz = 10000.0f, resonance = 0.0f;
        float envAmount = 0.0f, lfoVcf = 0.0f;
        int   filterMode = 0;
        float fA = 0.0f, fD = 0.55f, fS = 1.0f, fR = 0.0f;
        bool  fLoop = false;
        float aA = 0.0f, aD = 0.55f, aS = 1.0f, aR = 0.0f;
        bool  aLoop = false, gate = false;
        int   lfoShape = 0;
        float lfoRate = 5.0f, lfoVca = 0.0f;
        int   voiceMode = 1;
        const char* programName = "";
    };

    PolivoksDisplayWidget() { setName("PolivoksDisplay"); }

    void setState(const State& state) { m_state = state; }

    Size onMeasure(Constraints c, UIContext&) override
    {
        return c.constrain({330.0f, 168.0f});
    }

#ifndef YAWN_TEST_BUILD
    void render(UIContext& ctx) override;
#endif

    const State& state() const { return m_state; }

private:
    State m_state;
};

#ifndef YAWN_TEST_BUILD
namespace polivoks_ui {

inline void text(UIContext& ctx, Renderer2D& r, const char* s,
                 float x, float y, float size, Color color)
{
    if (ctx.textMetrics && s && *s)
        ctx.textMetrics->drawText(r, s, x, y, size, color);
}

inline float textW(UIContext& ctx, const char* s, float size)
{
    return (ctx.textMetrics && s && *s) ? ctx.textMetrics->textWidth(s, size) : 0.0f;
}

inline void cellFrame(Renderer2D& r, Rect b)
{
    r.drawRect(b.x, b.y, b.w, b.h, Color{26, 24, 21, 255});
    r.drawRectOutline(b.x, b.y, b.w, b.h, Color{66, 54, 38, 255});
}

inline const char* waveName(int wave)
{
    static const char* names[] = { "Tri", "Saw", "Sqr", "Pls1", "Pls2" };
    return names[std::clamp(wave, 0, 4)];
}

inline const char* octName(int oct)
{
    static const char* names[] = { "32'", "16'", "8'", "4'", "2'" };
    return names[std::clamp(oct, 0, 4)];
}

inline const char* lfoName(int shape)
{
    static const char* names[] = { "Tri", "Sqr", "S&H", "Noise" };
    return names[std::clamp(shape, 0, 3)];
}

inline void cvMarkers(Renderer2D& r, Rect g)
{
    r.drawRect(g.x, g.y + g.h * 0.5f, g.w, 1.0f, Color{40, 38, 34, 255});
    for (int i = 1; i < 4; ++i)
        r.drawRect(g.x + g.w * i / 4.0f, g.y, 1.0f, g.h, Color{34, 32, 29, 255});
}

inline void drawVco(Renderer2D& r, Rect g, int wave, float level, Color color)
{
    cvMarkers(r, g);
    const float mid = g.y + g.h * 0.5f;
    const float amp = g.h * 0.42f * (0.25f + 0.75f * std::clamp(level, 0.0f, 1.0f));
    const int n = std::max(2, static_cast<int>(g.w));
    float px = g.x, py = mid;
    for (int i = 0; i <= n; ++i)
    {
        const float p = static_cast<float>(i) / n;
        float v = 0.0f;
        switch (wave)
        {
            case 0: // 52% triangle with the peak spike
            {
                const float warp = p < 0.48f ? p * (0.5f / 0.48f)
                                             : 0.5f + (p - 0.48f) * (0.5f / 0.52f);
                const float saw = 2.0f * warp - 1.0f;
                v = 2.0f * std::fabs(saw) - 1.0f;
                const float d = std::min(p, 1.0f - p);
                if (d < 0.012f) v += 0.09f * (1.0f - d * (1.0f / 0.012f));
                break;
            }
            case 1: // saw
                v = 2.0f * p - 1.0f;
                if (p < 0.02f) v += 0.035f * (1.0f - p * 50.0f);
                break;
            default: // 45 / 20 / 8.8% pulse
            {
                const float duty = wave == 2 ? 0.45f : (wave == 3 ? 0.20f : 0.088f);
                v = (p < duty ? 1.0f : 0.0f) - duty;
                v *= 2.0f;
                break;
            }
        }
        const float x = g.x + p * g.w;
        const float y = mid - v * amp;
        if (i > 0) r.drawLine(px, py, x, y, color, 1.3f);
        px = x;
        py = y;
    }
}

inline void drawFilter(Renderer2D& r, Rect g, float cutoffHz, float res, int mode)
{
    cvMarkers(r, g);
    const float fc = std::log(std::clamp(cutoffHz, 100.0f, 10000.0f) / 100.0f) /
                     std::log(100.0f);
    const int n = std::max(2, static_cast<int>(g.w));
    const Color col{236, 166, 74, 255};
    float px = g.x, py = g.y + g.h;
    for (int i = 0; i <= n; ++i)
    {
        const float t = static_cast<float>(i) / n;
        const float dist = t - fc;
        float mag;
        if (mode == 1)
        {
            const float bw = std::max(0.025f, 0.11f - res * 0.07f);
            mag = std::exp(-dist * dist / (2.0f * bw * bw));
        }
        else
        {
            const float slope = 6.0f + res * 12.0f;
            mag = 1.0f / (1.0f + std::exp(dist * slope));
        }
        mag += res * 0.55f * std::exp(-dist * dist * 55.0f);
        mag = std::clamp(mag, 0.0f, 1.25f);
        const float x = g.x + t * g.w;
        const float y = g.y + g.h - mag * g.h * 0.82f;
        if (i > 0) r.drawLine(px, py, x, y, col, 1.3f);
        px = x;
        py = y;
    }
    const float cx = g.x + std::clamp(fc, 0.0f, 1.0f) * g.w;
    r.drawRect(cx, g.y, 1.0f, g.h, Color{236, 166, 74, 70});
}

inline void drawLfo(Renderer2D& r, Rect g, int shape, float rate)
{
    cvMarkers(r, g);
    const float mid = g.y + g.h * 0.5f;
    const float amp = g.h * 0.36f;
    const int n = std::max(2, static_cast<int>(g.w));
    const Color col{120, 190, 235, 255};
    float px = g.x, py = mid;
    for (int i = 0; i <= n; ++i)
    {
        const float p = static_cast<float>(i) / n;
        float v = 0.0f;
        switch (shape)
        {
            case 0: v = 1.0f - 4.0f * std::fabs(p - 0.5f); break;
            case 1: v = p < 0.5f ? 1.0f : -1.0f; break;
            case 2: // stepped random
            {
                const int step = static_cast<int>(p * 8.0f);
                uint32_t h = static_cast<uint32_t>(step) * 2654435761u;
                h ^= h >> 13;
                v = static_cast<float>(h & 0xFFFFu) / 32768.0f - 1.0f;
                break;
            }
            default: // low-frequency noise
            {
                uint32_t h = static_cast<uint32_t>(i) * 2246822519u;
                h ^= h >> 15;
                v = static_cast<float>(h & 0xFFFFu) / 32768.0f - 1.0f;
                v *= 0.7f;
                break;
            }
        }
        const float x = g.x + p * g.w;
        const float y = mid - v * amp;
        if (i > 0) r.drawLine(px, py, x, y, col, 1.3f);
        px = x;
        py = y;
    }
    (void)rate;
}

inline void envSegment(Renderer2D& r, float x1, float y1, float x2, float y2,
                       bool exponential, Color col)
{
    const float dx = x2 - x1;
    if (std::fabs(dx) < 1.0f) return;
    const int steps = std::max(4, static_cast<int>(std::fabs(dx) / 2.0f));
    float px = x1, py = y1;
    for (int i = 1; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / steps;
        const float x = x1 + dx * t;
        const float dy = y2 - y1;
        const float y = exponential && std::fabs(dy) > 1.0f
            ? y1 + dy * (1.0f - std::exp(-t * 3.5f)) / (1.0f - std::exp(-3.5f))
            : y1 + dy * t;
        r.drawLine(px, py, x, y, col, 1.3f);
        px = x;
        py = y;
    }
}

inline void drawAdsr(Renderer2D& r, Rect g, float a, float d, float s, float rel,
                     bool loop, Color col)
{
    cvMarkers(r, g);
    const float total = std::max(0.05f, a + d + 0.25f + rel);
    const float sc = g.w / total;
    const float aEnd = g.x + a * sc;
    const float dEnd = aEnd + d * sc;
    const float sEnd = dEnd + 0.25f * sc;
    const float rEnd = std::min(sEnd + rel * sc, g.x + g.w);
    const float sY = g.y + g.h * (1.0f - std::clamp(s, 0.0f, 1.0f));
    envSegment(r, g.x, g.y + g.h, aEnd, g.y, false, col);
    envSegment(r, aEnd, g.y, dEnd, sY, true, col);
    envSegment(r, dEnd, sY, sEnd, sY, false, col);
    envSegment(r, sEnd, sY, rEnd, g.y + g.h, true, col);
    if (loop)
        r.drawRect(aEnd, g.y, 1.0f, g.h, col.withAlpha(60));
}

} // namespace polivoks_ui

inline void PolivoksDisplayWidget::render(UIContext& ctx)
{
    if (!isVisible() || !ctx.renderer) return;
    auto& r = *ctx.renderer;
    using namespace polivoks_ui;

    const Rect b = m_bounds;
    r.drawRect(b.x, b.y, b.w, b.h, Color{17, 16, 14, 255});
    r.drawRectOutline(b.x, b.y, b.w, b.h, Color{78, 62, 38, 255});

    const Color amber{226, 166, 76, 255};
    const Color dim{150, 132, 104, 255};
    const Color bright{230, 216, 190, 255};

    text(ctx, r, "POLIVOKS 1982", b.x + 6, b.y + 2, 12.0f, amber);
    const char* voice = m_state.voiceMode == 0 ? "1 VOICE" : "2 VOICE";
    text(ctx, r, voice, b.x + 130, b.y + 4, 8.5f, dim);
    if (m_state.programName && *m_state.programName)
    {
        const float tw = textW(ctx, m_state.programName, 8.5f);
        text(ctx, r, m_state.programName, b.x + b.w - tw - 5, b.y + 4, 8.5f, dim);
    }

    const float cellW = (b.w - 8.0f) / 3.0f;
    const float cellH = (b.h - 22.0f) / 2.0f;
    const float x0 = b.x + 2.0f;
    const float y0 = b.y + 18.0f;

    auto cell = [&](int col, int row) {
        return Rect{x0 + col * cellW, y0 + row * cellH,
                    cellW - 2.0f, cellH - 2.0f};
    };

    auto header = [&](Rect c, const char* label, const char* value, Color vc) {
        cellFrame(r, c);
        text(ctx, r, label, c.x + 3.0f, c.y + 1.0f, 9.5f, dim);
        if (value && *value)
        {
            const float tw = textW(ctx, value, 8.5f);
            text(ctx, r, value, c.x + c.w - tw - 3.0f, c.y + 1.0f, 8.5f, vc);
        }
        return Rect{c.x + 4.0f, c.y + 13.0f, c.w - 8.0f, c.h - 16.0f};
    };

    char vbuf[32];

    // VCO I
    {
        Rect c = cell(0, 0);
        std::snprintf(vbuf, sizeof(vbuf), "%s %s", waveName(m_state.vco1Wave),
                      octName(m_state.vco1Oct));
        Rect g = header(c, "VCO I", vbuf, amber);
        drawVco(r, g, m_state.vco1Wave, m_state.vco1Level, amber);
    }

    // VCO II
    {
        Rect c = cell(1, 0);
        std::snprintf(vbuf, sizeof(vbuf), "%s %s", waveName(m_state.vco2Wave),
                      octName(m_state.vco2Oct));
        Rect g = header(c, "VCO II", vbuf, Color{232, 128, 72, 255});
        drawVco(r, g, m_state.vco2Wave, m_state.vco2Level, Color{232, 128, 72, 255});
    }

    // VCF
    {
        Rect c = cell(2, 0);
        const char* mode = m_state.filterMode == 1 ? "BP" : "LP";
        if (m_state.cutoffHz >= 1000.0f)
            std::snprintf(vbuf, sizeof(vbuf), "%s %.1fk", mode, m_state.cutoffHz / 1000.0f);
        else
            std::snprintf(vbuf, sizeof(vbuf), "%s %.0f", mode, m_state.cutoffHz);
        Rect g = header(c, "VCF", vbuf, amber);
        drawFilter(r, g, m_state.cutoffHz, m_state.resonance, m_state.filterMode);
    }

    // LFO
    {
        Rect c = cell(0, 1);
        std::snprintf(vbuf, sizeof(vbuf), "%s %.1f", lfoName(m_state.lfoShape),
                      m_state.lfoRate);
        Rect g = header(c, "LFO", vbuf, Color{120, 190, 235, 255});
        drawLfo(r, g, m_state.lfoShape, m_state.lfoRate);
    }

    // Filter envelope
    {
        Rect c = cell(1, 1);
        Rect g = header(c, "FILT ENV", m_state.fLoop ? "LOOP" : "ONE",
                        m_state.fLoop ? Color{236, 166, 74, 255} : dim);
        drawAdsr(r, g, m_state.fA, m_state.fD, m_state.fS, m_state.fR,
                 m_state.fLoop, Color{224, 190, 84, 255});
    }

    // Amp envelope
    {
        Rect c = cell(2, 1);
        const char* stateText = m_state.gate ? "GATE" : (m_state.aLoop ? "LOOP" : "ONE");
        Rect g = header(c, "AMP", stateText,
                        (m_state.gate || m_state.aLoop) ? bright : dim);
        drawAdsr(r, g, m_state.aA, m_state.aD, m_state.aS, m_state.aR,
                 m_state.aLoop, Color{104, 214, 128, 255});
    }
}
#endif // YAWN_TEST_BUILD

// ─────────────────────────────────────────────────────────────────────
// PolivoksPanel — grouped control body + display.
// ─────────────────────────────────────────────────────────────────────
class PolivoksPanel : public CustomDeviceBody
{
public:
    explicit PolivoksPanel(instruments::Instrument* inst)
        : m_inst(inst)
    {
        setName("PolivoksPanel");
        setAutoCaptureOnUnhandledPress(false);
        buildControls();
        buildSections();
    }

    void updateParamValue(int index, float value) override
    {
        if (index < 0 || index >= static_cast<int>(m_controls.size())) return;
        auto& c = m_controls[(size_t)index];
        if (c.knob)
        {
            if (!c.knob->isDragging() && !c.knob->isEditing())
                c.knob->setValue(value, ValueChangeSource::Programmatic);
        }
        else if (c.toggle)
        {
            c.toggle->setState(value >= 0.5f, ValueChangeSource::Programmatic);
        }
        else if (c.step)
        {
            c.step->setValue(static_cast<int>(std::lround(value)),
                             ValueChangeSource::Programmatic);
        }
    }

    float preferredBodyWidth() const override
    {
        float w = 8.0f + kDisplayW + 8.0f;
        for (const auto& s : m_sections)
            w += sectionWidth(s) + 6.0f;
        return w + 8.0f;
    }

    void setOnParamChange(std::function<void(int, float)> cb) override
    {
        m_onParamChange = std::move(cb);
    }

    void setOnParamTouch(std::function<void(int, float, bool)> cb) override
    {
        m_onParamTouch = std::move(cb);
    }

    bool hasEditingKnob() const override
    {
        for (const auto& c : m_controls)
            if (c.knob && c.knob->isEditing()) return true;
        return false;
    }

    bool forwardKeyDown(int key) override
    {
        for (auto& c : m_controls)
        {
            if (!c.knob || !c.knob->isEditing()) continue;
            KeyEvent ke;
            switch (key)
            {
                case 27: ke.key = Key::Escape; break;
                case 13: ke.key = Key::Enter; break;
                case 8:  ke.key = Key::Backspace; break;
                default: return false;
            }
            return c.knob->dispatchKeyDown(ke);
        }
        return false;
    }

    bool forwardTextInput(const char* text) override
    {
        for (auto& c : m_controls)
        {
            if (!c.knob || !c.knob->isEditing()) continue;
            c.knob->takeTextInput(text ? text : "");
            return true;
        }
        return false;
    }

    void cancelEditingKnobs() override
    {
        for (auto& c : m_controls)
            if (c.knob && c.knob->isEditing()) c.knob->endEdit(false);
    }

    void updateFromParams(instruments::Instrument& inst)
    {
        using P = instruments::PolivoksSynth;
        auto g = [&inst](int index) { return inst.getParameter(index); };
        auto gi = [&inst](int index) {
            return static_cast<int>(std::lround(inst.getParameter(index)));
        };

        PolivoksDisplayWidget::State s;
        s.vco1Wave = gi(P::kVco1Wave);
        s.vco1Oct = gi(P::kVco1Oct);
        s.vco1Level = g(P::kVco1Level);
        s.vco2Wave = gi(P::kVco2Wave);
        s.vco2Oct = gi(P::kVco2Oct);
        s.vco2Level = g(P::kVco2Level);
        s.vco2Detune = g(P::kVco2Detune);
        s.xmod = g(P::kXMod);
        s.noise = g(P::kNoiseLevel);
        s.cutoffHz = P::cutoffNormToHz(g(P::kCutoff));
        s.resonance = g(P::kResonance);
        s.envAmount = g(P::kEnvAmount);
        s.lfoVcf = g(P::kLfoToVcf);
        s.filterMode = gi(P::kFilterMode);
        s.fA = g(P::kFAttack);
        s.fD = g(P::kFDecay);
        s.fS = g(P::kFSustain);
        s.fR = g(P::kFRelease);
        s.fLoop = g(P::kFLoop) >= 0.5f;
        s.aA = g(P::kAAttack);
        s.aD = g(P::kADecay);
        s.aS = g(P::kASustain);
        s.aR = g(P::kARelease);
        s.aLoop = g(P::kALoop) >= 0.5f;
        s.gate = g(P::kGateHold) >= 0.5f;
        s.lfoShape = gi(P::kLfoShape);
        s.lfoRate = P::lfoNormToHz(g(P::kLfoRate));
        s.lfoVca = g(P::kLfoToVca);
        s.voiceMode = gi(P::kVoiceMode);
        s.programName = P::programName(gi(P::kProgram));
        m_display.setState(s);
    }

    Size onMeasure(Constraints c, UIContext&) override
    {
        return c.constrain({preferredBodyWidth(), c.maxH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override
    {
        Widget::onLayout(bounds, ctx);

        const float displayH = std::clamp(bounds.h - 8.0f, 96.0f, kDisplayH);
        m_display.measure(Constraints::tight(kDisplayW, displayH), ctx);
        m_display.layout({bounds.x + 4.0f, bounds.y + 4.0f, kDisplayW, displayH}, ctx);

        float x = bounds.x + 4.0f + kDisplayW + 6.0f;
        for (auto& sec : m_sections)
        {
            sec.x = x;
            sec.w = sectionWidth(sec);

            const int cols = std::max(1, (static_cast<int>(sec.params.size()) + 1) / 2);
            float colW[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (size_t i = 0; i < sec.params.size(); ++i)
            {
                const int col = static_cast<int>(i) % cols;
                if (col < 4)
                    colW[col] = std::max(colW[col], cellWidth(m_controls[(size_t)sec.params[i]]));
            }

            float cx = x;
            for (int col = 0; col < cols && col < 4; ++col)
            {
                for (size_t i = 0; i < sec.params.size(); ++i)
                {
                    if (static_cast<int>(i) % cols != col) continue;
                    const int row = static_cast<int>(i) / cols;
                    auto& c = m_controls[(size_t)sec.params[i]];
                    const float cw = cellWidth(c);
                    Rect r{cx, bounds.y + 20.0f + row * 68.0f, cw, 64.0f};
                    c.bounds = r;
                    c.widget()->measure(Constraints::tight(cw, 64.0f), ctx);
                    c.widget()->layout(r, ctx);
                }
                cx += colW[col] + 4.0f;
            }
            x += sec.w + 6.0f;
        }
    }

#ifndef YAWN_TEST_BUILD
    void render(UIContext& ctx) override
    {
        if (!isVisible() || !ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{22, 21, 19, 255});

        m_display.render(ctx);

        const float fs = 8.0f;
        const Color labelCol{170, 148, 112, 230};
        for (const auto& sec : m_sections)
        {
            if (tm && !sec.label.empty())
                tm->drawText(r, sec.label, sec.x + 2.0f, m_bounds.y + 2.0f,
                             fs, labelCol);
            r.drawRect(sec.x - 3.0f, m_bounds.y + 2.0f, 1.0f,
                       m_bounds.h - 4.0f, Color{64, 54, 42, 150});
        }

        for (auto& c : m_controls)
            c.widget()->render(ctx);
    }
#endif

    bool onMouseDown(MouseEvent& e) override
    {
        for (auto& c : m_controls)
        {
            const auto& b = c.bounds;
            if (e.x < b.x || e.x >= b.x + b.w || e.y < b.y || e.y >= b.y + b.h)
                continue;
            MouseEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            c.widget()->dispatchMouseDown(ev);
            m_dragging = c.widget();
            captureMouse();
            return true;
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override
    {
        if (!m_dragging) return false;
        MouseMoveEvent ev = e;
        ev.lx = e.x - m_dragging->bounds().x;
        ev.ly = e.y - m_dragging->bounds().y;
        m_dragging->dispatchMouseMove(ev);
        return true;
    }

    bool onMouseUp(MouseEvent& e) override
    {
        if (!m_dragging) return false;
        MouseEvent ev = e;
        ev.lx = e.x - m_dragging->bounds().x;
        ev.ly = e.y - m_dragging->bounds().y;
        m_dragging->dispatchMouseUp(ev);
        m_dragging = nullptr;
        releaseMouse();
        return true;
    }

    bool onScroll(ScrollEvent& e) override
    {
        for (auto& c : m_controls)
        {
            const auto& b = c.bounds;
            if (e.x < b.x || e.x >= b.x + b.w || e.y < b.y || e.y >= b.y + b.h)
                continue;
            ScrollEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            return c.widget()->dispatchScroll(ev);
        }
        return false;
    }

private:
    struct Control
    {
        int index = -1;
        enum class Kind { Knob, Toggle, Step } kind = Kind::Knob;
        std::unique_ptr<FwKnob> knob;
        std::unique_ptr<FwToggle> toggle;
        std::unique_ptr<FwStepSelector> step;
        Rect bounds{};

        Widget* widget() const
        {
            if (knob) return knob.get();
            if (toggle) return toggle.get();
            if (step) return step.get();
            return nullptr;
        }
    };

    struct Section
    {
        std::string label;
        std::vector<int> params;
        float x = 0.0f;
        float w = 0.0f;
    };

    static constexpr float kDisplayW = 330.0f;
    static constexpr float kDisplayH = 168.0f;

    static std::string shortLabel(const char* name)
    {
        std::string s = name ? name : "";
        if (s.rfind("VCO1 ", 0) == 0) s = s.substr(5);
        else if (s.rfind("VCO2 ", 0) == 0) s = s.substr(5);
        else if (s.rfind("Filt ", 0) == 0) s = s.substr(5);
        else if (s.rfind("Amp ", 0) == 0) s = s.substr(4);

        if (s == "Master Tune") return "Tune";
        if (s == "Voice Mode") return "Voice";
        if (s == "Filter Mode") return "Mode";
        if (s == "Resonance") return "Reso";
        if (s == "Env Amount") return "Env Amt";
        if (s == "LFO Shape") return "Shape";
        if (s == "LFO Rate") return "Rate";
        if (s == "Key Track") return "Key Trk";
        if (s == "Bend Range") return "Bend";
        if (s == "Gate On") return "Gate";
        if (s == "Attack") return "Atk";
        if (s == "Decay") return "Dcy";
        if (s == "Sustain") return "Sus";
        if (s == "Release") return "Rel";
        if (s == "Loop") return "Loop";
        if (s == "LFO>VCO1" || s == "LFO>VCO2" || s == "LFO>VCF" || s == "LFO>VCA")
            return "LFO";
        if (s == "Velocity") return "Vel";
        return s;
    }

    static std::string formatKnob(const instruments::InstrumentParameterInfo& info,
                                  float value)
    {
        char buf[32];
        if (info.formatFn)
        {
            info.formatFn(value, buf, static_cast<int>(sizeof(buf)));
            return std::string(buf);
        }
        const std::string unit = info.unit ? info.unit : "";
        if (unit == "%")       std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
        else if (unit == "dB") std::snprintf(buf, sizeof(buf), "%.1f", value);
        else if (unit == "st") std::snprintf(buf, sizeof(buf), "%.2f st", value);
        else                   std::snprintf(buf, sizeof(buf), "%.2f", value);
        return std::string(buf);
    }

    void buildControls()
    {
        if (!m_inst) return;
        const int count = m_inst->parameterCount();
        m_controls.resize((size_t)count);

        auto emit = [this](int index, float value) {
            if (m_onParamChange) m_onParamChange(index, value);
        };
        auto touchEnd = [this](int index, float start, float end) {
            if (!m_onParamTouch) return;
            m_onParamTouch(index, start, true);
            m_onParamTouch(index, end, false);
        };

        for (int i = 0; i < count; ++i)
        {
            const auto& info = m_inst->parameterInfo(i);
            Control& c = m_controls[(size_t)i];
            c.index = i;
            const std::string label = shortLabel(info.name);

            if (info.isBoolean || info.widgetHint == WidgetHint::Toggle)
            {
                c.kind = Control::Kind::Toggle;
                c.toggle = std::make_unique<FwToggle>(label);
                c.toggle->setFixedWidth(62.0f);
                c.toggle->setState(info.defaultValue >= 0.5f,
                                   ValueChangeSource::Programmatic);
                c.toggle->setOnChange([emit, i](bool on) { emit(i, on ? 1.0f : 0.0f); });
            }
            else if (info.widgetHint == WidgetHint::StepSelector && info.valueLabels)
            {
                c.kind = Control::Kind::Step;
                c.step = std::make_unique<FwStepSelector>();
                const int mn = static_cast<int>(std::lround(info.minValue));
                const int mx = static_cast<int>(std::lround(info.maxValue));
                c.step->setRange(mn, mx);
                c.step->setValue(static_cast<int>(std::lround(info.defaultValue)));
                c.step->setLabel(label);
                const auto* labels = info.valueLabels;
                const int labelCount = info.valueLabelCount;
                c.step->setFormatter([labels, labelCount, mn](int v) -> std::string {
                    const int idx = v - mn;
                    if (labels && idx >= 0 && idx < labelCount)
                        return std::string(labels[idx]);
                    return std::to_string(v);
                });
                c.step->setOnChange([emit, i](int v) { emit(i, static_cast<float>(v)); });
            }
            else
            {
                c.kind = Control::Kind::Knob;
                c.knob = std::make_unique<FwKnob>();
                c.knob->setRange(info.minValue, info.maxValue);
                c.knob->setDefaultValue(info.defaultValue);
                c.knob->setValue(info.defaultValue, ValueChangeSource::Programmatic);
                c.knob->setLabel(label);
                const auto& paramInfo = info;
                c.knob->setValueFormatter([paramInfo](float v) {
                    return formatKnob(paramInfo, v);
                });
                if (std::fabs(info.minValue + info.maxValue) < 0.001f)
                {
                    c.knob->setBipolar(true);
                    c.knob->addDetent(0.0f, 0.05f);
                }
                c.knob->setOnChange([emit, i](float v) { emit(i, v); });
                c.knob->setOnDragEnd([touchEnd, i](float start, float end) {
                    touchEnd(i, start, end);
                });
            }
        }
    }

    void buildSections()
    {
        using P = instruments::PolivoksSynth;
        m_sections = {
            { "PROGRAM",   { P::kProgram } },
            { "MASTER",    { P::kVolume, P::kTune, P::kGlide, P::kVoiceMode } },
            { "VCO I",     { P::kVco1Wave, P::kVco1Oct, P::kVco1Level, P::kLfoToVco1 } },
            { "VCO II",    { P::kVco2Wave, P::kVco2Oct, P::kVco2Detune,
                             P::kVco2Level, P::kLfoToVco2 } },
            { "MIXER",     { P::kNoiseLevel, P::kXMod } },
            { "FILTER",    { P::kCutoff, P::kResonance, P::kEnvAmount,
                             P::kLfoToVcf, P::kFilterMode } },
            { "FILT ENV",  { P::kFAttack, P::kFDecay, P::kFSustain,
                             P::kFRelease, P::kFLoop } },
            { "AMP",       { P::kAAttack, P::kADecay, P::kASustain,
                             P::kARelease, P::kALoop, P::kGateHold } },
            { "LFO",       { P::kLfoShape, P::kLfoRate, P::kLfoToVca } },
            { "MORE",      { P::kKeyTrack, P::kVelocity, P::kBendRange, P::kPedal } },
        };
    }

    static float cellWidth(const Control& c)
    {
        if (c.kind == Control::Kind::Step)
            return c.index == 0 ? 122.0f : 96.0f;
        return 62.0f;
    }

    float sectionWidth(const Section& s) const
    {
        const int n = static_cast<int>(s.params.size());
        if (n <= 0) return 0.0f;
        const int cols = std::max(1, (n + 1) / 2);
        float colW[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < n; ++i)
        {
            const int col = i % cols;
            if (col < 4)
                colW[col] = std::max(colW[col], cellWidth(m_controls[(size_t)s.params[i]]));
        }
        float w = 0.0f;
        for (int col = 0; col < cols && col < 4; ++col) w += colW[col];
        w += 4.0f * (cols - 1);
        return w;
    }

    instruments::Instrument* m_inst = nullptr;
    PolivoksDisplayWidget m_display;
    std::vector<Control> m_controls;
    std::vector<Section> m_sections;
    Widget* m_dragging = nullptr;
    std::function<void(int, float)> m_onParamChange;
    std::function<void(int, float, bool)> m_onParamTouch;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
