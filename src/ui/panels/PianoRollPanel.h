#pragma once
// PianoRollPanel — Framework widget replacement for PianoRoll.
//
// fw::Widget that renders the MIDI piano roll editor and handles mouse
// events internally.  Replaces the standalone PianoRoll class.
// Only included from App.cpp — never compiled in test builds.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/ScrollBar.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/V1EventBridge.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "midi/MidiClip.h"
#include "audio/Transport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#ifndef YAWN_TEST_BUILD
#include <SDL3/SDL_keyboard.h>
#endif

namespace yawn {
namespace ui {
namespace fw {

class PianoRollPanel : public Widget {
public:
    // ─── Enums ──────────────────────────────────────────────────────────
    enum class Tool { Draw, Select, Erase };
    enum class Snap { Quarter, Eighth, Sixteenth, ThirtySecond,
                      QuarterTriplet, EighthTriplet, SixteenthTriplet };

    // ─── Constants ──────────────────────────────────────────────────────
    static constexpr float kHandleHeight = 8.0f;    // drag-resize handle
    static constexpr float kMinPanelH    = 100.0f;
    static constexpr float kMaxPanelH    = 600.0f;
    static constexpr float kDefaultHeight = 300.0f;
    static constexpr float kToolbarH    = 24.0f;
    static constexpr float kPianoW      = 52.0f;
    static constexpr float kMinRowH     = 4.0f;
    static constexpr float kMaxRowH     = 24.0f;
    static constexpr float kScrollbarH  = 12.0f;
    static constexpr float kMinPxBeat   = 15.0f;
    static constexpr float kMaxPxBeat   = 500.0f;
    static constexpr float kRulerH      = 16.0f;
    static constexpr float kClipOpsW    = 44.0f;
    static constexpr float kVelLaneH   = 60.0f;
    static constexpr int   kNPitch      = 128;
    static constexpr uint16_t kDefVel   = 32512;

    PianoRollPanel() {
        // Tool picker — radio-style: exactly one of the three toggles
        // stays lit, corresponding to m_tool. onChange enforces that
        // clicking a lit button doesn't un-select (the set of tools
        // is mutually exclusive; you always have one active).
        static const char* toolNames[] = {"Draw", "Select", "Erase"};
        for (int i = 0; i < 3; ++i) {
            m_toolBtns[i].setLabel(toolNames[i]);
            m_toolBtns[i].setAccentColor(Color{100, 180, 255});
            m_toolBtns[i].setOnChange([this, i](bool on) {
                if (on) {
                    m_tool = static_cast<Tool>(i);
                } else {
                    // FwToggle flips state on every click; for radio
                    // behaviour we pin it back to on so the active
                    // tool can't be un-selected by clicking itself.
                    m_toolBtns[i].setState(true);
                }
            });
        }

        static const char* snapNames[] = {"1/4","1/8","1/16","1/32","4T","8T","16T"};
        static constexpr float snapW[] = {kSnapBtnW, kSnapBtnW, kSnapBtnW, kSnapBtnW,
                                          kTripletBtnW, kTripletBtnW, kTripletBtnW};
        for (int i = 0; i < 7; ++i) {
            m_snapBtns[i].setLabel(snapNames[i]);
            m_snapBtns[i].setAccentColor(Color{100, 180, 255});
            m_snapBtns[i].setOnChange([this, i](bool on) {
                if (on) {
                    m_snap = static_cast<Snap>(i);
                } else {
                    m_snapBtns[i].setState(true);
                }
            });
        }

        // Seed the radio states so the first paint shows the default
        // selection lit even before the panel syncs from m_tool / m_snap.
        m_toolBtns[static_cast<int>(m_tool)].setState(true);
        m_snapBtns[static_cast<int>(m_snap)].setState(true);

        m_snapLabel.setText("Snap:");
        m_snapLabel.setColor(Theme::textSecondary);

        // Loop — v2 FwToggle. Green accent carries the loop-on state.
        m_loopBtn.setLabel("Loop");
        m_loopBtn.setAccentColor(Color{80, 220, 100});
        m_loopBtn.setOnChange([this](bool on) {
            if (m_clip) m_clip->setLoop(on);
        });

        // Vel — v2 FwToggle. Soft-blue accent for the show-lane state.
        m_velBtn.setLabel("Vel");
        m_velBtn.setAccentColor(Color{100, 180, 255});
        m_velBtn.setOnChange([this](bool on) {
            m_showVelocityLane = on;
        });

        // Follow — v2 FwToggle. Same blue accent as m_velBtn.
        m_followBtn.setLabel("Follow");
        m_followBtn.setAccentColor(Color{100, 180, 255});
        m_followBtn.setOnChange([this](bool on) {
            m_followPlayhead = on;
        });

        // Zoom in/out — v2 FwButton, plain one-shot.
        m_zoomInBtn.setLabel("+");
        m_zoomInBtn.setOnClick([this]() {
            m_pxBeat = std::min(kMaxPxBeat, m_pxBeat * 1.3f);
        });

        m_zoomOutBtn.setLabel("-");
        m_zoomOutBtn.setOnClick([this]() {
            m_pxBeat = std::max(kMinPxBeat, m_pxBeat / 1.3f);
        });

        m_clipNameLabel.setColor(Theme::textPrimary);

        m_scrollbar.setOnScroll([this](float pos) {
            m_scrollX = pos;
        });
    }

