#pragma once
// SessionPanel — Framework widget replacement for SessionView.
//
// fw::Widget that renders the session grid (transport bar, track headers,
// scene labels, clip slots) and handles click events.  Text input, key
// events and scroll are still forwarded directly from App.cpp because
// the widget tree doesn't dispatch those yet.

#include "ui/framework/Widget.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "app/Project.h"
#include "core/Constants.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <SDL3/SDL_timer.h>

namespace yawn {
namespace ui {
namespace fw {

struct ClipSlotUIState {
    bool    playing      = false;
    bool    queued       = false;
    int64_t playPosition = 0;
};

class SessionPanel : public Widget {
public:
    SessionPanel() = default;

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    // ─── State updates (called from App) ────────────────────────────────

    void updateClipState(int trackIndex, bool playing, int64_t playPos) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackStates[trackIndex].playing      = playing;
            m_trackStates[trackIndex].playPosition  = playPos;
        }
    }

    void setTransportState(bool playing, double beats, double bpm,
                           int numerator = 4, int denominator = 4) {
        m_transportPlaying     = playing;
        m_transportBeats       = beats;
        m_transportBPM         = bpm;
        m_transportNumerator   = numerator;
        m_transportDenominator = denominator;
        m_animTimer += 1.0f / 60.0f;
    }

    void setSelectedTrack(int t) { m_selectedTrack = t; }
    int  selectedTrack() const   { return m_selectedTrack; }
    float scrollX() const        { return m_scrollX; }
    float scrollY() const        { return m_scrollY; }
    bool  isEditing() const      { return m_editMode != EditMode::None; }

    float preferredHeight() const {
        int scenes = m_project
            ? std::min(m_project->numScenes(), kVisibleScenes) : kVisibleScenes;
        return Theme::kTransportBarHeight + Theme::kTrackHeaderHeight
             + scenes * Theme::kClipSlotHeight;
    }

    // ─── Slot query (used by App for double-click → piano roll) ─────────

    bool getSlotAt(float mx, float my, int& trackOut, int& sceneOut) const {
        if (!m_project) return false;
        float gridX = m_bounds.x + Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + Theme::kTransportBarHeight + Theme::kTrackHeaderHeight;
        if (mx < gridX || my < gridY) return false;
        float cmx = mx + m_scrollX;
        float cmy = my + m_scrollY;
        int track = static_cast<int>((cmx - gridX) / Theme::kTrackWidth);
        int scene = static_cast<int>((cmy - gridY) / Theme::kClipSlotHeight);
        if (track < 0 || track >= m_project->numTracks()) return false;
        if (scene < 0 || scene >= m_project->numScenes()) return false;
        trackOut = track;
        sceneOut = scene;
        return true;
    }

    // ─── Text/Key events (widget tree doesn't dispatch these yet) ───────

    bool handleDoubleClick(float mx, float my) {
        if (mx >= m_bpmBoxX && mx <= m_bpmBoxX + m_bpmBoxW &&
            my >= m_bpmBoxY && my <= m_bpmBoxY + m_bpmBoxH) {
            m_editMode = EditMode::BPM;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", m_transportBPM);
            m_editBuffer = buf;
            return true;
        }
        if (mx >= m_tsNumBoxX && mx <= m_tsNumBoxX + m_tsNumBoxW &&
            my >= m_tsNumBoxY && my <= m_tsNumBoxY + m_tsNumBoxH) {
            m_editMode = EditMode::TimeSigNum;
            m_editBuffer = std::to_string(m_transportNumerator);
            return true;
        }
        if (mx >= m_tsDenBoxX && mx <= m_tsDenBoxX + m_tsDenBoxW &&
            my >= m_tsDenBoxY && my <= m_tsDenBoxY + m_tsDenBoxH) {
            m_editMode = EditMode::TimeSigDen;
            m_editBuffer = std::to_string(m_transportDenominator);
            return true;
        }
        return false;
    }

    bool handleTextInput(const char* text) {
        if (m_editMode == EditMode::None) return false;
        for (const char* p = text; *p; ++p) {
            char c = *p;
            if (m_editMode == EditMode::BPM) {
                if ((c >= '0' && c <= '9') ||
                    (c == '.' && m_editBuffer.find('.') == std::string::npos))
                    m_editBuffer += c;
            } else {
                if (c >= '0' && c <= '9')
                    m_editBuffer += c;
            }
        }
        return true;
    }

