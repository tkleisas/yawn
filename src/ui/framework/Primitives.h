#pragma once
// Primitive UI widgets for the YAWN framework.
//
// These are the building blocks used by all panels:
// Label, Button, Fader, Knob, Toggle, TextInput, NumberInput, DropDown.
//
// Each widget stores its own visual/interaction state and renders via
// paint(UIContext&). Actual Renderer2D/Font calls are guarded by
// #ifndef YAWN_TEST_BUILD so tests can exercise logic without OpenGL.

#include "Widget.h"
#include "../Theme.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Only include rendering headers in non-test builds
#ifndef YAWN_TEST_BUILD
#include "../Renderer.h"
#include "../Font.h"
#endif

namespace yawn {
namespace ui {
namespace fw {

// ═══════════════════════════════════════════════════════════════════════════
// Label
// ═══════════════════════════════════════════════════════════════════════════

class Label : public Widget {
public:
    Label() = default;
    explicit Label(const std::string& text, TextAlign align = TextAlign::Left)
        : m_text(text), m_align(align) {}

    void setText(const std::string& t) { m_text = t; }
    const std::string& text() const { return m_text; }
    void setAlign(TextAlign a) { m_align = a; }
    TextAlign align() const { return m_align; }
    void setColor(Color c) { m_color = c; }
    void setFontScale(float s) { m_fontScale = s; }

    Size measure(const Constraints& c, const UIContext&) override {
        // Estimate: 8px per char, 20px tall
        float w = m_text.size() * 8.0f;
        float h = 20.0f;
        return c.constrain({w, h});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

private:
    std::string m_text;
    TextAlign m_align = TextAlign::Left;
    Color m_color = Theme::textPrimary;
    float m_fontScale = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Button
// ═══════════════════════════════════════════════════════════════════════════

class FwButton : public Widget {
public:
    using Callback = std::function<void()>;

    FwButton() = default;
    explicit FwButton(const std::string& label, Callback onClick = nullptr)
        : m_label(label), m_onClick(std::move(onClick)) {}

    void setLabel(const std::string& label) { m_label = label; }
    const std::string& label() const { return m_label; }
    void setOnClick(Callback cb) { m_onClick = std::move(cb); }
    void setColor(Color bg) { m_bgColor = bg; m_customColor = true; }
    void setTextColor(Color c) { m_textColor = c; m_customTextColor = true; }
    void setDrawOutline(bool v) { m_drawOutline = v; }
    void setToggle(bool isToggle) { m_isToggle = isToggle; }
    void setToggleState(bool on) { m_toggleOn = on; }
    bool toggleState() const { return m_toggleOn; }
    bool isPressed() const { return m_pressed; }

    Size measure(const Constraints& c, const UIContext&) override {
        float w = m_label.size() * 8.0f + 16.0f;
        float h = 28.0f;
        return c.constrain({w, h});
    }

    float hoverAlpha() const { return m_hoverAlpha; }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {
        constexpr float kHoverSpeed = 0.15f;
        if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
        else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);
    }
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_pressed = true;
        captureMouse();
        return true;
    }

    bool onMouseUp(MouseEvent& e) override {
        bool wasPressed = m_pressed;
        m_pressed = false;
        releaseMouse();
        Point local = toLocal(e.x, e.y);
        if (wasPressed && hitTest(local.x, local.y)) {
            if (m_isToggle) m_toggleOn = !m_toggleOn;
            if (m_onClick) m_onClick();
            return true;
        }
        return false;
    }

private:
    std::string m_label;
    Callback m_onClick;
    Color m_bgColor{};
    bool m_customColor = false;
    Color m_textColor = Theme::textPrimary;
    bool m_customTextColor = false;
    bool m_drawOutline = true;
    bool m_isToggle = false;
    bool m_toggleOn = false;
    bool m_pressed = false;
    float m_hoverAlpha = 0.0f;
};

// ═══════════════════════════════════════════════════════════════════════════
// Toggle
// ═══════════════════════════════════════════════════════════════════════════

class FwToggle : public Widget {
public:
    using ChangeCallback = std::function<void(bool)>;

    FwToggle() = default;
    FwToggle(const std::string& label, bool initial = false)
        : m_label(label), m_on(initial) {}

