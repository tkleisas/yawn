#include "TabView.h"
#include "Painter.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
int utf8CodepointCount(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}
constexpr float kFallbackPxPerChar = 8.0f;

const std::string& emptyStr() { static const std::string s; return s; }
} // anon

TabView::TabView() {
    setFocusable(true);
    setRelayoutBoundary(true);
}

TabView::~TabView() = default;

// ───────────────────────────────────────────────────────────────────
// Tab management
// ───────────────────────────────────────────────────────────────────

int TabView::indexOfId(const std::string& id) const {
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].id == id) return i;
    }
    return -1;
}

void TabView::addTab(std::string id, std::string label, Widget* content) {
    TabEntry e;
    e.id      = std::move(id);
    e.label   = std::move(label);
    e.content = content;
    if (content) addChild(content);
    m_tabs.push_back(std::move(e));
    if (m_activeIdx < 0) {
        // First tab auto-activates.
        activate(0, ValueChangeSource::Programmatic);
    } else {
        // New tab is inactive — hide its content.
        if (content) content->setVisible(false);
    }
    invalidate();
}

void TabView::removeTab(const std::string& id) {
    const int idx = indexOfId(id);
    if (idx < 0) return;

    Widget* content = m_tabs[idx].content;
    if (content) removeChild(content);
    const bool removingActive = (idx == m_activeIdx);
    m_tabs.erase(m_tabs.begin() + idx);

    if (m_tabs.empty()) {
        m_activeIdx = -1;
    } else if (removingActive) {
        // Cascade to the tab at the same index (now the "next") or the
        // previous if we removed the last.
        int newIdx = std::min(idx, static_cast<int>(m_tabs.size()) - 1);
        // Force-fire onActivated even though m_activeIdx was idx
        // before the erase: the id changed.
        m_activeIdx = -1;
        activate(newIdx, ValueChangeSource::Programmatic);
    } else if (idx < m_activeIdx) {
        // Active tab shifted left by one.
        --m_activeIdx;
    }
    rebuildVisibility();
    invalidate();
}

void TabView::clearTabs() {
    for (auto& t : m_tabs) {
        if (t.content) removeChild(t.content);
    }
    m_tabs.clear();
    m_activeIdx = -1;
    m_hoverIdx  = -1;
    invalidate();
}

void TabView::setTabLabel(const std::string& id, std::string label) {
    const int idx = indexOfId(id);
    if (idx < 0) return;
    if (m_tabs[idx].label == label) return;
    m_tabs[idx].label = std::move(label);
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Activation
// ───────────────────────────────────────────────────────────────────

void TabView::setActiveTab(const std::string& id, ValueChangeSource src) {
    const int idx = indexOfId(id);
    if (idx < 0) return;
    activate(idx, src);
}

const std::string& TabView::activeTabId() const {
    if (m_activeIdx < 0 || m_activeIdx >= static_cast<int>(m_tabs.size()))
        return emptyStr();
    return m_tabs[m_activeIdx].id;
}

void TabView::selectNextTab() {
    if (m_tabs.empty()) return;
    const int next = (m_activeIdx + 1) % static_cast<int>(m_tabs.size());
    activate(next, ValueChangeSource::User);
}

void TabView::selectPrevTab() {
    if (m_tabs.empty()) return;
    const int n = static_cast<int>(m_tabs.size());
    const int prev = (m_activeIdx - 1 + n) % n;
    activate(prev, ValueChangeSource::User);
}

void TabView::activate(int idx, ValueChangeSource src) {
    if (idx < 0 || idx >= static_cast<int>(m_tabs.size())) return;
    if (idx == m_activeIdx) return;
    m_activeIdx = idx;
    rebuildVisibility();
    invalidate();
    if (src == ValueChangeSource::Automation) return;
    if (m_onActivated) m_onActivated(m_tabs[idx].id);
}

void TabView::rebuildVisibility() {
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].content) {
            m_tabs[i].content->setVisible(i == m_activeIdx);
        }
    }
}

// ───────────────────────────────────────────────────────────────────
// Appearance
// ───────────────────────────────────────────────────────────────────

void TabView::setTabStripHeight(float px) {
    if (px == m_stripHeight) return;
    m_stripHeight = px;
    invalidate();
}

float TabView::tabStripHeight() const {
    return (m_stripHeight > 0.0f)
        ? m_stripHeight
        : theme().metrics.controlHeight + 4.0f;
}

