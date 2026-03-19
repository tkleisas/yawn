#pragma once

#include "ui/Widget.h"
#include "ui/Theme.h"
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {

// Menu bar rendered at the top of the main window.
// Supports nested menus with keyboard accelerators.
class MenuBar {
public:
    struct MenuItem {
        std::string label;
        std::string shortcut;    // display text: "Ctrl+S"
        Widget::Callback action;
        bool separator = false;  // draw separator line before this item
        bool enabled   = true;
        bool checked   = false;
    };

    struct Menu {
        std::string title;
        std::vector<MenuItem> items;
    };

    static constexpr float kMenuBarHeight = 26.0f;
    static constexpr float kMenuItemHeight = 24.0f;
    static constexpr float kMenuPadding = 10.0f;
    static constexpr float kMenuMinWidth = 220.0f;

    void addMenu(const std::string& title, std::vector<MenuItem> items) {
        int idx = (int)m_menus.size();
        m_menus.push_back({title, std::move(items)});
        recalcBounds();
    }

    float height() const { return kMenuBarHeight; }

    // Recalculate menu title bounds (called automatically on addMenu)
    void recalcBounds() {
        float x = 8.0f;
        for (int i = 0; i < (int)m_menus.size() && i < kMaxMenus; ++i) {
            float padW = (float)m_menus[i].title.size() * 6.0f + kMenuPadding * 2;
            m_menuBounds[i] = {x, 0, padW, kMenuBarHeight};
            x += padW;
        }
    }

    void render(Renderer2D& renderer, Font& font, float width) {
#ifndef YAWN_TEST_BUILD
        // Bar background
        renderer.drawRect(0, 0, width, kMenuBarHeight,
                          Color{38, 38, 42, 255});
        renderer.drawRect(0, kMenuBarHeight - 1, width, 1,
                          Color{55, 55, 60, 255});

        // Menu titles
        float x = 8.0f;
        float scale = 12.0f / Theme::kFontSize;
        for (int i = 0; i < (int)m_menus.size(); ++i) {
            float tw = font.textWidth(m_menus[i].title.c_str(), scale);
            float padW = tw + kMenuPadding * 2;
            m_menuBounds[i] = {x, 0, padW, kMenuBarHeight};

            // Highlight hovered or open menu
            if (i == m_openMenu || (m_openMenu < 0 && i == m_hoverMenu)) {
                Color bg = (i == m_openMenu)
                    ? Color{60, 60, 65, 255}
                    : Color{50, 50, 55, 255};
                renderer.drawRect(x, 0, padW, kMenuBarHeight, bg);
            }

            float ty = (kMenuBarHeight - font.lineHeight(scale)) * 0.5f;
            font.drawText(renderer, m_menus[i].title.c_str(),
                          x + kMenuPadding, ty, scale, Theme::textPrimary);
            x += padW;
        }

        // Open menu dropdown
        if (m_openMenu >= 0 && m_openMenu < (int)m_menus.size()) {
            renderDropDown(renderer, font, m_openMenu);
        }
#else
        (void)renderer; (void)font; (void)width;
        // Still compute bounds for tests
        float x = 8.0f;
        for (int i = 0; i < (int)m_menus.size() && i < kMaxMenus; ++i) {
            float padW = (float)m_menus[i].title.size() * 6.0f + kMenuPadding * 2;
            m_menuBounds[i] = {x, 0, padW, kMenuBarHeight};
            x += padW;
        }
#endif
    }

    // Returns true if the click was consumed
    bool handleClick(float mx, float my) {
        // Check dropdown items first
        if (m_openMenu >= 0) {
            const auto& menu = m_menus[m_openMenu];
            float itemX = m_menuBounds[m_openMenu].x;
            float itemY = kMenuBarHeight;
            float menuW = calcMenuWidth(menu);

            for (int i = 0; i < (int)menu.items.size(); ++i) {
                if (menu.items[i].separator) itemY += 6;
                float iy = itemY;
                itemY += kMenuItemHeight;
                if (mx >= itemX && mx < itemX + menuW &&
                    my >= iy && my < iy + kMenuItemHeight) {
                    if (menu.items[i].enabled && menu.items[i].action) {
                        menu.items[i].action();
                    }
                    m_openMenu = -1;
                    return true;
                }
            }
            // Click outside dropdown closes it
            m_openMenu = -1;
            // Fall through to check menu titles
        }

        // Check menu title bar
        if (my < kMenuBarHeight) {
            for (int i = 0; i < (int)m_menus.size() && i < kMaxMenus; ++i) {
                if (mx >= m_menuBounds[i].x &&
                    mx < m_menuBounds[i].x + m_menuBounds[i].w) {
                    m_openMenu = (m_openMenu == i) ? -1 : i;
                    return true;
                }
            }
        }
        return false;
    }

    // Handle mouse move for hover switching between menus
    bool handleMouseMove(float mx, float my) {
        m_hoverMenu = -1;
        m_hoverItem = -1;

        if (my < kMenuBarHeight) {
            for (int i = 0; i < (int)m_menus.size() && i < kMaxMenus; ++i) {
                if (mx >= m_menuBounds[i].x &&
                    mx < m_menuBounds[i].x + m_menuBounds[i].w) {
                    m_hoverMenu = i;
                    if (m_openMenu >= 0) m_openMenu = i;
                    break;
                }
            }
        }
        else if (m_openMenu >= 0) {
            // Check dropdown hover
            const auto& menu = m_menus[m_openMenu];
            float itemX = m_menuBounds[m_openMenu].x;
            float itemY = kMenuBarHeight;
            float menuW = calcMenuWidth(menu);
            for (int i = 0; i < (int)menu.items.size(); ++i) {
                if (menu.items[i].separator) itemY += 6;
                if (mx >= itemX && mx < itemX + menuW &&
                    my >= itemY && my < itemY + kMenuItemHeight)
                    m_hoverItem = i;
                itemY += kMenuItemHeight;
            }
        }

        return m_openMenu >= 0;
    }

    bool isOpen() const { return m_openMenu >= 0; }
    void close() { m_openMenu = -1; }

    // Hit test: does the mouse overlap the menubar or open dropdown?
    bool contains(float mx, float my) const {
        if (my < kMenuBarHeight) return true;
        if (m_openMenu >= 0 && m_openMenu < (int)m_menus.size()) {
            float menuW = calcMenuWidth(m_menus[m_openMenu]);
            float menuH = calcMenuHeight(m_menus[m_openMenu]);
            float ix = m_menuBounds[m_openMenu].x;
            return mx >= ix && mx < ix + menuW &&
                   my >= kMenuBarHeight && my < kMenuBarHeight + menuH;
        }
        return false;
    }

private:
    struct Rect { float x, y, w, h; };
    static constexpr int kMaxMenus = 8;

    float calcMenuWidth(const Menu& menu) const {
        // Use fixed width for simplicity
        return kMenuMinWidth;
    }

    float calcMenuHeight(const Menu& menu) const {
        float h = 0;
        for (const auto& item : menu.items) {
            h += kMenuItemHeight;
            if (item.separator) h += 6;
        }
        return h;
    }

    void renderDropDown([[maybe_unused]] Renderer2D& renderer,
                       [[maybe_unused]] Font& font,
                       [[maybe_unused]] int menuIdx) {
#ifndef YAWN_TEST_BUILD
        const auto& menu = m_menus[menuIdx];
        float x = m_menuBounds[menuIdx].x;
        float y = kMenuBarHeight;
        float w = calcMenuWidth(menu);
        float h = calcMenuHeight(menu);

        // Shadow
        renderer.drawRect(x + 2, y + 2, w, h, Color{10, 10, 10, 150});
        // Background
        renderer.drawRect(x, y, w, h, Color{45, 45, 50, 255});
        renderer.drawRectOutline(x, y, w, h, Color{65, 65, 70, 255});

        float iy = y;
        float scale = 12.0f / Theme::kFontSize;
        float textYOff = (kMenuItemHeight - font.lineHeight(scale)) * 0.5f;
        for (int i = 0; i < (int)menu.items.size(); ++i) {
            const auto& item = menu.items[i];
            if (item.separator) {
                renderer.drawRect(x + 4, iy + 2, w - 8, 1, Color{60, 60, 65, 255});
                iy += 6;
            }

            if (i == m_hoverItem && item.enabled) {
                renderer.drawRect(x + 2, iy, w - 4, kMenuItemHeight,
                                  Color{65, 100, 160, 255});
            }

            Color tc = item.enabled ? Theme::textPrimary : Theme::textDim;
            font.drawText(renderer, item.label.c_str(), x + 8, iy + textYOff, scale, tc);

            if (!item.shortcut.empty()) {
                float sw = font.textWidth(item.shortcut.c_str(), scale);
                font.drawText(renderer, item.shortcut.c_str(),
                              x + w - sw - 10, iy + textYOff, scale, Theme::textDim);
            }

            if (item.checked) {
                renderer.drawRect(x + w - 20, iy + 8, 8, 8,
                                  Color{80, 200, 80, 255});
            }

            iy += kMenuItemHeight;
        }
#endif
    }

    std::vector<Menu> m_menus;
    Rect m_menuBounds[kMaxMenus] = {};
    int  m_openMenu  = -1;
    int  m_hoverMenu = -1;
    int  m_hoverItem = -1;
};

} // namespace ui
} // namespace yawn