    // ─── Public API ─────────────────────────────────────────────────────

    void setClip(midi::MidiClip* clip, int trackIdx) {
        m_clip = clip;
        m_trackIdx = trackIdx;
        m_selectedNotes.clear();
        m_dragMode = Drag::None;
        if (clip) centerOnContent();
    }

    void setTransport(const audio::Transport* t) { m_transport = t; }

    void setPlayBeat(double beat, bool playing) {
        m_playBeat = beat;
        m_midiPlaying = playing;
    }

    bool  isOpen()     const { return m_open; }
    bool  hasClip()    const { return m_clip != nullptr; }
    midi::MidiClip* clip() const { return m_clip; }
    int   trackIndex() const { return m_trackIdx; }
    float height()     const { return m_animatedHeight; }

    void setOpen(bool o) {
        m_open = o;
        m_targetHeight = o ? m_userHeight : 0.0f;
    }
    void close() { setOpen(false); m_clip = nullptr; m_selectedNotes.clear(); }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        updateAnimation();
        return c.constrain({c.maxW, m_animatedHeight});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    // ─── Mouse events ───────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override;
#endif

    bool handleRightClick(float mx, float my) {
        if (!m_open || !m_clip || !inGrid(mx, my)) return false;
        int hit = noteAt(mx, my);
        if (hit >= 0) deleteNote(hit);
        return true;
    }

#ifdef YAWN_TEST_BUILD
    bool onMouseMove(MouseMoveEvent&) override { return false; }
#else
    bool onMouseMove(MouseMoveEvent& e) override;
#endif

#ifdef YAWN_TEST_BUILD
    bool onMouseUp(MouseEvent&) override { return false; }
#else
    bool onMouseUp(MouseEvent& e) override;
#endif

    // ─── Scroll / Zoom ─────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void handleScroll(float, float, bool = false, bool = false, float = -1.0f, float = -1.0f) {}
#else
    void handleScroll(float dx, float dy, bool ctrl = false, bool shift = false,
                      float mouseX = -1.0f, float mouseY = -1.0f);
#endif

    // ─── Keyboard ───────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    bool handleKeyDown(int, bool = false) { return false; }
#else
    bool handleKeyDown(int key, bool ctrl = false);
#endif

private:
    // ─── Drag state ─────────────────────────────────────────────────────
    enum class Drag { None, Move, Resize, DrawLen, RubberBand, LoopStart, LoopEnd };

    void startMove(int /*idx*/) {
        m_dragMode = Drag::Move;
        // Save original positions for all selected notes
        m_origPositions.clear();
        for (int i : m_selectedNotes) {
            m_origPositions.push_back({m_clip->note(i).startBeat, m_clip->note(i).pitch});
        }
    }

