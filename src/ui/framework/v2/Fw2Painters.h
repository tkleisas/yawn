#pragma once

// UI v2 — paint-function bootstrap for the main executable.
//
// Call registerAllFw2Painters() once after the v2 UIContext's renderer
// + textMetrics are wired. The function populates the Painter registry
// so that Widget::render() can look up per-type paint bodies during
// the main render loop.
//
// Implementation lives in Fw2Painters.cpp, compiled only into YAWN
// (not yawn_core) since it includes v1 Renderer2D + Font headers which
// pull glad/GL.

#include "ui/framework/v2/Types.h"   // Rect

namespace yawn {
namespace ui {
namespace fw2 {

class UIContext;

void registerAllFw2Painters();

// Paint the modal scrim (full-viewport tinted rect) using the renderer
// in `ctx`. Call this between the main tree paint and
// LayerStack::paintLayers iff layerStack->hasModalActive() is true.
// Lives in Fw2Painters.cpp because it calls Renderer2D::drawRect,
// which would pull glad into yawn_core.
void paintModalScrim(UIContext& ctx, ::yawn::ui::fw::Rect viewport);

} // namespace fw2
} // namespace ui
} // namespace yawn
