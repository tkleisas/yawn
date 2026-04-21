#include "ContextMenu.h"

#include "Theme.h"
#include "UIContext.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Menu factory helpers
// ───────────────────────────────────────────────────────────────────

namespace Menu {

MenuEntry item(std::string label, std::function<void()> action, std::string shortcut) {
    MenuEntry e;
    e.kind     = MenuEntryKind::Item;
    e.label    = std::move(label);
    e.onClick  = std::move(action);
    e.shortcut = std::move(shortcut);
    return e;
}

MenuEntry submenu(std::string label, std::vector<MenuEntry> children) {
    MenuEntry e;
    e.kind     = MenuEntryKind::Submenu;
    e.label    = std::move(label);
    e.children = std::move(children);
    return e;
}

MenuEntry separator() {
    MenuEntry e;
    e.kind    = MenuEntryKind::Separator;
    e.enabled = false;
    return e;
}

MenuEntry header(std::string label) {
    MenuEntry e;
    e.kind    = MenuEntryKind::Header;
    e.label   = std::move(label);
    e.enabled = false;
    return e;
}

MenuEntry checkable(std::string label, bool checked,
                     std::function<void()> action, std::string shortcut) {
    MenuEntry e;
    e.kind     = MenuEntryKind::Checkable;
    e.label    = std::move(label);
    e.checked  = checked;
    e.onClick  = std::move(action);
    e.shortcut = std::move(shortcut);
    return e;
}

MenuEntry radio(std::string group, std::string label, bool checked,
                 std::function<void()> action) {
    MenuEntry e;
    e.kind       = MenuEntryKind::Radio;
    e.label      = std::move(label);
    e.checked    = checked;
    e.radioGroup = std::move(group);
    e.onClick    = std::move(action);
    return e;
}

} // namespace Menu

// ───────────────────────────────────────────────────────────────────
// Painter hook storage
// ───────────────────────────────────────────────────────────────────

namespace {
ContextMenuManager::PaintFn& painterSlot() {
    static ContextMenuManager::PaintFn fn = nullptr;
    return fn;
}
constexpr float kFallbackPxPerChar = 8.0f;

int utf8CodepointCount(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

float measureText(const std::string& s, UIContext& ctx, float fontSize) {
    if (s.empty()) return 0.0f;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(s, fontSize);
    return static_cast<float>(utf8CodepointCount(s)) * kFallbackPxPerChar;
}
} // anon

void ContextMenuManager::setPainter(PaintFn fn) { painterSlot() = fn; }
ContextMenuManager::PaintFn ContextMenuManager::painter() { return painterSlot(); }

// ───────────────────────────────────────────────────────────────────
// Row metrics
// ───────────────────────────────────────────────────────────────────

float ContextMenuManager::rowHeight() {
    // A touch shorter than controlHeight to feel compact in a list.
    return theme().metrics.controlHeight - 4.0f;
}

float ContextMenuManager::separatorRowHeight() {
    return theme().metrics.baseUnit * 2.0f;   // 8 px
}

float ContextMenuManager::rowYOffset(const std::vector<MenuEntry>& entries, int i) {
    const float padY  = theme().metrics.baseUnit * 0.5f;
    float y = padY;
    for (int k = 0; k < i && k < static_cast<int>(entries.size()); ++k) {
        y += (entries[k].kind == MenuEntryKind::Separator)
             ? separatorRowHeight() : rowHeight();
    }
    return y;
}

// ───────────────────────────────────────────────────────────────────
// Singleton
// ───────────────────────────────────────────────────────────────────

ContextMenuManager& ContextMenuManager::instance() {
    static ContextMenuManager g;
    return g;
}

// ───────────────────────────────────────────────────────────────────
// Lifecycle
// ───────────────────────────────────────────────────────────────────

void ContextMenuManager::show(std::vector<MenuEntry> entries, Point screenPos) {
    close();   // replace any currently-open menu
    if (entries.empty()) return;

    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) {
        // No stack wired — silently no-op, matches Tooltip behaviour
        // in the same situation. Tests without a stack skip the push.
        return;
    }

