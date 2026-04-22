#pragma once

// UI v2 — Runtime-swappable theme (palette + metrics).
//
// Replaces v1's `namespace Theme { inline constexpr Color ... }` with
// a data-driven, swappable Theme struct read via theme() / setTheme().
// Widgets consume tokens as theme().palette.X / theme().metrics.X.
//
// See docs/ui-v2-theme.md for the complete design.

#include "ui/Theme.h"   // reuse v1 Color

#include <array>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

// Reuse v1's Color — it's a pure POD rgba type, fine for v2.
using ::yawn::ui::Color;

// ───────────────────────────────────────────────────────────────────
// ThemePalette — every color token consumed by v2 widgets.
// ───────────────────────────────────────────────────────────────────
struct ThemePalette {
    // Backgrounds
    Color background{30, 30, 33};
    Color panelBg{40, 40, 43};
    Color surface{50, 50, 55};
    Color elevated{58, 58, 62};

    // Controls
    Color controlBg{40, 40, 43};
    Color controlHover{65, 65, 75};
    Color controlActive{80, 80, 85};
    Color border{70, 70, 75};
    Color borderSubtle{55, 55, 58};

    // Accent & state
    Color accent{255, 165, 0};
    Color accentHover{255, 180, 50};
    Color accentActive{220, 140, 0};
    Color success{80, 230, 80};
    Color warn{255, 200, 50};
    Color error{220, 70, 70};
    Color playing{80, 230, 80};
    Color recording{255, 60, 60};
    Color queued{255, 200, 50};

    // Modulation overlays (LFO / automation / CC indicator on knobs/faders)
    Color modulation{100, 150, 255};
    Color modulationRange{100, 150, 255, 60};

    // Text
    Color textPrimary{220, 220, 220};
    Color textSecondary{140, 140, 145};
    Color textDim{90, 90, 95};
    Color textOnAccent{20, 20, 22};
    Color textOnError{255, 255, 255};

    // Overlays
    Color dropShadow{0, 0, 0, 120};
    Color scrim{0, 0, 0, 140};

    // Track colors (rotating palette)
    std::array<Color, 8> trackColors{{
        {255, 120,  50},  // orange
        {100, 180, 255},  // blue
        {120, 220, 100},  // green
        {220, 100, 220},  // purple
        {255, 220,  60},  // yellow
        {100, 220, 220},  // cyan
        {255, 100, 130},  // pink
        {180, 180, 100},  // olive
    }};
};

// ───────────────────────────────────────────────────────────────────
// ThemeMetrics — every size/spacing token consumed by v2 widgets.
// All values in LOGICAL pixels (framework multiplies by dpiScale at
// paint time).
// ───────────────────────────────────────────────────────────────────
struct ThemeMetrics {
    // Spacing
    float baseUnit     = 4.0f;
    float cornerRadius = 3.0f;
    float borderWidth  = 1.0f;

    // Control dimensions
    float controlHeight      = 28.0f;
    float scrollbarThickness = 12.0f;
    float splitterThickness  = 6.0f;

    // Typography sizes bumped +2px from the original 13/11/16 after
    // user feedback that v2 widgets read as too small. New baselines
    // still fit comfortably within controlHeight = 28.
    float fontSize      = 15.0f;
    float fontSizeSmall = 13.0f;
    float fontSizeLarge = 18.0f;
    std::string fontPath;                     // empty = default bundled
    float lineHeightMultiplier = 1.2f;
};

// ───────────────────────────────────────────────────────────────────
// Theme — the value type held globally + swapped atomically.
// ───────────────────────────────────────────────────────────────────
struct Theme {
    std::string  name    = "Dark";
    std::string  version = "1.0";
    ThemePalette palette;
    ThemeMetrics metrics;
};

// ───────────────────────────────────────────────────────────────────
// Global access
// ───────────────────────────────────────────────────────────────────

// Returns a reference to the currently-active theme. Call from UI
// thread only. Cheap: ~one pointer indirection.
const Theme& theme();

// Swap the current theme. Bumps the UIContext epoch to invalidate
// measure caches (since metrics changes can affect sizes). UI-thread
// only.
void setTheme(Theme newTheme);

} // namespace fw2
} // namespace ui
} // namespace yawn
