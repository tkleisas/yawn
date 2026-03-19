#pragma once

#include "ui/Widget.h"
#include "ui/Theme.h"
#include "ui/MenuBar.h"
#include "instruments/Instrument.h"
#include "effects/AudioEffect.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace ui {

// Collapsible bottom panel that shows parameters for the selected
// instrument or effect.  Displays a row of knobs (one per parameter)
// grouped in a horizontal scroll area.

class DetailPanel {
public:
    static constexpr float kPanelHeight    = 220.0f;
    static constexpr float kCollapsedHeight = 28.0f;
    static constexpr float kKnobSize       = 56.0f;
    static constexpr float kKnobSpacing    = 14.0f;
    static constexpr float kRowHeight      = 90.0f;
    static constexpr float kHeaderHeight   = 28.0f;
    static constexpr float kLabelHeight    = 16.0f;
    static constexpr float kRowPadding     = 6.0f;

    bool isOpen() const { return m_open; }
    void setOpen(bool open) { m_open = open; }
    void toggle() { m_open = !m_open; }

    float height() const { return m_open ? kPanelHeight : kCollapsedHeight; }

    // Show parameters for an instrument
    void showInstrument(instruments::Instrument* inst) {
        m_instrument = inst;
        m_effect = nullptr;
        m_open = true;
        rebuildParams();
    }

    // Show parameters for an effect
    void showEffect(effects::AudioEffect* fx) {
        m_effect = fx;
        m_instrument = nullptr;
        m_open = true;
        rebuildParams();
    }

    void clear() {
        m_instrument = nullptr;
        m_effect = nullptr;
        m_params.clear();
    }

    const char* title() const {
        if (m_instrument) return m_instrument->name();
        if (m_effect) return m_effect->name();
        return "No Selection";
    }

    // Render the panel
    void render([[maybe_unused]] Renderer2D& renderer,
                [[maybe_unused]] Font& font,
                [[maybe_unused]] float x, [[maybe_unused]] float y,
                [[maybe_unused]] float width) {
#ifndef YAWN_TEST_BUILD
        float h = height();

        // Header bar (always visible)
        renderer.drawRect(x, y, width, kHeaderHeight, Color{35, 35, 40, 255});
        renderer.drawRect(x, y, width, 1, Color{55, 55, 60, 255});

        float headerScale = 14.0f / Theme::kFontSize;

        // Toggle arrow
        const char* arrow = m_open ? "▼" : "▶";
        font.drawText(renderer, arrow, x + 8, y + 6, headerScale, Theme::textSecondary);

        // Title
        font.drawText(renderer, title(), x + 26, y + 6, headerScale, Theme::textPrimary);

        if (!m_open) return;

        // Panel body
        float bodyY = y + kHeaderHeight;
        float bodyH = h - kHeaderHeight;
        renderer.drawRect(x, bodyY, width, bodyH, Color{32, 32, 36, 255});

        if (m_params.empty()) {
            font.drawText(renderer, "No parameters", x + 20, bodyY + 20, headerScale,
                          Theme::textDim);
            return;
        }

        // Multi-row knob layout
        float cellW = kKnobSize + kKnobSpacing;
        int knobsPerRow = std::max(1, (int)((width - 24.0f) / cellW));
        float startX = x + 12;

        for (int i = 0; i < (int)m_params.size(); ++i) {
            auto& p = m_params[i];
            int row = i / knobsPerRow;
            int col = i % knobsPerRow;

            float knobX = startX + col * cellW - m_scrollX;
            float knobY = bodyY + kRowPadding + row * kRowHeight;

            // Skip if off-screen vertically
            if (knobY + kRowHeight < bodyY || knobY > y + h)
                continue;
            // Skip if off-screen horizontally
            if (knobX + kKnobSize < x || knobX > x + width)
                continue;

            float value = currentValue(p.index);
            float norm = (value - p.minVal) / (p.maxVal - p.minVal);
            norm = std::clamp(norm, 0.0f, 1.0f);

            // Knob background
            float cx = knobX + kKnobSize * 0.5f;
            float cy = knobY + kKnobSize * 0.4f;
            float r = kKnobSize * 0.35f;

            // Background arc
            renderArc(renderer, cx, cy, r, 0.0f, 1.0f, Color{50, 50, 55, 255});

            // Value arc
            Color arcCol = (p.index == m_activeParam)
                ? Color{120, 200, 255, 255}
                : Color{80, 160, 210, 255};
            if (p.isBoolean) {
                arcCol = (value > 0.5f) ? Color{80, 200, 80, 255} : Color{80, 80, 85, 255};
                norm = (value > 0.5f) ? 1.0f : 0.0f;
            }
            renderArc(renderer, cx, cy, r, 0.0f, norm, arcCol);

            // Indicator dot
            float angle = (float)(-M_PI * 0.75 + norm * M_PI * 1.5);
            float dotX = cx + std::cos(angle) * r * 0.65f - 2.0f;
            float dotY = cy + std::sin(angle) * r * 0.65f - 2.0f;
            renderer.drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

            // Value text
            char valBuf[32];
            formatValue(p, value, valBuf, sizeof(valBuf));
            float valScale = 12.0f / Theme::kFontSize;
            float valW = font.textWidth(valBuf, valScale);
            font.drawText(renderer, valBuf,
                          knobX + (kKnobSize - valW) * 0.5f,
                          knobY + kKnobSize * 0.72f, valScale, Theme::textSecondary);

            // Parameter name below
            float nameScale = 12.0f / Theme::kFontSize;
            float nameW = font.textWidth(p.name.c_str(), nameScale);
            float maxLabelW = kKnobSize + kKnobSpacing - 2;
            float tx = knobX + (kKnobSize - std::min(nameW, maxLabelW)) * 0.5f;
            font.drawText(renderer, p.name.c_str(), tx,
                          knobY + kKnobSize + 4, nameScale, Theme::textDim);
        }
#endif
    }

