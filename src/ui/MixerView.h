#pragma once

#include "core/Constants.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/Mixer.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"

namespace yawn {
namespace ui {

// Meter data received from audio thread
struct MeterData {
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class MixerView {
public:
    MixerView() = default;

    void init(Project* project, audio::AudioEngine* engine);

    // Update meter levels from audio events
    void updateMeter(int trackIndex, float peakL, float peakR);

    // Render the mixer panel
    void render(Renderer2D& renderer, Font& font,
                float x, float y, float width, float height);

    // Handle mouse input
    bool handleClick(float mx, float my, bool isRightClick);
    bool handleDrag(float mx, float my);
    void handleRelease();

    float preferredHeight() const { return kMixerHeight; }

private:
    void renderChannelStrip(Renderer2D& renderer, Font& font,
                            int trackIndex, float x, float y, float w, float h);
    void renderReturnStrip(Renderer2D& renderer, Font& font,
                           int busIndex, float x, float y, float w, float h);
    void renderMasterStrip(Renderer2D& renderer, Font& font,
                           float x, float y, float w, float h);
    void renderMeter(Renderer2D& renderer,
                     float x, float y, float w, float h,
                     float peakL, float peakR);
    void renderFader(Renderer2D& renderer,
                     float x, float y, float w, float h,
                     float value, Color trackColor);
    void renderButton(Renderer2D& renderer, Font& font,
                      float x, float y, float w, float h,
                      const char* label, Color bgColor, Color textColor);

    void drawText(Renderer2D& renderer, Font& font,
                  const char* text, float x, float y, float scale, Color color);

    Project* m_project = nullptr;
    audio::AudioEngine* m_engine = nullptr;

    // Meter data from audio thread
    MeterData m_trackMeters[kMaxTracks] = {};
    MeterData m_returnMeters[kMaxReturnBuses] = {};
    MeterData m_masterMeter = {};

    // Dragging state
    enum class DragType { None, Fader, Pan };
    bool m_dragging = false;
    DragType m_dragType = DragType::None;
    int m_dragTarget = -1;   // track index, or special values for returns/master
    float m_dragStartY = 0;
    float m_dragStartValue = 0;

    // Layout cache
    float m_viewX = 0, m_viewY = 0, m_viewW = 0, m_viewH = 0;

    // Layout constants
    static constexpr float kMixerHeight = 280.0f;
    static constexpr float kStripWidth = 80.0f;
    static constexpr float kMeterWidth = 6.0f;
    static constexpr float kFaderWidth = 8.0f;
    static constexpr float kFaderHeight = 100.0f;
    static constexpr float kButtonHeight = 22.0f;
    static constexpr float kButtonWidth = 28.0f;
    static constexpr float kStripPadding = 2.0f;
    static constexpr float kSeparatorWidth = 2.0f;

    // Special drag target IDs
    static constexpr int kDragMaster = -1;
    static constexpr int kDragReturn0 = -100;
};

} // namespace ui
} // namespace yawn
