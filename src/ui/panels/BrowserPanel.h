#pragma once
// BrowserPanel — Tabbed panel (top-right) for Files, Presets, Clip, Config.
//
// The "Clip" tab shows follow action controls for the currently selected
// clip slot, giving them dedicated screen real-estate.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/FollowAction.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

namespace yawn {
namespace ui {
namespace fw {

class BrowserPanel : public Widget {
public:
    enum class Tab { Files = 0, Presets, Clip, Config, COUNT };

    BrowserPanel() { initFollowActionWidgets(); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ── Follow action API ──
    void setFollowAction(FollowAction* fa) {
        m_followActionPtr = fa;
        if (fa) {
            m_faEnableBtn.setLabel(fa->enabled ? "On" : "Off");
            m_faBarCountKnob.setValue(static_cast<float>(fa->barCount));
            m_faActionADropdown.setSelected(static_cast<int>(fa->actionA));
            m_faActionBDropdown.setSelected(static_cast<int>(fa->actionB));
            m_faChanceAKnob.setValue(static_cast<float>(fa->chanceA));
        }
    }

    FollowAction* followActionPtr() const { return m_followActionPtr; }

    // ── Events ──
    bool onMouseDown(MouseEvent& e) override {
        float mx = e.x, my = e.y;
        float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w;

        // Tab bar click
        if (my >= y && my < y + kTabH) {
            float tx = x + 2.0f;
            for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i) {
                if (mx >= tx && mx < tx + kTabW) {
                    m_activeTab = static_cast<Tab>(i);
                    return true;
                }
                tx += kTabW;
            }
            return true;
        }

        // Clip tab: follow action widgets
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            if (m_faActionADropdown.isOpen())
                return m_faActionADropdown.onMouseDown(e);
            if (m_faActionBDropdown.isOpen())
                return m_faActionBDropdown.onMouseDown(e);
            if (hitWidget(m_faEnableBtn, mx, my))
                return m_faEnableBtn.onMouseDown(e);
            if (hitWidget(m_faBarCountKnob, mx, my))
                return m_faBarCountKnob.onMouseDown(e);
            if (hitWidget(m_faActionADropdown, mx, my))
                return m_faActionADropdown.onMouseDown(e);
            if (hitWidget(m_faActionBDropdown, mx, my))
                return m_faActionBDropdown.onMouseDown(e);
            if (hitWidget(m_faChanceAKnob, mx, my))
                return m_faChanceAKnob.onMouseDown(e);
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            MouseMoveEvent me = e;
            if (m_faActionADropdown.isOpen()) {
                m_faActionADropdown.onMouseMove(me);
                return true;
            }
            if (m_faActionBDropdown.isOpen()) {
                m_faActionBDropdown.onMouseMove(me);
                return true;
            }
            if (m_faBarCountKnob.onMouseMove(me)) return true;
            if (m_faChanceAKnob.onMouseMove(me)) return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            if (m_faActionADropdown.isOpen())
                return m_faActionADropdown.onMouseUp(e);
            if (m_faActionBDropdown.isOpen())
                return m_faActionBDropdown.onMouseUp(e);
            if (hitWidget(m_faEnableBtn, e.x, e.y))
                return m_faEnableBtn.onMouseUp(e);
            if (m_faBarCountKnob.onMouseUp(e)) return true;
            if (hitWidget(m_faActionADropdown, e.x, e.y))
                return m_faActionADropdown.onMouseUp(e);
            if (hitWidget(m_faActionBDropdown, e.x, e.y))
                return m_faActionBDropdown.onMouseUp(e);
            if (m_faChanceAKnob.onMouseUp(e)) return true;
        }
        return false;
    }

    // Knob text editing support
    bool hasEditingKnob() const {
        return m_faBarCountKnob.isEditing() || m_faChanceAKnob.isEditing();
    }
    bool forwardKeyDown(int key) {
        KeyEvent ke;
        ke.keyCode = key;
        if (m_faBarCountKnob.isEditing()) return m_faBarCountKnob.onKeyDown(ke);
        if (m_faChanceAKnob.isEditing()) return m_faChanceAKnob.onKeyDown(ke);
        return false;
    }
    bool forwardTextInput(const char* text) {
        TextInputEvent te;
        std::strncpy(te.text, text, sizeof(te.text) - 1);
        te.text[sizeof(te.text) - 1] = '\0';
        if (m_faBarCountKnob.isEditing()) return m_faBarCountKnob.onTextInput(te);
        if (m_faChanceAKnob.isEditing()) return m_faChanceAKnob.onTextInput(te);
        return false;
    }
    void cancelEditingKnobs() {
        KeyEvent ke;
        ke.keyCode = 27; // Escape
        if (m_faBarCountKnob.isEditing()) m_faBarCountKnob.onKeyDown(ke);
        if (m_faChanceAKnob.isEditing()) m_faChanceAKnob.onKeyDown(ke);
    }

