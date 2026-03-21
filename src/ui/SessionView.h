#pragma once

#include "core/Constants.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "app/Project.h"
#include "audio/AudioEngine.h"
#include <string>

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
    // selectedTrack: output — set to track index if header was clicked
    bool handleClick(float mx, float my, bool isRightClick, int* selectedTrack = nullptr);

    // Handle double-click (for BPM/time sig editing)
    bool handleDoubleClick(float mx, float my);

    // Handle text input for active edit fields
    bool handleTextInput(const char* text);

    // Handle key input for edit fields (Enter to confirm, Escape to cancel)
    bool handleKeyDown(int keycode);

    // Handle scroll wheel. dx = horizontal, dy = vertical.
    void handleScroll(float dx, float dy);

    // Tap tempo — call each time the tap button is pressed
    void tapTempo();

    // Whether the session view has an active text edit
    bool isEditing() const { return m_editMode != EditMode::None; }

    // Transport info for rendering
    void setTransportState(bool playing, double beats, double bpm,
                           int numerator = 4, int denominator = 4);

    // Preferred height showing up to kVisibleScenes scenes
    float preferredHeight() const;

    void setSelectedTrack(int track) { m_selectedTrack = track; }
    int selectedTrack() const { return m_selectedTrack; }

    float scrollX() const { return m_scrollX; }
    float scrollY() const { return m_scrollY; }

    static constexpr int kVisibleScenes = 8;

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
    int m_transportNumerator = 4;
    int m_transportDenominator = 4;

    // Scroll state
    float m_scrollX = 0.0f;
    float m_scrollY = 0.0f;

    // Layout cache
    float m_viewX = 0, m_viewY = 0, m_viewW = 0, m_viewH = 0;

    // Currently playing scene (-1 = none)
    int m_activeScene = -1;
    int m_selectedTrack = 0;

    // Animation timer
    float m_animTimer = 0.0f;

    // BPM/Time signature editing
    enum class EditMode { None, BPM, TimeSigNum, TimeSigDen };
    EditMode m_editMode = EditMode::None;
    std::string m_editBuffer;
    float m_bpmBoxX = 0, m_bpmBoxY = 0, m_bpmBoxW = 0, m_bpmBoxH = 0;
    float m_tsBoxX = 0, m_tsBoxY = 0, m_tsBoxW = 0, m_tsBoxH = 0;

    // Tap tempo state
    static constexpr int kTapHistorySize = 4;
    double m_tapTimes[kTapHistorySize] = {};
    int m_tapCount = 0;
    float m_tapButtonX = 0, m_tapButtonY = 0, m_tapButtonW = 0, m_tapButtonH = 0;
    float m_tapFlash = 0.0f;
};

} // namespace ui
} // namespace yawn
