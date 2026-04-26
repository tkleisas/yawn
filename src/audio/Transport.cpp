#include "audio/Transport.h"

namespace yawn {
namespace audio {

void Transport::stop() {
    m_playing.store(false, std::memory_order_release);
    m_recording.store(false, std::memory_order_release);
    m_countInRemaining.store(0, std::memory_order_release);
    // Counter increments on every stop() call — even when already
    // stopped — so main-thread observers can detect the "Stop
    // button was pressed" event rather than just "transport went
    // from playing to not-playing" (which is only a subset).
    m_stopCounter.fetch_add(1, std::memory_order_release);
}

void Transport::beginCountIn() {
    int bars = countInBars();
    if (bars > 0) {
        int64_t countInSamples = static_cast<int64_t>(samplesPerBar() * bars);
        m_countInRemaining.store(countInSamples, std::memory_order_release);
    } else {
        m_countInRemaining.store(0, std::memory_order_release);
    }
}

int64_t Transport::countInElapsedSamples() const {
    int bars = countInBars();
    if (bars <= 0) return 0;
    int64_t total = static_cast<int64_t>(samplesPerBar() * bars);
    int64_t remaining = m_countInRemaining.load(std::memory_order_acquire);
    return total - remaining;
}

double Transport::countInProgress() const {
    int bars = countInBars();
    if (bars <= 0) return 1.0;
    int64_t total = static_cast<int64_t>(samplesPerBar() * bars);
    int64_t remaining = m_countInRemaining.load(std::memory_order_acquire);
    if (total <= 0) return 1.0;
    return 1.0 - static_cast<double>(remaining) / total;
}

void Transport::setTimeSignature(int numerator, int denominator) {
    m_numerator.store(std::max(1, numerator), std::memory_order_release);
    m_denominator.store(std::max(1, denominator), std::memory_order_release);
}

void Transport::setLoopRange(double startBeats, double endBeats) {
    m_loopStartBeats.store(startBeats, std::memory_order_release);
    m_loopEndBeats.store(endBeats, std::memory_order_release);
}

void Transport::advance(int numFrames) {
    m_didLoopWrap = false;
    // Handle count-in first
    int64_t countIn = m_countInRemaining.load(std::memory_order_acquire);
    if (countIn > 0) {
        int64_t newCountIn = countIn - numFrames;
        if (newCountIn <= 0) {
            m_countInRemaining.store(0, std::memory_order_release);
        } else {
            m_countInRemaining.store(newCountIn, std::memory_order_release);
        }
        return; // Don't advance position during count-in
    }
    if (m_playing.load(std::memory_order_acquire)) {
        m_positionInSamples.fetch_add(numFrames, std::memory_order_relaxed);

        // Loop wrap
        if (m_loopEnabled.load(std::memory_order_acquire)) {
            double loopEnd = m_loopEndBeats.load(std::memory_order_acquire);
            if (loopEnd > 0.0 && positionInBeats() >= loopEnd) {
                double loopStart = m_loopStartBeats.load(std::memory_order_acquire);
                int64_t startSamples = static_cast<int64_t>(loopStart * samplesPerBeat());
                m_positionInSamples.store(startSamples, std::memory_order_release);
                m_didLoopWrap = true;
            }
        }
    }
}

double Transport::positionInBeats() const {
    double pos = static_cast<double>(positionInSamples());
    double currentBpm = bpm();
    double sr = m_sampleRate;
    if (sr <= 0.0 || currentBpm <= 0.0) return 0.0;
    return (pos / sr) * (currentBpm / 60.0);
}

double Transport::samplesPerBeat() const {
    double currentBpm = bpm();
    if (currentBpm <= 0.0) return 0.0;
    return m_sampleRate * 60.0 / currentBpm;
}

double Transport::samplesPerBar() const {
    return samplesPerBeat() * numerator();
}

void Transport::reset() {
    m_playing.store(false, std::memory_order_release);
    m_recording.store(false, std::memory_order_release);
    m_positionInSamples.store(0, std::memory_order_release);
    m_bpm.store(kDefaultBPM, std::memory_order_release);
    m_numerator.store(kDefaultNumerator, std::memory_order_release);
    m_denominator.store(kDefaultDenominator, std::memory_order_release);
    m_countInRemaining.store(0, std::memory_order_release);
    m_loopEnabled.store(false, std::memory_order_release);
    m_loopStartBeats.store(0.0, std::memory_order_release);
    m_loopEndBeats.store(0.0, std::memory_order_release);
    m_didLoopWrap = false;
}

} // namespace audio
} // namespace yawn