void TabView::setTabMinWidth(float px) {
    if (px == m_tabMinWidth) return;
    m_tabMinWidth = px;
    invalidate();
}

// ───────────────────────────────────────────────────────────────────
// Layout
// ───────────────────────────────────────────────────────────────────

float TabView::measureTabWidth(const TabEntry& t, UIContext& ctx) const {
    const ThemeMetrics& m = theme().metrics;
    const float fontSize = m.fontSize;
    const float padX     = m.baseUnit * 3.0f;    // 12 px each side
    float labelW = 0.0f;
    if (!t.label.empty()) {
        labelW = ctx.textMetrics
            ? ctx.textMetrics->textWidth(t.label, fontSize)
            : static_cast<float>(utf8CodepointCount(t.label)) * kFallbackPxPerChar;
    }
    return std::max(m_tabMinWidth, labelW + padX * 2.0f);
}

Size TabView::onMeasure(Constraints c, UIContext& ctx) {
    (void)ctx;
    // TabView fills parent width + height.
    float w = (c.maxW > 0.0f && c.maxW < 1e8f) ? c.maxW : 400.0f;
    float h = (c.maxH > 0.0f && c.maxH < 1e8f) ? c.maxH : 300.0f;
    w = std::max(w, c.minW);
    h = std::max(h, c.minH);
    return {w, h};
}

void TabView::onLayout(Rect bounds, UIContext& ctx) {
    const float stripH = tabStripHeight();
    m_stripRect = Rect{bounds.x, bounds.y, bounds.w, stripH};
    m_contentArea = Rect{bounds.x, bounds.y + stripH,
                          bounds.w, std::max(0.0f, bounds.h - stripH)};

    // Compute tab widths + strip rects.
    float cursor = bounds.x;
    for (auto& t : m_tabs) {
        const float w = measureTabWidth(t, ctx);
        t.stripRect = Rect{cursor, bounds.y, w, stripH};
        cursor += w;
    }

    // Position content widgets. Active takes the content area; others
    // are hidden (zero-size rect so hit-tests miss them regardless).
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        Widget* c = m_tabs[i].content;
        if (!c) continue;
        if (i == m_activeIdx) {
            c->layout(m_contentArea, ctx);
        } else {
            c->layout(Rect{0, 0, 0, 0}, ctx);
        }
    }
}

// ───────────────────────────────────────────────────────────────────
// Render
// ───────────────────────────────────────────────────────────────────

void TabView::render(UIContext& ctx) {
    if (!m_visible) return;
    // Painter paints the strip. We then recurse the active child
    // ourselves (can't use the default m_children walk — that'd paint
    // hidden siblings too, and we want the strip painted BEFORE the
    // active content in some themes).
    if (PaintFn fn = findPainter(typeid(*this))) {
        fn(*this, ctx);
    }
    if (m_activeIdx >= 0 && m_activeIdx < static_cast<int>(m_tabs.size())) {
        Widget* c = m_tabs[m_activeIdx].content;
        if (c && c->isVisible()) c->render(ctx);
    }
}

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

int TabView::hitTestStrip(float sx, float sy) const {
    if (sx < m_stripRect.x || sx >= m_stripRect.x + m_stripRect.w) return -1;
    if (sy < m_stripRect.y || sy >= m_stripRect.y + m_stripRect.h) return -1;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const Rect& sr = m_tabs[i].stripRect;
        if (sx >= sr.x && sx < sr.x + sr.w) return i;
    }
    return -1;
}

bool TabView::onMouseDown(MouseEvent& e) {
    if (!m_enabled) return false;
    if (e.button != MouseButton::Left) return false;
    const int idx = hitTestStrip(e.x, e.y);
    if (idx < 0) return false;   // let it fall through to content
    activate(idx, ValueChangeSource::User);
    return true;
}

bool TabView::onMouseMove(MouseMoveEvent& e) {
    const int hit = hitTestStrip(e.x, e.y);
    if (hit != m_hoverIdx) m_hoverIdx = hit;
    return false;   // don't consume — hover tracking only
}

bool TabView::onKeyDown(KeyEvent& e) {
    if (!m_enabled || e.consumed) return false;
    const bool ctrl  = (e.modifiers & ModifierKey::Ctrl);
    const bool shift = (e.modifiers & ModifierKey::Shift);
    if (!ctrl) return false;
    if (e.key == Key::Tab) {
        if (shift) selectPrevTab();
        else       selectNextTab();
        return true;
    }
    return false;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
