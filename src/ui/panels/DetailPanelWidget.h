#pragma once
// DetailPanelWidget — Composite-widget detail panel.
//
// Migrated from v1 fw to fw2. Now inherits from fw2::Widget directly,
// so child widgets (FwKnob, DeviceWidget, SnapScrollContainer, etc.)
// are dispatched to natively without v1↔fw2 translation glue.
//
// Right-click is still forwarded directly from App.cpp via
// handleRightClick because the widget tree dispatch path doesn't
// surface right-clicks here yet.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/FilterDisplayWidget.h"
#include "ui/framework/v2/SubSynthDisplayPanel.h"
#include "ui/framework/v2/FMAlgorithmWidget.h"
#include "ui/framework/v2/SamplerDisplayPanel.h"
#include "ui/framework/v2/MultisamplerDisplayPanel.h"
#include "ui/framework/v2/WavetableDisplayPanel.h"
#include "ui/framework/v2/GranularDisplayPanel.h"
#include "ui/framework/v2/VocoderDisplayPanel.h"
#include "ui/framework/v2/SplineEQDisplayPanel.h"
#include "ui/framework/v2/ConvReverbDisplayPanel.h"
#include "ui/framework/v2/NeuralAmpDisplayPanel.h"
#include "ui/framework/v2/DrumSlopDisplayPanel.h"
#include "ui/framework/v2/DrumRackDisplayPanel.h"
#include "ui/framework/v2/InstrumentRackDisplayPanel.h"
#include "ui/framework/v2/DeviceHeaderWidget.h"
#include "ui/framework/v2/DeviceWidget.h"
#include "ui/framework/v2/LFODisplayWidget.h"
#include "ui/framework/v2/SnapScrollContainer.h"
#include "ui/framework/v2/WaveformWidget.h"
#include "ui/framework/v2/AutomationEnvelope.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/Theme.h"
#include "audio/Clip.h"
#include "audio/FollowAction.h"
#include "audio/TransientDetector.h"
#include "instruments/Instrument.h"
#include "instruments/Sampler.h"
#include "instruments/Multisampler.h"
#include "instruments/DrumSlop.h"
#include "instruments/DrumRack.h"
#include "instruments/InstrumentRack.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "effects/Filter.h"
#include "effects/SplineEQ.h"
#include "effects/ConvolutionReverb.h"
#include "effects/NeuralAmp.h"
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
namespace fw2 {

class DetailPanelWidget : public Widget {
public:
    // Layout constants (preserved for external users)
    static constexpr float kDefaultPanelHeight = 220.0f;
    static constexpr float kCollapsedHeight  = 8.0f;    // just the handle
    static constexpr float kHandleHeight     = 8.0f;    // drag-resize handle
    static constexpr float kMinPanelH        = 80.0f;
    static constexpr float kMaxPanelFraction = 0.75f;   // max 75% of window height
    static constexpr float kMinMaxPanelH     = 400.0f;  // absolute minimum for max height
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

    // PadFx — entries that render the per-pad effects of a DrumRack
    // alongside the rack's own widget. Same DeviceWidget UI as a
    // regular AudioFx; differs only in the chain it routes to (the
    // rack's selected pad's chain rather than the track's audio
    // chain). DeviceRef::padNote carries which pad the entry belongs
    // to so add / remove / move can find the right chain again.
    //
    // ChainFx — same idea for InstrumentRack: per-chain effects that
    // appear after the rack widget, swap when the user picks a
    // different chain. DeviceRef::padNote is reused as the chain
    // index for ChainFx entries (kept named padNote to avoid a
    // wider rename — the comment on the field clarifies usage).
    //
    // ChainInst — the InstrumentRack chain's nested instrument
    // exposed as its own widget so the user can edit the synth's
    // parameters without leaving the rack view. Uses a flat knob
    // grid (no instrument-specific display panel) to avoid clashing
    // with the outer rack's m_*Inst stash that drives tick-rebuild.
    enum class DeviceType { MidiFx, Instrument, AudioFx, PadFx, ChainFx,
                            ChainInst };
    enum class ViewMode  { Devices, AudioClip };

    // Clip view layout constants
    static constexpr float kClipTitleRowH    = 32.0f;   // top pad + title text
    static constexpr float kClipWaveformH    = 80.0f;
    static constexpr float kClipPropsH       = 80.0f;   // horizontal control strip
    static constexpr float kClipSectionGap   = 4.0f;
    static constexpr float kClipLabelW       = 60.0f;
    static constexpr float kClipValueW       = 80.0f;
    static constexpr float kAutoSectionH     = 90.0f;   // automation target picker + envelope

    DetailPanelWidget() {
        // Container widget that does its own hit-test + dispatch in
        // onMouseDown — opt OUT of the gesture state machine so an
        // unhandled press on dead space doesn't end up captured by
        // the panel and silently stack-overflow on the next mouseMove.
        setAutoCaptureOnUnhandledPress(false);
    }

    // --- Public API ---

    bool  isOpen() const { return m_open; }
    void  setOpen(bool open) {
        m_open = open;
        m_targetHeight = open ? m_userPanelHeight : kCollapsedHeight;
    }
    void  toggle() { setOpen(!m_open); }
    float height() const { return m_animatedHeight; }
    float panelHeight() const { return m_userPanelHeight; }
    // Programmatic height setter (used by VisualParamsPanel's drag handle
    // so its resize mirrors the audio/midi detail panel's resize).
    void setPanelHeight(float h) {
        m_userPanelHeight = std::clamp(h, kMinPanelH, maxPanelHeight());
        if (m_open) m_targetHeight = m_userPanelHeight;
    }

    // Call when window resizes so max panel height scales with screen
    void setWindowHeight(float h) { m_windowHeight = h; }
    float maxPanelHeight() const {
        float dynamic = m_windowHeight * kMaxPanelFraction;
        return std::max(dynamic, kMinMaxPanelH);
    }

    bool isFocused() const { return m_panelFocused; }
    void setFocused(bool f) { m_panelFocused = f; }

    // Right-click on a DrumRack pad — DetailPanel forwards the
    // event up to App via this callback so App can construct the
    // appropriate context menu (Add Pad FX, Remove, etc.). The
    // panel doesn't know about the audio-effect factory, so we
    // delegate. note in 0..127, (sx, sy) in screen coords.
    using DrumPadFxMenuCallback =
        std::function<void(instruments::DrumRack* rack, int note,
                            float sx, float sy)>;
    void setOnDrumPadFxMenu(DrumPadFxMenuCallback cb) {
        m_onDrumPadFxMenu = std::move(cb);
    }

    // Right-click on an InstrumentRack chain — same delegation
    // pattern as setOnDrumPadFxMenu. Args: the rack, the chain
    // index that was right-clicked, screen-space (sx, sy) for the
    // menu root.
    using InstrumentRackChainFxMenuCallback =
        std::function<void(instruments::InstrumentRack* rack, int chainIdx,
                            float sx, float sy)>;
    void setOnInstrumentRackChainFxMenu(
            InstrumentRackChainFxMenuCallback cb) {
        m_onInstrackChainFxMenu = std::move(cb);
    }

    // Cross-panel drag-drop hook. Called by App's mouse-up
    // handler when a global audio-clip drag is released over the
    // detail panel — gives the panel a chance to route the buffer
    // to a sub-target that needs precise placement, currently:
    //
    //   * DrumRack: drop onto a specific pad (uses the cursor
    //     position to pick which pad). Other rack pads are left
    //     untouched.
    //
    // Returns true on consumption. App falls back to its bulk
    // sample-receiver loaders (Sampler / Granular / DrumSlop /
    // Vocoder on the displayed track) when this returns false.
    bool tryConsumeAudioDropAt(float sx, float sy,
                                const audio::AudioBuffer& buf,
                                const std::string& name) {
        return tryConsumeAudioDropAt_inline(sx, sy, buf, name);
    }

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

    // Callback fired when the user right-clicks a device's parameter
    // knob — App opens its "Map to Macro N" context menu rooted at
    // the click position. Args: trackIndex, deviceType, chainIndex,
    // paramName, paramIndex, screenX, screenY. The param's name is
    // resolved by DetailPanel before the call so App doesn't need to
    // dereference instrument / effect device pointers.
    using ParamRightClickCallback =
        std::function<void(int trackIndex, DeviceType type, int chainIndex,
                            const std::string& paramName, int paramIndex,
                            float screenX, float screenY)>;
    void setOnParamRightClick(ParamRightClickCallback cb) {
        m_onParamRightClick = std::move(cb);
    }

    // Auto-Sample request — fired when the user clicks the "Auto-Sample"
    // button in MultisamplerDisplayPanel. App opens the AutoSampleDialog
    // with the supplied Multisampler as the capture target.
    using AutoSampleRequestCallback =
        std::function<void(instruments::Multisampler*)>;
    void setOnAutoSampleRequested(AutoSampleRequestCallback cb) {
        m_onAutoSampleRequested = std::move(cb);
    }

