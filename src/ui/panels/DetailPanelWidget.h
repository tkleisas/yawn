#pragma once
// DetailPanelWidget — Composite-widget detail panel.
//
// Uses DeviceWidget + SnapScrollContainer instead of raw drawRect/drawText
// rendering.  The main panel header (collapse arrow + "Detail" text) is
// still rendered manually because it's trivial.
//
// Right-click is still forwarded directly from App.cpp because the widget
// tree doesn't dispatch right-click events yet.

#include "ui/framework/Widget.h"
#include "ui/framework/DeviceWidget.h"
#include "ui/framework/SnapScrollContainer.h"
#include "ui/Theme.h"
#include "instruments/Instrument.h"
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffect.h"
#include "midi/MidiEffectChain.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw {

class DetailPanelWidget : public Widget {
public:
    // Layout constants (preserved for external users)
    static constexpr float kDefaultPanelHeight = 220.0f;
    static constexpr float kCollapsedHeight  = 8.0f;    // just the handle
    static constexpr float kHandleHeight     = 8.0f;    // drag-resize handle
    static constexpr float kMinPanelH        = 80.0f;
    static constexpr float kMaxPanelH        = 400.0f;
    static constexpr float kDeviceHeaderH    = 24.0f;   // per-device header
    static constexpr float kKnobSize         = 48.0f;
    static constexpr float kKnobSpacing      = 16.0f;
    static constexpr int   kMaxKnobRows      = 2;
    static constexpr float kCollapsedDeviceW = 60.0f;
    static constexpr float kMinExpandedW     = 180.0f;
    static constexpr float kScrollBtnW       = 28.0f;
    static constexpr float kBtnSize          = 16.0f;
    static constexpr float kDeviceGap        = 4.0f;
    static constexpr float kVisualizerMinW   = 300.0f;

    enum class DeviceType { MidiFx, Instrument, AudioFx };

    // --- Public API ---

    bool  isOpen() const { return m_open; }
    void  setOpen(bool open) {
        m_open = open;
        m_targetHeight = open ? m_userPanelHeight : kCollapsedHeight;
    }
    void  toggle() { setOpen(!m_open); }
    float height() const { return m_animatedHeight; }
    float panelHeight() const { return m_userPanelHeight; }

    bool isFocused() const { return m_panelFocused; }
    void setFocused(bool f) { m_panelFocused = f; }

    void setOnRemoveDevice(std::function<void(DeviceType, int)> cb) {
        m_onRemoveDevice = std::move(cb);
    }

