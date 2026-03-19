#pragma once

#include "ui/Widget.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace ui {

// ========================= Button =========================

class Button : public Widget {
public:
    Button() = default;
    Button(const char* label, Callback onClick = nullptr)
        : m_label(label), m_onClick(std::move(onClick)) {}

    void setLabel(const char* label) { m_label = label; }
    void setOnClick(Callback cb) { m_onClick = std::move(cb); }
    void setColor(Color bg) { m_bgColor = bg; m_customColor = true; }
    void setToggle(bool isToggle) { m_isToggle = isToggle; }
    void setToggleState(bool on) { m_toggleOn = on; }
    bool toggleState() const { return m_toggleOn; }

    void render(Renderer2D& renderer, Font& font) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        Color bg = m_customColor ? m_bgColor : Theme::panelBg;
        if (!m_enabled) bg = Color{50, 50, 53, 255};
        else if (m_pressed)  bg = Color{80, 80, 85, 255};
        else if (m_hovered)  bg = Color{65, 65, 70, 255};
        if (m_isToggle && m_toggleOn)
            bg = m_customColor ? m_bgColor : Color{200, 100, 40, 255};

        renderer.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        renderer.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                 Color{70, 70, 75, 255});
        if (!m_label.empty()) {
            float scale = std::min(m_bounds.h * 0.6f, 20.0f) / Theme::kFontSize;
            float tw = font.textWidth(m_label.c_str(), scale);
            float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
            float ty = m_bounds.y + m_bounds.h * 0.3f;
            Color tc = m_enabled ? Theme::textPrimary : Theme::textDim;
            font.drawText(renderer, m_label.c_str(), tx, ty, scale, tc);
        }
#else
        (void)renderer; (void)font;
