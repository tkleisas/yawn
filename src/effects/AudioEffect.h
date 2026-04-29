#pragma once

#include "core/ParameterInfo.h"
#include <cstdint>

namespace yawn {
namespace effects {

using ParameterInfo = ::yawn::ParameterInfo;

class AudioEffect {
public:
    virtual ~AudioEffect() = default;

    // Lifecycle — init() preallocates all memory, reset() clears state
    virtual void init(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    // Process interleaved stereo buffer in-place
    virtual void process(float* buffer, int numFrames, int numChannels) = 0;

    // Parameter access
    virtual int parameterCount() const = 0;
    virtual const ParameterInfo& parameterInfo(int index) const = 0;
    virtual float getParameter(int index) const = 0;
    virtual void setParameter(int index, float value) = 0;

    // Identity
    virtual const char* name() const = 0;
    virtual const char* id() const = 0;

    // Bypass
    bool bypassed() const { return m_bypassed; }
    void setBypassed(bool b) { m_bypassed = b; }

    // Wet/dry mix (0.0 = fully dry, 1.0 = fully wet)
    float mix() const { return m_mix; }
    void setMix(float m) { m_mix = (m < 0.0f) ? 0.0f : (m > 1.0f) ? 1.0f : m; }

    // Visualization support (override in visualizer effects)
    virtual bool isVisualizer() const { return false; }
    virtual const char* visualizerType() const { return ""; }
    virtual const float* displayData() const { return nullptr; }
    virtual int displaySize() const { return 0; }
    virtual bool hasNewData() const { return false; }
    virtual void clearNewData() {}

    // ── Sidechain input ────────────────────────────────────────────
    // Mirrors Instrument's sidechain plumbing — set by AudioEngine
    // each buffer cycle to point at the source track's interleaved
    // stereo buffer (or nullptr when the effect has no sidechain
    // routed). Effects that don't read it just ignore the pointer.
    // supportsSidechain() advertises the capability so the routing
    // UI can hide / disable the picker for effects that don't use
    // it (the same convention Instrument uses).
    void setSidechainInput(const float* buffer) { m_sidechainBuffer = buffer; }
    const float* sidechainInput() const { return m_sidechainBuffer; }
    virtual bool supportsSidechain() const { return false; }

    // ── Modulation-source output ───────────────────────────────────
    // Effects that produce a control-rate output for routing to
    // other devices' parameters (Envelope Follower, future LFO-as-
    // audio-effect, etc.) override hasModulationOutput() and
    // modulationValue(). The audio engine polls modulationValue()
    // once per block and feeds it into the modulation matrix the
    // same way LFO MidiEffect output is consumed today. Default:
    // no modulation output (LFO-style MIDI effects still own that
    // role for synths; this is the audio-side counterpart).
    virtual bool  hasModulationOutput() const { return false; }
    virtual float modulationValue() const { return 0.0f; }

protected:
    double m_sampleRate = kDefaultSampleRate;
    int    m_maxBlockSize = 4096;
    bool   m_bypassed = false;
    float  m_mix = 1.0f;
    const float* m_sidechainBuffer = nullptr;
};

} // namespace effects
} // namespace yawn
