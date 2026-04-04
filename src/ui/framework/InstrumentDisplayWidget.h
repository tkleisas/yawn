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
        float areaX = m_bounds.x + 4;
        float areaY = m_bounds.y + 14;
        float areaW = m_bounds.w - 8;
        float areaH = m_bounds.h - 28;
        if (areaW < 40 || areaH < 20) return;

        constexpr float boxW = 28, boxH = 22;
        float opX[4], opY[4];
        layoutOperators(algo, areaX, areaY, areaW, areaH, boxW, boxH, opX, opY);

        // Draw connections (modulator → destination)
        Color connCol{220, 150, 50, 200};
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
            float endY = std::min(by + 10, m_bounds.y + m_bounds.h - 4);
            for (float py = by + 1; py < endY; py += 2)
                r.drawRect(cx - 0.5f, py, 2.0f, 1.5f, outCol);
            r.drawTriangle(cx - 3, endY - 1, cx + 3, endY - 1, cx, endY + 2, outCol);
        }

        // Feedback indicator on operator 4 (index 3)
        if (m_feedback > 0.01f) {
            Color fbCol{255, 200, 60, 200};
            float rx = opX[3] + boxW + 2;
            float ry = opY[3] + boxH * 0.5f;
            // Self-loop: right side, going up and curving back
            for (int s = 0; s < 5; ++s)
                r.drawRect(rx + s * 1.5f, ry - s * 2.0f, 2.0f, 2.0f, fbCol);
            float topY = ry - 10;
            for (float px = rx + 6; px >= opX[3] + boxW * 0.7f; px -= 2)
                r.drawRect(px, topY, 2.0f, 2.0f, fbCol);
            r.drawTriangle(opX[3] + boxW * 0.7f - 1, topY - 2,
                           opX[3] + boxW * 0.7f - 1, topY + 4,
                           opX[3] + boxW * 0.7f - 4, topY + 1, fbCol);
        }

        // Draw operator boxes (on top of connections)
        float numScale = 10.0f / Theme::kFontSize;
        float numH = f.lineHeight(numScale);
        for (int op = 0; op < 4; ++op) {
            bool isCarrier = algo.carrier[op];
            float lvl = m_opLevel[op];
            Color boxBg = isCarrier
                ? Color{static_cast<uint8_t>(25 + lvl * 45), static_cast<uint8_t>(80 + lvl * 40), static_cast<uint8_t>(40 + lvl * 25), 255}
                : Color{static_cast<uint8_t>(75 + lvl * 40), static_cast<uint8_t>(50 + lvl * 30), static_cast<uint8_t>(20 + lvl * 20), 255};
            r.drawRoundedRect(opX[op], opY[op], boxW, boxH, 3.0f, boxBg);
            Color border = isCarrier ? Color{80, 220, 100, 230} : Color{230, 160, 60, 230};
            r.drawRectOutline(opX[op], opY[op], boxW, boxH, border);

            // Operator number centered inside box
            char label[4];
            std::snprintf(label, sizeof(label), "%d", op + 1);
            float tw = f.textWidth(label, numScale);
            float tx = opX[op] + (boxW - tw) * 0.5f;
            float ty = opY[op] + (boxH - numH) * 0.5f - numH * 0.1f;
            f.drawText(r, label, tx, ty, numScale, Color{240, 240, 255, 240});
        }

        // "OUT" label at bottom
        float outLblScale = 7.0f / Theme::kFontSize;
        float outTw = f.textWidth("OUT", outLblScale);
        f.drawText(r, "OUT", areaX + (areaW - outTw) / 2,
                   m_bounds.y + m_bounds.h - 12, outLblScale, outCol);
    }

    void layoutOperators(const AlgoDef& algo, float areaX, float areaY,
                         float areaW, float areaH, float boxW, float boxH,
                         float* opX, float* opY) {
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
            ? std::min((areaH - boxH) / maxDepth, boxH + 8.0f)
            : 0.0f;
        float hGap = 8.0f;

        for (int op = 0; op < 4; ++op) {
            int d = depth[op];
            int cnt = countAt[d];
            int idx = idxAt[d]++;
            float totalW = cnt * boxW + (cnt - 1) * hGap;
            float startX = areaX + (areaW - totalW) / 2;
            opX[op] = startX + idx * (boxW + hGap);
            opY[op] = areaY + (maxDepth - d) * vGap;
        }
    }

    void drawArrow(Renderer2D& r, float x1, float y1, float x2, float y2, Color col) {
        float dx = x2 - x1, dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 2) return;
        int steps = std::max(3, static_cast<int>(len / 2.0f));
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            r.drawRect(x1 + dx * t - 0.75f, y1 + dy * t - 0.75f, 2.0f, 2.0f, col);
        }
        r.drawTriangle(x2 - 3, y2 - 2, x2 + 3, y2 - 2, x2, y2 + 2, col);
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
        int steps = std::max(4, static_cast<int>(std::abs(dx) / 2.0f));
        float prevX = x1, prevY = y1;
        for (int i = 1; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            float px = x1 + dx * t;
            float dy = y2 - y1;
            float pyl;
            if (exponential && std::abs(dy) > 1)
                pyl = y1 + dy * (1.0f - std::exp(-t * 3.5f)) / (1.0f - std::exp(-3.5f));
            else
                pyl = y1 + dy * t;
            r.drawLine(prevX, prevY, px, pyl, m_color, 1.5f);
            prevX = px;
            prevY = pyl;
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
        float prevX = wx, prevY = midY;
        for (int i = 0; i <= steps; ++i) {
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
            float curX = wx + static_cast<float>(i);
            float curY = midY - val * amp;
            if (i > 0)
                r.drawLine(prevX, prevY, curX, curY, col, 1.5f);
            prevX = curX;
            prevY = curY;
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
        float prevX = px, prevY = py + ph;
        for (int i = 0; i <= steps; ++i) {
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
            float curX = px + static_cast<float>(i);
            float curY = py + ph * (1.0f - mag * 0.75f);
            if (i > 0)
                r.drawLine(prevX, prevY, curX, curY, lineCol, 1.5f);
            prevX = curX;
            prevY = curY;
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
// 2-row layout: [Osc1 | Osc2 | Filter] top, [Amp Env | Filter Env] bottom.
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
        return c.constrain({c.maxW, 96.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float gap = 3.0f;
        float halfH = (bounds.h - gap) * 0.5f;

        // Top row: Osc1, Osc2, Filter (equal width)
        float topUnit = (bounds.w - 2 * gap) / 3.0f;
        float x = bounds.x;
        m_osc1.layout({x, bounds.y, topUnit, halfH}, ctx);
        x += topUnit + gap;
        m_osc2.layout({x, bounds.y, topUnit, halfH}, ctx);
        x += topUnit + gap;
        m_filter.layout({x, bounds.y, topUnit, halfH}, ctx);

        // Bottom row: AmpADSR, FiltADSR (equal width)
        float botUnit = (bounds.w - gap) * 0.5f;
        float botY = bounds.y + halfH + gap;
        m_ampAdsr.layout({bounds.x, botY, botUnit, halfH}, ctx);
        m_filtAdsr.layout({bounds.x + botUnit + gap, botY, botUnit, halfH}, ctx);
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
// SamplerDisplayPanel — Composite panel for Sampler instrument.
// Top: sample waveform display (or "Drop Sample" placeholder).
// Bottom: small ADSR envelope display.
// ═══════════════════════════════════════════════════════════════════════════

class SamplerDisplayPanel : public Widget {
public:
    SamplerDisplayPanel() {
        setName("SamplerDisplay");
        m_adsr.setLabel("AMP");
        m_adsr.setColor(Color{80, 220, 100, 255});
    }

    void setADSR(float a, float d, float s, float rel) {
        m_adsr.setADSR(a, d, s, rel);
    }

    void setSampleData(const float* data, int frames, int channels) {
        m_sampleData = data;
        m_sampleFrames = frames;
        m_sampleChannels = channels;
    }

    void setLoopPoints(float start, float end) {
        m_loopStart = start;
        m_loopEnd = end;
    }

    void setPlayhead(float pos, bool playing) {
        m_playhead = pos;
        m_playing = playing;
    }

    void setReverse(bool rev) { m_reverse = rev; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 96.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float gap = 3.0f;
        float adsrH = std::min(40.0f, bounds.h * 0.35f);
        float waveH = bounds.h - adsrH - gap;
        m_waveRect = {bounds.x, bounds.y, bounds.w, waveH};
        m_adsr.layout({bounds.x, bounds.y + waveH + gap, bounds.w, adsrH}, ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        // Waveform area background
        r.drawRect(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                          Color{50, 50, 60, 255});

        float lblScale = 7.0f / Theme::kFontSize;
        const char* label = m_reverse ? "SAMPLE \xe2\x97\x80" : "SAMPLE";
        f.drawText(r, label, m_waveRect.x + 3, m_waveRect.y + 1,
                   lblScale, Theme::textDim);

        if (m_sampleData && m_sampleFrames > 0) {
            float px = m_waveRect.x + 2;
            float py = m_waveRect.y + 10;
            float pw = m_waveRect.w - 4;
            float ph = m_waveRect.h - 12;
            if (pw > 10 && ph > 6) {
                // Loop region highlight
                float lsX = px + m_loopStart * pw;
                float leX = px + m_loopEnd * pw;
                bool hasLoop = (m_loopStart > 0.001f || m_loopEnd < 0.999f);
                if (hasLoop) {
                    // Dim outside loop
                    if (lsX > px)
                        r.drawRect(px, py, lsX - px, ph, Color{10, 10, 14, 150});
                    if (leX < px + pw)
                        r.drawRect(leX, py, px + pw - leX, ph, Color{10, 10, 14, 150});
                    // Loop boundaries
                    r.drawRect(lsX, py, 1, ph, Color{255, 200, 50, 160});
                    r.drawRect(leX, py, 1, ph, Color{255, 200, 50, 160});
                }

                // Draw waveform envelope (first channel)
                float midY = py + ph * 0.5f;
                float halfH = ph * 0.5f;
                int numBars = static_cast<int>(pw);
                if (numBars < 1) numBars = 1;
                Color waveCol{0, 180, 230, 200};
                for (int i = 0; i < numBars; ++i) {
                    int startFrame = (i * m_sampleFrames) / numBars;
                    int endFrame = ((i + 1) * m_sampleFrames) / numBars;
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int s = startFrame; s < endFrame; ++s) {
                        float v = m_sampleData[s * m_sampleChannels];
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                    float top = midY - maxVal * halfH;
                    float bot = midY - minVal * halfH;
                    float barH = bot - top;
                    if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }
                    r.drawRect(px + i, top, 1, barH, waveCol);
                }
                // Center line
                r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 100});

                // Playhead
                if (m_playing && m_playhead > 0.0f) {
                    float phX = px + m_playhead * pw;
                    if (phX >= px && phX <= px + pw) {
                        r.drawRect(phX, py, 1.5f, ph, Color{255, 255, 255, 200});
                    }
                }
            }
        } else {
            // "Drop Sample" placeholder
            const char* msg = "Drop Sample";
            float tw = f.textWidth(msg, lblScale);
            float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            float ty = m_waveRect.y + (m_waveRect.h) * 0.5f - 4;
            f.drawText(r, msg, tx, ty, lblScale, Color{80, 80, 100, 180});
        }

        // ADSR below
        m_adsr.paint(ctx);
#endif
    }

private:
    ADSRDisplayWidget m_adsr;
    Rect m_waveRect{};
    const float* m_sampleData = nullptr;
    int m_sampleFrames = 0;
    int m_sampleChannels = 1;
    float m_loopStart = 0.0f, m_loopEnd = 1.0f;
    float m_playhead = 0.0f;
    bool m_playing = false;
    bool m_reverse = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// DrumSlopDisplayPanel — Composite panel for DrumSlop instrument.
// Top: loop waveform with slice markers.  Bottom: 4×4 (or 2×8) pad grid
// with note names and active/playing highlights.
// ═══════════════════════════════════════════════════════════════════════════

class DrumSlopDisplayPanel : public Widget {
public:
    DrumSlopDisplayPanel() { setName("DrumSlopDisplay"); }

    void setLoopData(const float* data, int frames, int channels) {
        m_loopData = data;
        m_loopFrames = frames;
        m_loopChannels = channels;
    }

    void setSliceCount(int count) { m_sliceCount = std::clamp(count, 1, 16); }

    // Slice boundaries as frame positions (count+1 entries: start of each + end)
    void setSliceBoundaries(const std::vector<int64_t>& bounds) {
        m_sliceBounds = bounds;
    }

    void setBaseNote(int note) { m_baseNote = std::clamp(note, 0, 127); }
    void setSelectedPad(int idx) { m_selectedPad = std::clamp(idx, 0, 15); }

    void setPadPlaying(int idx, bool playing) {
        if (idx >= 0 && idx < 16) m_padPlaying[idx] = playing;
    }

    // Callback when a pad is clicked in the UI
    void setOnPadClick(std::function<void(int)> cb) { m_onPadClick = std::move(cb); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 160.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        (void)ctx;
        m_bounds = bounds;
        float gap = 2.0f;
        m_waveH = std::min(40.0f, bounds.h * 0.35f);
        m_waveRect = {bounds.x, bounds.y, bounds.w, m_waveH};
        m_gridRect = {bounds.x, bounds.y + m_waveH + gap,
                      bounds.w, bounds.h - m_waveH - gap};
    }

    bool onMouseDown(MouseEvent& e) override {
        if (!e.isLeftButton()) return false;
        float mx = e.x, my = e.y;
        // Check if click is in the pad grid
        if (mx >= m_gridRect.x && mx < m_gridRect.x + m_gridRect.w &&
            my >= m_gridRect.y && my < m_gridRect.y + m_gridRect.h) {
            int cols = 4, rows = 4;
            float cellW = m_gridRect.w / cols;
            float cellH = m_gridRect.h / rows;
            int col = static_cast<int>((mx - m_gridRect.x) / cellW);
            int row = static_cast<int>((my - m_gridRect.y) / cellH);
            int padIdx = row * cols + col;
            if (padIdx >= 0 && padIdx < m_sliceCount && m_onPadClick)
                m_onPadClick(padIdx);
            return true;
        }
        return false;
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        // ─── Waveform area ───
        r.drawRect(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                          Color{50, 50, 60, 255});

        if (m_loopData && m_loopFrames > 0) {
            float px = m_waveRect.x + 2;
            float py = m_waveRect.y + 2;
            float pw = m_waveRect.w - 4;
            float ph = m_waveRect.h - 4;

            // Draw waveform (min/max envelope)
            if (pw > 4 && ph > 4) {
                float midY = py + ph * 0.5f;
                float halfH = ph * 0.5f;
                int numBars = static_cast<int>(pw);
                if (numBars < 1) numBars = 1;
                Color waveCol{0, 180, 230, 180};
                for (int i = 0; i < numBars; ++i) {
                    int startFrame = (i * m_loopFrames) / numBars;
                    int endFrame = ((i + 1) * m_loopFrames) / numBars;
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int s = startFrame; s < endFrame; ++s) {
                        float v = m_loopData[s * m_loopChannels];
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                    float top = midY - maxVal * halfH;
                    float bot = midY - minVal * halfH;
                    float barH = bot - top;
                    if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }
                    r.drawRect(px + i, top, 1, barH, waveCol);
                }
                r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 80});

                // Slice marker lines
                if (!m_sliceBounds.empty()) {
                    Color sliceCol{255, 200, 50, 140};
                    for (int i = 1; i < (int)m_sliceBounds.size() - 1; ++i) {
                        float xPos = px + (float)m_sliceBounds[i] / m_loopFrames * pw;
                        r.drawRect(xPos, py, 1, ph, sliceCol);
                    }
                }

                // Highlight selected pad's slice region
                if (m_selectedPad >= 0 && m_selectedPad < (int)m_sliceBounds.size() - 1) {
                    float selStart = px + (float)m_sliceBounds[m_selectedPad] / m_loopFrames * pw;
                    float selEnd = px + (float)m_sliceBounds[m_selectedPad + 1] / m_loopFrames * pw;
                    r.drawRect(selStart, py, selEnd - selStart, ph,
                               Color{255, 200, 50, 30});
                }
            }
        } else {
            float lblScale = 7.0f / Theme::kFontSize;
            const char* msg = "Drop Loop";
            float tw = f.textWidth(msg, lblScale);
            float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            f.drawText(r, msg, tx, ty, lblScale, Color{80, 80, 100, 180});
        }

        // ─── Pad grid (4 rows × 4 cols) ───
        int cols = 4, rows = 4;
        float cellW = m_gridRect.w / cols;
        float cellH = m_gridRect.h / rows;
        float lblScale = 6.5f / Theme::kFontSize;

        static const char* noteNames[] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };

        for (int i = 0; i < 16; ++i) {
            int col = i % cols;
            int row = i / cols;
            float cx = m_gridRect.x + col * cellW;
            float cy = m_gridRect.y + row * cellH;

            bool active = i < m_sliceCount;
            bool selected = (i == m_selectedPad);
            bool playing = m_padPlaying[i];

            // Pad background
            Color bg{35, 35, 42, 255};
            if (!active) bg = Color{25, 25, 30, 255};
            else if (playing) bg = Color{50, 90, 50, 255};
            else if (selected) bg = Color{55, 55, 70, 255};
            r.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2, bg);

            // Border
            Color border = selected ? Color{255, 200, 50, 200} :
                           playing  ? Color{80, 200, 80, 200}  :
                                      Color{60, 60, 75, 255};
            r.drawRectOutline(cx + 1, cy + 1, cellW - 2, cellH - 2, border);

            if (active) {
                // Note name (e.g. "C2", "D#3")
                int midiNote = m_baseNote + i;
                int octave = (midiNote / 12) - 1;
                int noteIdx = midiNote % 12;
                char label[8];
                std::snprintf(label, sizeof(label), "%s%d",
                              noteNames[noteIdx], octave);
                float tw = f.textWidth(label, lblScale);
                float tx = cx + (cellW - tw) * 0.5f;
                float ty = cy + (cellH) * 0.5f - 3.5f;
                Color textCol = selected ? Color{255, 220, 100, 255} :
                                           Color{160, 160, 180, 255};
                f.drawText(r, label, tx, ty, lblScale, textCol);

                // Pad number (small, top-left)
                char numBuf[4];
                std::snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
                f.drawText(r, numBuf, cx + 3, cy + 2,
                           5.5f / Theme::kFontSize, Color{80, 80, 100, 150});
            }
        }
#endif
    }

