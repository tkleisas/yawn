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
    }

    bool isPlaying() const { return m_playing.load(std::memory_order_acquire); }

    void setBPM(double bpm) { m_bpm.store(bpm, std::memory_order_release); }
    double bpm() const { return m_bpm.load(std::memory_order_acquire); }

    // Time signature: numerator / denominator (e.g. 3/4, 6/8, 7/8)
    void setTimeSignature(int numerator, int denominator) {
        m_numerator.store(std::max(1, numerator), std::memory_order_release);
        m_denominator.store(std::max(1, denominator), std::memory_order_release);
    }
    int numerator()   const { return m_numerator.load(std::memory_order_acquire); }
    int denominator() const { return m_denominator.load(std::memory_order_acquire); }

    // Advance transport position by numFrames. Called each audio callback.
    void advance(int numFrames) {
        if (m_playing.load(std::memory_order_acquire)) {
            m_positionInSamples.fetch_add(numFrames, std::memory_order_relaxed);
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
        m_positionInSamples.store(0, std::memory_order_release);
        m_bpm.store(kDefaultBPM, std::memory_order_release);
        m_numerator.store(kDefaultNumerator, std::memory_order_release);
        m_denominator.store(kDefaultDenominator, std::memory_order_release);
    }

private:
    double m_sampleRate = kDefaultSampleRate;
    std::atomic<bool> m_playing{false};
    std::atomic<double> m_bpm{kDefaultBPM};
    std::atomic<int64_t> m_positionInSamples{0};
    std::atomic<int> m_numerator{kDefaultNumerator};
    std::atomic<int> m_denominator{kDefaultDenominator};
};

} // namespace audio
} // namespace yawn
