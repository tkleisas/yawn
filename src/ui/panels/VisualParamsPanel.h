#pragma once

// VisualParamsPanel — knobs for a visual track's shader parameters
// (UI v2 version).
//
// Two sections:
//   1. Top row: 8 always-available generic knobs A..H — these map to the
//      preamble uniforms `knobA..knobH`. A natural fit for hardware
//      controllers (Push / Move / APC encoders).
//   2. Below: any custom `uniform float NAME;` declarations the shader
//      provides, with their @range annotations.
//
// Values are forwarded to App via setOnChanged / setOnKnobChanged so
// the App can update the live layer + persist to the clip.
//
// Migrated from v1 fw::Widget to fw2::Widget. All child knobs were
// already fw2, so the internal v1↔fw2 bridging (m_v2Dragging,
// captureMouse, toFw2Mouse) is gone. Integration into the v1
// rootLayout goes through `fw::VisualParamsPanelWrapper` in
// PanelWrappers.h.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/UIContext.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#include "ui/Font.h"
#endif
#include "ui/Theme.h"
#include "ui/framework/v2/Theme.h"
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
namespace fw2 {

class VisualParamsPanel : public Widget {
public:
    using ChangeCallback     = std::function<void(const std::string& name, float value)>;
    using KnobChangeCallback = std::function<void(int index, float value)>;
    using KnobRightClickCallback = std::function<void(int index, float mx, float my)>;
    using PostFXChangeCallback = std::function<void(int fxIndex,
                                                      const std::string& name,
                                                      float value)>;
    using PostFXRemoveCallback = std::function<void(int fxIndex)>;

