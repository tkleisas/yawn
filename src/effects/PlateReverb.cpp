#include "effects/PlateReverb.h"

#include <cstdio>

namespace yawn::effects {

namespace {

// Paper tunings in seconds (sample counts at the paper's 29761 Hz):
// input diffusers 142/107/379/277, left tank 672(+mod)/4453/1800/3720,
// right tank 908(+mod)/4217/2656/3163.
constexpr double kRefRate = 29761.0;
constexpr float kLineSec[12] = {
    142.0f / 29761.0f, 107.0f / 29761.0f, 379.0f / 29761.0f,
    277.0f / 29761.0f,
    672.0f / 29761.0f, 4453.0f / 29761.0f, 1800.0f / 29761.0f,
    3720.0f / 29761.0f,
    908.0f / 29761.0f, 4217.0f / 29761.0f, 2656.0f / 29761.0f,
    3163.0f / 29761.0f,
};
// Output taps (seconds), left then right, with signs:
//   L = +9[266] +9[2974] -10[1913] +11[1996] -5[1990] -6[187] -7[1066]
//   R = +5[353] +5[3627] -6[1228] +7[2673] -9[2111] -10[335] -11[121]
constexpr int   kTapLine[14] = {9, 9, 10, 11, 5, 6, 7, 5, 5, 6, 7, 9, 10, 11};
constexpr float kTapSign[14] = {1, 1, -1, 1, -1, -1, -1, 1, 1, -1, 1, -1, -1, -1};
constexpr float kTapSec[14] = {
    266.0f / 29761.0f, 2974.0f / 29761.0f, 1913.0f / 29761.0f,
    1996.0f / 29761.0f, 1990.0f / 29761.0f, 187.0f / 29761.0f,
    1066.0f / 29761.0f,
    353.0f / 29761.0f, 3627.0f / 29761.0f, 1228.0f / 29761.0f,
    2673.0f / 29761.0f, 2111.0f / 29761.0f, 335.0f / 29761.0f,
    121.0f / 29761.0f,
};
// Fixed tank diffusion gains (paper's decayDiffusion1/2 defaults).
constexpr float kFt = 0.7f, kSt = 0.5f;

void fmtMs(float v, char* b, int n) {
    std::snprintf(b, size_t(n), "%.0f ms", double(v) * 250.0);
}
void fmtSec(float v, char* b, int n) { // must match updateParams mapping
    std::snprintf(b, size_t(n), "%.1f s",
                  0.2 * std::pow(30.0, double(v)));
}
void fmtPct(float v, char* b, int n) {
    std::snprintf(b, size_t(n), "%.0f%%", double(v) * 100.0);
}

// Shared sine table for the two tank LFOs (513 entries: one extra for
// wrap-free linear interpolation). Built once at first init — never on
// the audio thread.
const std::array<float, 513>& sineTable() {
    static const std::array<float, 513> table = [] {
        std::array<float, 513> t{};
        for (int i = 0; i <= 512; ++i)
            t[size_t(i)] = float(std::sin(2.0 * 3.14159265358979 *
                                          double(i) / 512.0));
        return t;
    }();
    return table;
}

} // namespace

void PlateReverb::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    (void)maxBlockSize; // processing is per-sample; no block scratch

    const double scale = sampleRate / kRefRate;
    (void)sineTable(); // build off the audio thread
    m_excDepth = float(8.0 * scale); // ±8 samples at the paper's rate
    const int modHead = int(2.0 * m_excDepth) + 4;
    for (int i = 0; i < 12; ++i) {
        int len = int(kLineSec[i] * sampleRate + 0.5);
        if (i == 4 || i == 8) len += modHead; // modulated diffusers
        m_lines[i].resize(len);
    }

    m_preDelayBuf.assign(size_t(0.25 * sampleRate) + 2, 0.0f);
    m_preDelayPos = 0;

    // LFOs: ~0.5 Hz, mutually detuned (paper: excursion rate 0..1 Hz)
    m_lfoInc[0] = 0.50 / sampleRate;
    m_lfoInc[1] = 0.512 / sampleRate;

    reset();
    updateParams();
}

