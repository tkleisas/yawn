// ArrangementPanel implementation — timeline view for arrangement mode.
// Split from header for maintainability. Includes clip interaction,
// automation lane rendering, and breakpoint editing.

#include "ArrangementPanel.h"
#include "../Renderer.h"
#include "../Font.h"

namespace yawn {
namespace ui {
namespace fw {

// ── Layout helpers ─────────────────────────────────────────────────────

float ArrangementPanel::trackRowHeight(int t) const {
    float h = trackBaseHeight(t);
    if (m_expandedTracks.count(t) && m_project) {
        int nLanes = static_cast<int>(m_project->track(t).automationLanes.size());
        if (nLanes > 0) h += nLanes * kAutoLaneH;
    }
    return h;
}

float ArrangementPanel::trackYOffset(int t) const {
    float y = 0;
    for (int i = 0; i < t; ++i)
        y += trackRowHeight(i);
    return y;
}

int ArrangementPanel::trackAtY(float relY) const {
    if (!m_project) return 0;
    float y = 0;
    for (int t = 0; t < m_project->numTracks(); ++t) {
        float h = trackRowHeight(t);
        if (relY < y + h) return t;
        y += h;
    }
    return std::max(0, m_project->numTracks() - 1);
}

// Returns which auto lane sub-row the y falls into within a track
// Returns -1 if in the main clip row, or 0..n-1 for auto lane index
int ArrangementPanel::autoLaneAtY(int track, float relYInTrack) const {
    if (!m_project || !m_expandedTracks.count(track)) return -1;
    float base = trackBaseHeight(track);
    if (relYInTrack < base) return -1;
    float offset = relYInTrack - base;
    int lane = static_cast<int>(offset / kAutoLaneH);
    int nLanes = static_cast<int>(m_project->track(track).automationLanes.size());
    return (lane >= 0 && lane < nLanes) ? lane : -1;
}

std::string ArrangementPanel::autoLaneName(const automation::AutomationTarget& tgt) const {
    switch (tgt.type) {
        case automation::TargetType::Mixer: {
            auto mp = static_cast<automation::MixerParam>(tgt.paramIndex);
            switch (mp) {
                case automation::MixerParam::Volume: return "Vol";
                case automation::MixerParam::Pan:    return "Pan";
                default: {
                    int sendIdx = tgt.paramIndex - static_cast<int>(automation::MixerParam::SendLevel0);
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "Snd %d", sendIdx);
                    return buf;
                }
            }
        }
        case automation::TargetType::Instrument: {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "Inst P%d", tgt.paramIndex);
            return buf;
        }
        case automation::TargetType::AudioEffect: {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "FX%d P%d", tgt.chainIndex, tgt.paramIndex);
            return buf;
        }
        case automation::TargetType::MidiEffect: {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "MFX%d P%d", tgt.chainIndex, tgt.paramIndex);
            return buf;
        }
    }
    return "Param";
}

// ── Widget overrides ───────────────────────────────────────────────────

#ifndef YAWN_TEST_BUILD

void ArrangementPanel::paint(UIContext& ctx) {
    if (!m_project || !m_engine) return;
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w,  h = m_bounds.h;

    r.drawRect(x, y, w, h, Theme::background);

    float gridX = x + kTrackHeaderW;
    float gridW = w - kTrackHeaderW;
    float gridY = y + kRulerH;
    float gridH = h - kRulerH - kScrollbarH;

    // Auto-scroll: keep playhead visible during playback
    if (m_autoScroll && m_engine->transport().isPlaying()) {
        double beat = m_engine->transport().positionInBeats();
        float px = static_cast<float>(beat) * m_pixelsPerBeat;
        float margin = gridW * 0.1f;
        if (px - m_scrollX > gridW - margin) {
            m_scrollX = px - gridW + margin;
        } else if (px < m_scrollX) {
            m_scrollX = std::max(0.0f, px - margin);
        }
    }

    paintRuler(r, f, gridX, y, gridW);
    paintTrackHeaders(r, f, x, gridY, gridH);
    paintClipTimeline(r, f, gridX, gridY, gridW, gridH);
    paintAutoLanes(r, f, gridX, gridY, gridW, gridH);
    paintPlayhead(r, gridX, gridY, gridW, gridH + kRulerH);
    paintScrollbar(r, gridX, gridY + gridH, gridW);
}

#endif // !YAWN_TEST_BUILD