    static constexpr float kPanelHeight = 160.0f;
    static constexpr float kHandleH     = 8.0f;
    static constexpr float kTitleH      = 22.0f;
    static constexpr float kLabelW      = 84.0f;
    static constexpr float kRemoveW     = 16.0f;
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
            k->setDefaultValue(0.5f);
            k->setValue(0.5f);
            int idx = i;
            k->setOnChange([this, idx](float v) {
                if (m_onKnobChanged) m_onKnobChanged(idx, v);
            });
            k->setOnRightClick([this, idx](Point screen) {
                if (m_onKnobRightClick) m_onKnobRightClick(idx, screen.x, screen.y);
            });
            k->setValueFormatter([](float v) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.2f", v);
                return std::string(b);
            });
            m_knobAH[i] = std::move(k);
        }
        setFocusable(false);
        setRelayoutBoundary(true);
    }

    void setOnChanged(ChangeCallback cb)          { m_onChanged     = std::move(cb); }
    void setOnKnobChanged(KnobChangeCallback cb)  { m_onKnobChanged = std::move(cb); }
    void setOnKnobRightClick(KnobRightClickCallback cb) { m_onKnobRightClick = std::move(cb); }
    void setOnPostFXChanged(PostFXChangeCallback cb) { m_onPostFXChanged = std::move(cb); }
    void setOnPostFXRemove(PostFXRemoveCallback cb)  { m_onPostFXRemove  = std::move(cb); }

    // Link to the v1 DetailPanelWidget so our height mirrors it and
    // the top drag handle resizes both panels in lockstep.
    void setDetailPanel(::yawn::ui::fw::DetailPanelWidget* p) { m_detail = p; }

    void setKnobValues(const float* vals8) {
        for (int i = 0; i < 8; ++i) m_knobAH[i]->setValue(vals8[i]);
    }

    void setKnobDisplayValues(const float* vals8) {
        if (!vals8) {
            for (int i = 0; i < 8; ++i) m_knobAH[i]->setModulatedValue(std::nullopt);
            return;
        }
        for (int i = 0; i < 8; ++i) m_knobAH[i]->setModulatedValue(vals8[i]);
    }

    void rebuildCustom(const std::vector<visual::VisualEngine::LayerParamInfo>& params,
                       const std::string& shaderName) {
        m_shaderName = shaderName;

        bool matches = (m_customSig.size() == params.size()) &&
                        (m_customKnobs.size() == params.size());
        if (matches) {
            for (size_t i = 0; i < params.size(); ++i) {
                const auto& p  = params[i];
                const auto& sg = m_customSig[i];
                if (sg.name    != p.name ||
                    sg.min     != p.min  ||
                    sg.max     != p.max  ||
                    sg.defVal  != p.defaultValue) {
                    matches = false;
                    break;
                }
            }
        }

        if (matches) {
            for (size_t i = 0; i < params.size(); ++i) {
                m_customKnobs[i]->setValue(params[i].value,
                                            ValueChangeSource::Automation);
            }
            return;
        }

        m_customKnobs.clear();
        m_customSig.clear();
        m_customSig.reserve(params.size());
        for (const auto& p : params) {
            auto k = std::make_unique<FwKnob>();
            k->setLabel(p.name);
            k->setRange(p.min, p.max);
            k->setDefaultValue(p.defaultValue);
            k->setValue(p.value);
            std::string nm = p.name;
            k->setOnChange([this, nm](float v) {
                if (m_onChanged) m_onChanged(nm, v);
            });
            k->setValueFormatter([](float v) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.2f", v);
                return std::string(b);
            });
            m_customKnobs.push_back(std::move(k));
            m_customSig.push_back({p.name, p.min, p.max, p.defaultValue});
        }
    }

    struct PostFXEntry {
        std::string name;
        std::vector<std::unique_ptr<FwKnob>> knobs;
    };
    void rebuildPostFX(
        const std::vector<std::pair<std::string,
            std::vector<visual::VisualEngine::LayerParamInfo>>>& chain) {
        bool matches = (m_postFXSig.size() == chain.size()) &&
                        (m_postFX.size()    == chain.size());
        if (matches) {
            for (size_t i = 0; i < chain.size(); ++i) {
                const auto& sig  = m_postFXSig[i];
                const auto& incoming = chain[i];
                if (sig.name != incoming.first ||
                    sig.params.size() != incoming.second.size()) {
                    matches = false;
                    break;
                }
                for (size_t j = 0; j < incoming.second.size(); ++j) {
                    const auto& sp = sig.params[j];
                    const auto& p  = incoming.second[j];
                    if (sp.name != p.name || sp.min != p.min ||
                        sp.max  != p.max  || sp.defVal != p.defaultValue) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) break;
            }
        }

        if (matches) {
            for (size_t i = 0; i < chain.size(); ++i) {
                const auto& incoming = chain[i].second;
                auto& entry = m_postFX[i];
                for (size_t j = 0; j < incoming.size(); ++j) {
                    entry.knobs[j]->setValue(incoming[j].value,
                                              ValueChangeSource::Automation);
                }
            }
            return;
        }

        m_postFX.clear();
        m_postFXSig.clear();
        m_postFXSig.reserve(chain.size());
        for (size_t i = 0; i < chain.size(); ++i) {
            PostFXEntry entry;
            entry.name = chain[i].first;
            PostFXSig sig;
            sig.name = chain[i].first;
            for (const auto& p : chain[i].second) {
                auto k = std::make_unique<FwKnob>();
                k->setLabel(p.name);
                k->setRange(p.min, p.max);
                k->setDefaultValue(p.defaultValue);
                k->setValue(p.value);
                int fxIdx = static_cast<int>(i);
                std::string nm = p.name;
                k->setOnChange([this, fxIdx, nm](float v) {
                    if (m_onPostFXChanged) m_onPostFXChanged(fxIdx, nm, v);
                });
                k->setValueFormatter([](float v) {
                    char b[16];
                    std::snprintf(b, sizeof(b), "%.2f", v);
                    return std::string(b);
                });
                entry.knobs.push_back(std::move(k));
                sig.params.push_back({p.name, p.min, p.max, p.defaultValue});
            }
            m_postFX.push_back(std::move(entry));
            m_postFXSig.push_back(std::move(sig));
        }
    }

