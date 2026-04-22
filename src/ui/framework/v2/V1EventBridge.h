#pragma once

// UI v2 — v1 → fw2 event translation helpers.
//
// Purpose: v1 panels (ui::fw::Widget subclasses) need to forward mouse
// events to embedded v2 widgets that rely on the gesture state machine
// (FwKnob, FwFader — anything with drag semantics). The v2 widgets
// consume their own event types; this header provides the small
// header-only bridge so hosts don't have to hand-translate fields
// at each call site.
//
// Pattern:
//   1. In the v1 panel's onMouseDown, when a click lands inside a
//      v2 widget's bounds, call captureMouse() (v1) so the v1 App
//      event pump routes subsequent moves back here, and store a
//      pointer to the "currently-dragging v2 widget".
//   2. In onMouseMove/Up, forward translated events via the dragged
//      widget's dispatchMouseMove / dispatchMouseUp. On release,
//      clear the pointer and releaseMouse().
//
// Widget pointer tracking is up to the host — this header just
// provides the event translations.

#include "Widget.h"                     // fw2::MouseEvent, MouseMoveEvent
#include "ui/framework/EventSystem.h"   // v1::MouseEvent, MouseMoveEvent, MouseButton

namespace yawn {
namespace ui {
namespace fw2 {

inline uint64_t v1BridgeNowMs() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}

inline MouseButton toFw2Button(::yawn::ui::fw::MouseButton b) {
    switch (b) {
        case ::yawn::ui::fw::MouseButton::Right:  return MouseButton::Right;
        case ::yawn::ui::fw::MouseButton::Middle: return MouseButton::Middle;
        default:                                   return MouseButton::Left;
    }
}

inline uint16_t toFw2Modifiers(const ::yawn::ui::fw::Modifiers& m) {
    using namespace ModifierKey;
    uint16_t out = None;
    if (m.shift) out |= Shift;
    if (m.ctrl)  out |= Ctrl;
    if (m.alt)   out |= Alt;
    return out;
}

inline MouseEvent toFw2Mouse(const ::yawn::ui::fw::MouseEvent& e, Rect widgetBounds) {
    MouseEvent out{};
    out.x  = e.x;
    out.y  = e.y;
    out.lx = e.x - widgetBounds.x;
    out.ly = e.y - widgetBounds.y;
    out.button    = toFw2Button(e.button);
    out.modifiers = toFw2Modifiers(e.mods);
    out.timestampMs = v1BridgeNowMs();
    return out;
}

inline MouseMoveEvent toFw2MouseMove(const ::yawn::ui::fw::MouseMoveEvent& e,
                                      Rect widgetBounds) {
    MouseMoveEvent out{};
    out.x  = e.x;
    out.y  = e.y;
    out.lx = e.x - widgetBounds.x;
    out.ly = e.y - widgetBounds.y;
    out.dx = e.dx;
    out.dy = e.dy;
    out.modifiers = toFw2Modifiers(e.mods);
    out.timestampMs = v1BridgeNowMs();
    return out;
}

// ─── fw2 → v1 converters ────────────────────────────────────────────
//
// Opposite direction: a fw2::Widget panel that needs to call into a
// v1 sub-widget (tab body, legacy envelope editor, etc.) converts its
// inbound fw2 event to v1 at the boundary using these.

inline ::yawn::ui::fw::MouseButton toFw1Button(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return ::yawn::ui::fw::MouseButton::Right;
        case MouseButton::Middle: return ::yawn::ui::fw::MouseButton::Middle;
        default:                   return ::yawn::ui::fw::MouseButton::Left;
    }
}

inline ::yawn::ui::fw::Modifiers toFw1Modifiers(uint16_t m) {
    using namespace ModifierKey;
    ::yawn::ui::fw::Modifiers out{};
    out.shift = (m & Shift) != 0;
    out.ctrl  = (m & Ctrl)  != 0;
    out.alt   = (m & Alt)   != 0;
    return out;
}

inline ::yawn::ui::fw::MouseEvent toFw1Mouse(const MouseEvent& e) {
    ::yawn::ui::fw::MouseEvent out{};
    out.x = e.x;
    out.y = e.y;
    out.button    = toFw1Button(e.button);
    out.mods      = toFw1Modifiers(e.modifiers);
    out.clickCount = 1;
    return out;
}

inline ::yawn::ui::fw::MouseMoveEvent toFw1MouseMove(const MouseMoveEvent& e) {
    ::yawn::ui::fw::MouseMoveEvent out{};
    out.x = e.x;
    out.y = e.y;
    out.dx = e.dx;
    out.dy = e.dy;
    out.mods = toFw1Modifiers(e.modifiers);
    return out;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
