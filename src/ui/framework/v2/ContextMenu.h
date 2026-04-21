#pragma once

// UI v2 — ContextMenu.
//
// Popup menu with optional submenus. Sibling of FwDropDown's popup and
// Tooltip on OverlayLayer::Overlay — all three share the LayerStack
// plumbing, just with different input rules.
//
// Architecture: one OverlayEntry for the whole menu chain. Every level
// (root + open submenus) is stored inside ContextMenuManager; the
// entry's paint closure draws all levels, customHitTest asks whether
// the point is in ANY level, and onMouseDown/Move/Key route to the
// right level based on hit-test. Outside-click dismissal then closes
// every level at once (single LayerStack entry → single remove() call).
//
// Scope for this first pass:
//   • Item / Submenu / Separator / Header / Checkable / Radio.
//   • Root positions at a screen point; shifts left/up if clipped.
//   • Submenus open to the right of the parent row; flip left when
//     clipped. Shift up to stay in viewport.
//   • Mouse: hover highlights, hover-over-submenu-row opens submenu
//     after 200 ms; click on submenu row opens immediately; click on
//     Item/Checkable/Radio fires its callback and closes the chain.
//   • Keyboard: Up/Down navigate (skip disabled/separator/header);
//     Right opens submenu; Left closes topmost submenu; Enter/Space
//     activates highlighted row; Escape closes topmost level (or the
//     whole chain if at root).
//   • Outside-click dismisses the entire chain.
//
// Deferred: type-ahead search, top-level accelerator keys that fire
// items without opening the menu, icons (space reserved, texture hook
// comes later), and window-blur auto-dismiss.
//
// See docs/widgets/context_menu.md for the full spec.

#include "LayerStack.h"   // OverlayHandle
#include "Widget.h"       // Rect, Point, KeyEvent, MouseEvent

#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Entry model
// ───────────────────────────────────────────────────────────────────

enum class MenuEntryKind {
    Item,       // regular action
    Submenu,    // expands to children on hover/click
    Separator,  // thin divider line — non-selectable
    Header,     // group label — non-selectable, dimmer text
    Checkable,  // shows checkmark when `checked` true
    Radio,      // member of `radioGroup`; caller toggles + untoggles siblings
};

struct MenuEntry {
    MenuEntryKind kind    = MenuEntryKind::Item;
    std::string   label;
    std::string   shortcut;     // display-only (e.g. "Ctrl+S"); accelerator
                                 // firing is a future concern.
    std::function<void()> onClick;
    std::vector<MenuEntry> children;   // populated for Submenu kind

    bool        enabled     = true;
    bool        checked     = false;   // Checkable / Radio
    std::string radioGroup;             // Radio only — caller's convention
};

// Small factory helpers so call sites read linearly:
//
//   using namespace fw2::Menu;
//   ContextMenu::show({
//       item("Save",       [&]{ save(); },  "Ctrl+S"),
//       item("Save As...", [&]{ saveAs(); }),
//       separator(),
//       submenu("Export", {
//           item("As WAV", [&]{ exportWav(); }),
//           item("As MP3", [&]{ exportMp3(); }),
//       }),
//   }, {mouseX, mouseY});
//
namespace Menu {
MenuEntry item     (std::string label, std::function<void()> action,
                     std::string shortcut = "");
MenuEntry submenu  (std::string label, std::vector<MenuEntry> children);
MenuEntry separator();
MenuEntry header   (std::string label);
MenuEntry checkable(std::string label, bool checked,
                     std::function<void()> action,
                     std::string shortcut = "");
MenuEntry radio    (std::string group, std::string label, bool checked,
                     std::function<void()> action);
} // namespace Menu

// ───────────────────────────────────────────────────────────────────
// Manager (singleton) + static facade
// ───────────────────────────────────────────────────────────────────

class ContextMenuManager {
public:
    // One level = one open menu (root or submenu). Stored in a vector;
    // root is at index 0, submenus stack on top.
    struct Level {
        std::vector<MenuEntry> entries;
        Rect  bounds{};           // screen coords
        int   highlighted    = -1; // keyboard focus row
        int   openSubmenuRow = -1; // row in this level whose submenu is open
    };

    static ContextMenuManager& instance();

    ContextMenuManager(const ContextMenuManager&)            = delete;
    ContextMenuManager& operator=(const ContextMenuManager&) = delete;