protected:
    // ─── fw2 Widget overrides ───────────────────────────────────────

    Size onMeasure(Constraints c, UIContext& ctx) override {
        float configuredH = m_detail ? m_detail->panelHeight() : kPanelHeight;
        float minH = computeRequiredHeight(c.maxW, ctx);
        float h    = std::max(configuredH, minH);
        return c.constrain({c.maxW, h});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        float x0 = bounds.x + kPad;
        float y0 = bounds.y + kHandleH + kTitleH + 22.0f;
        for (int i = 0; i < 8; ++i) {
            float x = x0 + i * (kKnobW + kKnobGapX);
            m_knobAH[i]->measure(Constraints::tight(kKnobW, kKnobH), ctx);
            m_knobAH[i]->layout(Rect{x, y0, kKnobW, kKnobH}, ctx);
        }
        float y = y0 + kKnobH + kRowGap;
        float x = x0;
        for (auto& k : m_customKnobs) {
            float colW = columnWidthFor(*k, ctx);
            if (x + colW > bounds.x + bounds.w - kPad && x > x0) {
                x = x0;
                y += kKnobH + kKnobGapY;
            }
            k->measure(Constraints::tight(colW, kKnobH), ctx);
            k->layout(Rect{x, y, colW, kKnobH}, ctx);
            x += colW + kKnobGapX;
        }

        m_postFXSectionY = y + (m_customKnobs.empty() ? 0.0f : kKnobH) + 14.0f;
        m_removeRects.assign(m_postFX.size(), Rect{0, 0, 0, 0});

        float py = m_postFXSectionY + 18.0f;
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
            m_removeRects[i] = Rect{px, py, kRemoveW, kKnobH * 0.35f};
            float kx = px + kLabelW + kRemoveW + kKnobGapX;
            for (auto& k : entry.knobs) {
                k->measure(Constraints::tight(kKnobW, kKnobH), ctx);
                k->layout(Rect{kx, py, kKnobW, kKnobH}, ctx);
                kx += kKnobW + kKnobGapX;
            }
            px += cardW + 18.0f;
        }
    }

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&)     override { return false; }
    bool onMouseMove(MouseMoveEvent&) override { return false; }
    bool onMouseUp(MouseEvent&)       override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override {
        const bool rightClick = (e.button == MouseButton::Right);
        // Top handle → start resize drag.
        if (!rightClick && m_detail &&
            e.y >= m_bounds.y && e.y < m_bounds.y + kHandleH) {
            m_handleDrag   = true;
            m_handleStartY = e.y;
            m_handleStartH = m_detail->panelHeight();
            captureMouse();
            return true;
        }
        auto forwardTo = [&](FwKnob& k) {
            const auto& b = k.bounds();
            MouseEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            k.dispatchMouseDown(ev);
        };
        for (int i = 0; i < 8; ++i) {
            if (!hitWidgetV2(*m_knobAH[i], e.x, e.y)) continue;
            if (rightClick) {
                if (m_onKnobRightClick) m_onKnobRightClick(i, e.x, e.y);
                return true;
            }
            forwardTo(*m_knobAH[i]);
            return true;
        }
        for (auto& k : m_customKnobs) {
            if (!hitWidgetV2(*k, e.x, e.y)) continue;
            forwardTo(*k);
            return true;
        }
        for (size_t i = 0; i < m_postFX.size(); ++i) {
            const Rect& xr = m_removeRects[i];
            if (e.x >= xr.x + kLabelW && e.x < xr.x + kLabelW + xr.w &&
                e.y >= xr.y && e.y < xr.y + xr.h) {
                if (!rightClick && m_onPostFXRemove)
                    m_onPostFXRemove(static_cast<int>(i));
                return true;
            }
            for (auto& k : m_postFX[i].knobs) {
                if (!hitWidgetV2(*k, e.x, e.y)) continue;
                forwardTo(*k);
                return true;
            }
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_handleDrag && m_detail) {
            float delta = m_handleStartY - e.y;
            m_detail->setPanelHeight(m_handleStartH + delta);
            return true;
        }
        if (Widget* cap = Widget::capturedWidget()) {
            const auto& b = cap->bounds();
            MouseMoveEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            cap->dispatchMouseMove(ev);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_handleDrag) {
            m_handleDrag = false;
            releaseMouse();
            return true;
        }
        if (Widget* cap = Widget::capturedWidget()) {
            const auto& b = cap->bounds();
            MouseEvent ev = e;
            ev.lx = e.x - b.x;
            ev.ly = e.y - b.y;
            cap->dispatchMouseUp(ev);
            return true;
        }
        return false;
    }
#endif

