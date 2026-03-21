#pragma once

// Piano roll editor for MIDI clips.
// Renders a grid of pitches × beats with note blocks.
// Supports draw, select, erase tools with snap-to-grid.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Renderer.h"
#include "Font.h"
#include "Theme.h"
#include "../midi/MidiClip.h"
#include "../audio/Transport.h"

namespace yawn {
namespace ui {

class PianoRoll {
public:
    // ─── Enums ──────────────────────────────────────────────────────────
    enum class Tool { Draw, Select, Erase };
    enum class Snap { Quarter, Eighth, Sixteenth, ThirtySecond };

    // ─── Constants ──────────────────────────────────────────────────────
    static constexpr float kHeight      = 300.0f;
    static constexpr float kToolbarH    = 24.0f;
    static constexpr float kPianoW      = 52.0f;
    static constexpr float kMinRowH     = 4.0f;
    static constexpr float kMaxRowH     = 24.0f;
    static constexpr float kMinPxBeat   = 15.0f;
    static constexpr float kMaxPxBeat   = 500.0f;
    static constexpr int   kNPitch      = 128;
    static constexpr uint16_t kDefVel   = 32512;

    // ─── Public API ─────────────────────────────────────────────────────

    void setClip(midi::MidiClip* clip, int trackIdx) {
        m_clip = clip;
        m_trackIdx = trackIdx;
        m_selNote = -1;
        m_dragMode = Drag::None;
        if (clip) centerOnContent();
    }

    void setTransport(const audio::Transport* t) { m_transport = t; }

    bool  isOpen()     const { return m_open; }
    bool  hasClip()    const { return m_clip != nullptr; }
    midi::MidiClip* clip() const { return m_clip; }
    int   trackIndex() const { return m_trackIdx; }
    float height()     const { return m_open ? kHeight : 0.0f; }

    void setOpen(bool o) { m_open = o; }
    void close() { m_open = false; m_clip = nullptr; m_selNote = -1; }

    // ─── Render ─────────────────────────────────────────────────────────

    void render(Renderer2D& r, Font& f, float px, float py, float pw) {
        if (!m_open) return;
        m_px = px; m_py = py; m_pw = pw;
        m_gx = px + kPianoW;
        m_gy = py + kToolbarH;
        m_gw = pw - kPianoW;
        m_gh = kHeight - kToolbarH;

        // Background
        r.drawRect(px, py, pw, kHeight, Color{30, 30, 33});

        renderToolbar(r, f);

        r.pushClip(m_gx, m_gy, m_gw, m_gh);
        renderGrid(r);
        renderNotes(r);
        renderClipBound(r);
        r.popClip();

        r.pushClip(px, m_gy, kPianoW, m_gh);
        renderPianoKeys(r, f);
        r.popClip();

        // Top border
        r.drawRect(px, py, pw, 1, Color{60, 60, 65});
    }

    // ─── Click ──────────────────────────────────────────────────────────