    void startResize(int idx) {
        m_dragMode = Drag::Resize;
        m_origDur = m_clip->note(idx).duration;
    }

#ifdef YAWN_TEST_BUILD
    void finalizeRubberBand() {}
#else
    void finalizeRubberBand();
#endif

    // Shift key detection — use SDL in real builds, stub in test builds
#ifndef YAWN_TEST_BUILD
    bool isShiftHeld() const {
        return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    }
#else
    bool isShiftHeld() const { return false; }
#endif

    // ─── Rendering helpers ──────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void renderToolbar(UIContext&) {}
#else
    void renderToolbar(UIContext& ctx);
#endif

#ifdef YAWN_TEST_BUILD
    void renderPianoKeys(Renderer2D&, Font&) {}
#else
    void renderPianoKeys(Renderer2D& r, Font& f);
#endif

#ifdef YAWN_TEST_BUILD
    void renderPianoKeyLabels(Renderer2D&, Font&) {}
#else
    void renderPianoKeyLabels(Renderer2D& r, Font& f);
#endif

#ifdef YAWN_TEST_BUILD
    void renderGrid(Renderer2D&) {}
#else
    void renderGrid(Renderer2D& r);
#endif

#ifdef YAWN_TEST_BUILD
    void renderNotes(Renderer2D&) {}
#else
    void renderNotes(Renderer2D& r);
#endif

#ifdef YAWN_TEST_BUILD
    void renderClipBound(Renderer2D&) {}
#else
    void renderClipBound(Renderer2D& r);
#endif

#ifdef YAWN_TEST_BUILD
    void renderRubberBand(Renderer2D&) {}
#else
    void renderRubberBand(Renderer2D& r);
#endif

#ifdef YAWN_TEST_BUILD
    void renderPlayhead(Renderer2D&) {}
#else
    void renderPlayhead(Renderer2D& r);
#endif

#ifdef YAWN_TEST_BUILD
    void renderRuler(Renderer2D&, Font&) {}
#else
    void renderRuler(Renderer2D& r, Font& f);
#endif

#ifdef YAWN_TEST_BUILD
    void renderClipOps(Renderer2D&, Font&) {}
#else
    void renderClipOps(Renderer2D& r, Font& f);
#endif

#ifdef YAWN_TEST_BUILD
    void renderVelocityLane(Renderer2D&, Font&) {}
#else
    void renderVelocityLane(Renderer2D& r, Font& f);
#endif

    bool handleVelocityLaneClick(float mx, float my) {
        if (!m_clip) return true;
        double beat = xToBeat(mx);
        int hitIdx = -1;
        for (int i = 0; i < m_clip->noteCount(); ++i) {
            const auto& n = m_clip->note(i);
            if (beat >= n.startBeat && beat < n.startBeat + n.duration) {
                hitIdx = i;
                break;
            }
        }
        if (hitIdx < 0) return true;

        float velFrac = 1.0f - std::clamp((my - m_velY - 2.0f) / (kVelLaneH - 4.0f), 0.0f, 1.0f);
        uint16_t newVel = static_cast<uint16_t>(std::clamp(velFrac * 65535.0f, 1.0f, 65535.0f));

        auto& hitNote = m_clip->note(hitIdx);
        float oldFrac = hitNote.velocity / 65535.0f;

        if (m_selectedNotes.count(hitIdx) && m_selectedNotes.size() > 1 && oldFrac > 0.0f) {
            float ratio = velFrac / oldFrac;
            for (int idx : m_selectedNotes) {
                if (idx >= 0 && idx < m_clip->noteCount()) {
                    auto& n = m_clip->note(idx);
                    float nf = (n.velocity / 65535.0f) * ratio;
                    n.velocity = static_cast<uint16_t>(std::clamp(nf * 65535.0f, 1.0f, 65535.0f));
                }
            }
        } else {
            hitNote.velocity = newVel;
        }

        m_velDragging = true;
        m_velDragNoteIdx = hitIdx;
        m_velDragStartVel = velFrac;
        captureMouse();
        return true;
    }

#ifdef YAWN_TEST_BUILD
    void renderScrollbar(UIContext&) {}
#else
    void renderScrollbar(UIContext& ctx);
#endif

