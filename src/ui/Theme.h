#pragma once

#include <cstdint>

namespace yawn {
namespace ui {

struct Color {
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}

    constexpr Color withAlpha(uint8_t alpha) const { return {r, g, b, alpha}; }

    static Color lerp(const Color& a, const Color& b, float t) {
        return {
            static_cast<uint8_t>(a.r + (b.r - a.r) * t),
            static_cast<uint8_t>(a.g + (b.g - a.g) * t),
            static_cast<uint8_t>(a.b + (b.b - a.b) * t),
            static_cast<uint8_t>(a.a + (b.a - a.a) * t)
        };
    }
};

namespace Theme {
    // Background
    inline constexpr Color background       {30, 30, 33};
    inline constexpr Color panelBg          {40, 40, 43};
    inline constexpr Color trackHeaderBg    {50, 50, 55};
    inline constexpr Color sceneLabelBg     {42, 42, 46};

    // Clip slots
    inline constexpr Color clipSlotEmpty    {38, 38, 41};
    inline constexpr Color clipSlotHover    {55, 55, 60};
    inline constexpr Color clipSlotBorder   {60, 60, 65};

    // State colors
    inline constexpr Color playing          {80, 230, 80};
    inline constexpr Color queued           {255, 200, 50};
    inline constexpr Color stopped          {160, 160, 160};
    inline constexpr Color recording        {255, 60, 60};

    // Transport bar
    inline constexpr Color transportBg      {35, 35, 38};
    inline constexpr Color transportText    {200, 200, 200};
    inline constexpr Color transportAccent  {255, 165, 0};

    // Text
    inline constexpr Color textPrimary      {220, 220, 220};
    inline constexpr Color textSecondary    {140, 140, 145};
    inline constexpr Color textDim          {90, 90, 95};

    // Track colors (assigned round-robin)
    inline constexpr Color trackColors[] = {
        {255, 120,  50},  // orange
        {100, 180, 255},  // blue
        {120, 220, 100},  // green
        {220, 100, 220},  // purple
        {255, 220,  60},  // yellow
        {100, 220, 220},  // cyan
        {255, 100, 130},  // pink
        {180, 180, 100},  // olive
    };
    inline constexpr int kNumTrackColors = sizeof(trackColors) / sizeof(trackColors[0]);

    // Layout dimensions
    inline constexpr float kTransportBarHeight = 44.0f;
    inline constexpr float kTrackHeaderHeight  = 32.0f;
    inline constexpr float kTrackWidth         = 130.0f;
    inline constexpr float kClipSlotHeight     = 64.0f;
    inline constexpr float kSceneLabelWidth    = 44.0f;
    inline constexpr float kSlotPadding        = 2.0f;
    inline constexpr float kSlotCornerRadius   = 3.0f;
    inline constexpr float kFontSize           = 26.0f;
    inline constexpr float kSmallFontSize      = 22.0f;

} // namespace Theme
} // namespace ui
} // namespace yawn
