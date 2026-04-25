// SessionPanel.cpp — UI v2 rendering + event implementations.
// Split from SessionPanel.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "SessionPanel.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/Theme.h"

#include "stb_image.h"
#include <glad/gl.h>

namespace yawn {
namespace ui {
namespace fw2 {

void SessionPanel::render(UIContext& ctx) {
    if (!m_project || !m_engine) return;
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w,  h = m_bounds.h;

    r.drawRect(x, y, w, h, ::yawn::ui::Theme::background);

    float headerY = y;
    float gridY   = headerY + ::yawn::ui::Theme::kTrackHeaderHeight;
    float gridH   = h - ::yawn::ui::Theme::kTrackHeaderHeight - kScrollbarH;
    float gridX   = x + ::yawn::ui::Theme::kSceneLabelWidth;
    float gridW   = w - ::yawn::ui::Theme::kSceneLabelWidth;

    paintTrackHeaders(r, tm, gridX, headerY, gridW);
    paintSceneLabels(r, tm, x, gridY, gridH);
    paintClipGrid(r, tm, gridX, gridY, gridW, gridH);
    paintHScrollbar(r, gridX, gridY + gridH, gridW);
}

bool SessionPanel::onMouseDownWithClicks(MouseEvent& e, int clickCount) {
    if (!m_project || !m_engine) return false;

    float mx = e.x, my = e.y;
    bool rightClick = (e.button == MouseButton::Right);
    const bool shift = (e.modifiers & ::yawn::ui::fw2::ModifierKey::Shift) != 0;
    float x = m_bounds.x, y = m_bounds.y;
    float gridX   = x + ::yawn::ui::Theme::kSceneLabelWidth;
    float headerY = y;
    float gridY   = headerY + ::yawn::ui::Theme::kTrackHeaderHeight;

    // Horizontal scrollbar
    float gridW = m_bounds.w - ::yawn::ui::Theme::kSceneLabelWidth;
    float sbY = gridY + (m_bounds.h - ::yawn::ui::Theme::kTrackHeaderHeight - kScrollbarH);
    if (my >= sbY && my < sbY + kScrollbarH && mx >= gridX && mx < gridX + gridW) {
        float contentW = m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth;
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

    // Track header click — select track (or double-click to rename).
    if (my >= headerY && my < gridY && mx >= gridX) {
        float cmx = mx + m_scrollX;
        int ti = static_cast<int>((cmx - gridX) / ::yawn::ui::Theme::kTrackWidth);
        if (ti >= 0 && ti < m_project->numTracks()) {
            m_selectedTrack = ti;
            m_lastClickTrack = ti;
            if (clickCount >= 2) {
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
        int si = static_cast<int>((cmy - gridY) / ::yawn::ui::Theme::kClipSlotHeight);
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
                } else if (slot && slot->visualClip) {
                    if (m_onLaunchVisualClip)
                        m_onLaunchVisualClip(t, si, slot->visualClip->firstShaderPath());
                    m_project->track(t).defaultScene = si;
                    // Reflect play state so the grid shows a stop
                    // square (not a play triangle) on the active slot.
                    m_trackStates[t].playing      = true;
                    m_trackStates[t].playingScene = si;
                } else if (m_project->track(t).type != Track::Type::Visual) {
                    m_engine->sendCommand(audio::StopClipMsg{t});
                    m_engine->sendCommand(audio::StopMidiClipMsg{t});
                    m_project->track(t).defaultScene = -1;
                } else {
                    // Visual track with empty slot on this scene →
                    // clear the layer so the running visual goes dark.
                    if (m_onStopVisualClip) m_onStopVisualClip(t);
                    m_trackStates[t].playing      = false;
                    m_trackStates[t].playingScene = -1;
                    m_project->track(t).defaultScene = -1;
                }
            }
            m_activeScene = si;
            return true;
        }
    }

    // Clip grid click
    if (mx >= gridX) {
        int ti = static_cast<int>((cmx - gridX) / ::yawn::ui::Theme::kTrackWidth);
        int si = static_cast<int>((cmy - gridY) / ::yawn::ui::Theme::kClipSlotHeight);
        if (ti >= 0 && ti < m_project->numTracks() &&
            si >= 0 && si < m_project->numScenes()) {
            m_lastClickScene = si;
            m_selectedScene = si;
            auto* slot = m_project->getSlot(ti, si);
            bool hasClip = slot && !slot->empty();
            bool isPlaying = m_trackStates[ti].playing && m_trackStates[ti].playingScene == si;
            bool trackArmed = m_project->track(ti).armed;

            // Compute slot-local position
            float slotLocalX = cmx - gridX - ti * ::yawn::ui::Theme::kTrackWidth;

            bool isSlotRecording = m_trackStates[ti].recording
                               && m_trackStates[ti].recordingScene == si;

            const auto trackType = m_project->track(ti).type;

            // Record-setting pills — empty slots only. Length pill
            // cycles ∞ / 1 / 2 / 4 / 8 / 16; loop pill toggles. Both
            // hit-tested before the record-trigger click so clicking a
            // pill doesn't also start recording.
            if (!hasClip && slot && !rightClick && !isSlotRecording) {
                const float pad = ::yawn::ui::Theme::kSlotPadding;
                const float cellIX = cmx - gridX - ti * ::yawn::ui::Theme::kTrackWidth;
                const float cellIY = cmy - gridY - si * ::yawn::ui::Theme::kClipSlotHeight;
                const float iwLocal = ::yawn::ui::Theme::kTrackWidth - pad * 2;
                const float ihLocal = ::yawn::ui::Theme::kClipSlotHeight - pad * 2;
                const float pillH = 12.0f, lenW = 24.0f, loopW = 16.0f, gap = 3.0f;
                const float py  = pad + ihLocal - pillH - 4.0f;
                const float lenPx  = pad + iwLocal - lenW - 4.0f;
                const float loopPx = lenPx - loopW - gap;

                if (cellIY >= py && cellIY < py + pillH) {
                    if (cellIX >= lenPx && cellIX < lenPx + lenW) {
                        static constexpr int kCycle[] = {0, 1, 2, 4, 8, 16};
                        const int cur = slot->recordLengthBars;
                        int idx = 0;
                        for (int k = 0; k < 6; ++k) if (kCycle[k] == cur) { idx = k; break; }
                        const int next = kCycle[(idx + 1) % 6];
                        if (m_undoManager) {
                            m_undoManager->push({"Set Record Length",
                                [this, ti, si, cur]{
                                    auto* s = m_project->getSlot(ti, si);
                                    if (s) s->recordLengthBars = cur;
                                },
                                [this, ti, si, next]{
                                    auto* s = m_project->getSlot(ti, si);
                                    if (s) s->recordLengthBars = next;
                                }, ""});
                        }
                        slot->recordLengthBars = next;
                        return true;
                    }
                    if (cellIX >= loopPx && cellIX < loopPx + loopW) {
                        const bool was = slot->recordLoop;
                        if (m_undoManager) {
                            m_undoManager->push({"Toggle Record Loop",
                                [this, ti, si, was]{
                                    auto* s = m_project->getSlot(ti, si);
                                    if (s) s->recordLoop = was;
                                },
                                [this, ti, si, was]{
                                    auto* s = m_project->getSlot(ti, si);
                                    if (s) s->recordLoop = !was;
                                }, ""});
                        }
                        slot->recordLoop = !was;
                        return true;
                    }
                }
            }

            if (isSlotRecording) {
                // Any click on a recording slot stops recording (takes priority over context menu)
                auto recQ = m_project->track(ti).recordQuantize;
                if (trackType == Track::Type::Midi)
                    m_engine->sendCommand(audio::StopMidiRecordMsg{ti, recQ});
                else if (trackType == Track::Type::Audio)
                    m_engine->sendCommand(audio::StopAudioRecordMsg{ti, recQ});
            } else if (rightClick) {
                m_lastRightClickTrack = ti;
                m_lastRightClickScene = si;
                m_selectedTrack  = ti;
                m_lastClickTrack = ti;
            } else if (slotLocalX < kIconZoneW + ::yawn::ui::Theme::kSlotPadding) {
                if (isPlaying) {
                    if (slot->audioClip)
                        m_engine->sendCommand(audio::StopClipMsg{ti});
                    else if (slot->midiClip)
                        m_engine->sendCommand(audio::StopMidiClipMsg{ti});
                    // Visual clips have no "stop" — shader stays loaded.
                    m_project->track(ti).defaultScene = -1;
                } else if (hasClip) {
                    auto lq = slot->launchQuantize;
                    if (slot->audioClip)
                        m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(), lq, &slot->clipAutomation, slot->followAction});
                    else if (slot->midiClip)
                        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(), lq, &slot->clipAutomation, slot->followAction});
                    else if (slot->visualClip && m_onLaunchVisualClip)
                        m_onLaunchVisualClip(ti, si, slot->visualClip->firstShaderPath());
                    m_project->track(ti).defaultScene = si;
                } else if (trackArmed) {
                    int rlb = slot ? slot->recordLengthBars : 0;
                    if (trackType == Track::Type::Midi)
                        m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, true, rlb});
                    else if (trackType == Track::Type::Audio)
                        m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, true, rlb});
                    // Visual tracks can't record.
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
                    int rlb = slot ? slot->recordLengthBars : 0;
                    if (trackType == Track::Type::Midi)
                        m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, !shift, rlb});
                    else if (trackType == Track::Type::Audio)
                        m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, !shift, rlb});
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
    const auto trackType = m_project->track(ti).type;

    if (isRecording) {
        auto recQ = m_project->track(ti).recordQuantize;
        if (trackType == Track::Type::Midi)
            m_engine->sendCommand(audio::StopMidiRecordMsg{ti, recQ});
        else if (trackType == Track::Type::Audio)
            m_engine->sendCommand(audio::StopAudioRecordMsg{ti, recQ});
    } else if (isPlaying) {
        if (slot && slot->audioClip)
            m_engine->sendCommand(audio::StopClipMsg{ti});
        else if (slot && slot->midiClip)
            m_engine->sendCommand(audio::StopMidiClipMsg{ti});
        else if (slot && slot->visualClip) {
            // Visual stop path — App clears the engine layer + resets
            // its launch-state trackers via the callback.
            if (m_onStopVisualClip) m_onStopVisualClip(ti);
            m_trackStates[ti].playing      = false;
            m_trackStates[ti].playingScene = -1;
        }
        m_project->track(ti).defaultScene = -1;
    } else if (slot && slot->audioClip) {
        m_engine->sendCommand(audio::LaunchClipMsg{ti, si, slot->audioClip.get(),
            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
        m_project->track(ti).defaultScene = si;
    } else if (slot && slot->midiClip) {
        m_engine->sendCommand(audio::LaunchMidiClipMsg{ti, si, slot->midiClip.get(),
            slot->launchQuantize, &slot->clipAutomation, slot->followAction});
        m_project->track(ti).defaultScene = si;
    } else if (slot && slot->visualClip) {
        if (m_onLaunchVisualClip)
            m_onLaunchVisualClip(ti, si, slot->visualClip->firstShaderPath());
        m_project->track(ti).defaultScene = si;
        // Reflect play state so the grid shows a stop square on this
        // slot. Click again to stop via the isPlaying branch above.
        m_trackStates[ti].playing      = true;
        m_trackStates[ti].playingScene = si;
    } else if (trackArmed) {
        if (trackType == Track::Type::Midi)
            m_engine->sendCommand(audio::StartMidiRecordMsg{ti, si, true});
        else if (trackType == Track::Type::Audio)
            m_engine->sendCommand(audio::StartAudioRecordMsg{ti, si, true});
        // Visual tracks can't record.
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
    } else if (slot && slot->visualClip) {
        if (m_onLaunchVisualClip)
            m_onLaunchVisualClip(ti, si, slot->visualClip->firstShaderPath());
        m_project->track(ti).defaultScene = si;
        m_trackStates[ti].playing      = true;
        m_trackStates[ti].playingScene = si;
    }
}

void SessionPanel::launchScene(int scene) {
    if (!m_project || !m_engine) return;
    if (scene < 0 || scene >= m_project->numScenes()) return;
    for (int t = 0; t < m_project->numTracks(); ++t) {
        auto* slot = m_project->getSlot(t, scene);
        if (slot && slot->audioClip) {
            m_engine->sendCommand(audio::LaunchClipMsg{t, scene, slot->audioClip.get(),
                slot->launchQuantize, &slot->clipAutomation, slot->followAction});
            m_project->track(t).defaultScene = scene;
        } else if (slot && slot->midiClip) {
            m_engine->sendCommand(audio::LaunchMidiClipMsg{t, scene, slot->midiClip.get(),
                slot->launchQuantize, &slot->clipAutomation, slot->followAction});
            m_project->track(t).defaultScene = scene;
        } else if (slot && slot->visualClip) {
            if (m_onLaunchVisualClip)
                m_onLaunchVisualClip(t, scene, slot->visualClip->firstShaderPath());
            m_project->track(t).defaultScene = scene;
            m_trackStates[t].playing      = true;
            m_trackStates[t].playingScene = scene;
        } else if (m_project->track(t).type != Track::Type::Visual) {
            // Audio/MIDI tracks with empty slot → stop whatever's playing.
            // Visual tracks have no "stop" — leave the last shader loaded.
            m_engine->sendCommand(audio::StopClipMsg{t});
            m_engine->sendCommand(audio::StopMidiClipMsg{t});
            m_project->track(t).defaultScene = -1;
        }
    }
    m_activeScene = scene;
}

void SessionPanel::paintTrackHeaders(Renderer2D& r, TextMetrics& tm, float x, float y, float w) {
    if (!m_project) return;
    float h = ::yawn::ui::Theme::kTrackHeaderHeight;
    r.drawRect(x, y, w, h, ::yawn::ui::Theme::trackHeaderBg);
    r.drawRect(x, y + h - 1, w, 1, ::yawn::ui::Theme::clipSlotBorder);

    r.pushClip(x, y, w, h);
    const float labelSize = theme().metrics.fontSizeSmall;

    for (int t = 0; t < m_project->numTracks(); ++t) {
        float tx = x + t * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
        float tw = ::yawn::ui::Theme::kTrackWidth;
        if (tx + tw < x || tx > x + w) continue;

        if (t == m_selectedTrack)
            r.drawRect(tx + 1, y + 1, tw - 2, h - 2, ::yawn::ui::Color{50, 55, 65, 255});

        ::yawn::ui::Color col = ::yawn::ui::Theme::trackColors[
            m_project->track(t).colorIndex % ::yawn::ui::Theme::kNumTrackColors];
        r.drawRect(tx + 2, y + 2, tw - 4, 3, col);

        if (t == m_renameTrack) {
            // Inline rename text box — wraps around text at (tx+18, y+8)
            float textX = tx + 18, textY = y + 8;
            float boxX = tx + 14, boxY = y + 6;
            float boxW = tw - 18, boxH = 24;
            r.drawRect(boxX, boxY, boxW, boxH, ::yawn::ui::Color{20, 22, 28, 255});
            r.drawRectOutline(boxX, boxY, boxW, boxH, ::yawn::ui::Theme::transportAccent);

            // Compute the substring that fits in the box and cursor x.
            const std::string& text = m_renameText;
            const int bytes = static_cast<int>(text.size());
            int fitEnd = bytes;
            const float maxW = boxW - 4 - (textX - boxX);
            while (fitEnd > 0 &&
                   tm.textWidth(text.substr(0, fitEnd), labelSize) > maxW) {
                // Step back one UTF-8 code point.
                int step = 1;
                while (step < fitEnd &&
                       (static_cast<unsigned char>(text[fitEnd - step]) & 0xC0) == 0x80)
                    ++step;
                fitEnd -= step;
            }
            std::string visible = text.substr(0, fitEnd);
            if (!visible.empty()) {
                tm.drawText(r, visible, textX, textY, labelSize,
                             ::yawn::ui::Theme::textPrimary);
            }

            int cursorClamped = std::min(m_renameCursor, fitEnd);
            float cursorX = textX +
                tm.textWidth(text.substr(0, cursorClamped), labelSize);
            r.drawRect(cursorX, boxY + 2, 1, boxH - 4,
                        ::yawn::ui::Theme::transportAccent);
        } else {
            float textX = tx + 18, textY = y + 8;
            // Clip the name to the visible width so it doesn't spill over
            // the track-type icon on the next strip.
            const std::string& name = m_project->track(t).name;
            const float maxW = (tx + tw - 42) - textX;
            int fitEnd = static_cast<int>(name.size());
            while (fitEnd > 0 &&
                   tm.textWidth(name.substr(0, fitEnd), labelSize) > maxW) {
                int step = 1;
                while (step < fitEnd &&
                       (static_cast<unsigned char>(name[fitEnd - step]) & 0xC0) == 0x80)
                    ++step;
                fitEnd -= step;
            }
            if (fitEnd > 0) {
                tm.drawText(r, name.substr(0, fitEnd), textX, textY, labelSize,
                             ::yawn::ui::Theme::textPrimary);
            }
        }

        // Track type icon (left side) — waveform / MIDI port / visual frame.
        {
            const auto trackType = m_project->track(t).type;
            float iconSize = 10.0f;
            float ix = tx + 6;
            float iy = y + 7;  // above the track name
            if (trackType == Track::Type::Midi) {
                ::yawn::ui::Color iconCol{180,130,255,200};
                // MIDI DIN connector: circle with 3 dots inside
                float cr = iconSize * 0.5f;
                float ccx = ix + cr, ccy = iy + cr;
                r.drawFilledCircle(ccx, ccy, cr, iconCol, 16);
                ::yawn::ui::Color dot{30, 30, 35, 255};
                float dotR = 1.0f;
                r.drawFilledCircle(ccx - 2.5f, ccy, dotR, dot, 8);
                r.drawFilledCircle(ccx,        ccy, dotR, dot, 8);
                r.drawFilledCircle(ccx + 2.5f, ccy, dotR, dot, 8);
            } else if (trackType == Track::Type::Visual) {
                // Little monitor/frame glyph for visual tracks.
                ::yawn::ui::Color iconCol{110,200,230,220};
                r.drawRect(ix,              iy,              iconSize, 1.5f, iconCol);
                r.drawRect(ix,              iy + iconSize-1.5f, iconSize, 1.5f, iconCol);
                r.drawRect(ix,              iy,              1.5f, iconSize, iconCol);
                r.drawRect(ix + iconSize-1.5f, iy,           1.5f, iconSize, iconCol);
                r.drawFilledCircle(ix + iconSize*0.5f, iy + iconSize*0.5f,
                                    1.8f, iconCol, 10);
            } else {
                ::yawn::ui::Color iconCol{130,200,130,200};
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

        r.drawRect(tx + tw - 1, y, 1, h, ::yawn::ui::Theme::clipSlotBorder);
    }
    r.popClip();
}

void SessionPanel::paintSceneLabels(Renderer2D& r, TextMetrics& tm, float x, float y, float h) {
    if (!m_project) return;
    float w = ::yawn::ui::Theme::kSceneLabelWidth;
    r.drawRect(x, y, w, h, ::yawn::ui::Theme::sceneLabelBg);

    r.pushClip(x, y, w, h);
    const float labelSize = theme().metrics.fontSizeSmall;
    const float lh = tm.lineHeight(labelSize);

    for (int s = 0; s < m_project->numScenes(); ++s) {
        float sy = y + s * ::yawn::ui::Theme::kClipSlotHeight - m_scrollY;
        float sh = ::yawn::ui::Theme::kClipSlotHeight;
        if (sy + sh < y || sy > y + h) continue;

        r.drawRect(x + 2, sy + 2, w - 4, sh - 4, ::yawn::ui::Theme::clipSlotEmpty);

        const std::string& name = m_project->scene(s).name;
        if (!name.empty()) {
            float textX = x + 8;
            float textY = sy + sh * 0.5f - lh * 0.5f;
            tm.drawText(r, name, textX, textY, labelSize,
                         ::yawn::ui::Theme::textSecondary);
        }
        r.drawRect(x, sy + sh - 1, w, 1, ::yawn::ui::Theme::clipSlotBorder);
    }
    r.popClip();
}

void SessionPanel::paintClipGrid(Renderer2D& r, TextMetrics& tm, float x, float y, float w, float h) {
    if (!m_project) return;
    r.pushClip(x, y, w, h);
    for (int t = 0; t < m_project->numTracks(); ++t) {
        for (int s = 0; s < m_project->numScenes(); ++s) {
            float sx = x + t * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
            float sy = y + s * ::yawn::ui::Theme::kClipSlotHeight - m_scrollY;
            float sw = ::yawn::ui::Theme::kTrackWidth;
            float sh = ::yawn::ui::Theme::kClipSlotHeight;
            if (sx + sw < x || sx > x + w) continue;
            if (sy + sh < y || sy > y + h) continue;
            paintClipSlot(r, tm, t, s, sx, sy, sw, sh);
        }
    }

    // Controller grid region overlay
    if (m_showGridRegion) {
        float rx = x + m_gridOriginTrack * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
        float ry = y + m_gridOriginScene * ::yawn::ui::Theme::kClipSlotHeight - m_scrollY;
        float rw = m_gridCols * ::yawn::ui::Theme::kTrackWidth;
        float rh = m_gridRows * ::yawn::ui::Theme::kClipSlotHeight;
        r.drawRect(rx, ry, rw, rh, ::yawn::ui::Color{255, 80, 80, 25});
        r.drawRectOutline(rx, ry, rw, rh, ::yawn::ui::Color{255, 80, 80, 160}, 2.0f);
    }

    // Clip drag visual feedback
    if (m_clipDragging && m_dragSourceTrack >= 0) {
        // Dim the source slot
        float srcX = x + m_dragSourceTrack * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
        float srcY = y + m_dragSourceScene * ::yawn::ui::Theme::kClipSlotHeight - m_scrollY;
        r.drawRect(srcX, srcY, ::yawn::ui::Theme::kTrackWidth, ::yawn::ui::Theme::kClipSlotHeight,
                   ::yawn::ui::Color{0, 0, 0, m_clipDragIsCopy ? (uint8_t)60 : (uint8_t)120});

        // Highlight the target slot
        if (m_dragTargetTrack >= 0 && m_dragTargetScene >= 0 &&
            !(m_dragTargetTrack == m_dragSourceTrack &&
              m_dragTargetScene == m_dragSourceScene)) {
            float dstX = x + m_dragTargetTrack * ::yawn::ui::Theme::kTrackWidth - m_scrollX;
            float dstY = y + m_dragTargetScene * ::yawn::ui::Theme::kClipSlotHeight - m_scrollY;
            ::yawn::ui::Color highlight = m_clipDragIsCopy
                ? ::yawn::ui::Color{80, 180, 255, 60}
                : ::yawn::ui::Color{80, 255, 80, 60};
            r.drawRect(dstX, dstY, ::yawn::ui::Theme::kTrackWidth, ::yawn::ui::Theme::kClipSlotHeight, highlight);
            ::yawn::ui::Color border = m_clipDragIsCopy
                ? ::yawn::ui::Color{80, 180, 255, 200}
                : ::yawn::ui::Color{80, 255, 80, 200};
            r.drawRectOutline(dstX, dstY, ::yawn::ui::Theme::kTrackWidth, ::yawn::ui::Theme::kClipSlotHeight, border, 2.0f);

            // "+" indicator for copy mode
            if (m_clipDragIsCopy) {
                float cx = dstX + ::yawn::ui::Theme::kTrackWidth - 14.0f;
                float cy = dstY + 4.0f;
                r.drawRect(cx, cy, 10, 10, ::yawn::ui::Color{80, 180, 255, 220});
                r.drawRect(cx + 4, cy + 2, 2, 6, ::yawn::ui::Color{255, 255, 255, 255});
                r.drawRect(cx + 2, cy + 4, 6, 2, ::yawn::ui::Color{255, 255, 255, 255});
            }
        }
    }

    r.popClip();
    (void)tm;
}

void SessionPanel::paintHScrollbar(Renderer2D& r, float x, float y, float w) {
    r.drawRect(m_bounds.x, y, m_bounds.w, kScrollbarH, ::yawn::ui::Color{40, 40, 45});
    if (!m_project) return;
    float contentW = m_project->numTracks() * ::yawn::ui::Theme::kTrackWidth;
    if (contentW <= w) return;
    float thumbW = std::max(20.0f, w * (w / std::max(1.0f, contentW)));
    float maxScroll = contentW - w;
    float scrollFrac = m_scrollX / std::max(1.0f, maxScroll);
    float thumbX = x + scrollFrac * (w - thumbW);
    ::yawn::ui::Color thumbCol = (m_hsbDragging || m_hsbHovered)
        ? ::yawn::ui::Color{120, 120, 130}
        : ::yawn::ui::Color{90, 90, 100};
    r.drawRect(thumbX, y, thumbW, kScrollbarH, thumbCol);
}

unsigned SessionPanel::getThumbnailTexture(const std::string& path) const {
    if (path.empty()) return 0;
    auto it = m_thumbnailCache.find(path);
    if (it != m_thumbnailCache.end()) return it->second;

    int w = 0, h = 0, n = 0;
    unsigned char* pix = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!pix) {
        // Cache the negative result (0) so we don't retry every frame.
        m_thumbnailCache.emplace(path, 0);
        return 0;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pix);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pix);

    m_thumbnailCache.emplace(path, tex);
    return tex;
}

void SessionPanel::paintClipSlot(Renderer2D& r, TextMetrics& tm, int ti, int si,
                   float x, float y, float w, float h) {
    float pad = ::yawn::ui::Theme::kSlotPadding;
    float ix = x + pad, iy = y + pad;
    float iw = w - pad * 2, ih = h - pad * 2;

    const auto* slot = m_project->getSlot(ti, si);
    bool hasClip = slot && !slot->empty();
    const audio::Clip* aClip = slot ? slot->audioClip.get() : nullptr;
    const midi::MidiClip* mClip = slot ? slot->midiClip.get() : nullptr;
    const visual::VisualClip* vClip = slot ? slot->visualClip.get() : nullptr;
    bool isPlaying = m_trackStates[ti].playing && m_trackStates[ti].playingScene == si;
    bool trackArmed = m_project->track(ti).armed;
    bool recReady = !hasClip && trackArmed;
    bool recFullyArmed = recReady && m_globalRecordArmed;
    bool isRecording = m_trackStates[ti].recording
                    && m_trackStates[ti].recordingScene == si;
    bool isHovered = (m_hoveredTrack == ti && m_hoveredScene == si);
    bool iconHovered = isHovered && m_hoveredIcon;

    ::yawn::ui::Color bgCol = hasClip
        ? ::yawn::ui::Theme::panelBg
        : ::yawn::ui::Theme::clipSlotEmpty;
    r.drawRect(ix, iy, iw, ih, bgCol);

    // Video thumbnail behind the clip content, dimmed so the clip name
    // and play icon stay readable.
    if (vClip && !vClip->thumbnailPath.empty()) {
        GLuint tex = getThumbnailTexture(vClip->thumbnailPath);
        if (tex) {
            float contentX = ix + kIconZoneW;
            float contentW = iw - kIconZoneW;
            r.drawTexturedQuad(contentX, iy, contentW, ih,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                ::yawn::ui::Color{255, 255, 255, 160}, tex);
        }
    }

    const float smallSize = theme().metrics.fontSizeSmall;

    // "Importing…" overlay — shown while a background video transcode is
    // in progress for this slot.
    if (isSlotImporting(ti, si)) {
        r.drawRect(ix, iy, iw, ih, ::yawn::ui::Color{30, 50, 80, 220});
        float pct   = std::clamp(slotImportProgress(ti, si), 0.0f, 1.0f);
        char label[48];
        if (pct > 0.0f) std::snprintf(label, sizeof(label), "importing… %d%%",
                                        static_cast<int>(pct * 100.0f));
        else            std::snprintf(label, sizeof(label), "importing…");
        tm.drawText(r, label, ix + 6, iy + ih * 0.5f, smallSize,
                     ::yawn::ui::Color{200, 220, 255, 255});
        // Thin progress bar hugging the bottom of the slot.
        float barH = 3.0f;
        r.drawRect(ix, iy + ih - barH, iw, barH, ::yawn::ui::Color{20, 30, 50, 255});
        r.drawRect(ix, iy + ih - barH, iw * pct, barH, ::yawn::ui::Color{120, 200, 255, 255});
        return;
    }

    // Icon zone (left side)
    float iconCX = ix + kIconZoneW * 0.5f;
    float iconCY = iy + ih * 0.5f;
    ::yawn::ui::Color iconBg = iconHovered
        ? ::yawn::ui::Color{45, 45, 50, 255}
        : ::yawn::ui::Color{30, 30, 33, 255};
    r.drawRect(ix, iy, kIconZoneW, ih, iconBg);

    if (isRecording) {
        // Pulsing record circle
        float pulse = (std::sin(m_animTimer * 4.0f) + 1.0f) * 0.5f;
        uint8_t a = static_cast<uint8_t>(150 + static_cast<int>(pulse * 105));
        ::yawn::ui::Color recCol = ::yawn::ui::Color{220, 40, 40, a};
        r.drawFilledCircle(iconCX, iconCY, 5.0f, recCol, 16);
    } else if (isPlaying) {
        // Stop square (green)
        ::yawn::ui::Color stopCol = iconHovered
            ? ::yawn::ui::Color{100, 255, 100}
            : ::yawn::ui::Theme::playing;
        float half = 4.5f;
        r.drawRect(iconCX - half, iconCY - half, half * 2, half * 2, stopCol);
    } else if (hasClip) {
        // Play triangle
        ::yawn::ui::Color trkCol = ::yawn::ui::Theme::trackColors[
            m_project->track(ti).colorIndex % ::yawn::ui::Theme::kNumTrackColors];
        ::yawn::ui::Color triCol = iconHovered ? trkCol : trkCol.withAlpha(180);
        r.drawTriangle(iconCX - 3.0f, iconCY - 5.5f,
                        iconCX - 3.0f, iconCY + 5.5f,
                        iconCX + 5.0f, iconCY, triCol);
    } else if (recReady) {
        // Record-ready circle
        ::yawn::ui::Color recCol = recFullyArmed
            ? (iconHovered ? ::yawn::ui::Color{230, 50, 50} : ::yawn::ui::Color{200, 40, 40})
            : (iconHovered ? ::yawn::ui::Color{170, 60, 60} : ::yawn::ui::Color{140, 50, 50});
        r.drawFilledCircle(iconCX, iconCY, 4.5f, recCol, 16);
    }

    // Clip content (right of icon zone)
    float contentX = ix + kIconZoneW;
    float contentW = iw - kIconZoneW;

    if (hasClip) {
        ::yawn::ui::Color trkCol = ::yawn::ui::Theme::trackColors[
            m_project->track(ti).colorIndex % ::yawn::ui::Theme::kNumTrackColors];
        r.drawRect(contentX, iy, 2, ih, trkCol);

        // Clip name
        const std::string& name = aClip ? aClip->name
            : (mClip ? mClip->name()
            : (vClip ? vClip->name : std::string()));
        if (!name.empty()) {
            // Strip leading path components (everything up to + including
            // the last '/' or '\\') so only the file/clip stem shows.
            size_t cut = name.find_last_of("/\\");
            std::string display = (cut == std::string::npos)
                ? name : name.substr(cut + 1);

            const float maxW = (ix + iw - 4) - (contentX + 5);
            int fitEnd = static_cast<int>(display.size());
            while (fitEnd > 0 &&
                   tm.textWidth(display.substr(0, fitEnd), smallSize) > maxW) {
                int step = 1;
                while (step < fitEnd &&
                       (static_cast<unsigned char>(display[fitEnd - step]) & 0xC0) == 0x80)
                    ++step;
                fitEnd -= step;
            }
            if (fitEnd > 0) {
                tm.drawText(r, display.substr(0, fitEnd),
                             contentX + 5, iy + 2, smallSize,
                             ::yawn::ui::Theme::textPrimary);
            }
        }

        // Audio waveform
        if (aClip && aClip->buffer && aClip->buffer->numFrames() > 0) {
            float wfY = iy + 18, wfH = ih - 22;
            ::yawn::ui::Color wfCol = trkCol.withAlpha(160);
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
                r.drawRect(contentX + 4 + frac * (contentW - 8), wfY, 2, wfH,
                            ::yawn::ui::Theme::playing);
            }
        }

        // MIDI notes
        if (mClip && mClip->noteCount() > 0) {
            float nY = iy + 18, nH = ih - 22;
            ::yawn::ui::Color noteCol = trkCol.withAlpha(180);
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
                r.drawRect(phX, nY, 2, nH, ::yawn::ui::Theme::playing);
            }
        }

        // Playing border pulse
        if (isPlaying) {
            float pulse = (std::sin(m_animTimer * 6.0f) + 1.0f) * 0.5f;
            ::yawn::ui::Color bc = ::yawn::ui::Theme::playing.withAlpha(
                static_cast<uint8_t>(150 + pulse * 105));
            r.drawRectOutline(ix, iy, iw, ih, bc, 2.0f);
        }
    }

    // Recording border pulse (outside hasClip block for armed empty slots too)
    if (isRecording) {
        float pulse = (std::sin(m_animTimer * 4.0f) + 1.0f) * 0.5f;
        ::yawn::ui::Color recCol = ::yawn::ui::Color{220, 40, 40}.withAlpha(
            static_cast<uint8_t>(150 + static_cast<int>(pulse * 105)));
        r.drawRectOutline(ix, iy, iw, ih, recCol, 2.0f);
    }

    // Selection highlight (shown when not playing/recording to avoid visual clash)
    bool isSelected = (ti == m_selectedTrack && si == m_selectedScene);
    if (isSelected && !isPlaying && !isRecording) {
        r.drawRectOutline(ix, iy, iw, ih, ::yawn::ui::Color{255, 255, 255, 200}, 2.0f);
    } else if (isSelected) {
        // Subtle inner highlight when playing/recording already draws an outline
        r.drawRectOutline(ix + 2, iy + 2, iw - 4, ih - 4,
                           ::yawn::ui::Color{255, 255, 255, 80}, 1.0f);
    }

    // Live-input status pip — top-right of the content area. Shown only
    // when the clip is configured as a live source; colour reflects the
    // engine's LiveVideoSource::State for the launched layer, or grey
    // when the clip isn't the currently-launched one on its track.
    if (vClip && vClip->liveInput && !vClip->liveUrl.empty()) {
        int state = -1;
        if (m_onQueryLiveState) state = m_onQueryLiveState(ti, si);
        ::yawn::ui::Color pipCol;
        switch (state) {
            case 1: {  // Connecting — pulse
                float pulse = (std::sin(m_animTimer * 6.0f) + 1.0f) * 0.5f;
                uint8_t a = static_cast<uint8_t>(140 + pulse * 115);
                pipCol = ::yawn::ui::Color{230, 190, 50, a};
                break;
            }
            case 2: pipCol = ::yawn::ui::Color{80, 220, 100, 255}; break;  // Connected
            case 3: pipCol = ::yawn::ui::Color{220, 60, 60, 255};  break;  // Failed
            case 0: // Stopped
            default: pipCol = ::yawn::ui::Color{130, 130, 130, 220}; break;
        }
        float pipCX = ix + iw - 9.0f;
        float pipCY = iy + 9.0f;
        r.drawFilledCircle(pipCX, pipCY, 5.0f, ::yawn::ui::Color{0, 0, 0, 180}, 16);
        r.drawFilledCircle(pipCX, pipCY, 4.0f, pipCol, 16);
    }

    // Record-setting pills (only on empty slots — they configure
    // future recordings into this slot). Bottom-right has the bar
    // count; immediately to its left is a "L" loop indicator.
    if (!hasClip && slot) {
        const float pillH = 12.0f;
        const float lenPillW = 24.0f;
        const float loopPillW = 16.0f;
        const float gap = 3.0f;
        const float py = iy + ih - pillH - 4.0f;

        // Length pill
        const int bars = slot->recordLengthBars;
        const float lenPx = ix + iw - lenPillW - 4.0f;
        const ::yawn::ui::Color lenBg = bars > 0
            ? ::yawn::ui::Color{200, 60, 60, 220}
            : ::yawn::ui::Color{55, 55, 60, 200};
        r.drawRoundedRect(lenPx, py, lenPillW, pillH, pillH * 0.5f, lenBg);
        char buf[4];
        if (bars == 0) std::snprintf(buf, sizeof(buf), "%s", "\xe2\x88\x9e"); // ∞
        else           std::snprintf(buf, sizeof(buf), "%d", bars);
        const float fs = theme().metrics.fontSizeSmall;
        const float lh = tm.lineHeight(fs);
        const float tw2 = tm.textWidth(buf, fs);
        tm.drawText(r, buf, lenPx + (lenPillW - tw2) * 0.5f,
                     py + (pillH - lh) * 0.5f - lh * 0.15f, fs,
                     ::yawn::ui::Color{240, 240, 245, 255});

        // Loop pill — dim "L" when off, bright green "L" when on.
        const bool loop = slot->recordLoop;
        const float loopPx = lenPx - loopPillW - gap;
        const ::yawn::ui::Color loopBg = loop
            ? ::yawn::ui::Color{60, 130, 80, 220}
            : ::yawn::ui::Color{55, 55, 60, 200};
        r.drawRoundedRect(loopPx, py, loopPillW, pillH, pillH * 0.5f, loopBg);
        const float ltw = tm.textWidth("L", fs);
        tm.drawText(r, "L", loopPx + (loopPillW - ltw) * 0.5f,
                     py + (pillH - lh) * 0.5f - lh * 0.15f, fs,
                     ::yawn::ui::Color{240, 240, 245, 255});
    }

    r.drawRect(x + w - 1, y, 1, h, ::yawn::ui::Theme::clipSlotBorder);
    r.drawRect(x, y + h - 1, w, 1, ::yawn::ui::Theme::clipSlotBorder);

    (void)isHovered;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
