// SessionPanel.cpp — rendering and event implementations.
// Split from SessionPanel.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "SessionPanel.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "../Renderer.h"
#include "../Font.h"

namespace yawn {
namespace ui {
namespace fw {

void SessionPanel::paint(UIContext& ctx) {
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

bool SessionPanel::onMouseDown(MouseEvent& e) {
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
                    m_engine->sendCommand(audio::LaunchClipMsg{t, si, slot->audioClip.get(), slot->launchQuantize});
                else if (slot && slot->midiClip)
                    m_engine->sendCommand(audio::LaunchMidiClipMsg{t, si, slot->midiClip.get(), slot->launchQuantize});
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
            m_lastClickScene = si;
            m_selectedScene = si;
            auto* slot = m_project->getSlot(ti, si);
            bool hasClip = slot && !slot->empty();
            bool isPlaying = m_trackStates[ti].playing && m_trackStates[ti].playingScene == si;
            bool trackArmed = m_project->track(ti).armed;

            // Compute slot-local position
            float slotLocalX = cmx - gridX - ti * Theme::kTrackWidth;

            bool isSlotRecording = m_trackStates[ti].recording
                               && m_trackStates[ti].recordingScene == si;

            if (rightClick) {
                m_lastRightClickTrack = ti;
                m_lastRightClickScene = si;
                m_selectedTrack  = ti;
                m_lastClickTrack = ti;
            } else if (isSlotRecording) {
                auto recQ = m_project->track(ti).recordQuantize;
                if (m_project->track(ti).type == Track::Type::Midi)
                    m_engine->sendCommand(audio::StopMidiRecordMsg{ti, recQ});
                else
                    m_engine->sendCommand(audio::StopAudioRecordMsg{ti, recQ});
            } else if (slotLocalX < kIconZoneW + Theme::kSlotPadding) {
                if (isPlaying) {
                    if (slot->audioClip)
                        m_engine->sendCommand(audio::StopClipMsg{ti});
                    else
                        m_engine->sendCommand(audio::StopMidiClipMsg{ti});
                } else if (hasClip) {
                    auto lq = slot->launchQuantize;
                    if (slot->audioClip)
                        m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(), lq});
                    else if (slot->midiClip)
                        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(), lq});
                } else if (trackArmed) {
                    if (m_project->track(ti).type == Track::Type::Midi)
                        m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, true});
                    else
                        m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, true});
                }
            } else {
                m_selectedTrack  = ti;
                m_lastClickTrack = ti;
                if (slot && slot->audioClip) {
                    m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(), slot->launchQuantize});
                } else if (slot && slot->midiClip) {
                    m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(), slot->launchQuantize});
                } else if (trackArmed) {
                    if (m_project->track(ti).type == Track::Type::Midi)
                        m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, !e.mods.shift});
                    else
                        m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, !e.mods.shift});
                }
            }
            return true;
        }
    }
    return false;
}

void SessionPanel::drawText(Renderer2D& r, Font& f, const char* text,
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

float SessionPanel::drawTextRet(Renderer2D& r, Font& f, const char* text,
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

void SessionPanel::paintTrackHeaders(Renderer2D& r, Font& f, float x, float y, float w) {
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

void SessionPanel::paintSceneLabels(Renderer2D& r, Font& f, float x, float y, float h) {
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

void SessionPanel::paintClipGrid(Renderer2D& r, Font& f, float x, float y, float w, float h) {
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

void SessionPanel::paintHScrollbar(Renderer2D& r, float x, float y, float w) {
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

void SessionPanel::paintClipSlot(Renderer2D& r, Font& f, int ti, int si,
                   float x, float y, float w, float h) {
    float pad = Theme::kSlotPadding;
    float ix = x + pad, iy = y + pad;
    float iw = w - pad * 2, ih = h - pad * 2;

    const auto* slot = m_project->getSlot(ti, si);
    bool hasClip = slot && !slot->empty();
    const audio::Clip* aClip = slot ? slot->audioClip.get() : nullptr;
    const midi::MidiClip* mClip = slot ? slot->midiClip.get() : nullptr;
    bool isPlaying = m_trackStates[ti].playing && m_trackStates[ti].playingScene == si;
    bool trackArmed = m_project->track(ti).armed;
    bool recReady = !hasClip && trackArmed;
    bool recFullyArmed = recReady && m_globalRecordArmed;
    bool isRecording = m_trackStates[ti].recording
                    && m_trackStates[ti].recordingScene == si;
    bool isHovered = (m_hoveredTrack == ti && m_hoveredScene == si);
    bool iconHovered = isHovered && m_hoveredIcon;

    Color bgCol = hasClip ? Theme::panelBg : Theme::clipSlotEmpty;
    r.drawRect(ix, iy, iw, ih, bgCol);

    // Icon zone (left side)
    float iconCX = ix + kIconZoneW * 0.5f;
    float iconCY = iy + ih * 0.5f;
    Color iconBg = iconHovered ? Color{45, 45, 50, 255} : Color{30, 30, 33, 255};
    r.drawRect(ix, iy, kIconZoneW, ih, iconBg);

    if (isRecording) {
        // Pulsing record circle
        float pulse = (std::sin(m_animTimer * 4.0f) + 1.0f) * 0.5f;
        uint8_t a = static_cast<uint8_t>(150 + static_cast<int>(pulse * 105));
        Color recCol = Color{220, 40, 40, a};
        r.drawFilledCircle(iconCX, iconCY, 5.0f, recCol, 16);
    } else if (isPlaying) {
        // Stop square (green)
        Color stopCol = iconHovered ? Color{100, 255, 100} : Theme::playing;
        float half = 4.5f;
        r.drawRect(iconCX - half, iconCY - half, half * 2, half * 2, stopCol);
    } else if (hasClip) {
        // Play triangle
        Color trkCol = Theme::trackColors[
            m_project->track(ti).colorIndex % Theme::kNumTrackColors];
        Color triCol = iconHovered ? trkCol : trkCol.withAlpha(180);
        r.drawTriangle(iconCX - 3.0f, iconCY - 5.5f,
                        iconCX - 3.0f, iconCY + 5.5f,
                        iconCX + 5.0f, iconCY, triCol);
    } else if (recReady) {
        // Record-ready circle
        Color recCol = recFullyArmed
            ? (iconHovered ? Color{230, 50, 50} : Color{200, 40, 40})
            : (iconHovered ? Color{170, 60, 60} : Color{140, 50, 50});
        r.drawFilledCircle(iconCX, iconCY, 4.5f, recCol, 16);
    }

    // Clip content (right of icon zone)
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
    }

    // Recording border pulse (outside hasClip block for armed empty slots too)
    if (isRecording) {
        float pulse = (std::sin(m_animTimer * 4.0f) + 1.0f) * 0.5f;
        Color recCol = Color{220, 40, 40}.withAlpha(
            static_cast<uint8_t>(150 + static_cast<int>(pulse * 105)));
        r.drawRectOutline(ix, iy, iw, ih, recCol, 2.0f);
    }

    r.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
    r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);
}

} // namespace fw
} // namespace ui
} // namespace yawn
