#pragma once
// DeviceHeaderWidget — Composite header bar for audio devices in the detail panel.
//
// Renders a colored stripe, expand/collapse toggle, bypass button, device name,
// and optional remove button. Wraps the header rendering previously done inline
// in DetailPanelWidget::renderDevice().

#include "Widget.h"
#include "../Theme.h"
#include "../../util/Logger.h"
#include <string>
#include <functional>

#ifndef YAWN_TEST_BUILD
#include "../Renderer.h"
#include "../Font.h"
#endif

namespace yawn {
namespace ui {
namespace fw {

class DeviceHeaderWidget : public Widget {
public:
    using ActionCallback = std::function<void()>;
    using ToggleCallback = std::function<void(bool)>;

    enum class DeviceType { Instrument, AudioEffect, MidiEffect, Utility };

    DeviceHeaderWidget() = default;

    // Configuration
    void setDeviceName(const std::string& name) { m_name = name; }
    void setDeviceType(DeviceType type) { m_type = type; }
    void setBypassed(bool bypassed) { m_bypassed = bypassed; }
    bool isBypassed() const { return m_bypassed; }
    void setExpanded(bool expanded) { m_expanded = expanded; }
    bool isExpanded() const { return m_expanded; }
    void setRemovable(bool removable) { m_removable = removable; }

    // Callbacks
    void setOnExpandToggle(ToggleCallback cb) { m_onExpandToggle = std::move(cb); }
    void setOnBypassToggle(ToggleCallback cb) { m_onBypassToggle = std::move(cb); }
    void setOnRemove(ActionCallback cb) { m_onRemove = std::move(cb); }
    void setOnDragStart(ActionCallback cb) { m_onDragStart = std::move(cb); }

    // Preset system
    using PresetClickCallback = std::function<void(float x, float y)>;
    void setOnPresetClick(PresetClickCallback cb) { m_onPresetClick = std::move(cb); }
    void setPresetName(const std::string& name) { m_presetName = name; }

    // Device type color
    Color deviceColor() const {
        switch (m_type) {
            case DeviceType::Instrument:  return {60, 120, 200, 255};
            case DeviceType::AudioEffect: return {180, 80, 200, 255};
            case DeviceType::MidiEffect:  return {200, 160, 60, 255};
            case DeviceType::Utility:     return {100, 180, 100, 255};
        }
        return {180, 80, 200, 255};
    }

    static constexpr float kHeaderH  = 24.0f;
    static constexpr float kBtnSize  = 16.0f;
    static constexpr float kBypassW  = 24.0f;
    static constexpr float kStripeH  = 3.0f;

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return {c.maxW, kHeaderH};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        Widget::layout(bounds, ctx);
        float x = bounds.x;
        float y = bounds.y;
        float w = bounds.w;

        m_expandBtn = {x + 4, y + 4, kBtnSize, kBtnSize};
        m_bypassBtn = {x + 22, y + 4, kBypassW, kBtnSize};
        m_removeBtn = m_removable ? Rect{x + w - 20, y + 4, kBtnSize, kBtnSize}
                                  : Rect{};
        // m_presetBtn is computed in paint() where we have font metrics
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x;
        float y = m_bounds.y;
        float w = m_bounds.w;

        // Background
        Color bg = m_bypassed ? Color{30, 30, 34, 255} : Color{36, 36, 42, 255};
        r.drawRect(x, y, w, kHeaderH, bg);

        // Top color stripe
        Color stripe = deviceColor();
        if (m_bypassed) {
            stripe.r /= 2;
            stripe.g /= 2;
            stripe.b /= 2;
        }
        r.drawRect(x, y, w, kStripeH, stripe);

        float sml  = 10.0f / Theme::kFontSize;
        float tiny = 9.0f  / Theme::kFontSize;
        float med  = 12.0f / Theme::kFontSize;

        // Expand/collapse toggle
        const char* tog = m_expanded ? "\xe2\x96\xbc" : "\xe2\x96\xb6";
        f.drawText(r, tog, m_expandBtn.x, m_expandBtn.y, sml, Theme::textSecondary);

        // Bypass button
        Color bpBg  = m_bypassed ? Color{100, 40, 40, 255} : Color{40, 100, 40, 255};
        Color bpTxt = m_bypassed ? Color{220, 120, 120, 255} : Color{120, 220, 120, 255};
        r.drawRect(m_bypassBtn.x, m_bypassBtn.y, kBypassW, kBtnSize, bpBg);
        {
            const char* bpLabel = m_bypassed ? "Off" : "On";
            float bpLabelW = f.textWidth(bpLabel, tiny);
            f.drawText(r, bpLabel,
                       m_bypassBtn.x + (kBypassW - bpLabelW) * 0.5f,
                       m_bypassBtn.y + 3, tiny, bpTxt);
        }