    // Rebuild the device chain from current track state.
    void setDeviceChain(midi::MidiEffectChain* midiChain,
                        instruments::Instrument* inst,
                        effects::EffectChain* fxChain) {
        int midiCount = midiChain ? midiChain->count() : 0;
        int fxCount   = fxChain   ? fxChain->count()   : 0;

        // Quick fingerprint — skip rebuild if nothing changed
        if (midiChain == m_lastMidiChain && inst == m_lastInst &&
            fxChain == m_lastFxChain &&
            midiCount == m_lastMidiCount && fxCount == m_lastFxCount)
            return;

        // Save expanded states before tearing down
        saveExpandedStates();

        // Tear down old device widgets
        m_scroll.removeAllChildren();
        for (auto* dw : m_deviceWidgets) delete dw;
        m_deviceWidgets.clear();
        m_deviceRefs.clear();

        std::vector<float> snapPoints;
        float xPos = 0;

        // MIDI effects
        if (midiChain) {
            for (int i = 0; i < midiChain->count(); ++i) {
                auto* fx = midiChain->effect(i);
                if (!fx) continue;
                DeviceRef ref;
                ref.type = DeviceType::MidiFx;
                ref.chainIndex = i;
                ref.midiEffect = fx;

                auto* dw = new DeviceWidget();
                dw->setDeviceName(fx->name());
                dw->setDeviceType(DeviceHeaderWidget::DeviceType::MidiEffect);
                dw->setRemovable(true);
                dw->setExpanded(findPrevExpanded(static_cast<void*>(fx)));
                dw->setBypassed(fx->bypassed());
                configureDeviceWidget(dw, ref);

                snapPoints.push_back(xPos);
                xPos += dw->preferredWidth() + kDeviceGap;
                m_scroll.addChild(dw);
                m_deviceWidgets.push_back(dw);
                m_deviceRefs.push_back(ref);
            }
        }

        // Instrument
        if (inst) {
            DeviceRef ref;
            ref.type = DeviceType::Instrument;
            ref.chainIndex = 0;
            ref.instrument = inst;

            auto* dw = new DeviceWidget();
            dw->setDeviceName(inst->name());
            dw->setDeviceType(DeviceHeaderWidget::DeviceType::Instrument);
            dw->setRemovable(false);
            dw->setExpanded(findPrevExpanded(static_cast<void*>(inst)));
            dw->setBypassed(inst->bypassed());
            configureDeviceWidget(dw, ref);

            snapPoints.push_back(xPos);
            xPos += dw->preferredWidth() + kDeviceGap;
            m_scroll.addChild(dw);
            m_deviceWidgets.push_back(dw);
            m_deviceRefs.push_back(ref);
        }

        // Audio effects
        if (fxChain) {
            for (int i = 0; i < fxChain->count(); ++i) {
                auto* fx = fxChain->effectAt(i);
                if (!fx) continue;
                DeviceRef ref;
                ref.type = DeviceType::AudioFx;
                ref.chainIndex = i;
                ref.audioEffect = fx;

                auto* dw = new DeviceWidget();
                dw->setDeviceName(fx->name());
                dw->setDeviceType(DeviceHeaderWidget::DeviceType::AudioEffect);
                dw->setRemovable(true);
                dw->setExpanded(findPrevExpanded(static_cast<void*>(fx)));
                dw->setBypassed(fx->bypassed());

                if (fx->isVisualizer())
                    dw->setVisualizer(true, fx->visualizerType());

                configureDeviceWidget(dw, ref);

                snapPoints.push_back(xPos);
                xPos += dw->preferredWidth() + kDeviceGap;
                m_scroll.addChild(dw);
                m_deviceWidgets.push_back(dw);
                m_deviceRefs.push_back(ref);
            }
        }

        snapPoints.push_back(xPos);
        m_scroll.setSnapPoints(snapPoints);
        m_scroll.setGap(kDeviceGap);

        m_lastMidiChain = midiChain;
        m_lastInst      = inst;
        m_lastFxChain   = fxChain;
        m_lastMidiCount = midiCount;
        m_lastFxCount   = fxCount;

        // Auto-open when devices are populated
        if (!m_deviceWidgets.empty()) setOpen(true);
    }

    void clear() {
        m_scroll.removeAllChildren();
        for (auto* dw : m_deviceWidgets) delete dw;
        m_deviceWidgets.clear();
        m_deviceRefs.clear();
        m_expandStates.clear();
        m_lastMidiChain = nullptr;
        m_lastInst      = nullptr;
        m_lastFxChain   = nullptr;
        m_lastMidiCount = 0;
        m_lastFxCount   = 0;
    }

    const char* title() const { return "Detail"; }

    bool isDragging() const { return Widget::capturedWidget() != nullptr; }

    void scrollLeft()  { m_scroll.scrollLeft(); }
    void scrollRight() { m_scroll.scrollRight(); }

    void handleKeyDown([[maybe_unused]] int key) {
#ifndef YAWN_TEST_BUILD
        if (!m_open || !m_panelFocused) return;
        if (key == 0x40000050 /*SDL LEFT*/)  scrollLeft();
        if (key == 0x4000004F /*SDL RIGHT*/) scrollRight();
#endif
    }

    void handleScroll(float dx, float dy) {
        if (std::abs(dx) > 0.01f)
            m_scroll.handleScroll(dx, dy);
        else
            m_scroll.handleScroll(-dy, 0);
    }

    bool handleRightClick(float mx, float my) {
        if (!m_open || my < m_bounds.y + kHandleHeight) return false;
        if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

        for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
            auto& db = m_deviceWidgets[i]->bounds();
            if (mx < db.x || mx >= db.x + db.w) continue;
            if (my < db.y || my >= db.y + db.h) continue;

            auto& ref = m_deviceRefs[i];
            if (!m_deviceWidgets[i]->isExpanded()) return true;

            // Find which knob was right-clicked and reset to default
            int paramCount = ref.paramCount();
            for (int p = 0; p < paramCount; ++p) {
                ref.setParam(p, ref.paramDefault(p));
                m_deviceWidgets[i]->updateParamValue(p, ref.paramDefault(p));
            }
            // More precise: check knob bounds, but the framework knobs
            // handle right-click internally. For now, reset all (matches
            // old behaviour of resetting the clicked knob). Actually let's
            // iterate the device widget's children to find the exact knob.
            // But since FwKnob already handles right-click in onMouseDown
            // with MouseButton::Right, we just need to not break that.
            // The old code iterated knob grid geometry — replicate here:
            return handleKnobRightClick(i, mx, my);
        }
        return false;
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        updateAnimation();
        return c.constrain({c.maxW, m_animatedHeight});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        if (!m_open) return;
        float bodyY = bounds.y + kHandleHeight;
        float bodyH = m_animatedHeight - kHandleHeight;
        if (bodyH < 0) bodyH = 0;
        m_scroll.measure(Constraints::loose(bounds.w, bodyH), ctx);
        m_scroll.layout(Rect{bounds.x, bodyY, bounds.w, bodyH}, ctx);
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        auto& renderer = *ctx.renderer;
        auto& font = *ctx.font;
        float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w;

