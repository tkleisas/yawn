#include "ui/SessionView.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace yawn {
namespace ui {

void SessionView::init(Project* project, audio::AudioEngine* engine) {
    m_project = project;
    m_engine = engine;
}

void SessionView::updateClipState(int trackIndex, bool playing, int64_t playPosition) {
    if (trackIndex >= 0 && trackIndex < kMaxTracks) {
        m_trackStates[trackIndex].playing = playing;
        m_trackStates[trackIndex].playPosition = playPosition;
    }
}

void SessionView::setTransportState(bool playing, double beats, double bpm) {
    m_transportPlaying = playing;
    m_transportBeats = beats;
    m_transportBPM = bpm;
    m_animTimer += 1.0f / 60.0f; // approximate
}

float SessionView::preferredHeight() const {
    int scenes = m_project ? std::min(m_project->numScenes(), kVisibleScenes) : kVisibleScenes;
    return Theme::kTransportBarHeight + Theme::kTrackHeaderHeight
         + scenes * Theme::kClipSlotHeight;
}

void SessionView::render(Renderer2D& renderer, Font& font,
                          float x, float y, float width, float height) {
    m_viewX = x;
    m_viewY = y;
    m_viewW = width;
    m_viewH = height;

    // Background
    renderer.drawRect(x, y, width, height, Theme::background);

    float transportY = y;
    float headerY = transportY + Theme::kTransportBarHeight;
    float gridY = headerY + Theme::kTrackHeaderHeight;
    float gridH = height - Theme::kTransportBarHeight - Theme::kTrackHeaderHeight;
    float gridX = x + Theme::kSceneLabelWidth;
    float gridW = width - Theme::kSceneLabelWidth;

    renderTransportBar(renderer, font, x, transportY, width);
    renderTrackHeaders(renderer, font, gridX, headerY, gridW);
    renderSceneLabels(renderer, font, x, gridY, gridH);
    renderClipGrid(renderer, font, gridX, gridY, gridW, gridH);
}

void SessionView::renderTransportBar(Renderer2D& renderer, Font& font,
                                      float x, float y, float w) {
    float h = Theme::kTransportBarHeight;
    renderer.drawRect(x, y, w, h, Theme::transportBg);

    // Separator line
    renderer.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

    float textY = y + 6;
    float scale = Theme::kFontSize / font.pixelHeight();

    // Play/Stop indicator
    Color stateColor = m_transportPlaying ? Theme::playing : Theme::stopped;
    const char* stateText = m_transportPlaying ? "PLAYING" : "STOPPED";

    // Play indicator circle
    float circleX = x + 12;
    float circleY = y + h * 0.5f - 5;
    renderer.drawRect(circleX, circleY, 10, 10, stateColor);

    // State text
    float tx = circleX + 18;
    if (font.isLoaded()) {
        for (const char* p = stateText; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       stateColor, font.textureId());
            tx += g.xAdvance;
        }
    }

    // BPM
    char bpmText[32];
    std::snprintf(bpmText, sizeof(bpmText), "%.0f BPM", m_transportBPM);
    tx = x + 160;
    if (font.isLoaded()) {
        for (const char* p = bpmText; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::transportText, font.textureId());
            tx += g.xAdvance;
        }
    }

    // Bar.Beat position
    int bar = static_cast<int>(m_transportBeats / 4.0) + 1;
    int beat = static_cast<int>(std::fmod(m_transportBeats, 4.0)) + 1;
    char posText[32];
    std::snprintf(posText, sizeof(posText), "%d . %d", bar, beat);
    tx = x + 280;
    if (font.isLoaded()) {
        for (const char* p = posText; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::transportAccent, font.textureId());
            tx += g.xAdvance;
        }
    }
}

void SessionView::renderTrackHeaders(Renderer2D& renderer, Font& font,
                                      float x, float y, float w) {
    if (!m_project) return;

    float h = Theme::kTrackHeaderHeight;
    renderer.drawRect(x, y, w, h, Theme::trackHeaderBg);
    renderer.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);

    renderer.pushClip(x, y, w, h);
    float scale = Theme::kSmallFontSize / font.pixelHeight();
    float smallScale = scale * 0.7f;

    for (int t = 0; t < m_project->numTracks(); ++t) {
        float tx = x + t * Theme::kTrackWidth - m_scrollX;
        float tw = Theme::kTrackWidth;

        if (tx + tw < x || tx > x + w) continue; // off-screen

        // Selected track highlight
        if (t == m_selectedTrack) {
            renderer.drawRect(tx + 1, y + 1, tw - 2, h - 2, Color{50, 55, 65, 255});
        }

        // Track color indicator bar
        Color trackCol = Theme::trackColors[m_project->track(t).colorIndex % Theme::kNumTrackColors];
        renderer.drawRect(tx + 2, y + 2, tw - 4, 3, trackCol);

        // Track name
        if (font.isLoaded()) {
            float textX = tx + 6;
            float textY = y + 8;
            for (char c : m_project->track(t).name) {
                auto g = font.getGlyph(c, textX, textY, scale);
                renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                           g.u0, g.v0, g.u1, g.v1,
                                           Theme::textPrimary, font.textureId());
                textX += g.xAdvance;
                if (textX > tx + tw - 6) break;
            }
        }

        // Track type badge (Audio / MIDI)
        if (font.isLoaded()) {
            bool isMidi = (m_project->track(t).type == Track::Type::Midi);
            const char* typeLabel = isMidi ? "MIDI" : "Audio";
            Color typeCol = isMidi ? Color{180, 130, 255} : Color{130, 200, 130};
            font.drawText(renderer, typeLabel, tx + 6, y + 22, smallScale, typeCol);
        }

        // Separator
        renderer.drawRect(tx + tw - 1, y, 1, h, Theme::clipSlotBorder);
    }
    renderer.popClip();
}

