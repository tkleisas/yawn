#pragma once
// fw2::VocoderDisplayPanel — modulator viz + sidechain source picker
// for the Vocoder instrument.
//
// Layout (within ~120 px tall):
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │ MODULATOR   ← Audio 1            [▮▮▮▮▮░░░░]               │  ← header row
//   ├─────────────────────────────────────────────────────────────┤
//   │ Source: [Audio 1                                       ▾]  │  ← source dropdown (visible when SC selected)
//   │                                                             │
//   │      ╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱  (waveform OR placeholder)         │  ← modulator content
//   │                                                             │
//   ├─────────────────────────────────────────────────────────────┤
//   │ ███▓▓▓▒▒▒░░░  (band gradient)                              │  ← band-count strip
//   └─────────────────────────────────────────────────────────────┘
//
// Caller (DetailPanelWidget Vocoder setup) feeds in:
//   * modSource (0..6 from Vocoder::kModSource): controls the picker visibility
//   * sidechainSourceTrack (-1 = none, else track index)
//   * trackNames vector + selfTrackIdx for the dropdown contents
//   * modLevel polled from Vocoder::consumeModulatorLevel() each frame
//   * sample data + playhead (existing behaviour for kModSource=Sample)
//   * bandCount (existing behaviour)
//
// Emits:
//   * onSidechainSourceChanged(int trackIdx) — -1 = (none)
//
// Lives inside a GroupedKnobBody display slot, like the other
// instrument display panels.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class VocoderDisplayPanel : public Widget {
public:
    VocoderDisplayPanel() {
        setName("VocoderDisplay");
        setAutoCaptureOnUnhandledPress(false);

        m_sourceDD.setItems({"(none)"});
        m_sourceDD.setOnChange([this](int idx, const std::string&) {
            // Map dropdown idx (0=none, 1..=track) → absolute track id.
            // selfIdx is hidden from the list, so we re-insert it
            // when reconstructing the absolute index.
            int trackIdx = -1;
            if (idx > 0) {
                int t = idx - 1;
                if (m_selfIdx >= 0 && t >= m_selfIdx) ++t;
                trackIdx = t;
            }
            if (m_onSidechainChange) m_onSidechainChange(trackIdx);
        });
    }

    // ─── Host-driven state ──────────────────────────────────────────

    // Modulator sample preview (sample-source mode).
    void setModulatorData(const float* data, int frames) {
        m_modData = data;
        m_modFrames = frames;
    }
    void setPlayhead(float pos)    { m_playhead = std::clamp(pos, 0.0f, 1.0f); }
    void setPlaying(bool playing)  { m_playing = playing; }

    // Band-count strip across the bottom.
    void setBandCount(int bands)   { m_bandCount = std::clamp(bands, 4, 32); }

    // Vocoder::kModSource value (0=Sample, 1..5=Formant A/E/I/O/U,
    // 6=Sidechain). Drives whether the source dropdown is visible
    // and what the centre area shows.
    void setModSource(int idx)     { m_modSource = std::clamp(idx, 0, 6); }

    // Live modulator-input peak from Vocoder::consumeModulatorLevel(),
    // polled by displayUpdater each frame. UI-side decay smooths
    // flicker; the value is interpreted as 0..1 linear (clipped at 1
    // for the meter draw).
    void setModLevel(float peak)   { m_modLevelTarget = std::max(0.0f, peak); }

    // Track-name list and the "self" track index (so the dropdown
    // hides the vocoder's own track to prevent self-loops).
    void setAvailableSources(std::vector<std::string> trackNames,
                              int selfIdx) {
        m_trackNames = std::move(trackNames);
        m_selfIdx = selfIdx;
        rebuildSourceDD();
    }

    // Currently-routed sidechain source (absolute track index, -1=none).
    void setSidechainSource(int trackIdx) {
        m_sidechainSource = trackIdx;
        // Reflect into the dropdown's selected index.
        int ddIdx = 0;
        if (trackIdx >= 0) {
            int collapsed = trackIdx;
            if (m_selfIdx >= 0 && trackIdx > m_selfIdx) --collapsed;
            ddIdx = collapsed + 1;     // +1 for the "(none)" slot
        }
        m_sourceDD.setSelectedIndex(ddIdx, ValueChangeSource::Programmatic);
    }

    using SourceChangeCallback = std::function<void(int trackIdx)>;
    void setOnSidechainSourceChanged(SourceChangeCallback cb) {
        m_onSidechainChange = std::move(cb);
    }

    // ─── Layout / render ────────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, std::max(c.minH, 120.0f)});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float headerH    = 14.0f;
        const float ddH        = (m_modSource == 6) ? 22.0f : 0.0f;  // Sidechain mode
        const float bandH      = 10.0f;
        const float pad        = 2.0f;
        const float midY       = bounds.y + headerH + ddH;
        const float midH       = bounds.h - headerH - ddH - bandH - pad * 2;

        m_headerRect = {bounds.x + pad,           bounds.y + 1,
                         bounds.w - pad * 2,       headerH};
        if (ddH > 0.0f) {
            const float ddX = bounds.x + 60.0f;   // leave room for "Source:" label
            m_sourceDD.layout({ddX, midY + 1,
                                bounds.w - 60.0f - pad, ddH - 2}, ctx);
            m_ddRect = m_sourceDD.bounds();
        } else {
            m_ddRect = {0, 0, 0, 0};
        }
        m_waveRect = {bounds.x + pad,             midY + ddH,
                       bounds.w - pad * 2,         midH};
        m_bandRect = {bounds.x + pad,             bounds.y + bounds.h - bandH - pad,
                       bounds.w - pad * 2,         bandH};
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        // UI-side level decay for VU-style ballistics (~250 ms 0→silent).
        // Per-frame factor (assuming 60 Hz UI tick) ≈ 0.96.
        m_modLevelDisplay = std::max(m_modLevelTarget,
                                       m_modLevelDisplay * 0.96f);
        m_modLevelTarget *= 0.0f;   // consumed; next setModLevel re-arms

        const float lblFs = 12.0f;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{20, 20, 26, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        // ── Header: "MODULATOR  ← Source"  + level meter ──
        renderHeader(ctx, lblFs);

        // ── Sidechain dropdown row (only when modSource == SC) ──
        if (m_modSource == 6) {
            if (tm)
                tm->drawText(r, "Source:",
                              m_bounds.x + 4,
                              m_ddRect.y + 4,
                              lblFs, ::yawn::ui::Theme::textDim);
            m_sourceDD.render(ctx);
        }

        // ── Modulator viz ──
        renderModView(ctx, lblFs);

        // ── Band-count gradient strip ──
        if (m_bandRect.w > 4 && m_bandRect.h > 2) {
            const float barW = m_bandRect.w / m_bandCount;
            for (int b = 0; b < m_bandCount; ++b) {
                const float bx = m_bandRect.x + b * barW;
                const float t = static_cast<float>(b) / std::max(1, m_bandCount - 1);
                const uint8_t cr = static_cast<uint8_t>(80 + t * 150);
                const uint8_t cg = static_cast<uint8_t>(180 - t * 120);
                r.drawRect(bx + 0.5f, m_bandRect.y, barW - 1, m_bandRect.h,
                           Color{cr, cg, 220, 160});
            }
        }
    }