private:
    Rect m_waveRect{}, m_gridRect{};
    float m_waveH = 40.0f;

    const float* m_loopData = nullptr;
    int m_loopFrames = 0;
    int m_loopChannels = 1;

    int m_sliceCount = 8;
    std::vector<int64_t> m_sliceBounds;
    int m_baseNote = 36;
    int m_selectedPad = 0;
    bool m_padPlaying[16] = {};

    std::function<void(int)> m_onPadClick;
};

// ═══════════════════════════════════════════════════════════════════════════
// DrumRackDisplayPanel — 128-pad drum machine with paged 4×4 grid.
// Shows selected pad's sample waveform at top, pad grid below with page nav.
// ═══════════════════════════════════════════════════════════════════════════

class DrumRackDisplayPanel : public Widget {
public:
    DrumRackDisplayPanel() { setName("DrumRackDisplay"); }

    void setSelectedPad(int note) { m_selectedPad = std::clamp(note, 0, 127); }
    void setPage(int page) { m_page = std::clamp(page, 0, 7); }
    int  page() const { return m_page; }

    void setPadHasSample(int note, bool has) {
        if (note >= 0 && note < 128) m_padHasSample[note] = has;
    }

    void setPadPlaying(int note, bool playing) {
        if (note >= 0 && note < 128) m_padPlaying[note] = playing;
    }

