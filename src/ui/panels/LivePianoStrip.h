#pragma once
// LivePianoStrip — the live-coding editor's notation keyboard.
//
// A compact strip at the bottom of the `~` console's Edit tab: one octave
// of playable piano keys plus octave / note-length controls. Clicking a
// key (or playing a MIDI note while capture is armed) emits a
// yawn.midi()-notation token ("A3_16", "C#4_8*2") that the host inserts
// at the editor caret.
//
// Length buttons pick the _N duration suffix (whole-note division: _32…
// _1); the ×N button cycles a duration multiplier (_16*4). The MIDI
// toggle arms capture from the app's MIDI monitor: any note-on from a
// real keyboard produces a token with the played pitch (the octave
// control is bypassed) and the currently selected length.

#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Types.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "midi/MidiMonitorBuffer.h"

#include <algorithm>
#include <functional>
#include <string>

namespace yawn {
namespace ui {
namespace fw {

class LivePianoStrip {
public:
    // Emitted for every clicked/played key: e.g. "A3_16", "C#4_8*2".
    std::function<void(const std::string&)> onToken;

    void setMidiMonitor(midi::MidiMonitorBuffer* buf) { m_monitor = buf; }

    // Strip height (controls row + keys row).
    static constexpr float height() { return 78.0f; }

    // Called every frame by the host while the Edit tab is open: drains
    // new MIDI-monitor entries and emits tokens for armed capture.
    void pollMidi() {
        if (!m_midiArmed || !m_monitor || !onToken) return;
        const size_t head = m_monitor->headIndex();
        if (m_seenIndex == 0 || head < m_seenIndex)
            m_seenIndex = head;   // (re)arm: skip history
        const size_t oldest = m_monitor->oldestValid();
        if (m_seenIndex < oldest) m_seenIndex = oldest;
        for (size_t i = m_seenIndex; i < head; ++i) {
            const auto& e = m_monitor->at(i);
            if (e.type == midi::MidiMonitorEntry::Type::NoteOn && e.data2 > 0)
                onToken(tokenForPitch(static_cast<int>(e.data1)));
        }
        m_seenIndex = head;
    }

    // ── Painting ────────────────────────────────────────────────────
#ifndef YAWN_TEST_BUILD
    void paint(fw2::UIContext& ctx, const ::yawn::ui::fw::Rect& r) {
        auto& tm = *ctx.textMetrics;
        auto& renderer = *ctx.renderer;
        const auto& pal = fw2::theme().palette;
        const float fs = fw2::theme().metrics.fontSizeSmall;

        renderer.drawRect(r.x, r.y, r.w, r.h, ::yawn::ui::Theme::panelBg);
        renderer.drawRect(r.x, r.y, r.w, 1, ::yawn::ui::Theme::clipSlotBorder);

        // ── Controls row: octave ◀ C3 ▶ │ _32.._1 │ ×N │ MIDI ──
        const float cy = r.y + 3.0f;
        const float ch = 18.0f;
        m_ctrlY = cy; m_ctrlH = ch;

        float cx = r.x + 6.0f;
        auto button = [&](const std::string& s, bool active, Rect& out) {
            const float bw = tm.textWidth(s, fs) + 10.0f;
            out = Rect{cx, cy, bw, ch};
            renderer.drawRect(out.x, out.y, out.w, out.h,
                              active ? ::yawn::ui::Theme::transportAccent
                                     : ::yawn::ui::Theme::panelBg);
            renderer.drawRect(out.x, out.y, out.w, out.h,
                              ::yawn::ui::Theme::clipSlotBorder);
            tm.drawText(renderer, s, out.x + 5.0f,
                        out.y + (ch - tm.lineHeight(fs)) * 0.5f, fs,
                        active ? ::yawn::ui::Theme::panelBg
                               : pal.textPrimary);
            cx += bw + 4.0f;
        };

        button("◀", false, m_octDownRect);
        button("C" + std::to_string(m_octave), false, m_octRect);
        button("▶", false, m_octUpRect);
        cx += 4.0f;

        static const int lens[] = {32, 16, 8, 4, 2, 1};
        for (int d : lens)
            button("_" + std::to_string(d), m_lenDiv == d, m_lenRects[lenSlot(d)]);
        cx += 4.0f;
        button(m_mult > 1 ? ("×" + std::to_string(m_mult)) : "×1",
               m_mult > 1, m_multRect);
        cx += 8.0f;
        button(m_midiArmed ? "MIDI ●" : "MIDI", m_midiArmed, m_midiRect);

        // ── Keys row: one octave of whites + blacks ──
        const float ky = r.y + 3.0f + ch + 5.0f;
        const float kh = std::max(24.0f, r.y + r.h - ky - 5.0f);
        m_keysY = ky; m_keysH = kh;
        layoutKeys(r.x + 6.0f, r.w - 12.0f);

        for (int w = 0; w < 7; ++w) {
            const auto& k = m_white[w];
            renderer.drawRect(k.rect.x, ky, k.rect.w - 1.0f, kh,
                              ::yawn::ui::Theme::panelBg);
            renderer.drawRect(k.rect.x, ky, k.rect.w - 1.0f, kh,
                              ::yawn::ui::Theme::clipSlotBorder);
            const std::string lbl = keyLetter(w) + std::to_string(m_octave);
            tm.drawText(renderer, lbl,
                        k.rect.x + (k.rect.w - tm.textWidth(lbl, fs)) * 0.5f,
                        ky + kh - tm.lineHeight(fs) - 2.0f, fs,
                        pal.textSecondary);
        }
        for (const auto& k : m_black) {
            if (k.rect.w <= 0.0f) continue;
            renderer.drawRect(k.rect.x, ky, k.rect.w, kh * 0.62f,
                              ::yawn::ui::Theme::clipSlotBorder);
            const std::string lbl =
                std::string(keyLetter(k.whiteIdx)) + "#";   // after C → C#
            tm.drawText(renderer, lbl,
                        k.rect.x + (k.rect.w - tm.textWidth(lbl, fs)) * 0.5f,
                        ky + kh * 0.62f - tm.lineHeight(fs) - 1.0f, fs,
                        pal.textSecondary);
        }
    }
#endif

