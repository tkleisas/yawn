#include "RadioGroup.h"

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
} // anon

// ───────────────────────────────────────────────────────────────────
// FwRadioButton
// ───────────────────────────────────────────────────────────────────

FwRadioButton::FwRadioButton() {
    setFocusable(true);
    setRelayoutBoundary(true);
    setClickOnly(true);
}

FwRadioButton::FwRadioButton(std::string label) : FwRadioButton() {
    m_label = std::move(label);
}

void FwRadioButton::setLabel(std::string l) {
    if (l == m_label) return;
    m_label = std::move(l);
    invalidate();
}

void FwRadioButton::setSelected(bool s, ValueChangeSource src) {
    if (s == m_selected) return;
    m_selected = s;
    if (m_group) return;   // grouped buttons silently track group state
    if (src == ValueChangeSource::Automation) return;
    if (m_onChange) m_onChange(s);
}

void FwRadioButton::setMinWidth(float w) {
    if (w == m_minWidth) return;
    m_minWidth = w;
    invalidate();
}

float FwRadioButton::measureLabelWidth(UIContext& ctx) const {
    if (m_label.empty()) return 0.0f;
    const float fs = theme().metrics.fontSize;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(m_label, fs);
    return static_cast<float>(utf8CodepointCount(m_label)) * kFallbackPxPerChar;
}

Size FwRadioButton::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float circleSize = m.controlHeight * 0.6f;
    const float gap        = m.baseUnit;
    const float labelW     = measureLabelWidth(ctx);

    float w = circleSize;
    if (labelW > 0.0f) w += gap + labelW;
    if (m_minWidth > 0.0f) w = std::max(w, m_minWidth);
    w = std::max(w, c.minW);
    w = std::min(w, c.maxW);

    float h = m.controlHeight;
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

void FwRadioButton::onClick(const ClickEvent& /*e*/) {
    if (!m_enabled) return;
    if (m_group) {
        m_group->_notifyButtonClicked(this, ValueChangeSource::User);
        return;
    }
    // Standalone — self-toggle.
    const bool next = !m_selected;
    m_selected = next;
    if (m_onChange) m_onChange(next);
}

void FwRadioButton::onRightClick(const ClickEvent& e) {
    if (!m_enabled) return;
    if (m_onRightClick) m_onRightClick(e.screen);
}

bool FwRadioButton::onKeyDown(KeyEvent& e) {
    if (!m_enabled || e.consumed) return false;
    if (e.key == Key::Space || e.key == Key::Enter) {
        onClick(ClickEvent{});
        return true;
    }
    return false;
}

// ───────────────────────────────────────────────────────────────────
// FwRadioGroup
// ───────────────────────────────────────────────────────────────────

FwRadioGroup::FwRadioGroup() {
    setFocusable(false);
    setRelayoutBoundary(false);
}

FwRadioGroup::FwRadioGroup(std::vector<std::string> options) : FwRadioGroup() {
    for (auto& o : options) addOption(std::move(o));
}

FwRadioGroup::~FwRadioGroup() {
    // Clear back-pointers so dangling callbacks don't touch us.
    for (auto& b : m_buttons) if (b) b->_setGroup(nullptr);
}

// ─── Options ─────────────────────────────────────────────────────
//
// Buttons are NOT added to m_children — we own them in m_buttons and
// drive their render / measure / layout directly. This keeps their
// m_parent null so callers that want to dispatch mouse events to an
// individual button (hit-testing by its absolute bounds) don't have
// to worry about the parent-walk in globalBounds.

void FwRadioGroup::addOption(std::string label) {
    auto btn = std::make_unique<FwRadioButton>(std::move(label));
    btn->_setGroup(this);
    if (m_accentOverride) btn->setAccentColor(*m_accentOverride);
    m_buttons.push_back(std::move(btn));
    invalidate();
}

void FwRadioGroup::clearOptions() {
    for (auto& b : m_buttons) if (b) b->_setGroup(nullptr);
    m_buttons.clear();
    m_selected = -1;
    invalidate();
}

const std::string& FwRadioGroup::optionLabel(int idx) const {
    static const std::string empty;
    if (idx < 0 || idx >= static_cast<int>(m_buttons.size())) return empty;
    return m_buttons[idx]->label();
}

FwRadioButton* FwRadioGroup::button(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_buttons.size())) return nullptr;
    return m_buttons[idx].get();
}

// ─── Selection ───────────────────────────────────────────────────

