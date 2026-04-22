#include "DropDown.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
// Fallback width-per-char when no TextMetrics is wired — matches
// Label/Button conventions so test expectations line up.
constexpr float kFallbackPxPerChar = 8.0f;

int utf8CodepointCount(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

float measureWidth(const std::string& s, UIContext& ctx, float fontSize) {
    if (s.empty()) return 0.0f;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(s, fontSize);
    return static_cast<float>(utf8CodepointCount(s)) * kFallbackPxPerChar;
}

FwDropDown::PopupPaintFn& popupPainterSlot() {
    static FwDropDown::PopupPaintFn slot = nullptr;
    return slot;
}
} // anon

// ───────────────────────────────────────────────────────────────────
// Popup painter hook
// ───────────────────────────────────────────────────────────────────

void FwDropDown::setPopupPainter(PopupPaintFn fn) { popupPainterSlot() = fn; }
FwDropDown::PopupPaintFn FwDropDown::popupPainter()     { return popupPainterSlot(); }

// ───────────────────────────────────────────────────────────────────
// Auto-scroll state (shared static — only one dropdown open at once)
// ───────────────────────────────────────────────────────────────────

FwDropDown* FwDropDown::s_openDropdown = nullptr;

void FwDropDown::tickGlobal(float dtSec) {
    FwDropDown* dd = s_openDropdown;
    if (!dd || !dd->isOpen()) { s_openDropdown = nullptr; return; }

    // Determine which row the cursor is on (if any).
    const float ly = dd->m_lastPopupCursor.y - dd->m_popupBounds.y;
    const int hoveredRow = dd->indexAtPopupY(ly);
    if (hoveredRow < 0) {
        dd->m_autoScrollTimer = 0.0f;
        return;
    }

    const int totalItems   = static_cast<int>(dd->m_items.size());
    const int rows         = dd->visibleRowCount();
    const int firstVisible = dd->m_scrollOffset;
    const int lastVisible  = firstVisible + rows - 1;

    const bool canScrollUp   = dd->m_scrollOffset > 0;
    const bool canScrollDown = dd->m_scrollOffset + rows < totalItems;
    const bool atTop         = (hoveredRow == firstVisible) && canScrollUp;
    const bool atBottom      = (hoveredRow == lastVisible)  && canScrollDown;

    if (!atTop && !atBottom) {
        dd->m_autoScrollTimer = 0.0f;
        return;
    }

    dd->m_autoScrollTimer += dtSec;
    if (dd->m_autoScrollTimer < dd->m_autoScrollDelay) return;
    dd->m_autoScrollTimer -= dd->m_autoScrollDelay;

    const int maxOffset = std::max(0, totalItems - rows);
    if (atTop) {
        dd->m_scrollOffset = std::max(0, dd->m_scrollOffset - 1);
    } else {
        dd->m_scrollOffset = std::min(maxOffset, dd->m_scrollOffset + 1);
    }
    // Update highlight under the (stationary) cursor so the painted
    // row tint tracks the new item now occupying that screen row.
    const float newLy = dd->m_lastPopupCursor.y - dd->m_popupBounds.y;
    const int newIdx = dd->indexAtPopupY(newLy);
    if (newIdx >= 0) dd->m_highlighted = newIdx;
}

// ───────────────────────────────────────────────────────────────────
// Construction
// ───────────────────────────────────────────────────────────────────

FwDropDown::FwDropDown() {
    setFocusable(true);
    setRelayoutBoundary(true);   // size stable given constraints
}

FwDropDown::FwDropDown(std::vector<std::string> items) : FwDropDown() {
    setItems(std::move(items));
}

FwDropDown::~FwDropDown() {
    // Close any open popup BEFORE the base-class destructor runs. The
    // entry's onDismiss captures `this` and touches members, so it
    // has to fire while FwDropDown is still fully alive. Member
    // destruction (m_popupHandle last) would also call remove(), but
    // by then this is a Widget, not an FwDropDown — its closures
    // would see a sliced object.
    if (m_popupHandle.active()) {
        m_popupHandle.remove();
    }
}

// ───────────────────────────────────────────────────────────────────
// Items
// ───────────────────────────────────────────────────────────────────

