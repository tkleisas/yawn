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
#include "ui/framework/InstrumentDisplayWidget.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/SnapScrollContainer.h"
#include "ui/framework/WaveformWidget.h"
#include "ui/Theme.h"
#include "audio/Clip.h"
#include "audio/TransientDetector.h"
#include "instruments/Instrument.h"
#include "instruments/Sampler.h"
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
    enum class ViewMode  { Devices, AudioClip };

    // Clip view layout constants
    static constexpr float kClipTitleRowH    = 32.0f;   // top pad + title text
    static constexpr float kClipWaveformH    = 80.0f;
    static constexpr float kClipPropsH       = 80.0f;   // horizontal control strip
    static constexpr float kClipSectionGap   = 4.0f;
    static constexpr float kClipLabelW       = 60.0f;
    static constexpr float kClipValueW       = 80.0f;

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

    ViewMode viewMode() const { return m_viewMode; }

    // Check if any clip-property knob or device knob is in text-edit mode
    bool hasEditingKnob() const {
        if (m_transposeKnob.isEditing() || m_detuneKnob.isEditing() || m_bpmKnob.isEditing())
            return true;
        for (auto* dw : m_deviceWidgets)
            if (dw->hasEditingKnob()) return true;
        return false;
    }

    // Forward key events to the editing knob
    bool forwardKeyDown(int key) {
        KeyEvent ke;
        ke.keyCode = key;
        if (m_transposeKnob.isEditing()) return m_transposeKnob.onKeyDown(ke);
        if (m_detuneKnob.isEditing()) return m_detuneKnob.onKeyDown(ke);
        if (m_bpmKnob.isEditing()) return m_bpmKnob.onKeyDown(ke);
        for (auto* dw : m_deviceWidgets)
            if (dw->hasEditingKnob()) return dw->forwardKeyDown(key);
        return false;
    }

    // Forward text input to the editing knob
    bool forwardTextInput(const char* text) {
        TextInputEvent te;
        std::strncpy(te.text, text, sizeof(te.text) - 1);
        te.text[sizeof(te.text) - 1] = '\0';
        if (m_transposeKnob.isEditing()) return m_transposeKnob.onTextInput(te);
        if (m_detuneKnob.isEditing()) return m_detuneKnob.onTextInput(te);
        if (m_bpmKnob.isEditing()) return m_bpmKnob.onTextInput(te);
        for (auto* dw : m_deviceWidgets)
            if (dw->hasEditingKnob()) return dw->forwardTextInput(text);
        return false;
    }

    // Cancel any knob in text-edit mode (e.g., when clicking outside)
    void cancelEditingKnobs() {
        if (m_transposeKnob.isEditing()) { KeyEvent ke; ke.keyCode = 27; m_transposeKnob.onKeyDown(ke); }
        if (m_detuneKnob.isEditing()) { KeyEvent ke; ke.keyCode = 27; m_detuneKnob.onKeyDown(ke); }
        if (m_bpmKnob.isEditing()) { KeyEvent ke; ke.keyCode = 27; m_bpmKnob.onKeyDown(ke); }
        for (auto* dw : m_deviceWidgets) dw->cancelEditingKnobs();
    }

    // Show audio clip view: waveform + properties + effect chain
    void setAudioClip(const audio::Clip* clip, effects::EffectChain* fxChain,
                      int sampleRate = 44100) {
        if (clip == m_clipPtr && fxChain == m_lastFxChain &&
            (fxChain ? fxChain->count() : 0) == m_lastFxCount)
            return;

        m_viewMode = ViewMode::AudioClip;
        m_clipPtr = clip;
        m_clipSampleRate = sampleRate;
        m_waveformWidget.setClip(clip);
        m_waveformWidget.setSampleRate(sampleRate);

        // Setup warp mode dropdown
        m_warpModeDropdown.setItems({"Off", "Auto", "Beats", "Tones", "Texture", "Repitch"});
        m_warpModeDropdown.setSelected(static_cast<int>(clip->warpMode));
        m_warpModeDropdown.setOnChange([this](int idx) {
            if (m_clipPtr) {
                auto* mutableClip = const_cast<audio::Clip*>(m_clipPtr);
                mutableClip->warpMode = static_cast<audio::WarpMode>(idx);
            }
        });

        // Setup detect button
        m_detectBtn.setLabel("Detect");
        m_detectBtn.setOnClick([this]() {
            if (!m_clipPtr || !m_clipPtr->buffer) return;
            auto* mutableClip = const_cast<audio::Clip*>(m_clipPtr);
            mutableClip->transients = audio::TransientDetector::detect(*m_clipPtr->buffer);
            if (mutableClip->originalBPM <= 0.0 && !mutableClip->transients.empty())
                mutableClip->originalBPM = audio::TransientDetector::estimateBPM(
                    mutableClip->transients, static_cast<double>(m_clipSampleRate));
        });

        // Gain knob: 0 to ~+6dB, stored as linear gain in clip
        m_gainKnob.setRange(0.0f, 2.0f);
        m_gainKnob.setDefault(1.0f);
        m_gainKnob.setValue(clip->gain);
        m_gainKnob.setLabel("");  // external label drawn in paintAudioClipView
        m_gainKnob.setFormatCallback([](float v) -> std::string {
            if (v < 0.001f) return "-inf dB";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f dB", 20.0f * std::log10(v));
            return buf;
        });
        m_gainKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->gain = v;
        });

        // Transpose (semitones) — knob with integer step
        m_transposeKnob.setRange(-48.0f, 48.0f);
        m_transposeKnob.setDefault(0.0f);
        m_transposeKnob.setValue(static_cast<float>(clip->transposeSemitones));
        m_transposeKnob.setStep(1.0f);
        m_transposeKnob.setSensitivity(0.5f);
        m_transposeKnob.setLabel("");
        m_transposeKnob.setFormatCallback([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%+d st", static_cast<int>(v));
            return buf;
        });
        m_transposeKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->transposeSemitones = static_cast<int>(v);
        });

        // Detune (cents) — knob with integer step
        m_detuneKnob.setRange(-50.0f, 50.0f);
        m_detuneKnob.setDefault(0.0f);
        m_detuneKnob.setValue(static_cast<float>(clip->detuneCents));
        m_detuneKnob.setStep(1.0f);
        m_detuneKnob.setSensitivity(0.3f);
        m_detuneKnob.setLabel("");
        m_detuneKnob.setFormatCallback([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%+d ct", static_cast<int>(v));
            return buf;
        });
        m_detuneKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->detuneCents = static_cast<int>(v);
        });

        // BPM — knob with continuous value
        m_bpmKnob.setRange(20.0f, 999.0f);
        m_bpmKnob.setDefault(120.0f);
        m_bpmKnob.setValue(clip->originalBPM > 0 ? static_cast<float>(clip->originalBPM) : 120.0f);
        m_bpmKnob.setSensitivity(0.5f);
        m_bpmKnob.setLabel("");
        m_bpmKnob.setFormatCallback([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            return buf;
        });
        m_bpmKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->originalBPM = static_cast<double>(v);
        });

        // Loop toggle button
        m_loopToggleBtn.setLabel(clip->looping ? "On" : "Off");
        m_loopToggleBtn.setOnClick([this]() {
            if (m_clipPtr) {
                auto* mc = const_cast<audio::Clip*>(m_clipPtr);
                mc->looping = !mc->looping;
                m_loopToggleBtn.setLabel(mc->looping ? "On" : "Off");
            }
        });

        saveExpandedStates();
        m_scroll.removeAllChildren();
        for (auto* dw : m_deviceWidgets) delete dw;
        m_deviceWidgets.clear();
        m_deviceRefs.clear();

        m_lastMidiChain = nullptr;
        m_lastInst      = nullptr;
        m_lastFxChain   = fxChain;
        m_lastMidiCount = 0;
        m_lastFxCount   = fxChain ? fxChain->count() : 0;

        std::vector<float> snapPoints;
        float xPos = 0;

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
        setOpen(true);
    }

    void clearClipView() {
        m_viewMode = ViewMode::Devices;
        m_clipPtr = nullptr;
        m_waveformWidget.setClip(nullptr);
    }

    // Forward playback state to the waveform widget
    void setClipPlayPosition(int64_t pos) { m_waveformWidget.setPlayPosition(pos); }
    void setClipPlaying(bool playing)     { m_waveformWidget.setPlaying(playing); }

    WaveformWidget&       waveformWidget()       { return m_waveformWidget; }
    const WaveformWidget& waveformWidget() const { return m_waveformWidget; }

    // Rebuild the device chain from current track state.
    void setDeviceChain(midi::MidiEffectChain* midiChain,
                        instruments::Instrument* inst,
                        effects::EffectChain* fxChain) {
        int midiCount = midiChain ? midiChain->count() : 0;
        int fxCount   = fxChain   ? fxChain->count()   : 0;

        if (m_viewMode == ViewMode::Devices &&
            midiChain == m_lastMidiChain && inst == m_lastInst &&
            fxChain == m_lastFxChain &&
            midiCount == m_lastMidiCount && fxCount == m_lastFxCount)
            return;

        m_viewMode = ViewMode::Devices;
        m_clipPtr  = nullptr;

        saveExpandedStates();

        m_scroll.removeAllChildren();
        for (auto* dw : m_deviceWidgets) delete dw;
        m_deviceWidgets.clear();
        m_deviceRefs.clear();
        m_displayUpdaters.clear();

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
            if (!setupInstrumentDisplay(dw, inst, ref))
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
        m_displayUpdaters.clear();
        m_lastMidiChain = nullptr;
        m_lastInst      = nullptr;
        m_lastFxChain   = nullptr;
        m_lastMidiCount = 0;
        m_lastFxCount   = 0;
        m_viewMode = ViewMode::Devices;
        m_clipPtr  = nullptr;
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

    void handleScroll(float dx, float dy, bool ctrl = false) {
        if (m_viewMode == ViewMode::AudioClip) {
            auto& wb = m_waveformWidget.bounds();
            if (m_lastMouseX >= wb.x && m_lastMouseX < wb.x + wb.w &&
                m_lastMouseY >= wb.y && m_lastMouseY < wb.y + wb.h) {
                ScrollEvent se;
                se.x = m_lastMouseX; se.y = m_lastMouseY;
                se.lx = m_lastMouseX - wb.x;
                se.ly = m_lastMouseY - wb.y;
                se.dx = dx; se.dy = dy;
                se.mods.ctrl = ctrl;
                m_waveformWidget.onScroll(se);
                return;
            }
        }
        if (std::abs(dx) > 0.01f)
            m_scroll.handleScroll(dx, dy);
        else
            m_scroll.handleScroll(-dy, 0);
    }

    void setLastMousePos(float mx, float my) { m_lastMouseX = mx; m_lastMouseY = my; }

#ifdef YAWN_TEST_BUILD
    bool handleRightClick(float, float) { return false; }
#else
    bool handleRightClick(float mx, float my);
#endif

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        updateAnimation();
        return c.constrain({c.maxW, m_animatedHeight});
    }

