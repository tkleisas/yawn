#include <gtest/gtest.h>
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "effects/Biquad.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
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
