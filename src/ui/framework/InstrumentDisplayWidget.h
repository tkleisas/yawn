#pragma once
// InstrumentDisplayWidget.h — Custom visual display widgets for native instruments.
//
// Provides specialized visualizations rendered inside DeviceWidget's custom
// panel area (between header and knob grid):
//   - FMAlgorithmWidget:     4-operator FM routing diagram
//   - ADSRDisplayWidget:     Attack/Decay/Sustain/Release envelope curve
//   - OscDisplayWidget:      Oscillator waveform shape preview
//   - FilterDisplayWidget:   Filter frequency response curve
//   - SubSynthDisplayPanel:  Composite panel for Subtractive Synth

#include "Widget.h"
#include "Primitives.h"
#include "../Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef YAWN_TEST_BUILD
#include "../Renderer.h"
#include "../Font.h"
#endif

namespace yawn {
namespace ui {
namespace fw {

// ═══════════════════════════════════════════════════════════════════════════
// FMAlgorithmWidget — Draws 4 operators with modulation connections.
// Carriers are green, modulators are orange. Arrows show signal flow.
// Algorithm data mirrors FMSynth.h kAlgorithms (8 presets, 4 operators).
// ═══════════════════════════════════════════════════════════════════════════

class FMAlgorithmWidget : public Widget {
public:
    FMAlgorithmWidget() { setName("FMAlgorithmWidget"); }

    void setAlgorithm(int algo) { m_algorithm = std::clamp(algo, 0, 7); }
    void setFeedback(float fb) { m_feedback = std::clamp(fb, 0.0f, 1.0f); }
    void setOpLevels(float o1, float o2, float o3, float o4) {
        m_opLevel[0] = o1; m_opLevel[1] = o2;
        m_opLevel[2] = o3; m_opLevel[3] = o4;
    }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 60.0f});
    }
    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        // Background
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, Color{22, 22, 28, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        float lblScale = 8.0f / Theme::kFontSize;
        char algoText[16];
        std::snprintf(algoText, sizeof(algoText), "Algo %d", m_algorithm + 1);
        f.drawText(r, algoText, m_bounds.x + 4, m_bounds.y + 2, lblScale,
                   Color{180, 180, 200, 200});

        drawAlgorithm(r, f);
#endif
    }

private:
    int   m_algorithm = 0;
    float m_feedback  = 0.2f;
    float m_opLevel[4] = {1.0f, 0.8f, 0.6f, 0.4f};

