#pragma once

// UI v2 — FwToggle.
//
// Boolean on/off control with two visual variants:
//   Button — rectangular button that fills with accent when on.
//            Label centred. Content-driven width. Matches FwButton
//            geometry so a row of toggles + buttons lines up.
//   Switch — pill-shaped track with a knob that slides from left
//            (off) to right (on). Fixed compact width; no label on
//            the widget itself (use a Label alongside).
//
// Leaf widget — doesn't use LayerStack. Clicks flip state, fire
// onChange. Scroll-flips behaviour is opt-in (disabled by default
// to avoid surprise interactions with vertical scrolling).
//
// First pass: both variants paint statically. The 120 ms knob-slide
// animation the spec calls out is deferred — the Switch variant
// snaps into place today.
//
// See docs/widgets/toggle.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

enum class ToggleVariant {
    Button,   // rectangular button, accent fill when on
    Switch,   // pill track + knob
};

class FwToggle : public Widget {
public:
    using StateCallback      = std::function<void(bool)>;
    using RightClickCallback = std::function<void(Point)>;

    FwToggle();
    explicit FwToggle(std::string label);

    // ─── Content ──────────────────────────────────────────────────
    void setLabel(std::string l);
    const std::string& label() const { return m_label; }

    // ─── State ────────────────────────────────────────────────────
    // setState follows the Fader / DropDown convention —
    // ValueChangeSource=User fires callback, Programmatic skips it
    // to avoid accidental feedback loops, Automation is silent.
    void setState(bool on, ValueChangeSource src = ValueChangeSource::Programmatic);
    bool state() const { return m_on; }
    void toggle() { setState(!m_on, ValueChangeSource::User); }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(StateCallback cb)          { m_onChange = std::move(cb); }
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setVariant(ToggleVariant v);
    ToggleVariant variant() const { return m_variant; }

    void setAccentColor(Color c) { m_accentOverride = c; }
    void clearAccentColor()      { m_accentOverride.reset(); }
    const std::optional<Color>& accentColor() const { return m_accentOverride; }

    // ─── Behaviour ────────────────────────────────────────────────
    // Off by default — scrolling over a toggle is usually an
    // accident when the user meant to scroll the containing panel.
    void setScrollFlipsState(bool b) { m_scrollFlipsState = b; }
    bool scrollFlipsState() const    { return m_scrollFlipsState; }

    // ─── Sizing ───────────────────────────────────────────────────
    void  setMinWidth(float w);
    void  setFixedWidth(float w);
    float minWidth()   const { return m_minWidth; }
    float fixedWidth() const { return m_fixedWidth; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    void onClick(const ClickEvent& e)      override;
    void onRightClick(const ClickEvent& e) override;
    bool onKeyDown(KeyEvent& e)            override;
    bool onScroll(ScrollEvent& e)          override;

private:
    float measureLabelWidth(UIContext& ctx) const;

    // Content
    std::string m_label;

    // State
    bool m_on = false;

    // Appearance
    ToggleVariant         m_variant = ToggleVariant::Button;
    std::optional<Color>  m_accentOverride;

    // Behaviour
    bool  m_scrollFlipsState = false;
    float m_minWidth   = 0.0f;
    float m_fixedWidth = 0.0f;    // 0 = content-driven

    // Callbacks
    StateCallback        m_onChange;
    RightClickCallback   m_onRightClick;

    static constexpr float kFallbackPxPerChar = 8.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
