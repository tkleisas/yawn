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

    // Track header click — select track (or double-click to rename)
    if (my >= headerY && my < gridY && mx >= gridX) {
        float cmx = mx + m_scrollX;
        int ti = static_cast<int>((cmx - gridX) / Theme::kTrackWidth);
        if (ti >= 0 && ti < m_project->numTracks()) {
            m_selectedTrack = ti;
            m_lastClickTrack = ti;
            if (e.isDoubleClick()) {
                startTrackRename(ti);
            }
            return true;
        }
    }

    if (my < gridY) return false;

    float cmx = mx + m_scrollX;
    float cmy = my + m_scrollY;

    // Scene label click — launch scene (left) or context menu (right)
    if (mx >= x && mx < gridX) {
        int si = static_cast<int>((cmy - gridY) / Theme::kClipSlotHeight);
        if (si >= 0 && si < m_project->numScenes()) {
            if (rightClick) {
                m_rightClickSceneLabel = si;
                return true;
            }
            for (int t = 0; t < m_project->numTracks(); ++t) {
                auto* slot = m_project->getSlot(t, si);
                if (slot && slot->audioClip) {
                    m_engine->sendCommand(audio::LaunchClipMsg{t, si, slot->audioClip.get(), slot->launchQuantize, &slot->clipAutomation, slot->followAction});
                    m_project->track(t).defaultScene = si;
                } else if (slot && slot->midiClip) {
                    m_engine->sendCommand(audio::LaunchMidiClipMsg{t, si, slot->midiClip.get(), slot->launchQuantize, &slot->clipAutomation, slot->followAction});
                    m_project->track(t).defaultScene = si;
                } else {
                    m_engine->sendCommand(audio::StopClipMsg{t});
                    m_engine->sendCommand(audio::StopMidiClipMsg{t});
                    m_project->track(t).defaultScene = -1;
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
                    m_project->track(ti).defaultScene = -1;
                } else if (hasClip) {
                    auto lq = slot->launchQuantize;
                    if (slot->audioClip)
                        m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(), lq, &slot->clipAutomation, slot->followAction});
                    else if (slot->midiClip)
                        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(), lq, &slot->clipAutomation, slot->followAction});
                    m_project->track(ti).defaultScene = si;
                } else if (trackArmed) {
                    if (m_project->track(ti).type == Track::Type::Midi)
                        m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, true});
                    else
                        m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, true});
                }
            } else {
                m_selectedTrack  = ti;
                m_lastClickTrack = ti;
                if (slot && !slot->empty()) {
                    // Start potential clip drag (launch deferred to mouseUp)
                    m_clipDragPending = true;
                    m_clipDragging    = false;
                    m_dragSourceTrack = ti;
                    m_dragSourceScene = si;
                    m_dragStartX      = e.x;
                    m_dragStartY      = e.y;
                    m_dragTargetTrack = -1;
                    m_dragTargetScene = -1;
                    m_clipDragCompleted = false;
                    captureMouse();  // ensure we receive mouse move events
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

bool SessionPanel::launchOrStopSlot(int ti, int si) {
    if (!m_project || !m_engine) return false;
    if (ti < 0 || ti >= m_project->numTracks()) return false;
    if (si < 0 || si >= m_project->numScenes()) return false;

    auto* slot = m_project->getSlot(ti, si);
    bool isPlaying = m_trackStates[ti].playing && m_trackStates[ti].playingScene == si;
    bool isRecording = m_trackStates[ti].recording && m_trackStates[ti].recordingScene == si;
    bool trackArmed = m_project->track(ti).armed;

    if (isRecording) {
        auto recQ = m_project->track(ti).recordQuantize;
        if (m_project->track(ti).type == Track::Type::Midi)
            m_engine->sendCommand(audio::StopMidiRecordMsg{ti, recQ});
        else
            m_engine->sendCommand(audio::StopAudioRecordMsg{ti, recQ});
    } else if (isPlaying) {
        if (slot && slot->audioClip)
            m_engine->sendCommand(audio::StopClipMsg{ti});
        else
            m_engine->sendCommand(audio::StopMidiClipMsg{ti});
        m_project->track(ti).defaultScene = -1;
    } else if (slot && slot->audioClip) {
        m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(),
            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
        m_project->track(ti).defaultScene = si;
    } else if (slot && slot->midiClip) {
        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(),
            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
        m_project->track(ti).defaultScene = si;
    } else if (trackArmed) {
        if (m_project->track(ti).type == Track::Type::Midi)
            m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, true});
        else
            m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, true});
    } else {
        return false;
    }
    return true;
}