    // Sample waveform for the selected pad
    void setSelectedPadWaveform(const float* data, int frames, int channels) {
        m_waveData = data;
        m_waveFrames = frames;
        m_waveChannels = channels;
    }

    void setOnPadClick(std::function<void(int)> cb) { m_onPadClick = std::move(cb); }
    void setOnPageChange(std::function<void(int)> cb) { m_onPageChange = std::move(cb); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 160.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        (void)ctx;
        m_bounds = bounds;
        float gap = 2.0f;
        m_waveH = std::min(36.0f, bounds.h * 0.25f);
        m_pageBarH = 16.0f;
        m_waveRect = {bounds.x, bounds.y, bounds.w, m_waveH};
        m_pageBarRect = {bounds.x, bounds.y + m_waveH + gap, bounds.w, m_pageBarH};
        m_gridRect = {bounds.x, bounds.y + m_waveH + m_pageBarH + gap * 2,
                      bounds.w, bounds.h - m_waveH - m_pageBarH - gap * 2};
    }

    bool onMouseDown(MouseEvent& e) override {
        if (!e.isLeftButton()) return false;
        float mx = e.x, my = e.y;

        // Page bar clicks
        if (mx >= m_pageBarRect.x && mx < m_pageBarRect.x + m_pageBarRect.w &&
            my >= m_pageBarRect.y && my < m_pageBarRect.y + m_pageBarRect.h) {
            // < button
            float btnW = 14.0f;
            if (mx < m_pageBarRect.x + btnW) {
                int newPage = std::max(0, m_page - 1);
                if (newPage != m_page) {
                    m_page = newPage;
                    if (m_onPageChange) m_onPageChange(m_page);
                }
                return true;
            }
            // > button
            if (mx > m_pageBarRect.x + m_pageBarRect.w - btnW) {
                int newPage = std::min(7, m_page + 1);
                if (newPage != m_page) {
                    m_page = newPage;
                    if (m_onPageChange) m_onPageChange(m_page);
                }
                return true;
            }
            // Page buttons
            float innerW = m_pageBarRect.w - btnW * 2;
            float pageW = innerW / 8.0f;
            int clickedPage = static_cast<int>((mx - m_pageBarRect.x - btnW) / pageW);
            clickedPage = std::clamp(clickedPage, 0, 7);
            if (clickedPage != m_page) {
                m_page = clickedPage;
                if (m_onPageChange) m_onPageChange(m_page);
            }
            return true;
        }

        // Pad grid clicks
        if (mx >= m_gridRect.x && mx < m_gridRect.x + m_gridRect.w &&
            my >= m_gridRect.y && my < m_gridRect.y + m_gridRect.h) {
            int cols = 4, rows = 4;
            float cellW = m_gridRect.w / cols;
            float cellH = m_gridRect.h / rows;
            int col = static_cast<int>((mx - m_gridRect.x) / cellW);
            int row = static_cast<int>((my - m_gridRect.y) / cellH);
            int padIdx = row * cols + col;
            int note = m_page * 16 + padIdx;
            if (note >= 0 && note < 128 && m_onPadClick)
                m_onPadClick(note);
            return true;
        }
        return false;
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        // ─── Waveform area (selected pad's sample) ───
        r.drawRect(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_waveRect.x, m_waveRect.y, m_waveRect.w, m_waveRect.h,
                          Color{50, 50, 60, 255});

        if (m_waveData && m_waveFrames > 0) {
            float px = m_waveRect.x + 2;
            float py = m_waveRect.y + 2;
            float pw = m_waveRect.w - 4;
            float ph = m_waveRect.h - 4;

            if (pw > 4 && ph > 4) {
                float midY = py + ph * 0.5f;
                float halfH = ph * 0.5f;
                int numBars = static_cast<int>(pw);
                if (numBars < 1) numBars = 1;
                Color waveCol{0, 180, 230, 180};
                for (int i = 0; i < numBars; ++i) {
                    int startFrame = (i * m_waveFrames) / numBars;
                    int endFrame = ((i + 1) * m_waveFrames) / numBars;
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int s = startFrame; s < endFrame; ++s) {
                        float v = m_waveData[s * m_waveChannels];
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                    float top = midY - maxVal * halfH;
                    float bot = midY - minVal * halfH;
                    float barH = bot - top;
                    if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }
                    r.drawRect(px + i, top, 1, barH, waveCol);
                }
                r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 80});
            }
        } else {
            float lblScale = 7.0f / Theme::kFontSize;
            const char* msg = "Drop Sample";
            float tw = f.textWidth(msg, lblScale);
            float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            f.drawText(r, msg, tx, ty, lblScale, Color{80, 80, 100, 180});
        }

        // ─── Page navigation bar ───
        r.drawRect(m_pageBarRect.x, m_pageBarRect.y, m_pageBarRect.w,
                   m_pageBarRect.h, Color{25, 25, 32, 255});

        float btnW = 14.0f;
        float lblScale = 6.0f / Theme::kFontSize;

        // < > arrows
        f.drawText(r, "<", m_pageBarRect.x + 3, m_pageBarRect.y + 2,
                   lblScale, Color{160, 160, 180, 200});
        f.drawText(r, ">", m_pageBarRect.x + m_pageBarRect.w - 10,
                   m_pageBarRect.y + 2, lblScale, Color{160, 160, 180, 200});

        // Page buttons
        float innerW = m_pageBarRect.w - btnW * 2;
        float pageW = innerW / 8.0f;
        static const char* pageLabels[] = {
            "C-1", "C0", "C1", "C2", "C3", "C4", "C5", "C6"
        };
        for (int i = 0; i < 8; ++i) {
            float px = m_pageBarRect.x + btnW + i * pageW;
            bool active = (i == m_page);
            Color bg = active ? Color{60, 60, 80, 255} : Color{30, 30, 38, 255};
            r.drawRect(px + 1, m_pageBarRect.y + 1, pageW - 2, m_pageBarH - 2, bg);
            float tw = f.textWidth(pageLabels[i], lblScale);
            float tx = px + (pageW - tw) * 0.5f;
            Color tc = active ? Color{255, 200, 50, 255} : Color{120, 120, 140, 180};
            f.drawText(r, pageLabels[i], tx, m_pageBarRect.y + 2, lblScale, tc);
        }

        // ─── Pad grid (4 rows × 4 cols for current page) ───
        int cols = 4, rows = 4;
        float cellW = m_gridRect.w / cols;
        float cellH = m_gridRect.h / rows;
        float padLblScale = 6.5f / Theme::kFontSize;

        static const char* noteNames[] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };

        int baseNote = m_page * 16;
        for (int i = 0; i < 16; ++i) {
            int note = baseNote + i;
            if (note >= 128) break;
            int col = i % cols;
            int row = i / cols;
            float cx = m_gridRect.x + col * cellW;
            float cy = m_gridRect.y + row * cellH;

            bool hasSample = m_padHasSample[note];
            bool selected = (note == m_selectedPad);
            bool playing = m_padPlaying[note];

            // Pad background
            Color bg{35, 35, 42, 255};
            if (playing) bg = Color{50, 90, 50, 255};
            else if (selected) bg = Color{55, 55, 70, 255};
            else if (hasSample) bg = Color{40, 40, 52, 255};
            r.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2, bg);

            // Border
            Color border = selected ? Color{255, 200, 50, 200} :
                           playing  ? Color{80, 200, 80, 200}  :
                           hasSample ? Color{70, 70, 90, 255}  :
                                       Color{50, 50, 60, 255};
            r.drawRectOutline(cx + 1, cy + 1, cellW - 2, cellH - 2, border);

            // Note name
            int octave = (note / 12) - 1;
            int noteIdx = note % 12;
            char label[8];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIdx], octave);
            float tw = f.textWidth(label, padLblScale);
            float tx = cx + (cellW - tw) * 0.5f;
            float ty = cy + cellH * 0.5f - 3.5f;
            Color textCol = selected ? Color{255, 220, 100, 255} :
                            hasSample ? Color{180, 180, 200, 255} :
                                        Color{100, 100, 120, 180};
            f.drawText(r, label, tx, ty, padLblScale, textCol);

            // Small sample indicator dot (top-right corner if has sample)
            if (hasSample) {
                r.drawRect(cx + cellW - 6, cy + 3, 3, 3,
                           Color{0, 180, 230, 180});
            }
        }