void SessionView::renderSceneLabels(Renderer2D& renderer, Font& font,
                                     float x, float y, float h) {
    if (!m_project) return;

    float w = Theme::kSceneLabelWidth;
    renderer.drawRect(x, y, w, h, Theme::sceneLabelBg);

    renderer.pushClip(x, y, w, h);
    float scale = Theme::kSmallFontSize / font.pixelHeight();

    for (int s = 0; s < m_project->numScenes(); ++s) {
        float sy = y + s * Theme::kClipSlotHeight - m_scrollY;
        float sh = Theme::kClipSlotHeight;

        if (sy + sh < y || sy > y + h) continue; // off-screen

        // Scene launch button area
        renderer.drawRect(x + 2, sy + 2, w - 4, sh - 4, Theme::clipSlotEmpty);

        // Scene number
        if (font.isLoaded()) {
            float textX = x + 8;
            float textY = sy + sh * 0.5f - Theme::kSmallFontSize * 0.5f;

            char label[8];
            std::snprintf(label, sizeof(label), "%s", m_project->scene(s).name.c_str());
            Color labelCol = Theme::textSecondary;

            for (const char* p = label; *p; ++p) {
                auto g = font.getGlyph(*p, textX, textY, scale);
                renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                           g.u0, g.v0, g.u1, g.v1,
                                           labelCol, font.textureId());
                textX += g.xAdvance;
            }
        }

        // Separator
        renderer.drawRect(x, sy + sh - 1, w, 1, Theme::clipSlotBorder);
    }
    renderer.popClip();
}

void SessionView::renderClipGrid(Renderer2D& renderer, Font& font,
                                  float x, float y, float w, float h) {
    if (!m_project) return;

    renderer.pushClip(x, y, w, h);

    for (int t = 0; t < m_project->numTracks(); ++t) {
        for (int s = 0; s < m_project->numScenes(); ++s) {
            float sx = x + t * Theme::kTrackWidth - m_scrollX;
            float sy = y + s * Theme::kClipSlotHeight - m_scrollY;
            float sw = Theme::kTrackWidth;
            float sh = Theme::kClipSlotHeight;

            // Skip off-screen slots
            if (sx + sw < x || sx > x + w) continue;
            if (sy + sh < y || sy > y + h) continue;

            renderClipSlot(renderer, font, t, s, sx, sy, sw, sh);
        }
    }

    renderer.popClip();
}

void SessionView::renderClipSlot(Renderer2D& renderer, Font& font,
                                  int trackIndex, int sceneIndex,
                                  float x, float y, float w, float h) {
    float pad = Theme::kSlotPadding;
    float ix = x + pad;
    float iy = y + pad;
    float iw = w - pad * 2;
    float ih = h - pad * 2;

    const audio::Clip* clip = m_project->getClip(trackIndex, sceneIndex);
    bool isPlaying = m_trackStates[trackIndex].playing && clip != nullptr;

    // Slot background
    Color bgColor = clip ? Theme::panelBg : Theme::clipSlotEmpty;
    renderer.drawRect(ix, iy, iw, ih, bgColor);

    if (clip) {
        // Track color bar on left
        Color trackCol = Theme::trackColors[m_project->track(trackIndex).colorIndex % Theme::kNumTrackColors];
        renderer.drawRect(ix, iy, 3, ih, trackCol);

        // Clip name
        float scale = Theme::kSmallFontSize / font.pixelHeight();
        if (font.isLoaded()) {
            float textX = ix + 7;
            float textY = iy + 2;
            for (char c : clip->name) {
                if (c == '/' || c == '\\') { textX = ix + 7; continue; }  // show only filename
                auto g = font.getGlyph(c, textX, textY, scale);
                renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                           g.u0, g.v0, g.u1, g.v1,
                                           Theme::textPrimary, font.textureId());
                textX += g.xAdvance;
                if (textX > ix + iw - 4) break;
            }
        }

        // Mini waveform
        if (clip->buffer && clip->buffer->numFrames() > 0) {
            float wfY = iy + 18;
            float wfH = ih - 22;
            Color wfColor = trackCol.withAlpha(160);

            renderer.drawWaveform(clip->buffer->channelData(0),
                                  clip->buffer->numFrames(),
                                  ix + 4, wfY, iw - 8, wfH, wfColor);

            // Playhead indicator
            if (isPlaying) {
                int64_t pos = m_trackStates[trackIndex].playPosition;
                float frac = static_cast<float>(pos) / clip->buffer->numFrames();
                frac = std::fmod(frac, 1.0f);
                float phX = ix + 4 + frac * (iw - 8);
                renderer.drawRect(phX, wfY, 2, wfH, Theme::playing);
            }
        }

        // Playing border
        if (isPlaying) {
            float pulse = (std::sin(m_animTimer * 6.0f) + 1.0f) * 0.5f;
            Color borderCol = Theme::playing.withAlpha(static_cast<uint8_t>(150 + pulse * 105));
            renderer.drawRectOutline(ix, iy, iw, ih, borderCol, 2.0f);
        }
    }

    // Grid border
    renderer.drawRect(x + w - 1, y, 1, h, Theme::clipSlotBorder);
    renderer.drawRect(x, y + h - 1, w, 1, Theme::clipSlotBorder);
}

