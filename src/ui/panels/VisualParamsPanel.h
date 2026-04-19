#pragma once

// VisualParamsPanel — knobs for a visual track's shader parameters.
// Two sections:
//   1. Top row: 8 always-available generic knobs A..H — these map to the
//      preamble uniforms `knobA..knobH`. A natural fit for hardware
//      controllers (Push / Move / APC encoders).
//   2. Below: any custom `uniform float NAME;` declarations the shader
//      provides, with their @range annotations.
//
// Values are forwarded to App via setOnChanged / setOnKnobChanged so the
// App can update the live layer + persist to the clip.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "visual/VisualEngine.h"
#include "ui/panels/DetailPanelWidget.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

namespace yawn {
namespace ui {
namespace fw {

class VisualParamsPanel : public Widget {
public:
    using ChangeCallback     = std::function<void(const std::string& name, float value)>;
    using KnobChangeCallback = std::function<void(int index, float value)>;
    using KnobRightClickCallback = std::function<void(int index, float mx, float my)>;
    // (post-fx index, param name, new value)
    using PostFXChangeCallback = std::function<void(int fxIndex,
                                                      const std::string& name,
                                                      float value)>;
    // (post-fx index) — user clicked the × on that effect
    using PostFXRemoveCallback = std::function<void(int fxIndex)>;

    static constexpr float kPanelHeight = 160.0f;
    static constexpr float kHandleH     = 8.0f;   // top drag handle
    static constexpr float kTitleH      = 22.0f;
    static constexpr float kLabelW      = 84.0f;  // post-fx name label width
    static constexpr float kRemoveW     = 16.0f;  // × button width
    static constexpr float kKnobW       = 60.0f;
    static constexpr float kKnobH       = 78.0f;
    static constexpr float kKnobGapX    = 6.0f;
    static constexpr float kKnobGapY    = 6.0f;
    static constexpr float kPad         = 10.0f;
    static constexpr float kRowGap      = 6.0f;

    VisualParamsPanel() {
        for (int i = 0; i < 8; ++i) {
            auto k = std::make_unique<FwKnob>();
            char label[2] = { static_cast<char>('A' + i), 0 };
            k->setLabel(label);
            k->setRange(0.0f, 1.0f);
            k->setDefault(0.5f);
            k->setValue(0.5f);
            int idx = i;
            k->setOnChange([this, idx](float v) {
                if (m_onKnobChanged) m_onKnobChanged(idx, v);
            });
            k->setFormatCallback([](float v) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.2f", v);
                return std::string(b);
            });
            m_knobAH[i] = std::move(k);
        }
    }

    void setOnChanged(ChangeCallback cb)          { m_onChanged     = std::move(cb); }
    void setOnKnobChanged(KnobChangeCallback cb)  { m_onKnobChanged = std::move(cb); }
    void setOnKnobRightClick(KnobRightClickCallback cb) { m_onKnobRightClick = std::move(cb); }
    void setOnPostFXChanged(PostFXChangeCallback cb) { m_onPostFXChanged = std::move(cb); }
    void setOnPostFXRemove(PostFXRemoveCallback cb)  { m_onPostFXRemove  = std::move(cb); }

    // Link to the detail panel so our height mirrors it and the drag
    // handle we render at the top resizes both panels in lockstep.
    void setDetailPanel(DetailPanelWidget* p) { m_detail = p; }

    void setKnobValues(const float* vals8) {
        for (int i = 0; i < 8; ++i) m_knobAH[i]->setValue(vals8[i]);
    }

    // Per-frame LFO arc feedback. Pass either a pointer to 8 live display
    // values to drive the "breathing" arc, or nullptr to clear overrides.
    void setKnobDisplayValues(const float* vals8) {
        if (!vals8) {
            for (int i = 0; i < 8; ++i) m_knobAH[i]->clearDisplayValue();
            return;
        }
        for (int i = 0; i < 8; ++i) m_knobAH[i]->setDisplayValue(vals8[i]);
    }

    void rebuildCustom(const std::vector<visual::VisualEngine::LayerParamInfo>& params,
                       const std::string& shaderName) {
        m_shaderName = shaderName;
        m_customKnobs.clear();
        for (const auto& p : params) {
            auto k = std::make_unique<FwKnob>();
            k->setLabel(p.name);
            k->setRange(p.min, p.max);
            k->setDefault(p.defaultValue);
            k->setValue(p.value);
            std::string nm = p.name;
            k->setOnChange([this, nm](float v) {
                if (m_onChanged) m_onChanged(nm, v);
            });
            k->setFormatCallback([](float v) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.2f", v);
                return std::string(b);
            });
            m_customKnobs.push_back(std::move(k));
        }
    }

