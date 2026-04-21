#pragma once

// UI v2 — Tooltip.
//
// A hover-triggered info bubble that rides OverlayLayer::Tooltip on the
// LayerStack. Callers register tooltip text against widget pointers via
// Tooltip::attach(w, "text"); the manager watches the global pointer
// position, waits out a show delay, then pushes an overlay that paints
// the bubble until the pointer leaves.
//
// Scope for this first pass:
//   • Single active tooltip (per spec: only one visible at a time).
//   • Fixed show delay (0.6 s by default; setShowDelay lets callers
//     tweak without extending the API).
//   • 4-direction fallback positioning (Below → Above → Right → Left),
//     with viewport clamp.
//   • Plain text only. Word-wrap, fade-in/out animation, and the
//     1.5 s warm-up delay are intentionally deferred to a follow-up.
//
// Threading: single-threaded UI thread only. Static singleton is fine
// because the whole UI stack is single-threaded today; when we move
// async rendering in we'll box this into UIContext.

#include "Widget.h"          // Rect, Widget
#include "LayerStack.h"      // OverlayHandle

#include <string>
#include <unordered_map>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// TooltipManager — the internal engine. Most callers use the Tooltip
// facade below instead of touching this directly.
// ───────────────────────────────────────────────────────────────────
class TooltipManager {
public:
    static TooltipManager& instance();

    TooltipManager(const TooltipManager&)            = delete;
    TooltipManager& operator=(const TooltipManager&) = delete;

    // ─── Registration ─────────────────────────────────────────────
    // Associate a tooltip string with a widget pointer. Subsequent
    // attach() on the same widget overwrites. Passing empty text is
    // treated as detach (no tooltip will show).
    //
    // Widget lifetime: the manager must outlive the widget OR the
    // widget must detach before destruction. Widget::~Widget does an
    // automatic detach, so in practice callers rarely need to.
    void attach(Widget* w, std::string text);
    void detach(Widget* w);
    bool isAttached(Widget* w) const;

    // Forget everything. Invoked from tests to isolate state; never
    // called in the shipping app.
    void clearAll();

    // ─── Frame hooks ──────────────────────────────────────────────
    // App mainloop: call once per frame with the seconds since the
    // previous tick, and on every pointer movement with screen coords.
    // Pass coordinates outside every attached widget's bounds (or call
    // onPointerLeft()) to indicate "no tooltip target".
    void tick(float dtSec);
    void onPointerMoved(float sx, float sy);
    void onPointerLeft();

    // ─── Configuration ───────────────────────────────────────────
    void  setShowDelay(float sec) { m_showDelay = sec; }
    float showDelay() const       { return m_showDelay; }

    // ─── Painter hook ────────────────────────────────────────────
    // The main executable installs the paint function at startup
    // (Fw2Painters.cpp). Tests leave it null and the bubble simply
    // doesn't render — same shape as DropDown's popup painter.
    using PaintFn = void(*)(const Rect& bounds, const std::string& text, UIContext& ctx);
    static void    setPainter(PaintFn fn);
    static PaintFn painter();

    // ─── Diagnostics ─────────────────────────────────────────────
    Widget*            currentHover() const   { return m_hover; }
    bool               isVisible() const      { return m_handle.active(); }
    const std::string& currentText() const    { return m_currentText; }
    const Rect&        currentBounds() const  { return m_currentBounds; }
    float              hoverTimer() const     { return m_hoverTimer; }

private:
    TooltipManager() = default;

    // Build the overlay entry + push onto LayerStack.
    void showFor(Widget* w, UIContext& ctx);
    // Remove the overlay (no-op if not visible). Keeps m_hover intact
    // so the caller can decide whether to restart the timer.
    void hideOverlay();

    std::unordered_map<Widget*, std::string> m_tooltips;

    // Currently-hovered attached widget (or null).
    Widget*       m_hover      = nullptr;
    // Seconds since m_hover became the hovered target.
    float         m_hoverTimer = 0.0f;
    // Configurable — 0.6 s matches native tooltip UX.
    float         m_showDelay  = 0.6f;
    // Handle to the active OverlayEntry while visible.
    OverlayHandle m_handle;
    // Snapshot at show-time so the paint closure sees stable values
    // even if the underlying widget moves mid-hover.
    std::string   m_currentText;
    Rect          m_currentBounds{};
};

// ───────────────────────────────────────────────────────────────────
// Tooltip — convenience facade over TooltipManager.
//   Tooltip::attach(&myWidget, "Click to expand");
// ───────────────────────────────────────────────────────────────────
class Tooltip {
public:
    Tooltip() = delete;

    static void attach(Widget* w, std::string text) {
        TooltipManager::instance().attach(w, std::move(text));
    }
    static void detach(Widget* w) {
        TooltipManager::instance().detach(w);
    }
    static TooltipManager& manager() { return TooltipManager::instance(); }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
