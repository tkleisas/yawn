#pragma once

// PlateReverb — Dattorro (1997) plate reverb ("Effect Design, Part 1:
// Reverberator on Steroids", JAES), plate tunings.
//
// Topology follows the paper's figure: predelay → input-bandwidth
// one-pole → 4 series input-diffusion allpasses (chained through shared
// delay lines, as in the paper) → figure-8 tank: two cross-coupled
// sides, each [modulated allpass diffuser → long delay → damping
// one-pole → allpass → long delay]; stereo output from 7+7 signed taps
// inside the four tank lines. Tunings are stored in SECONDS (paper
// sample counts / 29761 Hz) and scaled to the host rate at init, so
// the plate sounds the same at any sample rate.
//
// Cortex-A53 (Pi Zero 2W) adaptations — measured, not guessed (see the
// reverb A/B bench notes in git history / Reverb.h):
//   * conditional-wrap ring indices everywhere — no sdiv per sample
//     (measured neutral vs modulo on A53, but free of worst cases)
//   * LINEAR (not cubic) interpolation for the two modulated allpass
//     reads — mod depth is ±8 samples at ~0.5 Hz; cubic's extra FP ops
//     are inaudible here
//   * clamp-to-zero (< 1e-18f) on the recursive one-pole states and the
//     four long-delay writes — the classic 1e-20 DC bias measured ~13%
//     SLOWER on the busy path on A53, so clamps only (the audio thread
//     also sets FPCR FZ+DN — see AudioEngine.cpp)
//   * the same silence gate as the Freeverb fix: input AND tail below
//     -100 dBFS for longer than the predelay → dry passthrough, tank
//     asleep, state reset on gate entry
//
// Simplifications vs the paper (documented, all inaudible or
// deliberate): fixed input bandwidth pole (the paper's bandwidth knob
// is not exposed), fixed tank diffusion gains (0.7 / 0.5 — the paper's
// decayDiffusion1/2), DECAY mapped to true per-side loop gain from an
// RT60 target.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class PlateReverb : public AudioEffect {
public:
    enum Param {
        kPreDelay = 0, // 0..1 -> 0..250 ms
        kDecay,        // 0..1 -> RT60 0.2..6 s (exponential)
        kDamping,      // 0..1 -> HF loss in the tank loop
        kDiffusion,    // 0..1 -> input diffuser allpass gain
        kWidth,        // 0..1 -> L/R crossmix (1 = full stereo)
        kWetDry,       // 0..1 -> wet level (dry = 1 - wet)
        kParamCount
    };

    const char* name() const override { return "Plate Reverb"; }
    const char* id()   const override { return "plate"; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels) override;

    int parameterCount() const override { return kParamCount; }
    const ParameterInfo& parameterInfo(int index) const override;

    float getParameter(int index) const override {
        return (index >= 0 && index < kParamCount) ? m_params[index] : 0.0f;
    }
    void setParameter(int index, float value) override {
        if (index >= 0 && index < kParamCount) {
            m_params[index] = std::clamp(value, 0.0f, 1.0f);
            updateParams();
        }
    }

private:
    // Ring buffer with conditional-wrap indexing (no division).
    // Single cursor: read()/write() address the same slot; the cursor
    // advances once per sample tick. read() after write() therefore
    // returns the JUST-WRITTEN sample — exactly the chained-allpass
    // behaviour of the paper's figure (and of the reference JS
    // implementation this was verified against).
    struct Line {
        void resize(int delaySamples) {
            m_buf.assign(size_t(delaySamples) + 1, 0.0f);
            m_pos = 0;
        }
        void clear() {
            std::fill(m_buf.begin(), m_buf.end(), 0.0f);
            m_pos = 0;
        }
        float read() const { return m_buf[m_pos]; }
        void write(float x) { m_buf[m_pos] = x; }
        void advance() { if (++m_pos >= int(m_buf.size())) m_pos = 0; }
        // Integer tap at `off` samples back from the cursor's next
        // position. off < size always holds for our tunings.
        float readAt(int off) const {
            int i = m_pos + off;
            if (i >= int(m_buf.size())) i -= int(m_buf.size());
            return m_buf[i];
        }
        // Fractional tap (linear interpolation) for modulated allpasses.
        float readFrac(float off) const {
            const int i0 = int(off);
            const float fr = off - float(i0);
            const float a = readAt(i0);
            return a + fr * (readAt(i0 + 1) - a);
        }
        std::vector<float> m_buf;
        int m_pos = 0;
    };

    static float clampDenorm(float x) {
        return std::fabs(x) < 1e-18f ? 0.0f : x;
    }

    void updateParams();
    float lfo(int which); // 0..1 sine, detuned rates, table-based

    // 12 tank/input lines + predelay (indices follow the reference
    // implementation's numbering: 0-3 input diffusers, 4-7 left tank,
    // 8-11 right tank).
    Line m_lines[12];
    std::vector<float> m_preDelayBuf;
    int m_preDelayPos = 0;

    // Derived coefficients (updateParams from the 0..1 params)
    float m_params[kParamCount] = {0.04f, 0.677f, 0.5f, 0.7f, 1.0f, 0.3f};
    float m_bw = 0.62f;    // input bandwidth pole (fixed; paper ~6 kHz @48k)
    float m_dc = 0.5f;     // per-side tank loop gain (from RT60)
    float m_dp = 0.5f;     // damping one-pole tracking coeff
    float m_fi = 0.665f, m_si = 0.665f; // input diffusion gains
    float m_wet = 0.18f, m_dry = 0.7f;  // wet pre-scaled by 0.6 (taps run hot)
    float m_width = 1.0f;
    int   m_pdSamples = 480;

    // Modulation state (±8 samples @29761 Hz equivalent, ~0.5 Hz)
    double m_lfoPhase[2] = {0.0, 0.37};
    double m_lfoInc[2] = {0.0, 0.0};
    float  m_excDepth = 8.0f; // samples, scaled at init

    // Recursive one-pole states (denormal-clamped)
    float m_lpIn = 0.0f, m_lpL = 0.0f, m_lpR = 0.0f;

    // Silence gate (same pattern as the Freeverb fix)
    int  m_silentFrames = 0;
    bool m_gated = false;
};

} // namespace effects
} // namespace yawn