    void setLabel(const std::string& l) { m_label = l; }
    void setOn(bool on) { m_on = on; }
    bool isOn() const { return m_on; }
    void setOnChange(ChangeCallback cb) { m_onChange = std::move(cb); }
    void setOnColor(Color c) { m_onColor = c; }

    Size measure(const Constraints& c, const UIContext&) override {
        float w = m_label.size() * 8.0f + 16.0f;
        float h = 24.0f;
        return c.constrain({w, h});
    }

    float hoverAlpha() const { return m_hoverAlpha; }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {
        constexpr float kHoverSpeed = 0.15f;
        if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
        else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);
    }
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        return true;
    }

    bool onMouseUp(MouseEvent& e) override {
        Point local = toLocal(e.x, e.y);
        if (hitTest(local.x, local.y)) {
            m_on = !m_on;
            if (m_onChange) m_onChange(m_on);
            return true;
        }
        return false;
    }

private:
    std::string m_label;
    bool m_on = false;
    float m_hoverAlpha = 0.0f;
    ChangeCallback m_onChange;
    Color m_onColor{200, 100, 40, 255};
};

// ═══════════════════════════════════════════════════════════════════════════
// Fader (vertical slider)
// ═══════════════════════════════════════════════════════════════════════════

class FwFader : public Widget {
public:
    using ValueCallback = std::function<void(float)>;

    FwFader() = default;

    void setValue(float v) { m_value = detail::cclamp(v, m_min, m_max); }
    float value() const { return m_value; }
    void setRange(float mn, float mx) { m_min = mn; m_max = mx; m_value = detail::cclamp(m_value, mn, mx); }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setTrackColor(Color c) { m_trackColor = c; }
    void setSensitivity(float s) { m_sensitivity = s; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({20.0f, 120.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_dragging = true;
        m_lastY = e.y;
        captureMouse();
        return true;
    }

    bool onMouseUp(MouseEvent&) override {
        m_dragging = false;
        releaseMouse();
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_dragging) return false;
        float dy = e.y - m_lastY;
        m_lastY = e.y;
        float range = m_max - m_min;
        float delta = -dy * m_sensitivity * range / m_bounds.h;
        float newVal = detail::cclamp(m_value + delta, m_min, m_max);
        if (newVal != m_value) {
            m_value = newVal;
            if (m_onChange) m_onChange(m_value);
        }
        return true;
    }

private:
    float m_value = 0.7f;
    float m_min = 0.0f, m_max = 2.0f;
    float m_sensitivity = 1.0f;
    bool m_dragging = false;
    float m_lastY = 0;
    ValueCallback m_onChange;
    Color m_trackColor{80, 200, 80, 255};
};

// ═══════════════════════════════════════════════════════════════════════════
// Knob (rotary control)
// ═══════════════════════════════════════════════════════════════════════════

class FwKnob : public Widget {
public:
    using ValueCallback = std::function<void(float)>;
    using FormatCallback = std::function<std::string(float)>;

    FwKnob() = default;

    void setValue(float v) { m_value = detail::cclamp(v, m_min, m_max); }
    float value() const { return m_value; }
    void setRange(float mn, float mx) { m_min = mn; m_max = mx; }
    void setDefault(float d) { m_default = d; }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setLabel(const std::string& l) { m_label = l; }
    void setSensitivity(float s) { m_sensitivity = s; }
    void setStep(float s) { m_step = s; }  // snap to step (0 = continuous)

    // Boolean mode: renders as full on/off circle instead of arc sweep
    void setBoolean(bool b) { m_boolean = b; }
    bool isBoolean() const { return m_boolean; }

    // Custom arc colors (default: blue)
    void setArcColor(Color c) { m_arcColor = c; m_customColor = true; }
    void setArcColorActive(Color c) { m_arcColorActive = c; m_customColor = true; }

    // Value format callback for displaying value + unit below knob
    void setFormatCallback(FormatCallback cb) { m_formatCb = std::move(cb); }

    bool isEditing() const { return m_editing; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({40.0f, 50.0f});
    }