#endif
    }

private:
    Rect m_waveRect{}, m_pageBarRect{}, m_gridRect{};
    float m_waveH = 36.0f;
    float m_pageBarH = 16.0f;

    int m_selectedPad = 36;
    int m_page = 2;  // Default page 2 = C1..D#2 (standard GM drum range)

    bool m_padHasSample[128] = {};
    bool m_padPlaying[128] = {};

    const float* m_waveData = nullptr;
    int m_waveFrames = 0;
    int m_waveChannels = 1;

    std::function<void(int)> m_onPadClick;
    std::function<void(int)> m_onPageChange;
};

// ═══════════════════════════════════════════════════════════════════════════
// InstrumentRackDisplayPanel — Shows chain list with key/vel range bars
// ═══════════════════════════════════════════════════════════════════════════

class InstrumentRackDisplayPanel : public Widget {
public:
    InstrumentRackDisplayPanel() { setName("InstrumentRackDisplay"); }

    struct ChainInfo {
        std::string name;       // instrument name (or "Empty")
        uint8_t keyLow = 0, keyHigh = 127;
        uint8_t velLow = 1, velHigh = 127;
        float   volume = 1.0f;
        bool    enabled = true;
    };

    void setChainCount(int n) { m_chainCount = std::clamp(n, 0, 8); }
    void setChain(int i, const ChainInfo& info) {
        if (i >= 0 && i < 8) m_chains[i] = info;
    }
    void setSelectedChain(int i) { m_selected = std::clamp(i, 0, 7); }

