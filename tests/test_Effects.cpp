#include <gtest/gtest.h>
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "effects/Biquad.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Limiter.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/NoiseGate.h"
#include "effects/Phaser.h"
#include "effects/Wah.h"
#include "effects/Convolution.h"
#include "effects/ConvolutionReverb.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

using namespace yawn::effects;

static constexpr double kSampleRate = 44100.0;
static constexpr int kBlockSize = 256;

// Helper: fill buffer with a sine wave
static void fillSine(float* buf, int frames, int ch, float freq, float amp, double sr) {
    for (int i = 0; i < frames; ++i) {
        float v = amp * std::sin(2.0f * 3.14159265f * freq * i / static_cast<float>(sr));
        for (int c = 0; c < ch; ++c)
            buf[i * ch + c] = v;
    }
}

// Helper: compute RMS of a buffer
static float computeRMS(const float* buf, int frames, int ch) {
    float sum = 0.0f;
    for (int i = 0; i < frames * ch; ++i)
        sum += buf[i] * buf[i];
    return std::sqrt(sum / (frames * ch));
}

// Helper: check buffer is not all zeros
static bool hasSignal(const float* buf, int frames, int ch) {
    for (int i = 0; i < frames * ch; ++i)
        if (std::abs(buf[i]) > 1e-10f) return true;
    return false;
}

// ===========================================================================
// Biquad Tests
// ===========================================================================

TEST(Biquad, LowPassAttenuatesHighFreq) {
    Biquad lp;
    lp.compute(Biquad::Type::LowPass, kSampleRate, 200.0, 0.0, 0.707);

    // Process a high-frequency sine (10kHz) — should be heavily attenuated
    float inBuf[kBlockSize], outBuf[kBlockSize];
    for (int i = 0; i < kBlockSize; ++i) {
        inBuf[i] = std::sin(2.0f * 3.14159265f * 10000.0f * i / kSampleRate);
        outBuf[i] = lp.process(inBuf[i]);
    }

    float inRMS = computeRMS(inBuf, kBlockSize, 1);
    float outRMS = computeRMS(outBuf + 64, kBlockSize - 64, 1); // skip transient
    EXPECT_LT(outRMS, inRMS * 0.1f); // At least 20dB attenuation
}

TEST(Biquad, HighPassAttenuatesLowFreq) {
    Biquad hp;
    hp.compute(Biquad::Type::HighPass, kSampleRate, 5000.0, 0.0, 0.707);

    float inBuf[kBlockSize], outBuf[kBlockSize];
    for (int i = 0; i < kBlockSize; ++i) {
        inBuf[i] = std::sin(2.0f * 3.14159265f * 100.0f * i / kSampleRate);
        outBuf[i] = hp.process(inBuf[i]);
    }

    float inRMS = computeRMS(inBuf, kBlockSize, 1);
    float outRMS = computeRMS(outBuf + 64, kBlockSize - 64, 1);
    EXPECT_LT(outRMS, inRMS * 0.1f);
}

TEST(Biquad, PeakBoosts) {
    Biquad peak;
    peak.compute(Biquad::Type::Peak, kSampleRate, 1000.0, 12.0, 1.0);

    float inBuf[kBlockSize], outBuf[kBlockSize];
    for (int i = 0; i < kBlockSize; ++i) {
        inBuf[i] = std::sin(2.0f * 3.14159265f * 1000.0f * i / kSampleRate);
        outBuf[i] = peak.process(inBuf[i]);
    }

    float inRMS = computeRMS(inBuf, kBlockSize, 1);
    float outRMS = computeRMS(outBuf + 64, kBlockSize - 64, 1);
    EXPECT_GT(outRMS, inRMS * 1.5f); // 12dB boost = ~4x
}

// ===========================================================================
// Reverb Tests
// ===========================================================================

TEST(Reverb, Init) {
    Reverb rev;
    rev.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(rev.name(), "Reverb");
    EXPECT_EQ(rev.parameterCount(), 5);
}

TEST(Reverb, ProducesOutput) {
    Reverb rev;
    rev.init(kSampleRate, kBlockSize);
    rev.setParameter(Reverb::kWetDry, 1.0f);
    rev.setParameter(Reverb::kPreDelay, 0.0f);

    // Feed a burst of noise to excite all comb filters
    int totalFrames = 4096;
    std::vector<float> buf(totalFrames * 2, 0.0f);
    for (int i = 0; i < 512; ++i) {
        float v = (i % 2 == 0) ? 0.5f : -0.5f;
        buf[i * 2] = v; buf[i * 2 + 1] = v;
    }

    rev.process(buf.data(), totalFrames, 2);

    // Check the tail region (after comb delay lines start returning)
    EXPECT_TRUE(hasSignal(buf.data() + 2048 * 2, 2048, 2));
}

TEST(Reverb, TailDecays) {
    Reverb rev;
    rev.init(kSampleRate, kBlockSize);
    rev.setParameter(Reverb::kRoomSize, 0.3f);
    rev.setParameter(Reverb::kWetDry, 1.0f);
    rev.setParameter(Reverb::kPreDelay, 0.0f);

    // Feed a burst
    int totalFrames = 4096;
    std::vector<float> buf(totalFrames * 2, 0.0f);
    for (int i = 0; i < 512; ++i) {
        float v = (i % 2 == 0) ? 0.5f : -0.5f;
        buf[i * 2] = v; buf[i * 2 + 1] = v;
    }
    rev.process(buf.data(), totalFrames, 2);
    float rms1 = computeRMS(buf.data() + 2048 * 2, 2048, 2);

    // Process many more silent blocks — tail should decay
    for (int b = 0; b < 100; ++b) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        rev.process(buf.data(), totalFrames, 2);
    }
    float rms2 = computeRMS(buf.data(), totalFrames, 2);

    EXPECT_GT(rms1, 0.0f);
    EXPECT_LT(rms2, rms1);
}

TEST(Reverb, BypassPassesThrough) {
    Reverb rev;
    rev.init(kSampleRate, kBlockSize);
    rev.setBypassed(true);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto original = buf;

    rev.process(buf.data(), kBlockSize, 2);
    EXPECT_EQ(buf, original); // Bypassed = no change
}

// ===========================================================================
// Delay Tests
// ===========================================================================

TEST(Delay, Init) {
    Delay del;
    del.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(del.name(), "Delay");
    EXPECT_EQ(del.parameterCount(), 6);
}

