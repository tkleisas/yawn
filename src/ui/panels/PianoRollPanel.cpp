// PianoRollPanel.cpp — rendering and event implementations.
// Split from PianoRollPanel.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "PianoRollPanel.h"
#include "../Renderer.h"
#include "../Font.h"
#include "util/Logger.h"

namespace yawn {
namespace ui {
namespace fw {

void PianoRollPanel::paint(UIContext& ctx) {
    if (m_animatedHeight < 1.0f || !m_clip) return;
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;

    m_px = m_bounds.x; m_py = m_bounds.y; m_pw = m_bounds.w;
    m_pianoX = m_px + kClipOpsW;
    m_gx = m_px + kClipOpsW + kPianoW;
    m_gy = m_py + kHandleHeight + kToolbarH + kRulerH;
    m_gw = m_pw - kClipOpsW - kPianoW;
    float velH = m_showVelocityLane ? kVelLaneH : 0.0f;
    m_gh = m_bounds.h - kHandleHeight - kToolbarH - kRulerH - velH - kScrollbarH;
    m_velY = m_gy + m_gh;

    // Follow playhead
    if (m_followPlayhead && m_midiPlaying && m_clip) {
        double clipLen = m_clip->lengthBeats();
        if (clipLen > 0) {
            double beat = std::fmod(m_playBeat, clipLen);
            if (beat < 0) beat += clipLen;
            float beatPx = static_cast<float>(beat * m_pxBeat);
            // Keep playhead in the middle third of the view
            if (beatPx - m_scrollX > m_gw * 0.8f || beatPx - m_scrollX < m_gw * 0.1f) {
                m_scrollX = std::max(0.0f, beatPx - m_gw * 0.3f);
                clampScroll();
            }
        }
    }

    r.pushClip(m_px, m_py, m_pw, m_animatedHeight);

    r.drawRect(m_px, m_py, m_pw, m_bounds.h, Color{30, 30, 33});

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

    renderToolbar(ctx);

    renderRuler(r, f);

    r.pushClip(m_gx, m_gy, m_gw, m_gh);
    renderGrid(r);
    renderNotes(r);
    renderClipBound(r);
    renderRubberBand(r);
    renderPlayhead(r);
    r.popClip();

    r.pushClip(m_pianoX, m_gy, kPianoW, m_gh);
    renderPianoKeys(r, f);
    r.popClip();
    renderPianoKeyLabels(r, f);

    if (m_showVelocityLane)
        renderVelocityLane(r, f);
    renderScrollbar(ctx);
    renderClipOps(r, f);

    r.drawRect(m_px, m_py, m_pw, 1, Color{60, 60, 65});

    r.popClip();
}

bool PianoRollPanel::onMouseDown(MouseEvent& e) {
    if (!m_open || !m_clip) return false;
    float mx = e.x, my = e.y;
    if (!inPanel(mx, my)) return false;

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

    if (my >= m_py + kHandleHeight && my < m_py + kHandleHeight + kToolbarH) {
        LOG_DEBUG("UI", "PianoRoll toolbar click at (%.0f, %.0f) py=%.0f toolbarH=%.0f",
                    mx, my, m_py, kToolbarH);
        return handleToolbarClick(mx, my);
    }

    {
        float rulerY = m_py + kHandleHeight + kToolbarH;
        if (my >= rulerY && my < rulerY + kRulerH && mx >= m_gx && mx < m_gx + m_gw) {
            return handleRulerClick(mx);
        }
    }

    if (mx >= m_px && mx < m_px + kClipOpsW && my >= m_gy && my < m_gy + m_gh) {
        return handleClipOpsClick(mx, my);
    }

    // Piano key vertical zoom drag
    if (mx >= m_pianoX && mx < m_pianoX + kPianoW && my >= m_gy && my < m_gy + m_gh) {
        m_pianoKeyDragging = true;
        m_pianoKeyDragStartY = my;
        m_pianoKeyDragStartRowH = m_rowH;
        captureMouse();
        return true;
    }

    if (mx < m_gx) return true;

    if (m_showVelocityLane && my >= m_velY && my < m_velY + kVelLaneH && mx >= m_gx && mx < m_gx + m_gw) {
        return handleVelocityLaneClick(mx, my);
    }

    {
        float velH = m_showVelocityLane ? kVelLaneH : 0.0f;
        float sbY = m_gy + m_gh + velH;
        if (my >= sbY && my < sbY + kScrollbarH && mx >= m_gx && mx < m_gx + m_gw) {
            // v2 scrollbar — route through the gesture SM.
            const auto& sb = m_scrollbar.bounds();
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, sb);
            m_scrollbar.dispatchMouseDown(ev);
            m_v2Dragging = &m_scrollbar;
            captureMouse();
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

bool PianoRollPanel::onMouseMove(MouseMoveEvent& e) {
    if (!m_open || !m_clip) return false;
    float mx = e.x, my = e.y;

    // v2 button/toggle drag in progress — forward translated events to
    // the gesture SM. Handled BEFORE v1 capture because v2 widgets
    // don't participate in v1's capturedWidget() mechanism.
    if (m_v2Dragging) {
        auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, m_v2Dragging->bounds());
        m_v2Dragging->dispatchMouseMove(ev);
        return true;
    }

    if (Widget::capturedWidget() && Widget::capturedWidget() != this) {
        return Widget::capturedWidget()->onMouseMove(e);
    }

    if (m_handleDragActive) {
        float delta = m_handleDragStartY - my;
        m_userHeight = std::clamp(
            m_handleDragStartH + delta, kMinPanelH, kMaxPanelH);
        m_targetHeight = m_userHeight;
        return true;
    }

    if (m_pianoKeyDragging) {
        float delta = m_pianoKeyDragStartY - my;  // drag up = increase
        m_rowH = std::clamp(m_pianoKeyDragStartRowH + delta * 0.05f, kMinRowH, kMaxRowH);
        return true;
    }

    if (m_dragMode == Drag::LoopStart || m_dragMode == Drag::LoopEnd) {
        if (m_clip) {
            // Auto-scroll when dragging near edges
            float edgeZone = 30.0f;
            float scrollSpeed = 4.0f;
            if (mx > m_gx + m_gw - edgeZone) {
                m_scrollX += scrollSpeed;
                clampScroll();
            } else if (mx < m_gx + edgeZone && m_scrollX > 0) {
                m_scrollX = std::max(0.0f, m_scrollX - scrollSpeed);
            }
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

    if (m_dragMode == Drag::None) return false;
    if (m_velDragging && m_clip) {
        float velFrac = 1.0f - std::clamp((my - m_velY - 2.0f) / (kVelLaneH - 4.0f), 0.0f, 1.0f);
        uint16_t newVel = static_cast<uint16_t>(std::clamp(velFrac * 65535.0f, 1.0f, 65535.0f));
        if (m_velDragNoteIdx >= 0 && m_velDragNoteIdx < m_clip->noteCount()) {
            float oldFrac = m_velDragStartVel;
            if (m_selectedNotes.count(m_velDragNoteIdx) && m_selectedNotes.size() > 1 && oldFrac > 0.0f) {
                float ratio = velFrac / oldFrac;
                for (int idx : m_selectedNotes) {
                    if (idx >= 0 && idx < m_clip->noteCount()) {
                        auto& n = m_clip->note(idx);
                        float nf = (n.velocity / 65535.0f) * ratio;
                        n.velocity = static_cast<uint16_t>(std::clamp(nf * 65535.0f, 1.0f, 65535.0f));
                    }
                }
                m_velDragStartVel = velFrac;
            } else {
                m_clip->note(m_velDragNoteIdx).velocity = newVel;
            }
        }
        return true;
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

bool PianoRollPanel::onMouseUp(MouseEvent& e) {
    // v2 button/toggle release — flush the down/up pair to the fw2
    // gesture SM and release v1 capture. Must come BEFORE the v1
    // capturedWidget() path for the same reason as onMouseMove.
    if (m_v2Dragging) {
        auto ev = ::yawn::ui::fw2::toFw2Mouse(e, m_v2Dragging->bounds());
        m_v2Dragging->dispatchMouseUp(ev);
        m_v2Dragging = nullptr;
        releaseMouse();
        return true;
    }

    if (Widget::capturedWidget() && Widget::capturedWidget() != this) {
        return Widget::capturedWidget()->onMouseUp(e);
    }

    if (m_handleDragActive) {
        m_handleDragActive = false;
        releaseMouse();
        return true;
    }
    if (m_velDragging) {
        m_velDragging = false;
        m_velDragNoteIdx = -1;
        releaseMouse();
        return true;
    }
    if (m_pianoKeyDragging) {
        m_pianoKeyDragging = false;
        releaseMouse();
        return true;
    }
    if (m_dragMode == Drag::LoopStart || m_dragMode == Drag::LoopEnd) {
        m_dragMode = Drag::None;
        releaseMouse();
        return true;
    }
    if (m_dragMode == Drag::Move && !m_selectedNotes.empty() && m_clip) {
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

void PianoRollPanel::handleScroll(float dx, float dy, bool ctrl, bool shift,
                    float mouseX, float mouseY) {
    if (!m_open) return;

    if (ctrl && shift) {
        float oldRowH = m_rowH;
        m_rowH = std::clamp(m_rowH + dy * 1.0f, kMinRowH, kMaxRowH);
        if (mouseY >= m_gy && mouseY < m_gy + m_gh && oldRowH > 0 && m_rowH != oldRowH) {
            float contentY = mouseY - m_gy + m_scrollY;
            float newContentY = contentY * (m_rowH / oldRowH);
            m_scrollY = newContentY - (mouseY - m_gy);
        }
    } else if (ctrl) {
        float oldPxBeat = m_pxBeat;
        m_pxBeat = std::clamp(m_pxBeat * (1.0f + dy * 0.08f), kMinPxBeat, kMaxPxBeat);
        if (mouseX >= m_gx && mouseX < m_gx + m_gw && oldPxBeat > 0 && m_pxBeat != oldPxBeat) {
            float contentX = mouseX - m_gx + m_scrollX;
            float newContentX = contentX * (m_pxBeat / oldPxBeat);
            m_scrollX = newContentX - (mouseX - m_gx);
        }
    } else if (shift) {
        m_scrollX -= dy * 30.0f;
    } else {
        m_scrollY -= dy * 30.0f;
    }
    clampScroll();
}

bool PianoRollPanel::handleKeyDown(int key, bool ctrl) {
    if (!m_open) return false;

    constexpr int kEscape    = 0x1B;
    constexpr int kDelete    = 0x7F;
    constexpr int kBackspace = 0x08;
    constexpr int kLeft      = 0x40000050;
    constexpr int kRight     = 0x4000004F;
    constexpr int kUp        = 0x40000052;
    constexpr int kDown      = 0x40000051;

    if (key == kEscape) {
        if (!m_selectedNotes.empty()) { m_selectedNotes.clear(); return true; }
        close();
        return true;
    }
    if (key == kDelete || key == kBackspace) {
        if (!m_selectedNotes.empty() && m_clip) {
            std::vector<int> indices(m_selectedNotes.rbegin(), m_selectedNotes.rend());
            m_selectedNotes.clear();
            for (int idx : indices) {
                if (idx >= 0 && idx < m_clip->noteCount())
                    m_clip->removeNote(idx);
            }
        }
        return true;
    }

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

    // Snap shortcuts (Ctrl+number)
    if (ctrl && key == '1') { m_snap = Snap::Quarter;          return true; }
    if (ctrl && key == '2') { m_snap = Snap::Eighth;            return true; }
    if (ctrl && key == '3') { m_snap = Snap::Sixteenth;         return true; }
    if (ctrl && key == '4') { m_snap = Snap::ThirtySecond;      return true; }
    if (ctrl && key == '5') { m_snap = Snap::QuarterTriplet;    return true; }
    if (ctrl && key == '6') { m_snap = Snap::EighthTriplet;     return true; }
    if (ctrl && key == '7') { m_snap = Snap::SixteenthTriplet;  return true; }

    if (key == 'v' || key == 'V') { m_showVelocityLane = !m_showVelocityLane; return true; }

    return false;
}

void PianoRollPanel::finalizeRubberBand() {
    if (!m_clip) return;
    float rx0 = std::min(m_rubberStartX, m_rubberEndX);
    float rx1 = std::max(m_rubberStartX, m_rubberEndX);
    float ry0 = std::min(m_rubberStartY, m_rubberEndY);
    float ry1 = std::max(m_rubberStartY, m_rubberEndY);

    double beatMin = xToBeat(rx0);
    double beatMax = xToBeat(rx1);
    int pitchMin = yToPitch(ry1);
    int pitchMax = yToPitch(ry0);

    if (!m_rubberShiftHeld)
        m_selectedNotes.clear();

    for (int i = 0; i < m_clip->noteCount(); ++i) {
        const auto& n = m_clip->note(i);
        double noteEnd = n.startBeat + n.duration;
        int p = static_cast<int>(n.pitch);
        if (noteEnd > beatMin && n.startBeat < beatMax &&
            p >= pitchMin && p <= pitchMax) {
            m_selectedNotes.insert(i);
        }
    }
}

void PianoRollPanel::renderToolbar(UIContext& ctx) {
    auto& r = *ctx.renderer;
    float tbY = m_py + kHandleHeight;
    r.drawRect(m_px, tbY, m_pw, kToolbarH, Color{35, 35, 38});
    float x = m_px + 4;
    float btnH = kToolbarH - 4;
    float gap = 4.0f;
    float sectionGap = 12.0f;

    // v2 toggles/buttons — setState drives the accent-fill visual; no
    // per-paint colour juggling needed. Sync state from the panel's
    // bools each paint (cheap no-op when unchanged).
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

    // Tool radio group — exactly one toggle lit, corresponding to m_tool.
    for (int i = 0; i < 3; ++i) {
        m_toolBtns[i].setState(static_cast<int>(m_tool) == i);
        m_toolBtns[i].layout(Rect{x, tbY + 2, kToolBtnW, btnH}, v2ctx);
        m_toolBtns[i].render(v2ctx);
        x += kToolBtnW + gap;
    }

    x += sectionGap;
    m_snapLabel.layout(Rect{x, tbY + 2, 40, btnH}, ctx);
    m_snapLabel.paint(ctx);
    x += 44;

    // Snap radio group. Triplet buttons (indices 4..6) get an orange
    // accent to visually group them apart from the power-of-two snaps.
    static constexpr float snapW[] = {kSnapBtnW, kSnapBtnW, kSnapBtnW, kSnapBtnW,
                                       kTripletBtnW, kTripletBtnW, kTripletBtnW};
    for (int i = 0; i < 7; ++i) {
        m_snapBtns[i].setAccentColor(i >= 4 ? Color{255, 160, 60}
                                            : Color{100, 180, 255});
        m_snapBtns[i].setState(static_cast<int>(m_snap) == i);
        m_snapBtns[i].layout(Rect{x, tbY + 2, snapW[i], btnH}, v2ctx);
        m_snapBtns[i].render(v2ctx);
        x += snapW[i] + gap;
    }

    x += sectionGap;
    bool loopOn = m_clip && m_clip->loop();
    m_loopBtn.setState(loopOn);
    m_loopBtn.layout(Rect{x, tbY + 2, kLoopBtnW, btnH}, v2ctx);
    m_loopBtn.render(v2ctx);
    x += kLoopBtnW + gap;

    m_velBtn.setState(m_showVelocityLane);
    m_velBtn.layout(Rect{x, tbY + 2, kVelBtnW, btnH}, v2ctx);
    m_velBtn.render(v2ctx);
    x += kVelBtnW + gap;

    m_followBtn.setState(m_followPlayhead);
    m_followBtn.layout(Rect{x, tbY + 2, kFollowBtnW, btnH}, v2ctx);
    m_followBtn.render(v2ctx);
    x += kFollowBtnW + sectionGap;

    // Zoom buttons — plain v2 FwButton, default look.
    m_zoomOutBtn.layout(Rect{x, tbY + 2, kZoomBtnW, btnH}, v2ctx);
    m_zoomOutBtn.render(v2ctx);
    x += kZoomBtnW + 2;

    m_zoomInBtn.layout(Rect{x, tbY + 2, kZoomBtnW, btnH}, v2ctx);
    m_zoomInBtn.render(v2ctx);

    // Clip name (right-aligned)
    if (m_clip && !m_clip->name().empty()) {
        m_clipNameLabel.setText(m_clip->name());
        float nameW = ctx.font ? ctx.font->textWidth(m_clip->name(),
            Theme::kSmallFontSize / Theme::kFontSize * 0.6f) : 100.0f;
        m_clipNameLabel.layout(Rect{m_px + m_pw - nameW - 8, tbY + 2, nameW, btnH}, ctx);
        m_clipNameLabel.paint(ctx);
    }
}

void PianoRollPanel::renderPianoKeys(Renderer2D& r, Font& f) {
    for (int p = 127; p >= 0; --p) {
        float y = pitchToY(p);
        if (y + m_rowH < m_gy || y > m_gy + m_gh) continue;

        bool black = isBlack(p);
        Color bg = black ? Color{40, 40, 45} : Color{180, 180, 185};

        r.drawRect(m_pianoX, y, kPianoW, m_rowH, bg);
        r.drawRect(m_pianoX, y + m_rowH - 1, kPianoW, 1, Color{60, 60, 65});
    }
    r.drawRect(m_pianoX + kPianoW - 1, m_gy, 1, m_gh, Color{70, 70, 75});
}

void PianoRollPanel::renderPianoKeyLabels(Renderer2D& r, Font& f) {
    float sc = 12.0f / f.pixelHeight();
    float textH = 12.0f;
    for (int p = 127; p >= 0; --p) {
        float y = pitchToY(p);
        if (y + m_rowH < m_gy || y > m_gy + m_gh) continue;
        if (p % 12 == 0 && m_rowH >= 8.0f) {
            int oct = (p / 12) - 1;
            char buf[8];
            std::snprintf(buf, sizeof(buf), "C%d", oct);
            bool black = isBlack(p);
            Color fg = black ? Color{180, 180, 180} : Color{30, 30, 30};
            float textY = std::clamp(y + 1.0f, m_gy, m_gy + m_gh - textH);
            f.drawText(r, buf, m_pianoX + 2, textY, sc, fg);
        }
    }
}

void PianoRollPanel::renderGrid(Renderer2D& r) {
    for (int p = 127; p >= 0; --p) {
        float y = pitchToY(p);
        if (y + m_rowH < m_gy || y > m_gy + m_gh) continue;

        Color bg;
        if (p % 12 == 0) bg = Color{42, 42, 48};
        else if (isBlack(p)) bg = Color{30, 30, 34};
        else bg = Color{38, 38, 42};

        r.drawRect(m_gx, y, m_gw, m_rowH, bg);
        Color lineC = (p % 12 == 0) ? Color{55, 55, 62} : Color{45, 45, 50};
        r.drawRect(m_gx, y + m_rowH - 1, m_gw, 1, lineC);
    }

    double bpb = beatsPerBar();
    double sv = snapVal();
    double maxB = maxBeats();
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

void PianoRollPanel::renderNotes(Renderer2D& r) {
    if (!m_clip) return;
    Color trackCol = Theme::trackColors[m_trackIdx % Theme::kNumTrackColors];

    double visStart = xToBeat(m_gx);
    double visEnd   = xToBeat(m_gx + m_gw);

    for (int i = 0; i < m_clip->noteCount(); ++i) {
        const auto& n = m_clip->note(i);
        double noteEnd = n.startBeat + n.duration;
        if (noteEnd < visStart) continue;
        if (n.startBeat > visEnd) break;

        float x = beatToX(n.startBeat);
        float y = pitchToY(n.pitch);
        float w = static_cast<float>(n.duration * m_pxBeat);
        float h = m_rowH;

        if (y + h < m_gy || y > m_gy + m_gh) continue;

        float velFrac = n.velocity / 65535.0f;
        uint8_t alpha = static_cast<uint8_t>(140 + velFrac * 115);
        Color noteCol = trackCol.withAlpha(alpha);

        r.drawRect(x, y + 1, std::max(2.0f, w), h - 2, noteCol);

        if (m_selectedNotes.count(i)) {
            r.drawRectOutline(x, y + 1, std::max(2.0f, w), h - 2,
                              Color{255, 255, 255}, 1.5f);
        }
    }
}

void PianoRollPanel::renderClipBound(Renderer2D& r) {
    if (!m_clip) return;

    bool loopOn = m_clip->loop();
    float loopStartX = beatToX(m_clip->loopStartBeat());
    float loopEndX   = beatToX(m_clip->lengthBeats());

    if (loopOn && loopStartX > m_gx) {
        float preW = std::min(loopStartX, m_gx + m_gw) - m_gx;
        if (preW > 0)
            r.drawRect(m_gx, m_gy, preW, m_gh, Color{15, 15, 18, 140});
    }

    if (loopEndX >= m_gx && loopEndX <= m_gx + m_gw) {
        Color boundCol = loopOn ? Color{80, 220, 100, 180}
                                : Color{255, 100, 50, 120};
        r.drawRect(loopEndX, m_gy, 2, m_gh, boundCol);

        float pastW = m_gx + m_gw - loopEndX - 2;
        if (pastW > 0)
            r.drawRect(loopEndX + 2, m_gy, pastW, m_gh, Color{15, 15, 18, 140});
    }

    if (loopStartX >= m_gx && loopStartX <= m_gx + m_gw) {
        Color markerCol = loopOn ? Color{80, 220, 100, 180}
                                 : Color{255, 160, 50, 120};
        r.drawRect(loopStartX, m_gy, 2, m_gh, markerCol);
    }

    if (loopOn) {
        float barH = 4.0f;
        float x0 = std::max(m_gx, loopStartX);
        float x1 = std::min(m_gx + m_gw, loopEndX);
        if (x1 > x0) {
            r.drawRect(x0, m_gy, x1 - x0, barH, Color{80, 220, 100, 120});
        }
    }
}

void PianoRollPanel::renderRubberBand(Renderer2D& r) {
    if (m_dragMode != Drag::RubberBand) return;
    float rx = std::min(m_rubberStartX, m_rubberEndX);
    float ry = std::min(m_rubberStartY, m_rubberEndY);
    float rw = std::abs(m_rubberEndX - m_rubberStartX);
    float rh = std::abs(m_rubberEndY - m_rubberStartY);
    r.drawRect(rx, ry, rw, rh, Color{100, 180, 255, 30});
    r.drawRectOutline(rx, ry, rw, rh, Color{100, 180, 255, 180}, 1.0f);
}

void PianoRollPanel::renderPlayhead(Renderer2D& r) {
    if (!m_midiPlaying || !m_clip) return;
    double clipLen = m_clip->lengthBeats();
    if (clipLen <= 0) return;
    double beat = std::fmod(m_playBeat, clipLen);
    if (beat < 0) beat += clipLen;
    float px = beatToX(beat);
    if (px < m_gx || px > m_gx + m_gw) return;
    r.drawRect(px, m_gy, 2.0f, m_gh, Color{255, 255, 255, 220});
}

void PianoRollPanel::renderRuler(Renderer2D& r, Font& f) {
    float rulerY = m_py + kHandleHeight + kToolbarH;
    float sc = 11.0f / f.pixelHeight();

    r.drawRect(m_gx, rulerY, m_gw, kRulerH, Color{38, 38, 42});
    r.drawRect(m_px, rulerY, kClipOpsW + kPianoW, kRulerH, Color{38, 38, 42});

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

    bool loopOn = m_clip->loop();
    double loopStart = m_clip->loopStartBeat();
    double loopEnd   = m_clip->lengthBeats();
    float lsx = std::max(m_gx, beatToX(loopStart));
    float lex = std::min(m_gx + m_gw, beatToX(loopEnd));
    if (lex > lsx) {
        Color regionCol = loopOn ? Color{80, 220, 100, 50} : Color{255, 140, 60, 40};
        r.drawRect(lsx, rulerY, lex - lsx, kRulerH, regionCol);
    }

    {
        float hx = beatToX(loopStart);
        if (hx >= m_gx - 6 && hx <= m_gx + m_gw + 6) {
            Color hc = loopOn ? Color{0, 255, 120} : Color{255, 160, 50};
            r.drawRect(hx - 1, rulerY, 3, kRulerH, hc);
            r.drawRect(hx, rulerY, 6, 6, hc);
        }
    }
    {
        float hx = beatToX(loopEnd);
        if (hx >= m_gx - 6 && hx <= m_gx + m_gw + 6) {
            Color hc = loopOn ? Color{0, 255, 120} : Color{255, 160, 50};
            r.drawRect(hx - 1, rulerY, 3, kRulerH, hc);
            r.drawRect(hx - 5, rulerY, 6, 6, hc);
        }
    }

    r.drawRect(m_gx, rulerY + kRulerH - 1, m_gw, 1, Color{55, 55, 60});

    // Playhead on ruler
    if (m_midiPlaying && m_clip) {
        double clipLen = m_clip->lengthBeats();
        if (clipLen > 0) {
            double beat = std::fmod(m_playBeat, clipLen);
            if (beat < 0) beat += clipLen;
            float px = beatToX(beat);
            if (px >= m_gx && px <= m_gx + m_gw) {
                r.drawRect(px - 1, rulerY, 2, kRulerH, Color{255, 255, 255, 220});
                // Small triangle marker
                r.drawTriangle(px - 4, rulerY, px + 4, rulerY,
                               px, rulerY + 5, Color{255, 255, 255, 220});
            }
        }
    }
}

void PianoRollPanel::renderClipOps(Renderer2D& r, Font& f) {
    float sc = 11.0f / f.pixelHeight();
    float x = m_px;
    float y = m_gy;
    float w = kClipOpsW;

    r.drawRect(x, y, w, m_gh, Color{32, 32, 36});

    static const char* labels[] = {"Dup", "x2", "/2", "Rev", "Clr", "1.1.1"};
    static const Color btnColors[] = {
        Color{80, 180, 255},
        Color{120, 230, 100},
        Color{255, 180, 60},
        Color{200, 130, 255},
        Color{255, 90, 90},
        Color{0, 220, 160}
    };
    float btnH = 20.0f;
    float gap = 4.0f;
    float startY = y + 6.0f;

    for (int i = 0; i < 6; ++i) {
        float by = startY + i * (btnH + gap);
        if (by + btnH > y + m_gh) break;
        m_clipOpsBtnY[i] = by;
        r.drawRect(x + 3, by, w - 6, btnH, Color{50, 50, 55});
        r.drawRect(x + 3, by, 3, btnH, btnColors[i]);
        f.drawText(r, labels[i], x + 10, by + 3, sc, Theme::textPrimary);
    }

    r.drawRect(x + w - 1, y, 1, m_gh, Color{55, 55, 60});
}

void PianoRollPanel::renderVelocityLane(Renderer2D& r, Font& f) {
    if (!m_clip) return;
    Color trackCol = Theme::trackColors[m_trackIdx % Theme::kNumTrackColors];
    float sc = 10.0f / f.pixelHeight();

    r.drawRect(m_gx, m_velY, m_gw, kVelLaneH, Color{28, 28, 32});
    r.drawRect(m_px, m_velY, kClipOpsW + kPianoW, kVelLaneH, Color{32, 32, 36});
    f.drawText(r, "Vel", m_px + 6, m_velY + kVelLaneH * 0.5f - 5, sc, Theme::textSecondary);
    r.drawRect(m_px + kClipOpsW + kPianoW - 1, m_velY, 1, kVelLaneH, Color{55, 55, 60});
    r.drawRect(m_gx, m_velY, m_gw, 1, Color{50, 50, 55});

    r.pushClip(m_gx, m_velY, m_gw, kVelLaneH);

    float ref100Y = m_velY + 2;
    float ref50Y  = m_velY + 2 + (kVelLaneH - 4) * 0.5f;
    r.drawRect(m_gx, ref100Y, m_gw, 1, Color{50, 50, 55});
    r.drawRect(m_gx, ref50Y, m_gw, 1, Color{40, 40, 45});

    double visStart = xToBeat(m_gx);
    double visEnd   = xToBeat(m_gx + m_gw);
    for (int i = 0; i < m_clip->noteCount(); ++i) {
        const auto& n = m_clip->note(i);
        double noteEnd = n.startBeat + n.duration;
        if (noteEnd < visStart) continue;
        if (n.startBeat > visEnd) break;

        float nx = beatToX(n.startBeat);
        float nw = std::max(3.0f, static_cast<float>(n.duration * m_pxBeat));
        if (nx + nw < m_gx || nx > m_gx + m_gw) continue;

        float velFrac = n.velocity / 65535.0f;
        float barH = velFrac * (kVelLaneH - 4);
        float barY = m_velY + kVelLaneH - 2 - barH;

        uint8_t br = static_cast<uint8_t>(60 + velFrac * 195);
        Color barCol{
            static_cast<uint8_t>(std::min(255, trackCol.r * br / 180)),
            static_cast<uint8_t>(std::min(255, trackCol.g * br / 180)),
            static_cast<uint8_t>(std::min(255, trackCol.b * br / 180))
        };
        r.drawRect(nx, barY, nw, barH, barCol);

        if (m_selectedNotes.count(i)) {
            r.drawRectOutline(nx, barY, nw, barH, Color{255, 255, 255}, 1.0f);
        }
    }

    r.popClip();
}

void PianoRollPanel::renderScrollbar(UIContext& ctx) {
    auto& r = *ctx.renderer;
    float velH = m_showVelocityLane ? kVelLaneH : 0.0f;
    float sbY = m_gy + m_gh + velH;
    r.drawRect(m_px, sbY, kClipOpsW + kPianoW, kScrollbarH, Color{40, 40, 45});

    float contentW = maxBeats() * m_pxBeat;
    m_scrollbar.setContentSize(contentW);
    m_scrollbar.setScrollPos(m_scrollX);
    {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_scrollbar.layout(Rect{m_gx, sbY, m_gw, kScrollbarH}, v2ctx);
        m_scrollbar.render(v2ctx);
    }
}

} // namespace fw
} // namespace ui
} // namespace yawn