void PlateReverb::reset() {
    for (auto& l : m_lines) l.clear();
    std::fill(m_preDelayBuf.begin(), m_preDelayBuf.end(), 0.0f);
    m_preDelayPos = 0;
    m_lpIn = m_lpL = m_lpR = 0.0f;
}

float PlateReverb::lfo(int which) {
    const auto& t = sineTable();
    double ph = m_lfoPhase[which];
    ph += m_lfoInc[which];
    if (ph >= 1.0) ph -= 1.0;
    m_lfoPhase[which] = ph;
    const double x = ph * 512.0;
    const int i0 = int(x);
    const float fr = float(x - i0);
    return t[size_t(i0)] + fr * (t[size_t(i0) + 1] - t[size_t(i0)]);
}

void PlateReverb::updateParams() {
    m_pdSamples = int(m_params[kPreDelay] * 0.25f * float(m_sampleRate));
    const int maxPd = int(m_preDelayBuf.size()) - 2;
    if (m_pdSamples > maxPd) m_pdSamples = maxPd;

    // DECAY: RT60 target -> per-side loop gain. The figure-8 loop time
    // is the sum of both sides' delays (rate-independent base: the
    // paper tunings sum to 21589/29761 s); dc is applied once per side.
    // The 0.60 factor is EMPIRICAL: the damping pole + modulated
    // diffusers add per-loop loss beyond the dc term (measured RT60 vs
    // naive target: 0.42x at factor 1.0, 1.6x at 0.42), so "RT60" is
    // approximate (±), worst at the damping extremes.
    const double rt60 = 0.2 * std::pow(30.0, double(m_params[kDecay]));
    constexpr double kLoopSec = 21589.0 / 29761.0;
    m_dc = float(std::pow(10.0, -3.0 * kLoopSec / rt60 * 0.5 * 0.60));
    if (m_dc > 0.98f) m_dc = 0.98f; // freeze stays finite

    m_dp = 1.0f - 0.995f * m_params[kDamping];
    m_fi = m_si = 0.35f + 0.45f * m_params[kDiffusion];
    m_width = m_params[kWidth];
    m_wet = m_params[kWetDry] * 0.6f;
    m_dry = 1.0f - m_params[kWetDry];
}

const ParameterInfo& PlateReverb::parameterInfo(int index) const {
    static const ParameterInfo infos[kParamCount] = {
        {"Pre-Delay", 0.0f, 1.0f, 0.04f, "",  false, false,
            WidgetHint::DentedKnob, nullptr, 0, fmtMs},
        {"Decay",     0.0f, 1.0f, 0.677f, "", false, false,
            WidgetHint::DentedKnob, nullptr, 0, fmtSec},
        {"Damping",   0.0f, 1.0f, 0.5f,  "",  false, false,
            WidgetHint::DentedKnob, nullptr, 0, fmtPct},
        {"Diffusion", 0.0f, 1.0f, 0.7f,  "",  false, false,
            WidgetHint::DentedKnob, nullptr, 0, fmtPct},
        {"Width",     0.0f, 1.0f, 1.0f,  "",  false, false,
            WidgetHint::DentedKnob, nullptr, 0, fmtPct},
        {"Wet/Dry",   0.0f, 1.0f, 0.3f,  "",  false, false,
            WidgetHint::DentedKnob, nullptr, 0, fmtPct},
    };
    return infos[std::clamp(index, 0, kParamCount - 1)];
}

