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

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        float scale = m_fontScale > 0 ? m_fontScale
                     : detail::cmin(m_bounds.h * 0.7f, 20.0f) / Theme::kFontSize;
        float tw = ctx.font->textWidth(m_text, scale);
        float tx = m_bounds.x;
        if (m_align == TextAlign::Center)
            tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
        else if (m_align == TextAlign::Right)
            tx = m_bounds.x + m_bounds.w - tw;
        float ty = m_bounds.y + m_bounds.h * 0.2f;
        ctx.font->drawText(*ctx.renderer, m_text.c_str(), tx, ty, scale, m_color);
#endif
    }

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

    void paint(UIContext& ctx) override {
        // Frame-based hover interpolation (works at any frame rate)
        constexpr float kHoverSpeed = 0.15f;
        if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
        else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        Color bg = m_customColor ? m_bgColor : Theme::controlBg;
        if (!m_enabled) bg = Color{50, 50, 53, 255};
        else if (m_pressed) bg = Theme::controlActive;
        else if (m_hoverAlpha > 0.001f)
            bg = Color::lerp(bg, Theme::controlHover, m_hoverAlpha);
        if (m_isToggle && m_toggleOn)
            bg = m_customColor ? m_bgColor : Color{200, 100, 40, 255};

        ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                      Theme::controlBorder);
        if (!m_label.empty()) {
            float scale = detail::cmin(m_bounds.h * 0.6f, 20.0f) / Theme::kFontSize;
            float tw = ctx.font->textWidth(m_label, scale);
            float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
            float ty = m_bounds.y + m_bounds.h * 0.3f;
            // Press effect: shift text down 1px when pressed
            if (m_pressed) ty += 1.0f;
            Color tc = m_enabled ? Theme::textPrimary : Theme::textDim;
            ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, scale, tc);
        }
#endif
    }

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_pressed = true;
        return true;
    }

    bool onMouseUp(MouseEvent& e) override {
        bool wasPressed = m_pressed;
        m_pressed = false;
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

    void paint(UIContext& ctx) override {
        constexpr float kHoverSpeed = 0.15f;
        if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
        else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        Color bg = m_on ? m_onColor : Theme::toggleOffBg;
        if (!m_on && m_hoverAlpha > 0.001f)
            bg = Color::lerp(bg, Theme::toggleOffHover, m_hoverAlpha);
        ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                      Theme::controlBorder);
        // LED dot
        float dotR = 4.0f;
        float dotX = m_bounds.x + 8 - dotR;
        float dotY = m_bounds.y + m_bounds.h * 0.5f - dotR;
        Color dotCol = m_on ? Color{80, 255, 80, 255} : Color{80, 80, 80, 255};
        ctx.renderer->drawRect(dotX, dotY, dotR * 2, dotR * 2, dotCol);

        if (!m_label.empty()) {
            float scale = detail::cmin(m_bounds.h * 0.55f, 16.0f) / Theme::kFontSize;
            float tx = m_bounds.x + 18;
            float ty = m_bounds.y + m_bounds.h * 0.25f;
            ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, scale, Theme::textPrimary);
        }
#endif
    }

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

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer) return;
        float cx = m_bounds.x + m_bounds.w * 0.5f;
        float trackW = 6.0f;
        float trackX = cx - trackW * 0.5f;

        ctx.renderer->drawRect(trackX, m_bounds.y, trackW, m_bounds.h,
                               Color{25, 25, 28, 255});

        float norm = (m_value - m_min) / (m_max - m_min);
        float fillH = m_bounds.h * norm;
        ctx.renderer->drawRect(trackX, m_bounds.y + m_bounds.h - fillH,
                               trackW, fillH, m_trackColor);

        float knobH = 6.0f;
        float knobW = m_bounds.w;
        float knobY = m_bounds.y + m_bounds.h - fillH - knobH * 0.5f;
        knobY = detail::cclamp(knobY, m_bounds.y, m_bounds.y + m_bounds.h - knobH);
        Color knobCol = m_dragging ? Color{220, 220, 220, 255}
                                   : Color{180, 180, 180, 255};
        ctx.renderer->drawRect(m_bounds.x, knobY, knobW, knobH, knobCol);
