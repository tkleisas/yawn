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
    if (trackIndex >= 0 && trackIndex < audio::kMixerMaxTracks) {
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

    if (!m_project) return;

    float stripX = x + 2;
    float stripY = y + 2;
    float stripH = height - 4;

    // Track channel strips
    for (int t = 0; t < m_project->numTracks(); ++t) {
        renderChannelStrip(renderer, font, t, stripX, stripY, kStripWidth, stripH);
        stripX += kStripWidth + kStripPadding;
    }

    // Separator before returns
    renderer.drawRect(stripX, y + 4, kSeparatorWidth, height - 8, Theme::clipSlotBorder);
    stripX += kSeparatorWidth + 4;

    // Return bus strips
    const char* returnNames[] = {"A", "B", "C", "D"};
    for (int r = 0; r < audio::kMaxReturnBuses; ++r) {
        renderReturnStrip(renderer, font, r, stripX, stripY, kStripWidth, stripH);
        stripX += kStripWidth + kStripPadding;
    }

    // Separator before master
    renderer.drawRect(stripX, y + 4, kSeparatorWidth, height - 8, Theme::clipSlotBorder);
    stripX += kSeparatorWidth + 4;

    // Master strip
    renderMasterStrip(renderer, font, stripX, stripY, kStripWidth + 20, stripH);
}