void FwDropDown::setItems(std::vector<std::string> items) {
    std::vector<Item> full;
    full.reserve(items.size());
    for (auto& s : items) full.push_back({std::move(s), true, false});
    setItems(std::move(full));
}

void FwDropDown::setItems(std::vector<Item> items) {
    // Skip the whole rebuild if the new vector matches the current one
    // verbatim. Panels like the mixer call setItems every frame with
    // the same static list; without this early-out the scroll offset
    // (and highlight) would be reset every frame, breaking wheel
    // scrolling inside the open popup.
    auto itemsEqual = [](const std::vector<Item>& a, const std::vector<Item>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].label     != b[i].label     ||
                a[i].enabled   != b[i].enabled   ||
                a[i].separator != b[i].separator) return false;
        }
        return true;
    };
    if (itemsEqual(m_items, items)) return;

    m_items = std::move(items);
    if (m_selected >= static_cast<int>(m_items.size())) m_selected = -1;
    m_highlighted  = -1;
    // Clamp scroll rather than zero it — a legitimate items change
    // that doesn't invalidate the current offset should keep the
    // user's scroll position.
    const int rows = std::min(static_cast<int>(m_items.size()), m_maxVisibleItems);
    const int maxOffset = std::max(0, static_cast<int>(m_items.size()) - rows);
    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxOffset);
    invalidate();
}

void FwDropDown::addItem(std::string label) {
    m_items.push_back({std::move(label), true, false});
    invalidate();
}

void FwDropDown::clearItems() {
    m_items.clear();
    m_selected     = -1;
    m_highlighted  = -1;
    m_scrollOffset = 0;
    invalidate();
}

void FwDropDown::setItemEnabled(int idx, bool enabled) {
    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return;
    m_items[idx].enabled = enabled;
    // paint-only: doesn't change measured width
}

bool FwDropDown::isItemEnabled(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return false;
    return m_items[idx].enabled;
}

void FwDropDown::setItemSeparator(int idx, bool separator) {
    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return;
    m_items[idx].separator = separator;
}

bool FwDropDown::isItemSeparator(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return false;
    return m_items[idx].separator;
}

const std::string& FwDropDown::itemLabel(int idx) const {
    static const std::string empty;
    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return empty;
    return m_items[idx].label;
}

// ───────────────────────────────────────────────────────────────────
// Selection
// ───────────────────────────────────────────────────────────────────

void FwDropDown::setSelectedIndex(int idx, ValueChangeSource src) {
    if (idx < -1 || idx >= static_cast<int>(m_items.size())) return;
    if (idx == m_selected) return;
    // Skip disabled / separator items when programmatic selection lands
    // on one — silently refuse.
    if (idx >= 0) {
        const Item& it = m_items[idx];
        if (!it.enabled || it.separator) return;
    }
    m_selected = idx;
    fireOnChange(src);
}

const std::string& FwDropDown::selectedLabel() const {
    static const std::string empty;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size())) return empty;
    return m_items[m_selected].label;
}

// ───────────────────────────────────────────────────────────────────
// Placeholder / popup config
// ───────────────────────────────────────────────────────────────────

void FwDropDown::setPlaceholder(std::string p) {
    if (p == m_placeholder) return;
    m_placeholder = std::move(p);
    // Invalidates measure only if nothing is selected (placeholder
    // width might be widest); be defensive and invalidate always.
    invalidate();
}

void FwDropDown::setMaxVisibleItems(int n) {
    if (n < 1) n = 1;
    m_maxVisibleItems = n;
}

void FwDropDown::setItemHeight(float h) {
    if (h <= 0.0f) m_itemHeightOverride.reset();
    else           m_itemHeightOverride = h;
}

float FwDropDown::itemHeight() const {
    if (m_itemHeightOverride) return *m_itemHeightOverride;
    return theme().metrics.controlHeight;
}

