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

namespace yawn {
namespace ui {
namespace fw2 {

void registerAllFw2Painters();

} // namespace fw2
} // namespace ui
} // namespace yawn