#endif
    }

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

    // Boolean mode: renders as full on/off circle instead of arc sweep
    void setBoolean(bool b) { m_boolean = b; }
    bool isBoolean() const { return m_boolean; }

    // Custom arc colors (default: blue)
    void setArcColor(Color c) { m_arcColor = c; m_customColor = true; }
    void setArcColorActive(Color c) { m_arcColorActive = c; m_customColor = true; }

    // Value format callback for displaying value + unit below knob
    void setFormatCallback(FormatCallback cb) { m_formatCb = std::move(cb); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({40.0f, 50.0f});
    }

    float hoverAlpha() const { return m_hoverAlpha; }

    void paint(UIContext& ctx) override {
        constexpr float kHoverSpeed = 0.15f;
        if (m_hovered) m_hoverAlpha = detail::cmin(1.0f, m_hoverAlpha + kHoverSpeed);
        else           m_hoverAlpha = detail::cmax(0.0f, m_hoverAlpha - kHoverSpeed);

#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        float cx = m_bounds.x + m_bounds.w * 0.5f;
        float cy = m_bounds.y + m_bounds.h * 0.4f;
        float r = detail::cmin(m_bounds.w, m_bounds.h * 0.7f) * 0.4f;

        // Background arc
        renderArc(*ctx.renderer, cx, cy, r, 0.0f, 1.0f, Color{50, 50, 55, 255});

        float norm = (m_value - m_min) / (m_max - m_min);
        norm = detail::cclamp(norm, 0.0f, 1.0f);

        // Value arc — neon palette
        Color arcCol;
        if (m_boolean) {
            bool on = m_value > (m_min + m_max) * 0.5f;
            arcCol = on ? Color{57, 255, 20, 255} : Color{80, 80, 85, 255};
            norm = on ? 1.0f : 0.0f;
        } else if (m_customColor) {
            arcCol = m_dragging ? m_arcColorActive : m_arcColor;
        } else {
            arcCol = m_dragging ? Color{0, 255, 255, 255} : Color{0, 200, 255, 255};
        }
        // Brighten arc on hover
        if (m_hoverAlpha > 0.001f && !m_boolean) {
            Color bright{
                static_cast<uint8_t>(detail::cmin(255.0f, arcCol.r + 40.0f)),
                static_cast<uint8_t>(detail::cmin(255.0f, arcCol.g + 40.0f)),
                static_cast<uint8_t>(detail::cmin(255.0f, arcCol.b + 40.0f)),
                arcCol.a};
            arcCol = Color::lerp(arcCol, bright, m_hoverAlpha);
        }
        renderArc(*ctx.renderer, cx, cy, r, 0.0f, norm, arcCol);

        // Dot indicator
        float angle = static_cast<float>(-M_PI * 0.75 + norm * M_PI * 1.5);
        float dotX = cx + std::cos(angle) * r * 0.65f - 2;
        float dotY = cy + std::sin(angle) * r * 0.65f - 2;
        ctx.renderer->drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

        // Value text (from format callback)
        float valScale = 10.0f / Theme::kFontSize;
        if (m_formatCb) {
            std::string valStr = m_formatCb(m_value);
            float vw = ctx.font->textWidth(valStr, valScale);
            ctx.font->drawText(*ctx.renderer, valStr.c_str(),
                               m_bounds.x + (m_bounds.w - vw) * 0.5f,
                               m_bounds.y + m_bounds.h * 0.72f, valScale, Theme::textSecondary);
        }

        // Label below
        if (!m_label.empty()) {
            float nameScale = 10.0f / Theme::kFontSize;
            float tw = ctx.font->textWidth(m_label, nameScale);
            float maxW = m_bounds.w - 2.0f;
            float tx = m_bounds.x + (m_bounds.w - detail::cmin(tw, maxW)) * 0.5f;
            float ty = m_bounds.y + m_bounds.h * 0.85f;
            ctx.font->drawText(*ctx.renderer, m_label.c_str(), tx, ty, nameScale, Theme::textDim);
        }
#endif
    }

    bool onMouseDown(MouseEvent& e) override {
        if (e.button == MouseButton::Left) {
            if (m_boolean) {
                // Toggle on click
                float mid = (m_min + m_max) * 0.5f;
                m_value = (m_value > mid) ? m_min : m_max;
                if (m_onChange) m_onChange(m_value);
                return true;
            }
            m_dragging = true;
            m_lastY = e.y;
            return true;
        }
        if (e.button == MouseButton::Right) {
            m_value = m_default;
            if (m_onChange) m_onChange(m_value);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        m_dragging = false;
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_dragging) return false;
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

private:
#ifndef YAWN_TEST_BUILD
    void renderArc(Renderer2D& renderer, float cx, float cy, float r,
                   float startNorm, float endNorm, Color color) {
        const int segs = 24;
        float startAngle = static_cast<float>(-M_PI * 0.75 + startNorm * M_PI * 1.5);
        float endAngle   = static_cast<float>(-M_PI * 0.75 + endNorm   * M_PI * 1.5);
        float step = (endAngle - startAngle) / segs;
        for (int i = 0; i < segs; ++i) {
            float a = startAngle + step * (i + 0.5f);
            float px = cx + std::cos(a) * r - 2;
            float py = cy + std::sin(a) * r - 2;
            renderer.drawRect(px, py, 4, 4, color);
        }
    }
#endif

    float m_value = 0.0f;
    float m_min = 0.0f, m_max = 1.0f;
    float m_default = 0.0f;
    float m_sensitivity = 1.0f;
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

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        Color bg = m_editing ? Color{55, 55, 60, 255} : Color{38, 38, 42, 255};
        ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                      m_editing ? Color{100, 160, 220, 255} : Color{60, 60, 65, 255});

        float scale = detail::cmin(m_bounds.h * 0.6f, 18.0f) / Theme::kFontSize;
        float tx = m_bounds.x + 4;
        float ty = m_bounds.y + m_bounds.h * 0.2f;

        if (m_text.empty() && !m_editing) {
            ctx.font->drawText(*ctx.renderer, m_placeholder.c_str(), tx, ty, scale, Theme::textDim);
        } else {
            const std::string& display = m_editing ? m_text + "|" : m_text;
            ctx.font->drawText(*ctx.renderer, display.c_str(), tx, ty, scale, Theme::textPrimary);
        }
