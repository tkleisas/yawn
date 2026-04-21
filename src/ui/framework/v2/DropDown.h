#pragma once

// UI v2 — FwDropDown.
//
// Single-select dropdown (combobox). First real consumer of LayerStack:
// the popup menu is pushed as an OverlayEntry on OverlayLayer::Overlay
// when open, so it draws above everything else and dismisses on
// outside-click without any clip/paintOverlay hack.
//
// This TU is yawn_core (no GL). Paint bodies — both the button and the
// popup — live in Fw2Painters.cpp in the main exe. The button paints
// through the Painter registry (typeid(FwDropDown)); the popup calls
// a static function pointer the main exe installs via setPopupPainter.
// Tests leave the pointer null and the popup just doesn't render —
// perfectly fine since we can't test pixels in yawn_tests anyway.
//
// See docs/widgets/dropdown.md for the full spec. This first-pass
// implementation ships the spec's core contract (items, selection,
// open/close/toggle, keyboard nav, scroll-to-change, anchor + flip,
// LayerStack dismiss) and defers the more ornamental bits (type-ahead
// search, PgUp/PgDn, icons, scroll bar, virtualization).

#include "Widget.h"
#include "Theme.h"
#include "LayerStack.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class FwDropDown : public Widget {
public:
    using ChangeCallback     = std::function<void(int index, const std::string& label)>;
    using RightClickCallback = std::function<void(Point screen)>;

    // Popup painter hook — installed once at app startup by
    // registerAllFw2Painters(). Kept as a free function pointer so
    // yawn_core has zero link dependency on the main exe (tests set
    // nothing → null → popup no-ops visually).
    using PopupPaintFn = void(*)(const FwDropDown&, UIContext&);
    static void         setPopupPainter(PopupPaintFn fn);
    static PopupPaintFn popupPainter();

    struct Item {
        std::string label;
        bool        enabled   = true;
        bool        separator = false;   // draws a divider row, not selectable
    };

    FwDropDown();
    explicit FwDropDown(std::vector<std::string> items);
    ~FwDropDown() override;

    // ─── Items ────────────────────────────────────────────────────
    // setItems() replaces — if the selected index is no longer valid
    // it's clamped to -1 (placeholder shown).
    void setItems(std::vector<std::string> items);
    void setItems(std::vector<Item>        items);
    void addItem(std::string label);
    void clearItems();
    int  itemCount() const { return static_cast<int>(m_items.size()); }
    const std::vector<Item>& items() const { return m_items; }

    void setItemEnabled(int idx, bool enabled);
    bool isItemEnabled(int idx) const;
    void setItemSeparator(int idx, bool separator);
    bool isItemSeparator(int idx) const;
    const std::string& itemLabel(int idx) const;

    // ─── Selection ────────────────────────────────────────────────
    void setSelectedIndex(int idx,
        ValueChangeSource src = ValueChangeSource::Programmatic);
    int  selectedIndex() const { return m_selected; }
    const std::string& selectedLabel() const;   // empty string if none

    // ─── Placeholder ──────────────────────────────────────────────
    void setPlaceholder(std::string p);
    const std::string& placeholder() const { return m_placeholder; }

    // ─── Popup config ─────────────────────────────────────────────
    void setMaxVisibleItems(int n);
    int  maxVisibleItems() const { return m_maxVisibleItems; }
    void setItemHeight(float h);
    float itemHeight() const;

    // ─── Behavior ────────────────────────────────────────────────
    // When true (default), scrolling over the closed button cycles
    // the selection ±1. When false, scroll is passed through.
    void setScrollChangesSelection(bool b) { m_scrollChangesSelection = b; }
    bool scrollChangesSelection() const    { return m_scrollChangesSelection; }

    // ─── Appearance ───────────────────────────────────────────────
    void setAccentColor(Color c)          { m_accentOverride = c; }
    void clearAccentColor()               { m_accentOverride.reset(); }
    const std::optional<Color>& accentColor() const { return m_accentOverride; }

    void setMinWidth(float w);
    void setPreferredWidth(float w);
    float minWidth()       const { return m_minWidth; }
    float preferredWidth() const { return m_preferredWidth; }

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnChange(ChangeCallback cb)           { m_onChange = std::move(cb); }
    void setOnRightClick(RightClickCallback cb)   { m_onRightClick = std::move(cb); }

    // ─── Open / close ─────────────────────────────────────────────
    // open() is a no-op if already open or no LayerStack is wired.
    // Opening pushes an OverlayEntry; the returned handle lives in
    // m_popupHandle until close() / destructor fires.
    void open();
    void close();
    void toggle();
    bool isOpen() const { return m_popupHandle.active(); }

    // ─── Paint-side accessors ────────────────────────────────────
    // Popup rect (screen coords). Only valid while isOpen() is true —
    // computed when the popup is pushed, not live. Null/undefined
    // when closed (returns m_popupBounds as last computed).
    const Rect& popupBounds()     const { return m_popupBounds; }
    bool        popupOpensUpward() const { return m_popupOpensUpward; }
    int         highlightedIndex() const { return m_highlighted; }
    int         scrollOffset()    const { return m_scrollOffset; }

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    void onClick(const ClickEvent& e)      override;
    void onRightClick(const ClickEvent& e) override;
    bool onKeyDown(KeyEvent& e)            override;
    bool onScroll(ScrollEvent& e)          override;

private:
    // Popup input handlers — called by the OverlayEntry closures the
    // LayerStack dispatches to while open. Return true to consume.
    bool popupOnMouseDown(MouseEvent& e);
    bool popupOnMouseMove(MouseMoveEvent& e);
    bool popupOnScroll(ScrollEvent& e);
    bool popupOnKey(KeyEvent& e);
    void popupOnDismiss();

    // Geometry helpers.
    void  computePopupRect(UIContext& ctx);   // sets m_popupBounds + m_popupOpensUpward
    float measureLongestItemWidth(UIContext& ctx) const;
    int   visibleRowCount() const;
    int   indexAtPopupY(float popupLocalY) const;
    void  clampScrollToHighlight();

    // Selection helpers. Move highlight to the next / previous
    // SELECTABLE item (skipping disabled + separators). Wraps at ends.
    int stepHighlight(int from, int direction) const;

    // Fire onChange if src != Automation (matches FwFader convention).
    void fireOnChange(ValueChangeSource src);

    // State
    std::vector<Item>     m_items;
    int                   m_selected          = -1;
    std::string           m_placeholder       = "Select...";

    // Popup config
    int                   m_maxVisibleItems   = 10;
    std::optional<float>  m_itemHeightOverride;
    bool                  m_scrollChangesSelection = true;

    // Appearance
    std::optional<Color>  m_accentOverride;
    float                 m_minWidth          = 0.0f;
    float                 m_preferredWidth    = 0.0f;

    // Callbacks
    ChangeCallback        m_onChange;
    RightClickCallback    m_onRightClick;

    // Popup state — only meaningful while open.
    OverlayHandle         m_popupHandle;
    Rect                  m_popupBounds{};
    bool                  m_popupOpensUpward = false;
    int                   m_highlighted     = -1;   // row being hover-highlighted
    int                   m_scrollOffset    = 0;    // first visible index
};

} // namespace fw2
} // namespace ui
} // namespace yawn