    // ─── Toolbar click ──────────────────────────────────────────────────

    bool handleToolbarClick(float mx, float my) {
        // All toolbar buttons are v2 widgets now. Route through the
        // fw2 gesture SM via m_v2Dragging + v1 capture so the down/up
        // pair reaches the widget even if the user drags off-button.
        auto routeV2Btn = [&](::yawn::ui::fw2::Widget& w) -> bool {
            const auto& b = w.bounds();
            if (mx < b.x || mx >= b.x + b.w) return false;
            if (my < b.y || my >= b.y + b.h) return false;
            MouseEvent src;
            src.x = mx; src.y = my;
            src.button = MouseButton::Left;
            auto ev = ::yawn::ui::fw2::toFw2Mouse(src, b);
            w.dispatchMouseDown(ev);
            m_v2Dragging = &w;
            captureMouse();
            return true;
        };

        for (int i = 0; i < 3; ++i)
            if (routeV2Btn(m_toolBtns[i])) return true;
        for (int i = 0; i < 7; ++i)
            if (routeV2Btn(m_snapBtns[i])) return true;
        if (routeV2Btn(m_loopBtn))    return true;
        if (routeV2Btn(m_velBtn))     return true;
        if (routeV2Btn(m_followBtn))  return true;
        if (routeV2Btn(m_zoomInBtn))  return true;
        if (routeV2Btn(m_zoomOutBtn)) return true;
        return true;
    }

    bool handleRulerClick(float mx) {
        if (!m_clip) return true;
        double loopStart = m_clip->loopStartBeat();
        double loopEnd   = m_clip->lengthBeats();
        float startX = beatToX(loopStart);
        float endX   = beatToX(loopEnd);

        // Near loop start handle? (within 8px)
        if (std::abs(mx - startX) < 8.0f) {
            m_dragMode = Drag::LoopStart;
            captureMouse();
            return true;
        }
        // Near loop end handle?
        if (std::abs(mx - endX) < 8.0f) {
            m_dragMode = Drag::LoopEnd;
            captureMouse();
            return true;
        }
        return true;
    }

    bool handleClipOpsClick(float /*mx*/, float my) {
        if (!m_clip) return true;
        float btnH = 20.0f;
        float gap = 4.0f;
        float startY = m_gy + 6.0f;

        for (int i = 0; i < 6; ++i) {
            float by = startY + i * (btnH + gap);
            if (my >= by && my < by + btnH) {
                executeClipOp(i);
                return true;
            }
        }
        return true;
    }

