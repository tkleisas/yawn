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
#include "audio/FollowAction.h"
#include "audio/TransientDetector.h"
#include "instruments/Instrument.h"
#include "instruments/Sampler.h"
#include "instruments/DrumSlop.h"
#include "instruments/DrumRack.h"
#include "instruments/InstrumentRack.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffect.h"
#include "midi/MidiEffectChain.h"
#include "midi/LFO.h"
#include "midi/MidiMapping.h"
#include "automation/AutomationLane.h"
#ifndef YAWN_TEST_BUILD
#include "ui/ContextMenu.h"
#endif
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
    static constexpr float kAutoSectionH     = 90.0f;   // automation target picker + envelope

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
    void setOnMoveDevice(std::function<void(DeviceType, int, int)> cb) {
        m_onMoveDevice = std::move(cb);
    }

    // Preset click: (DeviceType, chainIndex, screenX, screenY)
    using PresetClickCallback = std::function<void(DeviceType, int, float, float)>;
    void setOnPresetClick(PresetClickCallback cb) { m_onPresetClick = std::move(cb); }

    // Update preset name shown on a device header
    void setDevicePresetName(DeviceType type, int chainIndex, const std::string& name) {
        for (size_t i = 0; i < m_deviceRefs.size(); ++i) {
            auto& ref = m_deviceRefs[i];
            if (ref.type == type && ref.chainIndex == chainIndex) {
                m_deviceWidgets[i]->setPresetName(name);
                return;
            }
        }
    }

    // Callback fired when a knob drag begins/ends (for automation recording).
    // Args: trackIndex, targetType, chainIndex, paramIndex, value, touching
    using ParamTouchCallback = std::function<void(int, uint8_t, int, int, float, bool)>;
    void setOnParamTouch(ParamTouchCallback cb) { m_onParamTouch = std::move(cb); }

    void setTrackIndex(int idx) { m_autoTrackIndex = idx; }

    void setLearnManager(midi::MidiLearnManager* lm) { m_learnManager = lm; }

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

        // Set envelope editor time range from clip length
        if (clip->buffer && sampleRate > 0) {
            double clipBeats = static_cast<double>(clip->lengthInFrames()) / sampleRate * 2.0; // rough 120BPM
            if (clip->originalBPM > 0)
                clipBeats = static_cast<double>(clip->lengthInFrames()) / sampleRate * clip->originalBPM / 60.0;
            m_autoEnvelopeWidget.setTimeRange(0.0, std::max(1.0, clipBeats));
        } else {
            m_autoEnvelopeWidget.setTimeRange(0.0, 4.0);
        }
        m_autoEnvelopeWidget.setValueRange(0.0f, 1.0f);

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
        m_followActionPtr = nullptr;
        m_waveformWidget.setClip(nullptr);
    }

    // Set follow action pointer for the current clip slot
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

    // Forward playback state to the waveform widget
    void setClipPlayPosition(int64_t pos) { m_waveformWidget.setPlayPosition(pos); }
    void setClipPlaying(bool playing)     { m_waveformWidget.setPlaying(playing); }

    // Set clip automation context for the envelope editor
    void setClipAutomation(std::vector<automation::AutomationLane>* lanes, int trackIndex) {
        // Skip rebuild if nothing changed — prevents resetting dropdown selection
        if (lanes == m_clipAutoLanes && trackIndex == m_autoTrackIndex)
            return;

        m_clipAutoLanes = lanes;
        m_autoTrackIndex = trackIndex;

        buildAutoTargetList();
        m_autoTargetDropdown.setOnChange([this](int idx) {
            if (idx <= 0) {
                m_autoSelectedLaneIdx = -1;
                m_autoEnvelopeWidget.setPoints({});
                return;
            }
            auto tgt = targetFromDropdownIndex(idx);
            int laneIdx = findAutoLaneForTarget(tgt);
            if (laneIdx < 0 && m_clipAutoLanes) {
                // Create a new lane
                m_clipAutoLanes->push_back({tgt, {}, false});
                laneIdx = static_cast<int>(m_clipAutoLanes->size()) - 1;
            }
            m_autoSelectedLaneIdx = laneIdx;
            syncEnvelopeFromLane();
        });

        m_autoEnvelopeWidget.setOnPointAdd([this](double time, float value) {
            if (!m_clipAutoLanes || m_autoSelectedLaneIdx < 0 ||
                m_autoSelectedLaneIdx >= static_cast<int>(m_clipAutoLanes->size())) return;
            (*m_clipAutoLanes)[m_autoSelectedLaneIdx].envelope.addPoint(time, value);
            syncEnvelopeFromLane();
        });
        m_autoEnvelopeWidget.setOnPointMove([this](int idx, double time, float value) {
            if (!m_clipAutoLanes || m_autoSelectedLaneIdx < 0 ||
                m_autoSelectedLaneIdx >= static_cast<int>(m_clipAutoLanes->size())) return;
            (*m_clipAutoLanes)[m_autoSelectedLaneIdx].envelope.movePoint(idx, time, value);
            syncEnvelopeFromLane();
        });
        m_autoEnvelopeWidget.setOnPointRemove([this](int idx) {
            if (!m_clipAutoLanes || m_autoSelectedLaneIdx < 0 ||
                m_autoSelectedLaneIdx >= static_cast<int>(m_clipAutoLanes->size())) return;
            (*m_clipAutoLanes)[m_autoSelectedLaneIdx].envelope.removePoint(idx);
            syncEnvelopeFromLane();
        });
    }

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
                if (!setupMidiEffectDisplay(dw, fx, ref))
                    configureDeviceWidget(dw, ref);
                wireHeaderCallbacks(dw, ref);

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
            wireHeaderCallbacks(dw, ref);

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
        m_followActionPtr = nullptr;
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

    // MIDI Learn context menu for device parameters
