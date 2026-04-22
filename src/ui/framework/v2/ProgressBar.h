#pragma once

// UI v2 — FwProgressBar.
//
// Non-interactive progress indicator. Two modes:
//   • Determinate   — setValue(0..1), fill grows proportionally.
//   • Indeterminate — setIndeterminate(), an animated sweep loops.
//
// Output-only; no onChange. Right-click hook is wired for cancel
// menus (caller decides what to do with the position).
//
// See docs/widgets/progress_bar.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

enum class ProgressOrientation {
    Horizontal,
    Vertical,
};

class FwProgressBar : public Widget {
public:
    using RightClickCallback = std::function<void(Point)>;

    FwProgressBar();
    explicit FwProgressBar(ProgressOrientation o);

    // ─── Determinate / Indeterminate mode ────────────────────────
    // setValue clamps to [0,1]. Silently ignored when in error state.
    void  setValue(float v,
                    ValueChangeSource src = ValueChangeSource::Programmatic);
    float value()          const { return m_value; }
    bool  isDeterminate()  const { return m_determinate; }

    void setIndeterminate();
    void setDeterminate();

    // ─── Error ─────────────────────────────────────────────────────
    void setError(bool e);
    bool hasError() const { return m_error; }

    // ─── Orientation ──────────────────────────────────────────────
    void setOrientation(ProgressOrientation o);
    ProgressOrientation orientation() const { return m_orientation; }

    // ─── Appearance ───────────────────────────────────────────────
    void setAccentColor(Color c)          { m_accentOverride = c; }
    void clearAccentColor()               { m_accentOverride.reset(); }
    const std::optional<Color>& accentColor() const { return m_accentOverride; }

    void setCompleteColor(Color c)        { m_completeOverride = c; }
    void clearCompleteColor()             { m_completeOverride.reset(); }
    const std::optional<Color>& completeColor() const { return m_completeOverride; }

    // Fade-out animation when value hits 1.0 (default on). Callers
    // that remove the widget themselves should turn fade off so their
    // removal logic runs first.
    void setCompleteFade(bool on)         { m_completeFade = on; }
    bool completeFade() const             { return m_completeFade; }

    void setThickness(float px);
    float thickness() const;

    // Indeterminate sweep cycle length.
    void  setSweepDurationMs(float ms)    { m_sweepDurationMs = ms; }
    float sweepDurationMs() const         { return m_sweepDurationMs; }

    // Sizing
    void  setMinLength(float px);
    float minLength() const { return m_minLength; }

    // Right-click hook (for cancel menus).
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // ─── Animation tick (per-frame) ──────────────────────────────
    // Advances the sweep / fade-out animations. Called by the framework
    // on each paint. Public so the painter can call straight through;
    // no side effects beyond updating internal timing state.
    void tick(float dtSec);

    // ─── Paint-side accessors ────────────────────────────────────
    // Sweep phase in [0..1] (0 = leftmost / topmost, 1 = rightmost /
    // bottommost).  Ignored when not indeterminate.
    float sweepPhase()   const { return m_sweepPhase; }
    float completeAlpha() const { return m_completeAlpha; }

    // True while the widget wants continuous repaints — indeterminate
    // sweep, or a complete-fade in progress. Framework polls this and
    // keeps paint scheduled while it's true.
    bool requiresContinuousRepaint() const;

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Right-click gesture only — no click / drag.
    void onRightClick(const ClickEvent& e) override;

private:
    // State
    float m_value         = 0.0f;
    bool  m_determinate   = true;
    bool  m_error         = false;

    // Orientation / sizing
    ProgressOrientation m_orientation = ProgressOrientation::Horizontal;
    float m_thickness     = 0.0f;    // 0 = use controlHeight × 0.3
    float m_minLength     = 60.0f;

    // Animation
    float m_sweepPhase        = 0.0f;
    float m_sweepDurationMs   = 1200.0f;
    float m_completeHoldTimer = 0.0f;   // counts up while at 1.0
    float m_completeFadeTimer = 0.0f;   // counts up during fade-out
    float m_completeAlpha     = 1.0f;
    bool  m_completeFade      = true;

    // Appearance
    std::optional<Color> m_accentOverride;
    std::optional<Color> m_completeOverride;

    RightClickCallback m_onRightClick;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
