// PolivoksDsp.h - circuit-informed DSP blocks for the YAWN Polivoks 1982 port.
//
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 bokontep
//
// This is the YAWN-native port of the Polivoks 1982 DSP. The standalone VST
// source is GPL-3.0-or-later; the copyright holder licenses this YAWN
// integration under MIT. The filter/oscillator/envelope code is original
// project DSP; the small math helpers follow the MPC Plugin SDK's MIT helpers.
//
// Signal flow (the original is duophonic: two VCOs share one filter, one VCA
// and one pair of envelopes):
//
//   VCO2 (with LFO + detune) --+--> X-MOD into VCO1 pitch
//   VCO1 (with LFO) -----------+
//   VCO1 + VCO2 + noise -> unity-gain mixer -> Polivoks VCF (LP/BP)
//     -> LFO VCA -> envelope VCA -> 4x decimator -> 10 Hz output coupling
//
// The VCF model is a 4x oversampled TPT state-variable filter with:
//   * input saturation (the mixer is an unbuffered unity-gain summer),
//   * a small asymmetric step that grows as cutoff falls (K140UD12 output stage),
//   * saturating integrator states so resonance compresses with level and
//     self-oscillation settles into a stable limit cycle.
//
// The VCOs are free-running (no phase reset on note-on) with PolyBLEP saw and
// pulse edges, the measured 52% triangle rise and a small peak spike, and the
// measured pulse duties 45 / 20 / 8.8%. Everything is allocation free.

#ifndef POLIVOKS_DSP_H
#define POLIVOKS_DSP_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace plv {

constexpr float kPi = 3.14159265358979323846f;

