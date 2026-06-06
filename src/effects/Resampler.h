#pragma once
// Resampler — varispeed buffer (down/up-sampling glitch). Input is
// written into a short ring at 1×; a read pointer scans the same ring at
// `Rate` (linear-interpolated). Rate < 1 reads slower (down — lower
// pitch / smeared), Rate > 1 reads faster (up — higher pitch). Because
// the read and write pointers run at different speeds in a fixed Window,
// the read periodically laps the write, producing the characteristic
// resampling glitch. Distinct from Bitcrusher (which is fixed-rate
// zero-order-hold decimation, no pitch change).

#include "effects/AudioEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class Resampler : public AudioEffect {
public:
    enum Param { kRate, kWindow, kMix, kParamCount };

    const char* name() const override { return "Resampler"; }
    const char* id()   const override { return "resampler"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_bufLen = std::max(4, static_cast<int>(sampleRate));  // up to 1 s
        m_bufL.assign(m_bufLen, 0.0f);
        m_bufR.assign(m_bufLen, 0.0f);
        reset();
    }

    void reset() override {
        std::fill(m_bufL.begin(), m_bufL.end(), 0.0f);
        std::fill(m_bufR.begin(), m_bufR.end(), 0.0f);
        m_writePos = 0;
        m_readPos = 0.0;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed || m_bufLen <= 0) return;
        const bool   stereo = numChannels > 1;
        const double rate   = std::clamp(m_params[kRate], 0.1f, 4.0f);
        const int    window = std::clamp(
            static_cast<int>(m_params[kWindow] * 0.001f * m_sampleRate),
            4, m_bufLen);
        const float  mix    = std::clamp(m_params[kMix], 0.0f, 1.0f) * m_mix;

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;

            // Write at 1× into the window.
            const int wp = m_writePos % window;
            m_bufL[wp] = dryL;
            m_bufR[wp] = dryR;

            // Read at `rate` with linear interpolation, wrapped to window.
            if (m_readPos >= window) m_readPos -= window;
            const int   r0 = static_cast<int>(m_readPos);
            const float fr = static_cast<float>(m_readPos - r0);
            int r1 = r0 + 1; if (r1 >= window) r1 -= window;
            const float wetL = m_bufL[r0] + (m_bufL[r1] - m_bufL[r0]) * fr;
            const float wetR = m_bufR[r0] + (m_bufR[r1] - m_bufR[r0]) * fr;

            buffer[i * numChannels] = dryL * (1.0f - mix) + wetL * mix;
            if (stereo)
                buffer[i * numChannels + 1] = dryR * (1.0f - mix) + wetR * mix;

            m_readPos += rate;
            if (++m_writePos >= window) m_writePos = 0;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static const ParameterInfo infos[kParamCount] = {
            {"Rate",   0.1f,   4.0f,   1.0f, "x",  false, false, WidgetHint::DentedKnob},
            {"Window", 20.0f, 500.0f, 120.0f, "ms", false, false, WidgetHint::Knob},
            {"Mix",    0.0f,   1.0f,   1.0f, "",   false, false, WidgetHint::DentedKnob},
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
    float m_params[kParamCount] = {1.0f, 120.0f, 1.0f};
    std::vector<float> m_bufL, m_bufR;
    int    m_bufLen = 0, m_writePos = 0;
    double m_readPos = 0.0;
};

} // namespace effects
} // namespace yawn