bool ArrangementPanel::onMouseDown(MouseEvent& e) {
    if (!m_project) return false;
    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w, h = m_bounds.h;
    float gridX = x + kTrackHeaderW;
    float gridW = w - kTrackHeaderW;
    float gridY = y + kRulerH;
    float gridH = h - kRulerH - kScrollbarH;

    // Ruler click → set playhead or loop range
    if (e.x >= gridX && e.x < gridX + gridW &&
        e.y >= y && e.y < gridY) {
        double beat = (e.x - gridX + m_scrollX) / m_pixelsPerBeat;
        beat = std::max(0.0, beat);

        // Shift+click sets loop start, Shift+right-click sets loop end
        if (e.mods.shift) {
            double snapped = snapBeat(beat);
            if (e.isLeftButton()) {
                m_loopStart = snapped;
                if (m_loopEnd <= m_loopStart) m_loopEnd = m_loopStart + 4.0;
            } else if (e.isRightButton()) {
                m_loopEnd = snapped;
                if (m_loopEnd <= m_loopStart) m_loopStart = std::max(0.0, m_loopEnd - 4.0);
            }
            if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd);
            return true;
        }

        // Check if clicking on loop start/end marker for dragging
        if (m_loopEnabled && m_loopEnd > m_loopStart) {
            float lx0 = gridX + static_cast<float>(m_loopStart) * m_pixelsPerBeat - m_scrollX;
            float lx1 = gridX + static_cast<float>(m_loopEnd) * m_pixelsPerBeat - m_scrollX;
            if (std::abs(e.x - lx0) < 5.0f) {
                m_loopDragMode = LoopDragMode::DragStart;
                m_loopDragOrigStart = m_loopStart;
                m_loopDragOrigEnd = m_loopEnd;
                return true;
            }
            if (std::abs(e.x - lx1) < 5.0f) {
                m_loopDragMode = LoopDragMode::DragEnd;
                m_loopDragOrigStart = m_loopStart;
                m_loopDragOrigEnd = m_loopEnd;
                return true;
            }
        }

        if (m_onPlayheadClick) m_onPlayheadClick(beat);
        return true;
    }

    // Track header click
    if (e.x >= x && e.x < gridX &&
        e.y >= gridY && e.y < gridY + gridH) {
        float relY = e.y - gridY + m_scrollY;
        int track = trackAtY(relY);
        if (track >= 0 && track < m_project->numTracks()) {
            float ty = gridY + trackYOffset(track) - m_scrollY;
            float baseH = trackBaseHeight(track);

            // Check for resize handle (bottom 4px of base area)
            float resizeZone = ty + baseH;
            if (e.y >= resizeZone - 4 && e.y <= resizeZone + 2) {
                m_dragMode = DragMode::ResizeTrackH;
                m_resizeTrack = track;
                m_resizeOrigH = baseH;
                m_resizeMouseStart = e.y;
                captureMouse();
                return true;
            }

            // Button row Y and dimensions (must match paintTrackHeaders)
            float btnRowY = ty + baseH - 22;
            float btnH = 16.0f;
            float btnGap = 4.0f;

            // Expand/collapse button [▶/▼]
            float expBtnX = x + 10;
            float expW = 20.0f;
            if (e.x >= expBtnX && e.x < expBtnX + expW &&
                e.y >= btnRowY && e.y < btnRowY + btnH) {
                toggleTrackExpanded(track);
                return true;
            }

            // S/A toggle button [Ses/Arr]
            float saBtnX = expBtnX + expW + btnGap;
            float saW = 36.0f;
            if (e.x >= saBtnX && e.x < saBtnX + saW &&
                e.y >= btnRowY && e.y < btnRowY + btnH) {
                auto& tr = m_project->track(track);
                bool oldVal = tr.arrangementActive;
                tr.arrangementActive = !oldVal;
                if (m_onTrackArrToggle)
                    m_onTrackArrToggle(track, tr.arrangementActive);
                if (m_undoManager) {
                    m_undoManager->push({"Toggle Arrangement Active",
                        [this, track, oldVal]{ m_project->track(track).arrangementActive = oldVal;
                            if (m_onTrackArrToggle) m_onTrackArrToggle(track, oldVal); },
                        [this, track, oldVal]{ m_project->track(track).arrangementActive = !oldVal;
                            if (m_onTrackArrToggle) m_onTrackArrToggle(track, !oldVal); },
                        ""});
                }
                return true;
            }

            m_selectedTrack = track;
            if (m_onTrackClick) m_onTrackClick(track);

            // Double-click on track name area → rename
            if (e.isDoubleClick() && e.y < btnRowY) {
                startTrackRename(track);
            }
        }
        return true;
    }

    // Scrollbar drag
    float sbY = gridY + gridH;
    if (e.y >= sbY && e.y < sbY + kScrollbarH &&
        e.x >= gridX && e.x < gridX + gridW) {
        m_scrollDragging = true;
        m_scrollDragStart = e.x;
        m_scrollDragBase = m_scrollX;
        captureMouse();
        return true;
    }

    // Grid area
    if (e.x >= gridX && e.x < gridX + gridW &&
        e.y >= gridY && e.y < gridY + gridH) {
        float relY = e.y - gridY + m_scrollY;
        int trackIdx = trackAtY(relY);
        double beat = static_cast<double>((e.x - gridX + m_scrollX) / m_pixelsPerBeat);

        if (trackIdx < 0 || trackIdx >= m_project->numTracks()) return true;
        m_selectedTrack = trackIdx;
        if (m_onTrackClick) m_onTrackClick(trackIdx);

        float trackTop = trackYOffset(trackIdx);
        float relInTrack = relY - trackTop;
        int laneIdx = autoLaneAtY(trackIdx, relInTrack);

        // Automation lane click
        if (laneIdx >= 0) {
            auto& lane = m_project->track(trackIdx).automationLanes[laneIdx];
            float laneTop = gridY + trackTop + trackBaseHeight(trackIdx) + laneIdx * kAutoLaneH - m_scrollY;

            // Convert mouse to time and value
            double clickBeat = beat;
            float clickVal = 1.0f - (e.y - laneTop) / kAutoLaneH;
            clickVal = std::clamp(clickVal, 0.0f, 1.0f);

            // Hit-test existing breakpoints
            constexpr float kHitRadius = 8.0f;
            int hitPt = -1;
            for (int i = 0; i < lane.envelope.pointCount(); ++i) {
                auto& pt = lane.envelope.point(i);
                float ptX = gridX + static_cast<float>(pt.time) * m_pixelsPerBeat - m_scrollX;
                float ptY = laneTop + (1.0f - pt.value) * kAutoLaneH;
                float dx = e.x - ptX;
                float dy = e.y - ptY;
                if (dx * dx + dy * dy < kHitRadius * kHitRadius) {
                    hitPt = i;
                    break;
                }
            }

            if (e.isRightButton() && hitPt >= 0) {
                // Right-click: delete point
                auto oldPt = lane.envelope.point(hitPt);
                int ti = trackIdx, li = laneIdx;
                lane.envelope.removePoint(hitPt);
                if (m_undoManager) {
                    m_undoManager->push({"Delete Automation Point",
                        [this, ti, li, oldPt]{
                            m_project->track(ti).automationLanes[li].envelope.addPoint(oldPt.time, oldPt.value);
                        },
                        [this, ti, li, oldPt]{
                            auto& env = m_project->track(ti).automationLanes[li].envelope;
                            for (int i = 0; i < env.pointCount(); ++i) {
                                if (std::abs(env.point(i).time - oldPt.time) < 0.001 &&
                                    std::abs(env.point(i).value - oldPt.value) < 0.001f) {
                                    env.removePoint(i); break;
                                }
                            }
                        }, ""});
                }
                return true;
            }

            if (hitPt >= 0) {
                // Drag existing point
                m_dragMode = DragMode::AutoPoint;
                m_autoPointDragTrack = trackIdx;
                m_autoPointDragLane = laneIdx;
                m_autoPointDragIdx = hitPt;
                m_autoPointDragOrigTime = lane.envelope.point(hitPt).time;
                m_autoPointDragOrigVal = lane.envelope.point(hitPt).value;
                m_dragStartBeat = beat;
                captureMouse();
            } else {
                // Add new point
                lane.envelope.addPoint(clickBeat, clickVal);
                if (m_undoManager) {
                    int ti = trackIdx, li = laneIdx;
                    m_undoManager->push({"Add Automation Point",
                        [this, ti, li, clickBeat, clickVal]{
                            auto& env = m_project->track(ti).automationLanes[li].envelope;
                            for (int i = 0; i < env.pointCount(); ++i) {
                                if (std::abs(env.point(i).time - clickBeat) < 0.001 &&
                                    std::abs(env.point(i).value - clickVal) < 0.001f) {
                                    env.removePoint(i); break;
                                }
                            }
                        },
                        [this, ti, li, clickBeat, clickVal]{
                            m_project->track(ti).automationLanes[li].envelope.addPoint(clickBeat, clickVal);
                        }, ""});
                }
            }
            return true;
        }

        // Clip interaction (main row only)
        auto& clips = m_project->track(trackIdx).arrangementClips;
        int hitIdx = -1;
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            if (beat >= clips[i].startBeat && beat < clips[i].endBeat()) {
                hitIdx = i;
                break;
            }
        }

        if (hitIdx >= 0) {
            m_selClipTrack = trackIdx;
            m_selClipIdx = hitIdx;
            auto& clip = clips[hitIdx];

            float clipLpx = gridX + static_cast<float>(clip.startBeat) * m_pixelsPerBeat - m_scrollX;
            float clipRpx = gridX + static_cast<float>(clip.endBeat()) * m_pixelsPerBeat - m_scrollX;
            constexpr float kEdge = 6.0f;
            bool wideEnough = (clip.lengthBeats * m_pixelsPerBeat) > kEdge * 3;

            if (e.x < clipLpx + kEdge && wideEnough)
                m_dragMode = DragMode::ResizeLeft;
            else if (e.x > clipRpx - kEdge && wideEnough)
                m_dragMode = DragMode::ResizeRight;
            else
                m_dragMode = DragMode::MoveClip;

            m_dragStartBeat = beat;
            m_dragOrigStart = clip.startBeat;
            m_dragOrigLength = clip.lengthBeats;
            m_dragOrigOffset = clip.offsetBeats;
            m_dragOrigTrack = trackIdx;
            captureMouse();
        } else {
            clearClipSelection();

            // Double-click on empty space in MIDI track → add empty clip
            if (e.isDoubleClick() &&
                m_project->track(trackIdx).type == Track::Type::Midi) {
                double snapB = std::max(0.0, snapBeat(beat));
                double len = snapVal() > 0 ? std::max(snapVal() * 4.0, 4.0) : 4.0;

                ArrangementClip nc;
                nc.type = ArrangementClip::Type::Midi;
                nc.startBeat = snapB;
                nc.lengthBeats = len;
                nc.name = "Clip";
                nc.midiClip = std::make_shared<midi::MidiClip>();
                nc.midiClip->setLengthBeats(len);

                m_project->track(trackIdx).arrangementClips.push_back(std::move(nc));
                m_project->track(trackIdx).sortArrangementClips();
                m_project->updateArrangementLength();

                auto& cs = m_project->track(trackIdx).arrangementClips;
                for (int i = 0; i < static_cast<int>(cs.size()); ++i) {
                    if (std::abs(cs[i].startBeat - snapB) < 0.001) {
                        m_selClipTrack = trackIdx;
                        m_selClipIdx = i;
                        break;
                    }
                }
                if (m_onClipChange) m_onClipChange(trackIdx);
                if (m_undoManager) {
                    int ti = trackIdx;
                    m_undoManager->push({"Add Arrangement Clip",
                        [this, ti, snapB]{
                            auto& clips = m_project->track(ti).arrangementClips;
                            for (auto it = clips.begin(); it != clips.end(); ++it) {
                                if (std::abs(it->startBeat - snapB) < 0.001) {
                                    clips.erase(it); break;
                                }
                            }
                            m_project->updateArrangementLength();
                            clearClipSelection();
                        },
                        [this, ti, snapB, len]{
                            ArrangementClip nc2;
                            nc2.type = ArrangementClip::Type::Midi;
                            nc2.startBeat = snapB;
                            nc2.lengthBeats = len;
                            nc2.name = "Clip";
                            nc2.midiClip = std::make_shared<midi::MidiClip>();
                            nc2.midiClip->setLengthBeats(len);
                            m_project->track(ti).arrangementClips.push_back(std::move(nc2));
                            m_project->track(ti).sortArrangementClips();
                            m_project->updateArrangementLength();
                        }, ""});
                }
            }
        }
        return true;
    }

    return false;
}