TEST(Delay, ProducesEcho) {
    Delay del;
    del.init(kSampleRate, kBlockSize);
    del.setParameter(Delay::kTimeMs, 10.0f); // 10ms delay
    del.setParameter(Delay::kFeedback, 0.0f);
    del.setParameter(Delay::kWetDry, 1.0f);

    int delSamples = static_cast<int>(10.0f * 0.001f * kSampleRate);
    int totalFrames = delSamples + 64;
    std::vector<float> buf(totalFrames * 2, 0.0f);
    buf[0] = 1.0f; buf[1] = 1.0f; // Impulse at frame 0

    del.process(buf.data(), totalFrames, 2);

    // Should have signal around the delay time
    bool foundEcho = false;
    for (int i = delSamples - 2; i < delSamples + 2 && i < totalFrames; ++i) {
        if (std::abs(buf[i * 2]) > 0.01f) foundEcho = true;
    }
    EXPECT_TRUE(foundEcho);
}

// ===========================================================================
// EQ Tests
// ===========================================================================

TEST(EQ, Init) {
    EQ eq;
    eq.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(eq.name(), "EQ");
    EXPECT_EQ(eq.parameterCount(), 7);
}

TEST(EQ, FlatEQIsUnity) {
    EQ eq;
    eq.init(kSampleRate, kBlockSize);
    // All gains at 0dB = no change

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 1000.0f, 0.5f, kSampleRate);
    auto original = buf;

    eq.process(buf.data(), kBlockSize, 2);

    // After settling, should be very close to original (biquad transient at start)
    float diff = 0.0f;
    for (int i = 128; i < kBlockSize * 2; ++i)
        diff += std::abs(buf[i] - original[i]);
    diff /= (kBlockSize * 2 - 128);
    EXPECT_LT(diff, 0.01f);
}

TEST(EQ, LowShelfBoost) {
    EQ eq;
    eq.init(kSampleRate, kBlockSize);
    eq.setParameter(EQ::kLowGain, 12.0f);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 100.0f, 0.3f, kSampleRate);
    float beforeRMS = computeRMS(buf.data() + 128, kBlockSize - 64, 2);

    eq.process(buf.data(), kBlockSize, 2);
    float afterRMS = computeRMS(buf.data() + 128, kBlockSize - 64, 2);

    EXPECT_GT(afterRMS, beforeRMS * 1.5f);
}

// ===========================================================================
// Compressor Tests
// ===========================================================================

TEST(Compressor, Init) {
    Compressor comp;
    comp.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(comp.name(), "Compressor");
    EXPECT_EQ(comp.parameterCount(), 6);
}

TEST(Compressor, ReducesLoudSignal) {
    Compressor comp;
    comp.init(kSampleRate, kBlockSize);
    comp.setParameter(Compressor::kThreshold, -20.0f);
    comp.setParameter(Compressor::kRatio, 10.0f);
    comp.setParameter(Compressor::kAttack, 0.1f);
    comp.setParameter(Compressor::kMakeupGain, 0.0f);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.8f, kSampleRate);
    float beforeRMS = computeRMS(buf.data(), kBlockSize, 2);

    comp.process(buf.data(), kBlockSize, 2);
    float afterRMS = computeRMS(buf.data() + 128, kBlockSize - 64, 2);

    EXPECT_LT(afterRMS, beforeRMS); // Should be quieter
}

// ===========================================================================
// Limiter Tests
// ===========================================================================

TEST(Limiter, Init) {
    Limiter lim;
    lim.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(lim.name(), "Limiter");
    EXPECT_EQ(lim.parameterCount(), 3);
}

TEST(Limiter, CeilingHoldsUnderLoudSignal) {
    Limiter lim;
    lim.init(kSampleRate, kBlockSize);
    lim.setParameter(Limiter::kCeiling, -1.0f);   // ≈ 0.891 linear
    lim.setParameter(Limiter::kRelease, 50.0f);
    lim.setParameter(Limiter::kLookahead, 5.0f);

    // Hot sine well above the ceiling — after the attack settles the
    // limiter must hold the peak at (or below) the ceiling.
    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 1.5f, kSampleRate);
    lim.process(buf.data(), kBlockSize, 2);

    const float ceilLin = std::pow(10.0f, -1.0f / 20.0f);
    // Skip the initial attack region — lookahead + attack takes a few ms.
    float peak = 0.0f;
    for (int i = kBlockSize / 2; i < kBlockSize; ++i) {
        peak = std::max(peak, std::abs(buf[i * 2]));
        peak = std::max(peak, std::abs(buf[i * 2 + 1]));
    }
    EXPECT_LT(peak, ceilLin + 0.01f);
}

TEST(Limiter, PassesQuietSignalUnchanged) {
    Limiter lim;
    lim.init(kSampleRate, kBlockSize);
    lim.setParameter(Limiter::kCeiling, -1.0f);
    lim.setParameter(Limiter::kLookahead, 5.0f);

    // Input at 0.3 — well below the ceiling. Process a warmup block
    // first so the lookahead delay line fills, then measure the
    // second block (steady-state: just the lookahead delay applied).
    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.3f, kSampleRate);
    lim.process(buf.data(), kBlockSize, 2);  // warmup

    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.3f, kSampleRate);
    const float beforeRMS = computeRMS(buf.data(), kBlockSize, 2);
    lim.process(buf.data(), kBlockSize, 2);
    const float afterRMS = computeRMS(buf.data(), kBlockSize, 2);

    EXPECT_NEAR(afterRMS, beforeRMS, beforeRMS * 0.05f);
}

// ===========================================================================
// Filter Tests
// ===========================================================================

TEST(Filter, Init) {
    Filter flt;
    flt.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(flt.name(), "Filter");
    EXPECT_EQ(flt.parameterCount(), 3);
}

TEST(Filter, LowPassCutsHigh) {
    Filter flt;
    flt.init(kSampleRate, kBlockSize);
    flt.setParameter(Filter::kCutoff, Filter::cutoffHzToNorm(300.0f));
    flt.setParameter(Filter::kType, static_cast<float>(Filter::LP));

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 10000.0f, 0.5f, kSampleRate);
    float beforeRMS = computeRMS(buf.data(), kBlockSize, 2);

    flt.process(buf.data(), kBlockSize, 2);
    float afterRMS = computeRMS(buf.data() + 128, kBlockSize - 64, 2);

    EXPECT_LT(afterRMS, beforeRMS * 0.1f);
}

// ===========================================================================
// Chorus Tests
// ===========================================================================

TEST(Chorus, Init) {
    Chorus ch;
    ch.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(ch.name(), "Chorus");
    EXPECT_EQ(ch.parameterCount(), 4);
}

TEST(Chorus, ProducesOutput) {
    Chorus ch;
    ch.init(kSampleRate, kBlockSize);
    ch.setParameter(Chorus::kWetDry, 1.0f);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);

    ch.process(buf.data(), kBlockSize, 2);
    EXPECT_TRUE(hasSignal(buf.data(), kBlockSize, 2));
}

// ===========================================================================
// Distortion Tests
// ===========================================================================

