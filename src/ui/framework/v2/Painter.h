#pragma once

// UI v2 — Type-erased painter registry.
//
// v2 widget *logic* (measure / layout / events) lives in yawn_core and
// mustn't pull GL. But paint inherently calls into Renderer2D + Font,
// both of which include glad/gl.h. To keep that split clean, paint
// bodies live in the main executable and register themselves here,
// keyed by widget type_info. Widget::render() looks up the registered
// function for `typeid(*this)` and invokes it. Tests never register
// any painters → Widget::render() falls back to the virtual paint()
// no-op, and the render path is a safe walk.
//
// Add-a-widget flow:
//   1. Define the widget in yawn_core, logic only (measure, layout,
//      event handling). Don't override paint().
//   2. In the main exe, write a paint function `void paintX(Widget&,
//      UIContext&)` that downcasts + draws.
//   3. Call registerPainter(typeid(X), &paintX) once at startup.

#include <typeinfo>

namespace yawn {
namespace ui {
namespace fw2 {

class Widget;
class UIContext;

using PaintFn = void(*)(Widget&, UIContext&);

// Register a painter for a specific widget class. Subsequent calls for
// the same type_info overwrite (last-registration wins).
void registerPainter(const std::type_info& t, PaintFn fn);

// Look up the painter for a given widget type. Returns nullptr if no
// painter has been registered — callers should fall back to virtual
// paint() or simply skip painting.
PaintFn findPainter(const std::type_info& t);

// Drop all registered painters. Intended for test harnesses that want
// to isolate their environment; never called by the shipping app.
void clearPainters();

} // namespace fw2
} // namespace ui
} // namespace yawn