bool ArrangementPanel::onMouseUp(MouseEvent&) {
    if (m_loopDragMode != LoopDragMode::None) {
        if (m_undoManager && (m_loopStart != m_loopDragOrigStart || m_loopEnd != m_loopDragOrigEnd)) {
            double oldS = m_loopDragOrigStart, oldE = m_loopDragOrigEnd;
            double newS = m_loopStart, newE = m_loopEnd;
            m_undoManager->push({"Change Loop Range",
                [this, oldS, oldE]{ m_loopStart = oldS; m_loopEnd = oldE;
                    if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd); },
                [this, newS, newE]{ m_loopStart = newS; m_loopEnd = newE;
                    if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd); },
                ""});
        }
        m_loopDragMode = LoopDragMode::None;
        releaseMouse();
        return true;
    }
    if (m_scrollDragging) {
        m_scrollDragging = false;
        releaseMouse();
        return true;
    }
    if (m_dragMode != DragMode::None) {
        if (m_dragMode == DragMode::AutoPoint) {
            // Push undo for auto point move if it actually moved
            if (m_undoManager && m_autoPointDragTrack >= 0 && m_autoPointDragLane >= 0) {
                int ti = m_autoPointDragTrack, li = m_autoPointDragLane;
                double origTime = m_autoPointDragOrigTime;
                float origVal = m_autoPointDragOrigVal;
                // Find current position of the dragged point
                auto& lane = m_project->track(ti).automationLanes[li];
                if (m_autoPointDragIdx >= 0 && m_autoPointDragIdx < lane.envelope.pointCount()) {
                    auto pt = lane.envelope.point(m_autoPointDragIdx);
                    double newTime = pt.time;
                    float newVal = pt.value;
                    if (std::abs(newTime - origTime) > 0.001 || std::abs(newVal - origVal) > 0.001f) {
                        m_undoManager->push({"Move Automation Point",
                            [this, ti, li, newTime, newVal, origTime, origVal]{
                                auto& env = m_project->track(ti).automationLanes[li].envelope;
                                for (int i = 0; i < env.pointCount(); ++i) {
                                    if (std::abs(env.point(i).time - newTime) < 0.001 &&
                                        std::abs(env.point(i).value - newVal) < 0.001f) {
                                        env.movePoint(i, origTime, origVal); break;
                                    }
                                }
                            },
                            [this, ti, li, newTime, newVal, origTime, origVal]{
                                auto& env = m_project->track(ti).automationLanes[li].envelope;
                                for (int i = 0; i < env.pointCount(); ++i) {
                                    if (std::abs(env.point(i).time - origTime) < 0.001 &&
                                        std::abs(env.point(i).value - origVal) < 0.001f) {
                                        env.movePoint(i, newTime, newVal); break;
                                    }
                                }
                            }, ""});
                    }
                }
            }
            m_autoPointDragIdx = -1;
            m_autoPointDragLane = -1;
            m_autoPointDragTrack = -1;
        } else if (m_dragMode == DragMode::ResizeTrackH) {
            m_resizeTrack = -1;
        } else {
            int affected = m_selClipTrack;
            bool crossTrack = (m_dragOrigTrack >= 0 && m_dragOrigTrack != m_selClipTrack);
            if (m_onClipChange) {
                m_onClipChange(affected);
                if (crossTrack) m_onClipChange(m_dragOrigTrack);
            }
            // Push undo for clip move/resize
            if (m_undoManager && m_selClipTrack >= 0 && m_selClipIdx >= 0) {
                auto& clips = m_project->track(m_selClipTrack).arrangementClips;
                if (m_selClipIdx < static_cast<int>(clips.size())) {
                    auto& c = clips[m_selClipIdx];
                    double newStart = c.startBeat;
                    double newLen = c.lengthBeats;
                    int newTrack = m_selClipTrack;
                    double origStart = m_dragOrigStart;
                    double origLen = m_dragOrigLength;
                    int origTrack = m_dragOrigTrack >= 0 ? m_dragOrigTrack : m_selClipTrack;
                    bool changed = (newStart != origStart || newLen != origLen || newTrack != origTrack);
                    if (changed) {
                        if (crossTrack) {
                            // Cross-track move: clip was removed from origTrack, added to newTrack
                            ArrangementClip snapshot = c;
                            m_undoManager->push({"Move Arrangement Clip",
                                [this, origTrack, origStart, origLen, newTrack, newStart, snapshot]{
                                    // Remove from new track
                                    auto& dst = m_project->track(newTrack).arrangementClips;
                                    for (auto it = dst.begin(); it != dst.end(); ++it) {
                                        if (std::abs(it->startBeat - newStart) < 0.001) {
                                            dst.erase(it); break;
                                        }
                                    }
                                    // Add back to original track
                                    ArrangementClip restored = snapshot;
                                    restored.startBeat = origStart;
                                    restored.lengthBeats = origLen;
                                    m_project->track(origTrack).arrangementClips.push_back(std::move(restored));
                                    m_project->track(origTrack).sortArrangementClips();
                                    m_project->track(newTrack).sortArrangementClips();
                                    m_project->updateArrangementLength();
                                    clearClipSelection();
                                },
                                [this, origTrack, origStart, newTrack, newStart, newLen, snapshot]{
                                    // Remove from original track
                                    auto& src = m_project->track(origTrack).arrangementClips;
                                    for (auto it = src.begin(); it != src.end(); ++it) {
                                        if (std::abs(it->startBeat - origStart) < 0.001) {
                                            src.erase(it); break;
                                        }
                                    }
                                    // Add to new track
                                    ArrangementClip moved = snapshot;
                                    moved.startBeat = newStart;
                                    moved.lengthBeats = newLen;
                                    m_project->track(newTrack).arrangementClips.push_back(std::move(moved));
                                    m_project->track(newTrack).sortArrangementClips();
                                    m_project->track(origTrack).sortArrangementClips();
                                    m_project->updateArrangementLength();
                                    clearClipSelection();
                                }, ""});
                        } else {
                            // Same-track move or resize
                            m_undoManager->push({"Move/Resize Arrangement Clip",
                                [this, newTrack, newStart, origStart, origLen]{
                                    auto& cs = m_project->track(newTrack).arrangementClips;
                                    for (auto& cl : cs) {
                                        if (std::abs(cl.startBeat - newStart) < 0.001) {
                                            cl.startBeat = origStart;
                                            cl.lengthBeats = origLen;
                                            break;
                                        }
                                    }
                                    m_project->track(newTrack).sortArrangementClips();
                                    m_project->updateArrangementLength();
                                },
                                [this, newTrack, origStart, newStart, newLen]{
                                    auto& cs = m_project->track(newTrack).arrangementClips;
                                    for (auto& cl : cs) {
                                        if (std::abs(cl.startBeat - origStart) < 0.001) {
                                            cl.startBeat = newStart;
                                            cl.lengthBeats = newLen;
                                            break;
                                        }
                                    }
                                    m_project->track(newTrack).sortArrangementClips();
                                    m_project->updateArrangementLength();
                                }, ""});
                        }
                    }
                }
            }
        }
        m_dragMode = DragMode::None;
        releaseMouse();
        return true;
    }
    return false;
}