    bool handleKeyDown(int keycode) {
        if (m_editMode == EditMode::None) return false;
        if (keycode == 13 || keycode == 9) { // Enter or Tab
            if (m_editMode == EditMode::BPM) {
                double val = std::atof(m_editBuffer.c_str());
                val = std::clamp(val, 20.0, 999.0);
                if (m_engine)
                    m_engine->sendCommand(audio::TransportSetBPMMsg{val});
                if (keycode == 9) {
                    m_editMode = EditMode::TimeSigNum;
                    m_editBuffer = std::to_string(m_transportNumerator);
                    return true;
                }
            } else if (m_editMode == EditMode::TimeSigNum) {
                int val = std::atoi(m_editBuffer.c_str());
                val = std::clamp(val, 1, 32);
                if (m_engine)
                    m_engine->sendCommand(
                        audio::TransportSetTimeSignatureMsg{val, m_transportDenominator});
                if (keycode == 9) {
                    m_editMode = EditMode::TimeSigDen;
                    m_editBuffer = std::to_string(m_transportDenominator);
                    return true;
                }
            } else if (m_editMode == EditMode::TimeSigDen) {
                int val = std::atoi(m_editBuffer.c_str());
                if (val < 1) val = 1;
                else if (val <= 1) val = 1;
                else if (val <= 2) val = 2;
                else if (val <= 4) val = 4;
                else if (val <= 8) val = 8;
                else if (val <= 16) val = 16;
                else val = 32;
                if (m_engine)
                    m_engine->sendCommand(
                        audio::TransportSetTimeSignatureMsg{m_transportNumerator, val});
            }
            m_editMode = EditMode::None;
            m_editBuffer.clear();
            return true;
        }
        if (keycode == 27) { m_editMode = EditMode::None; m_editBuffer.clear(); return true; }
        if (keycode == 8)  { if (!m_editBuffer.empty()) m_editBuffer.pop_back(); return true; }
        return true; // consume all keys while editing
    }

    // ─── Scroll (forwarded from App) ────────────────────────────────────

    void handleScroll(float dx, float dy) {
        if (!m_project) return;
        float gridH = m_bounds.h - Theme::kTransportBarHeight - Theme::kTrackHeaderHeight - kScrollbarH;
        float contentH = m_project->numScenes() * Theme::kClipSlotHeight;
        float maxY = std::max(0.0f, contentH - gridH);
        m_scrollY = std::clamp(m_scrollY - dy * 30.0f, 0.0f, maxY);

        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        float maxX = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - dx * 30.0f, 0.0f, maxX);
    }

    void tapTempo() {
        double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
        m_tapFlash = 1.0f;
        if (m_tapCount > 0) {
            double last = m_tapTimes[(m_tapCount - 1) % kTapHistorySize];
            if (now - last > 2.0) m_tapCount = 0;
        }
        m_tapTimes[m_tapCount % kTapHistorySize] = now;
        m_tapCount++;
        if (m_tapCount >= 2) {
            int n = std::min(m_tapCount - 1, kTapHistorySize - 1);
            int si = m_tapCount - n - 1;
            double total = m_tapTimes[(m_tapCount - 1) % kTapHistorySize]
                         - m_tapTimes[si % kTapHistorySize];
            double avg = total / n;
            double bpm = std::clamp(std::round(60.0 / avg * 100.0) / 100.0, 20.0, 999.0);
            if (m_engine)
                m_engine->sendCommand(audio::TransportSetBPMMsg{bpm});
        }
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, preferredHeight()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
        if (!m_project || !m_engine) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w,  h = m_bounds.h;

        r.drawRect(x, y, w, h, Theme::background);

        float transportY = y;
        float headerY    = transportY + Theme::kTransportBarHeight;
        float gridY      = headerY + Theme::kTrackHeaderHeight;
        float gridH      = h - Theme::kTransportBarHeight - Theme::kTrackHeaderHeight - kScrollbarH;
        float gridX      = x + Theme::kSceneLabelWidth;
        float gridW      = w - Theme::kSceneLabelWidth;

        paintTransportBar(r, f, x, transportY, w);
        paintTrackHeaders(r, f, gridX, headerY, gridW);
        paintSceneLabels(r, f, x, gridY, gridH);
        paintClipGrid(r, f, gridX, gridY, gridW, gridH);
        paintHScrollbar(r, gridX, gridY + gridH, gridW);
    }