void FwDropDown::setMinWidth(float w)       { if (w != m_minWidth)       { m_minWidth = w; invalidate(); } }
void FwDropDown::setPreferredWidth(float w) { if (w != m_preferredWidth) { m_preferredWidth = w; invalidate(); } }

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwDropDown::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float fontSize = m.fontSize;

    // Width: preferred if set, else longest item + glyph + padding.
    float w;
    if (m_preferredWidth > 0.0f) {
        w = m_preferredWidth;
    } else {
        float content = measureLongestItemWidth(ctx);
        // Empty: fall back to placeholder width so an empty dropdown
        // still looks like one, not like a 16-px button.
        if (content == 0.0f) content = measureWidth(m_placeholder, ctx, fontSize);
        const float glyphW   = fontSize;            // ▾ is ~ one em
        const float padding  = m.baseUnit * 2.0f;   // 8 px each side
        w = content + glyphW + padding * 2.0f;
    }
    if (m_minWidth > 0.0f) w = std::max(w, m_minWidth);
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);

    // Height: control height.
    float h = m.controlHeight;
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

float FwDropDown::measureLongestItemWidth(UIContext& ctx) const {
    const float fontSize = theme().metrics.fontSize;
    float longest = 0.0f;
    for (const auto& it : m_items) {
        longest = std::max(longest, measureWidth(it.label, ctx, fontSize));
    }
    return longest;
}

// ───────────────────────────────────────────────────────────────────
// Open / close / toggle
// ───────────────────────────────────────────────────────────────────

void FwDropDown::open() {
    if (isOpen()) return;
    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;   // nothing to push into
    if (m_items.empty()) return;

    computePopupRect(ctx);

    // Highlight starts on current selection if any, else the first
    // selectable row (skipping disabled/separator).
    if (m_selected >= 0) m_highlighted = m_selected;
    else                 m_highlighted = stepHighlight(-1, +1);

    // Scroll so highlight is visible.
    clampScrollToHighlight();

    // Snapshot the anchor button's global bounds so the hit-test
    // closure below is independent of later layout churn.
    const Rect anchorBounds = globalBounds();

    OverlayEntry entry;
    entry.debugName            = "FwDropDown.popup";
    entry.bounds               = m_popupBounds;
    entry.modal                = false;
    entry.dismissOnOutsideClick = true;
    // customHitTest includes the anchor button area so clicks on the
    // button while the popup is open route through popupOnMouseDown
    // (which dismisses), not through outside-click fall-through that
    // would reach the hosting panel and toggle the dropdown open
    // again. Without this, "click button to close" flickers open.
    entry.customHitTest = [this, anchorBounds](float sx, float sy) {
        return m_popupBounds.contains(sx, sy) ||
               anchorBounds.contains(sx, sy);
    };
    entry.paint = [this](UIContext& ctx) {
        if (auto fn = popupPainterSlot()) fn(*this, ctx);
    };
    entry.onMouseDown = [this, anchorBounds](MouseEvent& e) {
        // Click on anchor button (not inside popup body) → close.
        if (anchorBounds.contains(e.x, e.y) &&
            !m_popupBounds.contains(e.x, e.y)) {
            close();
            return true;
        }
        return popupOnMouseDown(e);
    };
    entry.onMouseMove = [this](MouseMoveEvent& e) { return popupOnMouseMove(e); };
    entry.onScroll    = [this](ScrollEvent& e) { return popupOnScroll(e); };
    entry.onKey       = [this](KeyEvent& e) { return popupOnKey(e); };
    entry.onDismiss   = [this]() { popupOnDismiss(); };

    m_popupHandle = ctx.layerStack->push(OverlayLayer::Overlay, std::move(entry));
    // Register as the currently-open dropdown so tickGlobal can drive
    // our auto-scroll timer.
    s_openDropdown = this;
    m_autoScrollTimer = 0.0f;
}

void FwDropDown::close() {
    if (!isOpen()) return;
    // remove() on the handle fires onDismiss → popupOnDismiss().
    m_popupHandle.remove();
}

void FwDropDown::toggle() {
    isOpen() ? close() : open();
}

void FwDropDown::popupOnDismiss() {
    // LayerStack has already erased the entry and is walking the
    // victim's onDismiss. The handle MIGHT still report active (e.g.
    // LayerStack-initiated dismiss on outside-click or Escape never
    // touched our handle). Detach here so isOpen() flips to false.
    m_popupHandle.detach_noRemove();
    m_highlighted  = -1;
    m_scrollOffset = 0;
    m_autoScrollTimer = 0.0f;
    if (s_openDropdown == this) s_openDropdown = nullptr;
}

