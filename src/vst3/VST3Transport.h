#pragma once

#ifdef YAWN_HAS_VST3
#include "audio/Transport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <cmath>

namespace yawn::vst3 {

// One immutable snapshot per engine block, shared by instruments and effects,
// including devices nested inside racks. Thread-local storage keeps offline
// rendering independent of other engines and avoids locks on the audio thread.
inline thread_local const Steinberg::Vst::ProcessContext* currentTransport = nullptr;

inline Steinberg::Vst::ProcessContext makeProcessContext(
    const audio::Transport& transport, int64_t continuousSamples) {
    using Context = Steinberg::Vst::ProcessContext;
    Context context{};
    context.sampleRate = transport.sampleRate();
    context.projectTimeSamples = transport.positionInSamples();
    context.continousTimeSamples = continuousSamples;
    context.tempo = transport.bpm();
    context.projectTimeMusic = transport.positionInBeats();
    context.timeSigNumerator = transport.numerator();
    context.timeSigDenominator = transport.denominator();
    const double barLength = context.timeSigNumerator * 4.0 / context.timeSigDenominator;
    context.barPositionMusic = std::floor(context.projectTimeMusic / barLength) * barLength;
    context.cycleStartMusic = transport.loopStartBeats();
    context.cycleEndMusic = transport.loopEndBeats();
    context.state = Context::kTempoValid | Context::kTimeSigValid
                  | Context::kProjectTimeMusicValid | Context::kBarPositionValid
                  | Context::kCycleValid | Context::kContTimeValid;
    if (transport.isPlaying() && !transport.isCountingIn()) {
        context.state |= Context::kPlaying;
        if (transport.isRecording()) context.state |= Context::kRecording;
    }
    if (transport.isLoopEnabled()) context.state |= Context::kCycleActive;
    return context;
}

class TransportScope {
public:
    TransportScope(const audio::Transport& transport, int64_t continuousSamples)
        : m_context(makeProcessContext(transport, continuousSamples)),
          m_previous(currentTransport) { currentTransport = &m_context; }
    ~TransportScope() { currentTransport = m_previous; }
    TransportScope(const TransportScope&) = delete;
    TransportScope& operator=(const TransportScope&) = delete;
private:
    Steinberg::Vst::ProcessContext m_context;
    const Steinberg::Vst::ProcessContext* m_previous;
};

// Standalone device previews have no engine transport. Keep a valid stopped
// context for plugins that dereference it unconditionally.
inline Steinberg::Vst::ProcessContext processContext(double sampleRate) {
    if (currentTransport) {
        auto context = *currentTransport;
        // Effect chains may run at an oversampled rate. Musical time stays
        // unchanged, but VST sample timestamps use the processor's clock.
        const double ratio = sampleRate / context.sampleRate;
        context.projectTimeSamples = static_cast<int64_t>(std::llround(context.projectTimeSamples * ratio));
        context.continousTimeSamples = static_cast<int64_t>(std::llround(context.continousTimeSamples * ratio));
        context.sampleRate = sampleRate;
        return context;
    }
    audio::Transport stopped;
    stopped.setSampleRate(sampleRate);
    return makeProcessContext(stopped, 0);
}
} // namespace yawn::vst3
#endif