    // Algorithm presets (matches FMSynth.h kAlgorithms)
    struct AlgoDef { bool mod[4][4]; bool carrier[4]; };
    static constexpr AlgoDef kAlgos[8] = {
        // 0: Serial 4→3→2→1(C)
        {{{0,1,0,0},{0,0,1,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},
        // 1: (3+4)→2→1(C)
        {{{0,1,0,0},{0,0,1,1},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},
        // 2: 2→1(C), 4→3(C)
        {{{0,1,0,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,1,0}},
        // 3: (2+3+4)→1(C)
        {{{0,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},
        // 4: All carriers (additive)
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,1,1,1}},
        // 5: 4→3→1(C), 2→1(C)
        {{{0,1,1,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},
        // 6: 4→(1+2+3)(C)
        {{{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,0}}, {1,1,1,0}},
        // 7: 3→1(C), 4→2(C)
        {{{0,0,1,0},{0,0,0,1},{0,0,0,0},{0,0,0,0}}, {1,1,0,0}},
    };

#ifndef YAWN_TEST_BUILD
    void drawAlgorithm(Renderer2D& r, Font& f) {
        const auto& algo = kAlgos[m_algorithm];
        float areaX = m_bounds.x + 6;
        float areaY = m_bounds.y + 13;
        float areaW = m_bounds.w - 12;
        float areaH = m_bounds.h - 22;
        if (areaW < 40 || areaH < 20) return;

        constexpr float boxW = 26, boxH = 18;
        float opX[4], opY[4];
        layoutOperators(algo, areaX, areaY, areaW, areaH, boxW, boxH, opX, opY);

        // Draw connections (modulator → destination)
        Color connCol{220, 150, 50, 180};
        for (int dest = 0; dest < 4; ++dest)
            for (int src = 0; src < 4; ++src)
                if (algo.mod[dest][src])
                    drawArrow(r, opX[src] + boxW / 2, opY[src] + boxH,
                                 opX[dest] + boxW / 2, opY[dest], connCol);

        // Output arrows from carriers
        Color outCol{70, 210, 90, 200};
        for (int op = 0; op < 4; ++op) {
            if (!algo.carrier[op]) continue;
            float cx = opX[op] + boxW / 2;
            float by = opY[op] + boxH;
            float endY = std::min(by + 8, m_bounds.y + m_bounds.h - 4);
            for (float py = by + 1; py < endY; py += 2)
                r.drawRect(cx - 0.5f, py, 1.5f, 1.5f, outCol);
            r.drawTriangle(cx - 3, endY - 1, cx + 3, endY - 1, cx, endY + 2, outCol);
        }

        // Feedback indicator on operator 4 (index 3)
        if (m_feedback > 0.01f) {
            float nameScale = 7.0f / Theme::kFontSize;
            Color fbCol{255, 200, 60, 200};
            float fbx = opX[3] + boxW + 3;
            float fby = opY[3] + 2;
            // Draw small self-loop: right→up→left
            float rx = opX[3] + boxW + 1;
            float ry = opY[3] + boxH * 0.5f;
            for (int s = 0; s < 4; ++s)
                r.drawRect(rx + s * 2.0f, ry - s * 2.5f, 1.5f, 1.5f, fbCol);
            float topY = ry - 10;
            for (float px = rx + 6; px >= opX[3] + boxW * 0.7f; px -= 2)
                r.drawRect(px, topY, 1.5f, 1.5f, fbCol);
            r.drawTriangle(opX[3] + boxW * 0.7f - 1, topY - 2,
                           opX[3] + boxW * 0.7f - 1, topY + 3,
                           opX[3] + boxW * 0.7f - 4, topY + 0.5f, fbCol);
            f.drawText(r, "FB", fbx + 6, fby, nameScale, fbCol);
        }

        // Draw operator boxes
        float nameScale = 9.0f / Theme::kFontSize;
        for (int op = 0; op < 4; ++op) {
            bool isCarrier = algo.carrier[op];
            float lvl = m_opLevel[op];
            Color boxBg = isCarrier
                ? Color{static_cast<uint8_t>(25 + lvl * 40), static_cast<uint8_t>(80 + lvl * 35), static_cast<uint8_t>(40 + lvl * 20), 255}
                : Color{static_cast<uint8_t>(75 + lvl * 35), static_cast<uint8_t>(50 + lvl * 25), static_cast<uint8_t>(20 + lvl * 15), 255};
            r.drawRoundedRect(opX[op], opY[op], boxW, boxH, 3.0f, boxBg);
            Color border = isCarrier ? Color{80, 220, 100, 230} : Color{230, 160, 60, 230};
            r.drawRectOutline(opX[op], opY[op], boxW, boxH, border);

            char label[4];
            std::snprintf(label, sizeof(label), "%d", op + 1);
            float tw = f.textWidth(label, nameScale);
            f.drawText(r, label, opX[op] + (boxW - tw) / 2,
                       opY[op] + (boxH - 10) / 2, nameScale, Color{240, 240, 255, 230});
        }

        // "OUT" label
        float outLblScale = 7.0f / Theme::kFontSize;
        float outX = areaX + areaW / 2;
        f.drawText(r, "OUT", outX - 8, m_bounds.y + m_bounds.h - 11, outLblScale, outCol);
    }

    void layoutOperators(const AlgoDef& algo, float areaX, float areaY,
                         float areaW, float areaH, float boxW, float boxH,
                         float* opX, float* opY) {
        // Compute depth: carriers=0, their modulators=1, etc.
        int depth[4];
        for (int op = 0; op < 4; ++op)
            depth[op] = algo.carrier[op] ? 0 : -1;

        for (int pass = 0; pass < 4; ++pass)
            for (int dest = 0; dest < 4; ++dest)
                for (int src = 0; src < 4; ++src)
                    if (algo.mod[dest][src] && depth[dest] >= 0)
                        if (depth[src] < depth[dest] + 1)
                            depth[src] = depth[dest] + 1;

        for (int op = 0; op < 4; ++op)
            if (depth[op] < 0) depth[op] = 1;

        int maxDepth = 0;
        for (int op = 0; op < 4; ++op)
            if (depth[op] > maxDepth) maxDepth = depth[op];

        int countAt[5] = {}, idxAt[5] = {};
        for (int op = 0; op < 4; ++op) countAt[depth[op]]++;

        float vGap = maxDepth > 0
            ? std::min((areaH - boxH) / maxDepth, boxH + 6.0f)
            : 0.0f;

        for (int op = 0; op < 4; ++op) {
            int d = depth[op];
            int cnt = countAt[d];
            int idx = idxAt[d]++;
            float totalW = cnt * boxW + (cnt - 1) * 14.0f;
            float startX = areaX + (areaW - totalW) / 2;
            opX[op] = startX + idx * (boxW + 14.0f);
            opY[op] = areaY + (maxDepth - d) * vGap;
        }
    }

    void drawArrow(Renderer2D& r, float x1, float y1, float x2, float y2, Color col) {
        float dx = x2 - x1, dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 2) return;
        int steps = std::max(2, static_cast<int>(len / 2.5f));
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            r.drawRect(x1 + dx * t - 0.5f, y1 + dy * t - 0.5f, 1.5f, 1.5f, col);
        }
        // Arrowhead at destination
        r.drawTriangle(x2 - 3, y2 - 3, x2 + 3, y2 - 3, x2, y2 + 1, col);
    }
#endif // YAWN_TEST_BUILD
};

// ═══════════════════════════════════════════════════════════════════════════
// ADSRDisplayWidget — Renders an ADSR envelope curve.
// ═══════════════════════════════════════════════════════════════════════════

class ADSRDisplayWidget : public Widget {
public:
    ADSRDisplayWidget() { setName("ADSRDisplay"); }

    void setADSR(float a, float d, float s, float rel) {
        m_attack = a; m_decay = d; m_sustain = s; m_release = rel;
    }
    void setColor(Color c) { m_color = c; }
    void setLabel(const std::string& l) { m_label = l; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({120.0f, 48.0f});
    }
    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        float lblScale = 7.0f / Theme::kFontSize;
        if (!m_label.empty())
            f.drawText(r, m_label.c_str(), m_bounds.x + 3, m_bounds.y + 1,
                       lblScale, Theme::textDim);

        float px = m_bounds.x + 4;
        float py = m_bounds.y + 11;
        float pw = m_bounds.w - 8;
        float ph = m_bounds.h - 14;
        if (pw < 10 || ph < 6) return;

        // Normalize times
        float total = m_attack + m_decay + 0.25f + m_release;
        if (total < 0.001f) total = 1.0f;
        float sc = pw / total;

        float aEnd = px + m_attack * sc;
        float dEnd = aEnd + m_decay * sc;
        float sEnd = dEnd + 0.25f * sc;
        float rEnd = std::min(sEnd + m_release * sc, px + pw);
        float sY = py + ph * (1.0f - m_sustain);

        // Attack (linear up)
        drawSeg(r, px, py + ph, aEnd, py, false);
        // Decay (exponential to sustain)
        drawSeg(r, aEnd, py, dEnd, sY, true);
        // Sustain hold
        drawSeg(r, dEnd, sY, sEnd, sY, false);
        // Release (exponential to zero)
        drawSeg(r, sEnd, sY, rEnd, py + ph, true);

        // Phase dividers
        Color dimCol{m_color.r, m_color.g, m_color.b, 50};
        r.drawRect(aEnd, py, 1, ph, dimCol);
        r.drawRect(dEnd, py, 1, ph, dimCol);
        r.drawRect(sEnd, py, 1, ph, dimCol);
#endif
    }

private:
    float m_attack = 0.01f, m_decay = 0.1f, m_sustain = 0.7f, m_release = 0.3f;
    Color m_color{80, 220, 100, 255};
    std::string m_label;

#ifndef YAWN_TEST_BUILD
    void drawSeg(Renderer2D& r, float x1, float y1, float x2, float y2, bool exponential) {
        float dx = x2 - x1;
        if (std::abs(dx) < 1) return;
        int steps = std::max(4, static_cast<int>(std::abs(dx) / 1.5f));
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            float px = x1 + dx * t;
            float pyl;
            float dy = y2 - y1;
            if (exponential && std::abs(dy) > 1)
                pyl = y1 + dy * (1.0f - std::exp(-t * 3.5f)) / (1.0f - std::exp(-3.5f));
            else
                pyl = y1 + dy * t;
            r.drawRect(px, pyl, 1.5f, 1.5f, m_color);
        }
    }
#endif
};

// ═══════════════════════════════════════════════════════════════════════════
// OscDisplayWidget — Shows oscillator waveform shape.
// Waveform types: 0=Sine, 1=Saw, 2=Square, 3=Triangle, 4=Noise
// ═══════════════════════════════════════════════════════════════════════════

class OscDisplayWidget : public Widget {
public:
    OscDisplayWidget() { setName("OscDisplay"); }

    void setWaveform(int type) { m_waveform = std::clamp(type, 0, 4); }
    void setLevel(float l) { m_level = std::clamp(l, 0.0f, 1.0f); }
    void setColor(Color c) { m_color = c; }
    void setLabel(const std::string& l) { m_label = l; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({60.0f, 48.0f});
    }
    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        float lblScale = 7.0f / Theme::kFontSize;
        if (!m_label.empty())
            f.drawText(r, m_label.c_str(), m_bounds.x + 3, m_bounds.y + 1,
                       lblScale, Theme::textDim);

        float wx = m_bounds.x + 4;
        float wy = m_bounds.y + 11;
        float ww = m_bounds.w - 8;
        float wh = m_bounds.h - 14;
        if (ww < 10 || wh < 6) return;

        float midY = wy + wh / 2;
        r.drawRect(wx, midY, ww, 1, Color{35, 35, 45, 255});

        Color col = m_level > 0.01f
            ? m_color
            : Color{m_color.r, m_color.g, m_color.b, 50};
        float amp = wh / 2 * std::min(1.0f, m_level + 0.25f);

        int steps = static_cast<int>(ww);
        for (int i = 0; i < steps; ++i) {
            float t = static_cast<float>(i) / steps;
            float val = 0;
            switch (m_waveform) {
                case 0: val = std::sin(t * 2 * static_cast<float>(M_PI)); break;
                case 1: val = 2.0f * t - 1.0f; break;
                case 2: val = t < 0.5f ? 1.0f : -1.0f; break;
                case 3: val = t < 0.5f ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t); break;
                case 4: val = static_cast<float>((i * 1103515245 + 12345) & 0x7fffffff)
                              / static_cast<float>(0x7fffffff) * 2.0f - 1.0f; break;
            }
            r.drawRect(wx + i, midY - val * amp, 1.5f, 1.5f, col);
        }

        static const char* waveNames[] = {"Sin", "Saw", "Sqr", "Tri", "Nse"};
        const char* wn = (m_waveform >= 0 && m_waveform <= 4) ? waveNames[m_waveform] : "?";
        float tw = f.textWidth(wn, lblScale);
        f.drawText(r, wn, m_bounds.x + m_bounds.w - tw - 3, m_bounds.y + 1, lblScale, col);
#endif
    }

private:
    int m_waveform = 0;
    float m_level = 1.0f;
    Color m_color{0, 200, 255, 255};
    std::string m_label;
};