    void paint(UIContext& ctx) override {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        r.drawRect(x, y, w, h, Color{32, 32, 36});

        // ── Tab bar ──
        r.drawRect(x, y, w, kTabH, Color{38, 38, 42});
        r.drawRect(x, y + kTabH - 1, w, 1, Theme::clipSlotBorder);

        if (f.isLoaded()) {
            float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.85f;
            static const char* tabNames[] = {"Files", "Presets", "Clip", "Config"};
            float tx = x + 2.0f;
            for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i) {
                bool active = (static_cast<Tab>(i) == m_activeTab);
                if (active)
                    r.drawRect(tx, y + 2, kTabW, kTabH - 3, Color{50, 50, 56});
                f.drawText(r, tabNames[i], tx + 8, y + 5, scale,
                           active ? Theme::textPrimary : Theme::textDim);
                tx += kTabW;
            }
        }

        // ── Tab content ──
        float bodyY = y + kTabH;
        float bodyH = h - kTabH;
        r.pushClip(x, bodyY, w, bodyH);

        switch (m_activeTab) {
        case Tab::Clip:
            paintClipTab(r, f, x, bodyY, w, bodyH, ctx);
            break;
        default:
            paintPlaceholder(r, f, x, bodyY, w, bodyH);
            break;
        }

        r.popClip();

        // Dropdown overlays (outside clip region)
        if (m_activeTab == Tab::Clip) {
            if (m_faActionADropdown.isOpen())
                m_faActionADropdown.paintOverlay(ctx);
            if (m_faActionBDropdown.isOpen())
                m_faActionBDropdown.paintOverlay(ctx);
        }
    }

private:
    static constexpr float kTabH = 24.0f;
    static constexpr float kTabW = 65.0f;

    Tab m_activeTab = Tab::Files;

    // Follow action state
    FollowAction* m_followActionPtr = nullptr;
    FwButton   m_faEnableBtn;
    FwKnob     m_faBarCountKnob;
    FwDropDown m_faActionADropdown;
    FwDropDown m_faActionBDropdown;
    FwKnob     m_faChanceAKnob;

    static bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
    }