bool ArrangementPanel::onMouseMove(MouseMoveEvent& e) {
    // Loop marker drag
    if (m_loopDragMode != LoopDragMode::None) {
        float gridX = m_bounds.x + kTrackHeaderW;
        double beat = std::max(0.0, static_cast<double>((e.x - gridX + m_scrollX) / m_pixelsPerBeat));
        beat = snapBeat(beat);
        if (m_loopDragMode == LoopDragMode::DragStart) {
            m_loopStart = std::min(beat, m_loopEnd - 0.25);
        } else if (m_loopDragMode == LoopDragMode::DragEnd) {
            m_loopEnd = std::max(beat, m_loopStart + 0.25);
        }
        if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd);
        return true;
    }

    if (m_scrollDragging) {
        float delta = e.x - m_scrollDragStart;
        float contentW = static_cast<float>(m_project->arrangementLength()) * m_pixelsPerBeat;
        float gridW = m_bounds.w - kTrackHeaderW;
        float maxScroll = std::max(0.0f, contentW - gridW);
        float ratio = contentW > gridW ? contentW / gridW : 1.0f;
        m_scrollX = std::clamp(m_scrollDragBase + delta * ratio, 0.0f, maxScroll);
        return true;
    }

    if (m_dragMode == DragMode::ResizeTrackH) {
        float delta = e.y - m_resizeMouseStart;
        setTrackBaseHeight(m_resizeTrack, m_resizeOrigH + delta);
        return true;
    }

    // Hover detection for resize cursor on track header bottom borders
    if (m_dragMode == DragMode::None && m_project) {
        float hdrX = m_bounds.x;
        float gridY = m_bounds.y + kRulerH;
        float gridH = m_bounds.h - kRulerH - kScrollbarH;
        bool foundResize = false;
        if (e.x >= hdrX && e.x < hdrX + kTrackHeaderW &&
            e.y >= gridY && e.y < gridY + gridH) {
            int numTracks = m_project->numTracks();
            for (int t = 0; t < numTracks; ++t) {
                float ty = gridY + trackYOffset(t) - m_scrollY;
                float baseH = trackBaseHeight(t);
                float borderY = ty + baseH;
                if (e.y >= borderY - 4 && e.y <= borderY + 2) {
                    foundResize = true;
                    break;
                }
            }
        }
        m_hoverResize = foundResize;
    }

    if (m_dragMode == DragMode::None) return false;

    float gridX = m_bounds.x + kTrackHeaderW;
    float gridY = m_bounds.y + kRulerH;
    double curBeat = static_cast<double>((e.x - gridX + m_scrollX) / m_pixelsPerBeat);

    // Automation point drag
    if (m_dragMode == DragMode::AutoPoint) {
        if (m_autoPointDragTrack < 0 || m_autoPointDragLane < 0 || m_autoPointDragIdx < 0)
            return false;
        auto& lane = m_project->track(m_autoPointDragTrack).automationLanes[m_autoPointDragLane];
        float laneTop = gridY + trackYOffset(m_autoPointDragTrack) + trackBaseHeight(m_autoPointDragTrack)
                       + m_autoPointDragLane * kAutoLaneH - m_scrollY;
        double newTime = std::max(0.0, curBeat);
        float newVal = 1.0f - (e.y - laneTop) / kAutoLaneH;
        newVal = std::clamp(newVal, 0.0f, 1.0f);
        lane.envelope.movePoint(m_autoPointDragIdx, newTime, newVal);
        // Re-find index after sort inside movePoint
        m_autoPointDragIdx = -1;
        for (int i = 0; i < lane.envelope.pointCount(); ++i) {
            if (std::abs(lane.envelope.point(i).time - newTime) < 0.001 &&
                std::abs(lane.envelope.point(i).value - newVal) < 0.001f) {
                m_autoPointDragIdx = i;
                break;
            }
        }
        return true;
    }

    // Clip drag
    if (m_selClipTrack < 0 || m_selClipIdx < 0) return false;
    double delta = curBeat - m_dragStartBeat;

    if (m_dragMode == DragMode::MoveClip) {
        double newStart = snapBeat(m_dragOrigStart + delta);
        newStart = std::max(0.0, newStart);

        float relY = e.y - gridY + m_scrollY;
        int newTrack = trackAtY(relY);
        newTrack = std::clamp(newTrack, 0, m_project->numTracks() - 1);

        if (newTrack != m_selClipTrack) {
            auto& src = m_project->track(m_selClipTrack).arrangementClips;
            ArrangementClip moved = src[m_selClipIdx];
            moved.startBeat = newStart;
            src.erase(src.begin() + m_selClipIdx);
            m_project->track(m_selClipTrack).sortArrangementClips();

            auto& dst = m_project->track(newTrack).arrangementClips;
            dst.push_back(std::move(moved));
            m_project->track(newTrack).sortArrangementClips();

            m_selClipIdx = 0;
            for (int i = 0; i < static_cast<int>(dst.size()); ++i) {
                if (std::abs(dst[i].startBeat - newStart) < 0.001) {
                    m_selClipIdx = i; break;
                }
            }
            m_selClipTrack = newTrack;
            m_selectedTrack = newTrack;
        } else {
            auto& clips = m_project->track(m_selClipTrack).arrangementClips;
            clips[m_selClipIdx].startBeat = newStart;
            m_project->track(m_selClipTrack).sortArrangementClips();
            for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                if (std::abs(clips[i].startBeat - newStart) < 0.001) {
                    m_selClipIdx = i; break;
                }
            }
        }
        m_project->updateArrangementLength();
    }
    else if (m_dragMode == DragMode::ResizeRight) {
        double newLen = snapBeat(m_dragOrigLength + delta);
        double minLen = snapVal() > 0 ? snapVal() : 0.25;
        newLen = std::max(minLen, newLen);
        m_project->track(m_selClipTrack).arrangementClips[m_selClipIdx].lengthBeats = newLen;
        m_project->updateArrangementLength();
    }
    else if (m_dragMode == DragMode::ResizeLeft) {
        double newStart = snapBeat(m_dragOrigStart + delta);
        newStart = std::max(0.0, newStart);
        double endBt = m_dragOrigStart + m_dragOrigLength;
        double minLen = snapVal() > 0 ? snapVal() : 0.25;
        if (newStart > endBt - minLen) newStart = endBt - minLen;

        auto& c = m_project->track(m_selClipTrack).arrangementClips[m_selClipIdx];
        c.lengthBeats = endBt - newStart;
        c.offsetBeats = m_dragOrigOffset + (newStart - m_dragOrigStart);
        c.startBeat = newStart;
        m_project->updateArrangementLength();
    }

    return true;
}

