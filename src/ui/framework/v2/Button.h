#pragma once

// UI v2 — FwButton.
//
// Rectangular clickable element with label + optional icon. No value;
// caller wires onClick and maintains its own state.
//
// Validates the gesture layer for value-free widgets: click detection
// (press + release without drag-over-threshold), right-click, double-
// click, cancel-on-pointer-out + drag-back recovery.
//
// See docs/widgets/button.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class FwButton : public Widget {
public:
    using ClickCallback      = std::function<void()>;
    using RightClickCallback = std::function<void(Point screen)>;

    FwButton();
    explicit FwButton(std::string label);

    // ─── Content ──────────────────────────────────────────────────
    void setLabel(std::string label);
    const std::string& label() const { return m_label; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnClick(ClickCallback cb)            { m_onClick = std::move(cb); }
    void setOnDoubleClick(ClickCallback cb)      { m_onDoubleClick = std::move(cb); }
    void setOnRightClick(RightClickCallback cb)  { m_onRightClick = std::move(cb); }

    // ─── Appearance ───────────────────────────────────────────────
    void setHighlighted(bool h);          // persistent accent state
    bool isHighlighted() const            { return m_highlighted; }
    void setAccentColor(Color c)          { m_accentOverride = c; }
    void clearAccentColor()               { m_accentOverride.reset(); }

    // ─── Behavior ────────────────────────────────────────────────
    void setCancelOnPointerOut(bool b)    { m_cancelOnPointerOut = b; }
    void setMinWidth(float w);
    void setFixedWidth(float w);

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Gesture callbacks
    void onClick(const ClickEvent& e) override;
    void onDoubleClick(const ClickEvent& e) override;
    void onRightClick(const ClickEvent& e) override;

private:
    // Text measurement (uses UIContext::font with fallback for tests).
    float measureLabelWidth(UIContext& ctx) const;

    // Content
    std::string m_label;

    // Appearance state
    bool m_highlighted = false;
    std::optional<Color> m_accentOverride;

    // Behavior
    bool  m_cancelOnPointerOut = true;
    float m_minWidth    = 0.0f;
    float m_fixedWidth  = 0.0f;   // 0 = content-driven

    // Callbacks
    ClickCallback      m_onClick;
    ClickCallback      m_onDoubleClick;
    RightClickCallback m_onRightClick;

    static constexpr float kFallbackPxPerChar = 8.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
