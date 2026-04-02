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
// CustomDeviceBody — Abstract base for custom instrument body layouts.
// Replaces the flat knob grid in DeviceWidget when set.
// ═══════════════════════════════════════════════════════════════════════════

class CustomDeviceBody : public Widget {
public:
    virtual ~CustomDeviceBody() = default;
    virtual void updateParamValue(int index, float value) = 0;
    virtual float preferredBodyWidth() const = 0;
    virtual void setOnParamChange(std::function<void(int, float)> cb) = 0;

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