void FwRadioGroup::setSelectedIndex(int idx, ValueChangeSource src) {
    if (idx < 0 || idx >= static_cast<int>(m_buttons.size())) {
        idx = m_allowDeselect ? -1 : m_selected;
    }
    if (idx == m_selected) return;
    m_selected = idx;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        if (m_buttons[i])
            m_buttons[i]->setSelected(i == m_selected,
                                       ValueChangeSource::Programmatic);
    }
    fireOnChangeIfUser(src);
}

const std::string& FwRadioGroup::selectedLabel() const {
    static const std::string empty;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_buttons.size()))
        return empty;
    return m_buttons[m_selected]->label();
}

bool FwRadioGroup::_notifyButtonClicked(FwRadioButton* btn,
                                         ValueChangeSource src) {
    if (!btn) return true;
    int idx = -1;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        if (m_buttons[i].get() == btn) { idx = i; break; }
    }
    if (idx < 0) return true;

    if (idx == m_selected) {
        if (m_allowDeselect) {
            m_selected = -1;
            btn->setSelected(false, ValueChangeSource::Programmatic);
            fireOnChangeIfUser(src);
        }
        return true;
    }
    setSelectedIndex(idx, src);
    return true;
}

void FwRadioGroup::fireOnChangeIfUser(ValueChangeSource src) {
    if (src == ValueChangeSource::Automation) return;
    if (!m_onChange) return;
    static const std::string empty;
    const std::string& label =
        (m_selected >= 0 && m_selected < static_cast<int>(m_buttons.size()))
            ? m_buttons[m_selected]->label() : empty;
    m_onChange(m_selected, label);
}

// ─── Layout ──────────────────────────────────────────────────────

void FwRadioGroup::setOrientation(RadioOrientation o) {
    if (o == m_orientation) return;
    m_orientation = o;
    invalidate();
}

void FwRadioGroup::setGap(float px) {
    if (px == m_gap) return;
    m_gap = px;
    invalidate();
}

void FwRadioGroup::setAccentColor(Color c) {
    m_accentOverride = c;
    for (auto& b : m_buttons) if (b) b->setAccentColor(c);
}

void FwRadioGroup::clearAccentColor() {
    m_accentOverride.reset();
    for (auto& b : m_buttons) if (b) b->clearAccentColor();
}

Size FwRadioGroup::onMeasure(Constraints c, UIContext& ctx) {
    const float gap = (m_gap > 0.0f) ? m_gap : theme().metrics.baseUnit * 2.0f;
    const bool row = (m_orientation == RadioOrientation::Horizontal);
    float mainTotal = 0.0f;
    float crossMax  = 0.0f;
    bool  first     = true;

    for (auto& b : m_buttons) {
        if (!b) continue;
        Constraints cc;
        cc.minW = 0.0f; cc.minH = 0.0f;
        cc.maxW = c.maxW; cc.maxH = c.maxH;
        Size s = b->measure(cc, ctx);
        if (!first) mainTotal += gap;
        first = false;
        if (row) { mainTotal += s.w; crossMax = std::max(crossMax, s.h); }
        else     { mainTotal += s.h; crossMax = std::max(crossMax, s.w); }
    }

    Size out = row ? Size{mainTotal, crossMax} : Size{crossMax, mainTotal};
    return c.constrain(out);
}

void FwRadioGroup::render(UIContext& ctx) {
    if (!m_visible) return;
    // The group paints nothing of its own — straight delegate to the
    // buttons.
    for (auto& b : m_buttons) {
        if (b && b->isVisible()) b->render(ctx);
    }
}

void FwRadioGroup::onLayout(Rect bounds, UIContext& ctx) {
    // Position each button in absolute coords (same convention as
    // FlexBox). Caller-facing: `group.button(i).bounds()` returns
    // absolute coords, matching how leaf widgets like FwDropDown
    // behave when driven by a standalone dispatcher.
    const float gap = (m_gap > 0.0f) ? m_gap : theme().metrics.baseUnit * 2.0f;
    const bool row = (m_orientation == RadioOrientation::Horizontal);
    float cursor = row ? bounds.x : bounds.y;

    for (auto& b : m_buttons) {
        if (!b) continue;
        Constraints cc;
        cc.minW = 0.0f; cc.maxW = bounds.w;
        cc.minH = 0.0f; cc.maxH = bounds.h;
        Size s = b->measure(cc, ctx);

        Rect r;
        if (row) {
            r = Rect{cursor, bounds.y, s.w, std::min(s.h, bounds.h)};
            cursor += s.w + gap;
        } else {
            r = Rect{bounds.x, cursor, std::min(s.w, bounds.w), s.h};
            cursor += s.h + gap;
        }
        b->layout(r, ctx);
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
