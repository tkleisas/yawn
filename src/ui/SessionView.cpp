#include "ui/SessionView.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <SDL3/SDL_timer.h>

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

void SessionView::setTransportState(bool playing, double beats, double bpm,
                                    int numerator, int denominator) {
    m_transportPlaying = playing;
    m_transportBeats = beats;
    m_transportBPM = bpm;
    m_transportNumerator = numerator;
    m_transportDenominator = denominator;
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

    // --- BPM box (clickable/editable) ---
    float bpmX = x + 160;
    float boxH = h - 8;
    float boxY = y + 4;
    float bpmW = 100;
    m_bpmBoxX = bpmX; m_bpmBoxY = boxY; m_bpmBoxW = bpmW; m_bpmBoxH = boxH;

    bool editingBpm = (m_editMode == EditMode::BPM);
    Color bpmBg = editingBpm ? Color{60, 60, 80, 255} : Color{40, 40, 50, 255};
    renderer.drawRect(bpmX, boxY, bpmW, boxH, bpmBg);
    renderer.drawRectOutline(bpmX, boxY, bpmW, boxH,
                              editingBpm ? Theme::transportAccent : Theme::clipSlotBorder);

    char bpmText[32];
    if (editingBpm)
        std::snprintf(bpmText, sizeof(bpmText), "%s_", m_editBuffer.c_str());
    else
        std::snprintf(bpmText, sizeof(bpmText), "%.2f", m_transportBPM);
    tx = bpmX + 6;
    if (font.isLoaded()) {
        for (const char* p = bpmText; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       editingBpm ? Theme::transportAccent : Theme::transportText,
                                       font.textureId());
            tx += g.xAdvance;
        }
    }
    // "BPM" label
    tx = bpmX + bpmW + 4;
    if (font.isLoaded()) {
        for (const char* p = "BPM"; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale * 0.85f);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::textSecondary, font.textureId());
            tx += g.xAdvance;
        }
    }

    // --- Time signature: two separate boxes with / between ---
    float tsX = bpmX + bpmW + 50;
    float tsBoxW = 30;
    float slashGap = 4;

    // Numerator box
    bool editingNum = (m_editMode == EditMode::TimeSigNum);
    m_tsNumBoxX = tsX; m_tsNumBoxY = boxY; m_tsNumBoxW = tsBoxW; m_tsNumBoxH = boxH;
    Color numBg = editingNum ? Color{60, 60, 80, 255} : Color{40, 40, 50, 255};
    renderer.drawRect(tsX, boxY, tsBoxW, boxH, numBg);
    renderer.drawRectOutline(tsX, boxY, tsBoxW, boxH,
                              editingNum ? Theme::transportAccent : Theme::clipSlotBorder);

    char numText[8];
    if (editingNum)
        std::snprintf(numText, sizeof(numText), "%s_", m_editBuffer.c_str());
    else
        std::snprintf(numText, sizeof(numText), "%d", m_transportNumerator);
    tx = tsX + 6;
    if (font.isLoaded()) {
        for (const char* p = numText; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       editingNum ? Theme::transportAccent : Theme::transportText,
                                       font.textureId());
            tx += g.xAdvance;
        }
    }

    // "/" separator
    float slashX = tsX + tsBoxW + slashGap;
    if (font.isLoaded()) {
        tx = slashX;
        auto g = font.getGlyph('/', tx, textY, scale);
        renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                   g.u0, g.v0, g.u1, g.v1,
                                   Theme::textSecondary, font.textureId());
        slashX = tx + g.xAdvance;
    }

    // Denominator box
    float denX = slashX + slashGap;
    bool editingDen = (m_editMode == EditMode::TimeSigDen);
    m_tsDenBoxX = denX; m_tsDenBoxY = boxY; m_tsDenBoxW = tsBoxW; m_tsDenBoxH = boxH;
    Color denBg = editingDen ? Color{60, 60, 80, 255} : Color{40, 40, 50, 255};
    renderer.drawRect(denX, boxY, tsBoxW, boxH, denBg);
    renderer.drawRectOutline(denX, boxY, tsBoxW, boxH,
                              editingDen ? Theme::transportAccent : Theme::clipSlotBorder);

    char denText[8];
    if (editingDen)
        std::snprintf(denText, sizeof(denText), "%s_", m_editBuffer.c_str());
    else
        std::snprintf(denText, sizeof(denText), "%d", m_transportDenominator);
    tx = denX + 6;
    if (font.isLoaded()) {
        for (const char* p = denText; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       editingDen ? Theme::transportAccent : Theme::transportText,
                                       font.textureId());
            tx += g.xAdvance;
        }
    }

    // --- Tap tempo button ---
    float tapX = denX + tsBoxW + 16;
    float tapW = 48;
    m_tapButtonX = tapX; m_tapButtonY = boxY; m_tapButtonW = tapW; m_tapButtonH = boxH;

    m_tapFlash = std::max(0.0f, m_tapFlash - 1.0f / 15.0f);
    uint8_t flashR = static_cast<uint8_t>(40 + m_tapFlash * 60);
    uint8_t flashG = static_cast<uint8_t>(40 + m_tapFlash * 80);
    Color tapBg = {flashR, flashG, 50, 255};
    renderer.drawRect(tapX, boxY, tapW, boxH, tapBg);
    renderer.drawRectOutline(tapX, boxY, tapW, boxH, Theme::clipSlotBorder);
    tx = tapX + 6;
    if (font.isLoaded()) {
        for (const char* p = "TAP"; *p; ++p) {
            auto g = font.getGlyph(*p, tx, textY, scale * 0.85f);
            renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                       g.u0, g.v0, g.u1, g.v1,
                                       Theme::transportText, font.textureId());
            tx += g.xAdvance;
        }
    }

    // --- Bar.Beat position ---
    int bpb = std::max(1, m_transportNumerator);
    int bar = static_cast<int>(m_transportBeats / bpb) + 1;
    int beat = static_cast<int>(std::fmod(m_transportBeats, static_cast<double>(bpb))) + 1;
    char posText[32];
    std::snprintf(posText, sizeof(posText), "%d . %d", bar, beat);
    tx = tapX + tapW + 20;
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

    const auto* slot = m_project->getSlot(trackIndex, sceneIndex);
    bool hasClip = slot && !slot->empty();
    const audio::Clip* audioClip = slot ? slot->audioClip.get() : nullptr;
    const midi::MidiClip* midiClip = slot ? slot->midiClip.get() : nullptr;
    bool isPlaying = m_trackStates[trackIndex].playing && hasClip;

    // Slot background
    Color bgColor = hasClip ? Theme::panelBg : Theme::clipSlotEmpty;
    renderer.drawRect(ix, iy, iw, ih, bgColor);

    if (hasClip) {
        // Track color bar on left
        Color trackCol = Theme::trackColors[m_project->track(trackIndex).colorIndex % Theme::kNumTrackColors];
        renderer.drawRect(ix, iy, 3, ih, trackCol);

        // Clip name
        const std::string& clipName = audioClip ? audioClip->name
                                                : (midiClip ? midiClip->name() : std::string());
        float scale = Theme::kSmallFontSize / font.pixelHeight();
        if (font.isLoaded() && !clipName.empty()) {
            float textX = ix + 7;
            float textY = iy + 2;
            for (char c : clipName) {
                if (c == '/' || c == '\\') { textX = ix + 7; continue; }
                auto g = font.getGlyph(c, textX, textY, scale);
                renderer.drawTexturedQuad(g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0,
                                           g.u0, g.v0, g.u1, g.v1,
                                           Theme::textPrimary, font.textureId());
                textX += g.xAdvance;
                if (textX > ix + iw - 4) break;
            }
        }

        // Mini waveform for audio clips
        if (audioClip && audioClip->buffer && audioClip->buffer->numFrames() > 0) {
            float wfY = iy + 18;
            float wfH = ih - 22;
            Color wfColor = trackCol.withAlpha(160);

            renderer.drawWaveform(audioClip->buffer->channelData(0),
                                  audioClip->buffer->numFrames(),
                                  ix + 4, wfY, iw - 8, wfH, wfColor);

            // Playhead indicator
            if (isPlaying) {
                int64_t pos = m_trackStates[trackIndex].playPosition;
                float frac = static_cast<float>(pos) / audioClip->buffer->numFrames();
                frac = std::fmod(frac, 1.0f);
                float phX = ix + 4 + frac * (iw - 8);
                renderer.drawRect(phX, wfY, 2, wfH, Theme::playing);
            }
        }

        // Mini note view for MIDI clips
        if (midiClip && midiClip->noteCount() > 0) {
            float nY = iy + 18;
            float nH = ih - 22;
            Color noteCol = trackCol.withAlpha(180);

            // Find pitch range for scaling
            int minPitch = 127, maxPitch = 0;
            for (int i = 0; i < midiClip->noteCount(); ++i) {
                int p = midiClip->note(i).pitch;
                if (p < minPitch) minPitch = p;
                if (p > maxPitch) maxPitch = p;
            }
            int pitchRange = std::max(1, maxPitch - minPitch + 1);
            double lenBeats = midiClip->lengthBeats();

            for (int i = 0; i < midiClip->noteCount(); ++i) {
                const auto& n = midiClip->note(i);
                float nx = ix + 4 + static_cast<float>(n.startBeat / lenBeats) * (iw - 8);
                float nw = static_cast<float>(n.duration / lenBeats) * (iw - 8);
                nw = std::max(1.0f, nw);
                float ny = nY + nH - (static_cast<float>(n.pitch - minPitch + 1) / pitchRange) * nH;
                float nh = std::max(1.0f, nH / pitchRange);
                renderer.drawRect(nx, ny, nw, nh, noteCol);
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

    // Transport bar area clicks
    if (my < headerY) {
        // Tap tempo button
        if (mx >= m_tapButtonX && mx <= m_tapButtonX + m_tapButtonW &&
            my >= m_tapButtonY && my <= m_tapButtonY + m_tapButtonH) {
            tapTempo();
            return true;
        }
        // Single click on BPM or time sig just cancels any edit
        if (m_editMode != EditMode::None) {
            m_editMode = EditMode::None;
            m_editBuffer.clear();
        }
        return true;
    }

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
                auto* slot = m_project->getSlot(t, sceneIndex);
                if (slot && slot->audioClip) {
                    m_engine->sendCommand(audio::LaunchClipMsg{t, slot->audioClip.get()});
                } else if (slot && slot->midiClip) {
                    m_engine->sendCommand(audio::LaunchMidiClipMsg{t, slot->midiClip.get()});
                } else {
                    m_engine->sendCommand(audio::StopClipMsg{t});
                    m_engine->sendCommand(audio::StopMidiClipMsg{t});
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

            auto* slot = m_project->getSlot(trackIndex, sceneIndex);

            if (isRightClick) {
                m_engine->sendCommand(audio::StopClipMsg{trackIndex});
                m_engine->sendCommand(audio::StopMidiClipMsg{trackIndex});
                std::printf("Stop track %d\n", trackIndex + 1);
            } else if (slot && slot->audioClip) {
                m_engine->sendCommand(audio::LaunchClipMsg{trackIndex, slot->audioClip.get()});
                std::printf("Launch audio clip [%d, %d]\n", trackIndex + 1, sceneIndex + 1);
            } else if (slot && slot->midiClip) {
                m_engine->sendCommand(audio::LaunchMidiClipMsg{trackIndex, slot->midiClip.get()});
                std::printf("Launch MIDI clip [%d, %d]\n", trackIndex + 1, sceneIndex + 1);
            }
            return true;
        }
    }

    return false;
}

bool SessionView::getSlotAt(float mx, float my, int& trackOut, int& sceneOut) const {
    if (!m_project) return false;
    float gridX = m_viewX + Theme::kSceneLabelWidth;
    float gridY = m_viewY + Theme::kTransportBarHeight + Theme::kTrackHeaderHeight;

    if (mx < gridX || my < gridY) return false;

    float contentMX = mx + m_scrollX;
    float contentMY = my + m_scrollY;
    int track = static_cast<int>((contentMX - gridX) / Theme::kTrackWidth);
    int scene = static_cast<int>((contentMY - gridY) / Theme::kClipSlotHeight);

    if (track < 0 || track >= m_project->numTracks()) return false;
    if (scene < 0 || scene >= m_project->numScenes()) return false;

    trackOut = track;
    sceneOut = scene;
    return true;
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

bool SessionView::handleDoubleClick(float mx, float my) {
    // BPM box double-click → enter edit mode
    if (mx >= m_bpmBoxX && mx <= m_bpmBoxX + m_bpmBoxW &&
        my >= m_bpmBoxY && my <= m_bpmBoxY + m_bpmBoxH) {
        m_editMode = EditMode::BPM;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", m_transportBPM);
        m_editBuffer = buf;
        return true;
    }
    // Numerator box double-click
    if (mx >= m_tsNumBoxX && mx <= m_tsNumBoxX + m_tsNumBoxW &&
        my >= m_tsNumBoxY && my <= m_tsNumBoxY + m_tsNumBoxH) {
        m_editMode = EditMode::TimeSigNum;
        m_editBuffer = std::to_string(m_transportNumerator);
        return true;
    }
    // Denominator box double-click
    if (mx >= m_tsDenBoxX && mx <= m_tsDenBoxX + m_tsDenBoxW &&
        my >= m_tsDenBoxY && my <= m_tsDenBoxY + m_tsDenBoxH) {
        m_editMode = EditMode::TimeSigDen;
        m_editBuffer = std::to_string(m_transportDenominator);
        return true;
    }
    return false;
}

bool SessionView::handleTextInput(const char* text) {
    if (m_editMode == EditMode::None) return false;
    for (const char* p = text; *p; ++p) {
        char c = *p;
        if (m_editMode == EditMode::BPM) {
            if ((c >= '0' && c <= '9') || (c == '.' && m_editBuffer.find('.') == std::string::npos))
                m_editBuffer += c;
        } else {
            // Time signature: digits only
            if (c >= '0' && c <= '9')
                m_editBuffer += c;
        }
    }
    return true;
}

bool SessionView::handleKeyDown(int keycode) {
    if (m_editMode == EditMode::None) return false;

    // SDL_SCANCODE / SDLK constants:  Enter=13, Escape=27, Backspace=8, Tab=9
    if (keycode == 13 || keycode == 9) { // Enter or Tab
        if (m_editMode == EditMode::BPM) {
            double val = std::atof(m_editBuffer.c_str());
            val = std::clamp(val, 20.0, 999.0);
            if (m_engine)
                m_engine->sendCommand(audio::TransportSetBPMMsg{val});
            if (keycode == 9) {
                // Tab from BPM → jump to time sig numerator
                m_editMode = EditMode::TimeSigNum;
                m_editBuffer = std::to_string(m_transportNumerator);
                return true;
            }
        } else if (m_editMode == EditMode::TimeSigNum) {
            int val = std::atoi(m_editBuffer.c_str());
            val = std::clamp(val, 1, 32);
            if (m_engine)
                m_engine->sendCommand(audio::TransportSetTimeSignatureMsg{val, m_transportDenominator});
            if (keycode == 9) {
                // Tab from numerator → jump to denominator
                m_editMode = EditMode::TimeSigDen;
                m_editBuffer = std::to_string(m_transportDenominator);
                return true;
            }
        } else if (m_editMode == EditMode::TimeSigDen) {
            int val = std::atoi(m_editBuffer.c_str());
            // Denominator must be a power of 2 (1,2,4,8,16,32)
            if (val < 1) val = 1;
            else if (val <= 1) val = 1;
            else if (val <= 2) val = 2;
            else if (val <= 4) val = 4;
            else if (val <= 8) val = 8;
            else if (val <= 16) val = 16;
            else val = 32;
            if (m_engine)
                m_engine->sendCommand(audio::TransportSetTimeSignatureMsg{m_transportNumerator, val});
        }
        m_editMode = EditMode::None;
        m_editBuffer.clear();
        return true;
    }

    if (keycode == 27) { // Escape — cancel
        m_editMode = EditMode::None;
        m_editBuffer.clear();
        return true;
    }

    if (keycode == 8) { // Backspace
        if (!m_editBuffer.empty())
            m_editBuffer.pop_back();
        return true;
    }

    return true; // consume all keys while editing
}

void SessionView::tapTempo() {
    double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    m_tapFlash = 1.0f;

    if (m_tapCount > 0) {
        double lastTap = m_tapTimes[(m_tapCount - 1) % kTapHistorySize];
        if (now - lastTap > 2.0) {
            // Too long since last tap — reset
            m_tapCount = 0;
        }
    }

    m_tapTimes[m_tapCount % kTapHistorySize] = now;
    m_tapCount++;

    if (m_tapCount >= 2) {
        int numIntervals = std::min(m_tapCount - 1, kTapHistorySize - 1);
        int startIdx = m_tapCount - numIntervals - 1;
        double totalInterval = m_tapTimes[(m_tapCount - 1) % kTapHistorySize]
                             - m_tapTimes[startIdx % kTapHistorySize];
        double avgInterval = totalInterval / numIntervals;
        double bpm = 60.0 / avgInterval;
        bpm = std::clamp(bpm, 20.0, 999.0);
        // Round to 2 decimal places
        bpm = std::round(bpm * 100.0) / 100.0;
        if (m_engine)
            m_engine->sendCommand(audio::TransportSetBPMMsg{bpm});
    }
}

} // namespace ui
} // namespace yawn