#endif

    bool onMouseDown(MouseEvent& e) override {
        // Only the source dropdown is interactive — and only when SC
        // mode makes it visible. Forward clicks within its bounds.
        if (m_modSource == 6 &&
            e.x >= m_ddRect.x && e.x < m_ddRect.x + m_ddRect.w &&
            e.y >= m_ddRect.y && e.y < m_ddRect.y + m_ddRect.h) {
            return m_sourceDD.dispatchMouseDown(e);
        }
        return false;
    }

private:
#ifndef YAWN_TEST_BUILD
    void renderHeader(UIContext& ctx, float lblFs) {
        if (!ctx.renderer || !ctx.textMetrics) return;
        auto& r = *ctx.renderer;
        auto& tm = *ctx.textMetrics;

        tm.drawText(r, "MODULATOR",
                    m_headerRect.x + 1, m_headerRect.y,
                    lblFs, ::yawn::ui::Theme::textDim);

        // Source-name badge — picks a label from the current state.
        std::string srcLabel = currentSourceLabel();
        if (!srcLabel.empty()) {
            tm.drawText(r, srcLabel,
                        m_headerRect.x + 80, m_headerRect.y,
                        lblFs, Color{180, 200, 255, 220});
        }

        // Level meter — right-aligned strip in the header.
        const float meterW = 64.0f;
        const float meterX = m_headerRect.x + m_headerRect.w - meterW - 2;
        const float meterY = m_headerRect.y + 2;
        const float meterH = m_headerRect.h - 4;
        r.drawRect(meterX, meterY, meterW, meterH, Color{15, 15, 20, 255});
        const float fill = std::clamp(m_modLevelDisplay, 0.0f, 1.0f);
        if (fill > 0.0f) {
            // Green → yellow → red (clip).
            Color c = (fill > 0.95f) ? Color{255, 60, 60, 255}
                    : (fill > 0.7f)  ? Color{255, 200, 60, 255}
                                     : Color{80, 220, 100, 255};
            r.drawRect(meterX, meterY, meterW * fill, meterH, c);
        }
    }

    void renderModView(UIContext& ctx, float lblFs) {
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        const float px = m_waveRect.x, py = m_waveRect.y;
        const float pw = m_waveRect.w, ph = m_waveRect.h;
        if (pw < 4 || ph < 4) return;

        if (m_modSource == 0) {
            // Sample-source: existing waveform + playhead.
            if (m_modData && m_modFrames > 0) {
                const float midY = py + ph * 0.5f;
                const float halfH = ph * 0.5f;
                int numBars = static_cast<int>(pw);
                if (numBars < 1) numBars = 1;
                const Color waveCol{200, 120, 255, 200};
                for (int i = 0; i < numBars; ++i) {
                    const int s0 = (i * m_modFrames) / numBars;
                    const int s1 = ((i + 1) * m_modFrames) / numBars;
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int s = s0; s < s1; ++s) {
                        const float v = m_modData[s];
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
                if (m_playing && m_playhead > 0.0f) {
                    const float phX = px + m_playhead * pw;
                    if (phX >= px && phX <= px + pw)
                        r.drawRect(phX, py, 1.5f, ph, Color{255, 255, 255, 200});
                }
            } else if (tm) {
                const char* msg = "Drop Modulator";
                const float tw = tm->textWidth(msg, lblFs);
                const float tx = px + (pw - tw) * 0.5f;
                const float ty = py + ph * 0.5f - 4;
                tm->drawText(r, msg, tx, ty, lblFs, Color{80, 80, 100, 180});
            }
        } else if (m_modSource >= 1 && m_modSource <= 5) {
            // Formant mode — show vowel letter centred large.
            if (tm) {
                static const char* kVowels[] = {"A","E","I","O","U"};
                const char* v = kVowels[m_modSource - 1];
                const float bigFs = ph * 0.6f;
                const float tw = tm->textWidth(v, bigFs);
                const float tx = px + (pw - tw) * 0.5f;
                const float ty = py + (ph - bigFs) * 0.5f - bigFs * 0.15f;
                tm->drawText(r, v, tx, ty, bigFs,
                             Color{180, 200, 255, 200});
                const char* sub = "Internal Formant";
                const float subFs = lblFs;
                const float stw = tm->textWidth(sub, subFs);
                tm->drawText(r, sub,
                             px + (pw - stw) * 0.5f,
                             py + ph - subFs - 2,
                             subFs, Color{100, 100, 120, 200});
            }
        } else if (m_modSource == 6) {
            // Sidechain — render a hint when no source is selected.
            if (m_sidechainSource < 0 && tm) {
                const char* msg = "Pick a sidechain source above";
                const float tw = tm->textWidth(msg, lblFs);
                const float tx = px + (pw - tw) * 0.5f;
                const float ty = py + ph * 0.5f - 4;
                tm->drawText(r, msg, tx, ty, lblFs, Color{120, 120, 140, 200});
            } else if (tm) {
                std::string lbl = currentSourceLabel();
                if (lbl.empty()) lbl = "(routed)";
                const float bigFs = ph * 0.4f;
                const float tw = tm->textWidth(lbl, bigFs);
                const float tx = px + (pw - tw) * 0.5f;
                const float ty = py + (ph - bigFs) * 0.5f - bigFs * 0.15f;
                tm->drawText(r, lbl, tx, ty, bigFs,
                             Color{180, 200, 255, 180});
            }
        }
    }

    std::string currentSourceLabel() const {
        if (m_modSource == 0) return "← Sample";
        if (m_modSource >= 1 && m_modSource <= 5) {
            static const char* kV[] = {"A","E","I","O","U"};
            return std::string("← Formant ") + kV[m_modSource - 1];
        }
        if (m_modSource == 6) {
            if (m_sidechainSource < 0) return "← (no source)";
            if (m_sidechainSource < static_cast<int>(m_trackNames.size()))
                return "← " + m_trackNames[m_sidechainSource];
            return "← Track " + std::to_string(m_sidechainSource + 1);
        }
        return "";
    }
#endif

    void rebuildSourceDD() {
        std::vector<std::string> items;
        items.reserve(m_trackNames.size() + 1);
        items.emplace_back("(none)");
        for (int i = 0; i < static_cast<int>(m_trackNames.size()); ++i) {
            if (i == m_selfIdx) continue;     // hide self to block A→A loops
            items.push_back(m_trackNames[i]);
        }
        m_sourceDD.setItems(std::move(items));
        // Re-sync selection in case the trackNames changed mid-list.
        setSidechainSource(m_sidechainSource);
    }

    Rect m_headerRect{}, m_waveRect{}, m_bandRect{}, m_ddRect{};
    const float* m_modData = nullptr;
    int   m_modFrames = 0;
    float m_playhead = 0.0f;
    bool  m_playing = false;
    int   m_bandCount = 16;

    int   m_modSource = 0;          // 0..6 (Vocoder::kModSource)
    int   m_sidechainSource = -1;   // -1=none, else absolute track idx
    int   m_selfIdx = -1;           // hide self from picker
    std::vector<std::string> m_trackNames;

    // Live level metering (host pushes, UI decays).
    float m_modLevelTarget = 0.0f;
    float m_modLevelDisplay = 0.0f;

    FwDropDown m_sourceDD;
    SourceChangeCallback m_onSidechainChange;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