    void initFollowActionWidgets() {
        m_faEnableBtn.setLabel("Off");
        m_faEnableBtn.setOnClick([this]() {
            if (m_followActionPtr) {
                m_followActionPtr->enabled = !m_followActionPtr->enabled;
                m_faEnableBtn.setLabel(m_followActionPtr->enabled ? "On" : "Off");
            }
        });

        m_faBarCountKnob.setRange(1.0f, 64.0f);
        m_faBarCountKnob.setDefault(1.0f);
        m_faBarCountKnob.setValue(1.0f);
        m_faBarCountKnob.setStep(1.0f);
        m_faBarCountKnob.setSensitivity(0.3f);
        m_faBarCountKnob.setLabel("");
        m_faBarCountKnob.setFormatCallback([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
            return buf;
        });
        m_faBarCountKnob.setOnChange([this](float v) {
            if (m_followActionPtr)
                m_followActionPtr->barCount = static_cast<int>(v);
        });

        static const std::vector<std::string> kNames = {
            "None", "Stop", "Play Again", "Next", "Previous",
            "First", "Last", "Random", "Any"
        };
        m_faActionADropdown.setItems(kNames);
        m_faActionADropdown.setSelected(0);
        m_faActionADropdown.setOnChange([this](int idx) {
            if (m_followActionPtr)
                m_followActionPtr->actionA = static_cast<FollowActionType>(idx);
        });

        m_faActionBDropdown.setItems(kNames);
        m_faActionBDropdown.setSelected(0);
        m_faActionBDropdown.setOnChange([this](int idx) {
            if (m_followActionPtr)
                m_followActionPtr->actionB = static_cast<FollowActionType>(idx);
        });

        m_faChanceAKnob.setRange(0.0f, 100.0f);
        m_faChanceAKnob.setDefault(100.0f);
        m_faChanceAKnob.setValue(100.0f);
        m_faChanceAKnob.setStep(1.0f);
        m_faChanceAKnob.setSensitivity(0.3f);
        m_faChanceAKnob.setLabel("");
        m_faChanceAKnob.setFormatCallback([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(v));
            return buf;
        });
        m_faChanceAKnob.setOnChange([this](float v) {
            if (m_followActionPtr)
                m_followActionPtr->chanceA = static_cast<int>(v);
        });
    }

    void paintClipTab(Renderer2D& r, Font& f, float x, float y,
                      float w, float /*h*/, UIContext& ctx) {
        float pad = 10.0f;
        float sx = x + pad;
        float sw = w - pad * 2;
        float labelScale = 10.0f / Theme::kFontSize;

        // Section header
        r.drawRect(sx, y + 4, sw, 1.0f, Color{50, 50, 55, 255});
        float headerY = y + 8.0f;
        f.drawText(r, "Follow Actions", sx, headerY, labelScale, Theme::textPrimary);

        if (!m_followActionPtr) {
            f.drawText(r, "Select a clip slot", sx, headerY + 20.0f,
                       labelScale, Theme::textDim);
            return;
        }

        // Sync values from data
        m_faEnableBtn.setLabel(m_followActionPtr->enabled ? "On" : "Off");
        if (!m_faBarCountKnob.isEditing())
            m_faBarCountKnob.setValue(static_cast<float>(m_followActionPtr->barCount));
        m_faActionADropdown.setSelected(static_cast<int>(m_followActionPtr->actionA));
        m_faActionBDropdown.setSelected(static_cast<int>(m_followActionPtr->actionB));
        if (!m_faChanceAKnob.isEditing())
            m_faChanceAKnob.setValue(static_cast<float>(m_followActionPtr->chanceA));

        // Layout widgets vertically (panel is narrow)
        float rowY = headerY + 18.0f;
        float labelH = 14.0f;
        float inputH = 20.0f;
        float knobW = 48.0f, knobH = 50.0f;
        float dropW = sw - 60.0f;
        if (dropW < 80.0f) dropW = 80.0f;
        float rowGap = 6.0f;

        // Enable
        f.drawText(r, "Enable", sx, rowY, labelScale, Theme::textDim);
        float inputX = sx + 60.0f;
        m_faEnableBtn.layout(Rect{inputX, rowY - 1, 50.0f, inputH}, ctx);
        m_faEnableBtn.paint(ctx);
        rowY += labelH + rowGap;

        // Bar count
        f.drawText(r, "Bars", sx, rowY + 12.0f, labelScale, Theme::textDim);
        m_faBarCountKnob.layout(Rect{inputX, rowY, knobW, knobH}, ctx);
        m_faBarCountKnob.paint(ctx);
        rowY += knobH + rowGap;

        // Action A
        f.drawText(r, "Action A", sx, rowY + 2.0f, labelScale, Theme::textDim);
        m_faActionADropdown.layout(Rect{inputX, rowY, dropW, inputH}, ctx);
        m_faActionADropdown.paint(ctx);
        rowY += inputH + rowGap;

        // Action B
        f.drawText(r, "Action B", sx, rowY + 2.0f, labelScale, Theme::textDim);
        m_faActionBDropdown.layout(Rect{inputX, rowY, dropW, inputH}, ctx);
        m_faActionBDropdown.paint(ctx);
        rowY += inputH + rowGap;

        // Chance A
        f.drawText(r, "Chance A", sx, rowY + 12.0f, labelScale, Theme::textDim);
        m_faChanceAKnob.layout(Rect{inputX, rowY, knobW, knobH}, ctx);
        m_faChanceAKnob.paint(ctx);
    }

    void paintPlaceholder(Renderer2D& r, Font& f, float x, float y,
                          float w, float h) {
        if (f.isLoaded() && h > 40 && w > 80) {
            float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.7f;
            const char* label = "BROWSER";
            if (m_activeTab == Tab::Presets) label = "PRESETS";
            else if (m_activeTab == Tab::Config) label = "CONFIG";
            float tw = f.textWidth(label, scale);
            f.drawText(r, label, x + (w - tw) * 0.5f, y + h * 0.5f - 8,
                       scale, Theme::textDim);
        }
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