// ═══════════════════════════════════════════════════════════════════════════
// FilterDisplayWidget — Shows approximate filter frequency response.
// Filter types: 0=LP, 1=HP, 2=BP
// ═══════════════════════════════════════════════════════════════════════════

class FilterDisplayWidget : public Widget {
public:
    FilterDisplayWidget() { setName("FilterDisplay"); }

    void setCutoff(float hz) { m_cutoff = std::clamp(hz, 20.0f, 20000.0f); }
    void setResonance(float q) { m_resonance = std::clamp(q, 0.0f, 1.0f); }
    void setFilterType(int type) { m_type = std::clamp(type, 0, 2); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({100.0f, 48.0f});
    }
    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        float lblScale = 7.0f / Theme::kFontSize;
        static const char* typeNames[] = {"LP", "HP", "BP"};
        f.drawText(r, typeNames[m_type], m_bounds.x + 3, m_bounds.y + 1,
                   lblScale, Theme::textDim);

        float px = m_bounds.x + 4;
        float py = m_bounds.y + 11;
        float pw = m_bounds.w - 8;
        float ph = m_bounds.h - 14;
        if (pw < 10 || ph < 6) return;

        // Log-scale frequency: 20Hz→20kHz mapped to 0..1
        float cutoffNorm = std::log(m_cutoff / 20.0f) / std::log(1000.0f);