    // Handle click in panel area — returns true if consumed
    bool handleClick(float mx, float my, float panelX, float panelY, float panelW) {
        if (my < panelY || my > panelY + height()) return false;
        if (mx < panelX || mx > panelX + panelW) return false;

        // Header click toggles panel
        if (my < panelY + kHeaderHeight) {
            toggle();
            return true;
        }

        if (!m_open || m_params.empty()) return false;

        // Multi-row hit test
        float bodyY = panelY + kHeaderHeight;
        float cellW = kKnobSize + kKnobSpacing;
        int knobsPerRow = std::max(1, (int)((panelW - 24.0f) / cellW));
        float startX = panelX + 12;

        for (int i = 0; i < (int)m_params.size(); ++i) {
            auto& p = m_params[i];
            int row = i / knobsPerRow;
            int col = i % knobsPerRow;

            float knobX = startX + col * cellW - m_scrollX;
            float knobY = bodyY + kRowPadding + row * kRowHeight;

            if (mx >= knobX && mx < knobX + kKnobSize &&
                my >= knobY && my < knobY + kKnobSize + kLabelHeight) {

                if (p.isBoolean) {
                    float cur = currentValue(p.index);
                    setParamValue(p.index, cur > 0.5f ? 0.0f : 1.0f);
                } else {
                    m_dragging = true;
                    m_activeParam = p.index;
                    m_dragStartY = my;
                    m_dragStartValue = currentValue(p.index);
                }
                return true;
            }
        }

        return true; // consume click in panel body
    }

    bool handleDrag(float, float my) {
        if (!m_dragging) return false;

        auto it = std::find_if(m_params.begin(), m_params.end(),
                               [this](const ParamEntry& p) { return p.index == m_activeParam; });
        if (it == m_params.end()) return false;

        float range = it->maxVal - it->minVal;
        float deltaY = m_dragStartY - my;
        float sensitivity = range / 150.0f;
        float newVal = std::clamp(m_dragStartValue + deltaY * sensitivity,
                                  it->minVal, it->maxVal);
        setParamValue(m_activeParam, newVal);
        return true;
    }

    void handleRelease() {
        m_dragging = false;
        m_activeParam = -1;
    }

    bool isDragging() const { return m_dragging; }