    void executeClipOp(int op) {
        if (!m_clip) return;
        double loopStart = m_clip->loopStartBeat();
        double len = m_clip->lengthBeats();

        switch (op) {
        case 0: { // Dup — duplicate selected notes
            if (m_selectedNotes.empty()) break;
            std::vector<midi::MidiNote> copies;
            double minBeat = 1e9, maxEndBeat = 0;
            for (int idx : m_selectedNotes) {
                auto n = m_clip->note(idx);
                minBeat = std::min(minBeat, n.startBeat);
                maxEndBeat = std::max(maxEndBeat, n.startBeat + n.duration);
                copies.push_back(n);
            }
            double shift = maxEndBeat - minBeat;
            if (shift <= 0) shift = snapVal();
            m_selectedNotes.clear();
            for (auto& n : copies) {
                n.startBeat += shift;
                m_clip->addNote(n);
                autoExtend(n);
                int found = findNote(n.startBeat, n.pitch);
                if (found >= 0) m_selectedNotes.insert(found);
            }
            break;
        }
        case 1: { // x2 — double clip by repeating content
            int count = m_clip->noteCount();
            double regionLen = len - loopStart;
            std::vector<midi::MidiNote> copies;
            for (int i = 0; i < count; ++i) {
                auto n = m_clip->note(i);
                if (n.startBeat >= loopStart && n.startBeat < len) {
                    n.startBeat += regionLen;
                    copies.push_back(n);
                }
            }
            for (auto& n : copies) m_clip->addNote(n);
            m_clip->setLengthBeats(len + regionLen);
            m_selectedNotes.clear();
            break;
        }
        case 2: { // /2 — halve clip
            double newLen = loopStart + (len - loopStart) * 0.5;
            for (int i = m_clip->noteCount() - 1; i >= 0; --i) {
                if (m_clip->note(i).startBeat >= newLen)
                    m_clip->removeNote(i);
            }
            m_clip->setLengthBeats(newLen);
            m_selectedNotes.clear();
            break;
        }
        case 3: { // Rev — reverse note positions
            int count = m_clip->noteCount();
            std::vector<midi::MidiNote> notes;
            for (int i = 0; i < count; ++i)
                notes.push_back(m_clip->note(i));
            while (m_clip->noteCount() > 0)
                m_clip->removeNote(m_clip->noteCount() - 1);
            for (auto& n : notes) {
                n.startBeat = len - (n.startBeat + n.duration);
                if (n.startBeat < 0) n.startBeat = 0;
                m_clip->addNote(n);
            }
            m_selectedNotes.clear();
            break;
        }
        case 4: // Clr — clear all notes
            while (m_clip->noteCount() > 0)
                m_clip->removeNote(m_clip->noteCount() - 1);
            m_selectedNotes.clear();
            break;
        case 5: { // Set 1.1.1 here — crop clip to loop start
            if (loopStart <= 0) break;
            // Remove notes that end before loop start
            for (int i = m_clip->noteCount() - 1; i >= 0; --i) {
                const auto& n = m_clip->note(i);
                if (n.startBeat + n.duration <= loopStart)
                    m_clip->removeNote(i);
            }
            // Shift all remaining notes back by loopStart
            for (int i = 0; i < m_clip->noteCount(); ++i) {
                auto& n = m_clip->note(i);
                n.startBeat = std::max(0.0, n.startBeat - loopStart);
            }
            // Shift CC events: collect, remove, re-add shifted
            std::vector<midi::MidiCCEvent> keptCCs;
            for (int i = 0; i < m_clip->ccCount(); ++i) {
                auto cc = m_clip->ccEvent(i);
                if (cc.beat >= loopStart) {
                    cc.beat -= loopStart;
                    keptCCs.push_back(cc);
                }
            }
            while (m_clip->ccCount() > 0)
                m_clip->removeCC(m_clip->ccCount() - 1);
            for (auto& cc : keptCCs)
                m_clip->addCC(cc);
            // Adjust clip length and reset loop start
            m_clip->setLengthBeats(std::max(0.25, len - loopStart));
            m_clip->setLoopStartBeat(0);
            m_selectedNotes.clear();
            break;
        }
        }
    }

    // ─── Coordinate conversion ──────────────────────────────────────────

    float  beatToX(double b)  const { return m_gx + static_cast<float>(b * m_pxBeat) - m_scrollX; }
    double xToBeat(float x)   const { return (x - m_gx + m_scrollX) / m_pxBeat; }
    float  pitchToY(int p)    const { return m_gy + (127 - p) * m_rowH - m_scrollY; }
    int    yToPitch(float y)  const { return 127 - static_cast<int>((y - m_gy + m_scrollY) / m_rowH); }

    bool inPanel(float x, float y) const {
        return x >= m_px && x < m_px + m_pw && y >= m_py && y < m_py + m_bounds.h;
    }
    bool inGrid(float x, float y) const {
        return x >= m_gx && x < m_gx + m_gw && y >= m_gy && y < m_gy + m_gh;
    }

    // ─── Music helpers ──────────────────────────────────────────────────

