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
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

struct ClipSlotUIState {
    bool    playing      = false;
    bool    queued       = false;
    bool    recording    = false;
    int64_t playPosition = 0;
    // MIDI playhead: position as fraction of clip length (0.0–1.0)
    float   midiPlayFrac = 0.0f;
    bool    isMidiPlaying = false;
};

class SessionPanel : public Widget {
public:
    SessionPanel() = default;

    void init(Project* project, audio::AudioEngine* engine) {
        m_project = project;
        m_engine  = engine;
    }

    // ─── State updates (called from App) ────────────────────────────────

    void updateClipState(int trackIndex, bool playing, int64_t playPos,
                         bool isMidi = false, double clipLengthBeats = 0.0) {
        if (trackIndex >= 0 && trackIndex < kMaxTracks) {
            m_trackStates[trackIndex].playing      = playing;
            m_trackStates[trackIndex].playPosition  = playPos;
            if (isMidi && clipLengthBeats > 0.0) {
                double beats = static_cast<double>(playPos) / 1000000.0;
                m_trackStates[trackIndex].midiPlayFrac =
                    static_cast<float>(std::fmod(beats, clipLengthBeats) / clipLengthBeats);
                m_trackStates[trackIndex].isMidiPlaying = playing;
            } else {
                m_trackStates[trackIndex].isMidiPlaying = false;
            }
        }
    }

    void setSelectedTrack(int t) { m_selectedTrack = t; }
    int  selectedTrack() const   { return m_selectedTrack; }
    void setSelectedScene(int s) { m_selectedScene = s; }
    int  selectedScene() const   { return m_selectedScene; }
    float scrollX() const        { return m_scrollX; }
    float scrollY() const        { return m_scrollY; }
    void setScrollX(float sx)    { m_scrollX = sx; }

    void setGlobalRecordArmed(bool armed) { m_globalRecordArmed = armed; }
    void updateAnimTimer(float dt) { m_animTimer += dt; }

    float preferredHeight() const {
        int scenes = m_project
            ? std::min(m_project->numScenes(), kVisibleScenes) : kVisibleScenes;
        return Theme::kTrackHeaderHeight
             + scenes * Theme::kClipSlotHeight;
    }

    // ─── Scroll callback ───────────────────────────────────────────────
    void setOnScrollChanged(std::function<void(float)> cb) { m_onScrollChanged = std::move(cb); }

    // ─── Slot query (used by App for double-click → piano roll) ─────────

