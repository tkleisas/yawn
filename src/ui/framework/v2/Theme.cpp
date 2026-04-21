#include "Theme.h"

#include "UIContext.h"

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
Theme& mutableCurrentTheme() {
    static Theme g_theme{};   // default-initialized = default dark theme
    return g_theme;
}
} // anon

const Theme& theme() {
    return mutableCurrentTheme();
}

void setTheme(Theme newTheme) {
    mutableCurrentTheme() = std::move(newTheme);
    // Invalidate all measure caches by bumping the global epoch.
    // Metrics changes (font size, control height) can affect
    // measured sizes; palette-only changes are paint-only but we
    // bump anyway for simplicity. Future optimization: compare
    // old vs new metrics and skip the bump when only palette changed.
    UIContext::global().bumpEpoch();
}

} // namespace fw2
} // namespace ui
} // namespace yawn