    void setOnChainClick(std::function<void(int)> cb) { m_onChainClick = std::move(cb); }
    void setOnAddChain(std::function<void()> cb) { m_onAddChain = std::move(cb); }
    void setOnRemoveChain(std::function<void(int)> cb) { m_onRemoveChain = std::move(cb); }
    void setOnToggleChain(std::function<void(int)> cb) { m_onToggleChain = std::move(cb); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 160.0f});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        (void)ctx;
        m_bounds = bounds;
    }

    bool onMouseDown(MouseEvent& e) override {
        if (!e.isLeftButton()) return false;
        float lx = e.x - m_bounds.x;
        float ly = e.y - m_bounds.y;

        // Add button at the bottom
        float addBtnY = m_bounds.h - 20.0f;
        if (ly >= addBtnY && m_chainCount < 8) {
            if (m_onAddChain) m_onAddChain();
            return true;
        }

        // Chain rows
        float rowH = chainRowHeight();
        float headerH = 14.0f;
        int idx = static_cast<int>((ly - headerH) / rowH);
        if (idx < 0 || idx >= m_chainCount) return false;

        if (lx < 16.0f) {
            if (m_onToggleChain) m_onToggleChain(idx);
            return true;
        }

        if (lx > m_bounds.w - 18.0f) {
            if (m_onRemoveChain) m_onRemoveChain(idx);
            return true;
        }

        if (m_onChainClick) m_onChainClick(idx);
        return true;
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w, h = m_bounds.h;
        float lblScale = 7.0f / Theme::kFontSize;
        float smallScale = 6.0f / Theme::kFontSize;

        r.drawRect(x, y, w, h, Color{30, 30, 35, 255});

        // Header
        f.drawText(r, "Chains", x + 4, y + 2, lblScale, Color{180, 180, 180, 255});
        float headerH = 14.0f;

        float rowH = chainRowHeight();

        for (int i = 0; i < m_chainCount; ++i) {
            float ry = y + headerH + i * rowH;
            const auto& ch = m_chains[i];

            bool sel = (i == m_selected);
            r.drawRect(x, ry, w, rowH - 1,
                       sel ? Color{50, 55, 70, 255} : Color{38, 38, 42, 255});

            // Enable indicator
            Color eDot = ch.enabled ? Color{80, 200, 100, 255} : Color{80, 80, 80, 255};
            float dotCY = ry + rowH * 0.5f;
            r.drawRect(x + 4, dotCY - 3, 7, 7, eDot);

            // Chain number & name
            char label[64];
            snprintf(label, sizeof(label), "%d: %s", i + 1, ch.name.c_str());
            f.drawText(r, label, x + 16, ry + 2, lblScale, Color{220, 220, 220, 255});

            // Key range bar (blue)
            float barX = x + 16;
            float barW = w - 36;
            float barY = ry + 14;
            float barH = 4;
            r.drawRect(barX, barY, barW, barH, Color{25, 25, 30, 255});
            float k0 = ch.keyLow / 127.0f, k1 = ch.keyHigh / 127.0f;
            float kx0 = barX + k0 * barW;
            float kx1 = barX + k1 * barW;
            r.drawRect(kx0, barY, std::max(1.0f, kx1 - kx0), barH, Color{80, 140, 220, 255});

            // Vel range bar (yellow)
            barY += barH + 1;
            r.drawRect(barX, barY, barW, barH, Color{25, 25, 30, 255});
            float v0 = ch.velLow / 127.0f, v1 = ch.velHigh / 127.0f;
            float vx0 = barX + v0 * barW;
            float vx1 = barX + v1 * barW;
            r.drawRect(vx0, barY, std::max(1.0f, vx1 - vx0), barH, Color{220, 160, 50, 255});

            // Remove button
            f.drawText(r, "x", x + w - 14, ry + 2, lblScale, Color{160, 80, 80, 255});
        }

        // Empty state
        if (m_chainCount == 0) {
            f.drawText(r, "No chains", x + w * 0.5f - 24, y + h * 0.5f - 6,
                       lblScale, Color{120, 120, 120, 255});
        }

        // Add button
        float addY = y + h - 20;
        bool canAdd = m_chainCount < 8;
        Color addCol = canAdd ? Color{80, 180, 80, 255} : Color{60, 60, 60, 255};
        r.drawRect(x + 2, addY, w - 4, 18, Color{40, 40, 45, 255});
        const char* addTxt = "+ Add Chain";
        float addTw = f.textWidth(addTxt, smallScale);
        f.drawText(r, addTxt, x + (w - addTw) * 0.5f, addY + 3, smallScale, addCol);
#endif
    }

private:
    float chainRowHeight() const {
        float usable = m_bounds.h - 14.0f - 22.0f;
        int maxRows = std::max(m_chainCount, 1);
        return std::clamp(usable / maxRows, 20.0f, 28.0f);
    }

    Rect m_bounds{};
    int m_chainCount = 0;
    int m_selected = 0;
    ChainInfo m_chains[8];
    std::function<void(int)> m_onChainClick;
    std::function<void()>    m_onAddChain;
    std::function<void(int)> m_onRemoveChain;
    std::function<void(int)> m_onToggleChain;
};

// ═══════════════════════════════════════════════════════════════════════════
// WavetableDisplayPanel — Shows the current wavetable frame waveform.
// Updates dynamically as Table/Position parameters change.
// ═══════════════════════════════════════════════════════════════════════════

class WavetableDisplayPanel : public Widget {
public:
    WavetableDisplayPanel() { setName("WavetableDisplay"); }

    void setWaveformData(const float* data, int size) {
        m_waveData = data;
        m_waveSize = size;
    }

    void setPosition(float pos) { m_position = std::clamp(pos, 0.0f, 1.0f); }
    void setTableName(const char* name) { m_tableName = name ? name : ""; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 80.0f});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        m_waveRect = {bounds.x + 2, bounds.y + 12, bounds.w - 4, bounds.h - 14};
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float lblScale = 7.0f / Theme::kFontSize;

        // Background
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        // Label
        f.drawText(r, m_tableName.empty() ? "WAVETABLE" : m_tableName.c_str(),
                   m_bounds.x + 3, m_bounds.y + 1, lblScale, Theme::textDim);

        float px = m_waveRect.x, py = m_waveRect.y;
        float pw = m_waveRect.w, ph = m_waveRect.h;

        if (m_waveData && m_waveSize > 0 && pw > 4 && ph > 4) {
            float midY = py + ph * 0.5f;
            float halfH = ph * 0.45f;

            // Center line
            r.drawRect(px, midY, pw, 1, Color{40, 40, 50, 100});

            // Draw waveform as connected line segments
            Color waveCol{100, 200, 255, 220};
            int numPts = static_cast<int>(pw);
            if (numPts < 2) numPts = 2;
            for (int i = 0; i < numPts; ++i) {
                float t = static_cast<float>(i) / (numPts - 1);
                float sampleIdx = t * (m_waveSize - 1);
                int idx0 = static_cast<int>(sampleIdx);
                int idx1 = std::min(idx0 + 1, m_waveSize - 1);
                float frac = sampleIdx - idx0;
                float val = m_waveData[idx0] * (1.0f - frac) + m_waveData[idx1] * frac;
                val = std::clamp(val, -1.0f, 1.0f);

                float x = px + i;
                float y = midY - val * halfH;
                r.drawRect(x, y - 0.5f, 1.0f, 1.5f, waveCol);
            }

            // Position indicator bar at bottom
            float posX = px + m_position * pw;
            r.drawRect(posX - 0.5f, py + ph - 3, 2, 3, Color{255, 200, 50, 200});
        } else {
            const char* msg = "No Table";
            float tw = f.textWidth(msg, lblScale);
            float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            f.drawText(r, msg, tx, ty, lblScale, Color{80, 80, 100, 180});
        }
#endif
    }

private:
    Rect m_waveRect{};
    const float* m_waveData = nullptr;
    int m_waveSize = 0;
    float m_position = 0.0f;
    std::string m_tableName;
};

// ═══════════════════════════════════════════════════════════════════════════
// GranularDisplayPanel — Sample waveform display for Granular Synth.
// Shows loaded sample waveform with grain position indicator and scan marker.
// Displays "Drop Sample" placeholder when no sample is loaded.
// ═══════════════════════════════════════════════════════════════════════════

class GranularDisplayPanel : public Widget {
public:
    GranularDisplayPanel() { setName("GranularDisplay"); }

    void setSampleData(const float* data, int frames, int channels) {
        m_sampleData = data;
        m_sampleFrames = frames;
        m_sampleChannels = channels;
    }

