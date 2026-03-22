#pragma once
// PianoRollPanel — Framework widget replacement for PianoRoll.
//
// fw::Widget that renders the MIDI piano roll editor and handles mouse
// events internally.  Replaces the standalone PianoRoll class.
// Only included from App.cpp — never compiled in test builds.

#include "ui/framework/Widget.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
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
    static constexpr int   kNPitch      = 128;
    static constexpr uint16_t kDefVel   = 32512;

    PianoRollPanel() = default;

    // ─── Public API ─────────────────────────────────────────────────────

    void setClip(midi::MidiClip* clip, int trackIdx) {
        m_clip = clip;
        m_trackIdx = trackIdx;
        m_selectedNotes.clear();
        m_dragMode = Drag::None;
        if (clip) centerOnContent();
    }

    void setTransport(const audio::Transport* t) { m_transport = t; }

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

    void paint(UIContext& ctx) override {
        if (m_animatedHeight < 1.0f || !m_clip) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        m_px = m_bounds.x; m_py = m_bounds.y; m_pw = m_bounds.w;
        m_pianoX = m_px + kClipOpsW;
        m_gx = m_px + kClipOpsW + kPianoW;
        m_gy = m_py + kHandleHeight + kToolbarH + kRulerH;
        m_gw = m_pw - kClipOpsW - kPianoW;
        m_gh = m_bounds.h - kHandleHeight - kToolbarH - kRulerH - kScrollbarH;

        // Clip to animated bounds so content doesn't overflow during transition
        r.pushClip(m_px, m_py, m_pw, m_animatedHeight);

        // Background
        r.drawRect(m_px, m_py, m_pw, m_bounds.h, Color{30, 30, 33});

        // Handle bar (8px drag strip with grip dots)
        r.drawRect(m_px, m_py, m_pw, kHandleHeight, Color{55, 55, 60});
        {
            float cx = m_px + m_pw * 0.5f;
            float cy = m_py + kHandleHeight * 0.5f;
            Color dotCol{90, 90, 95};
            float dotR = 1.0f;
            float spacing = 6.0f;
            r.drawRect(cx - spacing - dotR, cy - dotR, dotR * 2, dotR * 2, dotCol);
            r.drawRect(cx - dotR,            cy - dotR, dotR * 2, dotR * 2, dotCol);
            r.drawRect(cx + spacing - dotR, cy - dotR, dotR * 2, dotR * 2, dotCol);
        }

        renderToolbar(r, f);

#ifndef YAWN_TEST_BUILD
        renderRuler(r, f);
#endif

        r.pushClip(m_gx, m_gy, m_gw, m_gh);
        renderGrid(r);
        renderNotes(r);
        renderClipBound(r);
        renderRubberBand(r);
        r.popClip();

        r.pushClip(m_pianoX, m_gy, kPianoW, m_gh);
        renderPianoKeys(r, f);
        r.popClip();

#ifndef YAWN_TEST_BUILD
        renderScrollbar(r);
        renderClipOps(r, f);
#endif

        // Top border
        r.drawRect(m_px, m_py, m_pw, 1, Color{60, 60, 65});

        r.popClip();
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_open || !m_clip) return false;
        float mx = e.x, my = e.y;
        if (!inPanel(mx, my)) return false;

        // Handle area — start drag or detect double-click to toggle
        if (my >= m_py && my < m_py + kHandleHeight) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastHandleClickTime).count();
            if (elapsed < 300) {
                setOpen(!m_open);
                m_lastHandleClickTime = {};
                return true;
            }
            m_lastHandleClickTime = now;
            m_handleDragActive = true;
            m_handleDragStartY = my;
            m_handleDragStartH = m_userHeight;
            captureMouse();
            return true;
        }

        // Toolbar area (now offset by handle height)
        if (my >= m_py + kHandleHeight && my < m_py + kHandleHeight + kToolbarH) {
            std::printf("[PianoRoll] Toolbar click at (%.0f, %.0f) py=%.0f toolbarH=%.0f\n",
                        mx, my, m_py, kToolbarH);
            return handleToolbarClick(mx, my);
        }

        // Ruler area — loop handle dragging
        {
            float rulerY = m_py + kHandleHeight + kToolbarH;
            if (my >= rulerY && my < rulerY + kRulerH && mx >= m_gx && mx < m_gx + m_gw) {
                return handleRulerClick(mx);
            }
        }

        // Clip ops area
        if (mx >= m_px && mx < m_px + kClipOpsW && my >= m_gy && my < m_gy + m_gh) {
            return handleClipOpsClick(mx, my);
        }

        // Piano key area
        if (mx < m_gx) return true;

        // Scrollbar area
        {
            float sbY = m_gy + m_gh;
            if (my >= sbY && my < sbY + kScrollbarH && mx >= m_gx && mx < m_gx + m_gw) {
                float totalW = maxBeats() * m_pxBeat;
                float maxScroll = std::max(0.0f, totalW - m_gw);
                float thumbW = std::max(20.0f, m_gw * (m_gw / std::max(1.0f, totalW)));
                float scrollFrac = m_scrollX / std::max(1.0f, totalW - m_gw);
                float thumbX = m_gx + scrollFrac * (m_gw - thumbW);

                if (mx >= thumbX && mx < thumbX + thumbW) {
                    m_scrollbarDragging = true;
                    m_scrollbarDragStartX = mx;
                    m_scrollbarDragStartScroll = m_scrollX;
                    captureMouse();
                } else {
                    float clickFrac = (mx - m_gx) / m_gw;
                    m_scrollX = std::clamp(clickFrac * maxScroll, 0.0f, maxScroll);
                    clampScroll();
                }
                return true;
            }
        }

        if (!inGrid(mx, my)) return false;

        double beat = snapBeat(xToBeat(mx));
        int pitch = yToPitch(my);
        if (pitch < 0 || pitch > 127 || beat < 0) return true;

        m_dragStartX = mx;
        m_dragStartY = my;

        if (m_tool == Tool::Draw) {
            int hit = noteAt(mx, my);
            if (hit >= 0) {
                m_selectedNotes = {hit};
                if (nearRightEdge(mx, hit)) {
                    startResize(hit);
                    captureMouse();
                } else {
                    startMove(hit);
                    captureMouse();
                }
            } else {
                createNote(beat, pitch);
                m_dragMode = Drag::DrawLen;
                captureMouse();
            }
        } else if (m_tool == Tool::Select) {
            int hit = noteAt(mx, my);
            if (hit >= 0) {
                bool shift = isShiftHeld();
                if (shift) {
                    // Toggle note in/out of selection
                    if (m_selectedNotes.count(hit))
                        m_selectedNotes.erase(hit);
                    else
                        m_selectedNotes.insert(hit);
                } else {
                    if (!m_selectedNotes.count(hit))
                        m_selectedNotes = {hit};
                }
                if (nearRightEdge(mx, hit) && m_selectedNotes.size() == 1) {
                    startResize(hit);
                    captureMouse();
                } else if (!m_selectedNotes.empty()) {
                    startMove(hit);
                    captureMouse();
                }
            } else {
                if (!isShiftHeld())
                    m_selectedNotes.clear();
                // Start rubber-band selection
                m_rubberBanding = true;
                m_rubberShiftHeld = isShiftHeld();
                m_rubberStartX = mx; m_rubberStartY = my;
                m_rubberEndX = mx;   m_rubberEndY = my;
                m_dragMode = Drag::RubberBand;
                captureMouse();
            }
        } else if (m_tool == Tool::Erase) {
            int hit = noteAt(mx, my);
            if (hit >= 0) deleteNote(hit);
        }

        return true;
    }

    bool handleRightClick(float mx, float my) {
        if (!m_open || !m_clip || !inGrid(mx, my)) return false;
        int hit = noteAt(mx, my);
        if (hit >= 0) deleteNote(hit);
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_open || !m_clip) return false;
        float mx = e.x, my = e.y;

        // Handle drag resize
        if (m_handleDragActive) {
            float delta = m_handleDragStartY - my; // drag up = taller
            m_userHeight = std::clamp(
                m_handleDragStartH + delta, kMinPanelH, kMaxPanelH);
            m_targetHeight = m_userHeight;
            return true;
        }

        // Loop handle drag
        if (m_dragMode == Drag::LoopStart || m_dragMode == Drag::LoopEnd) {
            if (m_clip) {
                double beat = snapBeat(std::max(0.0, xToBeat(mx)));
                if (m_dragMode == Drag::LoopStart) {
                    if (beat < m_clip->lengthBeats())
                        m_clip->setLoopStartBeat(beat);
                } else {
                    double minEnd = m_clip->loopStartBeat() + snapVal();
                    m_clip->setLengthBeats(std::max(minEnd, beat));
                }
            }
            return true;
        }

        // Scrollbar thumb drag
        if (m_scrollbarDragging) {
            float delta = mx - m_scrollbarDragStartX;
            float totalW = maxBeats() * m_pxBeat;
            float maxScroll = std::max(0.0f, totalW - m_gw);
            float scrollDelta = delta * (totalW / std::max(1.0f, m_gw));
            m_scrollX = std::clamp(m_scrollbarDragStartScroll + scrollDelta, 0.0f, maxScroll);
            clampScroll();
            return true;
        }

        // Track scrollbar hover for highlight
        {
            float sbY = m_gy + m_gh;
            m_scrollbarHovered = (my >= sbY && my < sbY + kScrollbarH
                                  && mx >= m_gx && mx < m_gx + m_gw);
        }

        if (m_dragMode == Drag::None) return false;

        if (m_dragMode == Drag::DrawLen && !m_selectedNotes.empty()) {
            int idx = *m_selectedNotes.begin();
            double curBeat = snapBeat(xToBeat(mx));
            auto& n = m_clip->note(idx);
            double newDur = curBeat - n.startBeat;
            n.duration = std::max(snapVal(), newDur);
            autoExtend(n);
        } else if (m_dragMode == Drag::Move && !m_selectedNotes.empty()) {
            double beatDelta = xToBeat(mx) - xToBeat(m_dragStartX);
            int pitchDelta = yToPitch(my) - yToPitch(m_dragStartY);
            int i = 0;
            for (int idx : m_selectedNotes) {
                if (i < static_cast<int>(m_origPositions.size())) {
                    auto& n = m_clip->note(idx);
                    n.startBeat = snapBeat(std::max(0.0, m_origPositions[i].beat + beatDelta));
                    int np = static_cast<int>(m_origPositions[i].pitch) + pitchDelta;
                    n.pitch = static_cast<uint8_t>(std::clamp(np, 0, 127));
                }
                ++i;
            }
        } else if (m_dragMode == Drag::Resize && !m_selectedNotes.empty()) {
            int idx = *m_selectedNotes.begin();
            double beatDelta = xToBeat(mx) - xToBeat(m_dragStartX);
            auto& n = m_clip->note(idx);
            double newEnd = snapBeat(n.startBeat + m_origDur + beatDelta);
            n.duration = std::max(snapVal(), newEnd - n.startBeat);
            autoExtend(n);
        } else if (m_dragMode == Drag::RubberBand) {
            m_rubberEndX = mx;
            m_rubberEndY = my;
        }

        return true;
    }

    bool onMouseUp(MouseEvent& e) override {
        (void)e;
        if (m_handleDragActive) {
            m_handleDragActive = false;
            releaseMouse();
            return true;
        }
        if (m_scrollbarDragging) {
            m_scrollbarDragging = false;
            releaseMouse();
            return true;
        }
        if (m_dragMode == Drag::LoopStart || m_dragMode == Drag::LoopEnd) {
            m_dragMode = Drag::None;
            releaseMouse();
            return true;
        }
        if (m_dragMode == Drag::Move && !m_selectedNotes.empty() && m_clip) {
            // Re-sort: remove all selected and re-add to maintain sorted order
            std::vector<midi::MidiNote> notes;
            notes.reserve(m_selectedNotes.size());
            std::vector<int> indices(m_selectedNotes.begin(), m_selectedNotes.end());
            for (auto& n_idx : indices)
                notes.push_back(m_clip->note(n_idx));
            for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i)
                m_clip->removeNote(indices[i]);
            m_selectedNotes.clear();
            for (auto& n : notes) {
                m_clip->addNote(n);
                int found = findNote(n.startBeat, n.pitch);
                if (found >= 0) m_selectedNotes.insert(found);
                autoExtend(n);
            }
        } else if (m_dragMode == Drag::RubberBand && m_clip) {
            finalizeRubberBand();
        }
        m_dragMode = Drag::None;
        m_rubberBanding = false;
        releaseMouse();
        return true;
    }

    // ─── Scroll / Zoom ─────────────────────────────────────────────────

    void handleScroll(float dx, float dy, bool ctrl = false, bool shift = false) {
        if (!m_open) return;

        if (ctrl && shift) {
            m_rowH = std::clamp(m_rowH + dy * 1.0f, kMinRowH, kMaxRowH);
        } else if (ctrl) {
            m_pxBeat = std::clamp(m_pxBeat * (1.0f + dy * 0.08f), kMinPxBeat, kMaxPxBeat);
        } else if (shift) {
            m_scrollX -= dy * 30.0f;
        } else {
            m_scrollY -= dy * 30.0f;
        }
        clampScroll();
    }

    // ─── Keyboard ───────────────────────────────────────────────────────

    bool handleKeyDown(int key) {
        if (!m_open) return false;

        // Key constants (matching SDL_Keycode values)
        constexpr int kEscape    = 0x1B;
        constexpr int kDelete    = 0x7F;         // SDL3 SDLK_DELETE
        constexpr int kBackspace = 0x08;
        constexpr int kLeft      = 0x40000050;   // SDL_SCANCODE_LEFT
        constexpr int kRight     = 0x4000004F;   // SDL_SCANCODE_RIGHT
        constexpr int kUp        = 0x40000052;   // SDL_SCANCODE_UP
        constexpr int kDown      = 0x40000051;   // SDL_SCANCODE_DOWN

        if (key == kEscape) {
            if (!m_selectedNotes.empty()) { m_selectedNotes.clear(); return true; }
            close();
            return true;
        }
        if (key == kDelete || key == kBackspace) {
            if (!m_selectedNotes.empty() && m_clip) {
                // Delete in reverse index order to keep earlier indices valid
                std::vector<int> indices(m_selectedNotes.rbegin(), m_selectedNotes.rend());
                m_selectedNotes.clear();
                for (int idx : indices) {
                    if (idx >= 0 && idx < m_clip->noteCount())
                        m_clip->removeNote(idx);
                }
            }
            return true;
        }

        // Arrow key nudging
        if (key == kLeft && !m_selectedNotes.empty() && m_clip) {
            for (int idx : m_selectedNotes) {
                auto& n = m_clip->note(idx);
                n.startBeat = std::max(0.0, n.startBeat - snapVal());
            }
            return true;
        }
        if (key == kRight && !m_selectedNotes.empty() && m_clip) {
            for (int idx : m_selectedNotes) {
                auto& n = m_clip->note(idx);
                n.startBeat += snapVal();
                autoExtend(n);
            }
            return true;
        }
        if (key == kUp && !m_selectedNotes.empty() && m_clip) {
            for (int idx : m_selectedNotes) {
                auto& n = m_clip->note(idx);
                if (n.pitch < 127) n.pitch += 1;
            }
            return true;
        }
        if (key == kDown && !m_selectedNotes.empty() && m_clip) {
            for (int idx : m_selectedNotes) {
                auto& n = m_clip->note(idx);
                if (n.pitch > 0) n.pitch -= 1;
            }
            return true;
        }

        if (key == '1') { m_snap = Snap::Quarter;          return true; }
        if (key == '2') { m_snap = Snap::Eighth;            return true; }
        if (key == '3') { m_snap = Snap::Sixteenth;         return true; }
        if (key == '4') { m_snap = Snap::ThirtySecond;      return true; }
        if (key == '5') { m_snap = Snap::QuarterTriplet;    return true; }
        if (key == '6') { m_snap = Snap::EighthTriplet;     return true; }
        if (key == '7') { m_snap = Snap::SixteenthTriplet;  return true; }

        return false;
    }

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

    void finalizeRubberBand() {
        if (!m_clip) return;
        float rx0 = std::min(m_rubberStartX, m_rubberEndX);
        float rx1 = std::max(m_rubberStartX, m_rubberEndX);
        float ry0 = std::min(m_rubberStartY, m_rubberEndY);
        float ry1 = std::max(m_rubberStartY, m_rubberEndY);

        double beatMin = xToBeat(rx0);
        double beatMax = xToBeat(rx1);
        int pitchMin = yToPitch(ry1);   // Y is inverted
        int pitchMax = yToPitch(ry0);

        if (!m_rubberShiftHeld)
            m_selectedNotes.clear();

        for (int i = 0; i < m_clip->noteCount(); ++i) {
            const auto& n = m_clip->note(i);
            double noteEnd = n.startBeat + n.duration;
            int p = static_cast<int>(n.pitch);
            // Note overlaps rubber band if ranges intersect
            if (noteEnd > beatMin && n.startBeat < beatMax &&
                p >= pitchMin && p <= pitchMax) {
                m_selectedNotes.insert(i);
            }
        }
    }

    // Shift key detection — use SDL in real builds, stub in test builds