    // ─── Mouse events ───────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (!m_project || !m_engine) return false;

        float mx = e.x, my = e.y;
        bool rightClick = (e.button == MouseButton::Right);
        float x = m_bounds.x, y = m_bounds.y;
        float gridX   = x + Theme::kSceneLabelWidth;
        float headerY = y + Theme::kTransportBarHeight;
        float gridY   = headerY + Theme::kTrackHeaderHeight;

        // Transport bar clicks
        if (my < headerY && my >= y) {
            if (mx >= m_tapButtonX && mx <= m_tapButtonX + m_tapButtonW &&
                my >= m_tapButtonY && my <= m_tapButtonY + m_tapButtonH) {
                tapTempo();
                return true;
            }
            if (m_editMode != EditMode::None) {
                m_editMode = EditMode::None;
                m_editBuffer.clear();
            }
            return true;
        }

        // Horizontal scrollbar
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float sbY = gridY + (m_bounds.h - Theme::kTransportBarHeight - Theme::kTrackHeaderHeight - kScrollbarH);
        if (my >= sbY && my < sbY + kScrollbarH && mx >= gridX && mx < gridX + gridW) {
            float contentW = m_project->numTracks() * Theme::kTrackWidth;
            float maxScroll = std::max(0.0f, contentW - gridW);
            if (maxScroll <= 0) return true;
            float thumbW = std::max(20.0f, gridW * (gridW / std::max(1.0f, contentW)));
            float scrollFrac = m_scrollX / std::max(1.0f, maxScroll);
            float thumbX = gridX + scrollFrac * (gridW - thumbW);
            if (mx >= thumbX && mx < thumbX + thumbW) {
                m_hsbDragging = true;
                m_hsbDragStartX = mx;
                m_hsbDragStartScroll = m_scrollX;
                captureMouse();
            } else {
                float clickFrac = (mx - gridX) / gridW;
                m_scrollX = std::clamp(clickFrac * maxScroll, 0.0f, maxScroll);
            }
            return true;
        }

        // Track header click — select track
        if (my >= headerY && my < gridY && mx >= gridX) {
            float cmx = mx + m_scrollX;
            int ti = static_cast<int>((cmx - gridX) / Theme::kTrackWidth);
            if (ti >= 0 && ti < m_project->numTracks()) {
                m_selectedTrack = ti;
                m_lastClickTrack = ti;
                return true;
            }
        }

        if (my < gridY) return false;

        float cmx = mx + m_scrollX;
        float cmy = my + m_scrollY;

        // Scene label click — launch scene
        if (mx >= x && mx < gridX) {
            int si = static_cast<int>((cmy - gridY) / Theme::kClipSlotHeight);
            if (si >= 0 && si < m_project->numScenes()) {
                for (int t = 0; t < m_project->numTracks(); ++t) {
                    auto* slot = m_project->getSlot(t, si);
                    if (slot && slot->audioClip)
                        m_engine->sendCommand(audio::LaunchClipMsg{t, slot->audioClip.get()});
                    else if (slot && slot->midiClip)
                        m_engine->sendCommand(audio::LaunchMidiClipMsg{t, slot->midiClip.get()});
                    else {
                        m_engine->sendCommand(audio::StopClipMsg{t});
                        m_engine->sendCommand(audio::StopMidiClipMsg{t});
                    }
                }
                m_activeScene = si;
                return true;
            }
        }