        Color lineCol{255, 160, 40, 220};
        int steps = static_cast<int>(pw);
        for (int i = 0; i < steps; ++i) {
            float t = static_cast<float>(i) / steps;
            float dist = t - cutoffNorm;
            float slope = 7.0f + m_resonance * 12.0f;
            float mag = 0;

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
            }
            mag = std::clamp(mag, 0.0f, 1.3f);
            float drawY = py + ph * (1.0f - mag * 0.75f);
            r.drawRect(px + i, drawY, 1.5f, 1.5f, lineCol);
        }

        // Cutoff frequency line
        float cutoffX = px + cutoffNorm * pw;
        if (cutoffX >= px && cutoffX <= px + pw)
            r.drawRect(cutoffX, py, 1, ph, Color{255, 160, 40, 60});

        // Frequency label
        char hzBuf[16];
        if (m_cutoff >= 1000)
            std::snprintf(hzBuf, sizeof(hzBuf), "%.1fk", m_cutoff / 1000.0f);
        else
            std::snprintf(hzBuf, sizeof(hzBuf), "%.0f", m_cutoff);
        float tw = f.textWidth(hzBuf, lblScale);
        f.drawText(r, hzBuf, m_bounds.x + m_bounds.w - tw - 3, m_bounds.y + 1,
                   lblScale, Color{255, 160, 40, 160});