    // ── Sidechain source plumbing ───────────────────────────────────
    // Used by sidechain-aware instrument panels (Vocoder for now;
    // ring-mod / gate / etc. when they grow sidechain UI). The
    // DetailPanelWidget itself doesn't know about the Project — App
    // wires three callbacks to bridge:
    //
    //   * Names provider: returns one display string per project
    //     track in track-index order. Drives the source dropdown's
    //     items.
    //   * Source provider: returns the currently-routed sidechain
    //     source for the given track (-1 = none, else absolute track
    //     index). Driven into the panel each frame so the dropdown
    //     stays in sync if changed elsewhere (e.g. Mixer I/O).
    //   * Source setter: applied to project + audio engine when the
    //     user picks a new source from the device-side dropdown.
    using TrackNamesProvider = std::function<std::vector<std::string>()>;
    using SidechainSourceProvider = std::function<int(int trackIdx)>;
    using SetSidechainSourceCallback =
        std::function<void(int trackIdx, int sourceIdx)>;
    void setTrackNamesProvider(TrackNamesProvider cb) {
        m_trackNamesProvider = std::move(cb);
    }
    void setSidechainSourceProvider(SidechainSourceProvider cb) {
        m_sidechainSourceProvider = std::move(cb);
    }
    void setOnSetSidechainSource(SetSidechainSourceCallback cb) {
        m_setSidechainSource = std::move(cb);
    }

    // ── IR-loader callback for ConvolutionReverb (and any future
    // IR-loading effect like NeuralAmp Stage 2 with .nam files).
    // Implemented App-side: opens an SDL file dialog, reads the
    // chosen file via libsndfile (or whatever decoder the format
    // needs), and pushes the data into the effect via its own
    // load API. Display panel just emits "user wants to load" —
    // doesn't touch SDL or the filesystem.
    using LoadConvIRCallback = std::function<void(effects::ConvolutionReverb*)>;
    void setOnLoadConvIR(LoadConvIRCallback cb) {
        m_onLoadConvIR = std::move(cb);
    }

    // Same pattern for the NeuralAmp device's "Load Model…" button.
    // App-side handler opens an SDL file dialog scoped to .nam,
    // then calls effect->setModelPath which kicks off the actual
    // NAM library load + Reset + prewarm.
    using LoadNamModelCallback = std::function<void(effects::NeuralAmp*)>;
    void setOnLoadNamModel(LoadNamModelCallback cb) {
        m_onLoadNamModel = std::move(cb);
    }

    void setTrackIndex(int idx) { m_autoTrackIndex = idx; }

    // Per-track latency from the effect chain (samples + sample rate
    // → millisecond display). Pushed by App.cpp each frame from
    // Mixer::trackLatencySamples(); panel renders a small "Latency"
    // readout in the body header when > 0. Phase 1 of the latency
    // roadmap — Phase 2 (auto-compensation) will use the same data
    // to align signal paths at the master out.
    void setTrackLatencySamples(int samples, double sampleRate) {
        if (samples != m_trackLatencySamples ||
            sampleRate != m_trackLatencySampleRate) {
            m_trackLatencySamples = samples;
            m_trackLatencySampleRate = sampleRate;
            invalidate();
        }
    }

    void setLearnManager(midi::MidiLearnManager* lm) { m_learnManager = lm; }

    ViewMode viewMode() const { return m_viewMode; }

    // Helper — locate the knob currently in edit mode, if any.
    // Returned pointer is valid only for the duration of the call.
    FwKnob* editingKnob() {
        if (m_gainKnob.isEditing())      return &m_gainKnob;
        if (m_transposeKnob.isEditing()) return &m_transposeKnob;
        if (m_detuneKnob.isEditing())    return &m_detuneKnob;
        if (m_bpmKnob.isEditing())       return &m_bpmKnob;
        return nullptr;
    }
    const FwKnob* editingKnob() const {
        if (m_gainKnob.isEditing())      return &m_gainKnob;
        if (m_transposeKnob.isEditing()) return &m_transposeKnob;
        if (m_detuneKnob.isEditing())    return &m_detuneKnob;
        if (m_bpmKnob.isEditing())       return &m_bpmKnob;
        return nullptr;
    }

    // Check if any clip-property knob or device knob is in text-edit mode
    bool hasEditingKnob() const {
        if (editingKnob() != nullptr) return true;
        for (auto* dw : m_deviceWidgets)
            if (dw->hasEditingKnob()) return true;
        return false;
    }

    // Forward key events to the editing knob. Knob consumes only
    // Enter / Escape / Backspace from the key stream; digits arrive
    // through forwardTextInput (SDL TEXT_INPUT).
    bool forwardKeyDown(int key) {
        if (auto* k = editingKnob()) {
            KeyEvent ke;
            switch (key) {
                case 27 /*SDLK_ESCAPE*/:    ke.key = Key::Escape;    break;
                case 13 /*SDLK_RETURN*/:    ke.key = Key::Enter;     break;
                case 8  /*SDLK_BACKSPACE*/: ke.key = Key::Backspace; break;
                default:                    return false;
            }
            return k->dispatchKeyDown(ke);
        }
        for (auto* dw : m_deviceWidgets)
            if (dw->hasEditingKnob()) return dw->forwardKeyDown(key);
        return false;
    }

    // Forward text input to the editing knob
    bool forwardTextInput(const char* text) {
        if (auto* k = editingKnob()) {
            k->takeTextInput(text ? text : "");
            return true;
        }
        for (auto* dw : m_deviceWidgets)
            if (dw->hasEditingKnob()) return dw->forwardTextInput(text);
        return false;
    }

    // Cancel any knob in text-edit mode (e.g., when clicking outside)
    void cancelEditingKnobs() {
        if (auto* k = editingKnob()) k->endEdit(/*commit*/false);
        for (auto* dw : m_deviceWidgets) dw->cancelEditingKnobs();
    }

    // Show audio clip view: waveform + properties + effect chain
    void setAudioClip(const audio::Clip* clip, effects::EffectChain* fxChain,
                      int sampleRate = static_cast<int>(kDefaultSampleRate)) {
        if (clip == m_clipPtr && fxChain == m_lastFxChain &&
            (fxChain ? fxChain->count() : 0) == m_lastFxCount)
            return;

        m_viewMode = ViewMode::AudioClip;
        m_clipPtr = clip;
        m_clipSampleRate = sampleRate;
        m_waveformWidget.setClip(clip);
        m_waveformWidget.setSampleRate(sampleRate);

        // Setup warp mode dropdown.
        // List ORDER must match WarpMode enum order (we map dropdown
        // index → WarpMode by static_cast). When you add a value to
        // WarpMode, add the matching label here at the same position.
        m_warpModeDropdown.setItems(std::vector<std::string>{
            "Off", "Auto", "Beats", "Tones", "Texture",
            "Tones (PGHI)", "Repitch"});
        m_warpModeDropdown.setSelectedIndex(static_cast<int>(clip->warpMode));
        m_warpModeDropdown.setOnChange([this](int idx, const std::string&) {
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

        // Crop to loop — truncates the buffer to the [loopStart,
        // loopEnd) region. Resets loop bounds to the new buffer's
        // full extent and clears warp markers + transients (they'd
        // point past the new end). Buffer swap uses shared_ptr
        // assignment so the audio thread keeps reading the old
        // buffer through its existing strong ref until next block.
        m_cropLoopBtn.setLabel("Crop");
        m_cropLoopBtn.setOnClick([this]() {
            if (!m_clipPtr || !m_clipPtr->buffer) return;
            auto* clip = const_cast<audio::Clip*>(m_clipPtr);
            const int64_t s = clip->loopStart;
            const int64_t e = clip->effectiveLoopEnd();
            if (e <= s) return;
            const int chans  = clip->buffer->numChannels();
            const int64_t n  = e - s;
            auto newBuf = std::make_shared<audio::AudioBuffer>(
                chans, static_cast<int>(n));
            for (int ch = 0; ch < chans; ++ch) {
                const float* src = clip->buffer->channelData(ch) + s;
                float*       dst = newBuf->channelData(ch);
                std::copy(src, src + n, dst);
            }
            clip->buffer    = std::move(newBuf);
            clip->loopStart = 0;
            clip->loopEnd   = -1;          // means "end of buffer"
            clip->warpMarkers.clear();
            clip->transients.clear();
            // Re-fit waveform view to the new (smaller) buffer so
            // the user immediately sees the cropped result without
            // having to F-zoom themselves.
            m_waveformWidget.fitToWidth();
        });

        // Reverse loop — produces a fresh buffer with samples
        // [loopStart, loopEnd) reversed and the head/tail copied
        // verbatim, then swaps the shared_ptr. Same atomic pattern
        // as Crop above: the audio thread keeps reading the old
        // buffer through its existing strong ref until the next
        // callback grabs the new pointer. No in-place mutation, no
        // glitch even on a currently-playing clip. Loop bounds stay
        // put so the user can layer forward+reversed plays.
        m_reverseLoopBtn.setLabel("Reverse");
        m_reverseLoopBtn.setOnClick([this]() {
            if (!m_clipPtr || !m_clipPtr->buffer) return;
            auto* clip = const_cast<audio::Clip*>(m_clipPtr);
            const int64_t s = clip->loopStart;
            const int64_t e = clip->effectiveLoopEnd();
            if (e <= s) return;
            const int     chans  = clip->buffer->numChannels();
            const int64_t frames = clip->buffer->numFrames();
            auto newBuf = std::make_shared<audio::AudioBuffer>(
                chans, static_cast<int>(frames));
            for (int ch = 0; ch < chans; ++ch) {
                const float* src = clip->buffer->channelData(ch);
                float*       dst = newBuf->channelData(ch);
                // Pre-loop tail copied as-is.
                std::copy(src, src + s, dst);
                // Loop region reversed into place.
                std::reverse_copy(src + s, src + e, dst + s);
                // Post-loop tail copied as-is.
                std::copy(src + e, src + frames, dst + e);
            }
            clip->buffer = std::move(newBuf);
            clip->warpMarkers.clear();
            clip->transients.clear();
        });

        // Gain knob: 0 to ~+6dB, stored as linear gain in clip.
        m_gainKnob.setRange(0.0f, 2.0f);
        m_gainKnob.setDefaultValue(1.0f);
        m_gainKnob.setValue(clip->gain);
        m_gainKnob.setLabel("");
        m_gainKnob.setShowLabel(false);
        m_gainKnob.setValueFormatter([](float v) -> std::string {
            if (v < 0.001f) return "-inf dB";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f dB", 20.0f * std::log10(v));
            return buf;
        });
        m_gainKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->gain = v;
        });

        // Transpose (semitones) — integer step.
        m_transposeKnob.setRange(-48.0f, 48.0f);
        m_transposeKnob.setDefaultValue(0.0f);
        m_transposeKnob.setValue(static_cast<float>(clip->transposeSemitones));
        m_transposeKnob.setStep(1.0f);
        m_transposeKnob.setPixelsPerFullRange(240.0f);
        m_transposeKnob.setLabel("");
        m_transposeKnob.setShowLabel(false);
        m_transposeKnob.setValueFormatter([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%+d st", static_cast<int>(v));
            return buf;
        });
        m_transposeKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->transposeSemitones = static_cast<int>(v);
        });