    void setPosition(float pos) { m_position = std::clamp(pos, 0.0f, 1.0f); }
    void setScanPosition(float scan) { m_scanPos = scan - std::floor(scan); }
    void setPlaying(bool playing) { m_playing = playing; }
    void setGrainSize(float sizeNorm) { m_grainSizeNorm = std::clamp(sizeNorm, 0.0f, 1.0f); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 80.0f});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        m_waveRect = {bounds.x + 2, bounds.y + 12, bounds.w - 4, bounds.h - 14};
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float lblScale = 7.0f / Theme::kFontSize;

        // Background
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        // Label
        f.drawText(r, "GRAINS", m_bounds.x + 3, m_bounds.y + 1, lblScale, Theme::textDim);

        float px = m_waveRect.x, py = m_waveRect.y;
        float pw = m_waveRect.w, ph = m_waveRect.h;

        if (m_sampleData && m_sampleFrames > 0 && pw > 4 && ph > 4) {
            float midY = py + ph * 0.5f;
            float halfH = ph * 0.5f;

            // Draw waveform envelope (min/max)
            int numBars = static_cast<int>(pw);
            if (numBars < 1) numBars = 1;
            Color waveCol{0, 180, 230, 180};
            for (int i = 0; i < numBars; ++i) {
                int startFrame = (i * m_sampleFrames) / numBars;
                int endFrame = ((i + 1) * m_sampleFrames) / numBars;
                float minVal = 0.0f, maxVal = 0.0f;
                for (int s = startFrame; s < endFrame; ++s) {
                    float v = m_sampleData[s * m_sampleChannels];
                    if (v < minVal) minVal = v;
                    if (v > maxVal) maxVal = v;
                }
                float top = midY - maxVal * halfH;
                float bot = midY - minVal * halfH;
                float barH = bot - top;
                if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }
                r.drawRect(px + i, top, 1, barH, waveCol);
            }

            // Center line
            r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 80});

            // Grain region highlight (position ± grain size)
            if (m_playing) {
                float posX = px + m_position * pw;
                float scanX = px + m_scanPos * pw;
                float combinedPos = m_position + m_scanPos;
                combinedPos -= std::floor(combinedPos);
                float cpX = px + combinedPos * pw;
                float grainW = m_grainSizeNorm * pw;
                float regionStart = cpX - grainW * 0.5f;
                float regionEnd = cpX + grainW * 0.5f;
                if (regionStart < px) regionStart = px;
                if (regionEnd > px + pw) regionEnd = px + pw;
                r.drawRect(regionStart, py, regionEnd - regionStart, ph,
                           Color{255, 150, 50, 40});

                // Position playhead
                r.drawRect(cpX - 0.5f, py, 2, ph, Color{255, 200, 50, 200});
            }
        } else {
            const char* msg = "Drop Sample";
            float tw = f.textWidth(msg, lblScale);
            float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            f.drawText(r, msg, tx, ty, lblScale, Color{80, 80, 100, 180});
        }
#endif
    }

private:
    Rect m_waveRect{};
    const float* m_sampleData = nullptr;
    int m_sampleFrames = 0;
    int m_sampleChannels = 1;
    float m_position = 0.5f;
    float m_scanPos = 0.0f;
    bool m_playing = false;
    float m_grainSizeNorm = 0.1f;
};

// ═══════════════════════════════════════════════════════════════════════════
// VocoderDisplayPanel — Modulator sample waveform display for Vocoder.
// Shows loaded modulator waveform with playhead, or "Drop Modulator" text.
// ═══════════════════════════════════════════════════════════════════════════

class VocoderDisplayPanel : public Widget {
public:
    VocoderDisplayPanel() { setName("VocoderDisplay"); }

    void setModulatorData(const float* data, int frames) {
        m_modData = data;
        m_modFrames = frames;
    }

    void setPlayhead(float pos) { m_playhead = std::clamp(pos, 0.0f, 1.0f); }
    void setPlaying(bool playing) { m_playing = playing; }
    void setBandCount(int bands) { m_bandCount = std::clamp(bands, 4, 32); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 80.0f});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
        float bandH = 10.0f;
        m_waveRect = {bounds.x + 2, bounds.y + 12,
                      bounds.w - 4, bounds.h - 14 - bandH - 2};
        m_bandRect = {bounds.x + 2, bounds.y + bounds.h - bandH - 2,
                      bounds.w - 4, bandH};
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float lblScale = 7.0f / Theme::kFontSize;

        // Background
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        // Label
        f.drawText(r, "MODULATOR", m_bounds.x + 3, m_bounds.y + 1,
                   lblScale, Theme::textDim);

        float px = m_waveRect.x, py = m_waveRect.y;
        float pw = m_waveRect.w, ph = m_waveRect.h;

        if (m_modData && m_modFrames > 0 && pw > 4 && ph > 4) {
            float midY = py + ph * 0.5f;
            float halfH = ph * 0.5f;

            // Draw waveform envelope (min/max, mono)
            int numBars = static_cast<int>(pw);
            if (numBars < 1) numBars = 1;
            Color waveCol{200, 120, 255, 200};
            for (int i = 0; i < numBars; ++i) {
                int startFrame = (i * m_modFrames) / numBars;
                int endFrame = ((i + 1) * m_modFrames) / numBars;
                float minVal = 0.0f, maxVal = 0.0f;
                for (int s = startFrame; s < endFrame; ++s) {
                    float v = m_modData[s];
                    if (v < minVal) minVal = v;
                    if (v > maxVal) maxVal = v;
                }
                float top = midY - maxVal * halfH;
                float bot = midY - minVal * halfH;
                float barH = bot - top;
                if (barH < 0.5f) { top = midY - 0.25f; barH = 0.5f; }
                r.drawRect(px + i, top, 1, barH, waveCol);
            }

            // Center line
            r.drawRect(px, midY, pw, 1, Color{50, 50, 60, 80});

            // Playhead
            if (m_playing && m_playhead > 0.0f) {
                float phX = px + m_playhead * pw;
                if (phX >= px && phX <= px + pw)
                    r.drawRect(phX, py, 1.5f, ph, Color{255, 255, 255, 200});
            }
        } else {
            const char* msg = "Drop Modulator";
            float tw = f.textWidth(msg, lblScale);
            float tx = m_waveRect.x + (m_waveRect.w - tw) * 0.5f;
            float ty = m_waveRect.y + m_waveRect.h * 0.5f - 4;
            f.drawText(r, msg, tx, ty, lblScale, Color{80, 80, 100, 180});
        }

        // Band visualizer (small bar at bottom showing band count)
        if (m_bandRect.w > 4 && m_bandRect.h > 2) {
            float barW = m_bandRect.w / m_bandCount;
            for (int b = 0; b < m_bandCount; ++b) {
                float bx = m_bandRect.x + b * barW;
                float t = static_cast<float>(b) / std::max(1, m_bandCount - 1);
                uint8_t cr = static_cast<uint8_t>(80 + t * 150);
                uint8_t cg = static_cast<uint8_t>(180 - t * 120);
                r.drawRect(bx + 0.5f, m_bandRect.y, barW - 1, m_bandRect.h,
                           Color{cr, cg, 220, 160});
            }
        }
#endif
    }

private:
    Rect m_waveRect{}, m_bandRect{};
    const float* m_modData = nullptr;
    int m_modFrames = 0;
    float m_playhead = 0.0f;
    bool m_playing = false;
    int m_bandCount = 16;
};

// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
// AutomationEnvelopeWidget — Editable breakpoint envelope display.
// Used in clip/track automation views. Shows breakpoints as draggable
// circles connected by lines, with grid background and value axis.
// ═══════════════════════════════════════════════════════════════════════════

class AutomationEnvelopeWidget : public Widget {
public:
    AutomationEnvelopeWidget() { setName("AutoEnvelope"); }

    void setTimeRange(double start, double end) { m_timeStart = start; m_timeEnd = end; }
    void setValueRange(float minV, float maxV) { m_valueMin = minV; m_valueMax = maxV; }

    void setPoints(const std::vector<std::pair<double,float>>& pts) { m_points = pts; }

    // Callbacks for user interaction
    using PointCallback = std::function<void(int idx, double time, float value)>;
    using AddCallback = std::function<void(double time, float value)>;
    using RemoveCallback = std::function<void(int idx)>;