    float hoverAlpha() const { return m_hoverAlpha; }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {
        constexpr float kHoverSpeed = 0.15f;
        if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
        else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);
    }
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button == MouseButton::Left) {
            if (m_editing) return true; // let key events handle edit mode
            if (m_boolean) {
                float mid = (m_min + m_max) * 0.5f;
                m_value = (m_value > mid) ? m_min : m_max;
                if (m_onChange) m_onChange(m_value);
                return true;
            }
            // Double-click detection → enter edit mode
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastClickTime).count();
            m_lastClickTime = now;
            if (elapsed < 300) {
                startEditing();
                return true;
            }
            m_dragging = true;
            m_lastY = e.y;
            captureMouse();
            return true;
        }
        if (e.button == MouseButton::Right) {
            if (m_editing) { cancelEdit(); return true; }
            m_value = m_default;
            if (m_onChange) m_onChange(m_value);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        m_dragging = false;
        releaseMouse();
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_dragging) return false;
        float dy = e.y - m_lastY;
        m_lastY = e.y;
        float range = m_max - m_min;
        float delta = -dy * m_sensitivity * range / 200.0f;
        float newVal = detail::cclamp(m_value + delta, m_min, m_max);
        if (m_step > 0) newVal = std::round(newVal / m_step) * m_step;
        if (newVal != m_value) {
            m_value = newVal;
            if (m_onChange) m_onChange(m_value);
        }
        return true;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (!m_editing) return false;
        if (e.isEnter()) { commitEdit(); return true; }
        if (e.isEscape()) { cancelEdit(); return true; }
        if (e.isBackspace() && m_editLen > 0) {
            m_editText[--m_editLen] = '\0';
            return true;
        }
        return true;
    }

    bool onTextInput(TextInputEvent& e) override {
        if (!m_editing) return false;
        for (const char* p = e.text; *p && m_editLen < 15; ++p) {
            char c = *p;
            if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
                m_editText[m_editLen++] = c;
                m_editText[m_editLen] = '\0';
            }
        }
        return true;
    }

    void onFocusChanged(FocusEvent& e) override {
        if (!e.gained && m_editing) commitEdit();
    }

private:
    void startEditing() {
        m_editing = true;
        m_editLen = 0;
        m_editText[0] = '\0';
    }

    void commitEdit() {
        m_editing = false;
        if (m_editLen > 0) {
            float v = static_cast<float>(std::atof(m_editText));
            v = detail::cclamp(v, m_min, m_max);
            if (m_step > 0) v = std::round(v / m_step) * m_step;
            m_value = v;
            if (m_onChange) m_onChange(m_value);
        }
    }

    void cancelEdit() { m_editing = false; }

#ifndef YAWN_TEST_BUILD
    void renderArc(Renderer2D& renderer, float cx, float cy, float r,
                   float startNorm, float endNorm, Color color);
#endif

    float m_value = 0.0f;
    float m_min = 0.0f, m_max = 1.0f;
    float m_default = 0.0f;
    float m_sensitivity = 1.0f;
    float m_step = 0.0f;  // 0 = continuous
    bool m_dragging = false;
    float m_lastY = 0;
    ValueCallback m_onChange;
    FormatCallback m_formatCb;
    std::string m_label;

    float m_hoverAlpha = 0.0f;

    // Boolean mode
    bool m_boolean = false;

    // Custom colors
    bool m_customColor = false;
    Color m_arcColor{0, 200, 255, 255};
    Color m_arcColorActive{0, 255, 255, 255};

    // Edit mode (double-click to type exact value)
    bool m_editing = false;
    char m_editText[16]{};
    int m_editLen = 0;
    std::chrono::steady_clock::time_point m_lastClickTime{};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextInput
// ═══════════════════════════════════════════════════════════════════════════

class FwTextInput : public Widget {
public:
    using ChangeCallback = std::function<void(const std::string&)>;

    FwTextInput() = default;