    Level root;
    root.entries = std::move(entries);
    root.bounds  = computeRootBounds(ctx, root.entries, screenPos);
    m_levels.push_back(std::move(root));

    pushOrRefreshEntry(ctx);
}

void ContextMenuManager::close() {
    m_levels.clear();
    m_hoverLevel = -1;
    m_hoverRow   = -1;
    m_submenuHoverTimer = 0.0f;
    if (m_handle.active()) m_handle.remove();
}

void ContextMenuManager::_testResetAll() { close(); }

// ───────────────────────────────────────────────────────────────────
// OverlayEntry push / refresh
// ───────────────────────────────────────────────────────────────────

void ContextMenuManager::refreshEntryBounds() {
    if (m_levels.empty()) return;
    // Union of all level rects — used as the entry's bounds for
    // default hit-test, though customHitTest also needs to check each
    // level individually. This union just gives LayerStack a rough
    // region. (It's OK if it overlaps gaps between submenus; the
    // customHitTest below filters accurately.)
    Rect u = m_levels[0].bounds;
    for (size_t i = 1; i < m_levels.size(); ++i) {
        const Rect& r = m_levels[i].bounds;
        float x0 = std::min(u.x, r.x);
        float y0 = std::min(u.y, r.y);
        float x1 = std::max(u.x + u.w, r.x + r.w);
        float y1 = std::max(u.y + u.h, r.y + r.h);
        u = {x0, y0, x1 - x0, y1 - y0};
    }
    // We can't edit bounds on a pushed entry — LayerStack stores by
    // value. For this first pass bounds are set at push time and
    // stay put; submenus reference their own bounds via customHitTest
    // which checks all levels dynamically, so the entry's union rect
    // only matters for the default hit-test fallback (unused since we
    // always supply customHitTest).
    (void)u;
}

void ContextMenuManager::pushOrRefreshEntry(UIContext& ctx) {
    if (m_handle.active()) {
        refreshEntryBounds();
        return;
    }

    OverlayEntry entry;
    entry.debugName             = "ContextMenu";
    entry.bounds                = m_levels.empty() ? Rect{} : m_levels[0].bounds;
    entry.modal                 = false;
    entry.dismissOnOutsideClick = true;

    entry.customHitTest = [this](float sx, float sy) {
        return hitTestAll(sx, sy).level >= 0;
    };
    entry.paint = [this](UIContext& c) {
        if (PaintFn fn = painterSlot()) fn(m_levels, c);
    };
    entry.onMouseDown = [this](MouseEvent& e) { return onMouseDown(e); };
    entry.onMouseMove = [this](MouseMoveEvent& e) { return onMouseMove(e); };
    entry.onScroll    = [](ScrollEvent&) { return true; };   // swallow, no scroll support
    entry.onKey       = [this](KeyEvent& e) { return onKey(e); };
    entry.onEscape    = [this]() { onEscape(); };
    entry.onDismiss   = [this]() {
        // LayerStack is removing our entry (outside click). Clear all
        // state — detach handle first so close() doesn't re-enter
        // remove().
        m_handle.detach_noRemove();
        m_levels.clear();
        m_hoverLevel = -1;
        m_hoverRow   = -1;
        m_submenuHoverTimer = 0.0f;
    };

    m_handle = ctx.layerStack->push(OverlayLayer::Overlay, std::move(entry));
}

// ───────────────────────────────────────────────────────────────────
// Hit-test
// ───────────────────────────────────────────────────────────────────

int ContextMenuManager::rowAt(const Level& L, float sy) const {
    const float padY = theme().metrics.baseUnit * 0.5f;
    float y = L.bounds.y + padY;
    for (int i = 0; i < static_cast<int>(L.entries.size()); ++i) {
        float h = (L.entries[i].kind == MenuEntryKind::Separator)
                  ? separatorRowHeight() : rowHeight();
        if (sy >= y && sy < y + h) return i;
        y += h;
    }
    return -1;
}