bool ArrangementPanel::onScroll(ScrollEvent& e) {
    if (!m_project) return false;

    // Ctrl+scroll = zoom
    if (e.mods.ctrl) {
        float zoomFactor = e.dy > 0 ? 1.15f : (1.0f / 1.15f);
        float oldZoom = m_pixelsPerBeat;
        m_pixelsPerBeat = std::clamp(m_pixelsPerBeat * zoomFactor, kMinZoom, kMaxZoom);

        float gridX = m_bounds.x + kTrackHeaderW;
        float mouseGridX = e.x - gridX;
        float beatAtMouse = (mouseGridX + m_scrollX) / oldZoom;
        m_scrollX = beatAtMouse * m_pixelsPerBeat - mouseGridX;
        m_scrollX = std::max(0.0f, m_scrollX);
        return true;
    }

    // Shift+scroll = horizontal
    if (e.mods.shift || std::abs(e.dx) > std::abs(e.dy)) {
        float d = e.mods.shift ? e.dy : e.dx;
        m_scrollX = std::max(0.0f, m_scrollX - d * 30.0f);
        return true;
    }

    // Vertical scroll
    m_scrollY = std::max(0.0f, m_scrollY - e.dy * 30.0f);
    return true;
}

bool ArrangementPanel::onKeyDown(KeyEvent& e) {
    if (!m_project) return false;

    // Delete selected clip
    if ((e.isDelete() || e.isBackspace()) && m_selClipTrack >= 0 && m_selClipIdx >= 0) {
        auto& clips = m_project->track(m_selClipTrack).arrangementClips;
        if (m_selClipIdx < static_cast<int>(clips.size())) {
            int t = m_selClipTrack;
            ArrangementClip backup = clips[m_selClipIdx];
            clips.erase(clips.begin() + m_selClipIdx);
            clearClipSelection();
            m_project->updateArrangementLength();
            if (m_onClipChange) m_onClipChange(t);
            if (m_undoManager) {
                m_undoManager->push({"Delete Arrangement Clip",
                    [this, t, backup]{
                        m_project->track(t).arrangementClips.push_back(backup);
                        m_project->track(t).sortArrangementClips();
                        m_project->updateArrangementLength();
                    },
                    [this, t, sb = backup.startBeat]{
                        auto& cs = m_project->track(t).arrangementClips;
                        for (auto it = cs.begin(); it != cs.end(); ++it) {
                            if (std::abs(it->startBeat - sb) < 0.001) {
                                cs.erase(it); break;
                            }
                        }
                        m_project->updateArrangementLength();
                        clearClipSelection();
                    }, ""});
            }
            return true;
        }
    }

    // Ctrl+D = duplicate selected clip
    if (e.keyCode == 'd' && e.mods.ctrl && m_selClipTrack >= 0 && m_selClipIdx >= 0) {
        auto& clips = m_project->track(m_selClipTrack).arrangementClips;
        if (m_selClipIdx < static_cast<int>(clips.size())) {
            ArrangementClip dup = clips[m_selClipIdx];
            double dupStart = dup.endBeat();
            dup.startBeat = dupStart;
            clips.push_back(std::move(dup));
            m_project->track(m_selClipTrack).sortArrangementClips();
            m_project->updateArrangementLength();
            int t = m_selClipTrack;
            for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                if (std::abs(clips[i].startBeat - dupStart) < 0.001) {
                    m_selClipIdx = i; break;
                }
            }
            if (m_onClipChange) m_onClipChange(t);
            if (m_undoManager) {
                m_undoManager->push({"Duplicate Arrangement Clip",
                    [this, t, dupStart]{
                        auto& cs = m_project->track(t).arrangementClips;
                        for (auto it = cs.begin(); it != cs.end(); ++it) {
                            if (std::abs(it->startBeat - dupStart) < 0.001) {
                                cs.erase(it); break;
                            }
                        }
                        m_project->updateArrangementLength();
                        clearClipSelection();
                    },
                    [this, t, dupStart, origIdx = m_selClipIdx - (m_selClipIdx > 0 ? 0 : 0)]{
                        auto& cs = m_project->track(t).arrangementClips;
                        // find original clip near the duplicated position minus its length
                        for (auto& c : cs) {
                            if (std::abs(c.endBeat() - dupStart) < 0.001) {
                                ArrangementClip dup2 = c;
                                dup2.startBeat = dupStart;
                                cs.push_back(std::move(dup2));
                                m_project->track(t).sortArrangementClips();
                                m_project->updateArrangementLength();
                                break;
                            }
                        }
                    }, ""});
            }
            return true;
        }
    }

    // L = toggle loop
    if (e.keyCode == 'l') {
        bool wasEnabled = m_loopEnabled;
        double oldStart = m_loopStart, oldEnd = m_loopEnd;
        m_loopEnabled = !m_loopEnabled;
        if (m_loopEnabled && m_loopEnd <= m_loopStart) {
            double cur = m_engine ? m_engine->transport().positionInBeats() : 0.0;
            int beatsPerBar = m_engine ? m_engine->transport().numerator() : 4;
            m_loopStart = snapBeat(cur);
            m_loopEnd = m_loopStart + beatsPerBar * 4.0;
        }
        if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd);
        if (m_undoManager) {
            bool newEnabled = m_loopEnabled;
            double newStart = m_loopStart, newEnd = m_loopEnd;
            m_undoManager->push({"Toggle Loop",
                [this, wasEnabled, oldStart, oldEnd]{ m_loopEnabled = wasEnabled; m_loopStart = oldStart; m_loopEnd = oldEnd;
                    if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd); },
                [this, newEnabled, newStart, newEnd]{ m_loopEnabled = newEnabled; m_loopStart = newStart; m_loopEnd = newEnd;
                    if (m_onLoopChange) m_onLoopChange(m_loopEnabled, m_loopStart, m_loopEnd); },
                ""});
        }
        return true;
    }

    // F = toggle auto-scroll (follow)
    if (e.keyCode == 'f') {
        m_autoScroll = !m_autoScroll;
        return true;
    }

    return false;
}

