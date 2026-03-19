#pragma once

#include <functional>
#include <vector>
#include <cmath>

namespace yawn {
namespace ui {

// Forward declarations — widgets reference these by pointer/reference only
class Renderer2D;
class Font;

// Base class for all interactive UI widgets.
// Widgets use absolute screen coordinates and render via Renderer2D.
class Widget {
public:
    struct Bounds { float x = 0, y = 0, w = 0, h = 0; };

    virtual ~Widget() = default;

    void setBounds(float x, float y, float w, float h) {
        m_bounds = {x, y, w, h};
    }
    const Bounds& bounds() const { return m_bounds; }

    bool contains(float mx, float my) const {
        return mx >= m_bounds.x && mx < m_bounds.x + m_bounds.w &&
               my >= m_bounds.y && my < m_bounds.y + m_bounds.h;
    }

    virtual void render(Renderer2D& renderer, Font& font) = 0;

    // Mouse events return true if consumed (captures further events)
    virtual bool onMouseDown(float mx, float my, int button) { (void)mx; (void)my; (void)button; return false; }
    virtual bool onMouseUp(float mx, float my, int button) { (void)mx; (void)my; (void)button; return false; }
    virtual bool onMouseDrag(float mx, float my, float dx, float dy) { (void)mx; (void)my; (void)dx; (void)dy; return false; }
    virtual void onMouseEnter() { m_hovered = true; }
    virtual void onMouseLeave() { m_hovered = false; }
    virtual bool onKeyDown(int key, bool shift, bool ctrl) { (void)key; (void)shift; (void)ctrl; return false; }
    virtual bool onTextInput(const char* text) { (void)text; return false; }
    virtual void onFocusGained() { m_focused = true; }
    virtual void onFocusLost() { m_focused = false; }

    bool isHovered()  const { return m_hovered; }
    bool isPressed()  const { return m_pressed; }
    bool isFocused()  const { return m_focused; }
    bool isEnabled()  const { return m_enabled; }
    bool isVisible()  const { return m_visible; }
    void setEnabled(bool e) { m_enabled = e; }
    void setVisible(bool v) { m_visible = v; }

    using Callback      = std::function<void()>;
    using ValueCallback = std::function<void(float)>;
    using IndexCallback = std::function<void(int)>;

protected:
    Bounds m_bounds{};
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_focused = false;
    bool m_enabled = true;
    bool m_visible = true;
};

// Central input dispatcher. Tracks mouse capture, hover, and keyboard focus.
// Widgets are registered in back-to-front order (last = topmost).
class InputState {
public:
    void addWidget(Widget* w) {
        if (w) m_widgets.push_back(w);
    }

    void removeWidget(Widget* w) {
        for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it) {
            if (*it == w) { m_widgets.erase(it); break; }
        }
        if (m_captured == w) m_captured = nullptr;
        if (m_hovered == w)  m_hovered = nullptr;
        if (m_focused == w)  m_focused = nullptr;
    }

    void clear() {
        m_widgets.clear();
        m_captured = m_hovered = m_focused = nullptr;
    }

    // Call each frame before dispatching events to update widget list positions
    bool onMouseDown(float mx, float my, int button) {
        m_mouseX = mx; m_mouseY = my;
        if (m_captured) {
            return m_captured->onMouseDown(mx, my, button);
        }
        // Hit-test back-to-front (topmost first)
        for (int i = (int)m_widgets.size() - 1; i >= 0; --i) {
            auto* w = m_widgets[i];
            if (!w->isVisible() || !w->isEnabled()) continue;
            if (w->contains(mx, my)) {
                if (w->onMouseDown(mx, my, button)) {
                    m_captured = w;
                    setFocus(w);
                    return true;
                }
            }
        }
        setFocus(nullptr);
        return false;
    }

    bool onMouseUp(float mx, float my, int button) {
        m_mouseX = mx; m_mouseY = my;
        if (m_captured) {
            bool result = m_captured->onMouseUp(mx, my, button);
            m_captured = nullptr;
            updateHover(mx, my);
            return result;
        }
        return false;
    }

    bool onMouseMove(float mx, float my) {
        float dx = mx - m_mouseX, dy = my - m_mouseY;
        m_mouseX = mx; m_mouseY = my;
        if (m_captured) {
            return m_captured->onMouseDrag(mx, my, dx, dy);
        }
        updateHover(mx, my);
        return false;
    }

    bool onKeyDown(int key, bool shift, bool ctrl) {
        if (m_focused && m_focused->isVisible() && m_focused->isEnabled())
            return m_focused->onKeyDown(key, shift, ctrl);
        return false;
    }

    bool onTextInput(const char* text) {
        if (m_focused && m_focused->isVisible() && m_focused->isEnabled())
            return m_focused->onTextInput(text);
        return false;
    }

    Widget* captured() const { return m_captured; }
    Widget* hovered()  const { return m_hovered; }
    Widget* focused()  const { return m_focused; }
    float mouseX() const { return m_mouseX; }
    float mouseY() const { return m_mouseY; }

    void setFocus(Widget* w) {
        if (m_focused == w) return;
        if (m_focused) m_focused->onFocusLost();
        m_focused = w;
        if (m_focused) m_focused->onFocusGained();
    }

private:
    void updateHover(float mx, float my) {
        Widget* newHover = nullptr;
        for (int i = (int)m_widgets.size() - 1; i >= 0; --i) {
            auto* w = m_widgets[i];
            if (!w->isVisible() || !w->isEnabled()) continue;
            if (w->contains(mx, my)) { newHover = w; break; }
        }
        if (newHover != m_hovered) {
            if (m_hovered) m_hovered->onMouseLeave();
            m_hovered = newHover;
            if (m_hovered) m_hovered->onMouseEnter();
        }
    }

    std::vector<Widget*> m_widgets;
    Widget* m_captured = nullptr;
    Widget* m_hovered  = nullptr;
    Widget* m_focused  = nullptr;
    float m_mouseX = 0, m_mouseY = 0;
};

} // namespace ui
} // namespace yawn
