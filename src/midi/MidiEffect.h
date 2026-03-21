#pragma once

#include "midi/MidiTypes.h"
#include <cstdint>

namespace yawn {
namespace midi {

// Transport state snapshot passed to MIDI effects each buffer
struct TransportInfo {
    double bpm              = 120.0;
    double sampleRate       = 44100.0;
    double positionInBeats  = 0.0;
    int64_t positionInSamples = 0;
    double samplesPerBeat   = 22050.0;
    bool   playing          = false;
    int    beatsPerBar      = 4;
    int    beatDenominator  = 4;
};

struct MidiEffectParameterInfo {
    const char* name    = "";
    float minValue      = 0.0f;
    float maxValue      = 1.0f;
    float defaultValue  = 0.0f;
    const char* unit    = "";
    bool isBoolean      = false;
};

// Abstract base class for all MIDI effects.
// Processes a MidiBuffer in-place on the audio thread.
class MidiEffect {
public:
    virtual ~MidiEffect() = default;

    virtual void init(double sampleRate) = 0;
    virtual void reset() = 0;

    // Transform MIDI messages in-place. numFrames = audio buffer size for timing.
    virtual void process(MidiBuffer& buffer, int numFrames,
                         const TransportInfo& transport) = 0;

    virtual const char* name() const = 0;
    virtual const char* id()   const = 0;

    virtual int parameterCount() const = 0;
    virtual const MidiEffectParameterInfo& parameterInfo(int index) const = 0;
    virtual float getParameter(int index) const = 0;
    virtual void  setParameter(int index, float value) = 0;

    bool bypassed() const { return m_bypassed; }
    void setBypassed(bool b) { m_bypassed = b; }

protected:
    bool   m_bypassed   = false;
    double m_sampleRate = 44100.0;
};

} // namespace midi
} // namespace yawn
