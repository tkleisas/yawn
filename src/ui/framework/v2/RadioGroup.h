#pragma once

// UI v2 — FwRadioButton + FwRadioGroup.
//
// Radio button = mutually-exclusive counterpart to FwCheckbox. Two
// widgets cooperate:
//   • FwRadioButton is the leaf (indicator circle + label).
//   • FwRadioGroup owns N buttons and enforces single-selection.
//
// When a button sits inside a group, clicking it routes through the
// group (which unselects the previous selection + fires the group-
// level onChange). Standalone buttons — rare — behave like a
// self-toggling indicator with their own onChange.
//
// See docs/widgets/radio_group.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <memory>

namespace yawn {
namespace ui {
namespace fw2 {

class FwRadioGroup;

// ───────────────────────────────────────────────────────────────────
// FwRadioButton
// ───────────────────────────────────────────────────────────────────

class FwRadioButton : public Widget {
public:
    using StateCallback      = std::function<void(bool selected)>;
    using RightClickCallback = std::function<void(Point)>;

    FwRadioButton();
    explicit FwRadioButton(std::string label);

    void setLabel(std::string l);
    const std::string& label() const { return m_label; }

    void setSelected(bool s,
                     ValueChangeSource src = ValueChangeSource::Programmatic);
    bool isSelected() const { return m_selected; }

    void setAccentColor(Color c)          { m_accentOverride = c; }
    void clearAccentColor()               { m_accentOverride.reset(); }
    const std::optional<Color>& accentColor() const { return m_accentOverride; }

    // Callbacks — only fire when the button has NO parent group
    // (the group is authoritative for grouped buttons).
    void setOnChange(StateCallback cb)          { m_onChange     = std::move(cb); }
    void setOnRightClick(RightClickCallback cb) { m_onRightClick = std::move(cb); }

    // Group association — set by FwRadioGroup when the button is
    // adopted; not intended for direct caller use.
    void        _setGroup(FwRadioGroup* g) { m_group = g; }
    FwRadioGroup* _group() const           { return m_group; }

    void  setMinWidth(float w);
    float minWidth() const { return m_minWidth; }

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;

    void onClick(const ClickEvent& e)       override;
    void onRightClick(const ClickEvent& e)  override;
    bool onKeyDown(KeyEvent& e)             override;

private:
    float measureLabelWidth(UIContext& ctx) const;

    std::string          m_label;
    bool                 m_selected     = false;
    std::optional<Color> m_accentOverride;
    float                m_minWidth     = 0.0f;
    StateCallback        m_onChange;
    RightClickCallback   m_onRightClick;
    FwRadioGroup*        m_group        = nullptr;

    static constexpr float kFallbackPxPerChar = 8.0f;
};

// ───────────────────────────────────────────────────────────────────
// FwRadioGroup
// ───────────────────────────────────────────────────────────────────

enum class RadioOrientation {
    Horizontal,
    Vertical,
};

class FwRadioGroup : public Widget {
public:
    using SelectionCallback =
        std::function<void(int index, const std::string& label)>;

    FwRadioGroup();
    explicit FwRadioGroup(std::vector<std::string> options);
    ~FwRadioGroup() override;

    // ─── Options ──────────────────────────────────────────────────
    void addOption(std::string label);
    void clearOptions();
    int  optionCount() const { return static_cast<int>(m_buttons.size()); }
    const std::string& optionLabel(int idx) const;

    // Access a button (for testing or per-button styling tweaks).
    FwRadioButton* button(int idx) const;

    // ─── Selection ────────────────────────────────────────────────
    void setSelectedIndex(int idx,
        ValueChangeSource src = ValueChangeSource::Programmatic);
    int  selectedIndex() const { return m_selected; }
    const std::string& selectedLabel() const;   // "" if none

    // When true, clicking the already-selected button deselects
    // (index → -1). Default false.
    void setAllowDeselect(bool b) { m_allowDeselect = b; }
    bool allowDeselect() const    { return m_allowDeselect; }

    // ─── Layout ────────────────────────────────────────────────────
    void setOrientation(RadioOrientation o);
    RadioOrientation orientation() const { return m_orientation; }
    void setGap(float px);
    float gap() const { return m_gap; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(SelectionCallback cb) { m_onChange = std::move(cb); }

    // ─── Appearance (delegates to each button) ────────────────────
    void setAccentColor(Color c);
    void clearAccentColor();

    // Called by FwRadioButton when a click goes through it. Returns
    // true if the group consumed the click (always does — it either
    // selects/deselects, or it's a no-op on a grouped button).
    bool _notifyButtonClicked(FwRadioButton* btn, ValueChangeSource src);

public:
    // Buttons aren't in m_children (see cpp for why), so the default
    // Widget::render recursion wouldn't reach them — override to
    // render them explicitly.
    void render(UIContext& ctx) override;

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

private:
    void fireOnChangeIfUser(ValueChangeSource src);

    std::vector<std::unique_ptr<FwRadioButton>> m_buttons;
    int                m_selected       = -1;
    bool               m_allowDeselect  = false;
    RadioOrientation   m_orientation    = RadioOrientation::Horizontal;
    float              m_gap            = 0.0f;   // 0 = theme baseUnit × 2
    std::optional<Color> m_accentOverride;
    SelectionCallback  m_onChange;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
