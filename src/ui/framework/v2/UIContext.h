#pragma once

// UI v2 — UIContext.
//
// Per-frame / per-process data the widget tree + event dispatch need.
// Also owns the global epoch counter used for measure / layout cache
// invalidation (bumped on DPI change, theme swap, font reload).
//
// See docs/ui-v2-measure-layout.md for how epoch interacts with
// caching.

#include "ui/Theme.h"              // Color
#include "Types.h"                  // Rect (for viewport)

#include <string>

namespace yawn {
namespace ui {

// Forward-decl v1 types consumed through pointers.
class Renderer2D;

namespace fw2 {

class LayerStack;

// Text-metrics abstraction so v2 doesn't pull glad/GL via v1's Font.h.
// Production wires a FontAdapter around v1 Font; tests use nullptr
// (widgets fall back to a predictable "8 px per char" measurement)
// or a custom mock for precise text-width tests.
//
// drawText() is part of the same interface so paint code can emit text
// without the caller knowing whether it's hitting a real stbtt atlas
// or a mock. FontAdapter wires both via v1 Font's matching methods.
class TextMetrics {
public:
    virtual ~TextMetrics() = default;
    virtual float textWidth(const std::string& s, float fontSize) const = 0;
    virtual float lineHeight(float fontSize) const = 0;

    // Paint text at (x,y) in logical pixels. `y` is the top-left of the
    // first glyph's ascender box (matches v1 Font's drawText origin).
    // Renderer is passed in rather than stored so TextMetrics stays a
    // pure metrics/paint shim — no per-frame state.
    virtual void drawText(Renderer2D& r, const std::string& s,
                          float x, float y, float fontSize,
                          Color color) const = 0;

    // Click → caret: the UTF-8 byte offset of the codepoint whose
    // midpoint is nearest `x` (which is measured from the string's
    // LEFT edge — callers subtract their own text origin). Walks the
    // string codepoint-by-codepoint, so multi-byte chars select as a
    // unit. Past the end → s.size(). Virtual so mocks can override.
    virtual int byteOffsetAtX(const std::string& s, float fontSize,
                              float x) const {
        int col = 0;
        float px = 0.0f;
        while (col < static_cast<int>(s.size())) {
            const int next = col + utf8CharBytes(s, col);
            const float cw = textWidth(s.substr(col, next - col), fontSize);
            if (x < px + cw * 0.5f) return col;
            px += cw;
            col = next;
        }
        return col;
    }

    // UTF-8 codepoint byte-length at byte offset `i` (1 for ASCII).
    static int utf8CharBytes(const std::string& s, size_t i) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80)           return 1;
        if ((b & 0xE0) == 0xC0) return 2;
        if ((b & 0xF0) == 0xE0) return 3;
        if ((b & 0xF8) == 0xF0) return 4;
        return 1;   // malformed — treat as single byte so we can escape
    }
};

class UIContext {
public:
    // Singleton access — v2 widgets read from a single per-process
    // context. Tests can install a replacement via setGlobal.
    static UIContext& global();
    static void       setGlobal(UIContext* ctx);  // test-only hook; nullptr = reset

    UIContext() = default;

    // ─── Epoch ──────────────────────────────────────────────────────
    // Monotonically increasing counter. Widgets compare cached
    // `lastGlobalEpoch == epoch()` as part of their cache-hit check.
    int  epoch() const         { return m_epoch; }
    void bumpEpoch()           { ++m_epoch; }

    // ─── DPI ────────────────────────────────────────────────────────
    // Logical-to-physical pixel scale. 1.0 at 96 DPI, 2.0 at 192 DPI,
    // etc. Mouse deltas are divided by this in the gesture layer to
    // yield DPI-independent logical deltas.
    float dpiScale() const     { return m_dpiScale; }
    void  setDpiScale(float s) {
        if (s != m_dpiScale) {
            m_dpiScale = s;
            bumpEpoch();    // DPI change invalidates everything
        }
    }

    // ─── Rendering hooks ────────────────────────────────────────────
    // Pointers to the framework's 2D renderer + text metrics provider.
    // App wires them at startup. Null during unit tests → widgets use
    // predictable fallback measurements.
    Renderer2D*   renderer     = nullptr;
    TextMetrics*  textMetrics  = nullptr;

    // ─── LayerStack ────────────────────────────────────────────────
    // The scene root for floating UI (modals, dropdowns, tooltips,
    // toasts). Widgets that need to present an overlay read from this
    // pointer; App owns the actual LayerStack instance. Null in unit
    // tests that don't need overlay behaviour.
    LayerStack*   layerStack   = nullptr;

    // ─── Viewport ──────────────────────────────────────────────────
    // Full window bounds in logical pixels. App updates per-frame.
    // Widgets that position overlays (Dropdown flip, Tooltip clamp,
    // ContextMenu edge-shift) read this to know where the edges are.
    // Default is effectively unbounded so unit tests that don't set it
    // get no clamping; tests that exercise clamping/flip install a
    // realistic value.
    ::yawn::ui::fw::Rect viewport{0.0f, 0.0f, 10000.0f, 10000.0f};

private:
    int   m_epoch    = 0;
    float m_dpiScale = 1.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