        // Clip grid click
        if (mx >= gridX) {
            int ti = static_cast<int>((cmx - gridX) / Theme::kTrackWidth);
            int si = static_cast<int>((cmy - gridY) / Theme::kClipSlotHeight);
            if (ti >= 0 && ti < m_project->numTracks() &&
                si >= 0 && si < m_project->numScenes()) {
                m_selectedTrack  = ti;
                m_lastClickTrack = ti;
                auto* slot = m_project->getSlot(ti, si);
                if (rightClick) {
                    m_engine->sendCommand(audio::StopClipMsg{ti});
                    m_engine->sendCommand(audio::StopMidiClipMsg{ti});
                } else if (slot && slot->audioClip) {
                    m_engine->sendCommand(audio::LaunchClipMsg{ti, slot->audioClip.get()});
                } else if (slot && slot->midiClip) {
                    m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, slot->midiClip.get()});
                }
                return true;
            }
        }
        return false;
    }

    // Track selected by last click (for App to sync selection)
    int lastClickTrack() const { int t = m_lastClickTrack; return t; }
    void clearLastClickTrack() { m_lastClickTrack = -1; }

    bool onMouseMove(MouseMoveEvent& e) override {
        float mx = e.x, my = e.y;
        if (m_hsbDragging && m_project) {
            float gridW = m_bounds.w - Theme::kSceneLabelWidth;
            float contentW = m_project->numTracks() * Theme::kTrackWidth;
            float maxScroll = std::max(0.0f, contentW - gridW);
            float delta = mx - m_hsbDragStartX;
            float scrollDelta = delta * (contentW / std::max(1.0f, gridW));
            m_scrollX = std::clamp(m_hsbDragStartScroll + scrollDelta, 0.0f, maxScroll);
            return true;
        }
        // Track scrollbar hover
        float gridX = m_bounds.x + Theme::kSceneLabelWidth;
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + Theme::kTransportBarHeight + Theme::kTrackHeaderHeight;
        float gridH = m_bounds.h - Theme::kTransportBarHeight - Theme::kTrackHeaderHeight - kScrollbarH;
        float sbY = gridY + gridH;
        m_hsbHovered = (my >= sbY && my < sbY + kScrollbarH && mx >= gridX && mx < gridX + gridW);
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_hsbDragging) {
            m_hsbDragging = false;
            releaseMouse();
            return true;
        }
        return false;
    }