#endif
    }

    bool onMouseDown(float, float, int button) override {
        if (button != 1) return false;
        m_pressed = true;
        return true;
    }

    bool onMouseUp(float mx, float my, int) override {
        bool wasPressed = m_pressed;
        m_pressed = false;
        if (wasPressed && contains(mx, my)) {
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
};

// ========================= Fader =========================
// Vertical slider. Drag up/down to change value (0..1 or custom range).

class Fader : public Widget {
public:
    Fader() = default;

    void setValue(float v) { m_value = std::clamp(v, m_min, m_max); }
    float value() const { return m_value; }
    void setRange(float mn, float mx) { m_min = mn; m_max = mx; m_value = std::clamp(m_value, mn, mx); }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setTrackColor(Color c) { m_trackColor = c; }
    void setSensitivity(float s) { m_sensitivity = s; }

    void render(Renderer2D& renderer, Font&) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        float cx = m_bounds.x + m_bounds.w * 0.5f;
        float trackW = 6.0f;
        float trackX = cx - trackW * 0.5f;

        // Track background
        renderer.drawRect(trackX, m_bounds.y, trackW, m_bounds.h,
                          Color{25, 25, 28, 255});

        // Filled portion
        float norm = (m_value - m_min) / (m_max - m_min);
        float fillH = m_bounds.h * norm;
        renderer.drawRect(trackX, m_bounds.y + m_bounds.h - fillH,
                          trackW, fillH, m_trackColor);

        // Knob (horizontal bar)
        float knobH = 6.0f;
        float knobW = m_bounds.w;
        float knobY = m_bounds.y + m_bounds.h - fillH - knobH * 0.5f;
        knobY = std::clamp(knobY, m_bounds.y, m_bounds.y + m_bounds.h - knobH);
        Color knobCol = m_pressed ? Color{220, 220, 220, 255}
                             : m_hovered ? Color{200, 200, 200, 255}
                                         : Color{180, 180, 180, 255};
        renderer.drawRect(m_bounds.x, knobY, knobW, knobH, knobCol);
#else
        (void)renderer;
#endif
    }

    bool onMouseDown(float, float, int button) override {
        if (button != 1) return false;
        m_pressed = true;
        return true;
    }

    bool onMouseUp(float, float, int) override {
        m_pressed = false;
        return true;
    }

    bool onMouseDrag(float, float, float, float dy) override {
        float range = m_max - m_min;
        float delta = -dy * m_sensitivity * range / m_bounds.h;
        float newVal = std::clamp(m_value + delta, m_min, m_max);
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
    ValueCallback m_onChange;
    Color m_trackColor{80, 200, 80, 255};
};

// ========================= Knob =========================
// Rotary control rendered as an arc. Vertical drag to change value.

class Knob : public Widget {
public:
    Knob() = default;

    void setValue(float v) { m_value = std::clamp(v, m_min, m_max); }
    float value() const { return m_value; }
    void setRange(float mn, float mx) { m_min = mn; m_max = mx; }
    void setDefault(float d) { m_default = d; }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setLabel(const char* l) { m_label = l; }
    void setSensitivity(float s) { m_sensitivity = s; }

    void render(Renderer2D& renderer, Font& font) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        float cx = m_bounds.x + m_bounds.w * 0.5f;
        float cy = m_bounds.y + m_bounds.h * 0.4f;
        float r = std::min(m_bounds.w, m_bounds.h * 0.7f) * 0.4f;

        // Background circle (approximated with rects)
        renderArc(renderer, cx, cy, r, 0.0f, 1.0f, Color{35, 35, 38, 255});

        // Value arc
        float norm = (m_value - m_min) / (m_max - m_min);
        Color arcCol = m_pressed ? Color{120, 200, 255, 255}
                            : m_hovered ? Color{100, 180, 230, 255}
                                        : Color{80, 160, 210, 255};
        renderArc(renderer, cx, cy, r, 0.0f, norm, arcCol);

        // Indicator dot
        float angle = (float)(-M_PI * 0.75 + norm * M_PI * 1.5);
        float dotX = cx + std::cos(angle) * r * 0.7f - 2;
        float dotY = cy + std::sin(angle) * r * 0.7f - 2;
        renderer.drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

        // Label below
        if (!m_label.empty()) {
            float scale = 16.0f / Theme::kFontSize;
            float tw = font.textWidth(m_label.c_str(), scale);
            float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
            float ty = m_bounds.y + m_bounds.h * 0.75f;
            font.drawText(renderer, m_label.c_str(), tx, ty, scale, Theme::textSecondary);
        }
#else
        (void)renderer; (void)font;
#endif
    }

    bool onMouseDown(float, float, int button) override {
        if (button == 1) { m_pressed = true; return true; }
        if (button == 3) { // Right-click: reset to default
            m_value = m_default;
            if (m_onChange) m_onChange(m_value);
            return true;
        }
        return false;
    }

    bool onMouseUp(float, float, int) override {
        m_pressed = false;
        return true;
    }

    bool onMouseDrag(float, float, float, float dy) override {
        float range = m_max - m_min;
        float delta = -dy * m_sensitivity * range / 200.0f;
        float newVal = std::clamp(m_value + delta, m_min, m_max);
        if (newVal != m_value) {
            m_value = newVal;
            if (m_onChange) m_onChange(m_value);
        }
        return true;
    }

private:
    // Render an arc as a series of small rectangles (16 segments)
    void renderArc([[maybe_unused]] Renderer2D& renderer, [[maybe_unused]] float cx,
                   [[maybe_unused]] float cy, [[maybe_unused]] float r,
                   [[maybe_unused]] float startNorm, [[maybe_unused]] float endNorm,
                   [[maybe_unused]] Color color) {
#ifndef YAWN_TEST_BUILD
        const int segs = 16;
        float startAngle = (float)(-M_PI * 0.75 + startNorm * M_PI * 1.5);
        float endAngle   = (float)(-M_PI * 0.75 + endNorm   * M_PI * 1.5);
        float step = (endAngle - startAngle) / segs;
        for (int i = 0; i < segs; ++i) {
            float a = startAngle + step * (i + 0.5f);
            float px = cx + std::cos(a) * r - 2;
            float py = cy + std::sin(a) * r - 2;
            renderer.drawRect(px, py, 4, 4, color);
        }
#endif
    }

    float m_value = 0.0f;
    float m_min = 0.0f, m_max = 1.0f;
    float m_default = 0.0f;
    float m_sensitivity = 1.0f;
    ValueCallback m_onChange;
    std::string m_label;
};