#ifdef YAWN_TEST_BUILD
    void layout(const Rect& bounds, const UIContext&) override { m_bounds = bounds; }
#else
    void layout(const Rect& bounds, const UIContext& ctx) override;
#endif

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    // ─── Events ─────────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return false; }
#else
    bool onMouseDown(MouseEvent& e) override;
#endif

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
        // Forward to waveform widget in audio clip view
        if (m_viewMode == ViewMode::AudioClip) {
            // When dropdown is open, forward moves for hover highlighting
            if (m_warpModeDropdown.isOpen()) {
                m_warpModeDropdown.onMouseMove(e);
                return true;
            }
            // Forward to draggable number inputs (they don't capture the mouse)
            if (m_transposeKnob.onMouseMove(e)) return true;
            if (m_detuneKnob.onMouseMove(e)) return true;
            if (m_bpmKnob.onMouseMove(e)) return true;
            auto& wb = m_waveformWidget.bounds();
            e.lx = e.x - wb.x;
            e.ly = e.y - wb.y;
            if (m_waveformWidget.onMouseMove(e)) return true;
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
        // Forward to waveform widget
        if (m_viewMode == ViewMode::AudioClip) {
            if (m_warpModeDropdown.isOpen())
                return m_warpModeDropdown.onMouseUp(e);
            auto& wb = m_waveformWidget.bounds();
            e.lx = e.x - wb.x;
            e.ly = e.y - wb.y;
            if (m_waveformWidget.onMouseUp(e)) return true;
            if (hitWidget(m_detectBtn, e.x, e.y))
                return m_detectBtn.onMouseUp(e);
            if (hitWidget(m_warpModeDropdown, e.x, e.y))
                return m_warpModeDropdown.onMouseUp(e);
            if (hitWidget(m_gainKnob, e.x, e.y))
                return m_gainKnob.onMouseUp(e);
            // Number inputs: always forward mouseUp (drag may leave bounds)
            if (m_transposeKnob.onMouseUp(e)) return true;
            if (m_detuneKnob.onMouseUp(e)) return true;
            if (m_bpmKnob.onMouseUp(e)) return true;
            if (hitWidget(m_loopToggleBtn, e.x, e.y))
                return m_loopToggleBtn.onMouseUp(e);
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

    bool onScroll(ScrollEvent& e) override {
        if (!m_open) return false;
        if (m_viewMode == ViewMode::AudioClip) {
            auto& wb = m_waveformWidget.bounds();
            if (e.x >= wb.x && e.x < wb.x + wb.w &&
                e.y >= wb.y && e.y < wb.y + wb.h) {
                e.lx = e.x - wb.x;
                e.ly = e.y - wb.y;
                return m_waveformWidget.onScroll(e);
            }
        }
        return false;
    }

private:
    bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
    }

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

        for (int p = 0; p < count; ++p)
            dw->updateParamValue(p, ref.getParam(p));

        dw->setOnParamChange([ref](int idx, float v) {
            DeviceRef r = ref;
            r.setParam(idx, v);
        });

        dw->setOnBypassToggle([ref](bool) {
            DeviceRef r = ref;
            r.toggleBypass();
        });

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
        for (auto& upd : m_displayUpdaters)
            if (upd) upd();
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

    // ── Create instrument-specific grouped layout (returns true if handled) ──
    bool setupInstrumentDisplay(DeviceWidget* dw, instruments::Instrument* inst,
                                const DeviceRef& ref) {
        if (!inst) return false;
        std::string nm = inst->name();

        GroupedKnobBody::Config config;

        if (nm == "FM Synth") {
            auto* algoW = new FMAlgorithmWidget();
            config.display = algoW;
            config.displayWidth = 130;
            config.sections = {
                {"",     {0, 1, 18}},         // Algorithm, Feedback, Volume
                {"Op 1", {2, 3, 4, 5}},       // Level, Ratio, Attack, Release
                {"Op 2", {6, 7, 8, 9}},
                {"Op 3", {10, 11, 12, 13}},
                {"Op 4", {14, 15, 16, 17}},
            };
            m_displayUpdaters.push_back([algoW, inst]() {
                algoW->setAlgorithm(static_cast<int>(inst->getParameter(0)));
                algoW->setFeedback(inst->getParameter(1));
                algoW->setOpLevels(inst->getParameter(2), inst->getParameter(6),
                                   inst->getParameter(10), inst->getParameter(14));
            });
        } else if (nm == "Subtractive Synth") {
            auto* panel = new SubSynthDisplayPanel();
            config.display = panel;
            config.displayWidth = 150;
            config.sections = {
                {"Osc",      {0, 1, 2, 3, 4, 5, 6, 7}},
                {"Filter",   {8, 9, 10, 11}},
                {"Amp",      {12, 13, 14, 15}},
                {"Filt Env", {16, 17, 18, 19}},
                {"LFO",      {20, 21, 22}},
            };
            m_displayUpdaters.push_back([panel, inst]() {
                panel->updateFromParams(
                    inst->getParameter(0),  inst->getParameter(1),
                    inst->getParameter(2),  inst->getParameter(3),
                    inst->getParameter(8),  inst->getParameter(9),
                    inst->getParameter(10),
                    inst->getParameter(12), inst->getParameter(13),
                    inst->getParameter(14), inst->getParameter(15),
                    inst->getParameter(16), inst->getParameter(17),
                    inst->getParameter(18), inst->getParameter(19));
            });
        } else if (nm == "Sampler") {
            auto* samplerPanel = new SamplerDisplayPanel();
            config.display = samplerPanel;
            config.displayWidth = 130;
            config.sections = {
                {"Sample", {0, 10, 11}},
                {"Loop",   {8, 9}},
                {"Amp",    {1, 2, 3, 4}},
                {"Filter", {5, 6}},
                {"",       {7}},
            };
            m_displayUpdaters.push_back([samplerPanel, inst]() {
                samplerPanel->setADSR(inst->getParameter(1), inst->getParameter(2),
                                      inst->getParameter(3), inst->getParameter(4));
                samplerPanel->setLoopPoints(inst->getParameter(8), inst->getParameter(9));
                samplerPanel->setReverse(inst->getParameter(10) > 0.5f);
                auto* sampler = dynamic_cast<instruments::Sampler*>(inst);
                if (sampler) {
                    if (sampler->hasSample())
                        samplerPanel->setSampleData(sampler->sampleData(),
                                                    sampler->sampleFrames(),
                                                    sampler->sampleChannels());
                    samplerPanel->setPlayhead(sampler->playheadPosition(),
                                              sampler->isPlaying());
                }
            });
        } else {
            return false;
        }

        // Build param descriptors
        int count = inst->parameterCount();
        std::vector<GroupedKnobBody::ParamDesc> params(count);
        for (int p = 0; p < count; ++p) {
            auto& info = inst->parameterInfo(p);
            params[p] = {p, info.name, info.unit ? info.unit : "",
                         info.minValue, info.maxValue, info.defaultValue, info.isBoolean};
        }

        auto* body = new GroupedKnobBody();
        body->configure(config, params);
        body->setOnParamChange([ref](int idx, float v) {
            DeviceRef r = ref; r.setParam(idx, v);
        });
        dw->setCustomBody(body);

        // Set initial values
        for (int p = 0; p < count; ++p)
            body->updateParamValue(p, ref.getParam(p));

        // Bypass / remove callbacks
        dw->setOnBypassToggle([ref](bool) { DeviceRef r = ref; r.toggleBypass(); });
        dw->setOnRemove([this, type = ref.type, chainIdx = ref.chainIndex]() {
            if (m_onRemoveDevice) m_onRemoveDevice(type, chainIdx);
        });
        return true;
    }

    // ── Right-click knob reset (matches old geometry-based hit-test) ──
#ifdef YAWN_TEST_BUILD
    bool handleKnobRightClick(size_t, float, float) { return false; }
#else
    bool handleKnobRightClick(size_t deviceIdx, float mx, float my);
#endif

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
    std::vector<std::function<void()>> m_displayUpdaters;  // instrument display updaters

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

    // Audio clip view state
    ViewMode m_viewMode = ViewMode::Devices;
    const audio::Clip* m_clipPtr = nullptr;
    int m_clipSampleRate = 44100;
    WaveformWidget m_waveformWidget;
    FwDropDown m_warpModeDropdown;
    FwButton m_detectBtn;
    FwKnob m_gainKnob;
    FwKnob m_transposeKnob;
    FwKnob m_detuneKnob;
    FwButton m_loopToggleBtn;
    FwKnob m_bpmKnob;
    float m_lastMouseX = 0, m_lastMouseY = 0;

    // ── Audio clip view rendering ──
#ifdef YAWN_TEST_BUILD
    void paintAudioClipView(Renderer2D&, Font&, float, float, float, float, float, UIContext&) {}
#else
    void paintAudioClipView(Renderer2D& renderer, Font& font,
                            float x, float bodyY, float w, float bodyH,
                            float hScale, UIContext& ctx);
#endif
};

} // namespace fw
} // namespace ui
} // namespace yawn