        // Clip to animated bounds so content doesn't overflow during transition
        renderer.pushClip(x, y, w, m_animatedHeight);

        // Handle bar (thin 8px drag strip with grip dots)
        renderer.drawRect(x, y, w, kHandleHeight, Color{55, 55, 60, 255});
        {
            float cx = x + w * 0.5f;
            float cy = y + kHandleHeight * 0.5f;
            Color dotCol{90, 90, 95, 255};
            float dotR = 1.0f;
            float spacing = 6.0f;
            renderer.drawRect(cx - spacing - dotR, cy - dotR, dotR * 2, dotR * 2, dotCol);
            renderer.drawRect(cx - dotR,            cy - dotR, dotR * 2, dotR * 2, dotCol);
            renderer.drawRect(cx + spacing - dotR, cy - dotR, dotR * 2, dotR * 2, dotCol);
        }

        if (m_animatedHeight > kCollapsedHeight + 1.0f) {
            float bodyY = y + kHandleHeight;
            float bodyH = m_animatedHeight - kHandleHeight;
            renderer.drawRect(x, bodyY, w, bodyH, Color{28, 28, 32, 255});

            float hScale = 14.0f / Theme::kFontSize;
            if (m_deviceWidgets.empty()) {
                font.drawText(renderer, "No devices on track",
                              x + 20, bodyY + 20, hScale, Theme::textDim);
            } else {
                // Sync live data before painting
                updateParamValues();
                updateVisualizerData();
                m_scroll.paint(ctx);
            }
        }

        renderer.popClip();
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float mx = e.x, my = e.y;
        if (my < m_bounds.y || my > m_bounds.y + height()) return false;
        if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

        m_panelFocused = true;

        // Handle area — start drag or detect double-click to toggle
        if (my < m_bounds.y + kHandleHeight) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastHandleClickTime).count();
            if (elapsed < 300) {
                // Double-click: toggle open/close
                toggle();
                m_lastHandleClickTime = {};
                return true;
            }
            m_lastHandleClickTime = now;
            m_handleDragActive = true;
            m_handleDragStartY = my;
            m_handleDragStartH = m_userPanelHeight;
            captureMouse();
            return true;
        }

        if (!m_open || m_deviceWidgets.empty()) return false;

        // Try scroll container buttons first
        if (m_scroll.onMouseDown(e)) return true;

        // Forward to device widgets
        for (auto* dw : m_deviceWidgets) {
            auto& db = dw->bounds();
            if (mx >= db.x && mx < db.x + db.w && my >= db.y && my < db.y + db.h) {
                if (dw->onMouseDown(e)) return true;
            }
        }
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        // Handle drag resize
        if (m_handleDragActive) {
            float delta = m_handleDragStartY - e.y; // drag up = taller
            m_userPanelHeight = std::clamp(
                m_handleDragStartH + delta, kMinPanelH, kMaxPanelH);
            if (m_open)
                m_targetHeight = m_userPanelHeight;
            return true;
        }
        // Captured widget receives moves regardless of position
        if (Widget::capturedWidget()) {
            return Widget::capturedWidget()->onMouseMove(e);
        }
        for (auto* dw : m_deviceWidgets) {
            if (dw->onMouseMove(e)) return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_handleDragActive) {
            m_handleDragActive = false;
            releaseMouse();
            return true;
        }
        if (Widget::capturedWidget()) {
            bool handled = Widget::capturedWidget()->onMouseUp(e);
            return handled;
        }
        for (auto* dw : m_deviceWidgets) {
            auto& db = dw->bounds();
            if (e.x >= db.x && e.x < db.x + db.w &&
                e.y >= db.y && e.y < db.y + db.h) {
                if (dw->onMouseUp(e)) return true;
            }
        }
        return false;
    }