// ========================= Toggle =========================
// On/off button with LED-style indicator.

class Toggle : public Widget {
public:
    Toggle() = default;
    Toggle(const char* label, bool initial = false)
        : m_label(label), m_on(initial) {}

    void setLabel(const char* l) { m_label = l; }
    void setOn(bool on) { m_on = on; }
    bool isOn() const { return m_on; }
    void setOnChange(std::function<void(bool)> cb) { m_onToggle = std::move(cb); }
    void setOnColor(Color c) { m_onColor = c; }

    void render(Renderer2D& renderer, Font& font) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        Color bg = m_on ? m_onColor : Color{50, 50, 53, 255};
        if (m_pressed) bg = Color{bg.r + 20, bg.g + 20, bg.b + 20, 255};
        else if (m_hovered && !m_on) bg = Color{60, 60, 63, 255};

        renderer.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        renderer.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                 Color{70, 70, 75, 255});

        if (!m_label.empty()) {
            float scale = std::min(m_bounds.h * 0.55f, 18.0f) / Theme::kFontSize;
            float tw = font.textWidth(m_label.c_str(), scale);
            float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
            float ty = m_bounds.y + m_bounds.h * 0.25f;
            Color tc = m_on ? Color{255, 255, 255, 255} : Theme::textSecondary;
            font.drawText(renderer, m_label.c_str(), tx, ty, scale, tc);
        }
#else
        (void)renderer; (void)font;
#endif
    }

    bool onMouseDown(float, float, int button) override {
        if (button != 1) return false;
        m_pressed = true;
        return true;
    }

    bool onMouseUp(float mx, float my, int) override {
        bool wasPressed = m_pressed;
        m_pressed = false;
        if (wasPressed && contains(mx, my)) {
            m_on = !m_on;
            if (m_onToggle) m_onToggle(m_on);
            return true;
        }
        return false;
    }

private:
    std::string m_label;
    bool m_on = false;
    Color m_onColor{200, 80, 40, 255};
    std::function<void(bool)> m_onToggle;
};

// ========================= NumberBox =========================
// Numeric value display. Click+drag to change, double-click to type.

class NumberBox : public Widget {
public:
    NumberBox() = default;

    void setValue(float v) { m_value = std::clamp(v, m_min, m_max); }
    float value() const { return m_value; }
    void setRange(float mn, float mx) { m_min = mn; m_max = mx; }
    void setFormat(const char* fmt) { m_format = fmt; }
    void setOnChange(ValueCallback cb) { m_onChange = std::move(cb); }
    void setSensitivity(float s) { m_sensitivity = s; }
    void setSuffix(const char* s) { m_suffix = s; }

    void render(Renderer2D& renderer, Font& font) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        Color bg = m_focused ? Color{55, 55, 60, 255}
                        : m_hovered ? Color{48, 48, 52, 255}
                                    : Color{38, 38, 42, 255};
        renderer.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        renderer.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                 m_focused ? Color{100, 160, 220, 255}
                                           : Color{60, 60, 65, 255});

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

        float scale = std::min(m_bounds.h * 0.6f, 18.0f) / Theme::kFontSize;
        float tw = font.textWidth(buf, scale);
        float tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
        float ty = m_bounds.y + m_bounds.h * 0.2f;
        font.drawText(renderer, buf, tx, ty, scale, Theme::textPrimary);
#else
        (void)renderer; (void)font;
