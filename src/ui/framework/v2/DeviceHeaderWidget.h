#pragma once
// fw2::DeviceHeaderWidget — header strip for device strips in the
// detail panel's device chain.
//
// Renders the colored-stripe + expand toggle + bypass pill + device name
// + preset drop-down + optional remove button. Owns its own hit-test
// rects so clicks route to the right callback. Fires either the matching
// button callback, or onDragStart if the click landed on the device-name
// area (for chain reordering).
//
// Migrated from v1 fw::DeviceHeaderWidget. Paint uses ctx.textMetrics +
// direct pixel sizes instead of v1's scale-against-Theme::kFontSize.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"
#include "util/Logger.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <functional>
#include <string>
#include <utility>

namespace yawn {
namespace ui {
namespace fw2 {

class DeviceHeaderWidget : public Widget {
public:
    using ActionCallback      = std::function<void()>;
    using ToggleCallback      = std::function<void(bool)>;
    using PresetClickCallback = std::function<void(float x, float y)>;

    enum class DeviceType { Instrument, AudioEffect, MidiEffect, Utility };

    DeviceHeaderWidget() { setName("DeviceHeaderWidget"); }

    // ─── Configuration ──────────────────────────────────────────────────
    void setDeviceName(const std::string& name) { m_name = name; }
    void setDeviceType(DeviceType type)         { m_type = type; }
    void setBypassed(bool b)                    { m_bypassed = b; }
    bool isBypassed() const                     { return m_bypassed; }
    void setExpanded(bool e)                    { m_expanded = e; }
    bool isExpanded() const                     { return m_expanded; }
    void setRemovable(bool r)                   { m_removable = r; }
    void setPresetName(const std::string& n)    { m_presetName = n; }

    // ─── Callbacks ──────────────────────────────────────────────────────
    void setOnExpandToggle(ToggleCallback cb)     { m_onExpandToggle = std::move(cb); }
    void setOnBypassToggle(ToggleCallback cb)     { m_onBypassToggle = std::move(cb); }
    void setOnRemove(ActionCallback cb)            { m_onRemove      = std::move(cb); }
    void setOnDragStart(ActionCallback cb)         { m_onDragStart   = std::move(cb); }
    void setOnPresetClick(PresetClickCallback cb)  { m_onPresetClick = std::move(cb); }

    // ─── Device-type stripe color ──────────────────────────────────────
    Color deviceColor() const {
        switch (m_type) {
            case DeviceType::Instrument:  return {60, 120, 200, 255};
            case DeviceType::AudioEffect: return {180, 80, 200, 255};
            case DeviceType::MidiEffect:  return {200, 160, 60, 255};
            case DeviceType::Utility:     return {100, 180, 100, 255};
        }
        return {180, 80, 200, 255};
    }

    static constexpr float kHeaderH = 24.0f;
    static constexpr float kBtnSize = 16.0f;
    static constexpr float kBypassW = 24.0f;
    static constexpr float kStripeH = 3.0f;

    // fw2 TextMetrics takes actual pixel sizes. The v1 widget used a
    // `pt / Theme::kFontSize(=26)` scale against a 48-px font bake,
    // yielding ~22/18/16 px at pt={12,10,9}. The button rows are only
    // 19 px tall (kHeaderH - kStripeH - 2) so kFsSmall can't exceed ~18
    // without clipping the glyph box vertically.
    static constexpr float kFsMed   = 20.0f;
    static constexpr float kFsSmall = 16.0f;
    static constexpr float kFsTiny  = 14.0f;

    // Hit-test rects (public so tests can inspect; kept in sync by
    // onLayout + render).
    Rect expandBtnRect() const { return m_expandBtn; }
    Rect bypassBtnRect() const { return m_bypassBtn; }
    Rect removeBtnRect() const { return m_removeBtn; }
    Rect presetBtnRect() const { return m_presetBtn; }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, kHeaderH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float x = bounds.x, y = bounds.y, w = bounds.w;
        m_expandBtn = {x + 4,  y + 4, kBtnSize, kBtnSize};
        m_bypassBtn = {x + 22, y + 4, kBypassW, kBtnSize};
        m_removeBtn = m_removable ? Rect{x + w - 20, y + 4, kBtnSize, kBtnSize}
                                   : Rect{};
        recomputePresetRect(bounds, ctx);
    }

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        // Preset rect depends on font metrics — keep it up-to-date in
        // case DPI/font changed after the last onLayout.
        recomputePresetRect(m_bounds, ctx);

        const float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w;

        const Color bg = m_bypassed ? Color{30, 30, 34, 255}
                                     : Color{36, 36, 42, 255};
        r.drawRect(x, y, w, kHeaderH, bg);

        Color stripe = deviceColor();
        if (m_bypassed) { stripe.r /= 2; stripe.g /= 2; stripe.b /= 2; }
        r.drawRect(x, y, w, kStripeH, stripe);

        auto* tm = ctx.textMetrics;

        // Expand/collapse toggle
        const char* tog = m_expanded ? "\xe2\x96\xbc" : "\xe2\x96\xb6";
        if (tm) tm->drawText(r, tog, m_expandBtn.x, m_expandBtn.y,
                              kFsSmall, ::yawn::ui::Theme::textSecondary);