    void setText(const std::string& t) { m_text = t; m_cursor = static_cast<int>(t.size()); }
    const std::string& text() const { return m_text; }
    void setPlaceholder(const std::string& p) { m_placeholder = p; }
    void setOnChange(ChangeCallback cb) { m_onChange = std::move(cb); }
    void setOnCommit(ChangeCallback cb) { m_onCommit = std::move(cb); }
    bool isEditing() const { return m_editing; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({120.0f, 24.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        startEditing();
        return true;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (!m_editing) return false;
        if (e.isEnter()) { commitEdit(); return true; }
        if (e.isEscape()) { cancelEdit(); return true; }
        if (e.isBackspace() && m_cursor > 0) {
            m_text.erase(m_cursor - 1, 1);
            m_cursor--;
            if (m_onChange) m_onChange(m_text);
            return true;
        }
        if (e.isDelete() && m_cursor < static_cast<int>(m_text.size())) {
            m_text.erase(m_cursor, 1);
            if (m_onChange) m_onChange(m_text);
            return true;
        }
        return true;
    }

    bool onTextInput(TextInputEvent& e) override {
        if (!m_editing) return false;
        m_text.insert(m_cursor, e.text);
        m_cursor += static_cast<int>(std::strlen(e.text));
        if (m_onChange) m_onChange(m_text);
        return true;
    }

    void onFocusChanged(FocusEvent& e) override {
        if (!e.gained && m_editing) commitEdit();
    }

private:
    void startEditing() {
        m_editing = true;
        m_cursor = static_cast<int>(m_text.size());
        m_savedText = m_text;
    }

    void commitEdit() {
        m_editing = false;
        if (m_onCommit) m_onCommit(m_text);
    }

    void cancelEdit() {
        m_editing = false;
        m_text = m_savedText;
    }

    std::string m_text;
    std::string m_placeholder;
    std::string m_savedText;
    int m_cursor = 0;
    bool m_editing = false;
    ChangeCallback m_onChange;
    ChangeCallback m_onCommit;
};

// ═══════════════════════════════════════════════════════════════════════════
// NumberInput
// ═══════════════════════════════════════════════════════════════════════════

class FwNumberInput : public Widget {
public:
    using ValueCallback = std::function<void(float)>;

    FwNumberInput() = default;

    void setValue(float v) { m_value = detail::cclamp(v, m_min, m_max); }
    float value() const { return m_value; }
    void setRange(float mn, float mx) { m_min = mn; m_max = mx; }
    void setFormat(const std::string& fmt) { m_format = fmt; }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setSensitivity(float s) { m_sensitivity = s; }
    void setSuffix(const std::string& s) { m_suffix = s; }
    bool isEditing() const { return m_editing; }
    void beginEdit() { startEditing(); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({60.0f, 22.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_dragging = true;
        m_lastY = e.y;
        return true;
    }

    bool onMouseUp(MouseEvent&) override {
        m_dragging = false;
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_dragging || m_editing) return false;
        float dy = e.y - m_lastY;
        m_lastY = e.y;
        float range = m_max - m_min;
        float delta = -dy * m_sensitivity * range / 200.0f;
        float newVal = detail::cclamp(m_value + delta, m_min, m_max);
        if (newVal != m_value) {
            m_value = newVal;
            if (m_onChange) m_onChange(m_value);
        }
        return true;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (!m_editing) {
            if (e.isEnter()) { startEditing(); return true; }
            return false;
        }
        if (e.isEnter()) { commitEdit(); return true; }
        if (e.isEscape()) { cancelEdit(); return true; }
        if (e.isBackspace() && m_editLen > 0) {
            m_editText[--m_editLen] = '\0';
            return true;
        }
        return true;
    }

    bool onTextInput(TextInputEvent& e) override {
        if (!m_editing) return false;
        for (int i = 0; e.text[i] && m_editLen < 15; ++i) {
            char c = e.text[i];
            if ((c >= '0' && c <= '9') || c == '.' || c == '-')
                m_editText[m_editLen++] = c;
        }
        m_editText[m_editLen] = '\0';
        return true;
    }

    void onFocusChanged(FocusEvent& e) override {
        if (!e.gained && m_editing) commitEdit();
    }

private:
    void startEditing() {
        m_editing = true;
        m_editLen = 0;
        m_editText[0] = '\0';
    }

public:
    void commitEdit() {
        m_editing = false;
        if (m_editLen > 0) {
            float v = static_cast<float>(std::atof(m_editText));
            v = detail::cclamp(v, m_min, m_max);
            if (v != m_value) {
                m_value = v;
                if (m_onChange) m_onChange(m_value);
            }
        }
    }

    void cancelEdit() { m_editing = false; }

private:
    float m_value = 0.0f;
    float m_min = 0.0f, m_max = 1000.0f;
    float m_sensitivity = 1.0f;
    std::string m_format = "%.1f";
    std::string m_suffix;
    ValueCallback m_onChange;
    bool m_hovered = false;
    bool m_dragging = false;
    float m_lastY = 0;
    bool m_editing = false;
    char m_editText[16] = {};
    int  m_editLen = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// DropDown
// ═══════════════════════════════════════════════════════════════════════════

class FwDropDown : public Widget {
public:
    using IndexCallback = std::function<void(int)>;

    FwDropDown() = default;

    void setItems(const std::vector<std::string>& items) { m_items = items; }
    void setSelected(int index) { m_selected = index; }
    int selected() const { return m_selected; }
    std::string selectedText() const {
        if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
            return m_items[m_selected];
        return "";
    }
    void setOnChange(IndexCallback cb) { m_onChange = std::move(cb); }
    bool isOpen() const { return m_open; }
    void close() { m_open = false; m_hoverItem = -1; }

    int maxVisible() const { return std::min(static_cast<int>(m_items.size()), 8); }
    float popupTop() const { return m_bounds.y + m_bounds.h; }
    float popupBottom() const { return popupTop() + maxVisible() * m_bounds.h; }
    float popupLeft() const { return m_bounds.x; }
    float popupRight() const { return m_bounds.x + m_bounds.w; }

    bool hitPopup(float mx, float my) const {
        if (!m_open) return false;
        return mx >= popupLeft() && mx < popupRight() && my >= popupTop() && my < popupBottom();
    }

    bool handlePopupClick(float mx, float my) {
        if (!m_open) return false;
        float relY = my - popupTop();
        float itemH = m_bounds.h;
        int idx = static_cast<int>(relY / itemH);
        if (idx >= 0 && idx < maxVisible()) {
            m_selected = idx;
            m_open = false;
            m_hoverItem = -1;
            if (m_onChange) m_onChange(m_selected);
            return true;
        }
        m_open = false;
        m_hoverItem = -1;
        return true;
    }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({100.0f, 24.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

#ifdef YAWN_TEST_BUILD
    void paintOverlay(UIContext&) override {}
#else
    void paintOverlay(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;

        if (m_open) {
            if (hitPopup(e.x, e.y)) {
                return handlePopupClick(e.x, e.y);
            }
            m_open = false;
            m_hoverItem = -1;
            return true;
        }

        m_open = true;
        m_hoverItem = -1;
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_open) return false;
        if (hitPopup(e.x, e.y)) {
            float relY = e.y - popupTop();
            m_hoverItem = static_cast<int>(relY / m_bounds.h);
            if (m_hoverItem < 0 || m_hoverItem >= maxVisible()) m_hoverItem = -1;
        } else {
            m_hoverItem = -1;
        }
        return true;
    }

private:
    std::vector<std::string> m_items;
    int m_selected = 0;
    int m_hoverItem = -1;
    bool m_open = false;
    IndexCallback m_onChange;
};

// ═══════════════════════════════════════════════════════════════════════════
// MeterWidget (stereo VU meter, display-only)
// ═══════════════════════════════════════════════════════════════════════════

class MeterWidget : public Widget {
public:
    MeterWidget() = default;

    void setPeak(float left, float right) { m_peakL = left; m_peakR = right; }
    float peakL() const { return m_peakL; }
    float peakR() const { return m_peakR; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({16.0f, 60.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    static float dbToHeight(float peak) {
        if (peak < 0.001f) return 0.0f;
        float db = 20.0f * std::log10(peak);
        return detail::cclamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    }

    static Color meterColor(float peak) {
        if (peak > 1.0f) return {255, 40, 40};
        if (peak > 0.7f) return {255, 200, 50};
        return {80, 220, 80};
    }

private:
    float m_peakL = 0.0f;
    float m_peakR = 0.0f;
};

// ═══════════════════════════════════════════════════════════════════════════
// PanWidget (horizontal pan bar, -1..+1)
// ═══════════════════════════════════════════════════════════════════════════

class PanWidget : public Widget {
public:
    using ValueCallback = std::function<void(float)>;

    PanWidget() = default;

    void setValue(float v) { m_value = detail::cclamp(v, -1.0f, 1.0f); }
    float value() const { return m_value; }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setThumbColor(Color c) { m_thumbColor = c; }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({60.0f, 16.0f});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button == MouseButton::Left) {
            m_dragging = true;
            m_lastX = e.x;
            captureMouse();
            return true;
        }
        if (e.button == MouseButton::Right) {
            m_value = 0.0f;
            if (m_onChange) m_onChange(m_value);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        m_dragging = false;
        releaseMouse();
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_dragging) return false;
        float dx = e.x - m_lastX;
        m_lastX = e.x;
        float panW = m_bounds.w;
        float delta = (dx / panW) * 2.0f;
        float newVal = detail::cclamp(m_value + delta, -1.0f, 1.0f);
        if (newVal != m_value) {
            m_value = newVal;
            if (m_onChange) m_onChange(m_value);
        }
        return true;
    }

private:
    float m_value = 0.0f;
    bool m_dragging = false;
    float m_lastX = 0;
    ValueCallback m_onChange;
    Color m_thumbColor{100, 180, 255};
};

// ═══════════════════════════════════════════════════════════════════════════
// ScrollBar (horizontal scrollbar with thumb drag)
// ═══════════════════════════════════════════════════════════════════════════

class ScrollBar : public Widget {
public:
    using ScrollCallback = std::function<void(float)>;

    ScrollBar() = default;

    void setContentSize(float size) { m_contentSize = size; }
    float contentSize() const { return m_contentSize; }
    void setScrollPos(float pos) { m_scrollPos = pos; }
    float scrollPos() const { return m_scrollPos; }
    void setOnScroll(ScrollCallback cb) { m_onScroll = std::move(cb); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, kDefaultHeight});
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        if (m_contentSize <= m_bounds.w) return false;

        float maxScroll = m_contentSize - m_bounds.w;
        float thumbW = std::max(20.0f,
            m_bounds.w * (m_bounds.w / std::max(1.0f, m_contentSize)));
        float scrollFrac = m_scrollPos / std::max(1.0f, maxScroll);
        float thumbX = m_bounds.x + scrollFrac * (m_bounds.w - thumbW);

        if (e.x >= thumbX && e.x < thumbX + thumbW) {
            m_dragging = true;
            m_dragStartX = e.x;
            m_dragStartScroll = m_scrollPos;
            captureMouse();
        } else {
            float clickFrac = (e.x - m_bounds.x) / m_bounds.w;
            m_scrollPos = detail::cclamp(clickFrac * maxScroll, 0.0f, maxScroll);
            if (m_onScroll) m_onScroll(m_scrollPos);
        }
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        float localY = e.y - m_bounds.y;
        m_hovered = (localY >= 0 && localY < m_bounds.h);

        if (m_dragging && m_contentSize > m_bounds.w) {
            float dx = e.x - m_dragStartX;
            float scrollDelta = dx * (m_contentSize / std::max(1.0f, m_bounds.w));
            float maxScroll = m_contentSize - m_bounds.w;
            m_scrollPos = detail::cclamp(m_dragStartScroll + scrollDelta, 0.0f, maxScroll);
            if (m_onScroll) m_onScroll(m_scrollPos);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_dragging) {
            m_dragging = false;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (m_contentSize <= m_bounds.w) return false;
        float maxScroll = m_contentSize - m_bounds.w;
        m_scrollPos = detail::cclamp(m_scrollPos - e.dx * 30.0f, 0.0f, maxScroll);
        if (m_onScroll) m_onScroll(m_scrollPos);
        return true;
    }

private:
    static constexpr float kDefaultHeight = 12.0f;

    float m_contentSize = 0.0f;
    float m_scrollPos = 0.0f;
    bool m_dragging = false;
    bool m_hovered = false;
    float m_dragStartX = 0;
    float m_dragStartScroll = 0;
    ScrollCallback m_onScroll;
};

} // namespace fw
} // namespace ui
} // namespace yawn