    void setOnPointMove(PointCallback cb) { m_onPointMove = std::move(cb); }
    void setOnPointAdd(AddCallback cb)    { m_onPointAdd = std::move(cb); }
    void setOnPointRemove(RemoveCallback cb) { m_onPointRemove = std::move(cb); }

    int dragIndex() const { return m_dragIdx; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({200.0f, 60.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w, h = m_bounds.h;

        // Background
        r.drawRect(x, y, w, h, Color{20, 20, 25, 255});
        r.drawRectOutline(x, y, w, h, Color{50, 50, 55, 255});

        // Grid lines (time divisions)
        double timeSpan = m_timeEnd - m_timeStart;
        if (timeSpan > 0) {
            double step = 1.0;
            if (timeSpan > 16) step = 4.0;
            else if (timeSpan > 8) step = 2.0;
            else if (timeSpan > 4) step = 1.0;
            else step = 0.5;

            double t = std::ceil(m_timeStart / step) * step;
            while (t < m_timeEnd) {
                float gx = timeToX(t);
                r.drawRect(gx, y + 1, 1.0f, h - 2, Color{35, 35, 40, 255});
                t += step;
            }
        }

        // Horizontal center line (value = 0.5 for normalized params)
        float midY = valueToY((m_valueMin + m_valueMax) * 0.5f);
        r.drawRect(x + 1, midY, w - 2, 1.0f, Color{40, 40, 48, 255});

        if (m_points.empty()) {
            float ts = 10.0f / f.pixelHeight();
            f.drawText(r, "Click to add points", x + w * 0.3f, y + h * 0.4f, ts,
                       Color{80, 80, 90, 255});
            return;
        }

        // Draw connecting lines between points
        for (size_t i = 0; i + 1 < m_points.size(); ++i) {
            float x0 = timeToX(m_points[i].first);
            float y0 = valueToY(m_points[i].second);
            float x1 = timeToX(m_points[i + 1].first);
            float y1 = valueToY(m_points[i + 1].second);
            drawLine(r, x0, y0, x1, y1, Color{100, 180, 255, 200});
        }

        // Extend line from edges
        if (!m_points.empty()) {
            float firstX = timeToX(m_points.front().first);
            float firstY = valueToY(m_points.front().second);
            if (firstX > x + 1)
                drawLine(r, x + 1, firstY, firstX, firstY, Color{100, 180, 255, 100});
            float lastX = timeToX(m_points.back().first);
            float lastY = valueToY(m_points.back().second);
            if (lastX < x + w - 1)
                drawLine(r, lastX, lastY, x + w - 1, lastY, Color{100, 180, 255, 100});
        }

        // Draw breakpoints as circles
        for (size_t i = 0; i < m_points.size(); ++i) {
            float px = timeToX(m_points[i].first);
            float py = valueToY(m_points[i].second);
            Color col = (static_cast<int>(i) == m_dragIdx)
                ? Color{255, 220, 80, 255} : Color{100, 180, 255, 255};
            r.drawFilledCircle(px, py, kPointRadius, col, 12);
            r.drawFilledCircle(px, py, kPointRadius - 1.5f, Color{30, 30, 40, 255}, 12);
            r.drawFilledCircle(px, py, kPointRadius - 2.5f, col, 12);
        }
    }
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button == MouseButton::Right) {
            // Right-click removes nearest point
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
        // Click empty area — add point
        double t = xToTime(e.x);
        float v = yToValue(e.y);
        if (m_onPointAdd) m_onPointAdd(t, v);
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_dragIdx >= 0 && m_dragIdx < static_cast<int>(m_points.size())) {
            double t = std::clamp(xToTime(e.x), m_timeStart, m_timeEnd);
            float v = std::clamp(yToValue(e.y), m_valueMin, m_valueMax);
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
    static constexpr float kHitRadius = 8.0f;

    double m_timeStart = 0.0, m_timeEnd = 4.0;
    float  m_valueMin = 0.0f, m_valueMax = 1.0f;
    std::vector<std::pair<double,float>> m_points;
    int m_dragIdx = -1;

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
        return m_bounds.y + m_bounds.h - frac * m_bounds.h; // higher value = higher on screen
    }
    double xToTime(float px) const {
        double span = m_timeEnd - m_timeStart;
        float frac = (px - m_bounds.x) / std::max(1.0f, m_bounds.w);
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

#ifndef YAWN_TEST_BUILD
    void drawLine(Renderer2D& r, float x0, float y0, float x1, float y1, Color c) const {
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) { r.drawRect(x0, y0, 1, 1, c); return; }
        float steps = std::max(1.0f, len);
        float sx = dx / steps, sy = dy / steps;
        for (float i = 0; i < steps; i += 1.0f)
            r.drawRect(x0 + i * sx, y0 + i * sy, 1.0f, 1.0f, c);
    }
#endif
};

// ═══════════════════════════════════════════════════════════════════════════
// LFODisplayWidget — Draws LFO waveform shape with animated phase dot.
// Used as a custom panel inside DeviceWidget for LFO MIDI effects.
// Shows one full cycle of the current waveform, depth-scaled, with a dot
// indicating the current output position.
// ═══════════════════════════════════════════════════════════════════════════

class LFODisplayWidget : public Widget {
public:
    LFODisplayWidget() { setName("LFODisplay"); }

    // 0=Sine, 1=Triangle, 2=Saw, 3=Square, 4=S&H
    void setShape(int s) { m_shape = std::clamp(s, 0, 4); }
    void setDepth(float d) { m_depth = std::clamp(d, 0.0f, 1.0f); }
    void setPhaseOffset(float p) { m_phaseOffset = std::clamp(p, 0.0f, 1.0f); }
    void setCurrentValue(float v) { m_currentValue = std::clamp(v, -1.0f, 1.0f); }
    void setCurrentPhase(double p) { m_currentPhase = p - std::floor(p); }
    void setLinked(bool linked) { m_linked = linked; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, 52.0f});
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
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        float wx = m_bounds.x + 4;
        float wy = m_bounds.y + 10;
        float ww = m_bounds.w - 8;
        float wh = m_bounds.h - 14;
        if (ww < 10 || wh < 6) return;

        float midY = wy + wh / 2;

        // Center line
        r.drawRect(wx, midY, ww, 1, Color{35, 35, 45, 255});

        // Waveform color (dimmer when depth is low)
        uint8_t alpha = static_cast<uint8_t>(100 + 155 * m_depth);
        Color waveCol{80, 180, 255, alpha};
        float amp = wh / 2 * std::max(m_depth, 0.15f);

        // Draw waveform (one full cycle with phase offset applied)
        int steps = static_cast<int>(ww);
        float prevX = wx, prevY = midY;
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            float phase = t + m_phaseOffset;
            phase = phase - std::floor(phase);
            float val = computeShape(phase);
            float curX = wx + static_cast<float>(i);
            float curY = midY - val * amp;
            if (i > 0)
                r.drawLine(prevX, prevY, curX, curY, waveCol, 1.5f);
            prevX = curX;
            prevY = curY;
        }

        // Animated phase dot — shows current position on the waveform
        float dotPhase = static_cast<float>(m_currentPhase) + m_phaseOffset;
        dotPhase = dotPhase - std::floor(dotPhase);
        float dotX = wx + dotPhase * ww;
        float dotVal = computeShape(dotPhase);
        float dotY = midY - dotVal * amp;
        Color dotCol{255, 200, 60, 255};
        r.drawRect(dotX - 3, dotY - 3, 6, 6, dotCol);

        // Shape label (top-left)
        float lblScale = 7.0f / Theme::kFontSize;
        static const char* shapeNames[] = {"Sin", "Tri", "Saw", "Sqr", "S&H"};
        const char* sn = (m_shape >= 0 && m_shape <= 4) ? shapeNames[m_shape] : "?";
        f.drawText(r, sn, m_bounds.x + 3, m_bounds.y + 1, lblScale,
                   Color{180, 180, 200, 200});

        // Link indicator (top-right)
        if (m_linked) {
            float tw = f.textWidth("Link", lblScale);
            f.drawText(r, "Link", m_bounds.x + m_bounds.w - tw - 3, m_bounds.y + 1,
                       lblScale, Color{100, 220, 140, 200});
        }