#endif
    }

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

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({60.0f, 22.0f});
    }

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        Color bg = m_editing ? Color{55, 55, 60, 255}
                  : m_hovered ? Color{48, 48, 52, 255}
                              : Color{38, 38, 42, 255};
        ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                      m_editing ? Color{100, 160, 220, 255} : Color{60, 60, 65, 255});

        char buf[64];
        if (m_editing) {
            std::snprintf(buf, sizeof(buf), "%s|", m_editText);
        } else {
            std::snprintf(buf, sizeof(buf), m_format.c_str(), m_value);
            if (!m_suffix.empty()) {
                size_t len = std::strlen(buf);
                std::snprintf(buf + len, sizeof(buf) - len, " %s", m_suffix.c_str());
            }
        }

        float scale = detail::cmin(m_bounds.h * 0.6f, 18.0f) / Theme::kFontSize;
        float tw = ctx.font->textWidth(buf, scale);
        float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
        float ty = m_bounds.y + m_bounds.h * 0.2f;
        ctx.font->drawText(*ctx.renderer, buf, tx, ty, scale, Theme::textPrimary);
#endif
    }

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

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({100.0f, 24.0f});
    }

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        Color bg = m_open ? Color{55, 55, 60, 255} : Color{42, 42, 46, 255};
        ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        ctx.renderer->drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                      Color{70, 70, 75, 255});

        float scale = detail::cmin(m_bounds.h * 0.6f, 18.0f) / Theme::kFontSize;
        float ty = m_bounds.y + m_bounds.h * 0.2f;

        // Current selection
        std::string txt = selectedText();
        ctx.font->drawText(*ctx.renderer, txt.c_str(), m_bounds.x + 6, ty, scale, Theme::textPrimary);

        // Arrow indicator
        ctx.font->drawText(*ctx.renderer, "v", m_bounds.x + m_bounds.w - 14, ty, scale, Theme::textSecondary);

        // Dropdown list
        if (m_open) {
            float itemH = m_bounds.h;
            float listY = m_bounds.y + m_bounds.h;
            for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                float iy = listY + i * itemH;
                Color itemBg = (i == m_hoverItem) ? Color{65, 65, 70, 255} : Color{48, 48, 52, 255};
                ctx.renderer->drawRect(m_bounds.x, iy, m_bounds.w, itemH, itemBg);
                ctx.renderer->drawRectOutline(m_bounds.x, iy, m_bounds.w, itemH, Color{60, 60, 65, 255});
                float itemTy = iy + itemH * 0.2f;
                ctx.font->drawText(*ctx.renderer, m_items[i].c_str(), m_bounds.x + 6, itemTy, scale, Theme::textPrimary);
            }
        }
#endif
    }

    bool onMouseDown(MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        Point local = toLocal(e.x, e.y);

        if (m_open) {
            float itemH = m_bounds.h;
            float listTop = m_bounds.h;
            for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                float iy = listTop + i * itemH;
                if (local.y >= iy && local.y < iy + itemH) {
                    m_selected = i;
                    m_open = false;
                    if (m_onChange) m_onChange(m_selected);
                    return true;
                }
            }
            m_open = false;
            return true;
        }

        m_open = true;
        return true;
    }

private:
    std::vector<std::string> m_items;
    int m_selected = 0;
    int m_hoverItem = -1;
    bool m_open = false;
    IndexCallback m_onChange;
};

} // namespace fw
} // namespace ui
} // namespace yawn