template <typename T>
constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Cubic soft clip, the same shape used by the standalone plugin.
inline float softClip(float x)
{
    if (x >= 1.5f) return 1.0f;
    if (x <= -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

inline float flushDenormal(float x)
{
    return std::fabs(x) < 1.0e-15f ? 0.0f : x;
}

class Xorshift32
{
public:
    explicit Xorshift32(uint32_t seed = 0x9E3779B9u) { reseed(seed); }
    void reseed(uint32_t seed) { state_ = seed != 0 ? seed : 0x9E3779B9u; }
    uint32_t next()
    {
        uint32_t x = state_;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state_ = x;
        return x;
    }
    float nextBipolar()
    {
        return (float)(next() >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
private:
    uint32_t state_;
};

constexpr int kOversample = 4;

enum Waveform
{
    kWaveTriangle = 0,
    kWaveSaw      = 1,
    kWaveSquare   = 2,
    kWavePulse1   = 3,
    kWavePulse2   = 4
};

// 2^x for small |x|; the polynomial is accurate to about 1e-4, plenty for
// exponential FM and pitch conversion.
inline float fastExp2(float x)
{
    x = clamp(x, -60.0f, 60.0f);
    const float xi = std::floor(x);
    const float f  = x - xi;
    const float p  = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * 0.00961813f)));
    return std::ldexp(p, (int)xi);
}

inline float midiToHzFast(float note) { return 440.0f * fastExp2((note - 69.0f) * (1.0f / 12.0f)); }

// ---------------------------------------------------------------------------
// VCO: free-running saw core with derived triangle and comparator pulses.
// ---------------------------------------------------------------------------
class PolivoksVco
{
public:
    void prepare(float oversampledRate)
    {
        sr_ = oversampledRate;
        const float lpHz = std::fmin(18000.0f, 0.45f * sr_);
        lpCoeff_ = 1.0f - std::exp(-2.0f * kPi * lpHz / sr_);
        hpCoeff_ = std::exp(-2.0f * kPi * 10.0f / sr_);
        reset(0.0f);
    }

    void reset(float phase)
    {
        phase_ = phase - std::floor(phase);
        lp_ = 0.0f;
        hpPrev_ = 0.0f;
        hpState_ = 0.0f;
    }

    void setWave(int wave) { wave_ = clamp(wave, 0, 4); }
    void setFrequency(float hz) { freq_ = clamp(hz, 0.01f, 20000.0f); }

    float process()
    {
        const float dt = freq_ / sr_;

        float y;
        switch (wave_)
        {
            case kWaveSaw:
                y = 2.0f * phase_ - 1.0f - polyBlep(phase_, dt);
                // small flyback transient at the reset
                if (phase_ < 0.02f) y += 0.035f * (1.0f - phase_ * 50.0f);
                break;

            case kWaveTriangle:
            {
                // 52% rise / 48% fall, then full-wave rectify to the triangle.
                const float p = (phase_ < 0.48f) ? phase_ * (0.5f / 0.48f)
                                                 : 0.5f + (phase_ - 0.48f) * (0.5f / 0.52f);
                const float saw = 2.0f * p - 1.0f;
                y = 2.0f * std::fabs(saw) - 1.0f;
                // 52% rise; the original has a narrow spike at the peak
                const float d = std::fmin(phase_, 1.0f - phase_);
                if (d < 0.012f) y += 0.09f * (1.0f - d * (1.0f / 0.012f));
                break;
            }

            case kWaveSquare:
            case kWavePulse1:
            case kWavePulse2:
            default:
            {
                const float duty = (wave_ == kWaveSquare) ? 0.45f
                                 : (wave_ == kWavePulse1 ? 0.20f : 0.088f);
                float raw = (phase_ < duty) ? 1.0f : 0.0f;
                raw += polyBlep(phase_, dt);
                float fall = phase_ - duty;
                if (fall < 0.0f) fall += 1.0f;
                raw -= polyBlep(fall, dt);
                y = (raw - duty) * 2.0f;   // zero-mean, comparable to the saw
                break;
            }
        }

        // VCO output stage bandwidth and the original ~10 Hz output coupling.
        lp_ += lpCoeff_ * (y - lp_);
        y = lp_;
        const float hp = y - hpPrev_ + hpCoeff_ * hpState_;
        hpState_ = flushDenormal(hp);
        hpPrev_ = y;

        phase_ += dt;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        return hp;
    }

private:
    static float polyBlep(float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    float sr_ = 176400.0f;
    float freq_ = 440.0f;
    float phase_ = 0.0f;
    float lp_ = 0.0f;
    float lpCoeff_ = 0.47f;
    float hpPrev_ = 0.0f;
    float hpState_ = 0.0f;
    float hpCoeff_ = 0.99964f;
    int wave_ = kWaveSaw;
};

// ---------------------------------------------------------------------------
// VCF: Polivoks two-integrator state-variable filter.
// ---------------------------------------------------------------------------
class PolivoksFilter
{
public:
    void prepare(float oversampledRate)
    {
        sr_ = oversampledRate;
        modeCoeff_ = 1.0f - std::exp(-1.0f / (0.003f * sr_));   // 3 ms mode crossfade
        reset();
    }

    void reset()
    {
        ic1_ = ic2_ = 0.0f;
        modeMix_ = modeTarget_;
    }

    // cutoff Hz, resonance 0..1 (1 = self-oscillation)
    void set(float cutoffHz, float resonance)
    {
        const float maxHz = std::fmin(18000.0f, 0.45f * sr_);
        cutoff_ = clamp(cutoffHz, 20.0f, maxHz);
        res_ = clamp(resonance, 0.0f, 1.0f);

        // The original resonance is eager and can rail into self-oscillation;
        // a little more feedback than a textbook SVF is intentional.
        k_ = 2.0f - 3.00f * res_;
        if (k_ < -1.00f) k_ = -1.00f;

        // Negative damping lowers the self-oscillation frequency. Compensate
        // the integrator time constant so the whistle still tracks the knob.
        float cutoffEff = cutoff_;
        if (k_ < 0.0f)
        {
            const float q = std::fmax(1.0f - 0.25f * k_ * k_, 0.25f);
            cutoffEff *= 1.03f / std::sqrt(q);
            if (cutoffEff > maxHz) cutoffEff = maxHz;
        }
        g_ = std::tan(kPi * cutoffEff / sr_);

        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;

        // The K140UD12 output stage develops a small step at low control
        // currents; model it as an asymmetric bias in the input stage that
        // grows towards 100 Hz. The final AC coupling removes the DC.
        lowCut_ = clamp(1.0f - cutoff_ * (1.0f / 10000.0f), 0.0f, 1.0f);
    }

    void setMode(float lpToBp) { modeTarget_ = clamp(lpToBp, 0.0f, 1.0f); }

    float process(float x)
    {
        modeMix_ += modeCoeff_ * (modeTarget_ - modeMix_);

        // unbuffered mixer into the filter: soft overload behind the 1/48 divider
        x = softClip(x * 1.2f);

        // low-cutoff output-stage step / asymmetry
        const float bias = 0.06f * lowCut_;
        x = softClip(x + bias) - bias;

        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;

        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;

        // saturating integrators: resonance compression and a bounded
        // self-oscillation instead of a numerical explosion
        ic1_ = flushDenormal(softClip(ic1_ * 0.8f) * 1.25f);
        ic2_ = flushDenormal(softClip(ic2_ * 0.8f) * 1.25f);

        const float lp = v2;
        const float bp = v1;
        return lp + modeMix_ * (bp - lp);
    }

private:
    float sr_ = 176400.0f;
    float cutoff_ = 1000.0f;
    float res_ = 0.0f;
    float g_ = 0.1f, k_ = 2.0f;
    float a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f;
    float lowCut_ = 0.0f;
    float modeTarget_ = 0.0f, modeMix_ = 0.0f, modeCoeff_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Envelope: analog-style ADSR with the Polivoks periodic (A+D loop) mode.
// ---------------------------------------------------------------------------
class PolivoksEnvelope
{
public:
    enum Stage { Idle = 0, Attack, Decay, Sustain, Release };

    void prepare(float oversampledRate) { sr_ = oversampledRate; }

    void set(float attackMs, float decayMs, float sustain01, float releaseMs, bool loop)
    {
        attackMs_ = attackMs;
        decayMs_ = decayMs;
        sustain_ = clamp(sustain01, 0.0f, 1.0f);
        releaseMs_ = releaseMs;
        loop_ = loop;

        attackCoef_ = coef(attackMs_, kAttackRatio);
        attackBase_ = (1.0f + kAttackRatio) * (1.0f - attackCoef_);

        decayCoef_ = coef(decayMs_, kDecayRatio);
        decayBase_ = (sustain_ - kDecayRatio) * (1.0f - decayCoef_);

        releaseCoef_ = coef(releaseMs_, kDecayRatio);
        releaseBase_ = -kDecayRatio * (1.0f - releaseCoef_);
    }

    void gateOn() { stage_ = Attack; }
    void gateOff() { if (stage_ != Idle) stage_ = Release; }
    void reset() { stage_ = Idle; level_ = 0.0f; }

    bool isIdle() const { return stage_ == Idle; }
    float level() const { return level_; }

    float process()
    {
        switch (stage_)
        {
            case Attack:
                level_ = attackBase_ + level_ * attackCoef_;
                if (level_ >= 1.0f) { level_ = 1.0f; stage_ = Decay; }
                break;

            case Decay:
                level_ = decayBase_ + level_ * decayCoef_;
                if (level_ <= sustain_)
                {
                    // Periodic mode loops A+D while the gate is held; the
                    // original only loops with sustain at zero.
                    if (loop_ && sustain_ < 0.001f) stage_ = Attack;
                    else { level_ = sustain_; stage_ = Sustain; }
                }
                break;

            case Sustain:
                level_ = sustain_;
                break;

            case Release:
                level_ = releaseBase_ + level_ * releaseCoef_;
                if (level_ <= kSilence) { level_ = 0.0f; stage_ = Idle; }
                break;

            case Idle:
            default:
                break;
        }
        return level_;
    }

private:
    static constexpr float kAttackRatio = 0.3f;
    static constexpr float kDecayRatio  = 0.0001f;
    static constexpr float kSilence     = 0.0005f;

    float coef(float ms, float targetRatio) const
    {
        const float samples = ms * 0.001f * sr_;
        if (samples < 1.0f) return 0.0f;
        return std::exp(-std::log((1.0f + targetRatio) / targetRatio) / samples);
    }

    float sr_ = 176400.0f;
    float attackMs_ = 5.0f, decayMs_ = 300.0f, sustain_ = 0.8f, releaseMs_ = 300.0f;
    float attackCoef_ = 0.0f, attackBase_ = 1.3f;
    float decayCoef_ = 0.0f, decayBase_ = 0.8f;
    float releaseCoef_ = 0.0f, releaseBase_ = 0.0f;
    float level_ = 0.0f;
    bool loop_ = false;
    Stage stage_ = Idle;
};

// ---------------------------------------------------------------------------
// LFO: triangle, positive square, stepped S&H and low-frequency noise.
// The square is deliberately unipolar positive (the original trills upward);
// triangle/noise/S&H are bipolar for pitch and filter modulation.
// ---------------------------------------------------------------------------
class PolivoksLfo
{
public:
    void prepare(float oversampledRate)
    {
        sr_ = oversampledRate;
        noiseCoeff_ = 1.0f - std::exp(-2.0f * kPi * 180.0f / sr_);
        reset();
    }

    void reset()
    {
        phase_ = 0.0f;
        sh_ = rng_.nextBipolar();
        noiseLp_ = 0.0f;
    }

    void setRate(float hz) { rate_ = clamp(hz, 0.01f, 100.0f); }
    void setShape(int shape) { shape_ = clamp(shape, 0, 3); }

    // signed: triangle/S&H/noise -1..1, square 0..+1
    // unipolar: triangle/S&H/noise 0..1, square 0/1
    void process(float& signedOut, float& unipolarOut)
    {
        switch (shape_)
        {
            case 0: // triangle
            {
                const float tri = 1.0f - 4.0f * std::fabs(phase_ - 0.5f);
                signedOut = tri;
                unipolarOut = 0.5f + 0.5f * tri;
                break;
            }
            case 1: // positive square
            {
                const float sq = (phase_ < 0.5f) ? 1.0f : 0.0f;
                signedOut = sq;
                unipolarOut = sq;
                break;
            }
            case 2: // sample & hold
            {
                signedOut = sh_;
                unipolarOut = 0.5f + 0.5f * sh_;
                break;
            }
            case 3: // low-frequency noise
            default:
            {
                noiseLp_ += noiseCoeff_ * (rng_.nextBipolar() - noiseLp_);
                const float n = clamp(noiseLp_ * 4.0f, -1.0f, 1.0f);
                signedOut = n;
                unipolarOut = 0.5f + 0.5f * n;
                break;
            }
        }

        phase_ += rate_ / sr_;
        if (phase_ >= 1.0f)
        {
            phase_ -= 1.0f;
            if (shape_ == 2) sh_ = rng_.nextBipolar();
        }
    }

private:
    float sr_ = 176400.0f;
    float rate_ = 5.0f;
    float phase_ = 0.0f;
    float sh_ = 0.0f;
    float noiseLp_ = 0.0f;
    float noiseCoeff_ = 0.005f;
    int shape_ = 0;
    Xorshift32 rng_{0x51f15e5du};
};

// ---------------------------------------------------------------------------
// Output-stage 10 Hz AC coupling.
// ---------------------------------------------------------------------------
class DcBlocker
{
public:
    void prepare(float sampleRate)
    {
        coeff_ = std::exp(-2.0f * kPi * 10.0f / sampleRate);
        reset();
    }
    void reset() { xPrev_ = yPrev_ = 0.0f; }
    float process(float x)
    {
        const float y = flushDenormal(x - xPrev_ + coeff_ * yPrev_);
        xPrev_ = x;
        yPrev_ = y;
        return y;
    }
private:
    float coeff_ = 0.99857f;
    float xPrev_ = 0.0f, yPrev_ = 0.0f;
};

} // namespace plv

#endif // POLIVOKS_DSP_H
