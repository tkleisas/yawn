#include "ui/MixerView.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace yawn {
namespace ui {

void MixerView::init(Project* project, audio::AudioEngine* engine) {
    m_project = project;
    m_engine = engine;
}

void MixerView::updateMeter(int trackIndex, float peakL, float peakR) {
    if (trackIndex >= 0 && trackIndex < kMaxTracks) {
        m_trackMeters[trackIndex] = {peakL, peakR};
    } else if (trackIndex == -1) {
        m_masterMeter = {peakL, peakR};
    } else if (trackIndex >= -5 && trackIndex <= -2) {
        int busIdx = -(trackIndex + 2);
        m_returnMeters[busIdx] = {peakL, peakR};
    }
}

void MixerView::drawText(Renderer2D& renderer, Font& font,
                          const char* text, float x, float y, float scale, Color color) {
    if (!font.isLoaded()) return;
    float tx = x;
    for (const char* p = text; *p; ++p) {
        auto g = font.getGlyph(*p, tx, y, scale);
        renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   color, font.textureId());
        tx += g.xAdvance;
    }
}

void MixerView::render(Renderer2D& renderer, Font& font,
                        float x, float y, float width, float height) {
    m_viewX = x; m_viewY = y; m_viewW = width; m_viewH = height;

    // Background
    renderer.drawRect(x, y, width, height, Theme::panelBg);
    renderer.drawRect(x, y, width, 1, Theme::clipSlotBorder);

    if (!m_project || !m_engine) return;

    // Align with session view: scene label area on left, then track columns
    float stripY = y + 2;
    float stripH = height - 4;

    // Scene label gutter (empty or label "MIXER")
    float pixH = font.pixelHeight();
    if (pixH < 1.0f) pixH = 1.0f;
    float scale = Theme::kSmallFontSize / pixH;
    drawText(renderer, font, "MIX", x + 6, y + stripH * 0.5f - 8, scale * 0.85f, Theme::textDim);

    // Track channel strips — aligned to session grid columns
    float gridX = x + Theme::kSceneLabelWidth;
    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = gridX + t * Theme::kTrackWidth;
        renderChannelStrip(renderer, font, t, sx, stripY, Theme::kTrackWidth, stripH);
    }

    // Separator after track strips
    float afterTracks = gridX + m_project->numTracks() * Theme::kTrackWidth;
    renderer.drawRect(afterTracks, y + 4, kSeparatorWidth, height - 8, Theme::clipSlotBorder);
    float retX = afterTracks + kSeparatorWidth + 4;

    // Return bus strips (narrower than track strips)
    float retStripW = 70.0f;
    for (int r = 0; r < kMaxReturnBuses; ++r) {
        renderReturnStrip(renderer, font, r, retX, stripY, retStripW, stripH);
        retX += retStripW + kStripPadding;
    }

    // Separator before master
    renderer.drawRect(retX, y + 4, kSeparatorWidth, height - 8, Theme::clipSlotBorder);
    retX += kSeparatorWidth + 4;

    // Master strip
    renderMasterStrip(renderer, font, retX, stripY, retStripW, stripH);
}