// ── Painting helpers ───────────────────────────────────────────────────

#ifndef YAWN_TEST_BUILD

void ArrangementPanel::paintRuler(Renderer2D& r, Font& f, float x, float y, float w) {
    r.drawRect(x, y, w, kRulerH, Color{35, 35, 40, 255});
    r.pushClip(x - 1, y, w + 2, kRulerH + 1);

    // Draw loop range highlight
    if (m_loopEnabled && m_loopEnd > m_loopStart) {
        float lx0 = x + static_cast<float>(m_loopStart) * m_pixelsPerBeat - m_scrollX;
        float lx1 = x + static_cast<float>(m_loopEnd) * m_pixelsPerBeat - m_scrollX;
        lx0 = std::max(lx0, x);
        lx1 = std::min(lx1, x + w);
        if (lx1 > lx0) {
            r.drawRect(lx0, y, lx1 - lx0, kRulerH, Color{60, 120, 80, 80});
            r.drawRect(lx0, y, 2, kRulerH, Color{100, 200, 120, 200});
            r.drawRect(lx1 - 2, y, 2, kRulerH, Color{100, 200, 120, 200});
        }
    }

    int beatsPerBar = m_engine ? m_engine->transport().numerator() : 4;
    float barWidthPx = beatsPerBar * m_pixelsPerBeat;

    int barLabelEvery = 1;
    if (barWidthPx < 30.0f) barLabelEvery = 8;
    else if (barWidthPx < 60.0f) barLabelEvery = 4;
    else if (barWidthPx < 120.0f) barLabelEvery = 2;

    float startBeat = m_scrollX / m_pixelsPerBeat;
    int startBar = static_cast<int>(startBeat / beatsPerBar);
    float endBeat = startBeat + w / m_pixelsPerBeat;
    int endBar = static_cast<int>(endBeat / beatsPerBar) + 1;

    float scale = Theme::kSmallFontSize / f.pixelHeight();
    float textH = Theme::kSmallFontSize;
    float textY = y + (kRulerH - textH) * 0.5f;

    for (int bar = startBar; bar <= endBar; ++bar) {
        float beatPos = static_cast<float>(bar * beatsPerBar);
        float px = x + beatPos * m_pixelsPerBeat - m_scrollX;
        r.drawRect(px, y, 1, kRulerH, Color{80, 80, 90, 255});

        if (bar >= 0 && (bar % barLabelEvery) == 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", bar + 1);
            float tx = px + 4;
            for (const char* p = buf; *p; ++p) {
                auto g = f.getGlyph(*p, tx, textY, scale);
                r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   Theme::textSecondary, f.textureId());
                tx += g.xAdvance;
            }
        }

        if (m_pixelsPerBeat >= 8.0f) {
            for (int b = 1; b < beatsPerBar; ++b) {
                float bpx = x + (beatPos + b) * m_pixelsPerBeat - m_scrollX;
                r.drawRect(bpx, y + kRulerH * 0.6f, 1, kRulerH * 0.4f,
                           Color{60, 60, 70, 255});
            }
        }
    }

    r.drawRect(x, y + kRulerH - 1, w, 1, Color{70, 70, 80, 255});
    r.popClip();
}

