#pragma once
// Rotary — Leslie speaker simulation.
//
// Models a rotating-speaker cabinet (Hammond/Leslie 122/147). Two
// rotors at different rates — horn (treble) and drum (bass) —
// each producing:
//   * Doppler shift via a modulated delay line. The speaker face
//     moves through ±r centimetres relative to a stationary mic,
//     so sound takes (r/c) seconds longer/shorter to reach. We
//     model this as a delay length that oscillates with rotor angle.
//   * Amplitude modulation. The driver radiates strongest along
//     its axis; as the horn/drum rotates that lobe sweeps toward
//     and away from the mic, modulating perceived loudness.
//   * Stereo image shift. The cabinet has two mics (or the player
//     uses two separate mics in real life); when the rotor faces
//     one mic it's silent at the other. We simulate by giving the
//     L and R amplitude modulators opposite phase polarity so the
//     stereo balance pans with rotation.
//
// Crossover: a simple 1st-order LP/HP pair at ~800 Hz default
// splits the input. The lows go through the drum-rotor processing,
// the highs through the horn-rotor processing, then they sum
// post-rotation and mix into the dry signal.
//
// Speed switching: tri-state (Slow / Stop / Fast). Changing the
// setting doesn't snap RPM — both rotors RAMP from current to
// target with a time constant set by the Acceleration knob.
// Horn ramps faster than drum (drum has more mechanical inertia),
// modelled with a 2.5× ratio.
//
// Latency: zero. The Doppler delay applies only to the wet path —
// the dry signal passes through directly via the mix knob, so
// no compensation needed for upstream/downstream alignment.

#include "effects/AudioEffect.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace yawn {
namespace effects {

class Rotary : public AudioEffect {
public:
    // ~5400 samples ≈ 110ms at 48k — much larger than the actual
    // modulation depth (a few ms) so the read tap stays comfortably
    // inside the buffer at any reasonable RPM × cabinet radius.
    static constexpr int kDelayBufFrames = 8192;

    enum SpeedMode { kSpeedSlow = 0, kSpeedStop = 1, kSpeedFast = 2 };

    enum Param {
        kSpeed,        // Slow / Stop / Fast (StepSelector)
        kAccel,        // 0.1..5 sec — RPM ramp time-constant (horn).
                       // Drum ramp is 2.5× this (real Leslie drum is
                       // mechanically heavier than the horn assembly).
        kDrive,        // 0..1 — pre-rotor soft saturation
                       // (Leslie tube preamp character).
        kCrossover,    // 200..1500 Hz — LP/HP split between drum + horn
        kHornSlowRPM,  // 20..100  — horn rotor RPM in Slow mode
        kHornFastRPM,  // 200..500 — horn rotor RPM in Fast mode
        kDrumSlowRPM,  // 15..100  — drum rotor RPM in Slow mode
        kDrumFastRPM,  // 150..400 — drum rotor RPM in Fast mode
        kMicWidth,     // 0..1 — stereo amplitude-mod depth
        kMix,          // 0..1 — dry/wet
        kParamCount
    };

    const char* name() const override { return "Rotary"; }
    const char* id()   const override { return "rotary"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;

        // Defaults chosen to sound "Leslie-like" out of the box:
        // Fast mode by default with classic 122 cabinet-ish RPMs,
        // 800 Hz crossover, ~1.5s acceleration, half-wet so dry
        // bleed keeps the signal recognisable on first drop-in.
        m_params[kSpeed]       = static_cast<float>(kSpeedFast);
        m_params[kAccel]       = 1.5f;
        m_params[kDrive]       = 0.2f;
        m_params[kCrossover]   = 800.0f;
        m_params[kHornSlowRPM] = 48.0f;
        m_params[kHornFastRPM] = 340.0f;
        m_params[kDrumSlowRPM] = 40.0f;
        m_params[kDrumFastRPM] = 290.0f;
        m_params[kMicWidth]    = 0.7f;
        m_params[kMix]         = 0.5f;
        reset();
    }