    // ── Hit testing (content-local coords, as the console forwards) ──
    bool click(float lx, float ly, const std::function<void(const std::string&)>& emit) {
        // Controls row.
        if (ly >= m_ctrlY && ly < m_ctrlY + m_ctrlH) {
            auto hit = [&](const Rect& rc) {
                return lx >= rc.x && lx < rc.x + rc.w;
            };
            if (hit(m_octDownRect)) { setOctave(m_octave - 1); return true; }
            if (hit(m_octUpRect))   { setOctave(m_octave + 1); return true; }
            static const int lens[] = {32, 16, 8, 4, 2, 1};
            for (int d : lens)
                if (hit(m_lenRects[lenSlot(d)])) { m_lenDiv = d; return true; }
            if (hit(m_multRect)) { cycleMult(); return true; }
            if (hit(m_midiRect)) {
                m_midiArmed = !m_midiArmed;
                m_seenIndex = 0;   // re-arm: ignore monitor history
                return true;
            }
            return false;
        }

        // Keys row (blacks first — they sit on top).
        if (ly >= m_keysY && ly < m_keysY + m_keysH && emit) {
            for (const auto& k : m_black) {
                if (k.rect.w > 0.0f && lx >= k.rect.x &&
                    lx < k.rect.x + k.rect.w &&
                    ly < m_keysY + m_keysH * 0.62f) {
                    emit(tokenForSemitone(k.semitone));
                    return true;
                }
            }
            for (const auto& k : m_white) {
                if (lx >= k.rect.x && lx < k.rect.x + k.rect.w - 1.0f) {
                    emit(tokenForSemitone(k.semitone));
                    return true;
                }
            }
        }
        return false;
    }

private:
    struct Key { Rect rect; int semitone = 0; int whiteIdx = 0; };

    static const char* keyLetter(int idx) {
        static const char* letters[7] = {"C","D","E","F","G","A","B"};
        return letters[idx];
    }
    static int whiteSemitone(int idx) {
        static const int s[7] = {0, 2, 4, 5, 7, 9, 11};
        return s[idx];
    }
    static int lenSlot(int div) {
        switch (div) {
            case 32: return 0; case 16: return 1; case 8: return 2;
            case 4:  return 3; case 2:  return 4; default: return 5;
        }
    }

    void setOctave(int o) { m_octave = std::clamp(o, -1, 9); }

    void cycleMult() {
        static const int seq[] = {1, 2, 3, 4, 6, 8};
        for (int i = 0; i < 6; ++i)
            if (seq[i] == m_mult) { m_mult = seq[(i + 1) % 6]; return; }
        m_mult = 1;
    }

    std::string tokenForSemitone(int semitoneInOctave) const {
        return tokenForPitch((m_octave + 1) * 12 + semitoneInOctave);
    }

    std::string tokenForPitch(int midi) const {
        static const char* names[12] = {"C","C#","D","D#","E","F","F#",
                                        "G","G#","A","A#","B"};
        const int pc = ((midi % 12) + 12) % 12;
        const int oct = midi / 12 - 1;   // MIDI 60 = C4
        std::string tok = std::string(names[pc]) + std::to_string(oct) +
                          "_" + std::to_string(m_lenDiv);
        if (m_mult > 1) tok += "*" + std::to_string(m_mult);
        return tok;
    }

    void layoutKeys(float x, float w) {
        const float whiteW = w / 7.0f;
        for (int i = 0; i < 7; ++i) {
            m_white[i].rect = Rect{x + i * whiteW, 0.0f, whiteW, 0.0f};
            m_white[i].semitone = whiteSemitone(i);
        }
        static const int blackAfter[5] = {0, 1, 3, 4, 5};   // after C D F G A
        static const int blackSemi[5]  = {1, 3, 6, 8, 10};
        const float bw = whiteW * 0.62f;
        for (int i = 0; i < 5; ++i) {
            const int after = blackAfter[i];
            m_black[i].whiteIdx = after;
            m_black[i].semitone = blackSemi[i];
            m_black[i].rect = Rect{m_white[after].rect.x + whiteW - bw * 0.5f,
                                   0.0f, bw, 0.0f};
        }
    }

    int  m_octave = 3;
    int  m_lenDiv = 16;
    int  m_mult = 1;
    bool m_midiArmed = false;

    midi::MidiMonitorBuffer* m_monitor = nullptr;
    size_t m_seenIndex = 0;

    // Cached control-row geometry for hit-testing (refreshed each paint).
    float m_ctrlY = -1, m_ctrlH = 0;
    Rect m_octDownRect, m_octRect, m_octUpRect;
    Rect m_lenRects[6], m_multRect, m_midiRect;
    float m_keysY = 0, m_keysH = 0;
    Key m_white[7];
    Key m_black[5];
};

} // namespace fw
} // namespace ui
} // namespace yawn