void MixerView::renderChannelStrip(Renderer2D& renderer, Font& font,
                                    int trackIndex, float x, float y, float w, float h) {
    // Strip background
    renderer.drawRect(x, y, w, h, Theme::background);

    Color trackCol = Theme::trackColors[m_project->track(trackIndex).colorIndex % Theme::kNumTrackColors];
    float scale = Theme::kSmallFontSize / font.pixelHeight();

    // Track color bar at top
    renderer.drawRect(x, y, w, 3, trackCol);

    // Track name
    float nameY = y + 5;
    char name[16];
    std::snprintf(name, sizeof(name), "%d", trackIndex + 1);
    drawText(renderer, font, name, x + 4, nameY, scale, Theme::textPrimary);

    // Mute / Solo buttons
    float btnY = y + 24;
    const auto& ch = m_engine->mixer().trackChannel(trackIndex);

    Color muteCol = ch.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty;
    Color soloCol = ch.soloed ? Color{255, 200, 50} : Theme::clipSlotEmpty;
    renderButton(renderer, font, x + 4, btnY, kButtonWidth, kButtonHeight, "M", muteCol,
                 ch.muted ? Color{0, 0, 0} : Theme::textSecondary);
    renderButton(renderer, font, x + 4 + kButtonWidth + 2, btnY, kButtonWidth, kButtonHeight, "S", soloCol,
                 ch.soloed ? Color{0, 0, 0} : Theme::textSecondary);

    // Fader
    float faderX = x + 4;
    float faderY = btnY + kButtonHeight + 6;
    float faderH = h - (faderY - y) - 4;
    renderFader(renderer, faderX, faderY, kFaderWidth, faderH, ch.volume, trackCol);

    // Meter
    float meterX = faderX + kFaderWidth + 4;
    renderMeter(renderer, meterX, faderY, kMeterWidth, faderH,
                m_trackMeters[trackIndex].peakL, m_trackMeters[trackIndex].peakR);

    // Pan knob (simplified as a horizontal bar)
    float panX = meterX + kMeterWidth + 8;
    float panW = w - (panX - x) - 4;
    float panY = faderY;
    float panH = 8;
    renderer.drawRect(panX, panY, panW, panH, Theme::clipSlotEmpty);
    float panCenter = panX + panW * 0.5f;
    float panPos = panCenter + ch.pan * (panW * 0.5f - 2);
    renderer.drawRect(panPos - 2, panY, 4, panH, trackCol);

    // Send level indicators (small dots)
    float sendY = panY + panH + 6;
    for (int s = 0; s < audio::kMaxReturnBuses; ++s) {
        const auto& send = ch.sends[s];
        float dotX = panX + s * 12;
        Color dotCol = send.enabled && send.level > 0.01f
            ? Color{100, 180, 255}.withAlpha(static_cast<uint8_t>(100 + send.level * 155))
            : Theme::clipSlotEmpty;
        renderer.drawRect(dotX, sendY, 8, 8, dotCol);
    }

    // dB label
    float db = ch.volume > 0.001f ? 20.0f * std::log10(ch.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    float labelScale = scale * 0.85f;
    float labelY = y + h - 18;
    drawText(renderer, font, dbText, x + 4, labelY, labelScale, Theme::textDim);
}

void MixerView::renderReturnStrip(Renderer2D& renderer, Font& font,
                                   int busIndex, float x, float y, float w, float h) {
    renderer.drawRect(x, y, w, h, Theme::background);

    Color busCol{100, 180, 255};
    float scale = Theme::kSmallFontSize / font.pixelHeight();

    renderer.drawRect(x, y, w, 3, busCol);

    const char* names[] = {"Ret A", "Ret B", "Ret C", "Ret D"};
    drawText(renderer, font, names[busIndex], x + 4, y + 5, scale, Theme::textPrimary);

    const auto& rb = m_engine->mixer().returnBus(busIndex);

    // Mute button
    float btnY = y + 24;
    Color muteCol = rb.muted ? Color{255, 80, 80} : Theme::clipSlotEmpty;
    renderButton(renderer, font, x + 4, btnY, kButtonWidth, kButtonHeight, "M", muteCol,
                 rb.muted ? Color{0, 0, 0} : Theme::textSecondary);

    // Fader
    float faderY = btnY + kButtonHeight + 6;
    float faderH = h - (faderY - y) - 4;
    renderFader(renderer, x + 4, faderY, kFaderWidth, faderH, rb.volume, busCol);

    // Meter
    renderMeter(renderer, x + 4 + kFaderWidth + 4, faderY, kMeterWidth, faderH,
                m_returnMeters[busIndex].peakL, m_returnMeters[busIndex].peakR);

    // dB label
    float db = rb.volume > 0.001f ? 20.0f * std::log10(rb.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    drawText(renderer, font, dbText, x + 4, y + h - 18, scale * 0.85f, Theme::textDim);
}

void MixerView::renderMasterStrip(Renderer2D& renderer, Font& font,
                                   float x, float y, float w, float h) {
    renderer.drawRect(x, y, w, h, Color{35, 35, 40});

    Color masterCol = Theme::transportAccent;
    float scale = Theme::kSmallFontSize / font.pixelHeight();

    renderer.drawRect(x, y, w, 3, masterCol);
    drawText(renderer, font, "MASTER", x + 4, y + 5, scale, Theme::textPrimary);

    const auto& master = m_engine->mixer().master();

    // Fader
    float faderY = y + 28;
    float faderH = h - (faderY - y) - 22;
    renderFader(renderer, x + 4, faderY, kFaderWidth + 4, faderH, master.volume, masterCol);

    // Meter (wider for master)
    renderMeter(renderer, x + kFaderWidth + 14, faderY, kMeterWidth * 2, faderH,
                m_masterMeter.peakL, m_masterMeter.peakR);

    // dB label
    float db = master.volume > 0.001f ? 20.0f * std::log10(master.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    drawText(renderer, font, dbText, x + 4, y + h - 18, scale * 0.85f, Theme::textDim);
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
    float knobY = y + h - frac * h - 4;
    float knobH = 8;

    renderer.drawRect(x, knobY, w, knobH, trackColor);
    renderer.drawRect(x + 1, knobY + 1, w - 2, knobH - 2, Color{200, 200, 200});

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

    float stripX = m_viewX + 2;
    float btnY = m_viewY + 2 + 24;

    // Check track strips
    for (int t = 0; t < m_project->numTracks(); ++t) {
        float sx = stripX;
        if (mx >= sx && mx < sx + kStripWidth) {
            // Mute button
            if (my >= btnY && my < btnY + kButtonHeight) {
                if (mx < sx + 4 + kButtonWidth) {
                    bool cur = m_engine->mixer().trackChannel(t).muted;
                    m_engine->sendCommand(audio::SetTrackMuteMsg{t, !cur});
                    return true;
                }
                if (mx < sx + 4 + kButtonWidth * 2 + 2) {
                    bool cur = m_engine->mixer().trackChannel(t).soloed;
                    m_engine->sendCommand(audio::SetTrackSoloMsg{t, !cur});
                    return true;
                }
            }
        }
        stripX += kStripWidth + kStripPadding;
    }

    return false;
}

bool MixerView::handleDrag(float mx, float my) {
    (void)mx; (void)my;
    return false;
}

void MixerView::handleRelease() {
    m_dragging = false;
    m_dragTarget = -1;
}

} // namespace ui
} // namespace yawn
