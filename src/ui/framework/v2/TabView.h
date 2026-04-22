#pragma once

// UI v2 — TabView.
//
// Tab strip + content area. One tab active at a time; clicking a tab
// swaps the visible content widget.
//
// First-pass scope covers the core "category switcher" pattern:
//   • addTab(id, label, content)
//   • setActiveTab(id) / activeTabId() / selectNext/PrevTab()
//   • Click on tab strip switches active tab
//   • onActivated callback
//   • Ctrl+Tab / Ctrl+Shift+Tab keyboard navigation
//
// Deferred (per spec Open Questions): close buttons, icons, disabled
// tabs, strip scrolling-on-overflow, Ctrl+1..9, right-click menus,
// vertical orientation, indicator-bar slide animation.
//
// See docs/widgets/tab_view.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class TabView : public Widget {
public:
    using IdCallback = std::function<void(const std::string& tabId)>;

    TabView();
    ~TabView() override;

    // ─── Tab management ──────────────────────────────────────────
    // Adds a tab. If this is the first tab it becomes active. Content
    // must outlive the TabView (TabView does NOT take ownership).
    void addTab(std::string id, std::string label, Widget* content);

    // Removes the tab with the given id. If the removed tab was
    // active, activation cascades to the next/previous tab (or to ""
    // when no tabs remain).
    void removeTab(const std::string& id);
    void clearTabs();

    int tabCount() const { return static_cast<int>(m_tabs.size()); }

    // Label mutation (re-measures the strip).
    void setTabLabel(const std::string& id, std::string label);

    // ─── Active tab ──────────────────────────────────────────────
    void setActiveTab(const std::string& id,
                       ValueChangeSource src = ValueChangeSource::Programmatic);
    const std::string& activeTabId() const;
    int                activeTabIndex() const { return m_activeIdx; }
    void selectNextTab();
    void selectPrevTab();

    // ─── Appearance / sizing ────────────────────────────────────
    void  setTabStripHeight(float px);
    float tabStripHeight() const;

    void  setTabMinWidth(float px);
    float tabMinWidth() const { return m_tabMinWidth; }

    // ─── Callbacks ──────────────────────────────────────────────
    void setOnActivated(IdCallback cb) { m_onActivated = std::move(cb); }

    // ─── Paint-side accessors (for Fw2Painters) ────────────────
    struct TabEntry {
        std::string id;
        std::string label;
        Widget*     content = nullptr;
        // Filled in by onLayout. Strip rect in absolute screen coords.
        Rect        stripRect{};
    };
    const std::vector<TabEntry>& tabs() const { return m_tabs; }
    int  hoverIndex() const { return m_hoverIdx; }
    Rect contentAreaRect() const { return m_contentArea; }

public:
    // Override render so the strip paints via the registered painter
    // AND only the active tab's content recurses.
    void render(UIContext& ctx) override;

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

    bool onMouseDown(MouseEvent& e) override;
    bool onMouseMove(MouseMoveEvent& e) override;
    bool onKeyDown(KeyEvent& e) override;

private:
    int  indexOfId(const std::string& id) const;
    int  hitTestStrip(float sx, float sy) const;
    void activate(int idx, ValueChangeSource src);
    void rebuildVisibility();
    float measureTabWidth(const TabEntry& t, UIContext& ctx) const;

    std::vector<TabEntry> m_tabs;
    int                   m_activeIdx = -1;
    int                   m_hoverIdx  = -1;

    float m_stripHeight    = 0.0f;   // 0 = theme-driven
    float m_tabMinWidth    = 60.0f;
    Rect  m_stripRect{};
    Rect  m_contentArea{};

    IdCallback m_onActivated;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