    static bool isBlack(int p) {
        int n = p % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    double beatsPerBar() const {
        if (!m_transport) return 4.0;
        return m_transport->numerator() * (4.0 / m_transport->denominator());
    }

    double snapVal() const {
        switch (m_snap) {
            case Snap::Quarter:          return 1.0;
            case Snap::Eighth:           return 0.5;
            case Snap::Sixteenth:        return 0.25;
            case Snap::ThirtySecond:     return 0.125;
            case Snap::QuarterTriplet:   return 2.0 / 3.0;
            case Snap::EighthTriplet:    return 1.0 / 3.0;
            case Snap::SixteenthTriplet: return 1.0 / 6.0;
        }
        return 0.25;
    }

    double snapBeat(double b) const {
        double s = snapVal();
        return std::round(b / s) * s;
    }

    float maxBeats() const {
        double len = m_clip ? m_clip->lengthBeats() : 4.0;
        return static_cast<float>(std::max(len + 4.0, 8.0));
    }

    // ─── Note operations ────────────────────────────────────────────────

    int noteAt(float mx, float my) const {
        if (!m_clip) return -1;
        double beat = xToBeat(mx);
        int pitch = yToPitch(my);
        for (int i = 0; i < m_clip->noteCount(); ++i) {
            const auto& n = m_clip->note(i);
            if (n.pitch == pitch &&
                beat >= n.startBeat && beat < n.startBeat + n.duration)
                return i;
        }
        return -1;
    }

    bool nearRightEdge(float mx, int idx) const {
        if (idx < 0 || !m_clip) return false;
        const auto& n = m_clip->note(idx);
        float rx = beatToX(n.startBeat + n.duration);
        return std::abs(mx - rx) < 6.0f;
    }

    void createNote(double beat, int pitch) {
        if (!m_clip || pitch < 0 || pitch > 127) return;
        midi::MidiNote n{};
        n.startBeat = beat;
        n.duration  = snapVal();
        n.pitch     = static_cast<uint8_t>(pitch);
        n.velocity  = kDefVel;
        m_clip->addNote(n);
        int found = findNote(beat, pitch);
        m_selectedNotes.clear();
        if (found >= 0) m_selectedNotes.insert(found);
        autoExtend(n);
    }

    void deleteNote(int idx) {
        if (!m_clip || idx < 0 || idx >= m_clip->noteCount()) return;
        m_clip->removeNote(idx);
        // Renumber selection: remove idx, shift down indices > idx
        std::set<int> updated;
        for (int s : m_selectedNotes) {
            if (s == idx) continue;
            updated.insert(s > idx ? s - 1 : s);
        }
        m_selectedNotes = std::move(updated);
    }

    int findNote(double beat, int pitch) const {
        if (!m_clip) return -1;
        for (int i = 0; i < m_clip->noteCount(); ++i) {
            const auto& n = m_clip->note(i);
            if (n.pitch == pitch && std::abs(n.startBeat - beat) < 0.001)
                return i;
        }
        return -1;
    }

    void autoExtend(const midi::MidiNote& n) {
        if (!m_clip) return;
        double end = n.startBeat + n.duration;
        if (end > m_clip->lengthBeats())
            m_clip->setLengthBeats(end);
    }

    // ─── Scroll clamping ────────────────────────────────────────────────

    void clampScroll() {
        float gh = m_gh > 0 ? m_gh : 276.0f;
        float gw = m_gw > 0 ? m_gw : 800.0f;
        float totalH = kNPitch * m_rowH;
        m_scrollY = std::clamp(m_scrollY, 0.0f, std::max(0.0f, totalH - gh));
        float totalW = maxBeats() * m_pxBeat;
        m_scrollX = std::clamp(m_scrollX, 0.0f, std::max(0.0f, totalW - gw));
    }

    void centerOnContent() {
        if (!m_clip || m_clip->noteCount() == 0) {
            // Center on C4–C5 area
            float gh = m_gh > 0 ? m_gh : 276.0f;
            m_scrollY = (127 - 72) * m_rowH - gh / 2.0f;
            m_scrollX = 0;
            clampScroll();
            return;
        }
        int minP = 127, maxP = 0;
        for (int i = 0; i < m_clip->noteCount(); ++i) {
            int p = m_clip->note(i).pitch;
            minP = std::min(minP, p);
            maxP = std::max(maxP, p);
        }
        int midP = (minP + maxP) / 2;
        float gh = m_gh > 0 ? m_gh : 276.0f;
        m_scrollY = (127 - midP) * m_rowH - gh / 2.0f;
        m_scrollX = 0;
        clampScroll();
    }

    // ─── Members ────────────────────────────────────────────────────────

    midi::MidiClip*          m_clip      = nullptr;
    const audio::Transport*  m_transport = nullptr;
    int   m_trackIdx  = 0;
    bool  m_open      = false;
    double m_playBeat  = 0.0;
    bool  m_midiPlaying = false;

    // Resizable panel height (user-adjustable via handle drag)
    float m_userHeight = kDefaultHeight;

    // Handle drag state
    bool  m_handleDragActive = false;
    float m_handleDragStartY = 0;
    float m_handleDragStartH = 0;
    std::chrono::steady_clock::time_point m_lastHandleClickTime{};

    // Animation state for smooth height transitions
    mutable float m_animatedHeight = 0.0f;
    float         m_targetHeight   = 0.0f;

    void updateAnimation() const {
        constexpr float kSpeed = 0.2f;
        m_animatedHeight += (m_targetHeight - m_animatedHeight) * kSpeed;
        if (std::abs(m_animatedHeight - m_targetHeight) < 0.5f)
            m_animatedHeight = m_targetHeight;
    }

    Tool  m_tool      = Tool::Draw;
    Snap  m_snap      = Snap::Sixteenth;

    float m_scrollX   = 0.0f;
    float m_scrollY   = 0.0f;
    float m_pxBeat    = 80.0f;
    float m_rowH      = 10.0f;

    std::set<int> m_selectedNotes;
    Drag  m_dragMode  = Drag::None;
    float m_dragStartX = 0, m_dragStartY = 0;
    double m_origDur = 0;

    // Multi-note move: original positions for all selected notes
    struct OrigNote { double beat; uint8_t pitch; };
    std::vector<OrigNote> m_origPositions;

    // Rubber-band selection state
    bool  m_rubberBanding = false;
    bool  m_rubberShiftHeld = false;
    float m_rubberStartX = 0, m_rubberStartY = 0;
    float m_rubberEndX = 0,   m_rubberEndY = 0;

    // Cached layout
    float m_px = 0, m_py = 0, m_pw = 0;
    float m_gx = 0, m_gy = 0, m_gw = 0, m_gh = 0;
    float m_pianoX = 0;

    // Toolbar widgets
    static constexpr float kToolBtnW    = 60.0f;
    static constexpr float kSnapBtnW    = 44.0f;
    static constexpr float kFollowBtnW  = 52.0f;
    static constexpr float kTripletBtnW = 32.0f;
    static constexpr float kLoopBtnW    = 50.0f;
    static constexpr float kVelBtnW     = 40.0f;
    static constexpr float kZoomBtnW    = 28.0f;

    ::yawn::ui::fw2::FwToggle m_toolBtns[3];
    ::yawn::ui::fw2::FwToggle m_snapBtns[7];
    Label     m_snapLabel;
    ::yawn::ui::fw2::FwToggle m_loopBtn;
    ::yawn::ui::fw2::FwToggle m_velBtn;
    ::yawn::ui::fw2::FwToggle m_followBtn;
    ::yawn::ui::fw2::FwButton m_zoomInBtn;
    ::yawn::ui::fw2::FwButton m_zoomOutBtn;

    // v2 widget that currently holds mouse capture (button/toggle
    // press in progress). onMouseMove/onMouseUp flush translated events
    // to this pointer before falling through to v1 capture handling.
    ::yawn::ui::fw2::Widget* m_v2Dragging = nullptr;
    Label     m_clipNameLabel;
    ::yawn::ui::fw2::FwScrollBar m_scrollbar;

    // Clip ops button Y positions
    float m_clipOpsBtnY[6] = {};

    // Follow playhead
    bool  m_followPlayhead = false;

    // Piano key vertical zoom drag
    bool  m_pianoKeyDragging = false;
    float m_pianoKeyDragStartY = 0;
    float m_pianoKeyDragStartRowH = 0;

    // Velocity lane
    bool  m_showVelocityLane = true;
    float m_velY = 0;
    bool  m_velDragging = false;
    int   m_velDragNoteIdx = -1;
    float m_velDragStartVel = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
