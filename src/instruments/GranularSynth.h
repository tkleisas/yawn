#pragma once
// GranularSynth.h — Granular synthesis instrument for YAWN
// Spawns overlapping grains from a loaded sample buffer, triggered by MIDI.
// Grains are windowed, pitch-shifted, and can be position-modulated.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <climits>
#include <algorithm>

namespace yawn::instruments {

class GranularSynth : public Instrument {
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kMaxGrains = 64;
    static constexpr int kParamCount = 16;

    enum Param {
        kPosition = 0,   // 0–1, position in sample
        kGrainSize,      // 10–500 ms
        kDensity,        // 1–40 grains/sec
        kSpread,         // 0–1, random position variation
        kPitch,          // -24 to +24 semitones
        kSpray,          // 0–100 ms, random time jitter
        kShape,          // 0=Hanning, 1=Triangle, 2=Gaussian, 3=Tukey
        kScan,           // -1 to +1, auto-advance rate
        kFilterCutoff,   // 200–20000 Hz
        kFilterReso,     // 0–1
        kAttack,         // 1–5000 ms
        kRelease,        // 1–5000 ms
        kStereoWidth,    // 0–1
        kReverse,        // 0 or 1 (boolean)
        kJitter,         // 0–1, pitch randomization
        kVolume,         // 0–1
    };

    const char* name() const override { return "Granular Synth"; }
    const char* id() const override { return "granular"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& v : m_voices) v = Voice{};
        for (auto& g : m_grains) g = Grain{};
        resetParams();
    }

    void reset() override {
        for (auto& v : m_voices) v.active = false;
        for (auto& g : m_grains) g.active = false;
        m_grainTimer = 0.0;
        m_scanPos = 0.0;
        m_rngState = 12345u;
    }

    void loadSample(const float* data, int numFrames, int numChannels,
                    int rootNote = 60) {
        m_sampleData.assign(data, data + numFrames * numChannels);
        m_sampleFrames = numFrames;
        m_sampleChannels = numChannels;
        m_rootNote = rootNote;
    }

    void clearSample() {
        m_sampleData.clear();
        m_sampleFrames = 0;
        m_sampleChannels = 1;
    }

    bool hasSample() const { return m_sampleFrames > 0; }
    int sampleFrames() const { return m_sampleFrames; }
    int sampleChannels() const { return m_sampleChannels; }
    const float* sampleData() const { return m_sampleData.empty() ? nullptr : m_sampleData.data(); }