#endif
    }

private:
    float m_cutoff = 5000.0f;
    float m_resonance = 0.0f;
    int   m_type = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// SubSynthDisplayPanel — Composite panel for Subtractive Synth.
// Shows: [Osc1][Osc2][Filter][Amp Env][Filter Env] in a horizontal row.
// ═══════════════════════════════════════════════════════════════════════════

class SubSynthDisplayPanel : public Widget {
public:
    SubSynthDisplayPanel() {
        setName("SubSynthDisplay");
        m_osc1.setLabel("OSC 1");
        m_osc1.setColor(Color{0, 200, 255, 255});
        m_osc2.setLabel("OSC 2");
        m_osc2.setColor(Color{0, 180, 230, 255});
        m_filter.setFilterType(0);
        m_ampAdsr.setLabel("AMP");
        m_ampAdsr.setColor(Color{80, 220, 100, 255});
        m_filtAdsr.setLabel("FILT");
        m_filtAdsr.setColor(Color{220, 200, 60, 255});
    }

    // Update all sub-displays from SubtractiveSynth parameter values.
    // Parameter indices match SubtractiveSynth::Params enum.
    void updateFromParams(
        float osc1Wave, float osc1Level,
        float osc2Wave, float osc2Level,
        float filterCutoff, float filterReso, float filterType,
        float ampA, float ampD, float ampS, float ampR,
        float filtA, float filtD, float filtS, float filtR)
    {
        m_osc1.setWaveform(static_cast<int>(osc1Wave));
        m_osc1.setLevel(osc1Level);
        m_osc2.setWaveform(static_cast<int>(osc2Wave));
        m_osc2.setLevel(osc2Level);
        m_filter.setCutoff(filterCutoff);
        m_filter.setResonance(filterReso);
        m_filter.setFilterType(static_cast<int>(filterType));
        m_ampAdsr.setADSR(ampA, ampD, ampS, ampR);
        m_filtAdsr.setADSR(filtA, filtD, filtS, filtR);
    }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 48.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float gap = 3.0f;
        // 5 panels: 2 narrow oscs, 1 medium filter, 2 medium ADSRs
        // Weight: osc=1, filter=1.5, adsr=1.5 → total=7
        float unit = (bounds.w - 4 * gap) / 7.0f;
        float x = bounds.x;
        m_osc1.layout({x, bounds.y, unit, bounds.h}, ctx);
        x += unit + gap;
        m_osc2.layout({x, bounds.y, unit, bounds.h}, ctx);
        x += unit + gap;
        m_filter.layout({x, bounds.y, unit * 1.5f, bounds.h}, ctx);
        x += unit * 1.5f + gap;
        m_ampAdsr.layout({x, bounds.y, unit * 1.5f, bounds.h}, ctx);
        x += unit * 1.5f + gap;
        m_filtAdsr.layout({x, bounds.y, unit * 1.5f, bounds.h}, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        m_osc1.paint(ctx);
        m_osc2.paint(ctx);
        m_filter.paint(ctx);
        m_ampAdsr.paint(ctx);
        m_filtAdsr.paint(ctx);
#endif
    }

private:
    OscDisplayWidget    m_osc1, m_osc2;
    FilterDisplayWidget m_filter;
    ADSRDisplayWidget   m_ampAdsr, m_filtAdsr;
};

