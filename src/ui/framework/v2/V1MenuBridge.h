#pragma once

// UI v2 — Transitional bridge: convert v1 ContextMenu::Item trees
// into fw2::MenuEntry vectors.
//
// Purpose: the fw2::ContextMenu migration touches 10 open() call
// sites and ~30 places where v1 items are constructed as
// `std::vector<ui::ContextMenu::Item>`. Rather than rewriting every
// item builder to use fw2::MenuEntry directly, we keep the v1
// item-building code intact and only change the .open() / render()
// sites. That keeps the migration diff minimal and contained to
// each call site's terminal line.
//
// Scheduled for deletion once v1 ContextMenu is removed entirely —
// when that happens every item builder will convert to v2 directly
// and this helper becomes dead code.

#include "ui/ContextMenu.h"   // v1 Item
#include "ContextMenu.h"      // v2 MenuEntry

#include <utility>

namespace yawn {
namespace ui {
namespace fw2 {

// Produce the body MenuEntry for a v1 Item (without its "separator
// above" prefix). Classifies by content shape because v1's Item
// struct conflates several kinds:
//   empty label + no action + no submenu  → Separator (pure divider)
//   label + no action + no submenu        → Header  (section title,
//                                           non-selectable — v1 idiom
//                                           like "── VST3 ──")
//   label + submenu                       → Submenu
//   label + action (with or without icon) → Item
inline MenuEntry v1ItemBodyToFw2(::yawn::ui::ContextMenu::Item&& in) {
    MenuEntry out;
    const bool hasLabel   = !in.label.empty();
    const bool hasAction  = static_cast<bool>(in.action);
    const bool hasSubmenu = !in.submenu.empty();

    if (!hasLabel && !hasAction && !hasSubmenu) {
        out.kind    = MenuEntryKind::Separator;
        out.enabled = false;
        return out;
    }
    if (hasSubmenu) {
        out.kind  = MenuEntryKind::Submenu;
        out.label = std::move(in.label);
        out.children.reserve(in.submenu.size());
        for (auto& child : in.submenu)
            out.children.push_back(v1ItemBodyToFw2(std::move(child)));
        out.enabled = in.enabled;
        return out;
    }
    if (hasLabel && !hasAction) {
        // v1 section-header idiom — label, no click behaviour, no
        // children. v2 treats these as non-selectable Header rows.
        out.kind    = MenuEntryKind::Header;
        out.label   = std::move(in.label);
        out.enabled = false;
        return out;
    }
    out.kind    = MenuEntryKind::Item;
    out.label   = std::move(in.label);
    out.onClick = std::move(in.action);
    out.enabled = in.enabled;
    return out;
}

// v1 semantics: `separator = true` means "draw a divider above this
// row" — the row ITSELF still renders with its label/action/submenu.
// Separator-only rows are denoted by separator=true AND empty content
// (label/action/submenu all absent). The bridge splits the "divider
// above" case into two v2 entries: an explicit Separator followed by
// the row's body.
inline std::vector<MenuEntry> v1ItemsToFw2(
    std::vector<::yawn::ui::ContextMenu::Item> items) {
    std::vector<MenuEntry> out;
    out.reserve(items.size() + 4);   // some slop for implicit separators
    for (auto& it : items) {
        const bool hasContent = !it.label.empty() || it.action || !it.submenu.empty();
        if (it.separator && hasContent) {
            MenuEntry sep;
            sep.kind = MenuEntryKind::Separator;
            sep.enabled = false;
            out.push_back(std::move(sep));
        }
        out.push_back(v1ItemBodyToFw2(std::move(it)));
    }
    return out;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