ContextMenuManager::HitResult
ContextMenuManager::hitTestAll(float sx, float sy) const {
    // Search newest-first so submenus take priority where bounds
    // overlap (shouldn't happen normally, but safe).
    for (int i = static_cast<int>(m_levels.size()) - 1; i >= 0; --i) {
        const Level& L = m_levels[i];
        if (!L.bounds.contains(sx, sy)) continue;
        return {i, rowAt(L, sy)};
    }
    return {-1, -1};
}

// ───────────────────────────────────────────────────────────────────
// Selectability helpers
// ───────────────────────────────────────────────────────────────────

bool ContextMenuManager::isSelectable(const MenuEntry& e) {
    switch (e.kind) {
        case MenuEntryKind::Separator:
        case MenuEntryKind::Header:
            return false;
        default:
            return e.enabled;
    }
}

int ContextMenuManager::stepHighlight(const std::vector<MenuEntry>& entries,
                                      int from, int dir) const {
    const int n = static_cast<int>(entries.size());
    if (n == 0) return -1;
    int idx;
    if (from < 0) idx = (dir > 0) ? (n - 1) : 0;
    else          idx = from;
    for (int attempts = 0; attempts < n; ++attempts) {
        idx = (idx + dir + n) % n;
        if (isSelectable(entries[idx])) return idx;
    }
    return -1;
}

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

bool ContextMenuManager::onMouseDown(MouseEvent& e) {
    HitResult hit = hitTestAll(e.x, e.y);
    if (hit.level < 0) {
        // Outside every level → close all. LayerStack's outside-click
        // path handles this too; we just let it dismiss naturally.
        return false;
    }
    // Close deeper levels when clicking on an ancestor level (user
    // is "backing out" of a submenu).
    if (hit.level < static_cast<int>(m_levels.size()) - 1) {
        closeLevelsFrom(hit.level + 1);
    }

    if (hit.row < 0) {
        // Clicked in a level's padding / gutter — eat but do nothing.
        return true;
    }

    const MenuEntry& entry = m_levels[hit.level].entries[hit.row];
    if (!isSelectable(entry)) return true;   // separator / header / disabled

    if (entry.kind == MenuEntryKind::Submenu) {
        // Click a submenu row → open immediately (no hover delay).
        UIContext& ctx = UIContext::global();
        openSubmenuForRow(hit.level, hit.row, ctx);
        return true;
    }

    // Item / Checkable / Radio — fire callback, close chain.
    activateRow(hit.level, hit.row);
    return true;
}

bool ContextMenuManager::onMouseMove(MouseMoveEvent& e) {
    HitResult hit = hitTestAll(e.x, e.y);
    if (hit.level < 0) {
        // Pointer outside all levels — don't close anything (mouse
        // might come back). Reset hover tracking so a following
        // re-entry starts a fresh timer.
        m_hoverLevel = -1;
        m_hoverRow   = -1;
        m_submenuHoverTimer = 0.0f;
        return false;
    }

    // Update each level's highlighted row if we're inside it.
    for (int i = 0; i <= hit.level; ++i) {
        // Keep the ancestor highlight where it was (clamp to its open
        // submenu row if any) — user is navigating deeper.
    }
    if (hit.row >= 0) {
        m_levels[hit.level].highlighted = hit.row;
    }

    // Hover tracking for submenu open delay.
    if (hit.level != m_hoverLevel || hit.row != m_hoverRow) {
        m_hoverLevel        = hit.level;
        m_hoverRow          = hit.row;
        m_submenuHoverTimer = 0.0f;

        // If pointer moved to a DIFFERENT item on the same level as
        // an open submenu, close deeper levels so the user can pick
        // a sibling without pre-closing.
        if (hit.level + 1 < static_cast<int>(m_levels.size())) {
            // Only if the hovered row isn't the same one whose submenu
            // is currently open.
            const Level& L = m_levels[hit.level];
            if (hit.row != L.openSubmenuRow) {
                closeLevelsFrom(hit.level + 1);
            }
        }
    }
    return true;
}

