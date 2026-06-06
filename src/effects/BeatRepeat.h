#pragma once
// Beat Repeat — tempo-synced rhythmic stutter. On each Interval grid
// point it rolls Chance; on a hit it captures the most-recent grain
// (length = Repeat) into a private buffer and loops it (optionally
// pitch-shifted) for Gate fraction of the interval, then releases back
// to dry. Tempo arrives via setTempo(): locked to the transport beat
// during playback, free-running at the current BPM when stopped (so it
// still stutters live input).

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yawn {
namespace effects {

class BeatRepeat : public AudioEffect {
public:
    enum Param { kInterval, kRepeat, kGate, kChance, kPitch, kMix, kParamCount };

    const char* name() const override { return "Beat Repeat"; }
    const char* id()   const override { return "beatrepeat"; }

    void setTempo(double bpm, double beat, bool playing) override {
        if (bpm > 0.0) m_bpm = bpm;
        m_hostBeat = beat;
        m_playing  = playing;
    }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        // 4 s of capture covers a full bar even at very low BPM.
        m_bufLen = std::max(1, static_cast<int>(sampleRate * 4.0));
        m_ringL.assign(m_bufLen, 0.0f);
        m_ringR.assign(m_bufLen, 0.0f);
        m_grainL.assign(m_bufLen, 0.0f);
        m_grainR.assign(m_bufLen, 0.0f);
        reset();
    }

    void reset() override {
        std::fill(m_ringL.begin(), m_ringL.end(), 0.0f);
        std::fill(m_ringR.begin(), m_ringR.end(), 0.0f);
        m_writePos = 0;
        m_beatPhase = 0.0;
        m_lastGrid = -1;
        m_active = false;
        m_grainLen = 0;
        m_grainPhase = 0.0;
        m_gateRemaining = 0;
        m_rng = 0x1234567u;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed || m_bufLen <= 0) return;
        const bool stereo = numChannels > 1;
        const double spb = (m_bpm > 0.0) ? (60.0 / m_bpm * m_sampleRate)
                                         : (0.5 * m_sampleRate);
        const double intervalBeats = gridBeats(static_cast<int>(m_params[kInterval]));
        const double repeatBeats   = gridBeats(static_cast<int>(m_params[kRepeat]));
        const float  gateFrac   = std::clamp(m_params[kGate],   0.0f, 1.0f);
        const float  chance     = std::clamp(m_params[kChance], 0.0f, 1.0f);
        const double pitchRatio = std::pow(2.0,
            std::clamp(m_params[kPitch], -24.0f, 24.0f) / 12.0);
        const float  mix = std::clamp(m_params[kMix], 0.0f, 1.0f) * m_mix;

        // Anchor the local beat clock to the transport each block while
        // playing; free-run otherwise.
        if (m_playing) m_beatPhase = m_hostBeat;

        const int sliceLen = std::clamp(
            static_cast<int>(repeatBeats * spb), 1, m_bufLen);

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            m_ringL[m_writePos] = dryL;
            m_ringR[m_writePos] = dryR;

            // Grid trigger — fire when the interval index advances.
            const long grid = (intervalBeats > 0.0)
                ? static_cast<long>(std::floor(m_beatPhase / intervalBeats)) : 0;
            if (grid != m_lastGrid) {
                m_lastGrid = grid;
                if (randf() < chance) {
                    m_grainLen = sliceLen;
                    // Copy the most-recent sliceLen samples into the grain
                    // so continued ring writes can't overwrite it mid-loop.
                    for (int k = 0; k < m_grainLen; ++k) {
                        int idx = (m_writePos - m_grainLen + 1 + k) % m_bufLen;
                        if (idx < 0) idx += m_bufLen;
                        m_grainL[k] = m_ringL[idx];
                        m_grainR[k] = m_ringR[idx];
                    }
                    m_grainPhase = 0.0;
                    m_gateRemaining = std::max(1,
                        static_cast<int>(gateFrac * intervalBeats * spb));
                    m_active = true;
                }
            }

            float wetL = dryL, wetR = dryR;
            if (m_active && m_grainLen > 0) {
                const int   p0 = static_cast<int>(m_grainPhase);
                const float fr = static_cast<float>(m_grainPhase - p0);
                int p1 = p0 + 1; if (p1 >= m_grainLen) p1 -= m_grainLen;
                wetL = m_grainL[p0] + (m_grainL[p1] - m_grainL[p0]) * fr;
                wetR = m_grainR[p0] + (m_grainR[p1] - m_grainR[p0]) * fr;
                m_grainPhase += pitchRatio;
                while (m_grainPhase >= m_grainLen) m_grainPhase -= m_grainLen;
                if (--m_gateRemaining <= 0) m_active = false;
            }

            buffer[i * numChannels] = dryL * (1.0f - mix) + wetL * mix;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * (1.0f - mix) + wetR * mix;

            if (++m_writePos >= m_bufLen) m_writePos = 0;
            m_beatPhase += 1.0 / spb;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const char* kGridLabels[] =
            {"1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"};
        static const ParameterInfo infos[kParamCount] = {
            {"Interval", 0.0f, 5.0f, 2.0f, "", false, false,
                WidgetHint::StepSelector, kGridLabels, 6},
            {"Repeat",   0.0f, 5.0f, 3.0f, "", false, false,
                WidgetHint::StepSelector, kGridLabels, 6},
            {"Gate",     0.0f, 1.0f, 1.0f, "",   false, false, WidgetHint::Knob},
            {"Chance",   0.0f, 1.0f, 1.0f, "",   false, false, WidgetHint::Knob},
            {"Pitch",  -12.0f, 12.0f, 0.0f, "st", false, false, WidgetHint::DentedKnob},
            {"Mix",      0.0f, 1.0f, 1.0f, "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int i) const override {
        return (i >= 0 && i < kParamCount) ? m_params[i] : 0.0f;
    }
    void setParameter(int i, float v) override {
        if (i < 0 || i >= kParamCount) return;
        const auto& pi = parameterInfo(i);
        m_params[i] = std::clamp(v, pi.minValue, pi.maxValue);
    }

private:
    // Step → beats: 0=1 bar, 1=1/2, 2=1/4, 3=1/8, 4=1/16, 5=1/32.
    static double gridBeats(int step) {
        static const double t[6] = {4.0, 2.0, 1.0, 0.5, 0.25, 0.125};
        return t[std::clamp(step, 0, 5)];
    }
    float randf() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0x1000000);
    }

    float m_params[kParamCount] = {2.0f, 3.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    std::vector<float> m_ringL, m_ringR, m_grainL, m_grainR;
    int    m_bufLen = 0, m_writePos = 0;
    double m_bpm = 120.0, m_hostBeat = 0.0, m_beatPhase = 0.0;
    bool   m_playing = false, m_active = false;
    long   m_lastGrid = -1;
    int    m_grainLen = 0, m_gateRemaining = 0;
    double m_grainPhase = 0.0;
    uint32_t m_rng = 0x1234567u;
};

} // namespace effects
} // namespace yawn