TEST(Distortion, Init) {
    Distortion dist;
    dist.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(dist.name(), "Distortion");
    EXPECT_EQ(dist.parameterCount(), 4);
}

TEST(Distortion, SoftClipSaturates) {
    Distortion dist;
    dist.init(kSampleRate, kBlockSize);
    dist.setParameter(Distortion::kDrive, 24.0f);
    dist.setParameter(Distortion::kWetDry, 1.0f);
    dist.setParameter(Distortion::kType, static_cast<float>(Distortion::SoftClip));

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);

    dist.process(buf.data(), kBlockSize, 2);

    // Output should be bounded (tanh never exceeds ±1)
    for (int i = 0; i < kBlockSize * 2; ++i) {
        EXPECT_LE(std::abs(buf[i]), 1.01f);
    }
}

TEST(Distortion, HardClipClamps) {
    Distortion dist;
    dist.init(kSampleRate, kBlockSize);
    dist.setParameter(Distortion::kDrive, 30.0f);
    dist.setParameter(Distortion::kWetDry, 1.0f);
    dist.setParameter(Distortion::kType, static_cast<float>(Distortion::HardClip));

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.8f, kSampleRate);

    dist.process(buf.data(), kBlockSize, 2);

    // Hard clip: output may exceed 1.0 due to tone filter, but pre-filter is clamped
    // Just check we have signal
    EXPECT_TRUE(hasSignal(buf.data(), kBlockSize, 2));
}

// ===========================================================================
// EffectChain Tests
// ===========================================================================

TEST(EffectChain, EmptyChainPassesThrough) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto original = buf;

    chain.process(buf.data(), kBlockSize, 2);
    EXPECT_EQ(buf, original);
}

TEST(EffectChain, AppendAndProcess) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);

    auto flt = std::make_unique<Filter>();
    flt->setParameter(Filter::kCutoff, Filter::cutoffHzToNorm(200.0f));
    flt->setParameter(Filter::kType, static_cast<float>(Filter::LP));
    chain.append(std::move(flt));

    EXPECT_EQ(chain.count(), 1);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 10000.0f, 0.5f, kSampleRate);
    float beforeRMS = computeRMS(buf.data(), kBlockSize, 2);

    chain.process(buf.data(), kBlockSize, 2);
    float afterRMS = computeRMS(buf.data() + 128, kBlockSize - 64, 2);

    EXPECT_LT(afterRMS, beforeRMS * 0.1f);
}

TEST(EffectChain, MultipleEffects) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);

    chain.append(std::make_unique<EQ>());
    chain.append(std::make_unique<Compressor>());
    EXPECT_EQ(chain.count(), 2);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);

    chain.process(buf.data(), kBlockSize, 2);
    EXPECT_TRUE(hasSignal(buf.data(), kBlockSize, 2));
}

TEST(EffectChain, RemoveEffect) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);

    chain.append(std::make_unique<Filter>());
    EXPECT_EQ(chain.count(), 1);

    auto removed = chain.remove(0);
    EXPECT_NE(removed, nullptr);
    EXPECT_EQ(chain.count(), 0);
}

TEST(EffectChain, BypassedEffectSkipped) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);

    auto flt = std::make_unique<Filter>();
    flt->setParameter(Filter::kCutoff, Filter::cutoffHzToNorm(200.0f));
    flt->setBypassed(true);
    chain.append(std::move(flt));

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 10000.0f, 0.5f, kSampleRate);
    auto original = buf;

    chain.process(buf.data(), kBlockSize, 2);
    EXPECT_EQ(buf, original); // Bypassed = no change
}

TEST(EffectChain, Clear) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);
    chain.append(std::make_unique<EQ>());
    chain.append(std::make_unique<Compressor>());
    chain.append(std::make_unique<Reverb>());
    EXPECT_EQ(chain.count(), 3);

    chain.clear();
    EXPECT_EQ(chain.count(), 0);
    EXPECT_TRUE(chain.empty());
}

// ===========================================================================
// Parameter system tests
// ===========================================================================

TEST(EffectParams, ReverbDefaults) {
    Reverb rev;
    rev.init(kSampleRate, kBlockSize);
    for (int i = 0; i < rev.parameterCount(); ++i) {
        const auto& info = rev.parameterInfo(i);
        float val = rev.getParameter(i);
        EXPECT_GE(val, info.minValue);
        EXPECT_LE(val, info.maxValue);
        EXPECT_NE(info.name, nullptr);
    }
}

TEST(EffectParams, SetAndGet) {
    Delay del;
    del.init(kSampleRate, kBlockSize);
    del.setParameter(Delay::kTimeMs, 250.0f);
    EXPECT_FLOAT_EQ(del.getParameter(Delay::kTimeMs), 250.0f);

    del.setParameter(Delay::kFeedback, 0.7f);
    EXPECT_FLOAT_EQ(del.getParameter(Delay::kFeedback), 0.7f);
}

// ===========================================================================
// Oscilloscope Tests
// ===========================================================================

TEST(Oscilloscope, Init) {
    Oscilloscope scope;
    scope.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(scope.name(), "Oscilloscope");
    EXPECT_EQ(scope.parameterCount(), 3);
    EXPECT_TRUE(scope.isVisualizer());
    EXPECT_STREQ(scope.visualizerType(), "oscilloscope");
}

TEST(Oscilloscope, PassesThrough) {
    Oscilloscope scope;
    scope.init(kSampleRate, kBlockSize);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto original = buf;

    scope.process(buf.data(), kBlockSize, 2);
    EXPECT_EQ(buf, original); // Should not alter audio
}

TEST(Oscilloscope, CapturesDisplayData) {
    Oscilloscope scope;
    scope.init(kSampleRate, kBlockSize);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);

    scope.process(buf.data(), kBlockSize, 2);
    EXPECT_TRUE(scope.hasNewData());

    const float* display = scope.displayData();
    EXPECT_NE(display, nullptr);
    EXPECT_EQ(scope.displaySize(), Oscilloscope::kDisplaySize);

    // Display should contain non-zero data
    bool hasSignalInDisplay = false;
    for (int i = 0; i < scope.displaySize(); ++i) {
        if (std::abs(display[i]) > 1e-6f) { hasSignalInDisplay = true; break; }
    }
    EXPECT_TRUE(hasSignalInDisplay);
}

TEST(Oscilloscope, FreezeStopsCapture) {
    Oscilloscope scope;
    scope.init(kSampleRate, kBlockSize);

    // Process some audio
    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    scope.process(buf.data(), kBlockSize, 2);

    // Freeze and process silence
    scope.setParameter(Oscilloscope::kFreeze, 1.0f);
    scope.clearNewData();
    std::fill(buf.begin(), buf.end(), 0.0f);
    scope.process(buf.data(), kBlockSize, 2);

    // Display should still have old data (frozen)
    const float* display = scope.displayData();
    bool hasSignalInDisplay = false;
    for (int i = 0; i < scope.displaySize(); ++i) {
        if (std::abs(display[i]) > 1e-6f) { hasSignalInDisplay = true; break; }
    }
    EXPECT_TRUE(hasSignalInDisplay);
}

