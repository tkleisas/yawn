#pragma once

#include "core/Constants.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "app/Project.h"
#include "audio/AudioEngine.h"

namespace yawn {
namespace ui {

// State tracked per clip slot for UI purposes
struct ClipSlotUIState {
    bool playing = false;
    bool queued = false;
    int64_t playPosition = 0;
};

class SessionView {
public:
    SessionView() = default;

    void init(Project* project, audio::AudioEngine* engine);

    // Update UI state from audio engine events
    void updateClipState(int trackIndex, bool playing, int64_t playPosition);

    // Render the session view
    void render(Renderer2D& renderer, Font& font,
                float x, float y, float width, float height);

    // Handle mouse input. Returns true if the click was consumed.
    bool handleClick(float mx, float my, bool isRightClick);

    // Transport info for rendering
    void setTransportState(bool playing, double beats, double bpm);

private:
    void renderTransportBar(Renderer2D& renderer, Font& font,
                            float x, float y, float w);
    void renderTrackHeaders(Renderer2D& renderer, Font& font,
                            float x, float y, float w);
    void renderSceneLabels(Renderer2D& renderer, Font& font,
                           float x, float y, float h);
    void renderClipGrid(Renderer2D& renderer, Font& font,
                        float x, float y, float w, float h);
    void renderClipSlot(Renderer2D& renderer, Font& font,
                        int trackIndex, int sceneIndex,
                        float x, float y, float w, float h);

    Project* m_project = nullptr;
    audio::AudioEngine* m_engine = nullptr;

    // Per-track clip state from audio thread
    ClipSlotUIState m_trackStates[kMaxTracks] = {};

    // Transport display
    bool m_transportPlaying = false;
    double m_transportBeats = 0.0;
    double m_transportBPM = 120.0;

    // Scroll state
    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;

    // Layout cache
    float m_viewX = 0, m_viewY = 0, m_viewW = 0, m_viewH = 0;

    // Currently playing scene (-1 = none)
    int m_activeScene = -1;

    // Animation timer
    float m_animTimer = 0.0f;
};

} // namespace ui
} // namespace yawn