// ═══════════════════════════════════════════════════════════════════════════
// CustomDeviceBody — Abstract base for custom instrument body layouts.
// Replaces the flat knob grid in DeviceWidget when set.
// ═══════════════════════════════════════════════════════════════════════════

class CustomDeviceBody : public Widget {
public:
    virtual ~CustomDeviceBody() = default;
    virtual void updateParamValue(int index, float value) = 0;
    virtual float preferredBodyWidth() const = 0;
    virtual void setOnParamChange(std::function<void(int, float)> cb) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Utility: format a parameter value for display (matches DeviceWidget format)
// ═══════════════════════════════════════════════════════════════════════════

inline std::string formatParamValue(float value, const std::string& unit, bool isBoolean) {
    if (isBoolean) return value > 0.5f ? "On" : "Off";
    char buf[32];
    if (unit == "Hz") {
        if (value >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.1fk", value / 1000.0f);
        else
            std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else if (unit == "dB") {
        std::snprintf(buf, sizeof(buf), "%.1f", value);
    } else if (unit == "ms") {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else if (unit == "%") {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
    } else if (unit == "s") {
        if (value < 0.1f) std::snprintf(buf, sizeof(buf), "%.0fms", value * 1000.0f);
        else              std::snprintf(buf, sizeof(buf), "%.2fs", value);
    } else if (unit == "x") {
        std::snprintf(buf, sizeof(buf), "%.1fx", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
    }
    return buf;
}

// ═══════════════════════════════════════════════════════════════════════════
// GroupedKnobBody — Lays out knobs in named sections with an optional
// inline display widget on the left.  Replaces the flat FwGrid for
// instruments that benefit from visual grouping.
//
// Layout:  [Display?]  |  [Section1]  |  [Section2]  |  ...
// Each section:  label at top, knobs in 2-row grid below, separator line.
// ═══════════════════════════════════════════════════════════════════════════

class GroupedKnobBody : public CustomDeviceBody {
public:
    struct ParamDesc {
        int index;
        std::string name, unit;
        float minVal, maxVal, defaultVal;
        bool isBoolean;
    };
    struct SectionDef {
        std::string label;
        std::vector<int> paramIndices;
    };
    struct Config {
        Widget* display = nullptr;    // optional left-side display (ownership transferred)
        float displayWidth = 0;
        std::vector<SectionDef> sections;
    };

    GroupedKnobBody() { setName("GroupedKnobBody"); }

    ~GroupedKnobBody() {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                delete ke.knob;
        delete m_display;
    }

    void configure(const Config& config, const std::vector<ParamDesc>& allParams) {
        m_display = config.display;
        m_displayWidth = config.displayWidth;

        for (auto& sd : config.sections) {
            InternalSection sec;
            sec.label = sd.label;
            for (int idx : sd.paramIndices) {
                if (idx < 0 || idx >= static_cast<int>(allParams.size())) continue;
                auto& pd = allParams[idx];
                auto* k = new FwKnob();
                k->setName(std::to_string(pd.index));
                k->setRange(pd.minVal, pd.maxVal);
                k->setDefault(pd.defaultVal);
                k->setValue(pd.defaultVal);
                k->setLabel(pd.name);
                k->setBoolean(pd.isBoolean);

                // Detect integer-range params and set step for snapping
                float range = pd.maxVal - pd.minVal;
                bool isInteger = (range > 0 && range <= 32 &&
                                  pd.minVal == std::floor(pd.minVal) &&
                                  pd.maxVal == std::floor(pd.maxVal) &&
                                  pd.unit.empty() && !pd.isBoolean);
                if (isInteger) {
                    k->setStep(1.0f);
                    k->setFormatCallback([](float v) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(v)));
                        return std::string(buf);
                    });
                } else {
                    k->setFormatCallback([unit = pd.unit, isBool = pd.isBoolean](float v) {
                        return formatParamValue(v, unit, isBool);
                    });
                }

                k->setOnChange([this, pidx = pd.index](float v) {
                    if (m_onParamChange) m_onParamChange(pidx, v);
                });
                sec.knobs.push_back({pd.index, k});
            }
            m_sections.push_back(std::move(sec));
        }
    }

    void updateParamValue(int index, float value) override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.paramIndex == index) { ke.knob->setValue(value); return; }
    }

