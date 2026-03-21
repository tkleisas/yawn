#pragma once
// TabPanel — Tabbed container that shows one child page at a time.
//
// Each tab has a label. Clicking a tab header switches the visible page.
// Tabs can be positioned at the top or left side.

#include "Widget.h"
#include <string>
#include <vector>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

enum class TabPosition { Top, Left };

class TabPanel : public Widget {
public:
    using TabChangedCallback = std::function<void(int oldIndex, int newIndex)>;

    explicit TabPanel(TabPosition pos = TabPosition::Top) : m_position(pos) {}

    // ─── Tab management ─────────────────────────────────────────────────

    int addTab(const std::string& label, Widget* content) {
        int idx = static_cast<int>(m_tabs.size());
        m_tabs.push_back({label, content});
        if (content) addChild(content);
        if (content && m_activeTab >= 0 && idx != m_activeTab)
            content->setVisible(false);
        if (m_tabs.size() == 1) setActiveTab(0);
        return idx;
    }

    void removeTab(int index) {
        if (index < 0 || index >= tabCount()) return;
        if (m_tabs[index].content) removeChild(m_tabs[index].content);
        m_tabs.erase(m_tabs.begin() + index);
        if (m_activeTab >= tabCount()) m_activeTab = tabCount() - 1;
    }

    int tabCount() const { return static_cast<int>(m_tabs.size()); }

    void setActiveTab(int index) {
        if (index < 0 || index >= tabCount()) return;
        int old = m_activeTab;
        m_activeTab = index;
        // Update visibility
        for (int i = 0; i < tabCount(); ++i) {
            if (m_tabs[i].content)
                m_tabs[i].content->setVisible(i == m_activeTab);
        }
        if (old != index && m_onTabChanged)
            m_onTabChanged(old, index);
    }

    int activeTab() const { return m_activeTab; }

    const std::string& tabLabel(int index) const {
        return m_tabs[index].label;
    }

    void setTabLabel(int index, const std::string& label) {
        if (index >= 0 && index < tabCount())
            m_tabs[index].label = label;
    }

    Widget* tabContent(int index) const {
        if (index < 0 || index >= tabCount()) return nullptr;
        return m_tabs[index].content;
    }

    void setOnTabChanged(TabChangedCallback cb) { m_onTabChanged = std::move(cb); }

    // ─── Configuration ──────────────────────────────────────────────────

    static constexpr float kTabHeight = 28.0f;
    static constexpr float kTabLeftWidth = 120.0f;
    static constexpr float kTabPadding = 12.0f;

    void setTabPosition(TabPosition pos) { m_position = pos; }
    TabPosition tabPosition() const { return m_position; }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        // Measure the active content
        float headerSize = (m_position == TabPosition::Top) ? kTabHeight : kTabLeftWidth;
        Size contentSize{0, 0};

        if (m_activeTab >= 0 && m_activeTab < tabCount() && m_tabs[m_activeTab].content) {
            Constraints cc = (m_position == TabPosition::Top)
                ? Constraints::loose(constraints.maxW, constraints.maxH - headerSize)
                : Constraints::loose(constraints.maxW - headerSize, constraints.maxH);
            contentSize = m_tabs[m_activeTab].content->measure(cc, ctx);
        }

        if (m_position == TabPosition::Top)
            return constraints.constrain({contentSize.w, contentSize.h + headerSize});
        else
            return constraints.constrain({contentSize.w + headerSize, contentSize.h});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        Rect contentBounds = contentAreaRect();

        for (int i = 0; i < tabCount(); ++i) {
            if (m_tabs[i].content && i == m_activeTab) {
                m_tabs[i].content->layout(contentBounds, ctx);
            }
        }
    }

    // ─── Geometry ───────────────────────────────────────────────────────

    Rect tabHeaderRect() const {
        if (m_position == TabPosition::Top)
            return {m_bounds.x, m_bounds.y, m_bounds.w, kTabHeight};
        else
            return {m_bounds.x, m_bounds.y, kTabLeftWidth, m_bounds.h};
    }

    Rect contentAreaRect() const {
        if (m_position == TabPosition::Top)
            return {m_bounds.x, m_bounds.y + kTabHeight,
                    m_bounds.w, m_bounds.h - kTabHeight};
        else
            return {m_bounds.x + kTabLeftWidth, m_bounds.y,
                    m_bounds.w - kTabLeftWidth, m_bounds.h};
    }

    // Returns the rect for a specific tab header
    Rect tabRect(int index) const {
        if (m_position == TabPosition::Top) {
            float x = m_bounds.x;
            for (int i = 0; i < index && i < tabCount(); ++i) {
                x += estimateTabWidth(i);
            }
            return {x, m_bounds.y, estimateTabWidth(index), kTabHeight};
        } else {
            return {m_bounds.x, m_bounds.y + index * kTabHeight,
                    kTabLeftWidth, kTabHeight};
        }
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        Point local = toLocal(e.x, e.y);
        Rect header = tabHeaderRect();
        Rect localHeader{header.x - m_bounds.x, header.y - m_bounds.y,
                         header.w, header.h};

        if (localHeader.contains(local.x, local.y)) {
            // Find which tab was clicked
            for (int i = 0; i < tabCount(); ++i) {
                Rect tr = tabRect(i);
                Rect localTR{tr.x - m_bounds.x, tr.y - m_bounds.y, tr.w, tr.h};
                if (localTR.contains(local.x, local.y)) {
                    setActiveTab(i);
                    e.consume();
                    return true;
                }
            }
        }
        return false;
    }

private:
    float estimateTabWidth(int index) const {
        if (index < 0 || index >= tabCount()) return 0;
        // Approximate: 8px per char + padding
        return m_tabs[index].label.size() * 8.0f + kTabPadding * 2;
    }

    struct Tab {
        std::string label;
        Widget* content = nullptr;
    };

    std::vector<Tab> m_tabs;
    int m_activeTab = -1;
    TabPosition m_position;
    TabChangedCallback m_onTabChanged;
};

} // namespace fw
} // namespace ui
} // namespace yawn