public:
    void render(UIContext& ctx) override {
        if (!m_visible) return;
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.textMetrics) return;
        auto& r  = *ctx.renderer;
        auto& tm = *ctx.textMetrics;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                    ::yawn::ui::Theme::panelBg);
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, kHandleH,
                    Color{35, 35, 40, 255});
        float hcx = m_bounds.x + m_bounds.w * 0.5f;
        float hy  = m_bounds.y + kHandleH * 0.5f - 1.0f;
        for (int i = -1; i <= 1; ++i)
            r.drawRect(hcx + i * 10.0f - 4.0f, hy, 8.0f, 2.0f,
                        Color{110, 110, 120, 200});

        const float titleSize = theme().metrics.fontSizeSmall;
        std::string title = "Visual Params";
        if (!m_shaderName.empty()) title += "  —  " + m_shaderName;
        const float titleLh = tm.lineHeight(titleSize);
        tm.drawText(r, title,
                    m_bounds.x + kPad,
                    m_bounds.y + kHandleH + (kTitleH - titleLh) * 0.5f,
                    titleSize, ::yawn::ui::Theme::textSecondary);

        for (int i = 0; i < 8; ++i) m_knobAH[i]->render(ctx);
        for (auto& k : m_customKnobs) k->render(ctx);

        if (!m_postFX.empty()) {
            r.drawRect(m_bounds.x + kPad, m_postFXSectionY,
                        m_bounds.w - 2.0f * kPad, 1.0f,
                        Color{60, 60, 68, 255});
            tm.drawText(r, "Post FX",
                        m_bounds.x + kPad, m_postFXSectionY + 14.0f,
                        titleSize, ::yawn::ui::Theme::textSecondary);

            for (size_t i = 0; i < m_postFX.size(); ++i) {
                auto& entry = m_postFX[i];
                std::string label = std::to_string(i + 1) + ". " + entry.name;
                const Rect& xr = m_removeRects[i];
                tm.drawText(r, label,
                            xr.x, xr.y + 11.0f,
                            titleSize, ::yawn::ui::Theme::textPrimary);
                r.drawRect(xr.x + kLabelW, xr.y,
                            xr.w, xr.h,
                            Color{160, 50, 50, 220});
                tm.drawText(r, "×",
                            xr.x + kLabelW + xr.w * 0.5f - 4.0f,
                            xr.y + xr.h * 0.5f + 5.0f,
                            titleSize * 1.1f, ::yawn::ui::Theme::textPrimary);

                for (auto& k : entry.knobs) k->render(ctx);
            }
        }
#else
        (void)ctx;
#endif
    }

private:
    bool hitWidgetV2(Widget& w, float mx, float my) {
        const auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    float columnWidthFor(const FwKnob& k, const UIContext& ctx) const {
        if (!ctx.textMetrics) return kKnobW;
        const float labelSize = 9.0f;
        const float lw = ctx.textMetrics->textWidth(k.label(), labelSize);
        return std::max(kKnobW, lw + 6.0f);
    }

    float computeRequiredHeight(float panelW, const UIContext& ctx) const {
        float topY   = kHandleH + kTitleH + 22.0f;
        float rowH   = kKnobH + kKnobGapY;
        float y      = topY + kKnobH + kRowGap;
        float xLimit = panelW - kPad;
        float x      = kPad;
        float lastY  = y;
        for (auto& k : m_customKnobs) {
            float colW = columnWidthFor(*k, ctx);
            if (x + colW > xLimit && x > kPad) {
                x = kPad;
                y += rowH;
            }
            x += colW + kKnobGapX;
            lastY = y;
        }
        float customBottom = lastY + (m_customKnobs.empty() ? 0.0f : kKnobH);
        float fxBottom = customBottom + 14.0f;
        if (!m_postFX.empty()) {
            float py = customBottom + 18.0f;
            float px = kPad;
            for (auto& entry : m_postFX) {
                float cardW = kLabelW + kRemoveW + kKnobGapX +
                               static_cast<float>(entry.knobs.size()) *
                               (kKnobW + kKnobGapX);
                if (px + cardW > panelW - kPad && px > kPad) {
                    px = kPad;
                    py += rowH;
                }
                px += cardW + 18.0f;
            }
            fxBottom = py + kKnobH;
        }
        return fxBottom + kPad;
    }

    std::unique_ptr<FwKnob> m_knobAH[8];
    std::vector<std::unique_ptr<FwKnob>> m_customKnobs;
    struct CustomSig { std::string name; float min, max, defVal; };
    std::vector<CustomSig> m_customSig;
    std::string m_shaderName;
    ChangeCallback         m_onChanged;
    KnobChangeCallback     m_onKnobChanged;
    KnobRightClickCallback m_onKnobRightClick;

    std::vector<PostFXEntry> m_postFX;
    struct PostFXSig {
        std::string name;
        std::vector<CustomSig> params;
    };
    std::vector<PostFXSig>   m_postFXSig;
    std::vector<Rect>        m_removeRects;
    float                    m_postFXSectionY = 0.0f;
    PostFXChangeCallback     m_onPostFXChanged;
    PostFXRemoveCallback     m_onPostFXRemove;

    ::yawn::ui::fw::DetailPanelWidget* m_detail = nullptr;
    bool  m_handleDrag   = false;
    float m_handleStartY = 0.0f;
    float m_handleStartH = 0.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