        // Detune (cents) — integer step.
        m_detuneKnob.setRange(-50.0f, 50.0f);
        m_detuneKnob.setDefaultValue(0.0f);
        m_detuneKnob.setValue(static_cast<float>(clip->detuneCents));
        m_detuneKnob.setStep(1.0f);
        m_detuneKnob.setPixelsPerFullRange(400.0f);
        m_detuneKnob.setLabel("");
        m_detuneKnob.setShowLabel(false);
        m_detuneKnob.setValueFormatter([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%+d ct", static_cast<int>(v));
            return buf;
        });
        m_detuneKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->detuneCents = static_cast<int>(v);
        });

        // BPM — continuous value.
        m_bpmKnob.setRange(20.0f, 999.0f);
        m_bpmKnob.setDefaultValue(120.0f);
        m_bpmKnob.setValue(clip->originalBPM > 0 ? static_cast<float>(clip->originalBPM) : 120.0f);
        m_bpmKnob.setPixelsPerFullRange(240.0f);
        m_bpmKnob.setLabel("");
        m_bpmKnob.setShowLabel(false);
        m_bpmKnob.setValueFormatter([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            return buf;
        });
        m_bpmKnob.setOnChange([this](float v) {
            if (m_clipPtr) const_cast<audio::Clip*>(m_clipPtr)->originalBPM = static_cast<double>(v);
        });

        // Loop toggle button.
        m_loopToggleBtn.setLabel(clip->looping ? "On" : "Off");
        m_loopToggleBtn.setState(clip->looping);
        m_loopToggleBtn.setOnChange([this](bool on) {
            if (m_clipPtr) {
                auto* mc = const_cast<audio::Clip*>(m_clipPtr);
                mc->looping = on;
                m_loopToggleBtn.setLabel(on ? "On" : "Off");
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
        m_autoEnvelopeWidget.setOverlayMode(true);

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

                if (!setupAudioEffectDisplay(dw, fx, ref))
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
        // See comment in setDeviceChain: m_scroll's invalidations don't
        // bubble to us (it's a member, not a child), so we have to
        // invalidate the panel ourselves to force onLayout to re-run.
        invalidate();
    }

    void clearClipView() {
        m_viewMode = ViewMode::Devices;
        m_clipPtr = nullptr;
        m_waveformWidget.setClip(nullptr);
    }
    // (setFollowAction removed — orphaned; FA UI lives in BrowserPanel.)

    // Forward playback state to the waveform widget
    void setClipPlayPosition(int64_t pos) { m_waveformWidget.setPlayPosition(pos); }
    void setClipPlaying(bool playing)     { m_waveformWidget.setPlaying(playing); }
    void setTransportBPM(double bpm)      { m_waveformWidget.setTransportBPM(bpm); }

    // Set clip automation context for the envelope editor
    void setClipAutomation(std::vector<automation::AutomationLane>* lanes, int trackIndex) {
        // Skip rebuild if nothing changed — prevents resetting dropdown selection
        if (lanes == m_clipAutoLanes && trackIndex == m_autoTrackIndex)
            return;

        m_clipAutoLanes = lanes;
        m_autoTrackIndex = trackIndex;

        buildAutoTargetList();
        m_autoTargetDropdown.setOnChange([this](int idx, const std::string&) {
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

        // DrumRack-only: also fingerprint the selected pad index +
        // the count of effects on that pad's chain. The rest of the
        // device tree could be unchanged, but a pad-selection change
        // still needs to swap in the new pad's PadFx widgets, and an
        // add / remove on the pad's own chain still needs a rebuild.
        // Other instruments leave both fields at -1 / 0 so the
        // fingerprint is a no-op for them.
        int padNote     = -1;
        int padFxCount  = 0;
        if (auto* dr = dynamic_cast<instruments::DrumRack*>(inst)) {
            padNote = dr->selectedPad();
            const auto* pc = dr->padFxChainOrNull(padNote);
            padFxCount = pc ? pc->count() : 0;
        }
        // InstrumentRack-only: same idea — selected chain index +
        // per-chain fx count. Other instruments leave both at -1/0.
        int chainIdx     = -1;
        int chainFxCount = 0;
        if (auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst)) {
            chainIdx = ir->selectedChain();
            if (chainIdx >= 0 && chainIdx < ir->chainCount()) {
                const auto* fc = ir->chainFxChainOrNull(chainIdx);
                chainFxCount = fc ? fc->count() : 0;
            } else {
                chainIdx = -1;
            }
        }

        if (m_viewMode == ViewMode::Devices &&
            midiChain == m_lastMidiChain && inst == m_lastInst &&
            fxChain == m_lastFxChain &&
            midiCount == m_lastMidiCount && fxCount == m_lastFxCount &&
            padNote == m_lastPadNote &&
            padFxCount == m_lastPadFxCount &&
            chainIdx == m_lastChainIdx &&
            chainFxCount == m_lastChainFxCount)
            return;

        m_viewMode = ViewMode::Devices;
        m_clipPtr  = nullptr;

        saveExpandedStates();

        m_scroll.removeAllChildren();
        for (auto* dw : m_deviceWidgets) delete dw;
        m_deviceWidgets.clear();
        m_deviceRefs.clear();
        m_displayUpdaters.clear();
        m_drumRackDisplay = nullptr;
        m_drumRackInst    = nullptr;
        m_msDisplay       = nullptr;
        m_msInst          = nullptr;
        m_instrackInst    = nullptr;

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

                if (!setupAudioEffectDisplay(dw, fx, ref))
                    configureDeviceWidget(dw, ref);

                snapPoints.push_back(xPos);
                xPos += dw->preferredWidth() + kDeviceGap;
                m_scroll.addChild(dw);
                m_deviceWidgets.push_back(dw);
                m_deviceRefs.push_back(ref);
            }
        }

        // Per-chain INSTRUMENT widget — exposes the nested
        // instrument's params as a flat knob grid so the user can
        // tweak SubSynth / Sampler / etc. without leaving the rack
        // view. Appended before the ChainFx widgets so the strip
        // reads left-to-right as: ... → Rack → "Chain N: SubSynth"
        // → "Chain N: Reverb" → ... Uses a flat knob grid
        // (configureDeviceWidget) instead of setupInstrumentDisplay
        // — the latter would overwrite m_drumRackInst / m_msInst /
        // m_instrackInst with the chain's nested pointers and
        // confuse the outer-rack tick rebuild logic.
        if (auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
            ir && chainIdx >= 0) {
            auto& chRef = ir->chain(chainIdx);
            if (chRef.instrument) {
                DeviceRef ref;
                ref.type        = DeviceType::ChainInst;
                ref.chainIndex  = 0;
                ref.padNote     = chainIdx;
                ref.instrument  = chRef.instrument.get();

                auto* dw = new DeviceWidget();
                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "Chain %d: %s",
                              chainIdx + 1, chRef.instrument->name());
                dw->setDeviceName(nameBuf);
                dw->setDeviceType(DeviceHeaderWidget::DeviceType::Instrument);
                dw->setRemovable(false);
                dw->setExpanded(findPrevExpanded(
                    static_cast<void*>(chRef.instrument.get())));
                dw->setBypassed(chRef.instrument->bypassed());

                configureDeviceWidget(dw, ref);

                snapPoints.push_back(xPos);
                xPos += dw->preferredWidth() + kDeviceGap;
                m_scroll.addChild(dw);
                m_deviceWidgets.push_back(dw);
                m_deviceRefs.push_back(ref);
            }
        }

        // Per-chain effects of the InstrumentRack instrument (if the
        // track hosts one). Appended after the track audio fx so the
        // chain reads left-to-right as: MIDI fx → Instrument → Track
        // audio fx → "Chain N: <fx>" widgets. Same fingerprint /
        // rebuild model as PadFx — selecting a different chain swaps
        // in that chain's fx; add / remove triggers a rebuild via
        // tick().
        if (auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
            ir && chainIdx >= 0) {
            auto* fc = ir->chainFxChainOrNull(chainIdx);
            if (fc) {
                for (int i = 0; i < fc->count(); ++i) {
                    auto* fx = fc->effectAt(i);
                    if (!fx) continue;
                    DeviceRef ref;
                    ref.type        = DeviceType::ChainFx;
                    ref.chainIndex  = i;
                    // Reuse padNote as the InstrumentRack chain
                    // index (see comment on DeviceRef::padNote).
                    ref.padNote     = chainIdx;
                    ref.audioEffect = fx;

                    auto* dw = new DeviceWidget();
                    char nameBuf[64];
                    std::snprintf(nameBuf, sizeof(nameBuf), "Chain %d: %s",
                                  chainIdx + 1, fx->name());
                    dw->setDeviceName(nameBuf);
                    dw->setDeviceType(DeviceHeaderWidget::DeviceType::AudioEffect);
                    dw->setRemovable(true);
                    dw->setExpanded(findPrevExpanded(static_cast<void*>(fx)));
                    dw->setBypassed(fx->bypassed());

                    if (fx->isVisualizer())
                        dw->setVisualizer(true, fx->visualizerType());

                    if (!setupAudioEffectDisplay(dw, fx, ref))
                        configureDeviceWidget(dw, ref);

                    snapPoints.push_back(xPos);
                    xPos += dw->preferredWidth() + kDeviceGap;
                    m_scroll.addChild(dw);
                    m_deviceWidgets.push_back(dw);
                    m_deviceRefs.push_back(ref);
                }
            }
        }

        // Per-pad effects of the DrumRack instrument (if the track
        // hosts one). Appended after the track audio fx so the chain
        // reads left-to-right as: MIDI fx → Instrument → Track audio
        // fx → "Pad N FX" header → per-pad fx. The pad fingerprint
        // above guarantees we rebuild whenever the user picks a
        // different pad or adds / removes an effect on the current
        // pad's chain.
        if (auto* dr = dynamic_cast<instruments::DrumRack*>(inst)) {
            const int sel = dr->selectedPad();
            auto* padChain = dr->padFxChainOrNull(sel);
            if (padChain) {
                for (int i = 0; i < padChain->count(); ++i) {
                    auto* fx = padChain->effectAt(i);
                    if (!fx) continue;
                    DeviceRef ref;
                    ref.type        = DeviceType::PadFx;
                    ref.chainIndex  = i;
                    ref.padNote     = sel;
                    ref.audioEffect = fx;

                    auto* dw = new DeviceWidget();
                    // Tag the device name with "Pad N:" so the pad
                    // these effects belong to is unambiguous on
                    // screen — multiple track-level chains with
                    // similarly-named effects can otherwise look
                    // identical to the user.
                    char nameBuf[64];
                    std::snprintf(nameBuf, sizeof(nameBuf), "Pad %d: %s",
                                  sel, fx->name());
                    dw->setDeviceName(nameBuf);
                    dw->setDeviceType(DeviceHeaderWidget::DeviceType::AudioEffect);
                    dw->setRemovable(true);
                    dw->setExpanded(findPrevExpanded(static_cast<void*>(fx)));
                    dw->setBypassed(fx->bypassed());

                    if (fx->isVisualizer())
                        dw->setVisualizer(true, fx->visualizerType());

                    if (!setupAudioEffectDisplay(dw, fx, ref))
                        configureDeviceWidget(dw, ref);

                    snapPoints.push_back(xPos);
                    xPos += dw->preferredWidth() + kDeviceGap;
                    m_scroll.addChild(dw);
                    m_deviceWidgets.push_back(dw);
                    m_deviceRefs.push_back(ref);
                }
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
        m_lastPadNote   = padNote;
        m_lastPadFxCount = padFxCount;
        m_lastChainIdx  = chainIdx;
        m_lastChainFxCount = chainFxCount;

        // Auto-open when devices are populated
        if (!m_deviceWidgets.empty()) setOpen(true);

        // Invalidate the panel's layout cache. m_scroll lives as a
        // member (not an fw2 child), so the addChild calls above only
        // bumped m_scroll's own version — they did NOT bubble up to us.
        // Without this, switching between two tracks while the panel
        // is already visible leaves the new device widgets with stale
        // (0,0,0,0) bounds: the panel.onLayout cache hits, m_scroll
        // .layout never runs, the user sees empty space and has to
        // toggle D twice to force a relayout.
        invalidate();
    }

    void clear() {
        m_scroll.removeAllChildren();
        for (auto* dw : m_deviceWidgets) delete dw;
        m_deviceWidgets.clear();
        m_deviceRefs.clear();
        m_expandStates.clear();
        m_displayUpdaters.clear();
        m_drumRackDisplay = nullptr;
        m_drumRackInst    = nullptr;
        m_msDisplay       = nullptr;
        m_msInst          = nullptr;
        m_instrackInst    = nullptr;
        m_lastMidiChain = nullptr;
        m_lastInst      = nullptr;
        m_lastFxChain   = nullptr;
        m_lastMidiCount = 0;
        m_lastFxCount   = 0;
        m_lastPadNote   = -1;
        m_lastPadFxCount = 0;
        m_lastChainIdx  = -1;
        m_lastChainFxCount = 0;
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
            const auto& wb = m_waveformWidget.bounds();
            if (m_lastMouseX >= wb.x && m_lastMouseX < wb.x + wb.w &&
                m_lastMouseY >= wb.y && m_lastMouseY < wb.y + wb.h) {
                ScrollEvent se;
                se.x = m_lastMouseX; se.y = m_lastMouseY;
                se.lx = m_lastMouseX - wb.x;
                se.ly = m_lastMouseY - wb.y;
                se.dx = dx; se.dy = dy;
                if (ctrl)
                    se.modifiers |= ModifierKey::Ctrl;
                m_waveformWidget.dispatchScroll(se);
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

    // v1 MIDI Learn context menu retired — fw2::ContextMenu handles
    // hover via LayerStack. API kept as an empty stub so call sites
    // can be removed in a follow-up commit.
    void handleDeviceContextMenuMouseMove(float, float) {}

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, m_animatedHeight});
    }

    // Per-frame animation step. Advance the open/close height
    // animation toward m_targetHeight and invalidate the measure
    // cache so the next layout pass picks up the new height.
    //
    // Must be called once per frame (App::update does this). Tests
    // drive convergence by calling tick() in a tight loop.
    //
    // Animation cannot live inside onMeasure because fw2 caches the
    // measured size against (constraints, epoch, localVersion); a
    // tight loop of measure() with the same constraints would only
    // run onMeasure once and the animation would never advance.
    void tick() {
        const float prev = m_animatedHeight;
        updateAnimation();
        if (m_animatedHeight != prev) invalidate();

        // DrumRack: rebuild the device tree whenever the selected
        // pad changes or the per-pad effect chain count changes
        // (user added / removed an effect via the right-click menu,
        // or a kit preset load swapped in a different chain).
        //
        // We use the m_drumRackInst member rather than dynamic_cast'ing
        // m_lastInst — both are nulled together in clear() / on every
        // device-tree rebuild, but going through the explicit member
        // sidesteps an `_RTDynamicCast` exception if any caller ever
        // forgets to clear() before destroying instruments. The
        // fingerprint inside setDeviceChain is the actual gate; this
        // tick just kicks the call. Cheap when nothing changed
        // (early-out via the fingerprint short-circuit).
        if (m_viewMode == ViewMode::Devices && m_drumRackInst &&
            m_drumRackInst == m_lastInst) {
            int sel = m_drumRackInst->selectedPad();
            const auto* pc = m_drumRackInst->padFxChainOrNull(sel);
            int cnt = pc ? pc->count() : 0;
            if (sel != m_lastPadNote || cnt != m_lastPadFxCount) {
                setDeviceChain(m_lastMidiChain, m_lastInst, m_lastFxChain);
            }
        }

        // InstrumentRack: same pattern. Rebuild when the user picks
        // a different chain (via the rack's chain list) or when the
        // selected chain's fx count changes (add / remove).
        if (m_viewMode == ViewMode::Devices && m_instrackInst &&
            m_instrackInst == m_lastInst) {
            int sel = m_instrackInst->selectedChain();
            if (sel >= m_instrackInst->chainCount()) sel = -1;
            int cnt = 0;
            if (sel >= 0) {
                const auto* fc = m_instrackInst->chainFxChainOrNull(sel);
                cnt = fc ? fc->count() : 0;
            }
            if (sel != m_lastChainIdx || cnt != m_lastChainFxCount) {
                setDeviceChain(m_lastMidiChain, m_lastInst, m_lastFxChain);
            }
        }
    }

#ifdef YAWN_TEST_BUILD
    void onLayout(Rect bounds, UIContext&) override { m_bounds = bounds; }
#else
    void onLayout(Rect bounds, UIContext& ctx) override;
#endif

    // ─── Rendering ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override;
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
                m_handleDragStartH + delta, kMinPanelH, maxPanelHeight());
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
        // A descendant has fw2 capture — route the move there directly.
        // Guard against self-dispatch (we set autoCaptureOnUnhandledPress=
        // false in the constructor, so this should never be us; the guard
        // is belt-and-suspenders against a future regression).
        if (auto* cap = Widget::capturedWidget(); cap && cap != this) {
            return cap->dispatchMouseMove(e);
        }
        // Forward to waveform widget in audio clip view
        if (m_viewMode == ViewMode::AudioClip) {
            if (m_waveformWidget.dispatchMouseMove(e)) return true;
        }
        for (auto* dw : m_deviceWidgets) {
            if (dw->dispatchMouseMove(e)) return true;
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
                int srcPadNote = m_deviceRefs[src].padNote;
                int targetChain = 0;
                for (int i = 0; i < ins; ++i) {
                    if (i != src && i < static_cast<int>(m_deviceRefs.size()) &&
                        m_deviceRefs[i].type == srcType)
                        ++targetChain;
                }
                if (targetChain != fromChain) {
                    if (srcType == DeviceType::PadFx) {
                        // PadFx reorder happens inline against the
                        // rack's own pad chain — no App callback
                        // needed because we already hold the rack
                        // pointer and the pad note locally.
                        if (m_drumRackInst) {
                            if (auto* chain =
                                    m_drumRackInst->padFxChain(srcPadNote))
                                chain->moveEffect(fromChain, targetChain);
                            // Reorder doesn't change the chain count,
                            // so tick()'s fingerprint check would NOT
                            // trigger a rebuild on its own. Force one
                            // by invalidating the count fingerprint;
                            // tick() will see the mismatch on the next
                            // frame and re-call setDeviceChain, which
                            // rebuilds the widgets in the new order.
                            m_lastPadFxCount = -1;
                        }
                    } else if (srcType == DeviceType::ChainFx) {
                        // ChainFx reorder — same inline pattern.
                        // srcPadNote is repurposed as the rack chain
                        // index for ChainFx widgets.
                        if (m_instrackInst) {
                            if (auto* chain =
                                    m_instrackInst->chainFxChain(srcPadNote))
                                chain->moveEffect(fromChain, targetChain);
                            m_lastChainFxCount = -1;
                        }
                    } else if (m_onMoveDevice) {
                        m_onMoveDevice(srcType, fromChain, targetChain);
                    }
                }
            }
            return true;
        }
        // A descendant has fw2 capture — release through it. Same self-
        // dispatch guard as in onMouseMove.
        if (auto* cap = Widget::capturedWidget(); cap && cap != this) {
            cap->dispatchMouseUp(e);
            return true;
        }
        // Forward to waveform widget
        if (m_viewMode == ViewMode::AudioClip) {
            if (m_waveformWidget.dispatchMouseUp(e)) return true;
        }
        for (auto* dw : m_deviceWidgets) {
            const auto& db = dw->bounds();
            if (e.x >= db.x && e.x < db.x + db.w &&
                e.y >= db.y && e.y < db.y + db.h) {
                if (dw->dispatchMouseUp(e)) return true;
            }
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_open) return false;
        if (m_viewMode == ViewMode::AudioClip) {
            const auto& wb = m_waveformWidget.bounds();
            if (e.x >= wb.x && e.x < wb.x + wb.w &&
                e.y >= wb.y && e.y < wb.y + wb.h) {
                return m_waveformWidget.dispatchScroll(e);
            }
        }
        return false;
    }

    // Drag-drop dispatcher implementation. Lives here (inline) so
    // the DrumRack-specific knowledge stays inside the panel that
    // already owns the display reference.
    bool tryConsumeAudioDropAt_inline(float sx, float sy,
                                       const audio::AudioBuffer& buf,
                                       const std::string& name) {
        const int frames   = buf.numFrames();
        const int channels = buf.numChannels();
        if (frames <= 0 || channels <= 0) return false;

        // Convert AudioBuffer (per-channel contiguous) → interleaved
        // once for either target — both samplers expect interleaved.
        auto interleave = [&]() {
            std::vector<float> il(static_cast<size_t>(frames) * channels);
            for (int ch = 0; ch < channels; ++ch) {
                const float* src = buf.channelData(ch);
                for (int i = 0; i < frames; ++i)
                    il[static_cast<size_t>(i) * channels + ch] = src[i];
            }
            return il;
        };

        // ── DrumRack: precise per-pad routing.
        if (m_drumRackDisplay && m_drumRackInst) {
            const int pad = m_drumRackDisplay->padNoteAt(sx, sy);
            if (pad >= 0) {
                const auto il = interleave();
                m_drumRackInst->loadPad(pad, il.data(), frames, channels);
                m_drumRackInst->setSelectedPad(pad);
                (void)name;
                return true;
            }
        }

        // ── Multisampler: drop anywhere on the panel adds a new
        // zone (rootNote C4, full key range). The user adjusts the
        // range / root afterward via the zone-map and editor. v1
        // intentionally doesn't try to derive key range from cursor
        // position over the 2D map — keeping it predictable trumps
        // a slightly fancier UX.
        if (m_msDisplay && m_msInst) {
            const auto& mb = m_msDisplay->bounds();
            if (sx >= mb.x && sx < mb.x + mb.w &&
                sy >= mb.y && sy < mb.y + mb.h) {
                const auto il = interleave();
                const int newIdx = m_msInst->addZone(
                    il.data(), frames, channels,
                    /*rootNote=*/60, /*lowKey=*/0, /*highKey=*/127);
                if (newIdx >= 0) {
                    m_msDisplay->setSelectedZone(newIdx);
                    (void)name;
                    return true;
                }
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
        // PadFx: which DrumRack pad's per-pad chain this entry
        // belongs to (0..127). ChainFx: which InstrumentRack chain
        // this entry belongs to (0..7). -1 for any other DeviceType.
        // Used by the inline remove / move handlers in DetailPanel
        // to relocate the right sub-chain.
        int padNote = -1;
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
                pi.formatFn = info.formatFn;
                if (info.valueLabels && info.valueLabelCount > 0)
                    pi.valueLabels.assign(info.valueLabels, info.valueLabels + info.valueLabelCount);
            } else if (ref.audioEffect) {
                auto& info = ref.audioEffect->parameterInfo(p);
                pi.name = info.name; pi.unit = info.unit;
                pi.minVal = info.minValue; pi.maxVal = info.maxValue;
                pi.defaultVal = info.defaultValue; pi.isBoolean = info.isBoolean;
                pi.widgetHint = info.widgetHint;
                pi.formatFn = info.formatFn;
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

        // Right-click on a device knob → "Map to Macro N" menu (App
        // builds + shows it). The param's display name is resolved
        // here against the live device pointer so App can stay
        // device-type agnostic in showMacroMappingMenu.
        // PadFx / ChainFx widgets opt out — the macro/automation
        // system has no schema for "pad N's chain index C param P"
        // (or "rack chain N's fx M's param P") yet, so wiring it
        // would let the user open a menu that mis-targets a track-
        // level effect. Easier to disallow until a proper sub-chain-
        // aware target type lands.
        if (ref.type != DeviceType::PadFx && ref.type != DeviceType::ChainFx &&
            ref.type != DeviceType::ChainInst) {
            dw->setOnParamRightClick(
                [this, ref](int paramIdx, float mx, float my) {
                    if (!m_onParamRightClick) return;
                    if (m_autoTrackIndex < 0) return;
                    std::string paramName;
                    if (ref.midiEffect && paramIdx < ref.midiEffect->parameterCount())
                        paramName = ref.midiEffect->parameterInfo(paramIdx).name
                                      ? ref.midiEffect->parameterInfo(paramIdx).name : "";
                    else if (ref.instrument && paramIdx < ref.instrument->parameterCount())
                        paramName = ref.instrument->parameterInfo(paramIdx).name
                                      ? ref.instrument->parameterInfo(paramIdx).name : "";
                    else if (ref.audioEffect && paramIdx < ref.audioEffect->parameterCount())
                        paramName = ref.audioEffect->parameterInfo(paramIdx).name
                                      ? ref.audioEffect->parameterInfo(paramIdx).name : "";
                    if (paramName.empty()) return;
                    m_onParamRightClick(m_autoTrackIndex, ref.type,
                                         ref.chainIndex, paramName,
                                         paramIdx, mx, my);
                });
        }

        // Automation recording: forward knob touch begin/end
        // (skipped for PadFx / ChainFx for the same reason as the
        // macro menu).
        if (ref.type != DeviceType::PadFx && ref.type != DeviceType::ChainFx &&
            ref.type != DeviceType::ChainInst) {
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

        // PadFx / ChainFx remove is handled inline rather than via
        // the App callback: removing requires the parent rack
        // pointer + the sub-index (pad note or chain index), both
        // of which we already have here. Going through
        // m_onRemoveDevice would force a wider callback signature
        // for the few device types that need it.
        if (ref.type == DeviceType::PadFx) {
            instruments::DrumRack* dr = m_drumRackInst;
            int note = ref.padNote;
            int chainIdx = ref.chainIndex;
            dw->setOnRemove([this, dr, note, chainIdx]() {
                if (!dr) return;
                if (auto* chain = dr->padFxChain(note))
                    chain->remove(chainIdx);
                // tick() will see the chain count drop and rebuild.
            });
        } else if (ref.type == DeviceType::ChainFx) {
            instruments::InstrumentRack* ir = m_instrackInst;
            int rackChain = ref.padNote;
            int chainIdx = ref.chainIndex;
            dw->setOnRemove([this, ir, rackChain, chainIdx]() {
                if (!ir) return;
                if (auto* chain = ir->chainFxChain(rackChain))
                    chain->remove(chainIdx);
            });
        } else {
            dw->setOnRemove([this, type = ref.type, chainIdx = ref.chainIndex]() {
                if (m_onRemoveDevice) m_onRemoveDevice(type, chainIdx);
            });
        }

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
        // (PadFx / ChainFx skipped — no MIDI Learn target type for
        // per-pad / per-chain fx, matching the macro / automation
        // skip above.)
        if (m_learnManager && ref.type != DeviceType::PadFx &&
            ref.type != DeviceType::ChainFx &&
            ref.type != DeviceType::ChainInst) {
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
                // Param 8 (Filter Cutoff) is 0..1 normalized → convert
                // to Hz for the display's filter-curve widget.
                const float cutoffHz =
                    instruments::SubtractiveSynth::cutoffNormToHz(
                        inst->getParameter(8));
                panel->updateFromParams(
                    inst->getParameter(0),  inst->getParameter(1),
                    inst->getParameter(2),  inst->getParameter(3),
                    cutoffHz,               inst->getParameter(9),
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
            // Slightly wider than the original 130 px so the sidechain
            // source dropdown has room when it appears, but not so wide
            // that the section knobs to the right get squeezed and
            // start their labels overlapping. 180 px fits "(none)" /
            // "Track 1" / "Audio 1" comfortably — long names get
            // ellipsised by the dropdown painter.
            config.displayWidth = 180;
            config.sections = {
                {"Source",   {1, 15, 2, 16}},   // Carrier Type, Detune, Mod Source, Carrier=SC
                {"Bands",    {0, 3, 6}},
                {"Envelope", {4, 5}},
                {"Mix",      {7, 8, 9}},
                {"Amp",      {10, 11}},
                {"Output",   {12, 13, 14}},
            };

            // Wire the device-side sidechain picker → App callback.
            // App-side handler updates project.track[t].sidechainSource
            // and sends SetSidechainSourceMsg to the audio engine.
            vocPanel->setOnSidechainSourceChanged(
                [this](int sourceIdx) {
                    if (m_autoTrackIndex < 0) return;
                    if (m_setSidechainSource)
                        m_setSidechainSource(m_autoTrackIndex, sourceIdx);
                });

            m_displayUpdaters.push_back([this, vocPanel, inst]() {
                auto* voc = dynamic_cast<instruments::Vocoder*>(inst);
                if (!voc) return;
                if (voc->hasModulatorSample())
                    vocPanel->setModulatorData(voc->modulatorData(),
                                               voc->modulatorFrames());
                vocPanel->setPlayhead(voc->modulatorPlayhead());
                vocPanel->setPlaying(voc->isPlaying());
                vocPanel->setBandCount(
                    static_cast<int>(voc->getParameter(instruments::Vocoder::kBands)));
                vocPanel->setModSource(
                    static_cast<int>(voc->getParameter(instruments::Vocoder::kModSource)));
                vocPanel->setModLevel(voc->consumeModulatorLevel());
                // Drain the per-band envelope peaks into the display
                // panel so the spectrum strip animates with the
                // modulator's spectral content. Sized to kMaxBands
                // (32); trailing entries are simply ignored at the
                // current band count.
                float bandLevels[instruments::Vocoder::kMaxBands] = {};
                voc->consumeBandLevels(bandLevels,
                                       instruments::Vocoder::kMaxBands);
                vocPanel->setBandLevels(bandLevels,
                                        instruments::Vocoder::kMaxBands);

                // Pull the latest project track list + current sidechain
                // source so the dropdown stays in sync with whatever
                // the Mixer I/O strip might have changed.
                if (m_trackNamesProvider) {
                    vocPanel->setAvailableSources(
                        m_trackNamesProvider(), m_autoTrackIndex);
                }
                if (m_sidechainSourceProvider && m_autoTrackIndex >= 0) {
                    vocPanel->setSidechainSource(
                        m_sidechainSourceProvider(m_autoTrackIndex));
                }
            });
        } else if (nm == "Drum Synth") {
            // 8 sections, one per drum, in MIDI-ascending order (matches
            // the DrumRoll's row order — bottom = Kick (note 36), top
            // = Tambourine (note 54)). Kick has 7 params (the only drum
            // with sine/white/pink mix knobs); the others have the
            // standard 4 (tune, attack, decay, drive).
            //
            // GroupedKnobBody packs 7 knobs into a 4×2 grid (cols=
            // ceil(7/2)=4) and 4 knobs into a 2×2 grid (cols=ceil(4/2)
            // =2), so the kick strip is roughly twice as wide as each
            // other-drum strip. Total body width ≈ 1230 px which fits
            // comfortably on a 1280-px screen with horizontal scroll
            // for narrower displays.
            config.sections = {
                {"Kick",  { 0,  1,  2,  3,  4,  5,  6}},
                {"Snare", { 7,  8,  9, 10}},
                {"Clap",  {11, 12, 13, 14}},
                {"Tom1",  {15, 16, 17, 18}},
                {"CHH",   {19, 20, 21, 22}},
                {"OHH",   {23, 24, 25, 26}},
                {"Tom2",  {27, 28, 29, 30}},
                {"Tamb",  {31, 32, 33, 34}},
            };
        } else if (nm == "Drum Rack") {
            auto* drPanel = new DrumRackDisplayPanel();
            config.display = drPanel;
            config.displayWidth = 160;
            // 0 = Volume (Global). 1..8 = per-selected-pad:
            // Pad Volume / Pan / Pitch / Choke / Attack / Decay
            // / Start / End. Grouped into Pad / AR Env / Region
            // so each row has breathing space.
            config.sections = {
                {"Global", {0}},
                {"Pad",    {1, 2, 3, 4}},
                {"AR Env", {5, 6}},
                {"Region", {7, 8}},
            };

            // Stash for the cross-panel drag-drop hook so audio
            // clips dropped on a pad can be routed there.
            m_drumRackDisplay = drPanel;
            m_drumRackInst    = dynamic_cast<instruments::DrumRack*>(inst);

            drPanel->setOnPadClick([inst](int note) {
                auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
                if (dr) dr->setSelectedPad(note);
            });

            // Right-click on a pad → forward up to App so it can
            // open the per-pad fx context menu (Add / Remove
            // effects). DetailPanel doesn't know about the audio-
            // effect factory; App owns that knowledge.
            drPanel->setOnPadRightClick([this, inst](int note,
                                                       float sx, float sy) {
                auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
                if (dr && m_onDrumPadFxMenu)
                    m_onDrumPadFxMenu(dr, note, sx, sy);
            });

            m_displayUpdaters.push_back([drPanel, inst, lastSel = -1]() mutable {
                auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
                if (!dr) return;

                int sel = dr->selectedPad();
                drPanel->setSelectedPad(sel);

                // Only auto-jump the page when the selection ACTUALLY
                // changed (e.g. external setSelectedPad from drop or
                // MIDI click). Without this guard, the updater fired
                // every frame would force the page to track sel/16
                // and any user-driven page navigation (arrow buttons
                // / page labels / scroll wheel) would be reverted on
                // the next render tick.
                if (sel != lastSel) {
                    drPanel->setPage(sel / 16);
                    lastSel = sel;
                }

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

                // Region trim markers — pushed every frame so the
                // markers track the Region knobs live as the user
                // drags them, and so a kit-preset load also updates
                // the display without an extra rebuild.
                drPanel->setSelectedPadRegion(dr->padStart(sel),
                                              dr->padEnd(sel));
            });
        } else if (nm == "Instrument Rack") {
            auto* irPanel = new InstrumentRackDisplayPanel();
            config.display = irPanel;
            // Bumped 160 → 240 so the chain rows fit "1: Subtractive
            // Synth" without truncation at the user's theme font
            // scale, and so the key/vel range bars have enough width
            // to read at a glance.
            config.displayWidth = 240;
            config.sections = {
                {"Rack",  {0}},
                {"Chain", {1, 2, 3, 4, 5, 6}},
            };

            // Stash the rack pointer so tick() can detect a chain
            // selection or per-chain fx count change and trigger a
            // rebuild — same pattern as m_drumRackInst.
            m_instrackInst = dynamic_cast<instruments::InstrumentRack*>(inst);

            irPanel->setOnChainClick([inst](int idx) {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (ir) ir->setSelectedChain(idx);
            });

            // Right-click on a chain row → forward up to App so it
            // can open the per-chain fx context menu (Add / Remove
            // effects). DetailPanel doesn't know about the audio-
            // effect factory; App owns that knowledge.
            irPanel->setOnChainRightClick([this, inst](int chainIdx,
                                                          float sx, float sy) {
                auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
                if (ir && m_onInstrackChainFxMenu)
                    m_onInstrackChainFxMenu(ir, chainIdx, sx, sy);
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
        } else if (nm == "Multisampler") {
            auto* msPanel = new MultisamplerDisplayPanel();
            config.display = msPanel;
            // Stash for the cross-panel drag-drop hook so audio
            // clips dropped on the panel can be added as zones.
            m_msDisplay = msPanel;
            m_msInst    = dynamic_cast<instruments::Multisampler*>(inst);
            // Wider than other display panels because it hosts both
            // a zone list and a per-zone editor side-by-side.
            config.displayWidth = 360.0f;
            // Multisampler param indices (Multisampler::Param):
            //   0..3  Amp ADSR
            //   4..6  Filter (Cutoff, Reso, Env amount)
            //   7..10 Filter ADSR
            //   11    Glide
            //   12    VelXfade
            //   13    Volume
            config.sections = {
                {"Amp",      {0, 1, 2, 3}},
                {"Filter",   {4, 5, 6}},
                {"Filt Env", {7, 8, 9, 10}},
                {"Misc",     {11, 12, 13}},
            };

            // Edit a single zone's metadata in place. The audio thread
            // reads zone fields without a lock, same caveat as
            // addZone / clearZones — UI-thread mutation is racy in
            // theory but harmless in practice for one-int writes.
            msPanel->setOnZoneFieldChange([inst](int zoneIdx,
                    const MultisamplerDisplayPanel::ZoneRow& src) {
                auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
                if (!ms) return;
                auto* z = ms->zone(zoneIdx);
                if (!z) return;
                z->rootNote = src.rootNote;
                z->lowKey   = src.lowKey;
                z->highKey  = src.highKey;
                z->lowVel   = src.lowVel;
                z->highVel  = src.highVel;
                z->tune     = src.tune;
                z->volume   = src.volume;
                z->pan      = src.pan;
                z->loop     = src.loop;
            });

            msPanel->setOnRemoveZone([inst](int zoneIdx) {
                auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
                if (!ms) return;
                ms->removeZone(zoneIdx);
            });

            msPanel->setOnAutoSampleClicked([this, inst]() {
                // Forward the click up to App, which owns the
                // FwAutoSampleDialog and the engine/midi context the
                // dialog needs to open with.
                auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
                if (ms && m_onAutoSampleRequested) {
                    m_onAutoSampleRequested(ms);
                }
            });

            // Push fresh zone rows from the instrument each frame.
            // setZones short-circuits when nothing changed, so the
            // user's mid-edit knob values aren't stomped.
            m_displayUpdaters.push_back([msPanel, inst]() {
                auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
                if (!ms) return;
                std::vector<MultisamplerDisplayPanel::ZoneRow> rows;
                rows.reserve(ms->zoneCount());
                for (int i = 0; i < ms->zoneCount(); ++i) {
                    const auto* z = ms->zone(i);
                    if (!z) continue;
                    MultisamplerDisplayPanel::ZoneRow r;
                    r.rootNote     = z->rootNote;
                    r.lowKey       = z->lowKey;
                    r.highKey      = z->highKey;
                    r.lowVel       = z->lowVel;
                    r.highVel      = z->highVel;
                    r.tune         = z->tune;
                    r.volume       = z->volume;
                    r.pan          = z->pan;
                    r.loop         = z->loop;
                    r.sampleFrames = z->sampleFrames;
                    // filename: derived in the auto-sampler / drop
                    // path (commit 3) — empty for now.
                    rows.push_back(r);
                }
                msPanel->setZones(std::move(rows));
            });
            // Width-change wiring (deferred to AFTER the GroupedKnobBody
            // is constructed below; we have to capture the body* once it
            // exists, so the actual setOnPreferredWidthChanged() call
            // lives just past the body/setCustomBody sequence).
            m_pendingMsPanelWidthHook = msPanel;
        } else {
            return false;
        }

        // Build param descriptors
        int count = inst->parameterCount();
        std::vector<GroupedKnobBody::ParamDesc> params(count);
        for (int p = 0; p < count; ++p) {
            auto& info = inst->parameterInfo(p);
            params[p] = {p, info.name, info.unit ? info.unit : "",
                         info.minValue, info.maxValue, info.defaultValue,
                         info.isBoolean, info.formatFn};
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

        // Multisampler panel toggles its 2D map → reports a new
        // preferred display width → body re-flows. Wired here, after
        // the body exists. Captures `body` only — the panel pointer
        // was stashed in m_pendingMsPanelWidthHook a few lines up.
        if (m_pendingMsPanelWidthHook) {
            m_pendingMsPanelWidthHook->setOnPreferredWidthChanged(
                [body](float newWidth) { body->setDisplayWidth(newWidth); });
            m_pendingMsPanelWidthHook = nullptr;
        }

        // Bypass / remove callbacks
        dw->setOnBypassToggle([ref](bool) { DeviceRef r = ref; r.toggleBypass(); });
        dw->setOnRemove([this, type = ref.type, chainIdx = ref.chainIndex]() {
            if (m_onRemoveDevice) m_onRemoveDevice(type, chainIdx);
        });
        return true;
    }

    // ── Build custom display for audio effects (currently Filter) ──
    bool setupAudioEffectDisplay(DeviceWidget* dw, effects::AudioEffect* fx,
                                 const DeviceRef& ref) {
        if (!fx) return false;
        std::string id = fx->id();

        if (id == "splineeq") {
            // Use setCustomBody (not setCustomPanel) so the panel
            // REPLACES the per-param knob layout as the device's
            // body. setCustomPanel inherited the device width from
            // the knob count — with 40 EQ params that ballooned the
            // strip past 1200 px regardless of any minW we passed.
            // As a CustomDeviceBody the panel controls its own
            // preferredBodyWidth() (270 px), so the strip sizes
            // sanely. The 40 params remain settable via automation
            // / preset / MIDI Learn — just not shown in the strip,
            // because the panel IS the editor.
            auto* disp = new SplineEQDisplayPanel();
            disp->setEQ(static_cast<effects::SplineEQ*>(fx));
            disp->setSampleRate(static_cast<double>(m_clipSampleRate));
            disp->setOnParamChange([ref](int idx, float v) {
                DeviceRef r = ref; r.setParam(idx, v);
            });
            dw->setCustomBody(disp);
            configureDeviceWidget(dw, ref);
            return true;
        }

        if (id == "convreverb") {
            // Same CustomDeviceBody pattern as the Spline EQ — the
            // panel replaces the per-param knob list. Buttons drive
            // an App-side IR loader (file dialog + libsndfile decode
            // + push to effect via loadIRMono). Per-frame updater
            // keeps the panel's filename / waveform thumbnail in
            // sync with the underlying effect.
            auto* disp = new ConvReverbDisplayPanel();
            auto* cr   = static_cast<effects::ConvolutionReverb*>(fx);
            disp->setOnParamChange([ref](int idx, float v) {
                DeviceRef r = ref; r.setParam(idx, v);
            });
            disp->setOnLoadRequest([this, cr]() {
                if (m_onLoadConvIR) m_onLoadConvIR(cr);
            });
            disp->setOnClearRequest([cr]() { cr->clearIR(); });
            dw->setCustomBody(disp);
            configureDeviceWidget(dw, ref);
            m_displayUpdaters.push_back([disp, cr]() {
                // Pull filename from the effect's stored path
                // (just the basename, not the full path — fits the
                // panel better).
                std::string p = cr->irPath();
                std::string base = p;
                const auto slash = p.find_last_of("/\\");
                if (slash != std::string::npos) base = p.substr(slash + 1);
                disp->setIRName(base);
                disp->setIRWaveform(cr->irData(), cr->irDataLength());
            });
            return true;
        }

        if (id == "neuralamp") {
            // Same CustomDeviceBody pattern as Conv Reverb. Buttons
            // drive an App-side handler that opens an SDL file
            // dialog scoped to .nam, then calls effect->setModelPath
            // (which kicks off the NAM library load + Reset +
            // prewarm inside NeuralAmp::setModelPath).
            auto* disp = new NeuralAmpDisplayPanel();
            auto* na   = static_cast<effects::NeuralAmp*>(fx);
            disp->setOnParamChange([ref](int idx, float v) {
                DeviceRef r = ref; r.setParam(idx, v);
            });
            disp->setOnLoadRequest([this, na]() {
                if (m_onLoadNamModel) m_onLoadNamModel(na);
            });
            disp->setOnClearRequest([na]() { na->setModelPath(""); });
            dw->setCustomBody(disp);
            configureDeviceWidget(dw, ref);
            m_displayUpdaters.push_back([disp, na]() {
                std::string p = na->modelPath();
                std::string base = p;
                const auto slash = p.find_last_of("/\\");
                if (slash != std::string::npos) base = p.substr(slash + 1);
                disp->setModelName(base);
                disp->setLoadedFlag(na->hasModel());
            });
            return true;
        }

        if (id == "filter") {
            auto* disp = new FilterDisplayWidget();
            dw->setCustomPanel(disp, 52.0f, 200.0f);
            configureDeviceWidget(dw, ref);

            m_displayUpdaters.push_back([disp, fx]() {
                auto* flt = static_cast<effects::Filter*>(fx);
                // Cutoff is stored 0..1 → convert to Hz for the display.
                disp->setCutoff(effects::Filter::cutoffNormToHz(
                    flt->getParameter(effects::Filter::kCutoff)));
                // Resonance stored 0.1..20 biquad Q → rough 0..1 normalize
                // so the curve bump scales sensibly.
                const float q = flt->getParameter(effects::Filter::kResonance);
                disp->setResonance(std::clamp((q - 0.1f) / 5.0f, 0.0f, 1.0f));
                disp->setFilterType(
                    static_cast<int>(flt->getParameter(effects::Filter::kType)));
            });
            return true;
        }

        return false;
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
                lfoDisp->setBias(lfo->getParameter(midi::LFO::kBias));
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

    // Drag-drop targets. Set when a Drum Rack / Multisampler
    // display is created so the App-side global drag handler can
    // route audio-clip drops:
    //   * DrumRack:    to the pad under the cursor.
    //   * Multisampler: as a new zone (full key range, default root).
    // Reset on every device-tree rebuild via setupDevicesForTrack.
    fw2::DrumRackDisplayPanel*       m_drumRackDisplay   = nullptr;
    instruments::DrumRack*           m_drumRackInst      = nullptr;
    fw2::MultisamplerDisplayPanel*   m_msDisplay         = nullptr;
    instruments::Multisampler*       m_msInst            = nullptr;
    // Set when an InstrumentRack widget is built so tick() can
    // detect a change in selectedChain() / per-chain fx count and
    // trigger a rebuild — same fingerprint pattern as DrumRack.
    instruments::InstrumentRack*     m_instrackInst      = nullptr;

    // App-wired callback for per-pad fx context menu (right-click).
    // See setOnDrumPadFxMenu().
    DrumPadFxMenuCallback m_onDrumPadFxMenu;
    // Same delegation for InstrumentRack chain right-click. App
    // builds the menu (Add/Remove effect entries) and shows it via
    // ContextMenu::show. Mirrors the per-pad pattern.
    InstrumentRackChainFxMenuCallback m_onInstrackChainFxMenu;

    // Two-stage wiring shim — the Multisampler panel's
    // preferred-width callback needs the GroupedKnobBody*, which
    // doesn't exist when we set up the panel itself. Stash the panel
    // here, then connect once the body is constructed in the same
    // setupInstrumentDisplay call. Always nulled out before returning.
    fw2::MultisamplerDisplayPanel* m_pendingMsPanelWidthHook = nullptr;

    bool m_open         = false;
    bool m_panelFocused = false;

    // Per-track effect-chain latency (samples) + the engine sample
    // rate at which it was measured — used to derive the millisecond
    // display. Set via setTrackLatencySamples().
    int    m_trackLatencySamples    = 0;
    double m_trackLatencySampleRate = 48000.0;

    // Resizable panel height (user-adjustable via handle drag)
    float m_userPanelHeight = kDefaultPanelHeight;
    float m_windowHeight = 800.0f;  // updated via setWindowHeight()

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
    ParamTouchCallback     m_onParamTouch;
    ParamRightClickCallback m_onParamRightClick;
    PresetClickCallback m_onPresetClick;
    AutoSampleRequestCallback m_onAutoSampleRequested;
    TrackNamesProvider          m_trackNamesProvider;
    SidechainSourceProvider     m_sidechainSourceProvider;
    SetSidechainSourceCallback  m_setSidechainSource;
    LoadConvIRCallback          m_onLoadConvIR;
    LoadNamModelCallback        m_onLoadNamModel;

    // Drag-to-reorder state
    bool  m_dragReorderActive = false;
    int   m_dragSourceIdx     = -1;
    int   m_dragInsertIdx     = -1;
    float m_dragStartX        = 0;

    // Fingerprint cache for setDeviceChain
    midi::MidiEffectChain*   m_lastMidiChain = nullptr;
    instruments::Instrument* m_lastInst      = nullptr;
    effects::EffectChain*    m_lastFxChain   = nullptr;
    int m_lastMidiCount  = 0;
    int m_lastFxCount    = 0;
    // DrumRack-only fingerprint pieces. Track the selected pad and
    // the count of effects on its chain so a pad change or a
    // chain-mutation triggers a rebuild via tickRebuildIfNeeded().
    int m_lastPadNote    = -1;
    int m_lastPadFxCount = 0;
    // InstrumentRack-only fingerprint. Same idea as PadNote/FxCount
    // but for the InstrumentRack's selected chain index and that
    // chain's per-chain fx count.
    int m_lastChainIdx   = -1;
    int m_lastChainFxCount = 0;

    // Audio clip view state
    ViewMode m_viewMode = ViewMode::Devices;
    const audio::Clip* m_clipPtr = nullptr;
    int m_clipSampleRate = static_cast<int>(kDefaultSampleRate);
    WaveformWidget m_waveformWidget;
    FwDropDown m_warpModeDropdown;
    FwButton m_detectBtn;
    // Audio-clip view destructive ops on the loop region. Crop
    // truncates the buffer to [loopStart, loopEnd); reverse flips
    // samples in place inside that range. Both clear warp markers
    // and transients (they'd be stale).
    FwButton m_cropLoopBtn;
    FwButton m_reverseLoopBtn;
    FwKnob m_gainKnob;
    FwKnob m_transposeKnob;
    FwKnob m_detuneKnob;
    FwToggle m_loopToggleBtn;
    FwKnob m_bpmKnob;
    float m_lastMouseX = 0, m_lastMouseY = 0;

    // Automation lane editor
    FwDropDown m_autoTargetDropdown;
    AutomationEnvelopeWidget m_autoEnvelopeWidget;
    int m_autoSelectedLaneIdx = -1;
    std::vector<automation::AutomationLane>* m_clipAutoLanes = nullptr;
    int m_autoTrackIndex = -1;

    // MIDI Learn
    midi::MidiLearnManager* m_learnManager = nullptr;

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
        const std::string prevText = m_autoTargetDropdown.selectedLabel();
        m_autoTargetDropdown.setItems(items);
        int restored = 0;
        if (!prevText.empty() && prevText != "(none)") {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i] == prevText) { restored = i; break; }
            }
        }
        m_autoTargetDropdown.setSelectedIndex(restored);
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
    void paintAudioClipView(Renderer2D&, TextMetrics&, float, float, float, float, UIContext&) {}
#else
    void paintAudioClipView(Renderer2D& renderer, TextMetrics& tm,
                            float x, float bodyY, float w, float bodyH,
                            UIContext& ctx);
#endif
};

} // namespace fw2
} // namespace ui
} // namespace yawn
