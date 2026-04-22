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
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/V1EventBridge.h"
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
            auto k = std::make_unique<::yawn::ui::fw2::FwKnob>();
            char label[2] = { static_cast<char>('A' + i), 0 };
            k->setLabel(label);
            k->setRange(0.0f, 1.0f);
            k->setDefaultValue(0.5f);
            k->setValue(0.5f);
            int idx = i;
            k->setOnChange([this, idx](float v) {
                if (m_onKnobChanged) m_onKnobChanged(idx, v);
            });
            k->setOnRightClick([this, idx](::yawn::ui::fw2::Point screen) {
                if (m_onKnobRightClick) m_onKnobRightClick(idx, screen.x, screen.y);
            });
            k->setValueFormatter([](float v) {
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
    // v2 renders this as a modulation overlay (secondary indicator +
    // "excursion" arc from primary to modulated position).
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

        // This is called every frame from the main update loop. If the
        // params signature hasn't changed, we MUST reuse the existing
        // FwKnob instances — destroying one mid-drag invalidates the
        // captured-widget pointer and silently breaks the drag. We
        // cache the signature alongside the knobs; any change (shader
        // swap, @range edit) triggers a full rebuild.
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
            // Same shader / same uniforms — just resync values from
            // the engine without touching knob identity.
            for (size_t i = 0; i < params.size(); ++i) {
                m_customKnobs[i]->setValue(params[i].value);
            }
            return;
        }

        m_customKnobs.clear();
        m_customSig.clear();
        m_customSig.reserve(params.size());
        for (const auto& p : params) {
            auto k = std::make_unique<::yawn::ui::fw2::FwKnob>();
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

    // Per-post-fx entry: shader name + its knobs. Re-built whenever the
    // chain changes or a different visual track is selected.
    struct PostFXEntry {
        std::string name;
        std::vector<std::unique_ptr<::yawn::ui::fw2::FwKnob>> knobs;
    };
    void rebuildPostFX(
        const std::vector<std::pair<std::string,
            std::vector<visual::VisualEngine::LayerParamInfo>>>& chain) {
        // Same idempotence rule as rebuildCustom: don't destroy the
        // FwKnob instances while the user may be mid-drag. Full rebuild
        // only when the chain structurally changes.
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
                    entry.knobs[j]->setValue(incoming[j].value);
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
                auto k = std::make_unique<::yawn::ui::fw2::FwKnob>();
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

    // Widget interface -----------------------------------------------------

    Size measure(const Constraints& c, const UIContext& ctx) override {
        // Configured height comes from the shared drag handle. We may
        // need more to actually fit every knob row — clicks below the
        // measured bounds never reach the panel, so rows outside the
        // box are invisible to event routing (not just truncated).
        float configuredH = m_detail ? m_detail->panelHeight() : kPanelHeight;
        float minH = computeRequiredHeight(c.maxW, ctx);
        float h    = std::max(configuredH, minH);
        return c.constrain({c.maxW, h});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        // v2 knobs layout against the fw2 UIContext.
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        float x0 = bounds.x + kPad;
        float y0 = bounds.y + kHandleH + kTitleH + 22.0f;
        for (int i = 0; i < 8; ++i) {
            float x = x0 + i * (kKnobW + kKnobGapX);
            m_knobAH[i]->layout(Rect{x, y0, kKnobW, kKnobH}, v2ctx);
        }
        // Row 2+: custom knobs, flow-wrapped. Per-knob column width
        // grows to fit the label so long names (e.g. "bandCenterY")
        // don't collide with neighbours.
        float y = y0 + kKnobH + kRowGap;
        float x = x0;
        for (auto& k : m_customKnobs) {
            float colW = columnWidthFor(*k, ctx);
            if (x + colW > bounds.x + bounds.w - kPad && x > x0) {
                x = x0;
                y += kKnobH + kKnobGapY;
            }
            k->layout(Rect{x, y, colW, kKnobH}, v2ctx);
            x += colW + kKnobGapX;
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
                k->layout(Rect{kx, py, kKnobW, kKnobH}, v2ctx);
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

        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        for (int i = 0; i < 8; ++i) m_knobAH[i]->render(v2ctx);
        for (auto& k : m_customKnobs) k->render(v2ctx);

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

                for (auto& k : entry.knobs) k->render(v2ctx);
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
        // v2 knob hit-test + dispatch via the v1→v2 bridge. Right-click
        // on A..H still fires the app-side callback (MIDI Learn etc.);
        // the v2 knob's right-click handler is a no-op for those since
        // we don't wire setOnRightClick on m_knobAH. For custom / post-fx
        // knobs we let the v2 gesture SM handle right-click normally.
        auto beginV2Drag = [&](::yawn::ui::fw2::FwKnob& k) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, k.bounds());
            k.dispatchMouseDown(ev);
            m_v2Dragging = &k;
            captureMouse();
        };
        for (int i = 0; i < 8; ++i) {
            if (!hitWidgetV2(*m_knobAH[i], e.x, e.y)) continue;
            if (rightClick) {
                if (m_onKnobRightClick) m_onKnobRightClick(i, e.x, e.y);
                return true;
            }
            beginV2Drag(*m_knobAH[i]);
            return true;
        }
        for (auto& k : m_customKnobs) {
            if (!hitWidgetV2(*k, e.x, e.y)) continue;
            beginV2Drag(*k);
            return true;
        }

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
            for (auto& k : m_postFX[i].knobs) {
                if (!hitWidgetV2(*k, e.x, e.y)) continue;
                beginV2Drag(*k);
                return true;
            }
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_handleDrag && m_detail) {
            float delta = m_handleStartY - e.y;   // drag up = taller
            m_detail->setPanelHeight(m_handleStartH + delta);
            return true;
        }
        // v2 knob drag in progress.
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseMove(ev);
            return true;
        }
        if (auto* cap = Widget::capturedWidget()) return cap->onMouseMove(e);
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_handleDrag) {
            m_handleDrag = false;
            releaseMouse();
            return true;
        }
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseUp(ev);
            m_v2Dragging = nullptr;
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
    // v2 widgets use fw2::Rect with the same field layout.
    bool hitWidgetV2(::yawn::ui::fw2::Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
    }

    // Column width for a single custom knob: wide enough to fit its
    // label at the same scale FwKnob uses (9px), plus a small padding
    // so adjacent labels don't kiss. Floored at kKnobW so the knob
    // graphic never shrinks.
    float columnWidthFor(const ::yawn::ui::fw2::FwKnob& k,
                         const UIContext& ctx) const {
#ifndef YAWN_TEST_BUILD
        if (!ctx.font) return kKnobW;
        float labelScale = 9.0f / Theme::kFontSize;
        float lw = ctx.font->textWidth(k.label(), labelScale);
        return std::max(kKnobW, lw + 6.0f);
#else
        (void)k; (void)ctx;
        return kKnobW;
#endif
    }

    // Minimum height that'll fit the full content: generic knob row,
    // wrapped custom knob rows, and (if present) the post-fx cards. UI
    // event routing uses the measured bounds — if we undersell, any
    // widget below the box is effectively unclickable.
    float computeRequiredHeight(float panelW, const UIContext& ctx) const {
        float topY   = kHandleH + kTitleH + 22.0f;
        float rowH   = kKnobH + kKnobGapY;
        // Generic knobs are one fixed row.
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
        // Post-FX block (if any): one row per card wrap, rough but
        // close enough given we just need "big enough" not "exact".
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

    Rect m_bounds{};
    std::unique_ptr<::yawn::ui::fw2::FwKnob> m_knobAH[8];
    std::vector<std::unique_ptr<::yawn::ui::fw2::FwKnob>> m_customKnobs;
    // Active v2 drag (one of m_knobAH, m_customKnobs, or m_postFX's
    // knobs) — set on mouseDown, cleared on mouseUp. Non-owning.
    ::yawn::ui::fw2::Widget* m_v2Dragging = nullptr;
    // Parallel array tracking the identity (name+range+default) each
    // custom knob was built for, so the per-frame rebuild call can
    // reuse existing instances when nothing structural changed.
    struct CustomSig { std::string name; float min, max, defVal; };
    std::vector<CustomSig> m_customSig;
    std::string m_shaderName;
    ChangeCallback         m_onChanged;
    KnobChangeCallback     m_onKnobChanged;
    KnobRightClickCallback m_onKnobRightClick;

    // Post-FX section
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

    DetailPanelWidget* m_detail = nullptr;
    bool  m_handleDrag   = false;
    float m_handleStartY = 0.0f;
    float m_handleStartH = 0.0f;
};

} // namespace fw
} // namespace ui
} // namespace yawn