    bool getSlotAt(float mx, float my, int& trackOut, int& sceneOut) const {
        if (!m_project) return false;
        float gridX = m_bounds.x + Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + Theme::kTrackHeaderHeight;
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

    // ─── Scroll (forwarded from App) ────────────────────────────────────

    void handleScroll(float dx, float dy) {
        if (!m_project) return;
        float gridH = m_bounds.h - Theme::kTrackHeaderHeight - kScrollbarH;
        float contentH = m_project->numScenes() * Theme::kClipSlotHeight;
        float maxY = std::max(0.0f, contentH - gridH);
        m_scrollY = std::clamp(m_scrollY - dy * 30.0f, 0.0f, maxY);

        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float contentW = m_project->numTracks() * Theme::kTrackWidth;
        float maxX = std::max(0.0f, contentW - gridW);
        m_scrollX = std::clamp(m_scrollX - dx * 30.0f, 0.0f, maxX);
        if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
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

        float headerY = y;
        float gridY   = headerY + Theme::kTrackHeaderHeight;
        float gridH   = h - Theme::kTrackHeaderHeight - kScrollbarH;
        float gridX   = x + Theme::kSceneLabelWidth;
        float gridW   = w - Theme::kSceneLabelWidth;

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
        float headerY = y;
        float gridY   = headerY + Theme::kTrackHeaderHeight;

        // Horizontal scrollbar
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float sbY = gridY + (m_bounds.h - Theme::kTrackHeaderHeight - kScrollbarH);
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
                if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
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
                m_lastClickScene = si;
                m_selectedScene = si;
                auto* slot = m_project->getSlot(ti, si);
                bool hasClip = slot && !slot->empty();
                bool isPlaying = m_trackStates[ti].playing && hasClip;
                bool trackArmed = m_project->track(ti).armed;

                // Compute slot-local position
                float slotLocalX = cmx - gridX - ti * Theme::kTrackWidth;

                if (rightClick) {
                    m_lastRightClickTrack = ti;
                    m_lastRightClickScene = si;
                } else if (slotLocalX < kIconZoneW + Theme::kSlotPadding) {
                    // Icon zone click — trigger action
                    if (isPlaying) {
                        // Stop the playing clip
                        if (slot->audioClip)
                            m_engine->sendCommand(audio::StopClipMsg{ti});
                        else
                            m_engine->sendCommand(audio::StopMidiClipMsg{ti});
                    } else if (hasClip) {
                        // Launch the clip
                        if (slot->audioClip)
                            m_engine->sendCommand(audio::LaunchClipMsg{ti, slot->audioClip.get()});
                        else if (slot->midiClip)
                            m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, slot->midiClip.get()});
                    } else if (trackArmed && m_globalRecordArmed) {
                        // Start recording into empty slot
                        if (m_project->track(ti).type == Track::Type::Midi)
                            m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, true});
                        else
                            m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si});
                    }
                } else {
                    // Content area click — launch or select
                    if (slot && slot->audioClip) {
                        m_engine->sendCommand(audio::LaunchClipMsg{ti, slot->audioClip.get()});
                    } else if (slot && slot->midiClip) {
                        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, slot->midiClip.get()});
                    } else if (trackArmed && m_globalRecordArmed) {
                        if (m_project->track(ti).type == Track::Type::Midi)
                            m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, !e.mods.shift});
                        else
                            m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si});
                    }
                }
                return true;
            }
        }
        return false;
    }

    // Track selected by last click (for App to sync selection)
    int lastClickTrack() const { return m_lastClickTrack; }
    int lastClickScene() const { return m_lastClickScene; }
    void clearLastClickTrack() { m_lastClickTrack = -1; m_lastClickScene = -1; }

    // Right-click on clip slot (for App to show context menu)
    int lastRightClickTrack() const { return m_lastRightClickTrack; }
    int lastRightClickScene() const { return m_lastRightClickScene; }
    void clearRightClick() { m_lastRightClickTrack = -1; m_lastRightClickScene = -1; }

    bool onMouseMove(MouseMoveEvent& e) override {
        float mx = e.x, my = e.y;
        if (m_hsbDragging && m_project) {
            float gridW = m_bounds.w - Theme::kSceneLabelWidth;
            float contentW = m_project->numTracks() * Theme::kTrackWidth;
            float maxScroll = std::max(0.0f, contentW - gridW);
            float delta = mx - m_hsbDragStartX;
            float scrollDelta = delta * (contentW / std::max(1.0f, gridW));
            m_scrollX = std::clamp(m_hsbDragStartScroll + scrollDelta, 0.0f, maxScroll);
            if (m_onScrollChanged) m_onScrollChanged(m_scrollX);
            return true;
        }
        // Track scrollbar hover
        float gridX = m_bounds.x + Theme::kSceneLabelWidth;
        float gridW = m_bounds.w - Theme::kSceneLabelWidth;
        float gridY = m_bounds.y + Theme::kTrackHeaderHeight;
        float gridH = m_bounds.h - Theme::kTrackHeaderHeight - kScrollbarH;
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

    static constexpr float kIconZoneW = 16.0f;

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
        bool trackArmed = m_project->track(ti).armed;
        bool recReady = !hasClip && trackArmed;
        bool recFullyArmed = recReady && m_globalRecordArmed;
        bool isRecording = m_trackStates[ti].recording;

        Color bgCol = hasClip ? Theme::panelBg : Theme::clipSlotEmpty;
        r.drawRect(ix, iy, iw, ih, bgCol);

        // --- Icon zone (left 16px) ---
        float iconX = ix + 2;
        float iconCY = iy + ih * 0.5f;
        r.drawRect(ix, iy, kIconZoneW, ih, Color{30, 30, 33, 255});

        if (isRecording) {
            // Pulsing red record circle
            float pulse = (std::sin(m_animTimer * 4.0f) + 1.0f) * 0.5f;
            uint8_t a = static_cast<uint8_t>(150 + static_cast<int>(pulse * 105));
            Color recCol = Color{220, 40, 40, a};
            // Approximate circle with cross-shaped rects
            r.drawRect(iconX + 2, iconCY - 4, 8, 8, recCol);
            r.drawRect(iconX + 3, iconCY - 5, 6, 10, recCol);
            r.drawRect(iconX + 4, iconCY - 5, 4, 10, recCol);
        } else if (isPlaying) {
            // Stop square (click to stop)
            Color stopCol = Theme::playing;
            r.drawRect(iconX + 3, iconCY - 4, 8, 8, stopCol);
        } else if (hasClip) {
            // Play triangle (click to launch)
            Color trkCol = Theme::trackColors[
                m_project->track(ti).colorIndex % Theme::kNumTrackColors];
            Color triCol = trkCol.withAlpha(180);
            r.drawRect(iconX + 2, iconCY - 5, 2, 10, triCol);
            r.drawRect(iconX + 4, iconCY - 4, 2, 8, triCol);
            r.drawRect(iconX + 6, iconCY - 3, 2, 6, triCol);
            r.drawRect(iconX + 8, iconCY - 2, 2, 4, triCol);
            r.drawRect(iconX + 10, iconCY - 1, 2, 2, triCol);
        } else if (recReady) {
            // Record-ready circle outline (brighter when global record is armed)
            Color recCol = recFullyArmed ? Color{200, 40, 40} : Color{140, 50, 50};
            r.drawRectOutline(iconX + 2, iconCY - 4, 8, 8, recCol, 1.5f);
        }

        // --- Clip content (right of icon zone) ---
        float contentX = ix + kIconZoneW;
        float contentW = iw - kIconZoneW;

        if (hasClip) {
            Color trkCol = Theme::trackColors[
                m_project->track(ti).colorIndex % Theme::kNumTrackColors];
            r.drawRect(contentX, iy, 2, ih, trkCol);

            // Clip name
            const std::string& name = aClip ? aClip->name
                : (mClip ? mClip->name() : std::string());
            float scale = Theme::kSmallFontSize / f.pixelHeight();
            if (f.isLoaded() && !name.empty()) {
                float tx2 = contentX + 5, ty2 = iy + 2;
                for (char c : name) {
                    if (c == '/' || c == '\\') { tx2 = contentX + 5; continue; }
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
                               contentX + 4, wfY, contentW - 8, wfH, wfCol);
                if (isPlaying) {
                    int64_t pos = m_trackStates[ti].playPosition;
                    float frac = std::fmod(
                        static_cast<float>(pos) / aClip->buffer->numFrames(), 1.0f);
                    r.drawRect(contentX + 4 + frac * (contentW - 8), wfY, 2, wfH, Theme::playing);
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
                    float nx = contentX + 4 + static_cast<float>(n.startBeat / len) * (contentW - 8);
                    float nw = std::max(1.0f,
                        static_cast<float>(n.duration / len) * (contentW - 8));
                    float ny = nY + nH -
                        (static_cast<float>(n.pitch - minP + 1) / pRange) * nH;
                    float nh = std::max(1.0f, nH / pRange);
                    r.drawRect(nx, ny, nw, nh, noteCol);
                }

                // MIDI playhead
                if (isPlaying && m_trackStates[ti].isMidiPlaying) {
                    float frac = m_trackStates[ti].midiPlayFrac;
                    float phX = contentX + 4 + frac * (contentW - 8);
                    r.drawRect(phX, nY, 2, nH, Theme::playing);
                }
            }

            // Playing border pulse
            if (isPlaying) {
                float pulse = (std::sin(m_animTimer * 6.0f) + 1.0f) * 0.5f;
                Color bc = Theme::playing.withAlpha(
                    static_cast<uint8_t>(150 + pulse * 105));
                r.drawRectOutline(ix, iy, iw, ih, bc, 2.0f);
            }

            // Recording border pulse
            if (isRecording) {
                float pulse = (std::sin(m_animTimer * 4.0f) + 1.0f) * 0.5f;
                Color recCol = Color{220, 40, 40}.withAlpha(
                    static_cast<uint8_t>(150 + static_cast<int>(pulse * 105)));
                r.drawRectOutline(ix, iy, iw, ih, recCol, 2.0f);
            }
        }

        r.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
        r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);
    }

    // ─── Data ───────────────────────────────────────────────────────────

    Project*            m_project = nullptr;
    audio::AudioEngine* m_engine  = nullptr;

    ClipSlotUIState m_trackStates[kMaxTracks] = {};

    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;

    bool  m_hsbDragging      = false;
    bool  m_hsbHovered       = false;
    float m_hsbDragStartX    = 0;
    float m_hsbDragStartScroll = 0;

    int   m_activeScene    = -1;
    int   m_selectedTrack  = 0;
    int   m_selectedScene  = 0;
    int   m_lastClickTrack = -1;
    int   m_lastClickScene = -1;
    int   m_lastRightClickTrack = -1;
    int   m_lastRightClickScene = -1;
    float m_animTimer      = 0.0f;
    bool  m_globalRecordArmed = false;

    std::function<void(float)> m_onScrollChanged;

    static constexpr float kScrollbarH    = 12.0f;
    static constexpr int kVisibleScenes  = 8;
};

} // namespace fw
} // namespace ui
} // namespace yawn
