#pragma once

// UI v2 — UIContext.
//
// Per-frame / per-process data the widget tree + event dispatch need.
// Also owns the global epoch counter used for measure / layout cache
// invalidation (bumped on DPI change, theme swap, font reload).
//
// See docs/ui-v2-measure-layout.md for how epoch interacts with
// caching.

namespace yawn {
namespace ui {

// Forward-decl v1 types consumed through pointers.
class Renderer2D;
class Font;

namespace fw2 {

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
    // Pointers to the framework's 2D renderer + font. Plumbed through
    // by App so widgets can paint via ctx.renderer / ctx.font. Null
    // during unit tests that don't render.
    Renderer2D* renderer = nullptr;
    Font*       font     = nullptr;

private:
    int   m_epoch    = 0;
    float m_dpiScale = 1.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