#ifndef YAWN_TEST_BUILD
    bool isShiftHeld() const {
        return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    }
#else
    bool isShiftHeld() const { return false; }
#endif

    // ─── Rendering helpers ──────────────────────────────────────────────

    void renderToolbar(Renderer2D& r, Font& f) {
        float tbY = m_py + kHandleHeight;
        r.drawRect(m_px, tbY, m_pw, kToolbarH, Color{35, 35, 38});
        float sc = 16.0f / f.pixelHeight();
        float ty = tbY + 3;
        float x = m_px + 6;

        // Tool buttons
        static const char* toolNames[] = {"Draw", "Select", "Erase"};
        for (int i = 0; i < 3; ++i) {
            bool active = static_cast<int>(m_tool) == i;
            Color bg = active ? Color{100, 180, 255} : Color{55, 55, 60};
            Color fg = active ? Color{10, 10, 15}    : Theme::textSecondary;
            m_toolBtnX[i] = x;
            r.drawRect(x, tbY + 2, m_toolBtnW, kToolbarH - 4, bg);
            f.drawText(r, toolNames[i], x + 4, ty, sc, fg);
            x += m_toolBtnW + 3;
        }

        x += 12;
        f.drawText(r, "Snap:", x, ty, sc, Theme::textSecondary);
        x += 42;

        // Snap buttons (4 straight + 3 triplet)
        static const char* snapNames[] = {"1/4","1/8","1/16","1/32","4T","8T","16T"};
        static constexpr float snapW[] = {m_snapBtnW, m_snapBtnW, m_snapBtnW, m_snapBtnW,
                                           m_tripletBtnW, m_tripletBtnW, m_tripletBtnW};
        for (int i = 0; i < 7; ++i) {
            bool active = static_cast<int>(m_snap) == i;
            bool isTriplet = (i >= 4);
            Color bg = active ? (isTriplet ? Color{255, 160, 60} : Color{100, 180, 255})
                              : Color{55, 55, 60};
            Color fg = active ? Color{10, 10, 15} : Theme::textSecondary;
            m_snapBtnX[i] = x;
            r.drawRect(x, tbY + 2, snapW[i], kToolbarH - 4, bg);
            f.drawText(r, snapNames[i], x + 3, ty, sc, fg);
            x += snapW[i] + 3;
        }

        // Loop toggle
        x += 12;
        {
            bool loopOn = m_clip && m_clip->loop();
            Color bg = loopOn ? Color{80, 220, 100} : Color{55, 55, 60};
            Color fg = loopOn ? Color{10, 10, 15}   : Theme::textSecondary;
            m_loopBtnX = x;
            r.drawRect(x, tbY + 2, m_loopBtnW, kToolbarH - 4, bg);
            f.drawText(r, "Loop", x + 4, ty, sc, fg);
            x += m_loopBtnW + 6;
        }

        // Clip name (right-aligned)
        if (m_clip && !m_clip->name().empty()) {
            f.drawText(r, m_clip->name().c_str(), m_px + m_pw - 160, ty, sc,
                       Theme::textPrimary);
        }
    }

    void renderPianoKeys(Renderer2D& r, Font& f) {
        float sc = 12.0f / f.pixelHeight();
        for (int p = 127; p >= 0; --p) {
            float y = pitchToY(p);
            if (y + m_rowH < m_gy || y > m_gy + m_gh) continue;

            bool black = isBlack(p);
            Color bg = black ? Color{40, 40, 45} : Color{180, 180, 185};
            Color fg = black ? Color{180, 180, 180} : Color{30, 30, 30};

            r.drawRect(m_pianoX, y, kPianoW, m_rowH, bg);
            r.drawRect(m_pianoX, y + m_rowH - 1, kPianoW, 1, Color{60, 60, 65});

            // Label C notes
            if (p % 12 == 0 && m_rowH >= 8.0f) {
                int oct = (p / 12) - 1;
                char buf[8];
                std::snprintf(buf, sizeof(buf), "C%d", oct);
                f.drawText(r, buf, m_pianoX + 2, y + 1, sc, fg);
            }
        }
        // Separator between keys and grid
        r.drawRect(m_pianoX + kPianoW - 1, m_gy, 1, m_gh, Color{70, 70, 75});
    }

    void renderGrid(Renderer2D& r) {
        // Pitch rows
        for (int p = 127; p >= 0; --p) {
            float y = pitchToY(p);
            if (y + m_rowH < m_gy || y > m_gy + m_gh) continue;

            Color bg;
            if (p % 12 == 0) bg = Color{42, 42, 48};
            else if (isBlack(p)) bg = Color{30, 30, 34};
            else bg = Color{38, 38, 42};

            r.drawRect(m_gx, y, m_gw, m_rowH, bg);
            // Thin horizontal separator — emphasize octave boundaries
            Color lineC = (p % 12 == 0) ? Color{55, 55, 62} : Color{45, 45, 50};
            r.drawRect(m_gx, y + m_rowH - 1, m_gw, 1, lineC);
        }

        // Vertical beat/bar lines
        double bpb = beatsPerBar();
        double sv = snapVal();
        double maxB = maxBeats();
        // Only draw visible range
        double visStart = std::max(0.0, xToBeat(m_gx));
        double visEnd   = xToBeat(m_gx + m_gw);
        double first    = std::floor(visStart / sv) * sv;

        for (double b = first; b <= std::min(maxB, visEnd); b += sv) {
            if (b < 0) continue;
            float x = beatToX(b);
            if (x < m_gx || x > m_gx + m_gw) continue;

            bool isBar  = std::fmod(b + 0.001, bpb) < 0.01;
            bool isBeat = std::fmod(b + 0.001, 1.0) < 0.01;

            Color c; float w;
            if (isBar)       { c = Color{80, 80, 88}; w = 1.5f; }
            else if (isBeat) { c = Color{58, 58, 65}; w = 1.0f; }
            else             { c = Color{44, 44, 50}; w = 1.0f; }

            r.drawRect(x, m_gy, w, m_gh, c);
        }
    }

    void renderNotes(Renderer2D& r) {
        if (!m_clip) return;
        Color trackCol = Theme::trackColors[m_trackIdx % Theme::kNumTrackColors];

        double visStart = xToBeat(m_gx);
        double visEnd   = xToBeat(m_gx + m_gw);

        for (int i = 0; i < m_clip->noteCount(); ++i) {
            const auto& n = m_clip->note(i);
            double noteEnd = n.startBeat + n.duration;
            if (noteEnd < visStart) continue;
            if (n.startBeat > visEnd) break; // sorted

            float x = beatToX(n.startBeat);
            float y = pitchToY(n.pitch);
            float w = static_cast<float>(n.duration * m_pxBeat);
            float h = m_rowH;

            if (y + h < m_gy || y > m_gy + m_gh) continue;

            // Velocity → brightness
            float velFrac = n.velocity / 65535.0f;
            uint8_t alpha = static_cast<uint8_t>(140 + velFrac * 115);
            Color noteCol = trackCol.withAlpha(alpha);

            r.drawRect(x, y + 1, std::max(2.0f, w), h - 2, noteCol);

            // Selection highlight
            if (m_selectedNotes.count(i)) {
                r.drawRectOutline(x, y + 1, std::max(2.0f, w), h - 2,
                                  Color{255, 255, 255}, 1.5f);
            }
        }
    }

    void renderClipBound(Renderer2D& r) {
        if (!m_clip) return;

        bool loopOn = m_clip->loop();
        float loopStartX = beatToX(m_clip->loopStartBeat());
        float loopEndX   = beatToX(m_clip->lengthBeats());

        // Shade area before loop start
        if (loopOn && loopStartX > m_gx) {
            float preW = std::min(loopStartX, m_gx + m_gw) - m_gx;
            if (preW > 0)
                r.drawRect(m_gx, m_gy, preW, m_gh, Color{15, 15, 18, 140});
        }

        // Clip/loop end boundary
        if (loopEndX >= m_gx && loopEndX <= m_gx + m_gw) {
            Color boundCol = loopOn ? Color{80, 220, 100, 180}
                                    : Color{255, 100, 50, 120};
            r.drawRect(loopEndX, m_gy, 2, m_gh, boundCol);

            // Shade area past the loop/clip end
            float pastW = m_gx + m_gw - loopEndX - 2;
            if (pastW > 0)
                r.drawRect(loopEndX + 2, m_gy, pastW, m_gh, Color{15, 15, 18, 140});
        }

        // Loop start marker
        if (loopStartX >= m_gx && loopStartX <= m_gx + m_gw) {
            Color markerCol = loopOn ? Color{80, 220, 100, 180}
                                     : Color{255, 160, 50, 120};
            r.drawRect(loopStartX, m_gy, 2, m_gh, markerCol);
        }

        // Loop region indicator bar at top of grid
        if (loopOn) {
            float barH = 4.0f;
            float x0 = std::max(m_gx, loopStartX);
            float x1 = std::min(m_gx + m_gw, loopEndX);
            if (x1 > x0) {
                r.drawRect(x0, m_gy, x1 - x0, barH, Color{80, 220, 100, 120});
            }
        }
    }

    void renderRubberBand(Renderer2D& r) {
        if (m_dragMode != Drag::RubberBand) return;
        float rx = std::min(m_rubberStartX, m_rubberEndX);
        float ry = std::min(m_rubberStartY, m_rubberEndY);
        float rw = std::abs(m_rubberEndX - m_rubberStartX);
        float rh = std::abs(m_rubberEndY - m_rubberStartY);
        r.drawRect(rx, ry, rw, rh, Color{100, 180, 255, 30});
        r.drawRectOutline(rx, ry, rw, rh, Color{100, 180, 255, 180}, 1.0f);
    }

    void renderRuler(Renderer2D& r, Font& f) {
        float rulerY = m_py + kHandleHeight + kToolbarH;
        float sc = 11.0f / f.pixelHeight();

        // Ruler background
        r.drawRect(m_gx, rulerY, m_gw, kRulerH, Color{38, 38, 42});
        r.drawRect(m_px, rulerY, kClipOpsW + kPianoW, kRulerH, Color{38, 38, 42});

        // Beat/bar numbers
        double bpb = beatsPerBar();
        double visStart = std::max(0.0, xToBeat(m_gx));
        double visEnd   = xToBeat(m_gx + m_gw);
        double firstBar = std::floor(visStart / bpb) * bpb;
        for (double b = firstBar; b <= visEnd; b += bpb) {
            float x = beatToX(b);
            if (x < m_gx || x > m_gx + m_gw) continue;
            int barNum = static_cast<int>(b / bpb) + 1;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", barNum);
            f.drawText(r, buf, x + 2, rulerY + 1, sc, Color{140, 140, 145});
            r.drawRect(x, rulerY, 1, kRulerH, Color{60, 60, 65});
        }

        if (!m_clip) return;

        // Loop region highlight
        bool loopOn = m_clip->loop();
        double loopStart = m_clip->loopStartBeat();
        double loopEnd   = m_clip->lengthBeats();
        float lsx = std::max(m_gx, beatToX(loopStart));
        float lex = std::min(m_gx + m_gw, beatToX(loopEnd));
        if (lex > lsx) {
            Color regionCol = loopOn ? Color{80, 220, 100, 50} : Color{255, 140, 60, 40};
            r.drawRect(lsx, rulerY, lex - lsx, kRulerH, regionCol);
        }

        // Loop start handle (flag)
        {
            float hx = beatToX(loopStart);
            if (hx >= m_gx - 6 && hx <= m_gx + m_gw + 6) {
                Color hc = loopOn ? Color{0, 255, 120} : Color{255, 160, 50};
                r.drawRect(hx - 1, rulerY, 3, kRulerH, hc);
                r.drawRect(hx, rulerY, 6, 6, hc);
            }
        }
        // Loop end handle
        {
            float hx = beatToX(loopEnd);
            if (hx >= m_gx - 6 && hx <= m_gx + m_gw + 6) {
                Color hc = loopOn ? Color{0, 255, 120} : Color{255, 160, 50};
                r.drawRect(hx - 1, rulerY, 3, kRulerH, hc);
                r.drawRect(hx - 5, rulerY, 6, 6, hc);
            }
        }

        // Bottom border
        r.drawRect(m_gx, rulerY + kRulerH - 1, m_gw, 1, Color{55, 55, 60});
    }

    void renderClipOps(Renderer2D& r, Font& f) {
        float sc = 11.0f / f.pixelHeight();
        float x = m_px;
        float y = m_gy;
        float w = kClipOpsW;

        // Background
        r.drawRect(x, y, w, m_gh, Color{32, 32, 36});

        // Buttons stacked vertically
        static const char* labels[] = {"Dup", "x2", "/2", "Rev", "Clr"};
        static const Color btnColors[] = {
            Color{80, 180, 255},   // Dup - blue
            Color{120, 230, 100},  // x2 - green
            Color{255, 180, 60},   // /2 - orange
            Color{200, 130, 255},  // Rev - purple
            Color{255, 90, 90}     // Clr - red
        };
        float btnH = 20.0f;
        float gap = 4.0f;
        float startY = y + 6.0f;

        for (int i = 0; i < 5; ++i) {
            float by = startY + i * (btnH + gap);
            if (by + btnH > y + m_gh) break;
            m_clipOpsBtnY[i] = by;
            r.drawRect(x + 3, by, w - 6, btnH, Color{50, 50, 55});
            // Colored left accent
            r.drawRect(x + 3, by, 3, btnH, btnColors[i]);
            f.drawText(r, labels[i], x + 10, by + 3, sc, Theme::textPrimary);
        }

        // Right border separator
        r.drawRect(x + w - 1, y, 1, m_gh, Color{55, 55, 60});
    }

    void renderScrollbar(Renderer2D& r) {
        float sbY = m_gy + m_gh;
        // Track background
        r.drawRect(m_gx, sbY, m_gw, kScrollbarH, Color{40, 40, 45});
        // Fill clip-ops + piano-key portion of the scrollbar row
        r.drawRect(m_px, sbY, kClipOpsW + kPianoW, kScrollbarH, Color{40, 40, 45});

        // Thumb
        float totalW = maxBeats() * m_pxBeat;
        float thumbW = std::max(20.0f, m_gw * (m_gw / std::max(1.0f, totalW)));
        float scrollFrac = m_scrollX / std::max(1.0f, totalW - m_gw);
        float thumbX = m_gx + scrollFrac * (m_gw - thumbW);

        Color thumbCol = (m_scrollbarDragging || m_scrollbarHovered)
                         ? Color{120, 120, 130}
                         : Color{90, 90, 100};
        r.drawRect(thumbX, sbY, thumbW, kScrollbarH, thumbCol);
    }

    // ─── Toolbar click ──────────────────────────────────────────────────

    bool handleToolbarClick(float mx, float /*my*/) {
        std::printf("[PianoRoll] toolbarClick mx=%.1f tools=[%.1f,%.1f,%.1f] w=%.0f snaps=[%.1f,%.1f,%.1f,%.1f] w=%.0f loop=%.1f\n",
                    mx, m_toolBtnX[0], m_toolBtnX[1], m_toolBtnX[2], m_toolBtnW,
                    m_snapBtnX[0], m_snapBtnX[1], m_snapBtnX[2], m_snapBtnX[3], m_snapBtnW,
                    m_loopBtnX);

        // Tool buttons
        for (int i = 0; i < 3; ++i) {
            if (mx >= m_toolBtnX[i] && mx < m_toolBtnX[i] + m_toolBtnW) {
                m_tool = static_cast<Tool>(i);
                std::printf("[PianoRoll] Tool → %d\n", i);
                return true;
            }
        }
        // Snap buttons (4 straight + 3 triplet)
        static constexpr float snapClickW[] = {m_snapBtnW, m_snapBtnW, m_snapBtnW, m_snapBtnW,
                                                m_tripletBtnW, m_tripletBtnW, m_tripletBtnW};
        for (int i = 0; i < 7; ++i) {
            if (mx >= m_snapBtnX[i] && mx < m_snapBtnX[i] + snapClickW[i]) {
                m_snap = static_cast<Snap>(i);
                std::printf("[PianoRoll] Snap → %d\n", i);
                return true;
            }
        }
        // Loop toggle
        if (m_clip && mx >= m_loopBtnX && mx < m_loopBtnX + m_loopBtnW) {
            m_clip->setLoop(!m_clip->loop());
            std::printf("[PianoRoll] Loop → %d\n", m_clip->loop());
            return true;
        }
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

        for (int i = 0; i < 5; ++i) {
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

    // Scrollbar drag state
    bool  m_scrollbarDragging = false;
    bool  m_scrollbarHovered  = false;
    float m_scrollbarDragStartX = 0;
    float m_scrollbarDragStartScroll = 0;

    // Cached layout
    float m_px = 0, m_py = 0, m_pw = 0;
    float m_gx = 0, m_gy = 0, m_gw = 0, m_gh = 0;
    float m_pianoX = 0;

    // Toolbar button positions
    static constexpr float m_toolBtnW    = 50.0f;
    static constexpr float m_snapBtnW    = 36.0f;
    static constexpr float m_tripletBtnW = 28.0f;
    static constexpr float m_loopBtnW    = 44.0f;
    float m_toolBtnX[3] = {};
    float m_snapBtnX[7] = {};
    float m_loopBtnX = 0;

    // Clip ops button Y positions
    float m_clipOpsBtnY[5] = {};
};

} // namespace fw
} // namespace ui
} // namespace yawn