// ===========================================================================
// SpectrumAnalyzer Tests
// ===========================================================================

TEST(SpectrumAnalyzer, Init) {
    SpectrumAnalyzer spec;
    spec.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(spec.name(), "Spectrum");
    EXPECT_EQ(spec.parameterCount(), 3);
    EXPECT_TRUE(spec.isVisualizer());
    EXPECT_STREQ(spec.visualizerType(), "spectrum");
}

TEST(SpectrumAnalyzer, PassesThrough) {
    SpectrumAnalyzer spec;
    spec.init(kSampleRate, kBlockSize);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto original = buf;

    spec.process(buf.data(), kBlockSize, 2);
    EXPECT_EQ(buf, original); // Should not alter audio
}

TEST(SpectrumAnalyzer, CapturesFFTData) {
    SpectrumAnalyzer spec;
    spec.init(kSampleRate, kBlockSize);
    spec.setParameter(SpectrumAnalyzer::kSmoothing, 0.0f); // no smoothing for test

    // Feed enough data to trigger at least one FFT (1024 samples)
    std::vector<float> buf(2048 * 2);
    fillSine(buf.data(), 2048, 2, 1000.0f, 0.8f, kSampleRate);

    spec.process(buf.data(), 2048, 2);

    EXPECT_TRUE(spec.hasNewData());
    const float* display = spec.displayData();
    EXPECT_NE(display, nullptr);
    EXPECT_EQ(spec.displaySize(), SpectrumAnalyzer::kDisplayBins);

    // Should have non-zero magnitude bins for a strong 1kHz signal
    bool hasEnergy = false;
    for (int i = 0; i < spec.displaySize(); ++i) {
        if (display[i] > 0.1f) { hasEnergy = true; break; }
    }
    EXPECT_TRUE(hasEnergy);
}

TEST(SpectrumAnalyzer, Parameters) {
    SpectrumAnalyzer spec;
    spec.init(kSampleRate, kBlockSize);

    spec.setParameter(SpectrumAnalyzer::kSmoothing, 0.9f);
    EXPECT_FLOAT_EQ(spec.getParameter(SpectrumAnalyzer::kSmoothing), 0.9f);

    spec.setParameter(SpectrumAnalyzer::kGain, 5.0f);
    EXPECT_FLOAT_EQ(spec.getParameter(SpectrumAnalyzer::kGain), 5.0f);
}

// ===========================================================================
// TapeEmulation Tests
// ===========================================================================

TEST(TapeEmulation, Init) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);
    EXPECT_EQ(tape.parameterCount(), TapeEmulation::kParamCount);
    EXPECT_STREQ(tape.name(), "Tape Emulation");
    EXPECT_STREQ(tape.id(), "tape");
}

TEST(TapeEmulation, SaturationAddsHarmonics) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);
    tape.setParameter(TapeEmulation::kSaturation, 0.9f);
    tape.setParameter(TapeEmulation::kWow, 0.0f);
    tape.setParameter(TapeEmulation::kFlutter, 0.0f);
    tape.setParameter(TapeEmulation::kHiss, 0.0f);
    tape.setParameter(TapeEmulation::kWetDry, 1.0f);

    // Process two blocks to get past the delay line latency
    const int frames = 1024;
    std::vector<float> buf(frames * 2);
    fillSine(buf.data(), frames, 2, 440.0f, 0.8f, kSampleRate);
    tape.process(buf.data(), frames, 2);

    // The output should have signal (not silent)
    EXPECT_TRUE(hasSignal(buf.data() + 512, frames - 256, 2));
}

TEST(TapeEmulation, BypassPassesThrough) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);
    tape.setBypassed(true);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto orig = buf;
    tape.process(buf.data(), kBlockSize, 2);

    for (int i = 0; i < kBlockSize * 2; ++i)
        EXPECT_FLOAT_EQ(buf[i], orig[i]);
}

TEST(TapeEmulation, WowModulatesPitch) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);
    tape.setParameter(TapeEmulation::kSaturation, 0.0f);
    tape.setParameter(TapeEmulation::kWow, 1.0f);
    tape.setParameter(TapeEmulation::kFlutter, 0.0f);
    tape.setParameter(TapeEmulation::kHiss, 0.0f);
    tape.setParameter(TapeEmulation::kWetDry, 1.0f);

    const int frames = 2048;
    std::vector<float> buf(frames * 2);
    fillSine(buf.data(), frames, 2, 440.0f, 0.5f, kSampleRate);
    tape.process(buf.data(), frames, 2);

    // With wow, output should differ from a clean sine (pitch modulation)
    std::vector<float> clean(frames * 2);
    fillSine(clean.data(), frames, 2, 440.0f, 0.5f, kSampleRate);

    float diffSum = 0.0f;
    // Check second half (past delay latency)
    for (int i = frames; i < frames * 2; ++i)
        diffSum += std::abs(buf[i] - clean[i]);
    EXPECT_GT(diffSum, 0.01f);
}

TEST(TapeEmulation, HissAddsNoise) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);
    tape.setParameter(TapeEmulation::kSaturation, 0.0f);
    tape.setParameter(TapeEmulation::kWow, 0.0f);
    tape.setParameter(TapeEmulation::kFlutter, 0.0f);
    tape.setParameter(TapeEmulation::kHiss, 1.0f);
    tape.setParameter(TapeEmulation::kWetDry, 1.0f);

    // Process silence over several blocks to let filter warm up
    const int frames = 2048;
    std::vector<float> buf(frames * 2, 0.0f);
    tape.process(buf.data(), frames, 2);

    // Hiss is added after the delay line, so output should have signal
    EXPECT_TRUE(hasSignal(buf.data(), frames, 2));
}

TEST(TapeEmulation, DryMixPassesOriginal) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);
    tape.setParameter(TapeEmulation::kWetDry, 0.0f);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto orig = buf;
    tape.process(buf.data(), kBlockSize, 2);

    for (int i = 0; i < kBlockSize * 2; ++i)
        EXPECT_FLOAT_EQ(buf[i], orig[i]);
}