    void reset() override {
        for (auto& b : m_hornBufL) b = 0.0f;
        for (auto& b : m_hornBufR) b = 0.0f;
        for (auto& b : m_drumBufL) b = 0.0f;
        for (auto& b : m_drumBufR) b = 0.0f;
        m_hornWrite = 0;
        m_drumWrite = 0;
        m_hornAngle = 0.0;
        m_drumAngle = 0.0;
        m_hornRPM   = targetHornRPM();
        m_drumRPM   = targetDrumRPM();
        m_lpL = m_lpR = 0.0f;
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        const float sr      = static_cast<float>(m_sampleRate);
        const float drive   = std::clamp(m_params[kDrive],     0.0f, 1.0f);
        const float xover   = std::clamp(m_params[kCrossover], 200.0f, 1500.0f);
        const float micWidth = std::clamp(m_params[kMicWidth], 0.0f, 1.0f);
        const float mix     = std::clamp(m_params[kMix],       0.0f, 1.0f);
        const float accel   = std::clamp(m_params[kAccel], 0.1f, 5.0f);
        const bool  stereo  = (numChannels > 1);

        // RPM ramp coefficients. exp(-1/τ·sr) per-sample → after τ
        // seconds the residual is e⁻¹ ≈ 36% — a "time constant" of
        // accel seconds for the horn, 2.5× longer for the drum.
        const float hornAccelCoef = std::exp(-1.0f / (accel       * sr));
        const float drumAccelCoef = std::exp(-1.0f / (accel * 2.5f * sr));

        const float hornTarget = targetHornRPM();
        const float drumTarget = targetDrumRPM();

        // Crossover one-pole coefficient:
        //   y[n] = α·x[n] + (1-α)·y[n-1]
        // with α = 1 - exp(-2π·fc/sr). Highpass = input - lowpass.
        const float xoverAlpha = 1.0f -
            std::exp(-2.0f * 3.14159265358979f * xover / sr);

        // Cabinet acoustic geometry — modulation depth per rotor.
        // Horn radius is bigger than drum radius in a real Leslie,
        // and the horn rotates faster, so the horn produces more
        // pitch shift. Numbers below are tuned by ear, not measured.
        const float hornDepthSamples = 22.0f;   // ~0.46 ms @ 48k
        const float drumDepthSamples = 38.0f;   // ~0.79 ms @ 48k
        const float hornBaseSamples  = 80.0f;   // base latency
        const float drumBaseSamples  = 130.0f;

        for (int i = 0; i < numFrames; ++i) {
            // 1) Smoothly ramp current RPM toward target (single-pole).
            m_hornRPM = hornTarget + (m_hornRPM - hornTarget) * hornAccelCoef;
            m_drumRPM = drumTarget + (m_drumRPM - drumTarget) * drumAccelCoef;

            // 2) Advance rotor angles. RPM/60 = revolutions per second.
            const double hornInc = static_cast<double>(m_hornRPM) / 60.0 / sr;
            const double drumInc = static_cast<double>(m_drumRPM) / 60.0 / sr;
            m_hornAngle += hornInc;
            m_drumAngle += drumInc;
            if (m_hornAngle >= 1.0) m_hornAngle -= 1.0;
            if (m_drumAngle >= 1.0) m_drumAngle -= 1.0;

            // 3) Pre-drive (soft saturation) on the dry input.
            const float dryL = buffer[i * numChannels];
            const float dryR = stereo ? buffer[i * numChannels + 1] : dryL;
            const float driveAmt = 1.0f + drive * 4.0f;
            const float saturatedL = std::tanh(dryL * driveAmt) /
                                     std::tanh(driveAmt);
            const float saturatedR = stereo
                ? std::tanh(dryR * driveAmt) / std::tanh(driveAmt)
                : saturatedL;

            // 4) Crossover into LF (drum) + HF (horn) bands.
            m_lpL += xoverAlpha * (saturatedL - m_lpL);
            const float lpL = m_lpL;
            const float hpL = saturatedL - lpL;
            float lpR = lpL, hpR = hpL;
            if (stereo) {
                m_lpR += xoverAlpha * (saturatedR - m_lpR);
                lpR = m_lpR;
                hpR = saturatedR - m_lpR;
            }

            // 5) Write into per-rotor delay lines. We feed the same
            // signal into both channels' buffers because the rotor
            // is a single physical source — the L/R difference comes
            // from the angle-offset taps below.
            m_hornBufL[m_hornWrite] = hpL;
            m_hornBufR[m_hornWrite] = hpR;
            m_drumBufL[m_drumWrite] = lpL;
            m_drumBufR[m_drumWrite] = lpR;

            // 6) Doppler taps. The L mic and R mic sit on opposite
            // sides of the cabinet, so when the rotor faces L the
            // L-side sound is "near" (short delay) and the R-side
            // is "far" (long delay) — a half-cycle phase offset.
            const double tau = 6.28318530717958647692; // 2π
            const float hornModL = static_cast<float>(std::sin(m_hornAngle * tau));
            const float hornModR = static_cast<float>(std::sin(m_hornAngle * tau + 3.14159265f));
            const float drumModL = static_cast<float>(std::sin(m_drumAngle * tau));
            const float drumModR = static_cast<float>(std::sin(m_drumAngle * tau + 3.14159265f));

            const float hornL = readDelay(m_hornBufL.data(), m_hornWrite,
                                           hornBaseSamples + hornDepthSamples * hornModL);
            const float hornR = readDelay(m_hornBufR.data(), m_hornWrite,
                                           hornBaseSamples + hornDepthSamples * hornModR);
            const float drumL = readDelay(m_drumBufL.data(), m_drumWrite,
                                           drumBaseSamples + drumDepthSamples * drumModL);
            const float drumR = readDelay(m_drumBufR.data(), m_drumWrite,
                                           drumBaseSamples + drumDepthSamples * drumModR);

            // 7) Amplitude modulation — directional tremolo.
            // gainL is loud when rotor faces L (cos near +1), quiet
            // when it faces R. micWidth scales modulation depth so
            // 0 = no AM (just Doppler), 1 = full ±micWidth swing.
            const float hornAmpL = 1.0f - micWidth * (0.5f - 0.5f * std::cos(m_hornAngle * tau));
            const float hornAmpR = 1.0f - micWidth * (0.5f + 0.5f * std::cos(m_hornAngle * tau));
            const float drumAmpL = 1.0f - micWidth * (0.5f - 0.5f * std::cos(m_drumAngle * tau));
            const float drumAmpR = 1.0f - micWidth * (0.5f + 0.5f * std::cos(m_drumAngle * tau));

            // 8) Sum horn + drum per channel = full wet signal.
            const float wetL = hornL * hornAmpL + drumL * drumAmpL;
            const float wetR = hornR * hornAmpR + drumR * drumAmpR;

            // 9) Mix dry/wet.
            const float outL = dryL * (1.0f - mix) + wetL * mix;
            const float outR = dryR * (1.0f - mix) + wetR * mix;

            buffer[i * numChannels] = outL;
            if (stereo) buffer[i * numChannels + 1] = outR;

            m_hornWrite = (m_hornWrite + 1) % kDelayBufFrames;
            m_drumWrite = (m_drumWrite + 1) % kDelayBufFrames;
        }
    }