void MixerView::renderChannelStrip(Renderer2D& renderer, Font& font,
                                    int trackIndex, float x, float y, float w, float h) {
    float pad = Theme::kSlotPadding;
    float ix = x + pad;
    float iw = w - pad * 2;
    float pixH = font.pixelHeight();
    if (pixH < 1.0f) pixH = 1.0f;
    float scale = Theme::kSmallFontSize / pixH;
    float smallScale = scale * 0.8f;

    // Strip background
    renderer.drawRect(ix, y, iw, h, Theme::background);

    // Selected track highlight
    if (trackIndex == m_selectedTrack) {
        renderer.drawRect(ix, y, iw, h, Color{50, 55, 65, 255});
    }

    Color trackCol = Theme::trackColors[m_project->track(trackIndex).colorIndex % Theme::kNumTrackColors];
    const auto& ch = m_engine->mixer().trackChannel(trackIndex);

    // Row 1: Track color bar (3px)
    renderer.drawRect(ix, y, iw, 3, trackCol);

    // Row 2: Track number
    float curY = y + 5;
    char name[16];
    std::snprintf(name, sizeof(name), "%d", trackIndex + 1);
    drawText(renderer, font, name, ix + 4, curY, scale, Theme::textPrimary);

    // Row 3: Mute / Solo buttons
    curY = y + 30;
    Color muteCol = ch.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty;
    Color soloCol = ch.soloed ? Color{255, 200, 50} : Theme::clipSlotEmpty;
    float btnW = (iw - 12) * 0.5f;
    if (btnW > kButtonWidth) btnW = kButtonWidth;
    renderButton(renderer, font, ix + 4, curY, btnW, kButtonHeight, "M", muteCol,
                 ch.muted ? Color{0, 0, 0} : Theme::textSecondary);
    renderButton(renderer, font, ix + 4 + btnW + 2, curY, btnW, kButtonHeight, "S", soloCol,
                 ch.soloed ? Color{0, 0, 0} : Theme::textSecondary);

    // Row 4: Pan bar (full width)
    curY += kButtonHeight + 6;
    float panW = iw - 8;
    float panH = 16;
    renderer.drawRect(ix + 4, curY, panW, panH, Theme::clipSlotEmpty);
    float panCenter = ix + 4 + panW * 0.5f;
    float panPos = panCenter + ch.pan * (panW * 0.5f - 4);
    renderer.drawRect(panPos - 4, curY, 8, panH, trackCol);

    // Row 5: Send indicators (compact row)
    curY += panH + 4;
    int maxDotsVisible = std::min(kMaxReturnBuses, static_cast<int>((panW + 2) / 10));
    for (int s = 0; s < maxDotsVisible; ++s) {
        const auto& send = ch.sends[s];
        float dotX = ix + 4 + s * 10;
        Color dotCol = send.enabled && send.level > 0.01f
            ? Color{100, 180, 255}.withAlpha(static_cast<uint8_t>(100 + send.level * 155))
            : Theme::clipSlotEmpty;
        renderer.drawRect(dotX, curY, 7, 7, dotCol);
    }

    // Row 6: Fader + Meter (remaining vertical space)
    curY += 12;
    float faderBottom = y + h - 22;
    float faderH = faderBottom - curY;
    if (faderH < 20) faderH = 20;

    // Fader on left, meter pair on right
    float faderX = ix + 4;
    renderFader(renderer, faderX, curY, kFaderWidth, faderH, ch.volume, trackCol);

    float meterX = faderX + kFaderWidth + 3;
    renderMeter(renderer, meterX, curY, kMeterWidth * 2, faderH,
                m_trackMeters[trackIndex].peakL, m_trackMeters[trackIndex].peakR);

    // Row 7: dB label at bottom
    float db = ch.volume > 0.001f ? 20.0f * std::log10(ch.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    drawText(renderer, font, dbText, ix + 4, y + h - 18, smallScale, Theme::textDim);

    // Column border
    renderer.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
}

void MixerView::renderReturnStrip(Renderer2D& renderer, Font& font,
                                   int busIndex, float x, float y, float w, float h) {
    renderer.drawRect(x, y, w, h, Theme::background);

    Color busCol{100, 180, 255};
    float pixH = font.pixelHeight();
    if (pixH < 1.0f) pixH = 1.0f;
    float scale = Theme::kSmallFontSize / pixH;
    float smallScale = scale * 0.8f;

    const auto& rb = m_engine->mixer().returnBus(busIndex);

    // Color bar
    renderer.drawRect(x, y, w, 3, busCol);

    // Name
    const char* names[] = {"Ret A", "Ret B", "Ret C", "Ret D", "Ret E", "Ret F", "Ret G", "Ret H"};
    drawText(renderer, font, names[busIndex], x + 4, y + 5, scale, Theme::textPrimary);

    // Mute button
    float curY = y + 26;
    Color muteCol = rb.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty;
    renderButton(renderer, font, x + 4, curY, kButtonWidth, kButtonHeight, "M", muteCol,
                 rb.muted ? Color{0, 0, 0} : Theme::textSecondary);

    // Pan bar
    curY += kButtonHeight + 6;
    float panW = w - 8;
    float panH = 16;
    renderer.drawRect(x + 4, curY, panW, panH, Theme::clipSlotEmpty);
    float panCenter = x + 4 + panW * 0.5f;
    float panPos = panCenter + rb.pan * (panW * 0.5f - 4);
    renderer.drawRect(panPos - 4, curY, 8, panH, busCol);

    // Fader + Meter
    curY += panH + 8;
    float faderBottom = y + h - 22;
    float faderH = faderBottom - curY;
    if (faderH < 20) faderH = 20;

    renderFader(renderer, x + 4, curY, kFaderWidth, faderH, rb.volume, busCol);
    renderMeter(renderer, x + 4 + kFaderWidth + 3, curY, kMeterWidth * 2, faderH,
                m_returnMeters[busIndex].peakL, m_returnMeters[busIndex].peakR);

    // dB label
    float db = rb.volume > 0.001f ? 20.0f * std::log10(rb.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    drawText(renderer, font, dbText, x + 4, y + h - 18, smallScale, Theme::textDim);
}

void MixerView::renderMasterStrip(Renderer2D& renderer, Font& font,
                                   float x, float y, float w, float h) {
    renderer.drawRect(x, y, w, h, Color{35, 35, 40});

    Color masterCol = Theme::transportAccent;
    float pixH = font.pixelHeight();
    if (pixH < 1.0f) pixH = 1.0f;
    float scale = Theme::kSmallFontSize / pixH;
    float smallScale = scale * 0.8f;

    const auto& master = m_engine->mixer().master();

    // Color bar
    renderer.drawRect(x, y, w, 3, masterCol);

    // Label
    drawText(renderer, font, "MASTER", x + 4, y + 5, scale, Theme::textPrimary);

    // Fader + Meter
    float curY = y + 30;
    float faderBottom = y + h - 22;
    float faderH = faderBottom - curY;
    if (faderH < 20) faderH = 20;

    renderFader(renderer, x + 4, curY, kFaderWidth + 2, faderH, master.volume, masterCol);
    renderMeter(renderer, x + kFaderWidth + 10, curY, kMeterWidth * 2 + 2, faderH,
                m_masterMeter.peakL, m_masterMeter.peakR);

    // dB label
    float db = master.volume > 0.001f ? 20.0f * std::log10(master.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    drawText(renderer, font, dbText, x + 4, y + h - 18, smallScale, Theme::textDim);
}

void MixerView::renderMeter(Renderer2D& renderer,
                             float x, float y, float w, float h,
                             float peakL, float peakR) {
    float halfW = w * 0.5f - 1;

    // Meter background
    renderer.drawRect(x, y, halfW, h, Color{20, 20, 22});
    renderer.drawRect(x + halfW + 2, y, halfW, h, Color{20, 20, 22});

    // Convert to dB and map to 0..1 range (-60dB to 0dB)
    auto dbToHeight = [](float peak) -> float {
        if (peak < 0.001f) return 0.0f;
        float db = 20.0f * std::log10(peak);
        return std::max(0.0f, std::min(1.0f, (db + 60.0f) / 60.0f));
    };

    float hL = dbToHeight(peakL) * h;
    float hR = dbToHeight(peakR) * h;

    // Green/yellow/red gradient based on level
    auto meterColor = [](float peak) -> Color {
        if (peak > 1.0f) return {255, 40, 40};     // clipping
        if (peak > 0.7f) return {255, 200, 50};     // hot
        return {80, 220, 80};                         // normal
    };

    renderer.drawRect(x, y + h - hL, halfW, hL, meterColor(peakL));
    renderer.drawRect(x + halfW + 2, y + h - hR, halfW, hR, meterColor(peakR));
}

void MixerView::renderFader(Renderer2D& renderer,
                             float x, float y, float w, float h,
                             float value, Color trackColor) {
    // Fader track
    float trackX = x + w * 0.5f - 1;
    renderer.drawRect(trackX, y, 2, h, Theme::clipSlotBorder);

    // Fader position (0.0 = bottom, 2.0 = top, 1.0 = unity)
    float frac = std::min(value / 2.0f, 1.0f);
    float knobH = 32;
    float knobY = y + h - frac * h - knobH * 0.5f;

    renderer.drawRect(x, knobY, w, knobH, trackColor);
    renderer.drawRect(x + 1, knobY + 2, w - 2, knobH - 4, Color{200, 200, 200});

    // Unity mark at 0.5 of the range (volume = 1.0)
    float unityY = y + h - 0.5f * h;
    renderer.drawRect(x - 2, unityY, w + 4, 1, Theme::textDim);
}

void MixerView::renderButton(Renderer2D& renderer, Font& font,
                              float x, float y, float w, float h,
                              const char* label, Color bgColor, Color textColor) {
    renderer.drawRect(x, y, w, h, bgColor);
    renderer.drawRectOutline(x, y, w, h, Theme::clipSlotBorder);

    float scale = Theme::kSmallFontSize / font.pixelHeight() * 0.85f;
    float textW = font.textWidth(label, scale);
    drawText(renderer, font, label, x + (w - textW) * 0.5f, y + 1, scale, textColor);
}

bool MixerView::handleClick(float mx, float my, bool isRightClick) {
    if (!m_project || !m_engine) return false;
    if (mx < m_viewX || mx > m_viewX + m_viewW) return false;
    if (my < m_viewY || my > m_viewY + m_viewH) return false;

    float gridX = m_viewX + Theme::kSceneLabelWidth;

    // --- Track channel strips ---
    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = gridX + t * Theme::kTrackWidth;
        if (mx < sx || mx >= sx + Theme::kTrackWidth) continue;

        float ix = sx + Theme::kSlotPadding;
        float iw = Theme::kTrackWidth - Theme::kSlotPadding * 2;

        // Mute / Solo buttons: y = m_viewY + 2 + 30
        float btnY = m_viewY + 2 + 30;
        float btnW = (iw - 12) * 0.5f;
        if (btnW > kButtonWidth) btnW = kButtonWidth;

        if (my >= btnY && my < btnY + kButtonHeight) {
            if (mx >= ix + 4 && mx < ix + 4 + btnW) {
                bool cur = m_engine->mixer().trackChannel(t).muted;
                m_engine->sendCommand(audio::SetTrackMuteMsg{t, !cur});
                return true;
            }
            if (mx >= ix + 4 + btnW + 2 && mx < ix + 4 + 2 * btnW + 2) {
                bool cur = m_engine->mixer().trackChannel(t).soloed;
                m_engine->sendCommand(audio::SetTrackSoloMsg{t, !cur});
                return true;
            }
        }

        // Pan bar: y = m_viewY + 60, h = 8
        float panY = m_viewY + 60;
        float panW = iw - 8;
        if (my >= panY && my < panY + 8 && mx >= ix + 4 && mx < ix + 4 + panW) {
            if (isRightClick) {
                m_engine->sendCommand(audio::SetTrackPanMsg{t, 0.0f});
                return true;
            }
            m_dragging = true;
            m_dragType = DragType::Pan;
            m_dragTarget = t;
            m_dragStartY = mx;
            m_dragStartValue = m_engine->mixer().trackChannel(t).pan;
            return true;
        }

        // Fader area: y = m_viewY + 84, extends to m_viewY + m_viewH - 2 - 22
        float faderY = m_viewY + 84;
        float faderBottom = m_viewY + m_viewH - 2 - 22;
        if (my >= faderY && my < faderBottom && mx >= ix + 4 && mx < ix + 4 + kFaderWidth) {
            if (isRightClick) {
                m_engine->sendCommand(audio::SetTrackVolumeMsg{t, 1.0f});
                return true;
            }
            m_dragging = true;
            m_dragType = DragType::Fader;
            m_dragTarget = t;
            m_dragStartY = my;
            m_dragStartValue = m_engine->mixer().trackChannel(t).volume;
            return true;
        }
    }

    // --- Return bus strips ---
    float afterTracks = gridX + m_project->numTracks() * Theme::kTrackWidth;
    float retX = afterTracks + kSeparatorWidth + 4;
    float retStripW = 70.0f;

    for (int r = 0; r < kMaxReturnBuses; ++r) {
        float rx = retX + r * (retStripW + kStripPadding);
        if (mx < rx || mx >= rx + retStripW) continue;

        const auto& rb = m_engine->mixer().returnBus(r);

        // Mute button: y = m_viewY + 2 + 26
        float muteY = m_viewY + 2 + 26;
        if (my >= muteY && my < muteY + kButtonHeight && mx >= rx + 4 && mx < rx + 4 + kButtonWidth) {
            m_engine->sendCommand(audio::SetReturnMuteMsg{r, !rb.muted});
            return true;
        }

        // Pan bar: y = m_viewY + 56, h = 8
        float panY = m_viewY + 56;
        float panW = retStripW - 8;
        if (my >= panY && my < panY + 8 && mx >= rx + 4 && mx < rx + 4 + panW) {
            if (isRightClick) {
                m_engine->sendCommand(audio::SetReturnPanMsg{r, 0.0f});
                return true;
            }
            m_dragging = true;
            m_dragType = DragType::Pan;
            m_dragTarget = kDragReturn0 + r;
            m_dragStartY = mx;
            m_dragStartValue = rb.pan;
            return true;
        }

        // Fader: y = m_viewY + 74
        float faderY = m_viewY + 74;
        float faderBottom = m_viewY + m_viewH - 2 - 22;
        if (my >= faderY && my < faderBottom && mx >= rx + 4 && mx < rx + 4 + kFaderWidth) {
            if (isRightClick) {
                m_engine->sendCommand(audio::SetReturnVolumeMsg{r, 1.0f});
                return true;
            }
            m_dragging = true;
            m_dragType = DragType::Fader;
            m_dragTarget = kDragReturn0 + r;
            m_dragStartY = my;
            m_dragStartValue = rb.volume;
            return true;
        }
    }

    // --- Master strip ---
    float masterX = retX + kMaxReturnBuses * (retStripW + kStripPadding) + kSeparatorWidth + 4;
    float masterFaderY = m_viewY + 2 + 30;
    float masterFaderBottom = m_viewY + m_viewH - 2 - 22;

    if (mx >= masterX + 4 && mx < masterX + 4 + kFaderWidth + 2 &&
        my >= masterFaderY && my < masterFaderBottom) {
        if (isRightClick) {
            m_engine->sendCommand(audio::SetMasterVolumeMsg{1.0f});
            return true;
        }
        m_dragging = true;
        m_dragType = DragType::Fader;
        m_dragTarget = kDragMaster;
        m_dragStartY = my;
        m_dragStartValue = m_engine->mixer().master().volume;
        return true;
    }

    return false;
}

bool MixerView::handleDrag(float mx, float my) {
    if (!m_dragging || !m_engine) return false;

    if (m_dragType == DragType::Fader) {
        float faderH = m_viewH - 4 - 84 - 22;
        if (faderH < 20) faderH = 20;
        float deltaY = m_dragStartY - my;  // up = increase volume
        float deltaVol = (deltaY / faderH) * 2.0f;
        float newVol = std::clamp(m_dragStartValue + deltaVol, 0.0f, 2.0f);

        if (m_dragTarget >= 0) {
            m_engine->sendCommand(audio::SetTrackVolumeMsg{m_dragTarget, newVol});
        } else if (m_dragTarget == kDragMaster) {
            m_engine->sendCommand(audio::SetMasterVolumeMsg{newVol});
        } else if (m_dragTarget <= kDragReturn0) {
            int bus = m_dragTarget - kDragReturn0;
            m_engine->sendCommand(audio::SetReturnVolumeMsg{bus, newVol});
        }
        return true;
    }

    if (m_dragType == DragType::Pan) {
        float panW = 100.0f;
        float deltaX = mx - m_dragStartY;  // m_dragStartY stores start X for pan
        float deltaPan = (deltaX / panW) * 2.0f;
        float newPan = std::clamp(m_dragStartValue + deltaPan, -1.0f, 1.0f);

        if (m_dragTarget >= 0) {
            m_engine->sendCommand(audio::SetTrackPanMsg{m_dragTarget, newPan});
        } else if (m_dragTarget <= kDragReturn0) {
            int bus = m_dragTarget - kDragReturn0;
            m_engine->sendCommand(audio::SetReturnPanMsg{bus, newPan});
        }
        return true;
    }

    return false;
}

void MixerView::handleRelease() {
    m_dragging = false;
    m_dragType = DragType::None;
    m_dragTarget = -1;
}

} // namespace ui
} // namespace yawn
