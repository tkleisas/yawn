#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

// Controls when the metronome sounds
enum class MetronomeMode : int {
    Always    = 0,  // play during both playback and recording
    RecordOnly = 1, // play only while recording
    PlayOnly   = 2, // play only during playback (not recording)
    Off        = 3  // never play (overrides enabled)
};

class Metronome {
public:
    void init(double sampleRate, int /*maxBlockSize*/) {
        m_sampleRate = sampleRate;
        generateClicks();
        m_clickPos = -1;
    }

    void setEnabled(bool e) { m_enabled = e; }
    bool enabled() const { return m_enabled; }

    void setVolume(float v) { m_volume = std::clamp(v, 0.0f, 1.0f); }
    float volume() const { return m_volume; }

    void setBeatsPerBar(int b) { m_beatsPerBar = std::max(1, b); }
    int beatsPerBar() const { return m_beatsPerBar; }

    void setMode(MetronomeMode m) { m_mode = m; }
    MetronomeMode mode() const { return m_mode; }

    void reset() {
        m_clickPos = -1;
        m_clickAccent = false;
    }

    // Call from audio thread: adds click on top of output buffer.
    // Must be called BEFORE transport.advance() so positionInSamples is current.
    void process(float* output, int numFrames, int numChannels,
                 double bpm, int64_t positionInSamples, bool playing,
                 bool recording = false) {
        if (!m_enabled || m_mode == MetronomeMode::Off) return;

        // Check mode constraints
        if (m_mode == MetronomeMode::RecordOnly && !recording) {
            if (m_clickPos >= 0) mixClickRange(output, numChannels, 0, numFrames);
            return;
        }
        if (m_mode == MetronomeMode::PlayOnly && recording) {
            if (m_clickPos >= 0) mixClickRange(output, numChannels, 0, numFrames);
            return;
        }

        // Finish any ongoing click even if transport stopped
        if (!playing || bpm <= 0.0) {
            if (m_clickPos >= 0)
                mixClickRange(output, numChannels, 0, numFrames);
            return;
        }

        double samplesPerBeat = m_sampleRate * 60.0 / bpm;
        int64_t bufferEnd = positionInSamples + numFrames;

        // Collect beat events that fall within this buffer
        struct BeatEvent { int frame; bool accent; };
        BeatEvent events[32];
        int numEvents = 0;

        double startBeatExact = (double)positionInSamples / samplesPerBeat;
        int firstBeatIdx = (int)std::ceil(startBeatExact - 1e-9);
        if (firstBeatIdx < 0) firstBeatIdx = 0;

        for (int b = firstBeatIdx; numEvents < 32; ++b) {
            int64_t beatSample = (int64_t)((double)b * samplesPerBeat + 0.5);
            if (beatSample >= bufferEnd) break;
            if (beatSample < positionInSamples) continue;
            int frame = (int)(beatSample - positionInSamples);
            events[numEvents++] = { frame, (b % m_beatsPerBar == 0) };
        }

        // Mix ongoing click up to first event, then trigger new clicks
        int currentFrame = 0;

        for (int e = 0; e < numEvents; ++e) {
            int eventFrame = events[e].frame;

            if (m_clickPos >= 0 && eventFrame > currentFrame)
                mixClickRange(output, numChannels, currentFrame, eventFrame);

            // Trigger new click
            m_clickPos = 0;
            m_clickAccent = events[e].accent;
            currentFrame = eventFrame;
        }

        // Mix remaining click to end of buffer
        if (m_clickPos >= 0)
            mixClickRange(output, numChannels, currentFrame, numFrames);
    }

    const std::vector<float>& accentClick() const { return m_accentClick; }
    const std::vector<float>& normalClick() const { return m_normalClick; }

private:
    void generateClicks() {
        // Accent click: 1500 Hz, 25ms, sharp exponential decay
        int accentLen = (int)(m_sampleRate * 0.025);
        m_accentClick.resize(accentLen);
        for (int i = 0; i < accentLen; ++i) {
            double t = (double)i / m_sampleRate;
            double env = std::exp(-t * 80.0);
            m_accentClick[i] = (float)(std::sin(2.0 * M_PI * 1500.0 * t) * env * 0.8);
        }

        // Normal click: 800 Hz, 20ms, softer
        int normalLen = (int)(m_sampleRate * 0.020);
        m_normalClick.resize(normalLen);
        for (int i = 0; i < normalLen; ++i) {
            double t = (double)i / m_sampleRate;
            double env = std::exp(-t * 100.0);
            m_normalClick[i] = (float)(std::sin(2.0 * M_PI * 800.0 * t) * env * 0.6);
        }
    }

    void mixClickRange(float* output, int numChannels, int fromFrame, int toFrame) {
        const auto& click = m_clickAccent ? m_accentClick : m_normalClick;
        int clickLen = (int)click.size();

        for (int f = fromFrame; f < toFrame && m_clickPos >= 0; ++f) {
            if (m_clickPos < clickLen) {
                float sample = click[m_clickPos] * m_volume;
                for (int ch = 0; ch < numChannels; ++ch)
                    output[f * numChannels + ch] += sample;
                ++m_clickPos;
            } else {
                m_clickPos = -1;
            }
        }
    }

    double m_sampleRate = 44100.0;
    bool m_enabled = false;
    float m_volume = 0.7f;
    int m_beatsPerBar = 4;
    MetronomeMode m_mode = MetronomeMode::Always;

    std::vector<float> m_accentClick;
    std::vector<float> m_normalClick;

    int m_clickPos = -1;
    bool m_clickAccent = false;
};

} // namespace audio
} // namespace yawn