    int parameterCount() const override { return kParamCount; }

    const ParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kSpeedLabels[] = {"Slow", "Stop", "Fast"};
        // Labels capped at ≤ 6 chars so the 5-cell-per-row knob grid
        // doesn't bleed neighbours. "H/D Slow/Fast" use single-letter
        // rotor prefix because the four cells sit together — easy to
        // read as the "horn cluster" + "drum cluster".
        static const ParameterInfo infos[] = {
            {"Speed",       0,2,2.0f, "", false, false,
                WidgetHint::StepSelector, kSpeedLabels, 3},
            {"Accel",       0.1f, 5.0f, 1.5f,   "s",  false},
            {"Drive",       0,    1,    0.2f,   "",   false, false, WidgetHint::DentedKnob},
            {"X-Over",      200,  1500, 800,    "Hz", false},
            {"H Slow",      20,   100,  48,     "rpm",false},
            {"H Fast",      200,  500,  340,    "rpm",false},
            {"D Slow",      15,   100,  40,     "rpm",false},
            {"D Fast",      150,  400,  290,    "rpm",false},
            {"Mic Wd",      0,    1,    0.7f,   "",   false, false, WidgetHint::DentedKnob},
            {"Mix",         0,    1,    0.5f,   "",   false, false, WidgetHint::DentedKnob},
        };
        return infos[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        m_params[index] = value;
    }

private:
    // Linear-interpolated read from a ring buffer, given the current
    // write head and a real-valued delay-in-samples.
    static float readDelay(const float* buf, int writeHead, float delaySamples) {
        float readPos = static_cast<float>(writeHead) - delaySamples;
        while (readPos < 0.0f)              readPos += kDelayBufFrames;
        while (readPos >= kDelayBufFrames)  readPos -= kDelayBufFrames;
        const int   i0 = static_cast<int>(readPos);
        const int   i1 = (i0 + 1) % kDelayBufFrames;
        const float frac = readPos - i0;
        return buf[i0] * (1.0f - frac) + buf[i1] * frac;
    }

    float targetHornRPM() const {
        const int mode = static_cast<int>(std::round(m_params[kSpeed]));
        switch (mode) {
            case kSpeedStop: return 0.0f;
            case kSpeedSlow: return std::clamp(m_params[kHornSlowRPM], 0.0f, 600.0f);
            default:         return std::clamp(m_params[kHornFastRPM], 0.0f, 600.0f);
        }
    }
    float targetDrumRPM() const {
        const int mode = static_cast<int>(std::round(m_params[kSpeed]));
        switch (mode) {
            case kSpeedStop: return 0.0f;
            case kSpeedSlow: return std::clamp(m_params[kDrumSlowRPM], 0.0f, 600.0f);
            default:         return std::clamp(m_params[kDrumFastRPM], 0.0f, 600.0f);
        }
    }

    float m_params[kParamCount] = {};

    std::array<float, kDelayBufFrames> m_hornBufL{};
    std::array<float, kDelayBufFrames> m_hornBufR{};
    std::array<float, kDelayBufFrames> m_drumBufL{};
    std::array<float, kDelayBufFrames> m_drumBufR{};
    int    m_hornWrite = 0;
    int    m_drumWrite = 0;

    double m_hornAngle = 0.0;
    double m_drumAngle = 0.0;
    float  m_hornRPM   = 0.0f;
    float  m_drumRPM   = 0.0f;

    float  m_lpL = 0.0f;
    float  m_lpR = 0.0f;
};

} // namespace effects
} // namespace yawn