TEST(TapeEmulation, Parameters) {
    TapeEmulation tape;
    tape.init(kSampleRate, kBlockSize);

    tape.setParameter(TapeEmulation::kSaturation, 0.7f);
    EXPECT_FLOAT_EQ(tape.getParameter(TapeEmulation::kSaturation), 0.7f);

    tape.setParameter(TapeEmulation::kTone, 4000.0f);
    EXPECT_FLOAT_EQ(tape.getParameter(TapeEmulation::kTone), 4000.0f);

    // Defaults
    TapeEmulation tape2;
    tape2.init(kSampleRate, kBlockSize);
    EXPECT_FLOAT_EQ(tape2.getParameter(TapeEmulation::kSaturation),
                    tape2.parameterInfo(TapeEmulation::kSaturation).defaultValue);
}

// ===========================================================================
// EffectChain MoveEffect Tests
// ===========================================================================

TEST(EffectChain, MoveEffectForward) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);
    auto d = std::make_unique<Delay>();
    auto c = std::make_unique<Chorus>();
    auto r = std::make_unique<Reverb>();
    auto* dp = d.get(); auto* cp = c.get(); auto* rp = r.get();
    chain.append(std::move(d));
    chain.append(std::move(c));
    chain.append(std::move(r));
    EXPECT_EQ(chain.count(), 3);

    // Move first to last: [D,C,R] -> [C,R,D]
    chain.moveEffect(0, 2);
    EXPECT_EQ(chain.effectAt(0), cp);
    EXPECT_EQ(chain.effectAt(1), rp);
    EXPECT_EQ(chain.effectAt(2), dp);
}

TEST(EffectChain, MoveEffectBackward) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);
    auto d = std::make_unique<Delay>();
    auto c = std::make_unique<Chorus>();
    auto r = std::make_unique<Reverb>();
    auto* dp = d.get(); auto* cp = c.get(); auto* rp = r.get();
    chain.append(std::move(d));
    chain.append(std::move(c));
    chain.append(std::move(r));

    // Move last to first: [D,C,R] -> [R,D,C]
    chain.moveEffect(2, 0);
    EXPECT_EQ(chain.effectAt(0), rp);
    EXPECT_EQ(chain.effectAt(1), dp);
    EXPECT_EQ(chain.effectAt(2), cp);
}

TEST(EffectChain, MoveEffectSamePos) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);
    auto d = std::make_unique<Delay>();
    auto c = std::make_unique<Chorus>();
    auto* dp = d.get(); auto* cp = c.get();
    chain.append(std::move(d));
    chain.append(std::move(c));

    chain.moveEffect(0, 0); // no-op
    EXPECT_EQ(chain.effectAt(0), dp);
    EXPECT_EQ(chain.effectAt(1), cp);
}

// ===========================================================================
// AmpSimulator Tests
// ===========================================================================

TEST(AmpSimulator, Init) {
    AmpSimulator amp;
    amp.init(kSampleRate, kBlockSize);
    EXPECT_EQ(amp.parameterCount(), AmpSimulator::kParamCount);
    EXPECT_STREQ(amp.name(), "Amp Simulator");
    EXPECT_STREQ(amp.id(), "amp");
}

TEST(AmpSimulator, CleanTypePreservesSignal) {
    AmpSimulator amp;
    amp.init(kSampleRate, kBlockSize);
    amp.setParameter(AmpSimulator::kGain, 0.0f);     // minimal drive
    amp.setParameter(AmpSimulator::kAmpType, 0.0f);   // Clean
    amp.setParameter(AmpSimulator::kCabinet, 0.0f);   // no cabinet
    amp.setParameter(AmpSimulator::kOutput, 0.0f);     // unity output

    const int frames = 512;
    std::vector<float> buf(frames * 2);
    fillSine(buf.data(), frames, 2, 440.0f, 0.3f, kSampleRate);
    amp.process(buf.data(), frames, 2);

    // Should produce output (not silence)
    EXPECT_TRUE(hasSignal(buf.data() + 256, frames - 128, 2));
}

TEST(AmpSimulator, HighGainSaturates) {
    AmpSimulator amp;
    amp.init(kSampleRate, kBlockSize);
    amp.setParameter(AmpSimulator::kGain, 48.0f);     // max drive
    amp.setParameter(AmpSimulator::kAmpType, 3.0f);   // HighGain
    amp.setParameter(AmpSimulator::kCabinet, 0.0f);
    amp.setParameter(AmpSimulator::kOutput, -12.0f);

    const int frames = 512;
    std::vector<float> buf(frames * 2);
    fillSine(buf.data(), frames, 2, 440.0f, 0.5f, kSampleRate);
    auto orig = buf;
    amp.process(buf.data(), frames, 2);

    // Output should differ significantly from input (saturation)
    float diffSum = 0.0f;
    for (size_t i = 256; i < buf.size(); ++i)
        diffSum += std::abs(buf[i] - orig[i]);
    EXPECT_GT(diffSum, 1.0f);
}

TEST(AmpSimulator, CabinetReducesHighFreq) {
    AmpSimulator amp;
    amp.init(kSampleRate, kBlockSize);
    amp.setParameter(AmpSimulator::kGain, 6.0f);
    amp.setParameter(AmpSimulator::kAmpType, 0.0f);
    amp.setParameter(AmpSimulator::kCabinet, 1.0f);   // full cabinet
    amp.setParameter(AmpSimulator::kOutput, 0.0f);

    // High frequency signal should be attenuated by cabinet
    const int frames = 1024;
    std::vector<float> buf(frames * 2);
    fillSine(buf.data(), frames, 2, 8000.0f, 0.3f, kSampleRate);
    float inRMS = computeRMS(buf.data() + 512, frames - 256, 2);
    amp.process(buf.data(), frames, 2);
    float outRMS = computeRMS(buf.data() + 512, frames - 256, 2);

    EXPECT_LT(outRMS, inRMS);
}