bool ContextMenuManager::onKey(KeyEvent& e) {
    if (e.consumed || m_levels.empty()) return false;

    // All key handling targets the topmost level.
    const int top = static_cast<int>(m_levels.size()) - 1;
    Level& L = m_levels[top];

    switch (e.key) {
        case Key::Escape:
            onEscape();
            return true;
        case Key::Up:
            L.highlighted = stepHighlight(L.entries, L.highlighted, -1);
            return true;
        case Key::Down:
            L.highlighted = stepHighlight(L.entries, L.highlighted, +1);
            return true;
        case Key::Right:
            if (L.highlighted >= 0 &&
                L.highlighted < static_cast<int>(L.entries.size()) &&
                L.entries[L.highlighted].kind == MenuEntryKind::Submenu) {
                UIContext& ctx = UIContext::global();
                openSubmenuForRow(top, L.highlighted, ctx);
            }
            return true;
        case Key::Left:
            if (top > 0) closeLevelsFrom(top);
            return true;
        case Key::Enter:
        case Key::Space:
            if (L.highlighted >= 0 &&
                L.highlighted < static_cast<int>(L.entries.size())) {
                const MenuEntry& ent = L.entries[L.highlighted];
                if (ent.kind == MenuEntryKind::Submenu) {
                    UIContext& ctx = UIContext::global();
                    openSubmenuForRow(top, L.highlighted, ctx);
                } else if (isSelectable(ent)) {
                    activateRow(top, L.highlighted);
                }
            }
            return true;
        default:
            return false;
    }
}

void ContextMenuManager::onEscape() {
    if (m_levels.empty()) return;
    if (m_levels.size() > 1) {
        // Close topmost submenu, keep parent open.
        closeLevelsFrom(static_cast<int>(m_levels.size()) - 1);
        return;
    }
    close();
}

// ───────────────────────────────────────────────────────────────────
// Interaction helpers
// ───────────────────────────────────────────────────────────────────

void ContextMenuManager::activateRow(int levelIdx, int row) {
    if (levelIdx < 0 || levelIdx >= static_cast<int>(m_levels.size())) return;
    const MenuEntry& e = m_levels[levelIdx].entries[row];
    // Capture the callback BEFORE close() wipes m_levels — otherwise
    // the function_target is destroyed mid-call.
    auto cb = e.onClick;
    close();
    if (cb) cb();
}

void ContextMenuManager::openSubmenuForRow(int parentLevelIdx, int parentRow,
                                           UIContext& ctx) {
    if (parentLevelIdx < 0 || parentLevelIdx >= static_cast<int>(m_levels.size())) return;
    Level& parent = m_levels[parentLevelIdx];
    if (parentRow < 0 || parentRow >= static_cast<int>(parent.entries.size())) return;
    const MenuEntry& trigger = parent.entries[parentRow];
    if (trigger.kind != MenuEntryKind::Submenu) return;
    if (trigger.children.empty()) return;

    // Close any deeper levels first (we may be switching submenus on
    // the same parent).
    if (parentLevelIdx + 1 < static_cast<int>(m_levels.size())) {
        closeLevelsFrom(parentLevelIdx + 1);
    }

    Level sub;
    sub.entries = trigger.children;
    sub.bounds  = computeSubmenuBounds(ctx, sub.entries, parent.bounds, parentRow);
    m_levels.push_back(std::move(sub));

    parent.openSubmenuRow = parentRow;
}

void ContextMenuManager::closeLevelsFrom(int firstToClose) {
    if (firstToClose <= 0 || firstToClose > static_cast<int>(m_levels.size())) {
        if (firstToClose == 0) { close(); return; }
        return;
    }
    m_levels.resize(firstToClose);
    if (!m_levels.empty()) m_levels.back().openSubmenuRow = -1;
    m_submenuHoverTimer = 0.0f;
}

// ───────────────────────────────────────────────────────────────────
// Hover-to-open tick
// ───────────────────────────────────────────────────────────────────