void ArrangementPanel::paintTrackHeaders(Renderer2D& r, Font& f,
                                          float x, float y, float h) {
    r.pushClip(x, y, kTrackHeaderW, h);
    int numTracks = m_project->numTracks();
    float scale = Theme::kSmallFontSize / f.pixelHeight();
    float smallScale = (Theme::kSmallFontSize * 0.85f) / f.pixelHeight();

    for (int t = 0; t < numTracks; ++t) {
        float ty = y + trackYOffset(t) - m_scrollY;
        float baseH = trackBaseHeight(t);
        float rowH = trackRowHeight(t);
        if (ty + rowH < y || ty > y + h) continue;

        auto& tr = m_project->track(t);

        // Main row background
        Color bg = (t == m_selectedTrack)
            ? Color{50, 55, 65, 255} : Color{42, 42, 46, 255};
        r.drawRect(x, ty, kTrackHeaderW, baseH, bg);

        // Color bar (left edge)
        Color col = Theme::trackColors[tr.colorIndex % Theme::kNumTrackColors];
        r.drawRect(x + 1, ty + 1, 4, baseH - 2, col);

        // Track name (top row)
        if (t == m_renameTrack) {
            // Inline rename text box — matches normal text position
            float boxX = x + 7, boxY = ty + 2;
            float boxW = kTrackHeaderW - 12, boxH = baseH / 2 - 2;
            r.drawRect(boxX, boxY, boxW, boxH, Color{20, 22, 28, 255});
            r.drawRectOutline(boxX, boxY, boxW, boxH, Theme::transportAccent);
            float cx = boxX + 4;
            float textY = ty + 4;
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
            float tx = x + 10;
            float textY = ty + 4;
            float maxNameX = x + kTrackHeaderW - 6;
            for (const char* p = tr.name.c_str(); *p && tx < maxNameX; ++p) {
                auto g = f.getGlyph(*p, tx, textY, scale);
                r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   Theme::textPrimary, f.textureId());
                tx += g.xAdvance;
            }
        }

        // Button row (bottom area)
        float btnRowY = ty + baseH - 22;
        float btnH = 16.0f;
        float btnGap = 4.0f;
        float bx = x + 10;

        // Expand/collapse button — filled triangle ▶ or ▼
        float expW = 20.0f;
        bool expanded = m_expandedTracks.count(t) > 0;
        Color expBg = expanded ? Color{55, 65, 75, 255} : Color{48, 48, 52, 255};
        r.drawRoundedRect(bx, btnRowY, expW, btnH, 3.0f, expBg, 4);
        {
            float cx = bx + expW * 0.5f;
            float cy = btnRowY + btnH * 0.5f;
            float ts = 4.0f; // triangle half-size
            Color triCol{180, 180, 190, 220};
            if (expanded) {
                // Down-pointing triangle ▼
                r.drawTriangle(cx - ts, cy - ts * 0.5f,
                               cx + ts, cy - ts * 0.5f,
                               cx,      cy + ts * 0.5f, triCol);
            } else {
                // Right-pointing triangle ▶
                r.drawTriangle(cx - ts * 0.4f, cy - ts,
                               cx - ts * 0.4f, cy + ts,
                               cx + ts * 0.6f, cy, triCol);
            }
        }
        bx += expW + btnGap;

        // S/A toggle button — text centered inside
        float saW = 36.0f;
        bool arrActive = tr.arrangementActive;
        Color saBg = arrActive ? Color{60, 130, 70, 255} : Color{60, 60, 65, 255};
        r.drawRoundedRect(bx, btnRowY, saW, btnH, 3.0f, saBg, 4);
        {
            const char* saLabel = arrActive ? "Arr" : "Ses";
            // Measure text width
            float tw = 0;
            for (const char* p = saLabel; *p; ++p) {
                auto g = f.getGlyph(*p, 0, 0, smallScale);
                tw += g.xAdvance;
            }
            float saTextX = bx + (saW - tw) * 0.5f;
            float saTextY = btnRowY + (btnH - Theme::kSmallFontSize * 0.85f) * 0.5f;
            for (const char* p = saLabel; *p; ++p) {
                auto g = f.getGlyph(*p, saTextX, saTextY, smallScale);
                r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   Color{255, 255, 255, 220}, f.textureId());
                saTextX += g.xAdvance;
            }
        }

        // Bottom border — slightly thicker as resize handle hint
        r.drawRect(x, ty + baseH - 2, kTrackHeaderW, 2, Color{70, 70, 78, 255});

        // Automation lane headers (when expanded)
        if (expanded) {
            int nLanes = static_cast<int>(tr.automationLanes.size());
            for (int li = 0; li < nLanes; ++li) {
                float ly = ty + baseH + li * kAutoLaneH;
                if (ly + kAutoLaneH < y || ly > y + h) continue;

                r.drawRect(x, ly, kTrackHeaderW, kAutoLaneH, Color{36, 36, 40, 255});

                std::string name = autoLaneName(tr.automationLanes[li].target);
                float lnx = x + 10;
                float lny = ly + (kAutoLaneH - Theme::kSmallFontSize * 0.8f) * 0.5f;
                for (const char* p = name.c_str(); *p && lnx < x + kTrackHeaderW - 4; ++p) {
                    auto g = f.getGlyph(*p, lnx, lny, smallScale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Color{160, 160, 180, 200}, f.textureId());
                    lnx += g.xAdvance;
                }

                r.drawRect(x, ly + kAutoLaneH - 1, kTrackHeaderW, 1,
                           Color{50, 50, 55, 255});
            }
        }
    }
    r.popClip();
}

void ArrangementPanel::paintClipTimeline(Renderer2D& r, Font& f,
                                          float x, float y, float w, float h) {
    r.pushClip(x, y, w, h);

    int numTracks = m_project->numTracks();
    float scale = Theme::kSmallFontSize / f.pixelHeight();
    float viewStartBeat = m_scrollX / m_pixelsPerBeat;
    float viewEndBeat = viewStartBeat + w / m_pixelsPerBeat;

    // Grid lines (bar lines)
    int beatsPerBar = m_engine ? m_engine->transport().numerator() : 4;
    {
        int startBar = static_cast<int>(viewStartBeat / beatsPerBar);
        int endBar = static_cast<int>(viewEndBeat / beatsPerBar) + 1;
        for (int bar = startBar; bar <= endBar; ++bar) {
            float px = x + bar * beatsPerBar * m_pixelsPerBeat - m_scrollX;
            r.drawRect(px, y, 1, h, Color{45, 45, 50, 255});
        }
    }

    // Track rows and clips
    for (int t = 0; t < numTracks; ++t) {
        float ty = y + trackYOffset(t) - m_scrollY;
        float rowH = trackRowHeight(t);
        if (ty + rowH < y || ty > y + h) continue;

        // Main clip row background
        float baseH = trackBaseHeight(t);
        Color rowBg = (t % 2 == 0) ? Color{33, 33, 36, 255} : Color{30, 30, 33, 255};
        r.drawRect(x, ty, w, baseH, rowBg);
        r.drawRect(x, ty + baseH - 1, w, 1, Color{45, 45, 50, 100});

        // Arrangement clips
        auto& track = m_project->track(t);
        Color trackCol = Theme::trackColors[track.colorIndex % Theme::kNumTrackColors];

        for (int ci = 0; ci < static_cast<int>(track.arrangementClips.size()); ++ci) {
            auto& clip = track.arrangementClips[ci];
            if (clip.endBeat() < viewStartBeat || clip.startBeat > viewEndBeat)
                continue;

            float cx = x + static_cast<float>(clip.startBeat) * m_pixelsPerBeat - m_scrollX;
            float cw = static_cast<float>(clip.lengthBeats) * m_pixelsPerBeat;
            float cy = ty + 2;
            float ch = baseH - 4;

            Color clipCol = clip.colorIndex >= 0
                ? Theme::trackColors[clip.colorIndex % Theme::kNumTrackColors]
                : trackCol;
            bool selected = (t == m_selClipTrack && ci == m_selClipIdx);
            Color bodyCol{clipCol.r, clipCol.g, clipCol.b,
                          static_cast<uint8_t>(selected ? 200 : 160)};
            r.drawRoundedRect(cx, cy, cw, ch, 2.0f, bodyCol, 4);

            r.drawRectOutline(cx, cy, cw, ch,
                selected ? Color{255, 255, 255, 200} : clipCol.withAlpha(100));

            if (selected && cw > 18.0f) {
                float hw = 3.0f, hh = ch * 0.4f;
                float hy = cy + (ch - hh) * 0.5f;
                r.drawRect(cx + 1, hy, hw, hh, Color{255, 255, 255, 140});
                r.drawRect(cx + cw - hw - 1, hy, hw, hh, Color{255, 255, 255, 140});
            }

            if (cw > 20.0f && !clip.name.empty()) {
                float ntx = cx + 4;
                float nty = cy + (ch - Theme::kSmallFontSize) * 0.5f;
                float nMaxX = cx + cw - 4;
                for (const char* p = clip.name.c_str(); *p && ntx < nMaxX; ++p) {
                    auto g = f.getGlyph(*p, ntx, nty, scale);
                    r.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Color{255, 255, 255, 220}, f.textureId());
                    ntx += g.xAdvance;
                }
            }
        }

        // Auto lane background rows (when expanded)
        if (m_expandedTracks.count(t)) {
            int nLanes = static_cast<int>(track.automationLanes.size());
            for (int li = 0; li < nLanes; ++li) {
                float ly = ty + baseH + li * kAutoLaneH;
                r.drawRect(x, ly, w, kAutoLaneH, Color{28, 28, 32, 255});
                // Center line (value = 0.5)
                r.drawRect(x, ly + kAutoLaneH * 0.5f, w, 1, Color{40, 40, 48, 150});
                r.drawRect(x, ly + kAutoLaneH - 1, w, 1, Color{40, 40, 45, 100});
            }
        }
    }

    r.popClip();
}