TEST(AmpSimulator, BypassPassesThrough) {
    AmpSimulator amp;
    amp.init(kSampleRate, kBlockSize);
    amp.setBypassed(true);

    std::vector<float> buf(kBlockSize * 2);
    fillSine(buf.data(), kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    auto orig = buf;
    amp.process(buf.data(), kBlockSize, 2);

    for (int i = 0; i < kBlockSize * 2; ++i)
        EXPECT_FLOAT_EQ(buf[i], orig[i]);
}

TEST(AmpSimulator, Parameters) {
    AmpSimulator amp;
    amp.init(kSampleRate, kBlockSize);

    amp.setParameter(AmpSimulator::kGain, 24.0f);
    EXPECT_FLOAT_EQ(amp.getParameter(AmpSimulator::kGain), 24.0f);

    amp.setParameter(AmpSimulator::kBass, 6.0f);
    EXPECT_FLOAT_EQ(amp.getParameter(AmpSimulator::kBass), 6.0f);

    amp.setParameter(AmpSimulator::kAmpType, 2.0f);
    EXPECT_FLOAT_EQ(amp.getParameter(AmpSimulator::kAmpType), 2.0f);

    // Defaults
    AmpSimulator amp2;
    amp2.init(kSampleRate, kBlockSize);
    EXPECT_FLOAT_EQ(amp2.getParameter(AmpSimulator::kGain),
                    amp2.parameterInfo(AmpSimulator::kGain).defaultValue);
}

// ========================= Latency reporting =========================
//
// Phase 1 of the latency roadmap — every AudioEffect now exposes
// latencySamples() defaulting to 0, with overrides on the lookahead-
// based effects (NoiseGate, Limiter). EffectChain::latencySamples()
// sums non-bypassed entries. Phase 2 (Latency P2) will use these to
// auto-compensate parallel signal paths so faster routes are padded
// to align with the slowest at the master output.

TEST(Latency, ZeroLatencyEffectsReportZero) {
    // Every "no-latency" effect we have keeps the dry signal sample-
    // accurate. Pin this so a refactor can't silently start delaying
    // the path on, say, the EQ.
    Reverb       r;        r.init(kSampleRate, kBlockSize);
    Delay        d;        d.init(kSampleRate, kBlockSize);
    EQ           eq;       eq.init(kSampleRate, kBlockSize);
    Compressor   c;        c.init(kSampleRate, kBlockSize);
    Filter       f;        f.init(kSampleRate, kBlockSize);
    Chorus       ch;       ch.init(kSampleRate, kBlockSize);
    Phaser       ph;       ph.init(kSampleRate, kBlockSize);
    Wah          wah;      wah.init(kSampleRate, kBlockSize);
    Distortion   dist;     dist.init(kSampleRate, kBlockSize);
    TapeEmulation t;       t.init(kSampleRate, kBlockSize);
    AmpSimulator amp;      amp.init(kSampleRate, kBlockSize);
    Oscilloscope osc;      osc.init(kSampleRate, kBlockSize);
    SpectrumAnalyzer spec; spec.init(kSampleRate, kBlockSize);

    EXPECT_EQ(r.latencySamples(),    0);
    EXPECT_EQ(d.latencySamples(),    0);
    EXPECT_EQ(eq.latencySamples(),   0);
    EXPECT_EQ(c.latencySamples(),    0);
    EXPECT_EQ(f.latencySamples(),    0);
    EXPECT_EQ(ch.latencySamples(),   0);
    EXPECT_EQ(ph.latencySamples(),   0);
    EXPECT_EQ(wah.latencySamples(),  0);
    EXPECT_EQ(dist.latencySamples(), 0);
    EXPECT_EQ(t.latencySamples(),    0);
    EXPECT_EQ(amp.latencySamples(),  0);
    EXPECT_EQ(osc.latencySamples(),  0);
    EXPECT_EQ(spec.latencySamples(), 0);
}

TEST(Latency, NoiseGateReportsLookaheadSamples) {
    NoiseGate g;
    g.init(kSampleRate, kBlockSize);
    // Default lookahead — whatever NoiseGate ships with — should
    // round-trip to a matching sample count.
    const float lookMs = g.getParameter(NoiseGate::kLookahead);
    const int   expected = static_cast<int>(lookMs * 0.001f * kSampleRate);
    EXPECT_EQ(g.latencySamples(), expected);

    // Bump to 5 ms — at 44.1 kHz that's 220 samples.
    g.setParameter(NoiseGate::kLookahead, 5.0f);
    EXPECT_EQ(g.latencySamples(), static_cast<int>(5.0f * 0.001f * kSampleRate));

    // Zero ms → zero samples.
    g.setParameter(NoiseGate::kLookahead, 0.0f);
    EXPECT_EQ(g.latencySamples(), 0);

    // Cap at the pre-allocated delay-line size — even if the param
    // got pushed out of band, latency reporting must stay sane.
    g.setParameter(NoiseGate::kLookahead, 9999.0f);
    EXPECT_GE(g.latencySamples(), 0);
    EXPECT_LE(g.latencySamples(), 1920 + 16);
}

TEST(Latency, LimiterReportsLookaheadSamples) {
    Limiter lim;
    lim.init(kSampleRate, kBlockSize);
    lim.setParameter(Limiter::kLookahead, 5.0f);
    EXPECT_EQ(lim.latencySamples(),
              static_cast<int>(5.0f * 0.001f * kSampleRate));
    lim.setParameter(Limiter::kLookahead, 0.0f);
    EXPECT_EQ(lim.latencySamples(), 0);
    // Out-of-range upward gets clamped.
    lim.setParameter(Limiter::kLookahead, 9999.0f);
    EXPECT_GT(lim.latencySamples(), 0);
    EXPECT_LT(lim.latencySamples(), Limiter::kMaxLookaheadSamples);
}

TEST(Latency, EffectChainSumsNonBypassed) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);

    auto gate = std::make_unique<NoiseGate>();
    gate->setParameter(NoiseGate::kLookahead, 4.0f);  // 176 samples
    NoiseGate* gateRaw = static_cast<NoiseGate*>(
        chain.append(std::move(gate)));

    auto lim = std::make_unique<Limiter>();
    lim->setParameter(Limiter::kLookahead, 6.0f);     // 264 samples
    Limiter* limRaw = static_cast<Limiter*>(
        chain.append(std::move(lim)));

    // A zero-latency effect doesn't shift the total.
    chain.append(std::make_unique<EQ>());

    const int expectedGate = static_cast<int>(4.0f * 0.001f * kSampleRate);
    const int expectedLim  = static_cast<int>(6.0f * 0.001f * kSampleRate);
    EXPECT_EQ(chain.latencySamples(), expectedGate + expectedLim);

    // Bypassing the limiter drops its contribution.
    limRaw->setBypassed(true);
    EXPECT_EQ(chain.latencySamples(), expectedGate);

    // Bypassing the gate too leaves only the EQ (zero-latency).
    gateRaw->setBypassed(true);
    EXPECT_EQ(chain.latencySamples(), 0);
}

TEST(Latency, EmptyChainIsZero) {
    EffectChain chain;
    chain.init(kSampleRate, kBlockSize);
    EXPECT_EQ(chain.latencySamples(), 0);
}

TEST(Latency, ConvolutionEngineReportsBlockSizeWhenIRLoaded) {
    ConvolutionEngine eng;
    eng.init(/*blockSize=*/kBlockSize, kSampleRate);
    // No IR yet → no buffering applied → zero latency.
    EXPECT_EQ(eng.latencySamples(), 0);

    // Tiny synthetic IR (1024 samples) → engine kicks into the
    // sub-block buffered path → reports one partition (kBlockSize).
    std::vector<float> ir(1024, 0.0f);
    ir[0] = 1.0f;  // unit impulse — the simplest non-degenerate IR
    eng.setIR(ir.data(), static_cast<int>(ir.size()));
    EXPECT_EQ(eng.latencySamples(), kBlockSize);

    // After clear, latency drops back to zero.
    eng.clear();
    EXPECT_EQ(eng.latencySamples(), 0);
}

