#pragma once
// UIContext — Shared rendering/layout context passed to all widgets.
// Holds DPI scale, font, theme references, and the animator.

#include "Types.h"

namespace yawn {
namespace ui {
namespace fw {

// Forward declarations
class Renderer2D;
class Font;
class Animator;

struct UIContext {
    Renderer2D* renderer = nullptr;
    Font*       font     = nullptr;
    Animator*   animator = nullptr;
    ScaleContext scale;

    // Convenience: convert design pixels to physical pixels
    float dp(float value) const { return scale.dp(value); }
    Size  dp(Size s)      const { return scale.dp(s); }
    Rect  dp(Rect r)      const { return scale.dp(r); }
    Insets dp(Insets i)    const { return scale.dp(i); }
};

} // namespace fw
} // namespace ui
} // namespace yawn