// ───────────────────────────────────────────────────────────────────
// Popup geometry
// ───────────────────────────────────────────────────────────────────

int FwDropDown::visibleRowCount() const {
    return std::min(static_cast<int>(m_items.size()), m_maxVisibleItems);
}

void FwDropDown::computePopupRect(UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float ih = itemHeight();
    const float pad = m.baseUnit * 0.5f;   // tight vertical gutter

    const Rect gb = globalBounds();
    const float btnW = gb.w;

    // Width — match button by default (spec's default when long item
    // still fits). If the longest item is wider than the button but
    // under 3× the button width, grow; beyond that, clip at 3× button.
    float contentW = measureLongestItemWidth(ctx);
    if (contentW == 0.0f) contentW = btnW;
    const float minW = btnW;
    const float maxW = btnW * 3.0f;
    float popupW = std::clamp(contentW + m.baseUnit * 2.0f, minW, maxW);

    // Height — item count capped by maxVisibleItems.
    const int rows = visibleRowCount();
    float popupH = rows * ih + pad * 2.0f;

    // Horizontal clamp to viewport.
    float popupX = gb.x;
    if (popupX + popupW > ctx.viewport.x + ctx.viewport.w) {
        popupX = ctx.viewport.x + ctx.viewport.w - popupW;
    }
    if (popupX < ctx.viewport.x) popupX = ctx.viewport.x;

    // Direction: try below first. Flip upward if not enough room AND
    // more room exists above.
    const float below = (ctx.viewport.y + ctx.viewport.h) - (gb.y + gb.h);
    const float above = gb.y - ctx.viewport.y;

    m_popupOpensUpward = false;
    float popupY;
    if (below >= popupH || below >= above) {
        // Open down.
        popupY = gb.y + gb.h;
        // Clamp height to available room if below is short but still
        // preferred.
        popupH = std::min(popupH, below);
    } else {
        // Open up.
        m_popupOpensUpward = true;
        popupH = std::min(popupH, above);
        popupY = gb.y - popupH;
    }

    m_popupBounds = {popupX, popupY, popupW, popupH};
}

int FwDropDown::indexAtPopupY(float popupLocalY) const {
    const float ih = itemHeight();
    const ThemeMetrics& m = theme().metrics;
    const float pad = m.baseUnit * 0.5f;
    if (popupLocalY < pad) return -1;
    const int rowIdx = static_cast<int>((popupLocalY - pad) / ih);
    const int actual = m_scrollOffset + rowIdx;
    if (actual < 0 || actual >= static_cast<int>(m_items.size())) return -1;
    return actual;
}

void FwDropDown::clampScrollToHighlight() {
    if (m_highlighted < 0) return;
    const int rows = visibleRowCount();
    if (m_highlighted < m_scrollOffset) {
        m_scrollOffset = m_highlighted;
    } else if (m_highlighted >= m_scrollOffset + rows) {
        m_scrollOffset = m_highlighted - rows + 1;
    }
    const int maxOffset = std::max(0, static_cast<int>(m_items.size()) - rows);
    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxOffset);
}

// ───────────────────────────────────────────────────────────────────
// Popup input handlers (dispatched by LayerStack while open)
// ───────────────────────────────────────────────────────────────────

bool FwDropDown::popupOnMouseDown(MouseEvent& e) {
    // Translate to popup-local coords.
    const float ly = e.y - m_popupBounds.y;
    const int idx = indexAtPopupY(ly);
    if (idx < 0) return false;   // gutter — do nothing, let scrim handle
    const Item& it = m_items[idx];
    if (!it.enabled || it.separator) return true;   // eat but ignore

    setSelectedIndex(idx, ValueChangeSource::User);
    close();
    return true;
}

bool FwDropDown::popupOnMouseMove(MouseMoveEvent& e) {
    const float ly = e.y - m_popupBounds.y;
    const int idx = indexAtPopupY(ly);
    // Cache cursor for tickGlobal's auto-scroll.
    m_lastPopupCursor = {e.x, e.y};
    // Don't change highlight on "hovering gutter" — keep last.
    if (idx >= 0) m_highlighted = idx;
    return true;
}