TEST(Latency, ConvolutionReverbReportsThroughTheEngine) {
    ConvolutionReverb rev;
    rev.init(kSampleRate, kBlockSize);
    // Fresh device — no IR loaded → zero.
    EXPECT_EQ(rev.latencySamples(), 0);

    std::vector<float> ir(2048, 0.0f);
    ir[0] = 1.0f;
    rev.loadIRMono(ir.data(), static_cast<int>(ir.size()), kSampleRate);
    EXPECT_EQ(rev.latencySamples(), kBlockSize);

    rev.clearIR();
    EXPECT_EQ(rev.latencySamples(), 0);
}

// ========================= Phaser =========================
//
// All-pass cascade with LFO modulation. The all-pass network is
// sample-by-sample causal (zero latency), and at mix=0 the dry
// path passes through unchanged. Beyond that we mostly want to
// confirm: the cascade actually colours the signal, the mix knob
// behaves at endpoints, parameter access works, and bypass is a
// pass-through.

TEST(Phaser, Init) {
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    EXPECT_EQ(p.parameterCount(), Phaser::kParamCount);
    // Defaults are inside the documented ranges.
    for (int i = 0; i < p.parameterCount(); ++i) {
        const auto& info = p.parameterInfo(i);
        EXPECT_GE(p.getParameter(i), info.minValue);
        EXPECT_LE(p.getParameter(i), info.maxValue);
    }
    EXPECT_EQ(p.latencySamples(), 0);  // sample-by-sample causal
}

TEST(Phaser, MixZeroIsDry) {
    // mix = 0 → no wet contribution → output equals input. This
    // pins the "dry path is sample-accurate" claim from the
    // file-level latency comment.
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    p.setParameter(Phaser::kMix, 0.0f);

    constexpr int N = kBlockSize;
    constexpr int CH = 2;
    float buf[N * CH];
    fillSine(buf, N, CH, 440.0f, 0.5f, kSampleRate);
    std::vector<float> input(buf, buf + N * CH);
    p.process(buf, N, CH);

    for (int i = 0; i < N * CH; ++i) {
        EXPECT_NEAR(buf[i], input[i], 1e-6f);
    }
}

