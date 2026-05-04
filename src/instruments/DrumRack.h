#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "effects/EffectChain.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// Drum Rack — pad-based percussion instrument (like Ableton Drum Rack).
// 128 pads (one per MIDI note 0-127), each with a sample, volume, pan, pitch.
// Monophonic per pad (retrigger on repeated hits), all pads play simultaneously.
class DrumRack : public Instrument {
public:
    static constexpr int kNumPads = 128;

    struct Pad {
        std::vector<float> sampleData;
        int   sampleFrames    = 0;
        int   sampleChannels  = 1;
        float volume          = 1.0f;
        float pan             = 0.0f;
        float pitchAdjust     = 0.0f; // semitones

        // Choke group. 0 = no group (pad never chokes anything). 1..4
        // = membership in that mute group — triggering ANY pad in
        // group N stops every other pad already playing in group N.
        // Classic use: closed + open hi-hats sharing a group so
        // closing the hat cuts off the open. 4 groups is enough for
        // the kits this rack is sized for.
        uint8_t chokeGroup    = 0;

        // Per-pad amplitude envelope. AR (attack-release) shape —
        // we run the underlying ADSR with sustain=0 so decay rolls
        // straight to silence, no hold stage. Defaults: 0.001s
        // attack (instant — preserves the sample's natural
        // transient), 10s decay (long enough that most one-shot
        // samples just play through naturally — user shortens to
        // gate / shape the tail).
        float    attackS = 0.001f;
        float    decayS  = 10.0f;
        Envelope env;

        // Playback region within the loaded sample. Both stored
        // normalised (0..1 fraction of sampleFrames). Defaults
        // = full sample. Trim either end to gate to a transient,
        // skip a noisy tail, or chop a long sample to taste
        // without re-recording. start >= end disables playback
        // (the render loop checks endFrame > startFrame).
        float    startNorm = 0.0f;
        float    endNorm   = 1.0f;

        // Per-pad audio effect chain. Lazy-allocated — nullptr
        // until the user adds the first effect to this pad, so
        // unused pads cost zero memory + zero CPU. When present,
        // the chain processes the pad's PRE-MIX signal: pad
        // volume × velocity × pan × env × sample → fx → sum into
        // master. Pre-fx pan/velocity feels natural for compressors
        // and saturation; m_volume (rack master) is applied AFTER
        // the chain on accumulation.
        //
        // Lifetime: the chain stays alive even when the pad is not
        // playing — that's the whole point of "let fx tail keep
        // ringing" after a choke or natural end. process() always
        // runs the chain when it exists, with zero input when the
        // pad isn't currently rendering audio.
        std::unique_ptr<effects::EffectChain> fx;

        // Playback state
        bool   playing   = false;
        double playPos   = 0.0;
        // Signed: positive = forward, negative = reverse. Set at
        // note-on from pitch + (endNorm < startNorm) → reverse.
        double playSpeed = 1.0;
        // End-of-region in sample frames, computed at note-on.
        // The render loop bails when playPos crosses this in the
        // playback direction (pos >= endFrameI for forward,
        // pos <= endFrameI for reverse).
        int    endFrameI = 0;
        float  velocity  = 1.0f;
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    // Load a sample into a pad. Note = MIDI note number (0-127).
    void loadPad(int note, const float* data, int numFrames, int numChannels) {
        if (note < 0 || note >= kNumPads || !data || numFrames <= 0) return;
        auto& pad = m_pads[note];
        pad.sampleData.assign(data, data + numFrames * numChannels);
        pad.sampleFrames = numFrames;
        pad.sampleChannels = numChannels;
    }

    void clearPad(int note) {
        if (note < 0 || note >= kNumPads) return;
        m_pads[note].sampleData.clear();
        m_pads[note].sampleFrames = 0;
        m_pads[note].sampleChannels = 1;
    }

