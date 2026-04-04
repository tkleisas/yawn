#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace yawn {
namespace audio {

// Transport state — manages play/stop, BPM, time signature, position.
// Read/written from both UI and audio threads via atomics and message queue.
class Transport {
public:
    static constexpr double kDefaultBPM = 120.0;
    static constexpr double kDefaultSampleRate = 44100.0;
    static constexpr int kDefaultNumerator = 4;
    static constexpr int kDefaultDenominator = 4;

    Transport() = default;

    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }
    double sampleRate() const { return m_sampleRate; }

    // --- Called from audio thread ---

    void play() { m_playing.store(true, std::memory_order_release); }
    void stop() {
        m_playing.store(false, std::memory_order_release);
        m_recording.store(false, std::memory_order_release);
        m_countInRemaining.store(0, std::memory_order_release);
    }

    bool isPlaying() const { return m_playing.load(std::memory_order_acquire); }

    // Recording state
    void startRecording() { m_recording.store(true, std::memory_order_release); }
    void stopRecording() { m_recording.store(false, std::memory_order_release); }
    bool isRecording() const { return m_recording.load(std::memory_order_acquire); }

    // Count-in
    void setCountInBars(int bars) { m_countInBars.store(bars, std::memory_order_release); }
    int countInBars() const { return m_countInBars.load(std::memory_order_acquire); }

    void beginCountIn() {
        int bars = countInBars();
        if (bars > 0) {
            int64_t countInSamples = static_cast<int64_t>(samplesPerBar() * bars);
            m_countInRemaining.store(countInSamples, std::memory_order_release);
        } else {
            m_countInRemaining.store(0, std::memory_order_release);
        }
    }

    bool isCountingIn() const {
        return m_countInRemaining.load(std::memory_order_acquire) > 0;
    }

    double countInProgress() const {
        int bars = countInBars();
        if (bars <= 0) return 1.0;
        int64_t total = static_cast<int64_t>(samplesPerBar() * bars);
        int64_t remaining = m_countInRemaining.load(std::memory_order_acquire);
        if (total <= 0) return 1.0;
        return 1.0 - static_cast<double>(remaining) / total;
    }

    void setBPM(double bpm) { m_bpm.store(bpm, std::memory_order_release); }
    double bpm() const { return m_bpm.load(std::memory_order_acquire); }

    // Time signature: numerator / denominator (e.g. 3/4, 6/8, 7/8)
    void setTimeSignature(int numerator, int denominator) {
        m_numerator.store(std::max(1, numerator), std::memory_order_release);
        m_denominator.store(std::max(1, denominator), std::memory_order_release);
    }
    int numerator()   const { return m_numerator.load(std::memory_order_acquire); }
    int denominator() const { return m_denominator.load(std::memory_order_acquire); }

    // Loop range (in beats)
    void setLoopEnabled(bool e) { m_loopEnabled.store(e, std::memory_order_release); }
    bool isLoopEnabled() const { return m_loopEnabled.load(std::memory_order_acquire); }

    void setLoopRange(double startBeats, double endBeats) {
        m_loopStartBeats.store(startBeats, std::memory_order_release);
        m_loopEndBeats.store(endBeats, std::memory_order_release);
    }
    double loopStartBeats() const { return m_loopStartBeats.load(std::memory_order_acquire); }
    double loopEndBeats() const { return m_loopEndBeats.load(std::memory_order_acquire); }

    // Returns true if the transport wrapped around the loop point this advance.
    bool didLoopWrap() const { return m_didLoopWrap; }

    // Advance transport position by numFrames. Called each audio callback.
    void advance(int numFrames) {
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

    void setPositionInSamples(int64_t pos) {
        m_positionInSamples.store(pos, std::memory_order_release);
    }

    int64_t positionInSamples() const {
        return m_positionInSamples.load(std::memory_order_acquire);
    }

    // Convert sample position to beats (quarter notes)
    double positionInBeats() const {
        double pos = static_cast<double>(positionInSamples());
        double currentBpm = bpm();
        double sr = m_sampleRate;
        if (sr <= 0.0 || currentBpm <= 0.0) return 0.0;
        return (pos / sr) * (currentBpm / 60.0);
    }

    // Samples per beat (quarter note) at current BPM
    double samplesPerBeat() const {
        double currentBpm = bpm();
        if (currentBpm <= 0.0) return 0.0;
        return m_sampleRate * 60.0 / currentBpm;
    }

    // Samples per bar based on time signature numerator
    double samplesPerBar() const {
        return samplesPerBeat() * numerator();
    }

    // Number of beats (quarter notes) per bar from time signature
    int beatsPerBar() const { return numerator(); }

    void reset() {
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

private:
    double m_sampleRate = kDefaultSampleRate;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_recording{false};
    std::atomic<double> m_bpm{kDefaultBPM};
    std::atomic<int64_t> m_positionInSamples{0};
    std::atomic<int> m_numerator{kDefaultNumerator};
    std::atomic<int> m_denominator{kDefaultDenominator};
    std::atomic<int> m_countInBars{0};
    std::atomic<int64_t> m_countInRemaining{0};
    std::atomic<bool> m_loopEnabled{false};
    std::atomic<double> m_loopStartBeats{0.0};
    std::atomic<double> m_loopEndBeats{0.0};
    bool m_didLoopWrap = false; // non-atomic, only written/read on audio thread
};

} // namespace audio
} // namespace yawn