    // Grain position info for UI playhead
    float scanPosition() const { return static_cast<float>(m_scanPos); }
    float currentPosition() const { return m_params[kPosition]; }
    bool isPlaying() const {
        for (auto& v : m_voices) if (v.active) return true;
        return false;
    }

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo info[kParamCount] = {
            {"Position",      0.0f,   1.0f,   0.5f,  "",    false},
            {"Grain Size",   10.0f, 500.0f, 100.0f,  "ms",  false},
            {"Density",       1.0f,  40.0f,   8.0f,  "g/s", false},
            {"Spread",        0.0f,   1.0f,   0.1f,  "",    false},
            {"Pitch",       -24.0f,  24.0f,   0.0f,  "st",  false},
            {"Spray",         0.0f, 100.0f,  10.0f,  "ms",  false},
            {"Shape",         0.0f,   3.0f,   0.0f,  "",    false},
            {"Scan",         -1.0f,   1.0f,   0.0f,  "",    false},
            {"Filter Cut",  200.0f, 20000.0f, 20000.0f, "Hz", false},
            {"Filter Reso",   0.0f,   1.0f,   0.0f,  "",    false},
            {"Attack",        1.0f, 5000.0f,  10.0f, "ms",  false},
            {"Release",       1.0f, 5000.0f, 200.0f, "ms",  false},
            {"Stereo Width",  0.0f,   1.0f,   0.5f,  "",    false},
            {"Reverse",       0.0f,   1.0f,   0.0f,  "",    true},
            {"Jitter",        0.0f,   1.0f,   0.0f,  "",    false},
            {"Volume",        0.0f,   1.0f,   0.8f,  "",    false},
        };
        return info[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        if (m_bypassed || m_sampleFrames == 0) return;

        const float position    = m_params[kPosition];
        const float grainSizeMs = m_params[kGrainSize];
        const float density     = m_params[kDensity];
        const float spread      = m_params[kSpread];
        const float pitchSt     = m_params[kPitch];
        const float sprayMs     = m_params[kSpray];
        const int   shape       = static_cast<int>(m_params[kShape]);
        const float scan        = m_params[kScan];
        const float filterCut   = m_params[kFilterCutoff];
        const float filterReso  = m_params[kFilterReso];
        const float stereoW     = m_params[kStereoWidth];
        const bool  reverse     = m_params[kReverse] > 0.5f;
        const float jitter      = m_params[kJitter];
        const float volume      = m_params[kVolume];

        const int grainSizeSamples = std::max(1, static_cast<int>(grainSizeMs * 0.001 * m_sampleRate));
        const double grainInterval = (density > 0.01) ? (m_sampleRate / density) : m_sampleRate;

        // Process MIDI events
        for (int i = 0; i < midi.count(); ++i) {
            const auto& ev = midi[i];
            if (ev.isNoteOn()) {
                noteOn(ev.note, ev.velocity, ev.channel);
            } else if (ev.isNoteOff()) {
                noteOff(ev.note, ev.channel);
            } else if (ev.isCC() && ev.ccNumber == 123) {
                for (auto& v : m_voices) {
                    if (v.active) { v.ampEnv.gate(false); }
                }
            }
        }

        // Count active voices
        int activeVoices = 0;
        for (auto& v : m_voices)
            if (v.active) ++activeVoices;
        if (activeVoices == 0) return;

        // Update scan position
        m_scanPos += scan * (1.0 / m_sampleRate) * 0.5;
        m_scanPos -= std::floor(m_scanPos); // keep 0–1

        // SVF filter coefficients
        const float cutNorm = std::clamp(filterCut / static_cast<float>(m_sampleRate), 0.001f, 0.499f);
        const float g = std::tan(3.14159265f * cutNorm);
        const float k = 2.0f - 2.0f * std::clamp(filterReso, 0.0f, 0.95f);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        // Spawn grains and render
        for (int s = 0; s < numFrames; ++s) {
            // Spawn new grains at grain interval for active voices
            m_grainTimer += 1.0;
            if (m_grainTimer >= grainInterval) {
                m_grainTimer -= grainInterval;
                for (auto& v : m_voices) {
                    if (!v.active || v.ampEnv.isIdle()) continue;
                    spawnGrain(v, position, spread, sprayMs, pitchSt, jitter,
                               grainSizeSamples, shape, reverse, stereoW);
                }
            }

            // Render all active grains
            float mixL = 0.0f, mixR = 0.0f;
            for (auto& g : m_grains) {
                if (!g.active) continue;

                // Get window amplitude
                float t = static_cast<float>(g.pos) / static_cast<float>(g.length);
                float window = grainWindow(t, g.shape);

                // Read sample with linear interpolation
                float samp = readSample(g.readPos);
                float val = samp * window * g.velocity;

                mixL += val * g.panL;
                mixR += val * g.panR;

                // Advance read position
                g.readPos += g.speed;
                if (g.readPos < 0) g.readPos += m_sampleFrames;
                if (g.readPos >= m_sampleFrames) g.readPos -= m_sampleFrames;
                g.pos++;
                if (g.pos >= g.length) g.active = false;
            }

            // Apply voice envelopes
            float envSum = 0.0f;
            for (auto& v : m_voices) {
                if (!v.active) continue;
                float e = v.ampEnv.process();
                envSum += e;
                if (v.ampEnv.isIdle()) v.active = false;
            }
            float envGain = (activeVoices > 0) ? (envSum / activeVoices) : 0.0f;
            mixL *= envGain;
            mixR *= envGain;

            // SVF low-pass filter
            if (filterCut < 19999.0f) {
                float v3 = mixL - m_filterIcL[1];
                float v1 = a1 * m_filterIcL[0] + a2 * v3;
                float v2 = m_filterIcL[1] + a2 * m_filterIcL[0] + a3 * v3;
                m_filterIcL[0] = 2.0f * v1 - m_filterIcL[0];
                m_filterIcL[1] = 2.0f * v2 - m_filterIcL[1];
                mixL = v2;

                v3 = mixR - m_filterIcR[1];
                v1 = a1 * m_filterIcR[0] + a2 * v3;
                v2 = m_filterIcR[1] + a2 * m_filterIcR[0] + a3 * v3;
                m_filterIcR[0] = 2.0f * v1 - m_filterIcR[0];
                m_filterIcR[1] = 2.0f * v2 - m_filterIcR[1];
                mixR = v2;
            }

            buffer[s * numChannels]     += mixL * volume;
            if (numChannels > 1)
                buffer[s * numChannels + 1] += mixR * volume;
        }

        // Re-count active voices
        activeVoices = 0;
        for (auto& v : m_voices)
            if (v.active) ++activeVoices;
    }

private:
    // ── Grain structure ──
    struct Grain {
        bool active = false;
        double readPos = 0.0;    // current position in sample
        double speed = 1.0;      // playback speed (pitch)
        int pos = 0;             // current position within grain
        int length = 0;          // grain length in samples
        int shape = 0;           // window shape
        float velocity = 1.0f;
        float panL = 0.707f;
        float panR = 0.707f;
    };