        // Bypass pill
        const Color bpBg  = m_bypassed ? Color{100, 40, 40, 255}
                                        : Color{40, 100, 40, 255};
        const Color bpTxt = m_bypassed ? Color{220, 120, 120, 255}
                                        : Color{120, 220, 120, 255};
        r.drawRect(m_bypassBtn.x, m_bypassBtn.y, kBypassW, kBtnSize, bpBg);
        if (tm) {
            const char* bpLabel = m_bypassed ? "Off" : "On";
            const float bpLabelW = tm->textWidth(bpLabel, kFsTiny);
            tm->drawText(r, bpLabel,
                          m_bypassBtn.x + (kBypassW - bpLabelW) * 0.5f,
                          m_bypassBtn.y + 3, kFsTiny, bpTxt);
        }

        // Device name
        const Color nameC = m_bypassed ? ::yawn::ui::Theme::textDim
                                         : ::yawn::ui::Theme::textPrimary;
        const float nameX = x + 22 + kBypassW + 6;
        if (tm) tm->drawText(r, m_name, nameX, m_expandBtn.y, kFsMed, nameC);

        // Remove button
        if (m_removable) {
            r.drawRect(m_removeBtn.x, m_removeBtn.y, kBtnSize, kBtnSize,
                       Color{60, 30, 30, 255});
            if (tm) tm->drawText(r, "X",
                                  m_removeBtn.x + 4, m_removeBtn.y + 3,
                                  kFsSmall, Color{200, 100, 100, 255});
        }

        // Preset drop-down
        {
            const Color pbBg  = {55, 55, 68, 255};
            const Color pbBrd = {100, 100, 120, 255};
            r.drawRect(m_presetBtn.x, m_presetBtn.y,
                       m_presetBtn.w, m_presetBtn.h, pbBg);
            r.drawRectOutline(m_presetBtn.x, m_presetBtn.y,
                              m_presetBtn.w, m_presetBtn.h, pbBrd);
            if (tm) {
                const std::string label = presetButtonLabel();
                tm->drawText(r, label,
                              m_presetBtn.x + 4, m_expandBtn.y,
                              kFsSmall, Color{200, 200, 210, 255});
            }
        }
    }
#endif

    // ─── Interaction ────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        const float px = e.x, py = e.y;

        LOG_INFO("UI",
                 "DeviceHeader::onMouseDown click=(%g,%g) presetBtn=(%g,%g,%g,%g) hasCallback=%d",
                 px, py, m_presetBtn.x, m_presetBtn.y,
                 m_presetBtn.w, m_presetBtn.h,
                 static_cast<int>(static_cast<bool>(m_onPresetClick)));

        if (hitTestRect(m_expandBtn, px, py)) {
            m_expanded = !m_expanded;
            if (m_onExpandToggle) m_onExpandToggle(m_expanded);
            return true;
        }
        if (hitTestRect(m_bypassBtn, px, py)) {
            m_bypassed = !m_bypassed;
            if (m_onBypassToggle) m_onBypassToggle(m_bypassed);
            return true;
        }
        if (m_removable && hitTestRect(m_removeBtn, px, py)) {
            if (m_onRemove) m_onRemove();
            return true;
        }
        if (m_presetBtn.w > 0 && hitTestRect(m_presetBtn, px, py)) {
            LOG_INFO("UI", "Preset button HIT!");
            if (m_onPresetClick)
                m_onPresetClick(m_presetBtn.x, m_presetBtn.y + m_presetBtn.h + 2);
            return true;
        }
        if (m_onDragStart) {
            m_onDragStart();
            return true;
        }
        return false;
    }

private:
    std::string m_name;
    std::string m_presetName;
    DeviceType  m_type      = DeviceType::AudioEffect;
    bool        m_bypassed  = false;
    bool        m_expanded  = true;
    bool        m_removable = true;

    ToggleCallback       m_onExpandToggle;
    ToggleCallback       m_onBypassToggle;
    ActionCallback       m_onRemove;
    ActionCallback       m_onDragStart;
    PresetClickCallback  m_onPresetClick;

    Rect m_expandBtn{};
    Rect m_bypassBtn{};
    Rect m_removeBtn{};
    Rect m_presetBtn{};

    std::string presetButtonLabel() const {
        const char* pLabel = m_presetName.empty() ? "Preset" : m_presetName.c_str();
        return std::string(pLabel) + " \xe2\x96\xbe";
    }

    void recomputePresetRect(Rect bounds, UIContext& ctx) {
        const float x = bounds.x, y = bounds.y, w = bounds.w;
        const float nameX   = x + 22 + kBypassW + 6;
        float nameW = 0;
        float labelW = 0;
        if (ctx.textMetrics) {
            nameW  = ctx.textMetrics->textWidth(m_name, kFsMed);
            labelW = ctx.textMetrics->textWidth(presetButtonLabel(), kFsSmall);
        } else {
            // Fallback approximation when text metrics aren't wired up
            // (unit tests). 8 px/char matches the other fw2 widgets'
            // test-mode assumption.
            nameW  = static_cast<float>(m_name.size()) * 8.0f;
            labelW = static_cast<float>(presetButtonLabel().size()) * 8.0f;
        }

        const float presetX  = nameX + nameW + 8;
        const float presetH  = kHeaderH - kStripeH - 2;
        const float presetY  = y + kStripeH + 1;
        float       presetW  = labelW + 10;
        const float maxRight = m_removable ? (x + w - 24) : (x + w - 4);
        if (presetX + presetW > maxRight) presetW = maxRight - presetX;
        if (presetW < 20) presetW = 20;
        m_presetBtn = {presetX, presetY, presetW, presetH};
    }

    static bool hitTestRect(const Rect& r, float px, float py) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