void PlateReverb::process(float* buffer, int numFrames, int numChannels) {
    if (m_bypassed) return;

    // ── Silence gate (same pattern as the Freeverb fix) ────────────
    // Input silent for longer than the predelay AND tank tail below
    // -100 dBFS → pass the (in-place) input through = dry-only, skip
    // the whole tank. State is reset on entry so no stale tails ring
    // into the next sound.
    float inPeak = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        const float a = std::fabs(buffer[i * numChannels]);
        const float b = numChannels > 1
            ? std::fabs(buffer[i * numChannels + 1]) : a;
        inPeak = std::max(inPeak, std::max(a, b));
    }
    if (inPeak >= 1e-5f) m_silentFrames = 0;
    else                 m_silentFrames += numFrames;
    const bool tankSilent =
        std::fabs(m_lpL) + std::fabs(m_lpR) < 1e-5f;
    if (m_silentFrames > m_pdSamples && tankSilent) {
        if (!m_gated) {
            m_gated = true;
            reset();
        }
        return;
    }
    m_gated = false;

    const float bw = m_bw, fi = m_fi, si = m_si;
    const float dc = m_dc, dp = m_dp;
    const int pd = m_pdSamples;
    const int pdSize = int(m_preDelayBuf.size());
    const float wA = 0.5f + 0.5f * m_width, wB = 0.5f - 0.5f * m_width;

    int tapIdx[14];
    for (int i = 0; i < 14; ++i)
        tapIdx[i] = int(kTapSec[i] * float(m_sampleRate) + 0.5f);

    for (int i = 0; i < numFrames; ++i) {
        const float inL = buffer[i * numChannels];
        const float inR = numChannels > 1 ? buffer[i * numChannels + 1]
                                          : inL;

        // Predelay + input bandwidth pole
        m_preDelayBuf[size_t(m_preDelayPos)] = (inL + inR) * 0.5f;
        int rd = m_preDelayPos - pd;
        if (rd < 0) rd += pdSize;
        m_lpIn += bw * (m_preDelayBuf[size_t(rd)] - m_lpIn);
        m_lpIn = clampDenorm(m_lpIn);

        // Input diffusion — 4 series allpasses chained through shared
        // lines (paper's figure; read-after-write within the tick).
        float pre = m_lpIn - fi * m_lines[0].read();
        m_lines[0].write(pre);
        pre = fi * (pre - m_lines[1].read()) + m_lines[0].read();
        m_lines[1].write(pre);
        pre = fi * pre + m_lines[1].read() - si * m_lines[2].read();
        m_lines[2].write(pre);
        pre = si * (pre - m_lines[3].read()) + m_lines[2].read();
        m_lines[3].write(pre);
        const float split = si * pre + m_lines[3].read();

        const float excL = m_excDepth * (1.0f + lfo(0));
        const float excR = m_excDepth * (1.0f + lfo(1));

        // Left tank side
        const float dL = m_lines[4].readFrac(excL);
        float temp = split + dc * m_lines[11].read() + kFt * dL;
        m_lines[4].write(temp);
        m_lines[5].write(clampDenorm(dL - kFt * temp));
        m_lpL += dp * (m_lines[5].read() - m_lpL);
        m_lpL = clampDenorm(m_lpL);
        temp = dc * m_lpL - kSt * m_lines[6].read();
        m_lines[6].write(temp);
        m_lines[7].write(clampDenorm(m_lines[6].read() + kSt * temp));

        // Right tank side
        const float dR = m_lines[8].readFrac(excR);
        temp = split + dc * m_lines[7].read() + kFt * dR;
        m_lines[8].write(temp);
        m_lines[9].write(clampDenorm(dR - kFt * temp));
        m_lpR += dp * (m_lines[9].read() - m_lpR);
        m_lpR = clampDenorm(m_lpR);
        temp = dc * m_lpR - kSt * m_lines[10].read();
        m_lines[10].write(temp);
        m_lines[11].write(clampDenorm(m_lines[10].read() + kSt * temp));

        // Stereo taps (the 7-tap sums run hot — a fixed -12 dB trim
        // keeps the effect from clipping its own output at high wet;
        // the wet gain above already carries the paper's 0.6 factor)
        float lo = 0.0f, ro = 0.0f;
        for (int tI = 0; tI < 7; ++tI)
            lo += kTapSign[tI] * m_lines[kTapLine[tI]].readAt(tapIdx[tI]);
        for (int tI = 7; tI < 14; ++tI)
            ro += kTapSign[tI] * m_lines[kTapLine[tI]].readAt(tapIdx[tI]);
        lo *= 0.25f; ro *= 0.25f; // -12 dB tap-sum trim

        // Width crossmix + wet/dry
        const float wl = lo * wA + ro * wB;
        const float wr = ro * wA + lo * wB;
        buffer[i * numChannels] = inL * m_dry + wl * m_wet;
        if (numChannels > 1)
            buffer[i * numChannels + 1] = inR * m_dry + wr * m_wet;

        // Advance every ring once per sample tick
        for (auto& l : m_lines) l.advance();
        if (++m_preDelayPos >= pdSize) m_preDelayPos = 0;
    }
}

} // namespace yawn::effects
