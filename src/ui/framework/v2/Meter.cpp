#include "Meter.h"
#include "UIContext.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

FwMeter::FwMeter() {
    setSizePolicy(SizePolicy::flex(1.0f));
    setRelayoutBoundary(true);
    setFocusable(false);
}

Size FwMeter::onMeasure(Constraints c, UIContext& /*ctx*/) {
    // Narrow vertical strip. Width accommodates L + gap + R with a
    // minimum that keeps the bars visible; height flexes.
    float w = 16.0f;
    float h = 60.0f;
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

} // namespace fw2
} // namespace ui
} // namespace yawn