void ArrangementPanel::paintAutoLanes(Renderer2D& r, Font&,
                                       float x, float y, float w, float h) {
    r.pushClip(x, y, w, h);

    int numTracks = m_project->numTracks();
    float viewStartBeat = m_scrollX / m_pixelsPerBeat;
    float viewEndBeat = viewStartBeat + w / m_pixelsPerBeat;

    for (int t = 0; t < numTracks; ++t) {
        if (!m_expandedTracks.count(t)) continue;
        float ty = y + trackYOffset(t) - m_scrollY;
        auto& track = m_project->track(t);
        int nLanes = static_cast<int>(track.automationLanes.size());

        for (int li = 0; li < nLanes; ++li) {
            float laneY = ty + trackBaseHeight(t) + li * kAutoLaneH;
            if (laneY + kAutoLaneH < y || laneY > y + h) continue;

            auto& lane = track.automationLanes[li];
            auto& pts = lane.envelope.points();
            if (pts.empty()) continue;

            Color lineCol{100, 160, 255, 200};
            Color ptCol{220, 230, 255, 255};

            // Draw envelope line segments
            for (int i = 0; i < static_cast<int>(pts.size()) - 1; ++i) {
                auto& a = pts[i];
                auto& b = pts[i + 1];
                if (b.time < viewStartBeat || a.time > viewEndBeat) continue;

                float ax = x + static_cast<float>(a.time) * m_pixelsPerBeat - m_scrollX;
                float ay = laneY + (1.0f - a.value) * kAutoLaneH;
                float bx = x + static_cast<float>(b.time) * m_pixelsPerBeat - m_scrollX;
                float by = laneY + (1.0f - b.value) * kAutoLaneH;
                r.drawLine(ax, ay, bx, by, lineCol);
            }

            // Extend to edges
            if (!pts.empty()) {
                auto& first = pts.front();
                float fy = laneY + (1.0f - first.value) * kAutoLaneH;
                float fx = x + static_cast<float>(first.time) * m_pixelsPerBeat - m_scrollX;
                if (fx > x)
                    r.drawLine(x, fy, fx, fy, Color{100, 160, 255, 80});

                auto& last = pts.back();
                float ly2 = laneY + (1.0f - last.value) * kAutoLaneH;
                float lx = x + static_cast<float>(last.time) * m_pixelsPerBeat - m_scrollX;
                if (lx < x + w)
                    r.drawLine(lx, ly2, x + w, ly2, Color{100, 160, 255, 80});
            }

            // Draw breakpoint handles
            for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
                auto& pt = pts[i];
                if (pt.time < viewStartBeat - 1 || pt.time > viewEndBeat + 1) continue;
                float px = x + static_cast<float>(pt.time) * m_pixelsPerBeat - m_scrollX;
                float py = laneY + (1.0f - pt.value) * kAutoLaneH;
                bool isDragged = (t == m_autoPointDragTrack &&
                                  li == m_autoPointDragLane &&
                                  i == m_autoPointDragIdx);
                Color pc = isDragged ? Color{255, 200, 60, 255} : ptCol;
                r.drawRect(px - 3, py - 3, 6, 6, pc);
                r.drawRectOutline(px - 3, py - 3, 6, 6, Color{40, 40, 50, 255});
            }
        }
    }

    r.popClip();
}

void ArrangementPanel::paintPlayhead(Renderer2D& r, float x, float y,
                                      float w, float h) {
    if (!m_engine) return;
    double beat = m_engine->transport().positionInBeats();
    float px = x + static_cast<float>(beat) * m_pixelsPerBeat - m_scrollX;

    if (px >= x && px <= x + w) {
        float rulerY = y - kRulerH;
        float triSize = 5.0f;
        r.drawTriangle(px - triSize, rulerY + kRulerH - 1,
                       px + triSize, rulerY + kRulerH - 1,
                       px, rulerY + kRulerH - triSize - 1,
                       Color{255, 255, 255, 200});
        r.drawRect(px, y, 1, h - kRulerH, Color{255, 255, 255, 150});
    }
}

void ArrangementPanel::paintScrollbar(Renderer2D& r, float x, float y, float w) {
    r.drawRect(x, y, w, kScrollbarH, Color{40, 40, 45, 255});

    float contentW = static_cast<float>(m_project->arrangementLength()) * m_pixelsPerBeat;
    if (contentW <= w) return;

    float thumbW = std::max(20.0f, w * (w / contentW));
    float scrollFrac = m_scrollX / (contentW - w);
    float thumbX = x + scrollFrac * (w - thumbW);

    Color thumbCol = m_scrollDragging ? Color{120, 120, 130, 255} : Color{80, 80, 90, 255};
    r.drawRoundedRect(thumbX, y + 1, thumbW, kScrollbarH - 2, 3.0f, thumbCol, 4);
}

#endif // !YAWN_TEST_BUILD

} // namespace fw
} // namespace ui
} // namespace yawn