    void setPadVolume(int note, float v)     { if (note >= 0 && note < kNumPads) m_pads[note].volume = std::clamp(v, 0.0f, 2.0f); }
    void setPadPan(int note, float p)        { if (note >= 0 && note < kNumPads) m_pads[note].pan = std::clamp(p, -1.0f, 1.0f); }
    void setPadPitch(int note, float semi)   { if (note >= 0 && note < kNumPads) m_pads[note].pitchAdjust = std::clamp(semi, -24.0f, 24.0f); }
    void setPadChokeGroup(int note, int g)   { if (note >= 0 && note < kNumPads) m_pads[note].chokeGroup = static_cast<uint8_t>(std::clamp(g, 0, 4)); }
    int  padChokeGroup(int note) const       { return (note >= 0 && note < kNumPads) ? m_pads[note].chokeGroup : 0; }
    void setPadAttack(int note, float s)     { if (note >= 0 && note < kNumPads) m_pads[note].attackS = std::clamp(s, 0.001f, 2.0f); }
    void setPadDecay(int note, float s)      { if (note >= 0 && note < kNumPads) m_pads[note].decayS  = std::clamp(s, 0.001f, 10.0f); }
    float padAttack(int note) const          { return (note >= 0 && note < kNumPads) ? m_pads[note].attackS : 0.001f; }
    float padDecay(int note) const           { return (note >= 0 && note < kNumPads) ? m_pads[note].decayS  : 10.0f; }
    void setPadStart(int note, float n)      { if (note >= 0 && note < kNumPads) m_pads[note].startNorm = std::clamp(n, 0.0f, 1.0f); }
    void setPadEnd(int note, float n)        { if (note >= 0 && note < kNumPads) m_pads[note].endNorm   = std::clamp(n, 0.0f, 1.0f); }
    float padStart(int note) const           { return (note >= 0 && note < kNumPads) ? m_pads[note].startNorm : 0.0f; }
    float padEnd(int note) const             { return (note >= 0 && note < kNumPads) ? m_pads[note].endNorm   : 1.0f; }

    // Per-pad effect chain access. Lazy-allocates on first call so
    // the user can directly start adding effects to the returned
    // chain. Returns nullptr only for an out-of-range note.
    effects::EffectChain* padFxChain(int note) {
        if (note < 0 || note >= kNumPads) return nullptr;
        if (!m_pads[note].fx) {
            m_pads[note].fx = std::make_unique<effects::EffectChain>();
            if (m_sampleRate > 0)
                m_pads[note].fx->init(m_sampleRate, m_maxBlockSize);
        }
        return m_pads[note].fx.get();
    }
    // Const view — returns nullptr if no chain has been allocated
    // (no effects added yet). Project / preset save uses this so
    // unused pads don't pollute JSON.
    const effects::EffectChain* padFxChainOrNull(int note) const {
        if (note < 0 || note >= kNumPads) return nullptr;
        return m_pads[note].fx.get();
    }
    void clearPadFx(int note) {
        if (note >= 0 && note < kNumPads) m_pads[note].fx.reset();
    }

    Pad&       pad(int note)       { return m_pads[note & 0x7F]; }
    const Pad& pad(int note) const { return m_pads[note & 0x7F]; }

    // Selected pad for UI editing
    int  selectedPad() const { return m_selectedPad; }
    void setSelectedPad(int note) { m_selectedPad = std::clamp(note, 0, kNumPads - 1); }

    bool isPadPlaying(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].playing : false;
    }

