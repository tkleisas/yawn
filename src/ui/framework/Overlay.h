#pragma once
// Overlay — Layer for modal dialogs, context menus, and popups.
//
// Overlays are rendered on top of the main widget tree and receive
// events first. When a modal overlay is active, events are blocked
// from reaching the main tree.

#include "Widget.h"

namespace yawn {
namespace ui {
namespace fw {

class Overlay : public Widget {
public:
    // Add an overlay widget (rendered on top, receives events first).
    void show(Widget* widget, bool modal = false) {
        if (!widget) return;
        // Remove if already showing
        hide(widget);
        m_entries.push_back({widget, modal});
        addChild(widget);
    }

    void hide(Widget* widget) {
        if (!widget) return;
        removeChild(widget);
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(),
                           [widget](const Entry& e) { return e.widget == widget; }),
            m_entries.end());
    }

    void hideAll() {
        removeAllChildren();
        m_entries.clear();
    }

    bool hasModal() const {
        for (const auto& e : m_entries)
            if (e.modal) return true;
        return false;
    }

    bool isEmpty() const { return m_entries.empty(); }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        // Overlay takes the full available size
        for (auto& e : m_entries) {
            e.widget->measure(constraints, ctx);
        }
        return {constraints.maxW, constraints.maxH};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        // Each overlay child can position itself within the full bounds
        for (auto& e : m_entries) {
            e.widget->layout(bounds, ctx);
        }
    }

    // ─── Events ─────────────────────────────────────────────────────────

    // Overlays dispatch events top-down (last added = topmost).
    Widget* dispatchMouseDown(MouseEvent& e) override {
        if (m_entries.empty()) return nullptr;

        // Dispatch to overlays in reverse order (topmost first)
        for (int i = static_cast<int>(m_entries.size()) - 1; i >= 0; --i) {
            Widget* hit = m_entries[i].widget->dispatchMouseDown(e);
            if (hit) return hit;

            // If this is a modal overlay, block further propagation
            if (m_entries[i].modal) {
                e.consume();
                return this;  // Modal eats the click
            }
        }
        return nullptr;
    }

    Widget* dispatchScroll(ScrollEvent& e) override {
        if (m_entries.empty()) return nullptr;

        for (int i = static_cast<int>(m_entries.size()) - 1; i >= 0; --i) {
            Widget* hit = m_entries[i].widget->dispatchScroll(e);
            if (hit) return hit;
            if (m_entries[i].modal) {
                e.consume();
                return this;
            }
        }
        return nullptr;
    }

private:
    struct Entry {
        Widget* widget = nullptr;
        bool    modal  = false;
    };
    std::vector<Entry> m_entries;
};

} // namespace fw
} // namespace ui
} // namespace yawn
