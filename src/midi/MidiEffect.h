#pragma once

#include "midi/MidiTypes.h"
#include <atomic>
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
    bool isPerVoice     = false;
};

// Abstract base class for all MIDI effects.
// Processes a MidiBuffer in-place on the audio thread.
class MidiEffect {
public:
    virtual ~MidiEffect() = default;

    // Stable instance ID — unique per effect, survives reordering
    uint32_t instanceId() const { return m_instanceId; }
    void setInstanceId(uint32_t id) {
        m_instanceId = id;
        // Keep counter ahead of any loaded ID
        uint32_t cur = s_nextInstanceId.load(std::memory_order_relaxed);
        while (id >= cur) {
            if (s_nextInstanceId.compare_exchange_weak(cur, id + 1, std::memory_order_relaxed))
                break;
        }
    }

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

    // --- Modulation output (for LFO and similar modulators) ---
    // Override in effects that produce modulation values for other parameters.
    // targetType: 0=Instrument, 1=AudioEffect, 2=MidiEffect, 3=Mixer
    virtual bool  hasModulationOutput() const { return false; }
    virtual float modulationValue()     const { return 0.0f; }
    virtual int   modulationTargetType()  const { return 0; }
    virtual int   modulationTargetChain() const { return 0; }
    virtual int   modulationTargetParam() const { return 0; }

    // --- Phase linking (for LFOs and similar oscillating modulators) ---
    // Links reference the leader's stable instanceId rather than slot indices.
    virtual bool     isLinkedToSource()  const { return false; }
    virtual uint32_t linkSourceId()      const { return 0; }
    virtual double   currentPhase()      const { return 0.0; }
    virtual void     overridePhase(double /*leaderPhase*/) {}

protected:
    bool   m_bypassed   = false;
    double m_sampleRate = 44100.0;

private:
    static inline std::atomic<uint32_t> s_nextInstanceId{1};
    uint32_t m_instanceId = s_nextInstanceId.fetch_add(1, std::memory_order_relaxed);
};

} // namespace midi
} // namespace yawn