    // Per-post-fx entry: shader name + its knobs. Re-built whenever the
    // chain changes or a different visual track is selected.
    struct PostFXEntry {
        std::string name;
        std::vector<std::unique_ptr<FwKnob>> knobs;
    };
    void rebuildPostFX(
        const std::vector<std::pair<std::string,
            std::vector<visual::VisualEngine::LayerParamInfo>>>& chain) {
        m_postFX.clear();
        for (size_t i = 0; i < chain.size(); ++i) {
            PostFXEntry entry;
            entry.name = chain[i].first;
            for (const auto& p : chain[i].second) {
                auto k = std::make_unique<FwKnob>();
                k->setLabel(p.name);
                k->setRange(p.min, p.max);
                k->setDefault(p.defaultValue);
                k->setValue(p.value);
                int fxIdx = static_cast<int>(i);
                std::string nm = p.name;
                k->setOnChange([this, fxIdx, nm](float v) {
                    if (m_onPostFXChanged) m_onPostFXChanged(fxIdx, nm, v);
                });
                k->setFormatCallback([](float v) {
                    char b[16];
                    std::snprintf(b, sizeof(b), "%.2f", v);
                    return std::string(b);
                });
                entry.knobs.push_back(std::move(k));
            }
            m_postFX.push_back(std::move(entry));
        }
    }

    // Widget interface -----------------------------------------------------

    Size measure(const Constraints& c, const UIContext&) override {
        // Use the detail panel's user-configured height, not its animated
        // height — the animated value collapses toward 0 when the detail
        // panel is hidden (which is exactly when *we're* showing).
        float h = m_detail ? m_detail->panelHeight() : kPanelHeight;
        return c.constrain({c.maxW, h});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float x0 = bounds.x + kPad;
        float y0 = bounds.y + kHandleH + kTitleH + 22.0f;
        for (int i = 0; i < 8; ++i) {
            float x = x0 + i * (kKnobW + kKnobGapX);
            m_knobAH[i]->layout(Rect{x, y0, kKnobW, kKnobH}, ctx);
        }
        // Row 2+: custom knobs, flow-wrapped.
        float y = y0 + kKnobH + kRowGap;
        float x = x0;
        for (auto& k : m_customKnobs) {
            if (x + kKnobW > bounds.x + bounds.w - kPad) {
                x = x0;
                y += kKnobH + kKnobGapY;
            }
            k->layout(Rect{x, y, kKnobW, kKnobH}, ctx);
            x += kKnobW + kKnobGapX;
        }

        // Post-FX cards start on a new row below the last custom knob.
        m_postFXSectionY = y + (m_customKnobs.empty() ? 0.0f : kKnobH) + 14.0f;
        m_removeRects.assign(m_postFX.size(), Rect{0, 0, 0, 0});

        float py = m_postFXSectionY + 18.0f;  // leave space for header
        float px = x0;
        for (size_t i = 0; i < m_postFX.size(); ++i) {
            auto& entry = m_postFX[i];
            float cardW = kLabelW + kRemoveW + kKnobGapX
                         + static_cast<float>(entry.knobs.size())
                         * (kKnobW + kKnobGapX);
            if (px + cardW > bounds.x + bounds.w - kPad && px > x0) {
                px = x0;
                py += kKnobH + kKnobGapY;
            }
            // × remove button
            m_removeRects[i] = Rect{px, py, kRemoveW, kKnobH * 0.35f};
            // Knobs start after label + × + small gap
            float kx = px + kLabelW + kRemoveW + kKnobGapX;
            for (auto& k : entry.knobs) {
                k->layout(Rect{kx, py, kKnobW, kKnobH}, ctx);
                kx += kKnobW + kKnobGapX;
            }
            px += cardW + 18.0f;
        }
    }

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
    bool onMouseDown(MouseEvent&) override { return false; }
#else
    void paint(UIContext& ctx) override {
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, Theme::panelBg);
        // Drag handle strip at top.
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, kHandleH,
                   Color{35, 35, 40, 255});
        float hcx = m_bounds.x + m_bounds.w * 0.5f;
        float hy  = m_bounds.y + kHandleH * 0.5f - 1.0f;
        for (int i = -1; i <= 1; ++i)
            r.drawRect(hcx + i * 10.0f - 4.0f, hy, 8.0f, 2.0f,
                       Color{110, 110, 120, 200});

        float scale = Theme::kSmallFontSize / f.pixelHeight();
        std::string title = "Visual Params";
        if (!m_shaderName.empty()) title += "  —  " + m_shaderName;
        f.drawText(r, title.c_str(),
                   m_bounds.x + kPad,
                   m_bounds.y + kHandleH + kTitleH * 0.5f
                     + f.pixelHeight() * scale * 0.5f - 2.0f,
                   scale, Theme::textDim);

        for (int i = 0; i < 8; ++i) m_knobAH[i]->paint(ctx);
        for (auto& k : m_customKnobs) k->paint(ctx);