bool SessionView::handleClick(float mx, float my, bool isRightClick, int* selectedTrack) {
    if (!m_project || !m_engine) return false;

    float gridX = m_viewX + Theme::kSceneLabelWidth;
    float headerY = m_viewY + Theme::kTransportBarHeight;
    float gridY = headerY + Theme::kTrackHeaderHeight;

    // Not in our area
    if (my < m_viewY || my > m_viewY + m_viewH) return false;
    if (mx < m_viewX || mx > m_viewX + m_viewW) return false;

    // Click in track header area — select track
    if (my >= headerY && my < gridY && mx >= gridX) {
        float contentMX = mx + m_scrollX;
        int trackIndex = static_cast<int>((contentMX - gridX) / Theme::kTrackWidth);
        if (trackIndex >= 0 && trackIndex < m_project->numTracks()) {
            m_selectedTrack = trackIndex;
            if (selectedTrack) *selectedTrack = trackIndex;
            std::printf("Selected track %d: %s\n", trackIndex + 1,
                        m_project->track(trackIndex).name.c_str());
            return true;
        }
    }

    // Not in grid area (above headers/transport)
    if (my < gridY) return false;

    // Account for scroll offsets
    float contentMX = mx + m_scrollX;
    float contentMY = my + m_scrollY;

    // Check if click is in scene label area (launch entire scene)
    if (mx >= m_viewX && mx < gridX) {
        int sceneIndex = static_cast<int>((contentMY - gridY) / Theme::kClipSlotHeight);
        if (sceneIndex >= 0 && sceneIndex < m_project->numScenes()) {
            for (int t = 0; t < m_project->numTracks(); ++t) {
                auto* clip = m_project->getClip(t, sceneIndex);
                if (clip) {
                    m_engine->sendCommand(audio::LaunchClipMsg{t, clip});
                } else {
                    m_engine->sendCommand(audio::StopClipMsg{t});
                }
            }
            m_activeScene = sceneIndex;
            std::printf("Launch scene %d\n", sceneIndex + 1);
            return true;
        }
    }

    // Check if click is in clip grid
    if (mx >= gridX) {
        int trackIndex = static_cast<int>((contentMX - gridX) / Theme::kTrackWidth);
        int sceneIndex = static_cast<int>((contentMY - gridY) / Theme::kClipSlotHeight);

        if (trackIndex >= 0 && trackIndex < m_project->numTracks() &&
            sceneIndex >= 0 && sceneIndex < m_project->numScenes()) {

            // Also select this track
            m_selectedTrack = trackIndex;
            if (selectedTrack) *selectedTrack = trackIndex;

            auto* clip = m_project->getClip(trackIndex, sceneIndex);

            if (isRightClick) {
                m_engine->sendCommand(audio::StopClipMsg{trackIndex});
                std::printf("Stop track %d\n", trackIndex + 1);
            } else if (clip) {
                m_engine->sendCommand(audio::LaunchClipMsg{trackIndex, clip});
                std::printf("Launch clip [%d, %d]\n", trackIndex + 1, sceneIndex + 1);
            }
            return true;
        }
    }

    return false;
}

void SessionView::handleScroll(float dx, float dy) {
    if (!m_project) return;

    float gridH = m_viewH - Theme::kTransportBarHeight - Theme::kTrackHeaderHeight;
    float contentH = m_project->numScenes() * Theme::kClipSlotHeight;
    float maxScrollY = std::max(0.0f, contentH - gridH);
    m_scrollY = std::clamp(m_scrollY - dy * 30.0f, 0.0f, maxScrollY);

    float gridW = m_viewW - Theme::kSceneLabelWidth;
    float contentW = m_project->numTracks() * Theme::kTrackWidth;
    float maxScrollX = std::max(0.0f, contentW - gridW);
    m_scrollX = std::clamp(m_scrollX - dx * 30.0f, 0.0f, maxScrollX);
}

} // namespace ui
} // namespace yawn
