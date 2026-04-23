#pragma once

// AudioEffect — abstract base class for all audio effects.
// All values stored as float, processed in interleaved stereo buffers.
// Effects must be real-time safe: no allocations in process(), all memory preallocated in init().

#include "WidgetHint.h"
#include <cstdint>

namespace yawn {
namespace effects {

struct ParameterInfo {
    const char* name;
    float minValue;
    float maxValue;
    float defaultValue;
    const char* unit;       // "dB", "ms", "Hz", "%", ""
    bool isBoolean;         // If true, 0.0 = off, 1.0 = on
    bool isPerVoice = false;
    WidgetHint widgetHint = WidgetHint::Knob;
    const char* const* valueLabels = nullptr;
    int valueLabelCount = 0;

    // Optional custom display formatter. When set, overrides the unit-
    // based default in the knob UI. Used for log-mapped params that store
    // 0..1 for uniform modulation but are displayed in natural units
    // (e.g. filter cutoff 0..1 shown as "5.0k Hz").
    using FormatFn = void (*)(float value, char* buf, int bufSize);
    FormatFn formatFn = nullptr;
};

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

protected:
    double m_sampleRate = 44100.0;
    int    m_maxBlockSize = 4096;
    bool   m_bypassed = false;
    float  m_mix = 1.0f;
};

} // namespace effects
} // namespace yawn