        // Post-FX section header + per-effect group.
        if (!m_postFX.empty()) {
            // Section separator line + label.
            r.drawRect(m_bounds.x + kPad, m_postFXSectionY,
                       m_bounds.w - 2.0f * kPad, 1.0f,
                       Color{60, 60, 68, 255});
            f.drawText(r, "Post FX",
                       m_bounds.x + kPad, m_postFXSectionY + 14.0f,
                       scale, Theme::textDim);

            for (size_t i = 0; i < m_postFX.size(); ++i) {
                auto& entry = m_postFX[i];
                // Label: "N. bloom"
                std::string label = std::to_string(i + 1) + ". " + entry.name;
                // Draw near the × button's Y; label sits just above.
                const Rect& xr = m_removeRects[i];
                f.drawText(r, label.c_str(),
                           xr.x, xr.y + 11.0f,
                           scale, Theme::textPrimary);
                // Remove button — small red "×" pill.
                r.drawRect(xr.x + kLabelW, xr.y,
                           xr.w, xr.h,
                           Color{160, 50, 50, 220});
                f.drawText(r, "×",
                           xr.x + kLabelW + xr.w * 0.5f - 4.0f,
                           xr.y + xr.h * 0.5f + 5.0f,
                           scale * 1.1f, Theme::textPrimary);

                for (auto& k : entry.knobs) k->paint(ctx);
            }
        }
    }

    bool onMouseDown(MouseEvent& e) override {
        const bool rightClick = (e.button == MouseButton::Right);
        // Top handle → start resize drag (left only).
        if (!rightClick && m_detail &&
            e.y >= m_bounds.y && e.y < m_bounds.y + kHandleH) {
            m_handleDrag      = true;
            m_handleStartY    = e.y;
            m_handleStartH    = m_detail->panelHeight();
            captureMouse();
            return true;
        }
        for (int i = 0; i < 8; ++i) {
            if (!hitWidget(*m_knobAH[i], e.x, e.y)) continue;
            if (rightClick) {
                if (m_onKnobRightClick) m_onKnobRightClick(i, e.x, e.y);
                return true;
            }
            return m_knobAH[i]->onMouseDown(e);
        }
        for (auto& k : m_customKnobs)
            if (hitWidget(*k, e.x, e.y)) return k->onMouseDown(e);

        // Post-FX knobs + remove buttons.
        for (size_t i = 0; i < m_postFX.size(); ++i) {
            // × remove button hit.
            const Rect& xr = m_removeRects[i];
            if (e.x >= xr.x + kLabelW && e.x < xr.x + kLabelW + xr.w &&
                e.y >= xr.y && e.y < xr.y + xr.h) {
                if (!rightClick && m_onPostFXRemove)
                    m_onPostFXRemove(static_cast<int>(i));
                return true;
            }
            for (auto& k : m_postFX[i].knobs)
                if (hitWidget(*k, e.x, e.y)) return k->onMouseDown(e);
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_handleDrag && m_detail) {
            float delta = m_handleStartY - e.y;   // drag up = taller
            m_detail->setPanelHeight(m_handleStartH + delta);
            return true;
        }
        if (auto* cap = Widget::capturedWidget()) return cap->onMouseMove(e);
        for (int i = 0; i < 8; ++i)
            if (hitWidget(*m_knobAH[i], e.x, e.y)) m_knobAH[i]->onMouseMove(e);
        for (auto& k : m_customKnobs)
            if (hitWidget(*k, e.x, e.y)) k->onMouseMove(e);
        for (auto& entry : m_postFX)
            for (auto& k : entry.knobs)
                if (hitWidget(*k, e.x, e.y)) k->onMouseMove(e);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_handleDrag) {
            m_handleDrag = false;
            releaseMouse();
            return true;
        }
        if (auto* cap = Widget::capturedWidget()) return cap->onMouseUp(e);
        return false;
    }
#endif

private:
    bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    Rect m_bounds{};
    std::unique_ptr<FwKnob> m_knobAH[8];
    std::vector<std::unique_ptr<FwKnob>> m_customKnobs;
    std::string m_shaderName;
    ChangeCallback         m_onChanged;
    KnobChangeCallback     m_onKnobChanged;
    KnobRightClickCallback m_onKnobRightClick;

    // Post-FX section
    std::vector<PostFXEntry> m_postFX;
    std::vector<Rect>        m_removeRects;
    float                    m_postFXSectionY = 0.0f;
    PostFXChangeCallback     m_onPostFXChanged;
    PostFXRemoveCallback     m_onPostFXRemove;

    DetailPanelWidget* m_detail = nullptr;
    bool  m_handleDrag   = false;
    float m_handleStartY = 0.0f;
    float m_handleStartH = 0.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