    // ── Voice structure ──
    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        Envelope ampEnv;
    };

    // ── Grain window functions ──
    float grainWindow(float t, int shape) const {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (shape) {
        case 0: // Hanning
            return 0.5f * (1.0f - std::cos(6.28318530f * t));
        case 1: // Triangle
            return (t < 0.5f) ? (2.0f * t) : (2.0f * (1.0f - t));
        case 2: { // Gaussian
            float x = (t - 0.5f) * 4.0f; // ±2 sigma
            return std::exp(-0.5f * x * x);
        }
        case 3: { // Tukey (tapered cosine, alpha=0.5)
            constexpr float alpha = 0.5f;
            if (t < alpha * 0.5f)
                return 0.5f * (1.0f - std::cos(6.28318530f * t / alpha));
            else if (t > 1.0f - alpha * 0.5f)
                return 0.5f * (1.0f - std::cos(6.28318530f * (1.0f - t) / alpha));
            return 1.0f;
        }
        default:
            return 0.5f * (1.0f - std::cos(6.28318530f * t));
        }
    }

    // ── Sample reading with linear interpolation ──
    float readSample(double pos) const {
        if (m_sampleFrames == 0) return 0.0f;
        int idx0 = static_cast<int>(pos);
        int idx1 = idx0 + 1;
        if (idx0 < 0) idx0 += m_sampleFrames;
        if (idx1 >= m_sampleFrames) idx1 -= m_sampleFrames;
        float frac = static_cast<float>(pos - std::floor(pos));

        // Mix down to mono for grain reading
        float s0 = 0.0f, s1 = 0.0f;
        for (int ch = 0; ch < m_sampleChannels; ++ch) {
            s0 += m_sampleData[static_cast<size_t>(idx0) * m_sampleChannels + ch];
            s1 += m_sampleData[static_cast<size_t>(idx1) * m_sampleChannels + ch];
        }
        s0 /= m_sampleChannels;
        s1 /= m_sampleChannels;
        return s0 + frac * (s1 - s0);
    }

    // ── Simple xorshift RNG ──
    float randFloat() {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        return static_cast<float>(m_rngState & 0x7FFFFF) / 8388607.0f;
    }

    // ── Spawn a grain ──
    void spawnGrain(Voice& v, float position, float spread, float sprayMs,
                    float pitchSt, float jitter, int grainSizeSamples,
                    int shape, bool reverse, float stereoWidth) {
        // Find free grain slot
        Grain* slot = nullptr;
        for (auto& g : m_grains) {
            if (!g.active) { slot = &g; break; }
        }
        if (!slot) {
            // Steal oldest grain (smallest remaining length)
            int minRemain = INT_MAX;
            for (auto& g : m_grains) {
                int remain = g.length - g.pos;
                if (remain < minRemain) { minRemain = remain; slot = &g; }
            }
        }
        if (!slot) return;

        // Position with spread randomization
        float pos = position + static_cast<float>(m_scanPos);
        pos -= std::floor(pos);
        if (spread > 0.0f)
            pos += (randFloat() - 0.5f) * spread;
        pos -= std::floor(pos);
        if (pos < 0.0f) pos += 1.0f;

        // Spray: random offset in time
        double readOffset = 0.0;
        if (sprayMs > 0.0f)
            readOffset = (randFloat() - 0.5f) * sprayMs * 0.001 * m_sampleRate;

        double startPos = pos * m_sampleFrames + readOffset;
        while (startPos < 0) startPos += m_sampleFrames;
        while (startPos >= m_sampleFrames) startPos -= m_sampleFrames;

        // Pitch: note-based + param + jitter
        float noteShift = static_cast<float>((int)v.note - m_rootNote);
        float totalPitch = noteShift + pitchSt;
        if (jitter > 0.0f)
            totalPitch += (randFloat() - 0.5f) * 2.0f * jitter;
        double speed = std::pow(2.0, totalPitch / 12.0);
        if (reverse) speed = -speed;

        // Stereo panning
        float pan = (stereoWidth > 0.0f) ? (randFloat() - 0.5f) * stereoWidth : 0.0f;
        float panL = std::cos((pan + 0.5f) * 1.5707963f);
        float panR = std::sin((pan + 0.5f) * 1.5707963f);

        slot->active = true;
        slot->readPos = startPos;
        slot->speed = speed;
        slot->pos = 0;
        slot->length = grainSizeSamples;
        slot->shape = shape;
        slot->velocity = v.velocity;
        slot->panL = panL;
        slot->panR = panR;
    }

    // ── Note management ──
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
        Voice* slot = nullptr;
        for (auto& v : m_voices) {
            if (!v.active) { slot = &v; break; }
        }
        if (!slot) {
            // Steal oldest voice
            int64_t oldest = INT64_MAX;
            for (auto& v : m_voices) {
                if (v.startOrder < oldest) { oldest = v.startOrder; slot = &v; }
            }
        }
        if (!slot) return;

        slot->active = true;
        slot->note = note;
        slot->channel = ch;
        slot->velocity = velocityToGain(vel16);
        slot->startOrder = m_voiceCounter++;

        float atkSec = m_params[kAttack] * 0.001f;
        float relSec = m_params[kRelease] * 0.001f;
        slot->ampEnv.setSampleRate(m_sampleRate);
        slot->ampEnv.setADSR(atkSec, 0.001f, 1.0f, relSec);
        slot->ampEnv.gate(true);
    }

    void noteOff(uint8_t note, uint8_t ch) {
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.channel == ch) {
                v.ampEnv.gate(false);
                break;
            }
        }
    }

    void resetParams() {
        for (int i = 0; i < kParamCount; ++i)
            m_params[i] = parameterInfo(i).defaultValue;
        m_grainTimer = 0.0;
        m_scanPos = 0.0;
        m_rngState = 12345u;
        m_voiceCounter = 0;
        m_filterIcL[0] = m_filterIcL[1] = 0.0f;
        m_filterIcR[0] = m_filterIcR[1] = 0.0f;
    }

    // ── State ──
    float m_params[kParamCount] = {};
    Voice m_voices[kMaxVoices] = {};
    Grain m_grains[kMaxGrains] = {};

    // Sample data
    std::vector<float> m_sampleData;
    int m_sampleFrames = 0;
    int m_sampleChannels = 1;
    int m_rootNote = 60;

    // Grain spawning state
    double m_grainTimer = 0.0;
    double m_scanPos = 0.0;
    uint32_t m_rngState = 12345u;
    int64_t m_voiceCounter = 0;

    // SVF filter state (stereo)
    float m_filterIcL[2] = {};
    float m_filterIcR[2] = {};
};

} // namespace yawn::instruments