void SessionPanel::launchSlotAt(int ti, int si) {
    if (!m_project || !m_engine) return;
    if (ti < 0 || ti >= m_project->numTracks()) return;
    if (si < 0 || si >= m_project->numScenes()) return;
    auto* slot = m_project->getSlot(ti, si);
    if (slot && slot->audioClip) {
        m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(),
            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
        m_project->track(ti).defaultScene = si;
    } else if (slot && slot->midiClip) {
        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(),
            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
        m_project->track(ti).defaultScene = si;
    }
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

    for (int t = 0; t < m_project->numTracks(); ++t) {
        float tx = x + t * Theme::kTrackWidth - m_scrollX;
        float tw = Theme::kTrackWidth;
        if (tx + tw < x || tx > x + w) continue;

        if (t == m_selectedTrack)
            r.drawRect(tx + 1, y + 1, tw - 2, h - 2, Color{50, 55, 65, 255});

        Color col = Theme::trackColors[m_project->track(t).colorIndex % Theme::kNumTrackColors];
        r.drawRect(tx + 2, y + 2, tw - 4, 3, col);

        if (f.isLoaded()) {
            if (t == m_renameTrack) {
                // Inline rename text box — wraps around text at (tx+18, y+8)
                float textX = tx + 18, textY = y + 8;
                float boxX = tx + 14, boxY = y + 6;
                float boxW = tw - 18, boxH = 24;
                r.drawRect(boxX, boxY, boxW, boxH, Color{20, 22, 28, 255});
                r.drawRectOutline(boxX, boxY, boxW, boxH, Theme::transportAccent);
                float cx = textX;
                if (m_renameCursor == 0)
                    r.drawRect(cx, boxY + 2, 1, boxH - 4, Theme::transportAccent);
                for (int i = 0; i < static_cast<int>(m_renameText.size()); ++i) {
                    auto g = f.getGlyph(m_renameText[i], cx, textY, scale);
                    if (cx + g.xAdvance > boxX + boxW - 4) break;
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textPrimary, f.textureId());
                    cx += g.xAdvance;
                    if (i == m_renameCursor - 1)
                        r.drawRect(cx, boxY + 2, 1, boxH - 4, Theme::transportAccent);
                }
            } else {
                float textX = tx + 18, textY = y + 8;
                for (char c : m_project->track(t).name) {
                    auto g = f.getGlyph(c, textX, textY, scale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textPrimary, f.textureId());
                    textX += g.xAdvance;
                    if (textX > tx + tw - 42) break;
                }
            }
        }

        // Track type icon(left side) — waveform for Audio, MIDI port for MIDI
        {
            bool isMidi = (m_project->track(t).type == Track::Type::Midi);
            Color iconCol = isMidi ? Color{180,130,255,200} : Color{130,200,130,200};
            float iconSize = 10.0f;
            float ix = tx + 6;
            float iy = y + 7;  // above the track name
            if (isMidi) {
                // MIDI DIN connector: circle with 3 dots inside
                float cr = iconSize * 0.5f;
                float ccx = ix + cr, ccy = iy + cr;
                r.drawFilledCircle(ccx, ccy, cr, iconCol, 16);
                Color dot{30, 30, 35, 255};
                float dotR = 1.0f;
                r.drawFilledCircle(ccx - 2.5f, ccy, dotR, dot, 8);
                r.drawFilledCircle(ccx,        ccy, dotR, dot, 8);
                r.drawFilledCircle(ccx + 2.5f, ccy, dotR, dot, 8);
            } else {
                // Audio waveform icon: 5 vertical bars of varying height
                float barW = 1.5f, gap = 1.0f;
                float heights[] = {3, 7, 10, 6, 4};
                for (int b = 0; b < 5; ++b) {
                    float bh = heights[b];
                    r.drawRect(ix + b * (barW + gap), iy + (iconSize - bh) * 0.5f,
                               barW, bh, iconCol);
                }
            }
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

    // Controller grid region overlay
    if (m_showGridRegion) {
        float rx = x + m_gridOriginTrack * Theme::kTrackWidth - m_scrollX;
        float ry = y + m_gridOriginScene * Theme::kClipSlotHeight - m_scrollY;
        float rw = m_gridCols * Theme::kTrackWidth;
        float rh = m_gridRows * Theme::kClipSlotHeight;
        r.drawRect(rx, ry, rw, rh, Color{255, 80, 80, 25});
        r.drawRectOutline(rx, ry, rw, rh, Color{255, 80, 80, 160}, 2.0f);
    }

    // Clip drag visual feedback
    if (m_clipDragging && m_dragSourceTrack >= 0) {
        // Dim the source slot
        float srcX = x + m_dragSourceTrack * Theme::kTrackWidth - m_scrollX;
        float srcY = y + m_dragSourceScene * Theme::kClipSlotHeight - m_scrollY;
        r.drawRect(srcX, srcY, Theme::kTrackWidth, Theme::kClipSlotHeight,
                   Color{0, 0, 0, m_clipDragIsCopy ? (uint8_t)60 : (uint8_t)120});

        // Highlight the target slot
        if (m_dragTargetTrack >= 0 && m_dragTargetScene >= 0 &&
            !(m_dragTargetTrack == m_dragSourceTrack &&
              m_dragTargetScene == m_dragSourceScene)) {
            float dstX = x + m_dragTargetTrack * Theme::kTrackWidth - m_scrollX;
            float dstY = y + m_dragTargetScene * Theme::kClipSlotHeight - m_scrollY;
            Color highlight = m_clipDragIsCopy ? Color{80, 180, 255, 60} : Color{80, 255, 80, 60};
            r.drawRect(dstX, dstY, Theme::kTrackWidth, Theme::kClipSlotHeight, highlight);
            Color border = m_clipDragIsCopy ? Color{80, 180, 255, 200} : Color{80, 255, 80, 200};
            r.drawRectOutline(dstX, dstY, Theme::kTrackWidth, Theme::kClipSlotHeight, border, 2.0f);

            // "+" indicator for copy mode
            if (m_clipDragIsCopy) {
                float cx = dstX + Theme::kTrackWidth - 14.0f;
                float cy = dstY + 4.0f;
                r.drawRect(cx, cy, 10, 10, Color{80, 180, 255, 220});
                r.drawRect(cx + 4, cy + 2, 2, 6, Color{255, 255, 255, 255});
                r.drawRect(cx + 2, cy + 4, 6, 2, Color{255, 255, 255, 255});
            }
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
            int nch = aClip->buffer->numChannels();
            if (nch >= 2) {
                r.drawWaveformStereo(aClip->buffer->channelData(0),
                                     aClip->buffer->channelData(1),
                                     aClip->buffer->numFrames(),
                                     contentX + 4, wfY, contentW - 8, wfH, wfCol);
            } else {
                r.drawWaveform(aClip->buffer->channelData(0),
                               aClip->buffer->numFrames(),
                               contentX + 4, wfY, contentW - 8, wfH, wfCol);
            }
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

    // Selection highlight (shown when not playing/recording to avoid visual clash)
    bool isSelected = (ti == m_selectedTrack && si == m_selectedScene);
    if (isSelected && !isPlaying && !isRecording) {
        r.drawRectOutline(ix, iy, iw, ih, Color{255, 255, 255, 200}, 2.0f);
    } else if (isSelected) {
        // Subtle inner highlight when playing/recording already draws an outline
        r.drawRectOutline(ix + 2, iy + 2, iw - 4, ih - 4, Color{255, 255, 255, 80}, 1.0f);
    }

    r.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
    r.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);
}

} // namespace fw
} // namespace ui
} // namespace yawn