#endif
    }

    bool onMouseDown(float, float, int button) override {
        if (button != 1) return false;
        m_pressed = true;
        if (m_editing) return true;
        // TODO: detect double-click for edit mode
        return true;
    }

    bool onMouseUp(float, float, int) override {
        m_pressed = false;
        return true;
    }

    bool onMouseDrag(float, float, float, float dy) override {
        if (m_editing) return true;
        float range = m_max - m_min;
        float delta = -dy * m_sensitivity * range / 200.0f;
        float newVal = std::clamp(m_value + delta, m_min, m_max);
        if (newVal != m_value) {
            m_value = newVal;
            if (m_onChange) m_onChange(m_value);
        }
        return true;
    }

    bool onKeyDown(int key, bool, bool) override {
        if (!m_editing) {
            if (key == 13) { startEditing(); return true; } // Enter
            return false;
        }
        if (key == 13) { commitEdit(); return true; }  // Enter
        if (key == 27) { cancelEdit(); return true; }  // Escape
        if (key == 8 && m_editLen > 0) {                // Backspace
            m_editText[--m_editLen] = '\0';
            return true;
        }
        return true;
    }

    bool onTextInput(const char* text) override {
        if (!m_editing) return false;
        for (int i = 0; text[i] && m_editLen < 15; ++i) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || c == '.' || c == '-')
                m_editText[m_editLen++] = c;
        }
        m_editText[m_editLen] = '\0';
        return true;
    }

    void onFocusLost() override {
        if (m_editing) commitEdit();
        m_focused = false;
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
            float v = (float)std::atof(m_editText);
            v = std::clamp(v, m_min, m_max);
            if (v != m_value) {
                m_value = v;
                if (m_onChange) m_onChange(m_value);
            }
        }
    }

    void cancelEdit() {
        m_editing = false;
    }

    float m_value = 0.0f;
    float m_min = 0.0f, m_max = 1000.0f;
    float m_sensitivity = 1.0f;
    std::string m_format = "%.1f";
    std::string m_suffix;
    ValueCallback m_onChange;

    bool m_editing = false;
    char m_editText[16] = {};
    int  m_editLen = 0;
};

// ========================= DropDown =========================
// Click to open a list, select an item.

class DropDown : public Widget {
public:
    DropDown() = default;

    void setItems(const std::vector<std::string>& items) {
        m_items = items;
        if (m_selected >= (int)items.size()) m_selected = 0;
    }
    void setSelected(int index) { m_selected = std::clamp(index, 0, (int)m_items.size() - 1); }
    int selected() const { return m_selected; }
    const std::string& selectedText() const {
        static std::string empty;
        return m_selected < (int)m_items.size() ? m_items[m_selected] : empty;
    }
    void setOnChange(IndexCallback cb) { m_onChange = std::move(cb); }

    void render(Renderer2D& renderer, Font& font) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        Color bg = m_hovered ? Color{55, 55, 60, 255}
                                    : Color{42, 42, 46, 255};
        renderer.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);
        renderer.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                                 Color{70, 70, 75, 255});

        // Current value
        float scale = std::min(m_bounds.h * 0.55f, 16.0f) / Theme::kFontSize;
        if (m_selected < (int)m_items.size())
            font.drawText(renderer, m_items[m_selected].c_str(),
                          m_bounds.x + 4, m_bounds.y + m_bounds.h * 0.2f,
                          scale, Theme::textPrimary);

        // Arrow indicator
        float ax = m_bounds.x + m_bounds.w - 12;
        float ay = m_bounds.y + m_bounds.h * 0.4f;
        renderer.drawRect(ax, ay, 8, 2, Theme::textSecondary);
        renderer.drawRect(ax + 2, ay + 3, 4, 2, Theme::textSecondary);

        // Open list
        if (m_open) {
            float itemH = m_bounds.h;
            float listH = itemH * m_items.size();
            float listY = m_bounds.y + m_bounds.h;
            renderer.drawRect(m_bounds.x, listY, m_bounds.w, listH,
                              Color{50, 50, 55, 255});
            renderer.drawRectOutline(m_bounds.x, listY, m_bounds.w, listH,
                                     Color{80, 80, 85, 255});
            for (int i = 0; i < (int)m_items.size(); ++i) {
                float iy = listY + itemH * i;
                if (i == m_hoverItem)
                    renderer.drawRect(m_bounds.x + 1, iy, m_bounds.w - 2, itemH,
                                      Color{70, 70, 80, 255});
                font.drawText(renderer, m_items[i].c_str(),
                              m_bounds.x + 4, iy + itemH * 0.2f,
                              scale, Theme::textPrimary);
            }
        }
#else
        (void)renderer; (void)font;