        // Device name
        Color nameC = m_bypassed ? Theme::textDim : Theme::textPrimary;
        float nameX = x + 22 + kBypassW + 6;
        f.drawText(r, m_name.c_str(), nameX, m_expandBtn.y, med, nameC);

        // Compute preset button position: right after device name
        float nameW = f.textWidth(m_name, med);
        float presetX = nameX + nameW + 8;
        float presetH = kHeaderH - kStripeH - 2;
        float presetY = y + kStripeH + 1;
        const char* pLabel = m_presetName.empty() ? "Preset" : m_presetName.c_str();
        std::string btnLabel = std::string(pLabel) + " \xe2\x96\xbe";
        float labelW = f.textWidth(btnLabel, sml);
        float presetW = labelW + 10;
        float maxRight = m_removable ? (x + w - 24) : (x + w - 4);
        if (presetX + presetW > maxRight)
            presetW = maxRight - presetX;
        if (presetW < 20) presetW = 20;
        m_presetBtn = {presetX, presetY, presetW, presetH};
        // Cache for layout() hit-test
        m_lastNameW = nameW;
        m_lastPresetW = presetW;

        // Remove button
        if (m_removable) {
            r.drawRect(m_removeBtn.x, m_removeBtn.y, kBtnSize, kBtnSize,
                       Color{60, 30, 30, 255});
            f.drawText(r, "X", m_removeBtn.x + 4, m_removeBtn.y + 3, sml,
                       Color{200, 100, 100, 255});
        }

        // Preset dropdown button
        {
            Color pbBg  = {55, 55, 68, 255};
            Color pbBrd = {100, 100, 120, 255};
            r.drawRect(m_presetBtn.x, m_presetBtn.y,
                       m_presetBtn.w, m_presetBtn.h, pbBg);
            r.drawRectOutline(m_presetBtn.x, m_presetBtn.y,
                              m_presetBtn.w, m_presetBtn.h, pbBrd);
            // Label with arrow — use same Y as device name for consistent baseline
            f.drawText(r, btnLabel.c_str(),
                       m_presetBtn.x + 4, m_expandBtn.y, sml,
                       Color{200, 200, 210, 255});
        }
#endif
    }

    // ─── Interaction ────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float px = e.x;
        float py = e.y;

        LOG_INFO("UI", "DeviceHeader::onMouseDown click=(%g,%g) presetBtn=(%g,%g,%g,%g) hasCallback=%d",
                 px, py, m_presetBtn.x, m_presetBtn.y, m_presetBtn.w, m_presetBtn.h,
                 (int)(bool)m_onPresetClick);

        if (hitTest(m_expandBtn, px, py)) {
            m_expanded = !m_expanded;
            if (m_onExpandToggle) m_onExpandToggle(m_expanded);
            return true;
        }
        if (hitTest(m_bypassBtn, px, py)) {
            m_bypassed = !m_bypassed;
            if (m_onBypassToggle) m_onBypassToggle(m_bypassed);
            return true;
        }
        if (m_removable && hitTest(m_removeBtn, px, py)) {
            if (m_onRemove) m_onRemove();
            return true;
        }
        // Preset button — use the rect computed/updated by paint()
        if (m_presetBtn.w > 0 && hitTest(m_presetBtn, px, py)) {
            LOG_INFO("UI", "Preset button HIT!");
            if (m_onPresetClick)
                m_onPresetClick(m_presetBtn.x, m_presetBtn.y + m_presetBtn.h + 2);
            return true;
        }
        // Click on header name area → start drag reorder
        if (m_onDragStart) {
            m_onDragStart();
            return true;
        }
        return false;
    }

private:
    std::string m_name;
    std::string m_presetName;
    DeviceType  m_type = DeviceType::AudioEffect;
    bool        m_bypassed  = false;
    bool        m_expanded  = true;
    bool        m_removable = true;
    float       m_lastNameW   = 0;  // cached from paint() for layout() hit-test
    float       m_lastPresetW = 0;

    ToggleCallback m_onExpandToggle;
    ToggleCallback m_onBypassToggle;
    ActionCallback m_onRemove;
    ActionCallback m_onDragStart;
    PresetClickCallback m_onPresetClick;

    // Hit-test regions (computed in layout)
    Rect m_expandBtn{};
    Rect m_bypassBtn{};
    Rect m_removeBtn{};
    Rect m_presetBtn{};

    static bool hitTest(const Rect& r, float px, float py) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