    bool handleClick(float mx, float my) {
        if (!m_open || !m_clip) return false;
        if (!inPanel(mx, my)) return false;

        // Toolbar area — check Y bounds precisely
        if (my >= m_py && my < m_py + kToolbarH) {
            std::printf("[PianoRoll] Toolbar click at (%.0f, %.0f) py=%.0f toolbarH=%.0f\n",
                        mx, my, m_py, kToolbarH);
            return handleToolbarClick(mx, my);
        }

        // Piano key area
        if (mx < m_gx) return true;

        if (!inGrid(mx, my)) return false;

        double beat = snapBeat(xToBeat(mx));
        int pitch = yToPitch(my);
        if (pitch < 0 || pitch > 127 || beat < 0) return true;

        m_dragStartX = mx;
        m_dragStartY = my;

        if (m_tool == Tool::Draw) {
            int hit = noteAt(mx, my);
            if (hit >= 0) {
                m_selNote = hit;
                if (nearRightEdge(mx, hit)) {
                    startResize(hit);
                } else {
                    startMove(hit);
                }
            } else {
                createNote(beat, pitch);
                m_dragMode = Drag::DrawLen;
            }
        } else if (m_tool == Tool::Select) {
            int hit = noteAt(mx, my);
            if (hit >= 0) {
                m_selNote = hit;
                if (nearRightEdge(mx, hit)) {
                    startResize(hit);
                } else {
                    startMove(hit);
                }
            } else {
                m_selNote = -1;
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

    // ─── Drag ───────────────────────────────────────────────────────────

    bool handleDrag(float mx, float my) {
        if (!m_open || !m_clip || m_dragMode == Drag::None) return false;

        if (m_dragMode == Drag::DrawLen && m_selNote >= 0) {
            double curBeat = snapBeat(xToBeat(mx));
            auto& n = m_clip->note(m_selNote);
            double newDur = curBeat - n.startBeat;
            n.duration = std::max(snapVal(), newDur);
            autoExtend(n);
        } else if (m_dragMode == Drag::Move && m_selNote >= 0) {
            double beatDelta = xToBeat(mx) - xToBeat(m_dragStartX);
            int pitchDelta = yToPitch(my) - yToPitch(m_dragStartY);
            auto& n = m_clip->note(m_selNote);
            n.startBeat = snapBeat(std::max(0.0, m_origBeat + beatDelta));
            int np = static_cast<int>(m_origPitch) + pitchDelta;
            n.pitch = static_cast<uint8_t>(std::clamp(np, 0, 127));
        } else if (m_dragMode == Drag::Resize && m_selNote >= 0) {
            double beatDelta = xToBeat(mx) - xToBeat(m_dragStartX);
            auto& n = m_clip->note(m_selNote);
            double newEnd = snapBeat(n.startBeat + m_origDur + beatDelta);
            n.duration = std::max(snapVal(), newEnd - n.startBeat);
            autoExtend(n);
        }

        return true;
    }

    void handleRelease() {
        if (m_dragMode == Drag::Move && m_selNote >= 0 && m_clip) {
            // Re-sort: remove and re-add to maintain sorted order
            auto n = m_clip->note(m_selNote);
            m_clip->removeNote(m_selNote);
            m_clip->addNote(n);
            m_selNote = findNote(n.startBeat, n.pitch);
            autoExtend(n);
        }
        m_dragMode = Drag::None;
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

        if (key == kEscape) {
            if (m_selNote >= 0) { m_selNote = -1; return true; }
            close();
            return true;
        }
        if (key == kDelete || key == kBackspace) {
            if (m_selNote >= 0 && m_clip) deleteNote(m_selNote);
            return true;
        }
        if (key == '1') { m_snap = Snap::Quarter;      return true; }
        if (key == '2') { m_snap = Snap::Eighth;        return true; }
        if (key == '3') { m_snap = Snap::Sixteenth;     return true; }
        if (key == '4') { m_snap = Snap::ThirtySecond;  return true; }

        return false;
    }

private:
    // ─── Drag state ─────────────────────────────────────────────────────
    enum class Drag { None, Move, Resize, DrawLen };

    void startMove(int idx) {
        m_dragMode = Drag::Move;
        m_origBeat  = m_clip->note(idx).startBeat;
        m_origPitch = m_clip->note(idx).pitch;
    }

    void startResize(int idx) {
        m_dragMode = Drag::Resize;
        m_origDur = m_clip->note(idx).duration;
    }

    // ─── Rendering helpers ──────────────────────────────────────────────

    void renderToolbar(Renderer2D& r, Font& f) {
        r.drawRect(m_px, m_py, m_pw, kToolbarH, Color{35, 35, 38});
        float sc = 16.0f / f.pixelHeight();
        float ty = m_py + 3;
        float x = m_px + 6;

        // Tool buttons
        static const char* toolNames[] = {"Draw", "Select", "Erase"};
        for (int i = 0; i < 3; ++i) {
            bool active = static_cast<int>(m_tool) == i;
            Color bg = active ? Color{100, 180, 255} : Color{55, 55, 60};
            Color fg = active ? Color{10, 10, 15}    : Theme::textSecondary;
            m_toolBtnX[i] = x;
            r.drawRect(x, m_py + 2, m_toolBtnW, kToolbarH - 4, bg);
            f.drawText(r, toolNames[i], x + 4, ty, sc, fg);
            x += m_toolBtnW + 3;
        }

        x += 12;
        f.drawText(r, "Snap:", x, ty, sc, Theme::textSecondary);
        x += 42;

        // Snap buttons
        static const char* snapNames[] = {"1/4", "1/8", "1/16", "1/32"};
        for (int i = 0; i < 4; ++i) {
            bool active = static_cast<int>(m_snap) == i;
            Color bg = active ? Color{100, 180, 255} : Color{55, 55, 60};
            Color fg = active ? Color{10, 10, 15}    : Theme::textSecondary;
            m_snapBtnX[i] = x;
            r.drawRect(x, m_py + 2, m_snapBtnW, kToolbarH - 4, bg);
            f.drawText(r, snapNames[i], x + 3, ty, sc, fg);
            x += m_snapBtnW + 3;
        }

        // Loop toggle
        x += 12;
        {
            bool loopOn = m_clip && m_clip->loop();
            Color bg = loopOn ? Color{80, 220, 100} : Color{55, 55, 60};
            Color fg = loopOn ? Color{10, 10, 15}   : Theme::textSecondary;
            m_loopBtnX = x;
            r.drawRect(x, m_py + 2, m_loopBtnW, kToolbarH - 4, bg);
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

            r.drawRect(m_px, y, kPianoW, m_rowH, bg);
            r.drawRect(m_px, y + m_rowH - 1, kPianoW, 1, Color{60, 60, 65});

            // Label C notes
            if (p % 12 == 0 && m_rowH >= 8.0f) {
                int oct = (p / 12) - 1;
                char buf[8];
                std::snprintf(buf, sizeof(buf), "C%d", oct);
                f.drawText(r, buf, m_px + 2, y + 1, sc, fg);
            }
        }
        // Separator between keys and grid
        r.drawRect(m_px + kPianoW - 1, m_gy, 1, m_gh, Color{70, 70, 75});
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
            if (i == m_selNote) {
                r.drawRectOutline(x, y + 1, std::max(2.0f, w), h - 2,
                                  Color{255, 255, 255}, 1.5f);
            }
        }
    }

    void renderClipBound(Renderer2D& r) {
        if (!m_clip) return;

        // Loop region markers
        bool loopOn = m_clip->loop();
        float loopStartX = beatToX(0.0);
        float loopEndX   = beatToX(m_clip->lengthBeats());

        // Clip/loop end boundary
        if (loopEndX >= m_gx && loopEndX <= m_gx + m_gw) {
            Color boundCol = loopOn ? Color{80, 220, 100}.withAlpha(180)
                                    : Color{255, 100, 50}.withAlpha(120);
            r.drawRect(loopEndX, m_gy, 2, m_gh, boundCol);

            // Shade area past the loop/clip end
            float pastW = m_gx + m_gw - loopEndX - 2;
            if (pastW > 0)
                r.drawRect(loopEndX + 2, m_gy, pastW, m_gh,
                           Color{15, 15, 18}.withAlpha(140));
        }

        // Loop start marker (at beat 0)
        if (loopOn && loopStartX >= m_gx && loopStartX <= m_gx + m_gw) {
            r.drawRect(loopStartX, m_gy, 2, m_gh,
                       Color{80, 220, 100}.withAlpha(180));
        }

        // Loop region indicator bar at top of grid
        if (loopOn) {
            float barH = 4.0f;
            float x0 = std::max(m_gx, loopStartX);
            float x1 = std::min(m_gx + m_gw, loopEndX);
            if (x1 > x0) {
                r.drawRect(x0, m_gy, x1 - x0, barH,
                           Color{80, 220, 100}.withAlpha(120));
            }
        }
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
        // Snap buttons
        for (int i = 0; i < 4; ++i) {
            if (mx >= m_snapBtnX[i] && mx < m_snapBtnX[i] + m_snapBtnW) {
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

    // ─── Coordinate conversion ──────────────────────────────────────────

    float  beatToX(double b)  const { return m_gx + static_cast<float>(b * m_pxBeat) - m_scrollX; }
    double xToBeat(float x)   const { return (x - m_gx + m_scrollX) / m_pxBeat; }
    float  pitchToY(int p)    const { return m_gy + (127 - p) * m_rowH - m_scrollY; }
    int    yToPitch(float y)  const { return 127 - static_cast<int>((y - m_gy + m_scrollY) / m_rowH); }

    bool inPanel(float x, float y) const {
        return x >= m_px && x < m_px + m_pw && y >= m_py && y < m_py + kHeight;
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
            case Snap::Quarter:      return 1.0;
            case Snap::Eighth:       return 0.5;
            case Snap::Sixteenth:    return 0.25;
            case Snap::ThirtySecond: return 0.125;
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
        m_selNote = findNote(beat, pitch);
        autoExtend(n);
    }

    void deleteNote(int idx) {
        if (!m_clip || idx < 0 || idx >= m_clip->noteCount()) return;
        m_clip->removeNote(idx);
        if (m_selNote == idx) m_selNote = -1;
        else if (m_selNote > idx) --m_selNote;
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

    Tool  m_tool      = Tool::Draw;
    Snap  m_snap      = Snap::Sixteenth;

    float m_scrollX   = 0.0f;
    float m_scrollY   = 0.0f;
    float m_pxBeat    = 80.0f;
    float m_rowH      = 10.0f;

    int   m_selNote   = -1;
    Drag  m_dragMode  = Drag::None;
    float m_dragStartX = 0, m_dragStartY = 0;
    double m_origBeat = 0, m_origDur = 0;
    uint8_t m_origPitch = 0;

    // Cached layout
    float m_px = 0, m_py = 0, m_pw = 0;
    float m_gx = 0, m_gy = 0, m_gw = 0, m_gh = 0;

    // Toolbar button positions
    static constexpr float m_toolBtnW = 50.0f;
    static constexpr float m_snapBtnW = 36.0f;
    static constexpr float m_loopBtnW = 44.0f;
    float m_toolBtnX[3] = {};
    float m_snapBtnX[4] = {};
    float m_loopBtnX = 0;
};

} // namespace ui
} // namespace yawn