#endif
    }

    bool onMouseDown(float mx, float my, int button) override {
        if (button != 1) return false;
        if (m_open) {
            float itemH = m_bounds.h;
            float listY = m_bounds.y + m_bounds.h;
            int idx = (int)((my - listY) / itemH);
            if (idx >= 0 && idx < (int)m_items.size()) {
                m_selected = idx;
                if (m_onChange) m_onChange(m_selected);
            }
            m_open = false;
            return true;
        }
        m_open = !m_open;
        m_pressed = true;
        return true;
    }

    bool onMouseUp(float, float, int) override {
        m_pressed = false;
        return true;
    }

    bool onMouseDrag(float, float my, float, float) override {
        if (m_open) {
            float itemH = m_bounds.h;
            float listY = m_bounds.y + m_bounds.h;
            m_hoverItem = (int)((my - listY) / itemH);
        }
        return m_open;
    }

    void onFocusLost() override {
        m_open = false;
        m_focused = false;
    }

    bool contains(float mx, float my) const {
        if (Widget::contains(mx, my)) return true;
        if (m_open) {
            float listH = m_bounds.h * m_items.size();
            float listY = m_bounds.y + m_bounds.h;
            return mx >= m_bounds.x && mx < m_bounds.x + m_bounds.w &&
                   my >= listY && my < listY + listH;
        }
        return false;
    }

private:
    std::vector<std::string> m_items;
    int m_selected = 0;
    int m_hoverItem = -1;
    bool m_open = false;
    IndexCallback m_onChange;
};

// ========================= TextLabel =========================
// Static text label (non-interactive unless editable).

class TextLabel : public Widget {
public:
    TextLabel() = default;
    TextLabel(const char* text) : m_text(text) {}

    void setText(const char* t) { m_text = t; }
    const std::string& text() const { return m_text; }
    void setColor(Color c) { m_color = c; }
    void setAlign(int align) { m_align = align; } // 0=left, 1=center, 2=right

    void render(Renderer2D& renderer, Font& font) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible || m_text.empty()) return;
        float scale = std::min(m_bounds.h * 0.65f, 18.0f) / Theme::kFontSize;
        float tw = font.textWidth(m_text.c_str(), scale);
        float tx = m_bounds.x;
        if (m_align == 1)      tx = m_bounds.x + (m_bounds.w - tw) * 0.5f;
        else if (m_align == 2) tx = m_bounds.x + m_bounds.w - tw;
        float ty = m_bounds.y + m_bounds.h * 0.2f;
        font.drawText(renderer, m_text.c_str(), tx, ty, scale, m_color);
#else
        (void)renderer; (void)font;
#endif
    }

private:
    std::string m_text;
    Color m_color = Theme::textPrimary;
    int m_align = 0;
};

// ========================= Meter =========================
// VU-style level meter (non-interactive, display only).

class Meter : public Widget {
public:
    void setLevel(float l, float r) { m_peakL = l; m_peakR = r; }

    void render(Renderer2D& renderer, Font&) override {
#ifndef YAWN_TEST_BUILD
        if (!m_visible) return;
        float barW = (m_bounds.w - 2) * 0.5f;

        // Background
        renderer.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{20, 20, 22, 255});

        auto drawBar = [&](float x, float peak) {
            float dB = (peak > 0.0001f) ? 20.0f * std::log10(peak) : -60.0f;
            float norm = std::clamp((dB + 60.0f) / 60.0f, 0.0f, 1.0f);
            float barH = m_bounds.h * norm;
            Color c = Color{60, 200, 60, 255};
            if (peak > 0.9f)      c = Color{255, 60, 60, 255};
            else if (peak > 0.7f) c = Color{255, 200, 50, 255};
            renderer.drawRect(x, m_bounds.y + m_bounds.h - barH, barW, barH, c);
        };

        drawBar(m_bounds.x, m_peakL);
        drawBar(m_bounds.x + barW + 2, m_peakR);
#else
        (void)renderer;
#endif
    }

private:
    float m_peakL = 0.0f, m_peakR = 0.0f;
};

} // namespace ui
} // namespace yawn