#endif
    }

private:
    float computeShape(float phase) const {
        switch (m_shape) {
            case 0: return std::sin(phase * 6.283185307f);
            case 1: // Triangle
                if (phase < 0.25f) return phase * 4.0f;
                if (phase < 0.75f) return 2.0f - phase * 4.0f;
                return phase * 4.0f - 4.0f;
            case 2: return 2.0f * phase - 1.0f;           // Saw
            case 3: return (phase < 0.5f) ? 1.0f : -1.0f; // Square
            case 4: { // S&H — pseudo-random per 1/8 segments
                int seg = static_cast<int>(phase * 8.0f);
                uint32_t hash = static_cast<uint32_t>(seg) * 1664525u + 1013904223u;
                return (static_cast<float>(hash & 0xFFFF) / 32767.5f) - 1.0f;
            }
            default: return 0.0f;
        }
    }

    int    m_shape        = 0;
    float  m_depth        = 0.5f;
    float  m_phaseOffset  = 0.0f;
    float  m_currentValue = 0.0f;
    double m_currentPhase = 0.0;
    bool   m_linked       = false;
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
    virtual void setOnParamTouch(std::function<void(int, float, bool)> cb) { (void)cb; }

    // Text-edit support for knob double-click entry
    virtual bool hasEditingKnob() const { return false; }
    virtual bool forwardKeyDown(int /*key*/) { return false; }
    virtual bool forwardTextInput(const char* /*text*/) { return false; }
    virtual void cancelEditingKnobs() {}
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

// Shorten parameter labels for compact knob display
inline std::string shortenLabel(const std::string& name) {
    // Strip "OpN " prefix (section header provides operator context)
    std::string s = name;
    if (s.size() > 4 && s[0] == 'O' && s[1] == 'p' && s[2] >= '1' && s[2] <= '9' && s[3] == ' ')
        s = s.substr(4);

    // Common abbreviations
    if (s == "Algorithm")  return "Algo";
    if (s == "Feedback")   return "FB";
    if (s == "Volume")     return "Vol";
    if (s == "Level")      return "Lvl";
    if (s == "Attack")     return "Atk";
    if (s == "Release")    return "Rel";
    if (s == "Cutoff")     return "Cut";
    if (s == "Resonance")  return "Res";
    if (s == "Depth")      return "Dpt";
    if (s == "Amount")     return "Amt";
    if (s == "Sustain")    return "Sus";
    if (s == "Decay")      return "Dcy";
    if (s == "Noise Level") return "Noise";
    if (s == "Root Note")  return "Root";
    if (s == "Filter Type") return "Type";
    if (s == "Filter Cutoff") return "Cut";
    if (s == "Filter Cut")    return "Cut";
    if (s == "Filter Res")    return "Res";
    if (s == "Filter Reso")   return "Reso";
    if (s == "Filter Env")    return "FiltEnv";
    if (s == "Osc1 Wave")     return "Wave";
    if (s == "Osc1 Level")    return "Level";
    if (s == "Osc2 Wave")     return "Wave";
    if (s == "Osc2 Level")    return "Level";
    if (s == "Osc2 Detune")   return "Detune";
    if (s == "Osc2 Octave")   return "Octave";
    if (s == "Sub Level")     return "Sub";
    if (s == "LFO Rate")      return "Rate";
    if (s == "LFO Depth")     return "Depth";
    if (s == "Amp Attack")    return "Atk";
    if (s == "Amp Decay")     return "Dcy";
    if (s == "Amp Sustain")   return "Sus";
    if (s == "Amp Release")   return "Rel";
    if (s == "Filt Attack")   return "Atk";
    if (s == "Filt Decay")    return "Dcy";
    if (s == "Filt Sustain")  return "Sus";
    if (s == "Filt Release")  return "Rel";
    if (s == "Loop Start")    return "Start";
    if (s == "Loop End")      return "End";
    if (s == "Sample Gain")   return "Gain";
    if (s == "Slice Count")   return "Slices";
    if (s == "Slice Mode")    return "Mode";
    if (s == "Orig BPM")      return "BPM";
    if (s == "Base Note")     return "Base";
    if (s == "Swing")         return "Swng";
    if (s == "Filter Cut")    return "Cut";
    if (s == "Filter Reso")   return "Reso";
    if (s == "Start Offset")  return "Offs";
    // DrumSlop per-pad
    if (s == "Pad Vol")       return "Vol";
    if (s == "Pad Pan")       return "Pan";
    if (s == "Pad Pitch")     return "Pitch";
    if (s == "Pad Rev")       return "Rev";
    if (s == "Pad Cutoff")    return "Cut";
    if (s == "Pad Reso")      return "Reso";
    if (s == "Pad Atk")       return "Atk";
    if (s == "Pad Dec")       return "Dcy";
    if (s == "Pad Sus")       return "Sus";
    if (s == "Pad Rel")       return "Rel";
    return s;
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
                k->setLabel(shortenLabel(pd.name));
                k->setBoolean(pd.isBoolean);

                // Detect integer-range params and set step for snapping
                float range = pd.maxVal - pd.minVal;
                bool isInteger = (range >= 2 && range <= 32 &&
                                  pd.minVal == std::floor(pd.minVal) &&
                                  pd.maxVal == std::floor(pd.maxVal) &&
                                  pd.unit.empty() && !pd.isBoolean);
                if (isInteger) {
                    k->setStep(1.0f);
                    k->setSensitivity(2.5f);
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
                k->setOnTouch([this, pidx = pd.index, k](bool touching) {
                    if (m_onParamTouch) m_onParamTouch(pidx, k->value(), touching);
                });
                sec.knobs.push_back({pd.index, k});
            }
            m_sections.push_back(std::move(sec));
        }
    }

    void updateParamValue(int index, float value) override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.paramIndex == index) {
                    if (!ke.knob->isDragging()) ke.knob->setValue(value);
                    return;
                }
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

    void setOnParamTouch(std::function<void(int, float, bool)> cb) override {
        m_onParamTouch = std::move(cb);
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
        // Forward to display widget first (e.g. DrumSlop pad grid)
        if (m_display && m_display->bounds().contains(e.x, e.y))
            if (m_display->onMouseDown(e)) return true;
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

    // Text-edit support: check if any knob is in double-click edit mode
    bool hasEditingKnob() const {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.knob->isEditing()) return true;
        return false;
    }

    bool forwardKeyDown(int key) {
        KeyEvent ke;
        ke.keyCode = key;
        for (auto& sec : m_sections)
            for (auto& entry : sec.knobs)
                if (entry.knob->isEditing()) return entry.knob->onKeyDown(ke);
        return false;
    }

    bool forwardTextInput(const char* text) {
        TextInputEvent te;
        std::strncpy(te.text, text, sizeof(te.text) - 1);
        te.text[sizeof(te.text) - 1] = '\0';
        for (auto& sec : m_sections)
            for (auto& entry : sec.knobs)
                if (entry.knob->isEditing()) return entry.knob->onTextInput(te);
        return false;
    }

    void cancelEditingKnobs() {
        KeyEvent ke;
        ke.keyCode = 27; // Escape
        for (auto& sec : m_sections)
            for (auto& entry : sec.knobs)
                if (entry.knob->isEditing()) entry.knob->onKeyDown(ke);
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
    std::function<void(int, float, bool)> m_onParamTouch;

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