private:
    // ─── Text helper ────────────────────────────────────────────────────

    void drawText(Renderer2D& r, Font& f, const char* text,
                  float x, float y, float scale, Color color) {
        if (!f.isLoaded()) return;
        float tx = x;
        for (const char* p = text; *p; ++p) {
            auto g = f.getGlyph(*p, tx, y, scale);
            r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                               g.u0, g.v0, g.u1, g.v1, color, f.textureId());
            tx += g.xAdvance;
        }
    }

    float drawTextRet(Renderer2D& r, Font& f, const char* text,
                      float x, float y, float scale, Color color) {
        if (!f.isLoaded()) return x;
        float tx = x;
        for (const char* p = text; *p; ++p) {
            auto g = f.getGlyph(*p, tx, y, scale);
            r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                               g.u0, g.v0, g.u1, g.v1, color, f.textureId());
            tx += g.xAdvance;
        }
        return tx;
    }

    // ─── Transport Bar ──────────────────────────────────────────────────

    void paintTransportBar(Renderer2D& r, Font& f, float x, float y, float w) {
        float h = Theme::kTransportBarHeight;
        r.drawRect(x, y, w, h, Theme::transportBg);
        r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

        float textY = y + 6;
        float scale = Theme::kFontSize / f.pixelHeight();

        // Play/Stop indicator
        Color stateCol = m_transportPlaying ? Theme::playing : Theme::stopped;
        const char* stateTxt = m_transportPlaying ? "PLAYING" : "STOPPED";
        float circX = x + 12, circY = y + h * 0.5f - 5;
        r.drawRect(circX, circY, 10, 10, stateCol);
        drawText(r, f, stateTxt, circX + 18, textY, scale, stateCol);

        // BPM box
        float bpmX = x + 160, boxH = h - 8, boxY = y + 4, bpmW = 100;
        m_bpmBoxX = bpmX; m_bpmBoxY = boxY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;
        bool editBpm = (m_editMode == EditMode::BPM);
        Color bpmBg = editBpm ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(bpmX, boxY, bpmW, boxH, bpmBg);
        r.drawRectOutline(bpmX, boxY, bpmW, boxH,
                          editBpm ? Theme::transportAccent : Theme::clipSlotBorder);
        char bpmBuf[32];
        if (editBpm) std::snprintf(bpmBuf, sizeof(bpmBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(bpmBuf, sizeof(bpmBuf), "%.2f", m_transportBPM);
        float tx = drawTextRet(r, f, bpmBuf, bpmX + 6, textY, scale,
                               editBpm ? Theme::transportAccent : Theme::transportText);
        drawText(r, f, "BPM", bpmX + bpmW + 4, textY, scale * 0.85f, Theme::textSecondary);

        // Time signature boxes
        float tsX = bpmX + bpmW + 50, tsBoxW = 30, slashGap = 4;

        // Numerator
        bool editNum = (m_editMode == EditMode::TimeSigNum);
        m_tsNumBoxX = tsX; m_tsNumBoxY = boxY; m_tsNumBoxW = tsBoxW; m_tsNumBoxH = boxH;
        Color numBg = editNum ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(tsX, boxY, tsBoxW, boxH, numBg);
        r.drawRectOutline(tsX, boxY, tsBoxW, boxH,
                          editNum ? Theme::transportAccent : Theme::clipSlotBorder);
        char numBuf[8];
        if (editNum) std::snprintf(numBuf, sizeof(numBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(numBuf, sizeof(numBuf), "%d", m_transportNumerator);
        drawText(r, f, numBuf, tsX + 6, textY, scale,
                 editNum ? Theme::transportAccent : Theme::transportText);

        // Slash
        float slashX = tsX + tsBoxW + slashGap;
        slashX = drawTextRet(r, f, "/", slashX, textY, scale, Theme::textSecondary);

        // Denominator
        float denX = slashX + slashGap;
        bool editDen = (m_editMode == EditMode::TimeSigDen);
        m_tsDenBoxX = denX; m_tsDenBoxY = boxY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
        Color denBg = editDen ? Color{60,60,80,255} : Color{40,40,50,255};
        r.drawRect(denX, boxY, tsBoxW, boxH, denBg);
        r.drawRectOutline(denX, boxY, tsBoxW, boxH,
                          editDen ? Theme::transportAccent : Theme::clipSlotBorder);
        char denBuf[8];
        if (editDen) std::snprintf(denBuf, sizeof(denBuf), "%s_", m_editBuffer.c_str());
        else         std::snprintf(denBuf, sizeof(denBuf), "%d", m_transportDenominator);
        drawText(r, f, denBuf, denX + 6, textY, scale,
                 editDen ? Theme::transportAccent : Theme::transportText);

        // Tap tempo button
        float tapX = denX + tsBoxW + 16, tapW = 48;
        m_tapButtonX = tapX; m_tapButtonY = boxY; m_tapButtonW = tapW; m_tapButtonH = boxH;
        m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
        uint8_t fR = static_cast<uint8_t>(40 + m_tapFlash * 60);
        uint8_t fG = static_cast<uint8_t>(40 + m_tapFlash * 80);
        Color tapBg = {fR, fG, 50, 255};
        r.drawRect(tapX, boxY, tapW, boxH, tapBg);
        r.drawRectOutline(tapX, boxY, tapW, boxH, Theme::clipSlotBorder);
        drawText(r, f, "TAP", tapX + 6, textY, scale * 0.85f, Theme::transportText);

        // Bar.Beat position
        int bpb = std::max(1, m_transportNumerator);
        int bar  = static_cast<int>(m_transportBeats / bpb) + 1;
        int beat = static_cast<int>(std::fmod(m_transportBeats, (double)bpb)) + 1;
        char posBuf[32];
        std::snprintf(posBuf, sizeof(posBuf), "%d . %d", bar, beat);
        drawText(r, f, posBuf, tapX + tapW + 20, textY, scale, Theme::transportAccent);
    }

    // ─── Track Headers ──────────────────────────────────────────────────

    void paintTrackHeaders(Renderer2D& r, Font& f, float x, float y, float w) {
        if (!m_project) return;
        float h = Theme::kTrackHeaderHeight;
        r.drawRect(x, y, w, h, Theme::trackHeaderBg);
        r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

        r.pushClip(x, y, w, h);
        float scale = Theme::kSmallFontSize / f.pixelHeight();
        float smallScale = scale * 0.7f;

        for (int t = 0; t < m_project->numTracks(); ++t) {
            float tx = x + t * Theme::kTrackWidth - m_scrollX;
            float tw = Theme::kTrackWidth;
            if (tx + tw < x || tx > x + w) continue;

            if (t == m_selectedTrack)
                r.drawRect(tx + 1, y + 1, tw - 2, h - 2, Color{50, 55, 65, 255});

            Color col = Theme::trackColors[m_project->track(t).colorIndex % Theme::kNumTrackColors];
            r.drawRect(tx + 2, y + 2, tw - 4, 3, col);

            if (f.isLoaded()) {
                float textX = tx + 6, textY = y + 8;
                for (char c : m_project->track(t).name) {
                    auto g = f.getGlyph(c, textX, textY, scale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textPrimary, f.textureId());
                    textX += g.xAdvance;
                    if (textX > tx + tw - 6) break;
                }
            }

            if (f.isLoaded()) {
                bool isMidi = (m_project->track(t).type == Track::Type::Midi);
                const char* label = isMidi ? "MIDI" : "Audio";
                Color typeCol = isMidi ? Color{180,130,255} : Color{130,200,130};
                f.drawText(r, label, tx + 6, y + 22, smallScale, typeCol);
            }

            r.drawRect(tx + tw - 1, y, 1, h, Theme::clipSlotBorder);
        }
        r.popClip();
    }

    // ─── Scene Labels ───────────────────────────────────────────────────

    void paintSceneLabels(Renderer2D& r, Font& f, float x, float y, float h) {
        if (!m_project) return;
        float w = Theme::kSceneLabelWidth;
        r.drawRect(x, y, w, h, Theme::sceneLabelBg);

        r.pushClip(x, y, w, h);
        float scale = Theme::kSmallFontSize / f.pixelHeight();

        for (int s = 0; s < m_project->numScenes(); ++s) {
            float sy = y + s * Theme::kClipSlotHeight - m_scrollY;
            float sh = Theme::kClipSlotHeight;
            if (sy + sh < y || sy > y + h) continue;

            r.drawRect(x + 2, sy + 2, w - 4, sh - 4, Theme::clipSlotEmpty);

            if (f.isLoaded()) {
                float textX = x + 8;
                float textY = sy + sh * 0.5f - Theme::kSmallFontSize * 0.5f;
                char label[8];
                std::snprintf(label, sizeof(label), "%s",
                              m_project->scene(s).name.c_str());
                for (const char* p = label; *p; ++p) {
                    auto g = f.getGlyph(*p, textX, textY, scale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textSecondary, f.textureId());
                    textX += g.xAdvance;
                }
            }
            r.drawRect(x, sy + sh - 1, w, 1, Theme::clipSlotBorder);
        }
        r.popClip();
    }

    // ─── Clip Grid ──────────────────────────────────────────────────────

    void paintClipGrid(Renderer2D& r, Font& f, float x, float y, float w, float h) {
        if (!m_project) return;
        r.pushClip(x, y, w, h);
        for (int t = 0; t < m_project->numTracks(); ++t) {
            for (int s = 0; s < m_project->numScenes(); ++s) {
                float sx = x + t * Theme::kTrackWidth - m_scrollX;
                float sy = y + s * Theme::kClipSlotHeight - m_scrollY;
                float sw = Theme::kTrackWidth;
                float sh = Theme::kClipSlotHeight;
                if (sx + sw < x || sx > x + w) continue;
                if (sy + sh < y || sy > y + h) continue;
                paintClipSlot(r, f, t, s, sx, sy, sw, sh);
            }
        }
        r.popClip();
    }

    void paintHScrollbar(Renderer2D& r, float x, float y, float w) {
        r.drawRect(m_bounds.x, y, m_bounds.w, kScrollbarH, Color{40, 40, 45});
        if (!m_project) return;
        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        if (contentW <= w) return;
        float thumbW = std::max(20.0f, w * (w / std::max(1.0f, contentW)));
        float maxScroll = contentW - w;
        float scrollFrac = m_scrollX / std::max(1.0f, maxScroll);
        float thumbX = x + scrollFrac * (w - thumbW);
        Color thumbCol = (m_hsbDragging || m_hsbHovered) ? Color{120, 120, 130} : Color{90, 90, 100};
        r.drawRect(thumbX, y, thumbW, kScrollbarH, thumbCol);
    }

    void paintClipSlot(Renderer2D& r, Font& f, int ti, int si,
                       float x, float y, float w, float h) {
        float pad = Theme::kSlotPadding;
        float ix = x + pad, iy = y + pad;
        float iw = w - pad * 2, ih = h - pad * 2;

        const auto* slot = m_project->getSlot(ti, si);
        bool hasClip = slot && !slot->empty();
        const audio::Clip* aClip = slot ? slot->audioClip.get() : nullptr;
        const midi::MidiClip* mClip = slot ? slot->midiClip.get() : nullptr;
        bool isPlaying = m_trackStates[ti].playing && hasClip;

        Color bgCol = hasClip ? Theme::panelBg : Theme::clipSlotEmpty;
        r.drawRect(ix, iy, iw, ih, bgCol);

        if (hasClip) {
            Color trkCol = Theme::trackColors[
                m_project->track(ti).colorIndex % Theme::kNumTrackColors];
            r.drawRect(ix, iy, 3, ih, trkCol);

            // Clip name
            const std::string& name = aClip ? aClip->name
                : (mClip ? mClip->name() : std::string());
            float scale = Theme::kSmallFontSize / f.pixelHeight();
            if (f.isLoaded() && !name.empty()) {
                float tx2 = ix + 7, ty2 = iy + 2;
                for (char c : name) {
                    if (c == '/' || c == '\\') { tx2 = ix + 7; continue; }
                    auto g = f.getGlyph(c, tx2, ty2, scale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1-g.x0, g.y1-g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textPrimary, f.textureId());
                    tx2 += g.xAdvance;
                    if (tx2 > ix + iw - 4) break;
                }
            }

            // Audio waveform
            if (aClip && aClip->buffer && aClip->buffer->numFrames() > 0) {
                float wfY = iy + 18, wfH = ih - 22;
                Color wfCol = trkCol.withAlpha(160);
                r.drawWaveform(aClip->buffer->channelData(0),
                               aClip->buffer->numFrames(),
                               ix + 4, wfY, iw - 8, wfH, wfCol);
                if (isPlaying) {
                    int64_t pos = m_trackStates[ti].playPosition;
                    float frac = std::fmod(
                        static_cast<float>(pos) / aClip->buffer->numFrames(), 1.0f);
                    r.drawRect(ix + 4 + frac * (iw - 8), wfY, 2, wfH, Theme::playing);
                }
            }

            // MIDI notes
            if (mClip && mClip->noteCount() > 0) {
                float nY = iy + 18, nH = ih - 22;
                Color noteCol = trkCol.withAlpha(180);
                int minP = 127, maxP = 0;
                for (int i = 0; i < mClip->noteCount(); ++i) {
                    int p = mClip->note(i).pitch;
                    if (p < minP) minP = p;
                    if (p > maxP) maxP = p;
                }
                int pRange = std::max(1, maxP - minP + 1);
                double len = mClip->lengthBeats();
                for (int i = 0; i < mClip->noteCount(); ++i) {
                    const auto& n = mClip->note(i);
                    float nx = ix + 4 + static_cast<float>(n.startBeat / len) * (iw - 8);
                    float nw = std::max(1.0f,
                        static_cast<float>(n.duration / len) * (iw - 8));
                    float ny = nY + nH -
                        (static_cast<float>(n.pitch - minP + 1) / pRange) * nH;
                    float nh = std::max(1.0f, nH / pRange);
                    r.drawRect(nx, ny, nw, nh, noteCol);
                }
            }

            // Playing border pulse
            if (isPlaying) {
                float pulse = (std::sin(m_animTimer * 6.0f) + 1.0f) * 0.5f;
                Color bc = Theme::playing.withAlpha(
                    static_cast<uint8_t>(150 + pulse * 105));
                r.drawRectOutline(ix, iy, iw, ih, bc, 2.0f);
            }
        }

        r.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
        r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    ClipSlotUIState m_trackStates[kMaxTracks] = {};

    bool   m_transportPlaying     = false;
    double m_transportBeats       = 0.0;
    double m_transportBPM         = 120.0;
    int    m_transportNumerator   = 4;
    int    m_transportDenominator = 4;

    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;

    bool  m_hsbDragging      = false;
    bool  m_hsbHovered       = false;
    float m_hsbDragStartX    = 0;
    float m_hsbDragStartScroll = 0;

    int   m_activeScene    = -1;
    int   m_selectedTrack  = 0;
    int   m_lastClickTrack = -1;
    float m_animTimer      = 0.0f;

    enum class EditMode { None, BPM, TimeSigNum, TimeSigDen };
    EditMode    m_editMode = EditMode::None;
    std::string m_editBuffer;

    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsNumBoxX = 0, m_tsNumBoxY = 0, m_tsNumBoxW = 0, m_tsNumBoxH = 0;
    float m_tsDenBoxX = 0, m_tsDenBoxY = 0, m_tsDenBoxW = 0, m_tsDenBoxH = 0;
    float m_tapButtonX = 0, m_tapButtonY = 0, m_tapButtonW = 0, m_tapButtonH = 0;

    static constexpr float kScrollbarH    = 12.0f;
    static constexpr int kVisibleScenes  = 8;
    static constexpr int kTapHistorySize = 4;
    double m_tapTimes[kTapHistorySize] = {};
    int    m_tapCount = 0;
    float  m_tapFlash = 0.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
