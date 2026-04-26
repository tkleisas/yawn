#pragma once

#include "midi/MidiTypes.h"
#include "core/ParameterInfo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yawn {
namespace instruments {

using InstrumentParameterInfo = ::yawn::ParameterInfo;

inline float noteToFreq(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

inline float velocityToGain(uint16_t vel16) {
    return (float)vel16 / 65535.0f;
}

// Abstract base class for all instruments (synths, samplers, racks).
// Instruments consume MIDI and produce interleaved stereo audio.
class Instrument {
public:
    virtual ~Instrument() = default;

    virtual void init(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    // Render audio from MIDI input. Output is ADDED to buffer (interleaved stereo).
    virtual void process(float* buffer, int numFrames, int numChannels,
                         const midi::MidiBuffer& midi) = 0;

    virtual const char* name() const = 0;
    virtual const char* id()   const = 0;

    virtual int parameterCount() const = 0;
    virtual const InstrumentParameterInfo& parameterInfo(int index) const = 0;
    virtual float getParameter(int index) const = 0;
    virtual void  setParameter(int index, float value) = 0;

    bool bypassed() const { return m_bypassed; }
    void setBypassed(bool b) { m_bypassed = b; }

    // Sidechain input: set by AudioEngine each buffer cycle.
    // Points to the interleaved stereo buffer of the source track (or nullptr).
    void setSidechainInput(const float* buffer) { m_sidechainBuffer = buffer; }
    const float* sidechainInput() const { return m_sidechainBuffer; }
    virtual bool supportsSidechain() const { return false; }

protected:
    double m_sampleRate   = 44100.0;
    int    m_maxBlockSize = 256;
    bool   m_bypassed     = false;
    const float* m_sidechainBuffer = nullptr;
};

} // namespace instruments
} // namespace yawn