bool FwDropDown::popupOnScroll(ScrollEvent& e) {
    // Scroll the visible window.
    const int dir = (e.dy > 0) ? -1 : (e.dy < 0 ? +1 : 0);
    if (dir == 0) return true;
    const int rows = visibleRowCount();
    const int maxOffset = std::max(0, static_cast<int>(m_items.size()) - rows);
    m_scrollOffset = std::clamp(m_scrollOffset + dir, 0, maxOffset);
    return true;
}

bool FwDropDown::popupOnKey(KeyEvent& e) {
    if (e.consumed) return false;
    switch (e.key) {
        case Key::Down:
            m_highlighted = stepHighlight(m_highlighted, +1);
            clampScrollToHighlight();
            return true;
        case Key::Up:
            m_highlighted = stepHighlight(m_highlighted, -1);
            clampScrollToHighlight();
            return true;
        case Key::Enter:
        case Key::Space:
            if (m_highlighted >= 0) {
                setSelectedIndex(m_highlighted, ValueChangeSource::User);
            }
            close();
            return true;
        case Key::Escape:
            close();
            return true;
        default:
            return false;
    }
}

// ───────────────────────────────────────────────────────────────────
// Widget event overrides (closed state)
// ───────────────────────────────────────────────────────────────────

void FwDropDown::onClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    toggle();
}

void FwDropDown::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    if (m_onRightClick) m_onRightClick(e.screen);
}

bool FwDropDown::onKeyDown(KeyEvent& e) {
    if (!m_enabled || e.consumed) return false;
    // Only the closed state is handled here; open-state keys go
    // through the OverlayEntry.onKey.
    if (isOpen()) return false;
    switch (e.key) {
        case Key::Enter:
        case Key::Space:
            open();
            return true;
        case Key::Down:
            // Cycle selection down by one (matches v1 convention; the
            // spec's Alt+Down-to-open variant is deferred).
            if (!m_items.empty()) {
                int next = stepHighlight(m_selected, +1);
                if (next >= 0) setSelectedIndex(next, ValueChangeSource::User);
            }
            return true;
        case Key::Up:
            if (!m_items.empty()) {
                int prev = stepHighlight(m_selected, -1);
                if (prev >= 0) setSelectedIndex(prev, ValueChangeSource::User);
            }
            return true;
        default:
            return false;
    }
}

bool FwDropDown::onScroll(ScrollEvent& e) {
    if (!m_enabled || !m_scrollChangesSelection) return false;
    if (m_items.empty()) return false;
    // Convention: wheel up (dy > 0) = previous, wheel down = next.
    const int dir = (e.dy > 0) ? -1 : (e.dy < 0 ? +1 : 0);
    if (dir == 0) return false;
    const int next = stepHighlight(m_selected, dir);
    if (next >= 0) setSelectedIndex(next, ValueChangeSource::User);
    return true;
}

// ───────────────────────────────────────────────────────────────────
// Selection helpers
// ───────────────────────────────────────────────────────────────────

int FwDropDown::stepHighlight(int from, int direction) const {
    const int n = static_cast<int>(m_items.size());
    if (n == 0) return -1;
    // Seed so the first iteration's `(idx + direction + n) % n` lands
    // on the intended starting row:
    //   from >= 0:  seed = from, first step moves one over (normal).
    //   from < 0, direction > 0:  seed = n-1, first step lands on 0.
    //   from < 0, direction < 0:  seed = 0,   first step lands on n-1.
    int idx;
    if (from < 0) idx = (direction > 0) ? (n - 1) : 0;
    else          idx = from;
    for (int attempts = 0; attempts < n; ++attempts) {
        idx = (idx + direction + n) % n;
        const Item& it = m_items[idx];
        if (it.enabled && !it.separator) return idx;
    }
    return -1;
}

void FwDropDown::fireOnChange(ValueChangeSource src) {
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) {
        const std::string lbl = (m_selected >= 0) ? m_items[m_selected].label
                                                   : std::string{};
        m_onChange(m_selected, lbl);
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
