#pragma once

// UI v2 — FwCheckbox.
//
// Tri-state checkbox: Off / On / Indeterminate. Click flips Off↔On;
// Indeterminate is set programmatically only (parent-of-mixed-children
// convention). Label right of the box, clicking either flips.
//
// Leaf widget — no LayerStack. Matches Toggle's callback shape and
// paint-path split (logic here in yawn_core, painter in Fw2Painters).
//
// See docs/widgets/checkbox.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

enum class CheckState {
    Off,
    On,
    Indeterminate,
};

class FwCheckbox : public Widget {
public:
    using StateCallback      = std::function<void(CheckState)>;
    using RightClickCallback = std::function<void(Point)>;

    FwCheckbox();
    explicit FwCheckbox(std::string label);

    // ─── Content ──────────────────────────────────────────────────
    void setLabel(std::string l);
    const std::string& label() const { return m_label; }

    // ─── State ────────────────────────────────────────────────────
    // User clicks cycle Off ↔ On (Indeterminate rolls to On). Callers
    // put the box into Indeterminate programmatically via
    // setIndeterminate() when the underlying state is "mixed" — e.g.
    // a parent-of-children master checkbox in a preferences tree.
    void setChecked(bool on, ValueChangeSource src = ValueChangeSource::Programmatic);
    void setIndeterminate(ValueChangeSource src = ValueChangeSource::Programmatic);
    CheckState state()     const { return m_state; }
    bool       isChecked() const { return m_state == CheckState::On; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(StateCallback cb)          { m_onChange = std::move(cb); }
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setAccentColor(Color c) { m_accentOverride = c; }
    void clearAccentColor()      { m_accentOverride.reset(); }
    const std::optional<Color>& accentColor() const { return m_accentOverride; }

    // ─── Sizing ───────────────────────────────────────────────────
    void  setMinWidth(float w);
    float minWidth() const { return m_minWidth; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    void onClick(const ClickEvent& e)      override;
    void onRightClick(const ClickEvent& e) override;
    bool onKeyDown(KeyEvent& e)            override;

private:
    void assignState(CheckState s, ValueChangeSource src);
    float measureLabelWidth(UIContext& ctx) const;

    // Content
    std::string m_label;

    // State
    CheckState m_state = CheckState::Off;

    // Appearance
    std::optional<Color> m_accentOverride;
    float                m_minWidth = 0.0f;

    // Callbacks
    StateCallback      m_onChange;
    RightClickCallback m_onRightClick;

    static constexpr float kFallbackPxPerChar = 8.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
