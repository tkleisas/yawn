#pragma once

// ContextMenu — floating popup menu triggered by right-click.
// Reuses MenuItem structure from MenuBar.

#include "ui/Widget.h"
#include "ui/Theme.h"
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {

class ContextMenu {
public:
    struct Item {
        std::string label;
        Widget::Callback action;
        bool separator = false;
        bool enabled   = true;
        std::vector<Item> submenu; // non-empty = opens submenu on hover
    };

    static constexpr float kItemHeight = 24.0f;
    static constexpr float kMenuWidth  = 200.0f;
    static constexpr float kSubmenuOverlap = 4.0f;

    bool isOpen() const { return m_open; }

    void open(float x, float y, std::vector<Item> items) {
        m_x = x;
        m_y = y;
        m_items = std::move(items);
        m_open = true;
        m_hoverItem = -1;
        m_submenuOpen = -1;
        m_submenuHover = -1;
    }

    void close() {
        m_open = false;
        m_items.clear();
        m_hoverItem = -1;
        m_submenuOpen = -1;
        m_submenuHover = -1;
    }

    // Returns true if click was consumed
    bool handleClick(float mx, float my) {
        if (!m_open) return false;

        // Check submenu first
        if (m_submenuOpen >= 0 && m_submenuOpen < (int)m_items.size()) {
            const auto& sub = m_items[m_submenuOpen].submenu;
            float sx = m_x + kMenuWidth - kSubmenuOverlap;
            float sy = m_y + itemYOffset(m_submenuOpen);
            if (mx >= sx && mx < sx + kMenuWidth) {
                float iy = sy;
                for (int i = 0; i < (int)sub.size(); ++i) {
                    if (sub[i].separator) iy += 6;
                    if (my >= iy && my < iy + kItemHeight) {
                        if (sub[i].enabled && sub[i].action) {
                            sub[i].action();
                        }
                        close();
                        return true;
                    }
                    iy += kItemHeight;
                }
            }
        }

        // Check main menu
        float iy = m_y;
        for (int i = 0; i < (int)m_items.size(); ++i) {
            if (m_items[i].separator) iy += 6;
            if (mx >= m_x && mx < m_x + kMenuWidth &&
                my >= iy && my < iy + kItemHeight) {
                if (m_items[i].enabled) {
                    if (!m_items[i].submenu.empty()) {
                        // Toggle submenu
                        return true;
                    }
                    if (m_items[i].action) {
                        m_items[i].action();
                    }
                }
                close();
                return true;
            }
            iy += kItemHeight;
        }

        // Clicked outside — close
        close();
        return true;
    }

    void handleMouseMove(float mx, float my) {
        if (!m_open) return;
        m_hoverItem = -1;
        m_submenuHover = -1;

        // Check submenu hover
        if (m_submenuOpen >= 0 && m_submenuOpen < (int)m_items.size()) {
            const auto& sub = m_items[m_submenuOpen].submenu;
            float sx = m_x + kMenuWidth - kSubmenuOverlap;
            float sy = m_y + itemYOffset(m_submenuOpen);
            float siy = sy;
            for (int i = 0; i < (int)sub.size(); ++i) {
                if (sub[i].separator) siy += 6;
                if (mx >= sx && mx < sx + kMenuWidth &&
                    my >= siy && my < siy + kItemHeight) {
                    m_submenuHover = i;
                    return;
                }
                siy += kItemHeight;
            }
        }

        // Check main menu hover
        float iy = m_y;
        for (int i = 0; i < (int)m_items.size(); ++i) {
            if (m_items[i].separator) iy += 6;
            if (mx >= m_x && mx < m_x + kMenuWidth &&
                my >= iy && my < iy + kItemHeight) {
                m_hoverItem = i;
                if (!m_items[i].submenu.empty()) {
                    m_submenuOpen = i;
                } else {
                    m_submenuOpen = -1;
                }
                return;
            }
            iy += kItemHeight;
        }
    }

    // Hit test — is the point inside this menu or its submenu?
    bool contains(float mx, float my) const {
        if (!m_open) return false;
        float totalH = menuHeight(m_items);
        if (mx >= m_x && mx < m_x + kMenuWidth &&
            my >= m_y && my < m_y + totalH) return true;
        // Check submenu bounds
        if (m_submenuOpen >= 0 && m_submenuOpen < (int)m_items.size()) {
            const auto& sub = m_items[m_submenuOpen].submenu;
            float sx = m_x + kMenuWidth - kSubmenuOverlap;
            float sy = m_y + itemYOffset(m_submenuOpen);
            float sh = menuHeight(sub);
            if (mx >= sx && mx < sx + kMenuWidth &&
                my >= sy && my < sy + sh) return true;
        }
        return false;
    }

    void render([[maybe_unused]] Renderer2D& renderer,
                [[maybe_unused]] Font& font) {
#ifndef YAWN_TEST_BUILD
        if (!m_open) return;
        renderMenu(renderer, font, m_x, m_y, m_items, m_hoverItem);

        // Render submenu
        if (m_submenuOpen >= 0 && m_submenuOpen < (int)m_items.size() &&
            !m_items[m_submenuOpen].submenu.empty()) {
            float sx = m_x + kMenuWidth - kSubmenuOverlap;
            float sy = m_y + itemYOffset(m_submenuOpen);
            renderMenu(renderer, font, sx, sy,
                       m_items[m_submenuOpen].submenu, m_submenuHover);
        }
#endif
    }

private:
    float menuHeight(const std::vector<Item>& items) const {
        float h = 0;
        for (const auto& item : items) {
            h += kItemHeight;
            if (item.separator) h += 6;
        }
        return h;
    }

    float itemYOffset(int index) const {
        float y = 0;
        for (int i = 0; i < index && i < (int)m_items.size(); ++i) {
            if (m_items[i].separator) y += 6;
            y += kItemHeight;
        }
        if (index < (int)m_items.size() && m_items[index].separator) y += 6;
        return y;
    }

    void renderMenu([[maybe_unused]] Renderer2D& renderer,
                     [[maybe_unused]] Font& font,
                     [[maybe_unused]] float x, [[maybe_unused]] float y,
                     [[maybe_unused]] const std::vector<Item>& items,
                     [[maybe_unused]] int hoverIdx) {
#ifndef YAWN_TEST_BUILD
        float h = menuHeight(items);
        // Shadow
        renderer.drawRect(x + 2, y + 2, kMenuWidth, h, Color{10, 10, 10, 150});
        // Background
        renderer.drawRect(x, y, kMenuWidth, h, Color{45, 45, 50, 255});
        renderer.drawRectOutline(x, y, kMenuWidth, h, Color{65, 65, 70, 255});

        float scale = 12.0f / Theme::kFontSize;
        float textYOff = (kItemHeight - font.lineHeight(scale)) * 0.5f;
        float iy = y;
        for (int i = 0; i < (int)items.size(); ++i) {
            const auto& item = items[i];
            if (item.separator) {
                renderer.drawRect(x + 4, iy + 2, kMenuWidth - 8, 1, Color{60, 60, 65, 255});
                iy += 6;
            }
            if (i == hoverIdx && item.enabled) {
                renderer.drawRect(x + 2, iy, kMenuWidth - 4, kItemHeight,
                                  Color{65, 100, 160, 255});
            }
            Color tc = item.enabled ? Theme::textPrimary : Theme::textDim;
            font.drawText(renderer, item.label.c_str(), x + 8, iy + textYOff, scale, tc);

            // Submenu arrow indicator
            if (!item.submenu.empty()) {
                font.drawText(renderer, ">", x + kMenuWidth - 18, iy + textYOff, scale, Theme::textDim);
            }
            iy += kItemHeight;
        }
#endif
    }

    std::vector<Item> m_items;
    float m_x = 0, m_y = 0;
    bool m_open = false;
    int m_hoverItem = -1;
    int m_submenuOpen = -1;
    int m_submenuHover = -1;
};

} // namespace ui
} // namespace yawn