    // Right-click reset to default
    bool handleRightClick(float mx, float my, float panelX, float panelY, float panelW) {
        if (!m_open || my < panelY + kHeaderHeight) return false;
        if (mx < panelX || mx > panelX + panelW) return false;

        float bodyY = panelY + kHeaderHeight;
        float cellW = kKnobSize + kKnobSpacing;
        int knobsPerRow = std::max(1, (int)((panelW - 24.0f) / cellW));
        float startX = panelX + 12;

        for (int i = 0; i < (int)m_params.size(); ++i) {
            auto& p = m_params[i];
            int row = i / knobsPerRow;
            int col = i % knobsPerRow;

            float knobX = startX + col * cellW - m_scrollX;
            float knobY = bodyY + kRowPadding + row * kRowHeight;

            if (mx >= knobX && mx < knobX + kKnobSize &&
                my >= knobY && my < knobY + kKnobSize + kLabelHeight) {
                setParamValue(p.index, p.defaultVal);
                return true;
            }
        }
        return false;
    }

private:
    struct ParamEntry {
        int index;
        std::string name;
        std::string unit;
        float minVal, maxVal, defaultVal;
        bool isBoolean;
    };

    void rebuildParams() {
        m_params.clear();
        m_scrollX = 0;
        m_activeParam = -1;

        int count = 0;
        if (m_instrument) count = m_instrument->parameterCount();
        else if (m_effect) count = m_effect->parameterCount();

        for (int i = 0; i < count; ++i) {
            ParamEntry entry;
            entry.index = i;
            if (m_instrument) {
                const auto& info = m_instrument->parameterInfo(i);
                entry.name = info.name;
                entry.unit = info.unit;
                entry.minVal = info.minValue;
                entry.maxVal = info.maxValue;
                entry.defaultVal = info.defaultValue;
                entry.isBoolean = info.isBoolean;
            } else if (m_effect) {
                const auto& info = m_effect->parameterInfo(i);
                entry.name = info.name;
                entry.unit = info.unit;
                entry.minVal = info.minValue;
                entry.maxVal = info.maxValue;
                entry.defaultVal = info.defaultValue;
                entry.isBoolean = info.isBoolean;
            }
            m_params.push_back(std::move(entry));
        }
    }

    float currentValue(int paramIndex) const {
        if (m_instrument) return m_instrument->getParameter(paramIndex);
        if (m_effect) return m_effect->getParameter(paramIndex);
        return 0.0f;
    }

    void setParamValue(int paramIndex, float value) {
        if (m_instrument) m_instrument->setParameter(paramIndex, value);
        else if (m_effect) m_effect->setParameter(paramIndex, value);
    }

    void formatValue(const ParamEntry& p, float value, char* buf, int bufSize) const {
        if (p.isBoolean) {
            std::snprintf(buf, bufSize, "%s", value > 0.5f ? "On" : "Off");
        } else if (p.unit == "Hz") {
            if (value >= 1000.0f) std::snprintf(buf, bufSize, "%.1fk", value / 1000.0f);
            else std::snprintf(buf, bufSize, "%.0f", value);
        } else if (p.unit == "dB") {
            std::snprintf(buf, bufSize, "%.1f", value);
        } else if (p.unit == "ms") {
            std::snprintf(buf, bufSize, "%.0f", value);
        } else if (p.unit == "%") {
            std::snprintf(buf, bufSize, "%.0f%%", value * 100.0f);
        } else {
            std::snprintf(buf, bufSize, "%.2f", value);
        }
    }

    void renderArc([[maybe_unused]] Renderer2D& renderer,
                   [[maybe_unused]] float cx, [[maybe_unused]] float cy,
                   [[maybe_unused]] float r,
                   [[maybe_unused]] float startNorm, [[maybe_unused]] float endNorm,
                   [[maybe_unused]] Color color) {
#ifndef YAWN_TEST_BUILD
        const int segs = 24;
        float startAngle = (float)(-M_PI * 0.75 + startNorm * M_PI * 1.5);
        float endAngle   = (float)(-M_PI * 0.75 + endNorm   * M_PI * 1.5);
        float step = (endAngle - startAngle) / segs;
        for (int i = 0; i < segs; ++i) {
            float a = startAngle + step * (i + 0.5f);
            float px = cx + std::cos(a) * r - 2.0f;
            float py = cy + std::sin(a) * r - 2.0f;
            renderer.drawRect(px, py, 4, 4, color);
        }
#endif
    }

    instruments::Instrument* m_instrument = nullptr;
    effects::AudioEffect*    m_effect = nullptr;

    std::vector<ParamEntry> m_params;
    float m_scrollX = 0;

    bool  m_dragging = false;
    int   m_activeParam = -1;
    float m_dragStartY = 0;
    float m_dragStartValue = 0;

    bool m_open = false;
};

} // namespace ui
} // namespace yawn