void ContextMenuManager::tick(float dtSec) {
    if (m_levels.empty() || m_hoverLevel < 0 || m_hoverRow < 0) return;
    if (m_hoverLevel >= static_cast<int>(m_levels.size())) return;
    Level& L = m_levels[m_hoverLevel];
    if (m_hoverRow >= static_cast<int>(L.entries.size())) return;
    const MenuEntry& e = L.entries[m_hoverRow];
    if (e.kind != MenuEntryKind::Submenu) return;
    if (L.openSubmenuRow == m_hoverRow) return;   // already open

    m_submenuHoverTimer += dtSec;
    if (m_submenuHoverTimer < m_submenuHoverDelay) return;

    UIContext& ctx = UIContext::global();
    openSubmenuForRow(m_hoverLevel, m_hoverRow, ctx);
    m_submenuHoverTimer = 0.0f;
}

// ───────────────────────────────────────────────────────────────────
// Geometry
// ───────────────────────────────────────────────────────────────────

Size ContextMenuManager::computeSize(UIContext& ctx,
                                     const std::vector<MenuEntry>& entries) const {
    const ThemeMetrics& m  = theme().metrics;
    const float fontSize   = m.fontSize;
    const float padX       = m.baseUnit * 2.0f;
    const float padY       = m.baseUnit * 0.5f;
    const float columnGap  = m.baseUnit * 2.0f;
    const float leftMarker = fontSize;         // reserved for check/radio/spacer
    const float rightArrow = fontSize * 0.75f; // submenu chevron

    float maxLabelW    = 0.0f;
    float maxShortcutW = 0.0f;
    bool  anyWithShortcut = false;
    float totalH       = padY * 2.0f;

    for (const auto& e : entries) {
        if (e.kind == MenuEntryKind::Separator) {
            totalH += separatorRowHeight();
            continue;
        }
        const float lw = measureText(e.label, ctx, fontSize);
        maxLabelW = std::max(maxLabelW, lw);
        if (!e.shortcut.empty()) {
            anyWithShortcut = true;
            maxShortcutW = std::max(maxShortcutW, measureText(e.shortcut, ctx, fontSize));
        }
        totalH += rowHeight();
    }

    float w = padX * 2.0f + leftMarker + maxLabelW + rightArrow;
    if (anyWithShortcut) w += columnGap + maxShortcutW;
    // Sensible minimum so tiny menus don't look pinched.
    w = std::max(w, 140.0f);
    return {w, totalH};
}

Rect ContextMenuManager::computeRootBounds(UIContext& ctx,
                                           const std::vector<MenuEntry>& entries,
                                           Point anchor) const {
    Size s = computeSize(ctx, entries);
    const Rect& v = ctx.viewport;

    float x = anchor.x;
    float y = anchor.y;
    // Shift left if we'd clip the right edge.
    if (x + s.w > v.x + v.w) x = v.x + v.w - s.w;
    if (x < v.x)              x = v.x;
    // Shift up if we'd clip the bottom edge.
    if (y + s.h > v.y + v.h) y = v.y + v.h - s.h;
    if (y < v.y)              y = v.y;
    return {x, y, s.w, s.h};
}

Rect ContextMenuManager::computeSubmenuBounds(UIContext& ctx,
                                              const std::vector<MenuEntry>& entries,
                                              const Rect& parent,
                                              int parentRow) const {
    Size s = computeSize(ctx, entries);
    const Rect& v = ctx.viewport;
    const float gap = 1.0f;   // slight overlap with parent looks tight

    float x = parent.x + parent.w + gap;
    // Flip left if we'd clip the right edge.
    if (x + s.w > v.x + v.w) x = parent.x - s.w - gap;
    if (x < v.x)              x = v.x;

    // Align top to the parent-row's top, then clamp to viewport.
    // We approximate row Y by parentRow × rowHeight + padding —
    // accurate when every ancestor row above parentRow is rowHeight
    // (Items / Submenus / Checkables / Radios / Headers). Separators
    // above the parent row would make this misalign by a few pixels;
    // visually acceptable, and we can tighten this up later by
    // passing the exact parent Level in.
    const float padY   = theme().metrics.baseUnit * 0.5f;
    const float rowTop = parent.y + padY + parentRow * rowHeight();
    float y = rowTop;
    if (y + s.h > v.y + v.h) y = v.y + v.h - s.h;
    if (y < v.y)              y = v.y;
    return {x, y, s.w, s.h};
}

} // namespace fw2
} // namespace ui
} // namespace yawn