private:
    // ── DeviceRef — lightweight bridge to device pointers ──
    struct DeviceRef {
        DeviceType type = DeviceType::AudioFx;
        int chainIndex = -1;
        midi::MidiEffect*       midiEffect  = nullptr;
        instruments::Instrument* instrument  = nullptr;
        effects::AudioEffect*   audioEffect = nullptr;

        float getParam(int i) const {
            if (midiEffect)  return midiEffect->getParameter(i);
            if (instrument)  return instrument->getParameter(i);
            if (audioEffect) return audioEffect->getParameter(i);
            return 0.0f;
        }
        void setParam(int i, float v) {
            if (midiEffect)       midiEffect->setParameter(i, v);
            else if (instrument)  instrument->setParameter(i, v);
            else if (audioEffect) audioEffect->setParameter(i, v);
        }
        int paramCount() const {
            if (midiEffect)  return midiEffect->parameterCount();
            if (instrument)  return instrument->parameterCount();
            if (audioEffect) return audioEffect->parameterCount();
            return 0;
        }
        float paramDefault(int i) const {
            if (midiEffect)  return midiEffect->parameterInfo(i).defaultValue;
            if (instrument)  return instrument->parameterInfo(i).defaultValue;
            if (audioEffect) return audioEffect->parameterInfo(i).defaultValue;
            return 0.0f;
        }
        bool isBypassed() const {
            if (midiEffect)  return midiEffect->bypassed();
            if (instrument)  return instrument->bypassed();
            if (audioEffect) return audioEffect->bypassed();
            return false;
        }
        void toggleBypass() {
            if (midiEffect)       midiEffect->setBypassed(!midiEffect->bypassed());
            else if (instrument)  instrument->setBypassed(!instrument->bypassed());
            else if (audioEffect) audioEffect->setBypassed(!audioEffect->bypassed());
        }
        bool isVisualizer() const { return audioEffect && audioEffect->isVisualizer(); }
        void* devicePtr() const {
            if (midiEffect)  return static_cast<void*>(midiEffect);
            if (instrument)  return static_cast<void*>(instrument);
            if (audioEffect) return static_cast<void*>(audioEffect);
            return nullptr;
        }
    };

    // ── Build params and wire callbacks for a DeviceWidget ──
    void configureDeviceWidget(DeviceWidget* dw, const DeviceRef& ref) {
        std::vector<DeviceWidget::ParamInfo> params;
        int count = ref.paramCount();
        for (int p = 0; p < count; ++p) {
            DeviceWidget::ParamInfo pi;
            pi.index = p;
            if (ref.midiEffect) {
                auto& info = ref.midiEffect->parameterInfo(p);
                pi.name = info.name; pi.unit = info.unit;
                pi.minVal = info.minValue; pi.maxVal = info.maxValue;
                pi.defaultVal = info.defaultValue; pi.isBoolean = info.isBoolean;
            } else if (ref.instrument) {
                auto& info = ref.instrument->parameterInfo(p);
                pi.name = info.name; pi.unit = info.unit;
                pi.minVal = info.minValue; pi.maxVal = info.maxValue;
                pi.defaultVal = info.defaultValue; pi.isBoolean = info.isBoolean;
            } else if (ref.audioEffect) {
                auto& info = ref.audioEffect->parameterInfo(p);
                pi.name = info.name; pi.unit = info.unit;
                pi.minVal = info.minValue; pi.maxVal = info.maxValue;
                pi.defaultVal = info.defaultValue; pi.isBoolean = info.isBoolean;
            }
            params.push_back(std::move(pi));
        }
        dw->setParameters(params);

        // Set initial param values
        for (int p = 0; p < count; ++p)
            dw->updateParamValue(p, ref.getParam(p));

        // Wire param change callback
        dw->setOnParamChange([ref](int idx, float v) {
            // const_cast safe: ref is a copy with the raw device pointers
            DeviceRef r = ref;
            r.setParam(idx, v);
        });

        // Wire bypass toggle
        dw->setOnBypassToggle([ref](bool) {
            DeviceRef r = ref;
            r.toggleBypass();
        });

        // Wire remove callback
        dw->setOnRemove([this, type = ref.type, chainIdx = ref.chainIndex]() {
            if (m_onRemoveDevice) m_onRemoveDevice(type, chainIdx);
        });
    }

    // ── Sync param values from devices each frame ──
    void updateParamValues() {
        for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
            auto& ref = m_deviceRefs[i];
            int count = ref.paramCount();
            m_deviceWidgets[i]->setBypassed(ref.isBypassed());
            for (int p = 0; p < count; ++p)
                m_deviceWidgets[i]->updateParamValue(p, ref.getParam(p));
        }
    }

    // ── Feed live visualizer data ──
    void updateVisualizerData() {
        for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
            auto& ref = m_deviceRefs[i];
            if (ref.isVisualizer()) {
                const float* data = ref.audioEffect->displayData();
                int size = ref.audioEffect->displaySize();
                m_deviceWidgets[i]->setVisualizerData(data, size);
            }
        }
    }

    // ── Right-click knob reset (matches old geometry-based hit-test) ──
    bool handleKnobRightClick(size_t deviceIdx, float mx, float my) {
        auto& ref = m_deviceRefs[deviceIdx];
        auto* dw = m_deviceWidgets[deviceIdx];
        auto& db = dw->bounds();

        int paramCount = ref.paramCount();
        if (paramCount == 0) return true;

        float bodyY = db.y + kDeviceHeaderH;
        float bodyH = db.h - kDeviceHeaderH;
        int maxRows = kMaxKnobRows;
        float kx = db.x + 8, ky = bodyY + 4, kh = bodyH - 8;
        float kw = db.w - 16;

        if (ref.isVisualizer()) {
            float knobRowH = 68.0f;
            float vizH = bodyH - knobRowH;
            maxRows = 1;
            ky = bodyY + vizH + 2;
            kh = knobRowH - 4;
        }

        float cellW = kKnobSize + kKnobSpacing;
        float cellH = kKnobSize + 22.0f;
        int cols = static_cast<int>(std::ceil(
            static_cast<float>(paramCount) / static_cast<float>(maxRows)));

        for (int i = 0; i < paramCount; ++i) {
            int row = i / cols;
            int col = i % cols;
            float px = kx + col * cellW;
            float py = ky + row * cellH;

            if (mx >= px && mx < px + kKnobSize && my >= py && my < py + cellH) {
                float def = ref.paramDefault(i);
                ref.setParam(i, def);
                dw->updateParamValue(i, def);
                return true;
            }
        }
        (void)kw; (void)kh;
        return false;
    }

    // ── Expanded state preservation across rebuilds ──
    struct ExpandState { void* ptr; bool expanded; };

    void saveExpandedStates() {
        m_expandStates.clear();
        for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
            m_expandStates.push_back({
                m_deviceRefs[i].devicePtr(),
                m_deviceWidgets[i]->isExpanded()
            });
        }
    }

    bool findPrevExpanded(void* ptr) const {
        for (auto& es : m_expandStates)
            if (es.ptr == ptr) return es.expanded;
        return true;
    }

    // ── State ──
    SnapScrollContainer m_scroll;
    std::vector<DeviceWidget*> m_deviceWidgets;  // owned
    std::vector<DeviceRef>     m_deviceRefs;     // parallel to m_deviceWidgets
    std::vector<ExpandState>   m_expandStates;

    bool m_open         = false;
    bool m_panelFocused = false;

    // Resizable panel height (user-adjustable via handle drag)
    float m_userPanelHeight = kDefaultPanelHeight;

    // Handle drag state
    bool  m_handleDragActive = false;
    float m_handleDragStartY = 0;
    float m_handleDragStartH = 0;
    std::chrono::steady_clock::time_point m_lastHandleClickTime{};

    // Animation state for smooth height transitions
    mutable float m_animatedHeight = kCollapsedHeight;
    float         m_targetHeight   = kCollapsedHeight;

    void updateAnimation() const {
        constexpr float kSpeed = 0.2f;
        m_animatedHeight += (m_targetHeight - m_animatedHeight) * kSpeed;
        if (std::abs(m_animatedHeight - m_targetHeight) < 0.5f)
            m_animatedHeight = m_targetHeight;
    }

    std::function<void(DeviceType, int)> m_onRemoveDevice;

    // Fingerprint cache for setDeviceChain
    midi::MidiEffectChain*   m_lastMidiChain = nullptr;
    instruments::Instrument* m_lastInst      = nullptr;
    effects::EffectChain*    m_lastFxChain   = nullptr;
    int m_lastMidiCount = 0;
    int m_lastFxCount   = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