    // ─── Lifecycle ────────────────────────────────────────────────
    // show() replaces any previously-open menu. Pass an empty entries
    // vector to have it effectively no-op after a close().
    void show(std::vector<MenuEntry> entries, Point screenPos);
    void close();
    bool isOpen() const { return !m_levels.empty(); }

    // ─── Per-frame tick ───────────────────────────────────────────
    // Advances the hover-to-open submenu timer. Call once per frame
    // with seconds since the previous call.
    void tick(float dtSec);

    // ─── Configuration ───────────────────────────────────────────
    void  setSubmenuHoverDelay(float sec) { m_submenuHoverDelay = sec; }
    float submenuHoverDelay() const       { return m_submenuHoverDelay; }

    // ─── Accessors (used by painters + tests) ─────────────────────
    int           levelCount() const { return static_cast<int>(m_levels.size()); }
    const Level&  level(int i) const { return m_levels[i]; }
    const std::vector<Level>& levels() const { return m_levels; }

    // ─── Painter hook ────────────────────────────────────────────
    // Main exe installs in Fw2Painters.cpp. Null in tests → paint is
    // a no-op, state still advances.
    using PaintFn = void(*)(const std::vector<Level>& levels, UIContext& ctx);
    static void    setPainter(PaintFn fn);
    static PaintFn painter();

    // ─── Row metrics (used by painter + geometry) ────────────────
    // Height of a single row. Separators are shorter; headers match.
    static float rowHeight();
    static float separatorRowHeight();
    // Y offset of the i'th entry within a level, measured from the
    // level's top-left. Mirrors the geometry computeSize used.
    static float rowYOffset(const std::vector<MenuEntry>& entries, int i);

    // Test-only: reset all state (used by test fixtures so menus from
    // a prior test don't leak in).
    void _testResetAll();

private:
    ContextMenuManager() = default;

    // Event routing for the single OverlayEntry.
    bool onMouseDown(MouseEvent& e);
    bool onMouseMove(MouseMoveEvent& e);
    bool onKey(KeyEvent& e);
    void onEscape();

    // Push the OverlayEntry onto LayerStack if not already pushed.
    // Re-computes entry bounds from the union of level bounds.
    void pushOrRefreshEntry(UIContext& ctx);
    void refreshEntryBounds();

    // Interaction helpers.
    void activateRow(int levelIdx, int row);
    void openSubmenuForRow(int parentLevelIdx, int parentRow, UIContext& ctx);
    void closeLevelsFrom(int firstToClose);

    // Hit test — find (level, row) under a screen point. Returns
    // {-1, -1} if outside all levels; {level, -1} if inside level
    // gutter/padding (no active row).
    struct HitResult { int level = -1; int row = -1; };
    HitResult hitTestAll(float sx, float sy) const;
    int rowAt(const Level& L, float sy) const;

    // Geometry.
    Size computeSize(UIContext& ctx, const std::vector<MenuEntry>& entries) const;
    Rect computeRootBounds(UIContext& ctx,
                            const std::vector<MenuEntry>& entries,
                            Point anchor) const;
    Rect computeSubmenuBounds(UIContext& ctx,
                               const std::vector<MenuEntry>& entries,
                               const Rect& parent, int parentRow) const;

    // Step highlight in a level, skipping disabled / separator / header.
    int stepHighlight(const std::vector<MenuEntry>& entries, int from, int dir) const;

    // Helpers.
    static bool isSelectable(const MenuEntry& e);

    std::vector<Level> m_levels;
    OverlayHandle      m_handle;
    float              m_submenuHoverTimer = 0.0f;
    float              m_submenuHoverDelay = 0.2f;
    // Tracks the row the pointer currently hovers on the DEEPEST level
    // it's inside — used as the "about to open submenu" candidate.
    int                m_hoverLevel = -1;
    int                m_hoverRow   = -1;
};

// Static facade — call sites use this, manager is an implementation
// detail.
class ContextMenu {
public:
    ContextMenu() = delete;

    static void show(std::vector<MenuEntry> entries, Point screenPos) {
        ContextMenuManager::instance().show(std::move(entries), screenPos);
    }
    static void close() { ContextMenuManager::instance().close(); }
    static bool isOpen() { return ContextMenuManager::instance().isOpen(); }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
