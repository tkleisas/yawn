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
    using ChainPassChangeCallback = std::function<void(int passIndex,
                                                        const std::string& name,
                                                        float value)>;
    using ChainPassRemoveCallback = std::function<void(int passIndex)>;
    using ChainPassBypassCallback = std::function<void(int passIndex, bool bypassed)>;
    // Called when the user drag-reorders a chain card. `from` is the
    // original index, `to` is the slot it should end up at. `to` is
    // always in [0, chain.size()] — caller should remove `from` then
    // insert at `to`, adjusting if `to > from`.
    using ChainPassReorderCallback = std::function<void(int from, int to)>;
    using ChainAddCallback        = std::function<void(float screenX, float screenY)>;

    // Snapshot entry for a single chain pass — fed in by App.cpp on
    // each updateDetailForSelectedTrack tick. The struct keeps the
    // bypass flag adjacent to name + params so the panel doesn't need
    // a parallel sidecar vector for it.
    struct ChainPassSnap {
        std::string                                   name;
        bool                                          bypassed = false;
        std::vector<visual::VisualEngine::LayerParamInfo> params;
    };

    static constexpr float kPanelHeight = 160.0f;
    static constexpr float kHandleH     = 8.0f;
    static constexpr float kTitleH      = 22.0f;
    static constexpr float kLabelW      = 84.0f;   // legacy — narrow remaining uses
    static constexpr float kRemoveW     = 16.0f;
    static constexpr float kKnobW       = 60.0f;
    static constexpr float kKnobH       = 78.0f;
    static constexpr float kKnobGapX    = 6.0f;
    static constexpr float kKnobGapY    = 6.0f;
    static constexpr float kPad         = 10.0f;
    static constexpr float kRowGap      = 6.0f;

    // Card chrome — matches DeviceHeaderWidget visual language so
    // chain effects read like audio devices to the user.
    static constexpr float kCardStripeH = 3.0f;          // colored top stripe
    static constexpr float kCardHeaderH = 24.0f;         // bypass + name + × strip
    static constexpr float kCardPadX    = 6.0f;          // horizontal inset
    static constexpr float kCardBypassW = 28.0f;         // "On"/"Off" pill width
    static constexpr float kCardBtnH    = 16.0f;
    static constexpr float kCardCloseW  = 16.0f;
    // Total card height = stripe + header + knob row + bottom pad.
    static constexpr float kCardH       = kCardStripeH + kCardHeaderH + kKnobH + 4.0f;

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
    void setOnChainPassChanged(ChainPassChangeCallback cb) { m_onChainPassChanged = std::move(cb); }
    void setOnChainPassRemove(ChainPassRemoveCallback cb)  { m_onChainPassRemove  = std::move(cb); }
    void setOnChainPassBypassToggle(ChainPassBypassCallback cb) { m_onChainPassBypass = std::move(cb); }
    void setOnChainPassReorder(ChainPassReorderCallback cb) { m_onChainPassReorder = std::move(cb); }
    void setOnChainAdd(ChainAddCallback cb)               { m_onChainAdd        = std::move(cb); }

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
        // Custom-knob set changed — bust the measure cache so the
        // panel can grow if more knobs were added (same reasoning as
        // rebuildShaderChain).
        invalidate();
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

    // Per-pass card in the Shader Chain section. Mirror of PostFXEntry
    // but addressing the visual clip's chain (not the engine's master
    // post-FX chain). Tracks the bypass flag so render can grey the
    // card out without an extra parallel vector.
    struct ChainPassEntry {
        std::string name;
        bool        bypassed = false;
        std::vector<std::unique_ptr<FwKnob>> knobs;
    };

    // Rebuild the shader-chain section. One ChainPassSnap per pass
    // (index 0..N-1). The matches-and-reuse fast path is what keeps
    // continuous knob drags smooth — only mutated values get pushed,
    // no widget churn.
    void rebuildShaderChain(const std::vector<ChainPassSnap>& chain) {
        bool matches = (m_chainSig.size() == chain.size()) &&
                        (m_chain.size()    == chain.size());
        if (matches) {
            for (size_t i = 0; i < chain.size(); ++i) {
                const auto& sig  = m_chainSig[i];
                const auto& incoming = chain[i];
                if (sig.name != incoming.name ||
                    sig.params.size() != incoming.params.size()) {
                    matches = false;
                    break;
                }
                for (size_t j = 0; j < incoming.params.size(); ++j) {
                    const auto& sp = sig.params[j];
                    const auto& p  = incoming.params[j];
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
                const auto& incoming = chain[i].params;
                auto& entry = m_chain[i];
                entry.bypassed = chain[i].bypassed;
                for (size_t j = 0; j < incoming.size(); ++j) {
                    entry.knobs[j]->setValue(incoming[j].value,
                                              ValueChangeSource::Automation);
                }
            }
            return;
        }

        m_chain.clear();
        m_chainSig.clear();
        m_chainSig.reserve(chain.size());
        // Keep the per-card rect vectors paired with m_chain at every
        // mutation. onLayout() recomputes the actual positions each
        // time, but render() can run between rebuild and the next
        // layout (the v1 wrapper only re-layouts on bounds change).
        // Without this, an OOB read fires when render walks m_chain
        // and indexes either rect array in lockstep.
        m_chainRemoveRects.assign(chain.size(), Rect{0, 0, 0, 0});
        m_chainBypassRects.assign(chain.size(), Rect{0, 0, 0, 0});
        m_chainCardRects.assign(chain.size(), Rect{0, 0, 0, 0});
        // Bump the measure cache so the auto-grow side effect in
        // onMeasure actually runs next frame — without this the v1
        // wrapper happily reuses the old smaller measured size and
        // the new card lands clipped off the bottom of the panel.
        invalidate();
        for (size_t i = 0; i < chain.size(); ++i) {
            ChainPassEntry entry;
            entry.name     = chain[i].name;
            entry.bypassed = chain[i].bypassed;
            PostFXSig sig;
            sig.name = chain[i].name;
            for (const auto& p : chain[i].params) {
                auto k = std::make_unique<FwKnob>();
                k->setLabel(p.name);
                k->setRange(p.min, p.max);
                k->setDefaultValue(p.defaultValue);
                k->setValue(p.value);
                int passIdx = static_cast<int>(i);
                std::string nm = p.name;
                k->setOnChange([this, passIdx, nm](float v) {
                    if (m_onChainPassChanged) m_onChainPassChanged(passIdx, nm, v);
                });
                k->setValueFormatter([](float v) {
                    char b[16];
                    std::snprintf(b, sizeof(b), "%.2f", v);
                    return std::string(b);
                });
                entry.knobs.push_back(std::move(k));
                sig.params.push_back({p.name, p.min, p.max, p.defaultValue});
            }
            m_chain.push_back(std::move(entry));
            m_chainSig.push_back(std::move(sig));
        }
    }
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
        // Same paired-vector invariant as the chain side.
        m_removeRects.assign(chain.size(), Rect{0, 0, 0, 0});
        m_postFXCardRects.assign(chain.size(), Rect{0, 0, 0, 0});
        // Same auto-grow trigger as rebuildShaderChain — structural
        // change must invalidate the measure cache or the v1 wrapper
        // keeps the panel at its old smaller size.
        invalidate();
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
        // Auto-grow: when adding a chain pass / post-fx / custom knob
        // pushes content past the user's manually-set panel height,
        // bump panelHeight so the new card is actually visible. We
        // never shrink — manual taller drags are respected. Without
        // this the wrapper's tight constraint clips off-screen rows.
        if (m_detail && minH > configuredH) {
            m_detail->setPanelHeight(minH);
            configuredH = minH;
        }
        float h = std::max(configuredH, minH);
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

        // ── Shader Chain section ────────────────────────────────────
        // Placed above Post-FX so it's the first chain you see when
        // building up a visual track. The "+ Add Pass" button always
        // appears at the trailing edge so users with empty chains have
        // a discoverable starting point.
        m_chainSectionY = y + (m_customKnobs.empty() ? 0.0f : kKnobH) + 14.0f;
        m_chainRemoveRects.assign(m_chain.size(), Rect{0, 0, 0, 0});
        m_chainBypassRects.assign(m_chain.size(), Rect{0, 0, 0, 0});
        m_chainCardRects  .assign(m_chain.size(), Rect{0, 0, 0, 0});

        // Card layout reproduces DeviceHeaderWidget's strip + header
        // pattern so chain effects read like audio devices: stripe at
        // top, then a header row with bypass pill + name + × button,
        // then the knob row underneath.
        const float cardWMin = std::max(120.0f,
            kCardPadX * 2 + kCardBypassW + 60.0f + kCardCloseW + 12.0f);
        float cy = m_chainSectionY + 18.0f;
        float cx = x0;
        for (size_t i = 0; i < m_chain.size(); ++i) {
            auto& entry = m_chain[i];
            float knobsW = static_cast<float>(entry.knobs.size())
                            * (kKnobW + kKnobGapX);
            float cardW = std::max(cardWMin,
                                    knobsW + kCardPadX * 2 - kKnobGapX);
            if (cx + cardW > bounds.x + bounds.w - kPad && cx > x0) {
                cx = x0;
                cy += kCardH + kKnobGapY;
            }
            m_chainCardRects[i] = Rect{cx, cy, cardW, kCardH};

            const float headerY = cy + kCardStripeH;
            // Bypass pill on the left (after a small inset).
            m_chainBypassRects[i] = Rect{cx + kCardPadX,
                                          headerY + (kCardHeaderH - kCardBtnH) * 0.5f,
                                          kCardBypassW, kCardBtnH};
            // Remove × on the right.
            m_chainRemoveRects[i] = Rect{cx + cardW - kCardPadX - kCardCloseW,
                                          headerY + (kCardHeaderH - kCardBtnH) * 0.5f,
                                          kCardCloseW, kCardBtnH};

            // Knob row sits below the header with a small inset.
            const float knobsY = cy + kCardStripeH + kCardHeaderH + 2.0f;
            float kx = cx + kCardPadX;
            for (auto& k : entry.knobs) {
                k->measure(Constraints::tight(kKnobW, kKnobH), ctx);
                k->layout(Rect{kx, knobsY, kKnobW, kKnobH}, ctx);
                kx += kKnobW + kKnobGapX;
            }
            cx += cardW + 8.0f;
        }
        // "+ Add Pass" button — vertically centred on the card row.
        constexpr float kAddBtnW = 72.0f, kAddBtnH = 22.0f;
        if (cx + kAddBtnW > bounds.x + bounds.w - kPad && cx > x0) {
            cx = x0;
            cy += kCardH + kKnobGapY;
        }
        m_chainAddRect = Rect{cx, cy + (kCardH - kAddBtnH) * 0.5f,
                              kAddBtnW, kAddBtnH};

        // ── Post-FX section ─────────────────────────────────────────
        // Post-FX cards mirror the chain card geometry (stripe +
        // header + knob row) for visual consistency, minus the bypass
        // pill since the engine's master post-FX chain doesn't track
        // bypass per stage today.
        const float chainBottom = cy + (m_chain.empty() ? kAddBtnH : kCardH);
        m_postFXSectionY = chainBottom + 14.0f;
        m_removeRects     .assign(m_postFX.size(), Rect{0, 0, 0, 0});
        m_postFXCardRects .assign(m_postFX.size(), Rect{0, 0, 0, 0});

        float py = m_postFXSectionY + 18.0f;
        float px = x0;
        for (size_t i = 0; i < m_postFX.size(); ++i) {
            auto& entry = m_postFX[i];
            float knobsW = static_cast<float>(entry.knobs.size())
                            * (kKnobW + kKnobGapX);
            float cardW  = std::max(cardWMin, knobsW + kCardPadX * 2 - kKnobGapX);
            if (px + cardW > bounds.x + bounds.w - kPad && px > x0) {
                px = x0;
                py += kCardH + kKnobGapY;
            }
            m_postFXCardRects[i] = Rect{px, py, cardW, kCardH};
            const float headerY = py + kCardStripeH;
            m_removeRects[i] = Rect{px + cardW - kCardPadX - kCardCloseW,
                                     headerY + (kCardHeaderH - kCardBtnH) * 0.5f,
                                     kCardCloseW, kCardBtnH};
            const float knobsY = py + kCardStripeH + kCardHeaderH + 2.0f;
            float kx = px + kCardPadX;
            for (auto& k : entry.knobs) {
                k->measure(Constraints::tight(kKnobW, kKnobH), ctx);
                k->layout(Rect{kx, knobsY, kKnobW, kKnobH}, ctx);
                kx += kKnobW + kKnobGapX;
            }
            px += cardW + 8.0f;
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
        // Shader chain: bypass pill + remove × + per-pass knobs +
        // drag-to-reorder if the press lands on the header background.
        auto inRect = [](const Rect& rr, float x, float y) {
            return x >= rr.x && x < rr.x + rr.w &&
                   y >= rr.y && y < rr.y + rr.h;
        };
        for (size_t i = 0; i < m_chain.size(); ++i) {
            if (!rightClick &&
                inRect(m_chainBypassRects[i], e.x, e.y)) {
                if (m_onChainPassBypass) {
                    m_chain[i].bypassed = !m_chain[i].bypassed;
                    m_onChainPassBypass(static_cast<int>(i),
                                          m_chain[i].bypassed);
                }
                return true;
            }
            if (!rightClick &&
                inRect(m_chainRemoveRects[i], e.x, e.y)) {
                if (m_onChainPassRemove)
                    m_onChainPassRemove(static_cast<int>(i));
                return true;
            }
            for (auto& k : m_chain[i].knobs) {
                if (!hitWidgetV2(*k, e.x, e.y)) continue;
                forwardTo(*k);
                return true;
            }
            // Header background drag — anything inside the card's
            // top stripe + header strip that isn't the bypass pill or
            // × button arms a reorder gesture. Capturing the mouse
            // means subsequent move/up events route here, not to
            // whatever lives behind the panel — that prevents the
            // fall-through-to-parent crash and lets us track the drop
            // target until release.
            const Rect& cr = m_chainCardRects[i];
            const float headerBottom = cr.y + kCardStripeH + kCardHeaderH;
            if (!rightClick &&
                e.x >= cr.x && e.x < cr.x + cr.w &&
                e.y >= cr.y && e.y < headerBottom) {
                m_chainDragFrom    = static_cast<int>(i);
                m_chainDragActive  = false;
                m_chainDragOriginX = e.x;
                m_chainDragOriginY = e.y;
                m_chainDragCurX    = e.x;
                m_chainDragCurY    = e.y;
                captureMouse();
                return true;
            }
        }
        // "+ Add Pass" button.
        if (!rightClick &&
            e.x >= m_chainAddRect.x && e.x < m_chainAddRect.x + m_chainAddRect.w &&
            e.y >= m_chainAddRect.y && e.y < m_chainAddRect.y + m_chainAddRect.h) {
            if (m_onChainAdd) m_onChainAdd(e.x, e.y);
            return true;
        }
        for (size_t i = 0; i < m_postFX.size(); ++i) {
            if (!rightClick &&
                inRect(m_removeRects[i], e.x, e.y)) {
                if (m_onPostFXRemove)
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
        if (m_chainDragFrom >= 0) {
            m_chainDragCurX = e.x;
            m_chainDragCurY = e.y;
            // Promote the gesture to active once the cursor leaves a
            // small dead-zone around the press point. Until then,
            // releasing snaps back to a no-op (so a normal click on
            // the header doesn't accidentally rearrange anything).
            const float dx = e.x - m_chainDragOriginX;
            const float dy = e.y - m_chainDragOriginY;
            if (!m_chainDragActive && (dx * dx + dy * dy) > 25.0f)
                m_chainDragActive = true;
            return true;
        }
        // CRITICAL: skip self-dispatch. fw2::Widget::dispatchMouseDown
        // captures the mouse to *us* whenever onMouseDown returns
        // false (which happens for any click on empty card space),
        // and naively forwarding here would re-enter dispatchMouseMove
        // → onMouseMove → … → stack overflow → silent process kill.
        if (Widget* cap = Widget::capturedWidget(); cap && cap != this) {
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
        if (m_chainDragFrom >= 0) {
            int   from   = m_chainDragFrom;
            bool  active = m_chainDragActive;
            float upX    = e.x;
            m_chainDragFrom   = -1;
            m_chainDragActive = false;
            releaseMouse();
            if (active && m_onChainPassReorder) {
                int target = computeChainReorderTarget(upX);
                if (target >= 0 && target != from && target != from + 1) {
                    m_onChainPassReorder(from, target);
                }
            }
            return true;
        }
        // Same self-dispatch guard as onMouseMove — never re-enter
        // ourselves via Widget::capturedWidget().
        if (Widget* cap = Widget::capturedWidget(); cap && cap != this) {
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

        // ── Shader Chain header + cards + "+ Add Pass" ──────────────
        r.drawRect(m_bounds.x + kPad, m_chainSectionY,
                    m_bounds.w - 2.0f * kPad, 1.0f,
                    Color{60, 60, 68, 255});
        tm.drawText(r, "Shader Chain",
                    m_bounds.x + kPad, m_chainSectionY + 14.0f,
                    titleSize, ::yawn::ui::Theme::textSecondary);
        // Card chrome — match DeviceHeaderWidget visual language.
        // Magenta-ish stripe matches the AudioEffect device colour
        // since chain effects play the same conceptual role.
        const Color kCardStripe   {180,  80, 200, 255};
        const Color kCardStripeBp { 90,  40, 100, 255};   // dimmed when bypassed
        const Color kCardBg       { 36,  36,  42, 255};
        const Color kCardBgBp     { 30,  30,  34, 255};
        const Color kCardBorder   { 60,  60,  68, 255};
        const Color kBypassOnBg   { 40, 100,  40, 255};
        const Color kBypassOffBg  {100,  40,  40, 255};
        const Color kBypassOnTxt  {120, 220, 120, 255};
        const Color kBypassOffTxt {220, 120, 120, 255};
        const Color kRemoveBg     { 60,  30,  30, 255};
        const Color kRemoveTxt    {200, 100, 100, 255};
        for (size_t i = 0; i < m_chain.size(); ++i) {
            auto& entry = m_chain[i];
            const Rect& cr = m_chainCardRects[i];
            const bool bp = entry.bypassed;
            const bool dragging = (m_chainDragActive &&
                                    m_chainDragFrom == static_cast<int>(i));

            // Card body + outline. Dim while being drag-reordered so
            // the user can see where the source card lifts from.
            const Color bodyC   = dragging ? Color{20, 20, 24, 255}
                                            : (bp ? kCardBgBp : kCardBg);
            const Color borderC = dragging ? Color{200, 160, 80, 255}
                                            : kCardBorder;
            r.drawRect(cr.x, cr.y, cr.w, cr.h, bodyC);
            r.drawRect(cr.x, cr.y, cr.w, kCardStripeH,
                        bp ? kCardStripeBp : kCardStripe);
            r.drawRectOutline(cr.x, cr.y, cr.w, cr.h, borderC);

            // Bypass pill (on/off).
            const Rect& bypass = m_chainBypassRects[i];
            r.drawRect(bypass.x, bypass.y, bypass.w, bypass.h,
                        bp ? kBypassOffBg : kBypassOnBg);
            const char* bpLabel = bp ? "Off" : "On";
            const float bpLabelW = tm.textWidth(bpLabel, titleSize);
            tm.drawText(r, bpLabel,
                         bypass.x + (bypass.w - bpLabelW) * 0.5f,
                         bypass.y + 2.0f,
                         titleSize, bp ? kBypassOffTxt : kBypassOnTxt);

            // Pass index + name in the header, between bypass and ×.
            std::string label = std::to_string(i + 1) + ". " + entry.name;
            const float nameX = bypass.x + bypass.w + 6.0f;
            tm.drawText(r, label, nameX, bypass.y + 2.0f, titleSize,
                         bp ? ::yawn::ui::Theme::textDim
                            : ::yawn::ui::Theme::textPrimary);

            // Remove × on the right.
            const Rect& xr = m_chainRemoveRects[i];
            r.drawRect(xr.x, xr.y, xr.w, xr.h, kRemoveBg);
            const float xLabelW = tm.textWidth("X", titleSize);
            tm.drawText(r, "X",
                         xr.x + (xr.w - xLabelW) * 0.5f,
                         xr.y + 2.0f, titleSize, kRemoveTxt);

            for (auto& k : entry.knobs) k->render(ctx);
        }
        // Drop indicator while a card is being drag-reordered. Drawn
        // as a vertical bar in the gap between cards (or at the
        // trailing edge for end-of-list drops). Only shown once the
        // gesture has crossed the dead-zone, to avoid flicker on
        // every header click.
        if (m_chainDragActive && !m_chain.empty()) {
            int target = computeChainReorderTarget(m_chainDragCurX);
            if (target >= 0 && target != m_chainDragFrom &&
                target != m_chainDragFrom + 1) {
                float ix;
                if (target == 0) {
                    ix = m_chainCardRects[0].x - 4.0f;
                } else if (target >= static_cast<int>(m_chain.size())) {
                    const Rect& last = m_chainCardRects.back();
                    ix = last.x + last.w + 4.0f;
                } else {
                    const Rect& cr = m_chainCardRects[target];
                    ix = cr.x - 4.0f;
                }
                const float iy = m_chainCardRects[0].y;
                r.drawRect(ix - 1.0f, iy, 3.0f, kCardH,
                            Color{220, 180, 80, 255});
            }
        }
        // "+ Add Pass" pill
        r.drawRoundedRect(m_chainAddRect.x, m_chainAddRect.y,
                           m_chainAddRect.w, m_chainAddRect.h,
                           m_chainAddRect.h * 0.5f,
                           Color{60, 130, 80, 220});
        const float addLh = tm.lineHeight(titleSize);
        const float addTw = tm.textWidth("+ Add Pass", titleSize);
        tm.drawText(r, "+ Add Pass",
                    m_chainAddRect.x + (m_chainAddRect.w - addTw) * 0.5f,
                    m_chainAddRect.y + (m_chainAddRect.h - addLh) * 0.5f
                        - addLh * 0.15f,
                    titleSize, Color{240, 240, 245, 255});

        if (!m_postFX.empty()) {
            r.drawRect(m_bounds.x + kPad, m_postFXSectionY,
                        m_bounds.w - 2.0f * kPad, 1.0f,
                        Color{60, 60, 68, 255});
            tm.drawText(r, "Post FX",
                        m_bounds.x + kPad, m_postFXSectionY + 14.0f,
                        titleSize, ::yawn::ui::Theme::textSecondary);

            for (size_t i = 0; i < m_postFX.size(); ++i) {
                auto& entry = m_postFX[i];
                const Rect& cr = m_postFXCardRects[i];
                r.drawRect(cr.x, cr.y, cr.w, cr.h, kCardBg);
                r.drawRect(cr.x, cr.y, cr.w, kCardStripeH, kCardStripe);
                r.drawRectOutline(cr.x, cr.y, cr.w, cr.h, kCardBorder);

                const Rect& xr = m_removeRects[i];
                std::string label = std::to_string(i + 1) + ". " + entry.name;
                tm.drawText(r, label,
                             cr.x + kCardPadX, xr.y + 2.0f,
                             titleSize, ::yawn::ui::Theme::textPrimary);
                r.drawRect(xr.x, xr.y, xr.w, xr.h, kRemoveBg);
                const float xLabelW = tm.textWidth("X", titleSize);
                tm.drawText(r, "X",
                             xr.x + (xr.w - xLabelW) * 0.5f,
                             xr.y + 2.0f, titleSize, kRemoveTxt);

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

    // Map a horizontal cursor position to an insertion slot in the
    // chain. Returns a slot index in [0, m_chain.size()] — slot N
    // means "drop here as the new last entry". If the cursor sits
    // past the last card, returns the last slot. Returns -1 when the
    // chain is empty (no valid drop targets).
    int computeChainReorderTarget(float cursorX) const {
        if (m_chain.empty()) return -1;
        for (size_t i = 0; i < m_chainCardRects.size(); ++i) {
            const Rect& cr = m_chainCardRects[i];
            const float mid = cr.x + cr.w * 0.5f;
            if (cursorX < mid)
                return static_cast<int>(i);
        }
        return static_cast<int>(m_chain.size());
    }

    float columnWidthFor(const FwKnob& k, const UIContext& ctx) const {
        if (!ctx.textMetrics) return kKnobW;
        const float labelSize = 9.0f;
        const float lw = ctx.textMetrics->textWidth(k.label(), labelSize);
        return std::max(kKnobW, lw + 6.0f);
    }

    float computeRequiredHeight(float panelW, const UIContext& ctx) const {
        float topY   = kHandleH + kTitleH + 22.0f;
        float rowH   = kKnobH + kKnobGapY;       // custom-knob row height
        float cardRowH = kCardH + kKnobGapY;     // card-row height (taller)
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

        const float cardWMin = std::max(120.0f,
            kCardPadX * 2 + kCardBypassW + 60.0f + kCardCloseW + 12.0f);

        // Shader Chain row(s) — taller card geometry to match the new
        // header strip + bypass + name + × layout, plus the trailing
        // "+ Add Pass" button.
        float chainBottom = customBottom;
        {
            float cy = customBottom + 18.0f;
            float cx = kPad;
            for (auto& entry : m_chain) {
                float knobsW = static_cast<float>(entry.knobs.size())
                                * (kKnobW + kKnobGapX);
                float cardW  = std::max(cardWMin, knobsW + kCardPadX * 2 - kKnobGapX);
                if (cx + cardW > panelW - kPad && cx > kPad) {
                    cx = kPad;
                    cy += cardRowH;
                }
                cx += cardW + 8.0f;
            }
            constexpr float kAddBtnW = 72.0f;
            if (cx + kAddBtnW > panelW - kPad && cx > kPad) {
                cx = kPad;
                cy += cardRowH;
            }
            chainBottom = cy + kCardH;
        }

        float fxBottom = chainBottom + 14.0f;
        if (!m_postFX.empty()) {
            float py = chainBottom + 18.0f;
            float px = kPad;
            for (auto& entry : m_postFX) {
                float knobsW = static_cast<float>(entry.knobs.size())
                                * (kKnobW + kKnobGapX);
                float cardW  = std::max(cardWMin, knobsW + kCardPadX * 2 - kKnobGapX);
                if (px + cardW > panelW - kPad && px > kPad) {
                    px = kPad;
                    py += cardRowH;
                }
                px += cardW + 8.0f;
            }
            fxBottom = py + kCardH;
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

    // Shader Chain (per-track).
    std::vector<ChainPassEntry> m_chain;
    std::vector<PostFXSig>      m_chainSig;
    std::vector<Rect>           m_chainRemoveRects;   // × button hit-rect per card
    std::vector<Rect>           m_chainBypassRects;   // On/Off pill hit-rect per card
    std::vector<Rect>           m_chainCardRects;     // full-card outline per card
    Rect                        m_chainAddRect{};
    float                       m_chainSectionY = 0.0f;
    ChainPassChangeCallback     m_onChainPassChanged;
    ChainPassRemoveCallback     m_onChainPassRemove;
    ChainPassBypassCallback     m_onChainPassBypass;
    ChainPassReorderCallback    m_onChainPassReorder;
    ChainAddCallback            m_onChainAdd;

    // Drag-to-reorder state for the Shader Chain cards. -1 = no
    // gesture. Goes "armed" on a header press, becomes "active" once
    // the cursor moves past a small dead-zone so quick clicks on the
    // header don't accidentally rearrange the chain.
    int   m_chainDragFrom    = -1;
    bool  m_chainDragActive  = false;
    float m_chainDragOriginX = 0.0f;
    float m_chainDragOriginY = 0.0f;
    float m_chainDragCurX    = 0.0f;
    float m_chainDragCurY    = 0.0f;

    // Post-FX card outlines (paired with m_postFX, like the chain side).
    std::vector<Rect>           m_postFXCardRects;

    ::yawn::ui::fw::DetailPanelWidget* m_detail = nullptr;
    bool  m_handleDrag   = false;
    float m_handleStartY = 0.0f;
    float m_handleStartH = 0.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