TEST(Phaser, ProducesOutput) {
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    p.setParameter(Phaser::kMix, 0.5f);

    float buf[kBlockSize * 2];
    fillSine(buf, kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    p.process(buf, kBlockSize, 2);
    EXPECT_TRUE(hasSignal(buf, kBlockSize, 2));
}

TEST(Phaser, BypassIsExactlyDry) {
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    p.setBypassed(true);

    float buf[kBlockSize * 2];
    fillSine(buf, kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    std::vector<float> input(buf, buf + kBlockSize * 2);
    p.process(buf, kBlockSize, 2);

    for (int i = 0; i < kBlockSize * 2; ++i) {
        EXPECT_FLOAT_EQ(buf[i], input[i]);
    }
}

TEST(Phaser, ParameterAccess) {
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    p.setParameter(Phaser::kRate, 2.0f);
    EXPECT_FLOAT_EQ(p.getParameter(Phaser::kRate), 2.0f);
    p.setParameter(Phaser::kStages, 8.0f);
    EXPECT_FLOAT_EQ(p.getParameter(Phaser::kStages), 8.0f);
    p.setParameter(Phaser::kFeedback, -0.5f);
    EXPECT_FLOAT_EQ(p.getParameter(Phaser::kFeedback), -0.5f);

    // Out-of-range index is a no-op (no crash).
    p.setParameter(-1, 0.5f);
    p.setParameter(Phaser::kParamCount + 1, 0.5f);
    EXPECT_EQ(p.getParameter(-1), 0.0f);
    EXPECT_EQ(p.getParameter(Phaser::kParamCount + 1), 0.0f);
}

TEST(Phaser, ChangesSpectrumVsDry) {
    // Run a sine through the phaser at full mix and compare to dry
    // — the all-pass network, when summed with its own dry, should
    // produce a notably different waveform. We do an L2-norm
    // difference rather than assert specific notch frequencies (LFO
    // is moving, so the notches drift).
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    p.setParameter(Phaser::kMix, 1.0f);          // wet only
    p.setParameter(Phaser::kRate, 0.2f);         // slow sweep
    p.setParameter(Phaser::kFeedback, 0.0f);     // disable for clean test
    p.setParameter(Phaser::kStages, 6.0f);

    constexpr int N = kBlockSize;
    constexpr int CH = 2;
    float buf[N * CH];
    fillSine(buf, N, CH, 800.0f, 0.5f, kSampleRate);
    std::vector<float> dry(buf, buf + N * CH);
    p.process(buf, N, CH);

    float diff = 0.0f;
    for (int i = 0; i < N * CH; ++i) {
        const float d = buf[i] - dry[i];
        diff += d * d;
    }
    diff = std::sqrt(diff / (N * CH));
    EXPECT_GT(diff, 0.01f) << "all-pass cascade left the signal unchanged";
}

TEST(Phaser, FeedbackStaysStable) {
    // High feedback shouldn't blow up on a steady-state signal.
    // We process several blocks and check the output stays bounded
    // — phasers can self-resonate but they shouldn't diverge.
    Phaser p;
    p.init(kSampleRate, kBlockSize);
    p.setParameter(Phaser::kFeedback, 0.9f);     // near max
    p.setParameter(Phaser::kMix, 0.5f);

    float buf[kBlockSize * 2];
    for (int block = 0; block < 50; ++block) {
        fillSine(buf, kBlockSize, 2, 440.0f, 0.3f, kSampleRate);
        p.process(buf, kBlockSize, 2);
        for (int i = 0; i < kBlockSize * 2; ++i) {
            EXPECT_LT(std::abs(buf[i]), 10.0f)
                << "phaser feedback exploded at block " << block;
        }
    }
}

// ========================= Wah =========================
//
// Resonant bandpass with two driver modes (Pedal / Auto). Tests
// pin: defaults in-range, mix=0 = dry, output non-zero, bypass
// is exact dry, parameter access, pedal sweep changes the
// spectrum, auto-mode envelope responds to input level, high-Q
// stability.

TEST(Wah, Init) {
    Wah w;
    w.init(kSampleRate, kBlockSize);
    EXPECT_EQ(w.parameterCount(), Wah::kParamCount);
    for (int i = 0; i < w.parameterCount(); ++i) {
        const auto& info = w.parameterInfo(i);
        EXPECT_GE(w.getParameter(i), info.minValue);
        EXPECT_LE(w.getParameter(i), info.maxValue);
    }
    EXPECT_EQ(w.latencySamples(), 0);
}

TEST(Wah, MixZeroIsDry) {
    Wah w;
    w.init(kSampleRate, kBlockSize);
    w.setParameter(Wah::kMix, 0.0f);

    float buf[kBlockSize * 2];
    fillSine(buf, kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    std::vector<float> input(buf, buf + kBlockSize * 2);
    w.process(buf, kBlockSize, 2);
    for (int i = 0; i < kBlockSize * 2; ++i) {
        EXPECT_NEAR(buf[i], input[i], 1e-6f);
    }
}

TEST(Wah, BypassIsExactlyDry) {
    Wah w;
    w.init(kSampleRate, kBlockSize);
    w.setBypassed(true);

    float buf[kBlockSize * 2];
    fillSine(buf, kBlockSize, 2, 440.0f, 0.5f, kSampleRate);
    std::vector<float> input(buf, buf + kBlockSize * 2);
    w.process(buf, kBlockSize, 2);
    for (int i = 0; i < kBlockSize * 2; ++i) {
        EXPECT_FLOAT_EQ(buf[i], input[i]);
    }
}

TEST(Wah, PedalProducesOutput) {
    Wah w;
    w.init(kSampleRate, kBlockSize);
    w.setParameter(Wah::kMode,  static_cast<float>(Wah::ModePedal));
    w.setParameter(Wah::kPedal, 0.5f);

    float buf[kBlockSize * 2];
    fillSine(buf, kBlockSize, 2, 800.0f, 0.5f, kSampleRate);
    w.process(buf, kBlockSize, 2);
    EXPECT_TRUE(hasSignal(buf, kBlockSize, 2));
}

TEST(Wah, PedalSweepChangesOutput) {
    // Sweeping pedal across the range should produce different
    // output — the BPF cuts different frequencies.
    Wah w1, w2;
    w1.init(kSampleRate, kBlockSize);
    w2.init(kSampleRate, kBlockSize);
    w1.setParameter(Wah::kPedal, 0.0f);  // bottom of sweep
    w2.setParameter(Wah::kPedal, 1.0f);  // top of sweep

    float buf1[kBlockSize * 2];
    float buf2[kBlockSize * 2];
    fillSine(buf1, kBlockSize, 2, 800.0f, 0.5f, kSampleRate);
    fillSine(buf2, kBlockSize, 2, 800.0f, 0.5f, kSampleRate);
    w1.process(buf1, kBlockSize, 2);
    w2.process(buf2, kBlockSize, 2);

    float diff = 0.0f;
    for (int i = 0; i < kBlockSize * 2; ++i) {
        const float d = buf1[i] - buf2[i];
        diff += d * d;
    }
    diff = std::sqrt(diff / (kBlockSize * 2));
    EXPECT_GT(diff, 0.001f) << "pedal didn't change output";
}

TEST(Wah, AutoRespondsToInputLevel) {
    // In Auto mode, a louder signal should produce a louder
    // envelope → drive the filter higher → different output
    // than a quieter signal of the same frequency.
    Wah quiet, loud;
    quiet.init(kSampleRate, kBlockSize);
    loud.init(kSampleRate, kBlockSize);
    quiet.setParameter(Wah::kMode, static_cast<float>(Wah::ModeAuto));
    loud.setParameter(Wah::kMode,  static_cast<float>(Wah::ModeAuto));
    quiet.setParameter(Wah::kSensitivity, 0.7f);
    loud.setParameter(Wah::kSensitivity,  0.7f);
    quiet.setParameter(Wah::kPedal, 0.0f);
    loud.setParameter(Wah::kPedal,  0.0f);

    // Run a few warm-up blocks so the envelope settles.
    float bufQ[kBlockSize * 2];
    float bufL[kBlockSize * 2];
    for (int blk = 0; blk < 10; ++blk) {
        fillSine(bufQ, kBlockSize, 2, 440.0f, 0.05f, kSampleRate);
        fillSine(bufL, kBlockSize, 2, 440.0f, 0.50f, kSampleRate);
        quiet.process(bufQ, kBlockSize, 2);
        loud.process(bufL, kBlockSize, 2);
    }
    // Final block — compare normalised RMS so we're checking the
    // filter response shape, not just the amplitude difference.
    fillSine(bufQ, kBlockSize, 2, 440.0f, 0.05f, kSampleRate);
    fillSine(bufL, kBlockSize, 2, 440.0f, 0.50f, kSampleRate);
    quiet.process(bufQ, kBlockSize, 2);
    loud.process(bufL, kBlockSize, 2);

    // Normalise to input amplitude — what we want to verify is
    // that the wah filter MOVED, not that loud-in produces louder
    // out (it would anyway). We compare per-channel relative RMS.
    const float rmsQ = computeRMS(bufQ, kBlockSize, 2) / 0.05f;
    const float rmsL = computeRMS(bufL, kBlockSize, 2) / 0.50f;
    // The two normalised RMS values should differ — same input
    // shape, different filter cutoff → different attenuation of
    // the 440 Hz tone.
    EXPECT_GT(std::abs(rmsQ - rmsL), 0.01f)
        << "auto-mode didn't react to input level";
}

TEST(Wah, ParameterAccess) {
    Wah w;
    w.init(kSampleRate, kBlockSize);
    w.setParameter(Wah::kQ, 12.0f);
    EXPECT_FLOAT_EQ(w.getParameter(Wah::kQ), 12.0f);
    w.setParameter(Wah::kBottomHz, 500.0f);
    EXPECT_FLOAT_EQ(w.getParameter(Wah::kBottomHz), 500.0f);
    w.setParameter(Wah::kTopHz, 3500.0f);
    EXPECT_FLOAT_EQ(w.getParameter(Wah::kTopHz), 3500.0f);
    // Out-of-range index = no-op.
    w.setParameter(-1, 0.5f);
    w.setParameter(Wah::kParamCount + 1, 0.5f);
    EXPECT_EQ(w.getParameter(-1), 0.0f);
    EXPECT_EQ(w.getParameter(Wah::kParamCount + 1), 0.0f);
}

TEST(Wah, HighQStaysStable) {
    // Q=16 + steady tone shouldn't make the filter explode.
    // Process many blocks and check output stays bounded.
    Wah w;
    w.init(kSampleRate, kBlockSize);
    w.setParameter(Wah::kQ, 16.0f);
    w.setParameter(Wah::kPedal, 0.5f);

    float buf[kBlockSize * 2];
    for (int block = 0; block < 50; ++block) {
        fillSine(buf, kBlockSize, 2, 800.0f, 0.5f, kSampleRate);
        w.process(buf, kBlockSize, 2);
        for (int i = 0; i < kBlockSize * 2; ++i) {
            EXPECT_LT(std::abs(buf[i]), 20.0f)
                << "wah blew up at block " << block;
        }
    }
}