    float preferredBodyWidth() const override {
        float w = 0;
        if (m_display) w += m_displayWidth + kSectionGap;
        for (auto& sec : m_sections)
            w += sectionWidth(sec) + kSectionGap;
        return w;
    }

    void setOnParamChange(std::function<void(int, float)> cb) override {
        m_onParamChange = std::move(cb);
    }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({preferredBodyWidth(), c.maxH});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float x = bounds.x;
        float y = bounds.y;
        float h = bounds.h;

        if (m_display) {
            m_display->layout({x, y, m_displayWidth, h}, ctx);
            x += m_displayWidth + kSectionGap;
        }

        for (auto& sec : m_sections) {
            int cols = sectionCols(sec);
            float secW = sectionWidth(sec);
            sec.x = x;
            sec.w = secW;

            float knobY = y + kLabelH;
            for (size_t i = 0; i < sec.knobs.size(); ++i) {
                int col = static_cast<int>(i) % cols;
                int row = static_cast<int>(i) / cols;
                float kx = x + kPadX + col * kCellW;
                float ky = knobY + row * kRowH;
                sec.knobs[i].knob->layout({kx, ky, kKnobW, kKnobH}, ctx);
            }

            x += secW + kSectionGap;
        }
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        if (m_display) m_display->paint(ctx);

        float lblScale = 8.0f / Theme::kFontSize;
        bool first = true;
        for (auto& sec : m_sections) {
            // Section label
            if (!sec.label.empty())
                f.drawText(r, sec.label.c_str(),
                           sec.x + kPadX, m_bounds.y + 1,
                           lblScale, Color{120, 160, 220, 200});

            // Separator line (left edge, skip first)
            if (!first)
                r.drawRect(sec.x - kSectionGap / 2, m_bounds.y + 2,
                           1, m_bounds.h - 4, Color{55, 55, 65, 150});
            first = false;

            for (auto& ke : sec.knobs)
                ke.knob->paint(ctx);
        }
#endif
    }

    bool onMouseDown(MouseEvent& e) override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.knob->bounds().contains(e.x, e.y))
                    return ke.knob->onMouseDown(e);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.knob->bounds().contains(e.x, e.y))
                    return ke.knob->onMouseUp(e);
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.knob->onMouseMove(e)) return true;
        return false;
    }

private:
    static constexpr float kKnobW      = 52.0f;
    static constexpr float kKnobH      = 64.0f;
    static constexpr float kCellW      = 62.0f;
    static constexpr float kRowH       = 68.0f;
    static constexpr float kLabelH     = 13.0f;
    static constexpr float kSectionGap = 14.0f;
    static constexpr float kPadX       = 6.0f;
    static constexpr int   kMaxRows    = 2;

    struct KnobEntry { int paramIndex; FwKnob* knob; };
    struct InternalSection {
        std::string label;
        std::vector<KnobEntry> knobs;
        float x = 0, w = 0;
    };

    Widget* m_display = nullptr;
    float   m_displayWidth = 0;
    std::vector<InternalSection> m_sections;
    std::function<void(int, float)> m_onParamChange;

    static int sectionCols(const InternalSection& s) {
        int n = static_cast<int>(s.knobs.size());
        return n <= kMaxRows ? 1 : (n + kMaxRows - 1) / kMaxRows;
    }
    static float sectionWidth(const InternalSection& s) {
        return 2 * kPadX + sectionCols(s) * kCellW;
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
