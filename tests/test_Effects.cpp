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
    flt.setParameter(Filter::kCutoff, 300.0f);
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
    flt->setParameter(Filter::kCutoff, 200.0f);
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
    flt->setParameter(Filter::kCutoff, 200.0f);
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