    bool hasSample(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].sampleFrames > 0 : false;
    }

    // Sample data accessors for waveform display
    const float* padSampleData(int note) const {
        if (note < 0 || note >= kNumPads || m_pads[note].sampleFrames <= 0) return nullptr;
        return m_pads[note].sampleData.data();
    }
    int padSampleFrames(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].sampleFrames : 0;
    }
    int padSampleChannels(int note) const {
        return (note >= 0 && note < kNumPads) ? m_pads[note].sampleChannels : 1;
    }

    const char* name() const override { return "Drum Rack"; }
    const char* id()   const override { return "drumrack"; }

    // Parameter indices
    static constexpr int kVolume     = 0;
    static constexpr int kPadVolume  = 1;
    static constexpr int kPadPan     = 2;
    static constexpr int kPadPitch   = 3;
    static constexpr int kPadChoke   = 4;
    static constexpr int kPadAttack  = 5;
    static constexpr int kPadDecay   = 6;
    static constexpr int kPadStart   = 7;
    static constexpr int kPadEnd     = 8;
    static constexpr int kParamCount = 9;

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int idx) const override {
        static constexpr const char* kChokeLabels[] = {"None", "1", "2", "3", "4"};
        static const InstrumentParameterInfo infos[] = {
            {"Volume",     0,   1,   0.8f, "",   false, false, WidgetHint::DentedKnob},
            {"Pad Volume", 0,   2,   1.0f, "",   false, false, WidgetHint::DentedKnob},
            {"Pad Pan",   -1,   1,   0.0f, "",   false, false, WidgetHint::DentedKnob},
            {"Pad Pitch", -24,  24,  0.0f, "st", false, false, WidgetHint::DentedKnob},
            {"Choke",      0,   4,   0.0f, "",   false, false,
                WidgetHint::StepSelector, kChokeLabels, 5},
            {"Attack",     0.001f, 2.0f,  0.001f, "s", false, false, WidgetHint::Knob},
            {"Decay",      0.001f, 10.0f, 10.0f,  "s", false, false, WidgetHint::Knob},
            {"Start",      0,   1,   0.0f, "",   false, false, WidgetHint::DentedKnob},
            {"End",        0,   1,   1.0f, "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(idx, 0, kParamCount - 1)];
    }

    float getParameter(int idx) const override {
        switch (idx) {
        case kVolume:    return m_volume;
        case kPadVolume: return m_pads[m_selectedPad].volume;
        case kPadPan:    return m_pads[m_selectedPad].pan;
        case kPadPitch:  return m_pads[m_selectedPad].pitchAdjust;
        case kPadChoke:  return static_cast<float>(m_pads[m_selectedPad].chokeGroup);
        case kPadAttack: return m_pads[m_selectedPad].attackS;
        case kPadDecay:  return m_pads[m_selectedPad].decayS;
        case kPadStart:  return m_pads[m_selectedPad].startNorm;
        case kPadEnd:    return m_pads[m_selectedPad].endNorm;
        default:         return 0.0f;
        }
    }

    void setParameter(int idx, float v) override {
        switch (idx) {
        case kVolume:    m_volume = std::clamp(v, 0.0f, 1.0f); break;
        case kPadVolume: m_pads[m_selectedPad].volume = std::clamp(v, 0.0f, 2.0f); break;
        case kPadPan:    m_pads[m_selectedPad].pan = std::clamp(v, -1.0f, 1.0f); break;
        case kPadPitch:  m_pads[m_selectedPad].pitchAdjust = std::clamp(v, -24.0f, 24.0f); break;
        case kPadChoke:  m_pads[m_selectedPad].chokeGroup =
                            static_cast<uint8_t>(std::clamp(static_cast<int>(v + 0.5f), 0, 4));
                         break;
        case kPadAttack: m_pads[m_selectedPad].attackS = std::clamp(v, 0.001f, 2.0f); break;
        case kPadDecay:  m_pads[m_selectedPad].decayS  = std::clamp(v, 0.001f, 10.0f); break;
        case kPadStart:  m_pads[m_selectedPad].startNorm = std::clamp(v, 0.0f, 1.0f); break;
        case kPadEnd:    m_pads[m_selectedPad].endNorm   = std::clamp(v, 0.0f, 1.0f); break;
        }
    }

    // Kit preset save/load. Hooks into the standard
    // saveDevicePreset / loadDevicePreset path: the manager
    // creates an asset directory for this preset and we drop
    // every loaded pad's WAV in there + a JSON sidecar with the
    // per-pad params (vol/pan/pitch/choke/AR/region). Loading
    // wipes existing pads first so partial overlap doesn't leak
    // half-old / half-new state.
    nlohmann::json saveExtraState(
            const std::filesystem::path& assetDir) const override;
    void loadExtraState(const nlohmann::json& state,
                        const std::filesystem::path& assetDir) override;

private:
    Pad   m_pads[kNumPads];
    float m_volume = 0.8f;
    int   m_selectedPad = 36; // Default to C2 (standard GM kick drum)

    // Scratch interleaved-stereo buffer for per-pad fx routing.
    // Each pad with state (playing OR has fx with possibly-still-
    // ringing tail) renders here, then the chain processes it,
    // then we accumulate into the rack's output. Sized in init()
    // for maxBlockSize × 2 channels — capped at the engine's max
    // block size, never reallocated mid-process.
    std::vector<float> m_scratch;
};

} // namespace instruments
} // namespace yawn