#ifdef YAWN_TEST_BUILD
    void handleDeviceContextMenuMouseMove(float, float) {}
#else
    void handleDeviceContextMenuMouseMove(float mx, float my) {
        if (m_deviceContextMenu.isOpen())
            m_deviceContextMenu.handleMouseMove(mx, my);
    }
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
        // Handle drag-to-reorder (constrained to same device type)
        if (m_dragReorderActive && m_dragSourceIdx >= 0 &&
            m_dragSourceIdx < static_cast<int>(m_deviceRefs.size())) {
            auto srcType = m_deviceRefs[m_dragSourceIdx].type;
            // Find range of same-type devices
            int groupFirst = -1, groupLast = -1;
            for (size_t i = 0; i < m_deviceRefs.size(); ++i) {
                if (m_deviceRefs[i].type == srcType) {
                    if (groupFirst < 0) groupFirst = static_cast<int>(i);
                    groupLast = static_cast<int>(i);
                }
            }
            if (groupFirst < 0) { m_dragInsertIdx = m_dragSourceIdx; return true; }
            // Find nearest gap within [groupFirst .. groupLast+1]
            int best = groupFirst;
            float bestDist = 1e9f;
            for (int i = groupFirst; i <= groupLast + 1; ++i) {
                float edgeX;
                if (i < static_cast<int>(m_deviceWidgets.size()))
                    edgeX = m_deviceWidgets[i]->bounds().x;
                else {
                    auto& last = m_deviceWidgets.back()->bounds();
                    edgeX = last.x + last.w;
                }
                float dist = std::abs(e.x - edgeX);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = i;
                }
            }
            m_dragInsertIdx = best;
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
            if (m_autoTargetDropdown.isOpen()) {
                m_autoTargetDropdown.onMouseMove(e);
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
        // Handle drag-to-reorder drop
        if (m_dragReorderActive) {
            m_dragReorderActive = false;
            releaseMouse();
            int src = m_dragSourceIdx;
            int ins = m_dragInsertIdx;
            m_dragSourceIdx = -1;
            m_dragInsertIdx = -1;
            // Compute target chain index from widget-level insertion gap.
            // Count same-type devices before insertion point (excluding source).
            if (src >= 0 && ins >= 0 && src < static_cast<int>(m_deviceRefs.size())) {
                auto srcType = m_deviceRefs[src].type;
                int fromChain = m_deviceRefs[src].chainIndex;
                int targetChain = 0;
                for (int i = 0; i < ins; ++i) {
                    if (i != src && i < static_cast<int>(m_deviceRefs.size()) &&
                        m_deviceRefs[i].type == srcType)
                        ++targetChain;
                }
                if (targetChain != fromChain && m_onMoveDevice)
                    m_onMoveDevice(srcType, fromChain, targetChain);
            }
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

    // ── Wire header-only callbacks (preset) that setup*Display may skip ──
    void wireHeaderCallbacks(DeviceWidget* dw, const DeviceRef& ref) {
        dw->setOnPresetClick([this, type = ref.type, chainIdx = ref.chainIndex](float x, float y) {
            if (m_onPresetClick) m_onPresetClick(type, chainIdx, x, y);
        });
    }

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
                pi.widgetHint = info.widgetHint;
                if (info.valueLabels && info.valueLabelCount > 0)
                    pi.valueLabels.assign(info.valueLabels, info.valueLabels + info.valueLabelCount);
            } else if (ref.instrument) {
                auto& info = ref.instrument->parameterInfo(p);
                pi.name = info.name; pi.unit = info.unit;
                pi.minVal = info.minValue; pi.maxVal = info.maxValue;
                pi.defaultVal = info.defaultValue; pi.isBoolean = info.isBoolean;
                pi.widgetHint = info.widgetHint;
                if (info.valueLabels && info.valueLabelCount > 0)
                    pi.valueLabels.assign(info.valueLabels, info.valueLabels + info.valueLabelCount);
            } else if (ref.audioEffect) {
                auto& info = ref.audioEffect->parameterInfo(p);
                pi.name = info.name; pi.unit = info.unit;
                pi.minVal = info.minValue; pi.maxVal = info.maxValue;
                pi.defaultVal = info.defaultValue; pi.isBoolean = info.isBoolean;
                pi.widgetHint = info.widgetHint;
                if (info.valueLabels && info.valueLabelCount > 0)
                    pi.valueLabels.assign(info.valueLabels, info.valueLabels + info.valueLabelCount);
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

        // Automation recording: forward knob touch begin/end
        {
            uint8_t tt = 0;
            if (ref.type == DeviceType::Instrument)
                tt = static_cast<uint8_t>(automation::TargetType::Instrument);
            else if (ref.type == DeviceType::AudioFx)
                tt = static_cast<uint8_t>(automation::TargetType::AudioEffect);
            else
                tt = static_cast<uint8_t>(automation::TargetType::MidiEffect);
            int ci = ref.chainIndex;
            dw->setOnParamTouch([this, tt, ci](int idx, float v, bool touching) {
                if (m_onParamTouch && m_autoTrackIndex >= 0)
                    m_onParamTouch(m_autoTrackIndex, tt, ci, idx, v, touching);
            });
        }

        dw->setOnBypassToggle([ref](bool) {
            DeviceRef r = ref;
            r.toggleBypass();
        });

        dw->setOnRemove([this, type = ref.type, chainIdx = ref.chainIndex]() {
            if (m_onRemoveDevice) m_onRemoveDevice(type, chainIdx);
        });

        // Wire preset click
        dw->setOnPresetClick([this, type = ref.type, chainIdx = ref.chainIndex](float x, float y) {
            if (m_onPresetClick) m_onPresetClick(type, chainIdx, x, y);
        });

        // Wire drag-to-reorder: find this device's index in m_deviceWidgets
        dw->setOnDragStart([this, dw]() {
            for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
                if (m_deviceWidgets[i] == dw) {
                    m_dragReorderActive = true;
                    m_dragSourceIdx = static_cast<int>(i);
                    m_dragInsertIdx = static_cast<int>(i);
                    m_dragStartX = m_lastMouseX;
                    captureMouse();
                    return;
                }
            }
        });

        // CC label lookup for MIDI Learn visual feedback
        if (m_learnManager) {
            automation::TargetType tt;
            if (ref.type == DeviceType::Instrument)
                tt = automation::TargetType::Instrument;
            else if (ref.type == DeviceType::AudioFx)
                tt = automation::TargetType::AudioEffect;
            else
                tt = automation::TargetType::MidiEffect;
            int ci = ref.chainIndex;
            dw->setCCLabelCallback([this, tt, ci](int paramIdx) -> std::string {
                if (!m_learnManager || m_autoTrackIndex < 0) return {};
                automation::AutomationTarget t;
                t.type = tt;
                t.trackIndex = m_autoTrackIndex;
                t.chainIndex = ci;
                t.paramIndex = paramIdx;
                auto* m = m_learnManager->findByTarget(t);
                return m ? m->label() : std::string{};
            });
        }
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
        } else if (nm == "DrumSlop") {
            auto* dsPanel = new DrumSlopDisplayPanel();
            config.display = dsPanel;
            config.displayWidth = 160;
            config.sections = {
                {"Global", {0, 1, 2, 3, 4, 5}},
                {"Pad", {6, 7, 8, 9, 10, 11}},
                {"Pad Env", {12, 13, 14, 15}},
            };

            // Pad click callback to select pad
            dsPanel->setOnPadClick([inst](int padIdx) {
                auto* ds = dynamic_cast<instruments::DrumSlop*>(inst);
                if (ds) ds->setSelectedPad(padIdx);
            });

            m_displayUpdaters.push_back([dsPanel, inst]() {
                auto* ds = dynamic_cast<instruments::DrumSlop*>(inst);
                if (!ds) return;

                dsPanel->setSliceCount(ds->sliceCount());
                dsPanel->setBaseNote(static_cast<int>(ds->getParameter(
                    instruments::DrumSlop::kBaseNote)));
                dsPanel->setSelectedPad(ds->selectedPad());

                if (ds->hasLoop()) {
                    dsPanel->setLoopData(ds->loopData(), ds->loopFrames(),
                                         ds->loopChannels());
                    // Build slice boundaries for display
                    std::vector<int64_t> bounds;
                    int sc = ds->sliceCount();
                    for (int i = 0; i <= sc; ++i)
                        bounds.push_back(ds->sliceBoundary(i));
                    dsPanel->setSliceBoundaries(bounds);
                }

                for (int i = 0; i < instruments::DrumSlop::kNumPads; ++i)
                    dsPanel->setPadPlaying(i, ds->isPadPlaying(i));
            });
        } else if (nm == "Wavetable Synth") {
            auto* wtPanel = new WavetableDisplayPanel();
            config.display = wtPanel;
            config.displayWidth = 130;
            config.sections = {
                {"Osc",      {0, 1, 14, 15, 16}},
                {"Amp",      {5, 6, 7, 8}},
                {"Filter",   {2, 3, 4}},
                {"Filt Env", {9, 10, 11, 12}},
                {"",         {13, 17}},
            };
            static const char* tableNames[] = {
                "Basic", "PWM", "Formant", "Harmonic", "Digital"
            };
            m_displayUpdaters.push_back([wtPanel, inst]() {
                auto* wt = dynamic_cast<instruments::WavetableSynth*>(inst);
                if (!wt) return;
                int table = wt->currentTable();
                float pos = wt->getParameter(instruments::WavetableSynth::kPosition);
                int numFrames = wt->frameCount(table);
                if (numFrames > 0) {
                    // Compute morphed waveform at current position
                    static float morphBuf[instruments::WavetableSynth::kFrameSize];
                    float framePos = pos * (numFrames - 1);
                    int f0 = static_cast<int>(framePos);
                    int f1 = std::min(f0 + 1, numFrames - 1);
                    float frac = framePos - f0;
                    const float* d0 = wt->frameData(table, f0);
                    const float* d1 = wt->frameData(table, f1);
                    if (d0 && d1) {
                        for (int s = 0; s < instruments::WavetableSynth::kFrameSize; ++s)
                            morphBuf[s] = d0[s] * (1.0f - frac) + d1[s] * frac;
                        wtPanel->setWaveformData(morphBuf,
                                                 instruments::WavetableSynth::kFrameSize);
                    }
                }
                wtPanel->setPosition(pos);
                if (table >= 0 && table < 5)
                    wtPanel->setTableName(tableNames[table]);
            });
        } else if (nm == "Granular Synth") {
            auto* grPanel = new GranularDisplayPanel();
            config.display = grPanel;
            config.displayWidth = 130;
            config.sections = {
                {"Grain",    {0, 1, 2, 3}},
                {"Pitch",    {4, 5, 14}},
                {"Shape",    {6, 7, 12, 13}},
                {"Filter",   {8, 9}},
                {"Env",      {10, 11}},
                {"",         {15, 16}},
            };
            m_displayUpdaters.push_back([grPanel, inst]() {
                auto* gr = dynamic_cast<instruments::GranularSynth*>(inst);
                if (!gr) return;
                if (gr->hasSample())
                    grPanel->setSampleData(gr->sampleData(), gr->sampleFrames(),
                                           gr->sampleChannels());
                grPanel->setPosition(gr->currentPosition());
                grPanel->setScanPosition(gr->scanPosition());
                grPanel->setPlaying(gr->isPlaying());
                // Normalize grain size for display (100ms / total length)
                float grainMs = gr->getParameter(instruments::GranularSynth::kGrainSize);
                float totalMs = gr->sampleFrames() > 0
                    ? 1000.0f * gr->sampleFrames() / 44100.0f : 1.0f;
                grPanel->setGrainSize(grainMs / totalMs);
            });
        } else if (nm == "Vocoder") {
            auto* vocPanel = new VocoderDisplayPanel();
            config.display = vocPanel;
            config.displayWidth = 130;
            config.sections = {
                {"Source",   {1, 2}},
                {"Bands",    {0, 3, 6}},
                {"Envelope", {4, 5}},
                {"Mix",      {7, 8, 9}},
                {"Amp",      {10, 11}},
                {"",         {12, 13}},
            };
            m_displayUpdaters.push_back([vocPanel, inst]() {
                auto* voc = dynamic_cast<instruments::Vocoder*>(inst);
                if (!voc) return;
                if (voc->hasModulatorSample())
                    vocPanel->setModulatorData(voc->modulatorData(),
                                               voc->modulatorFrames());
                vocPanel->setPlayhead(voc->modulatorPlayhead());
                vocPanel->setPlaying(voc->isPlaying());
                vocPanel->setBandCount(
                    static_cast<int>(voc->getParameter(instruments::Vocoder::kBands)));
            });
        } else if (nm == "Drum Rack") {
            auto* drPanel = new DrumRackDisplayPanel();
            config.display = drPanel;
            config.displayWidth = 160;
            config.sections = {
                {"Global", {0}},
                {"Pad",    {1, 2, 3}},
            };

            drPanel->setOnPadClick([inst](int note) {
                auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
                if (dr) dr->setSelectedPad(note);
            });

            m_displayUpdaters.push_back([drPanel, inst]() {
                auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
                if (!dr) return;

                int sel = dr->selectedPad();
                drPanel->setSelectedPad(sel);

                // Update which page the selected pad is on
                drPanel->setPage(sel / 16);

                // Update sample/playing state for all 128 pads
                for (int n = 0; n < instruments::DrumRack::kNumPads; ++n) {
                    drPanel->setPadHasSample(n, dr->hasSample(n));
                    drPanel->setPadPlaying(n, dr->isPadPlaying(n));
                }

                // Selected pad waveform
                const float* data = dr->padSampleData(sel);
                if (data)
                    drPanel->setSelectedPadWaveform(data, dr->padSampleFrames(sel),
                                                    dr->padSampleChannels(sel));
                else
                    drPanel->setSelectedPadWaveform(nullptr, 0, 1);
            });
        } else if (nm == "Instrument Rack") {
            auto* irPanel = new InstrumentRackDisplayPanel();
            config.display = irPanel;
            config.displayWidth = 160;
            config.sections = {
                {"Rack",  {0}},
                {"Chain", {1, 2, 3, 4, 5, 6}},
            };

            irPanel->setOnChainClick([inst](int idx) {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (ir) ir->setSelectedChain(idx);
            });

            irPanel->setOnAddChain([inst]() {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (ir && ir->chainCount() < instruments::InstrumentRack::kMaxChains)
                    ir->addChain(std::make_unique<instruments::SubtractiveSynth>());
            });

            irPanel->setOnRemoveChain([inst](int idx) {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (ir && idx < ir->chainCount())
                    ir->removeChain(idx);
            });

            irPanel->setOnToggleChain([inst](int idx) {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (ir && idx < ir->chainCount())
                    ir->chain(idx).enabled = !ir->chain(idx).enabled;
            });

            m_displayUpdaters.push_back([irPanel, inst]() {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (!ir) return;

                irPanel->setChainCount(ir->chainCount());
                irPanel->setSelectedChain(ir->selectedChain());

                for (int i = 0; i < ir->chainCount(); ++i) {
                    const auto& ch = ir->chain(i);
                    InstrumentRackDisplayPanel::ChainInfo ci;
                    ci.name    = ch.instrument ? ch.instrument->name() : "Empty";
                    ci.keyLow  = ch.keyLow;
                    ci.keyHigh = ch.keyHigh;
                    ci.velLow  = ch.velLow;
                    ci.velHigh = ch.velHigh;
                    ci.volume  = ch.volume;
                    ci.enabled = ch.enabled;
                    irPanel->setChain(i, ci);
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

    // ── Build custom display for MIDI effects (currently LFO) ──
    bool setupMidiEffectDisplay(DeviceWidget* dw, midi::MidiEffect* fx,
                                const DeviceRef& ref) {
        if (!fx) return false;
        std::string nm = fx->id();

        if (nm == "lfo") {
            auto* lfoDisp = new LFODisplayWidget();
            dw->setCustomPanel(lfoDisp, 52.0f, 200.0f);
            configureDeviceWidget(dw, ref);

            m_displayUpdaters.push_back([lfoDisp, fx]() {
                auto* lfo = static_cast<midi::LFO*>(fx);
                lfoDisp->setShape(static_cast<int>(lfo->getParameter(midi::LFO::kShape)));
                lfoDisp->setDepth(lfo->getParameter(midi::LFO::kDepth));
                lfoDisp->setPhaseOffset(lfo->getParameter(midi::LFO::kPhase));
                lfoDisp->setCurrentValue(lfo->currentValue());
                lfoDisp->setCurrentPhase(lfo->currentPhase());
                lfoDisp->setLinked(lfo->linkTargetId() != 0);
            });
            return true;
        }

        return false;
    }

    // ── Right-click knob reset (matches old geometry-based hit-test) ──
#ifdef YAWN_TEST_BUILD
    bool handleKnobRightClick(size_t, float, float) { return false; }
#else
    bool handleKnobRightClick(size_t deviceIdx, float mx, float my);
#endif

#ifndef YAWN_TEST_BUILD
    void openDeviceMidiLearnMenu(float mx, float my,
                                 const automation::AutomationTarget& target,
                                 float paramMin, float paramMax,
                                 std::function<void()> resetAction);
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
    std::function<void(DeviceType, int, int)> m_onMoveDevice;
    ParamTouchCallback m_onParamTouch;
    PresetClickCallback m_onPresetClick;

    // Drag-to-reorder state
    bool  m_dragReorderActive = false;
    int   m_dragSourceIdx     = -1;
    int   m_dragInsertIdx     = -1;
    float m_dragStartX        = 0;

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

    // Follow action widgets
    FollowAction* m_followActionPtr = nullptr;
    FwButton m_faEnableBtn;
    FwKnob m_faBarCountKnob;
    FwDropDown m_faActionADropdown;
    FwDropDown m_faActionBDropdown;
    FwKnob m_faChanceAKnob;

    // Automation lane editor
    FwDropDown m_autoTargetDropdown;
    AutomationEnvelopeWidget m_autoEnvelopeWidget;
    int m_autoSelectedLaneIdx = -1;
    std::vector<automation::AutomationLane>* m_clipAutoLanes = nullptr;
    int m_autoTrackIndex = -1;

    // MIDI Learn
    midi::MidiLearnManager* m_learnManager = nullptr;
#ifndef YAWN_TEST_BUILD
    ui::ContextMenu m_deviceContextMenu;
#endif

    void buildAutoTargetList() {
        std::vector<std::string> items;
        items.push_back("(none)");
        // Mixer params
        items.push_back("Volume");
        items.push_back("Pan");
        // Instrument params (from current device chain)
        if (m_lastInst) {
            int pc = m_lastInst->parameterCount();
            for (int i = 0; i < pc && i < 16; ++i) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Inst: %s", m_lastInst->parameterInfo(i).name);
                items.push_back(buf);
            }
        }
        // Audio effect params
        if (m_lastFxChain) {
            for (int c = 0; c < m_lastFxChain->count(); ++c) {
                auto* fx = m_lastFxChain->effectAt(c);
                if (!fx) continue;
                int pc = fx->parameterCount();
                for (int p = 0; p < pc && p < 8; ++p) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s: %s", fx->name(), fx->parameterInfo(p).name);
                    items.push_back(buf);
                }
            }
        }
        // Preserve current selection if the selected text still exists in the new list
        std::string prevText = m_autoTargetDropdown.selectedText();
        m_autoTargetDropdown.setItems(items);
        int restored = 0;
        if (!prevText.empty() && prevText != "(none)") {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i] == prevText) { restored = i; break; }
            }
        }
        m_autoTargetDropdown.setSelected(restored);
    }

    automation::AutomationTarget targetFromDropdownIndex(int idx) const {
        if (idx <= 0) return {};
        idx -= 1; // skip "(none)"
        // Mixer: Volume=0, Pan=1
        if (idx == 0) return automation::AutomationTarget::mixer(m_autoTrackIndex, automation::MixerParam::Volume);
        if (idx == 1) return automation::AutomationTarget::mixer(m_autoTrackIndex, automation::MixerParam::Pan);
        idx -= 2;
        // Instrument params
        if (m_lastInst) {
            int pc = std::min(m_lastInst->parameterCount(), 16);
            if (idx < pc) return automation::AutomationTarget::instrument(m_autoTrackIndex, idx);
            idx -= pc;
        }
        // Audio effect params
        if (m_lastFxChain) {
            for (int c = 0; c < m_lastFxChain->count(); ++c) {
                auto* fx = m_lastFxChain->effectAt(c);
                if (!fx) continue;
                int pc = std::min(fx->parameterCount(), 8);
                if (idx < pc) return automation::AutomationTarget::audioEffect(m_autoTrackIndex, c, idx);
                idx -= pc;
            }
        }
        return {};
    }

    void syncEnvelopeFromLane() {
        if (!m_clipAutoLanes || m_autoSelectedLaneIdx < 0 ||
            m_autoSelectedLaneIdx >= static_cast<int>(m_clipAutoLanes->size())) {
            m_autoEnvelopeWidget.setPoints({});
            return;
        }
        auto& env = (*m_clipAutoLanes)[m_autoSelectedLaneIdx].envelope;
        std::vector<std::pair<double,float>> pts;
        for (int i = 0; i < env.pointCount(); ++i)
            pts.push_back({env.point(i).time, env.point(i).value});
        m_autoEnvelopeWidget.setPoints(pts);
    }

    int findAutoLaneForTarget(const automation::AutomationTarget& tgt) const {
        if (!m_clipAutoLanes) return -1;
        for (size_t i = 0; i < m_clipAutoLanes->size(); ++i)
            if ((*m_clipAutoLanes)[i].target == tgt) return static_cast<int>(i);
        return -1;
    }

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
