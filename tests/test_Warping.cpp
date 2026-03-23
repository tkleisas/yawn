#include <gtest/gtest.h>
#include "audio/TransientDetector.h"
#include "audio/TimeStretcher.h"
#include "audio/WarpMarker.h"
#include "audio/Clip.h"
#include "audio/ClipEngine.h"
#include "audio/Transport.h"
#include "audio/AudioBuffer.h"
#include <cmath>
#include <numeric>
#include <memory>

using namespace yawn;
using namespace yawn::audio;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==================== WarpMarker Tests ====================

TEST(WarpMarker, DefaultConstruction) {
    WarpMarker m{0, 0.0};
    EXPECT_EQ(m.samplePosition, 0);
    EXPECT_DOUBLE_EQ(m.beatPosition, 0.0);
}

TEST(WarpMarker, ConstructWithValues) {
    WarpMarker m{44100, 1.0};
    EXPECT_EQ(m.samplePosition, 44100);
    EXPECT_DOUBLE_EQ(m.beatPosition, 1.0);
}

TEST(WarpMode, EnumValues) {
    EXPECT_NE(WarpMode::Off, WarpMode::Auto);
    EXPECT_NE(WarpMode::Beats, WarpMode::Tones);
    EXPECT_NE(WarpMode::Texture, WarpMode::Repitch);
}

// ==================== Clip Warp Tests ====================

TEST(ClipWarp, SpeedRatioWarpOff) {
    Clip clip;
    clip.warpMode = WarpMode::Off;
    clip.originalBPM = 120.0;
    EXPECT_DOUBLE_EQ(clip.warpSpeedRatio(140.0), 1.0);
}

TEST(ClipWarp, SpeedRatioNoBPM) {
    Clip clip;
    clip.warpMode = WarpMode::Beats;
    clip.originalBPM = 0.0;
    EXPECT_DOUBLE_EQ(clip.warpSpeedRatio(140.0), 1.0);
}

TEST(ClipWarp, SpeedRatioSameBPM) {
    Clip clip;
    clip.warpMode = WarpMode::Beats;
    clip.originalBPM = 120.0;
    EXPECT_DOUBLE_EQ(clip.warpSpeedRatio(120.0), 1.0);
}

TEST(ClipWarp, SpeedRatioSlower) {
    // Original 120 BPM played at 60 BPM → ratio 2.0 (advance faster through source)
    Clip clip;
    clip.warpMode = WarpMode::Auto;
    clip.originalBPM = 120.0;
    EXPECT_DOUBLE_EQ(clip.warpSpeedRatio(60.0), 2.0);
}

TEST(ClipWarp, SpeedRatioFaster) {
    // Original 120 BPM played at 240 BPM → ratio 0.5 (advance slower through source)
    Clip clip;
    clip.warpMode = WarpMode::Repitch;
    clip.originalBPM = 120.0;
    EXPECT_DOUBLE_EQ(clip.warpSpeedRatio(240.0), 0.5);
}

TEST(ClipWarp, ClonePreservesWarpData) {
    Clip clip;
    clip.warpMode = WarpMode::Tones;
    clip.originalBPM = 130.0;
    clip.warpMarkers = {{0, 0.0}, {44100, 1.0}, {88200, 2.0}};
    clip.transients = {0, 22050, 44100, 66150};
    
    auto cloned = clip.clone();
    EXPECT_EQ(cloned->warpMode, WarpMode::Tones);
    EXPECT_DOUBLE_EQ(cloned->originalBPM, 130.0);
    EXPECT_EQ(cloned->warpMarkers.size(), 3u);
    EXPECT_EQ(cloned->warpMarkers[1].samplePosition, 44100);
    EXPECT_DOUBLE_EQ(cloned->warpMarkers[1].beatPosition, 1.0);
    EXPECT_EQ(cloned->transients.size(), 4u);
}

// Helper: create a Clip with a shared_ptr buffer
static std::unique_ptr<Clip> makeWarpClip(int channels, int frames) {
    auto clip = std::make_unique<Clip>();
    clip->buffer = std::make_shared<AudioBuffer>(channels, frames);
    clip->loopStart = 0;
    clip->loopEnd = frames;
    clip->looping = false;
    clip->gain = 1.0f;
    return clip;
}

// ==================== TransientDetector Tests ====================

static AudioBuffer makeClickTrack(int sampleRate, int numClicks, double intervalSec) {
    int totalFrames = static_cast<int>(numClicks * intervalSec * sampleRate + sampleRate);
    AudioBuffer buf(1, totalFrames);
    float* data = buf.channelData(0);
    std::fill(data, data + totalFrames, 0.0f);
    
    for (int c = 0; c < numClicks; ++c) {
        int pos = static_cast<int>(c * intervalSec * sampleRate);
        // Short impulse (5 samples)
        for (int i = 0; i < 5 && pos + i < totalFrames; ++i) {
            data[pos + i] = 1.0f;
        }
    }
    return buf;
}

static AudioBuffer makeSineWave(int sampleRate, float freq, float durationSec) {
    int numFrames = static_cast<int>(sampleRate * durationSec);
    AudioBuffer buf(1, numFrames);
    float* data = buf.channelData(0);
    for (int i = 0; i < numFrames; ++i) {
        data[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sampleRate);
    }
    return buf;
}

TEST(TransientDetector, DetectsClicks) {
    // Create 4 clicks at 120 BPM (0.5s apart)
    auto buf = makeClickTrack(44100, 4, 0.5);
    
    TransientDetector::Config cfg;
    cfg.sampleRate = 44100.0;
    cfg.sensitivity = 0.7f;
    cfg.minInterOnsetSec = 0.1;
    
    auto transients = TransientDetector::detect(buf, cfg);
    
    // Should detect close to 4 transients (first may be at time 0)
    EXPECT_GE(transients.size(), 2u);
    EXPECT_LE(transients.size(), 6u);
}

TEST(TransientDetector, EmptyBuffer) {
    AudioBuffer buf(1, 0);
    auto transients = TransientDetector::detect(buf);
    EXPECT_TRUE(transients.empty());
}

TEST(TransientDetector, SilentBuffer) {
    AudioBuffer buf(1, 44100);
    float* data = buf.channelData(0);
    std::fill(data, data + 44100, 0.0f);
    
    auto transients = TransientDetector::detect(buf);
    EXPECT_TRUE(transients.empty());
}

TEST(TransientDetector, SmallBuffer) {
    AudioBuffer buf(1, 100); // smaller than window size
    auto transients = TransientDetector::detect(buf);
    EXPECT_TRUE(transients.empty());
}

TEST(TransientDetector, TransientPositionsAreOrdered) {
    auto buf = makeClickTrack(44100, 8, 0.25);
    
    TransientDetector::Config cfg;
    cfg.sampleRate = 44100.0;
    cfg.sensitivity = 0.8f;
    
    auto transients = TransientDetector::detect(buf, cfg);
    
    for (size_t i = 1; i < transients.size(); ++i) {
        EXPECT_GT(transients[i], transients[i - 1]);
    }
}

TEST(TransientDetector, MinInterOnsetEnforced) {
    auto buf = makeClickTrack(44100, 10, 0.05); // clicks every 50ms
    
    TransientDetector::Config cfg;
    cfg.sampleRate = 44100.0;
    cfg.sensitivity = 0.9f;
    cfg.minInterOnsetSec = 0.2; // require 200ms gap
    
    auto transients = TransientDetector::detect(buf, cfg);
    
    for (size_t i = 1; i < transients.size(); ++i) {
        double gapSec = static_cast<double>(transients[i] - transients[i - 1]) / 44100.0;
        EXPECT_GE(gapSec, 0.19); // allow small tolerance
    }
}

TEST(TransientDetector, EstimateBPMTooFewTransients) {
    std::vector<int64_t> transients = {0, 22050};
    double bpm = TransientDetector::estimateBPM(transients, 44100.0);
    EXPECT_DOUBLE_EQ(bpm, 0.0);
}

TEST(TransientDetector, EstimateBPMRegular120) {
    // Transients at 120 BPM = every 0.5s = every 22050 samples at 44100Hz
    std::vector<int64_t> transients;
    for (int i = 0; i < 16; ++i) {
        transients.push_back(i * 22050);
    }
    
    double bpm = TransientDetector::estimateBPM(transients, 44100.0, 60.0, 200.0);
    EXPECT_NEAR(bpm, 120.0, 2.0);
}

TEST(TransientDetector, EstimateBPMRegular140) {
    // 140 BPM = 60/140 = 0.4286s per beat = 18900 samples
    std::vector<int64_t> transients;
    int samplesPerBeat = static_cast<int>(44100.0 * 60.0 / 140.0);
    for (int i = 0; i < 16; ++i) {
        transients.push_back(i * samplesPerBeat);
    }
    
    double bpm = TransientDetector::estimateBPM(transients, 44100.0, 60.0, 200.0);
    EXPECT_NEAR(bpm, 140.0, 2.0);
}

// ==================== TimeStretcher Tests ====================

TEST(TimeStretcher, IdentityWSOLA) {
    TimeStretcher ts;
    ts.init(44100.0, 4096, TimeStretcher::Algorithm::WSOLA);
    ts.setSpeedRatio(1.0);
    
    // Create a simple sine wave input
    const int inputSize = 44100; // 1 second
    std::vector<float> input(inputSize);
    for (int i = 0; i < inputSize; ++i) {
        input[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }
    
    std::vector<float> output(inputSize * 2, 0.0f);
    int consumed = 0;
    int written = ts.process(input.data(), inputSize, output.data(), inputSize * 2, consumed);
    
    EXPECT_GT(written, 0);
    EXPECT_GT(consumed, 0);
}

TEST(TimeStretcher, StretchWSOLA) {
    TimeStretcher ts;
    ts.init(44100.0, 4096, TimeStretcher::Algorithm::WSOLA);
    ts.setSpeedRatio(0.5); // half speed = double length output
    
    const int inputSize = 44100;
    std::vector<float> input(inputSize);
    for (int i = 0; i < inputSize; ++i) {
        input[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }
    
    std::vector<float> output(inputSize * 4, 0.0f);
    int consumed = 0;
    int written = ts.process(input.data(), inputSize, output.data(), inputSize * 4, consumed);
    
    // At 0.5 speed, we consume fewer input samples per output sample
    EXPECT_GT(written, 0);
}

TEST(TimeStretcher, CompressWSOLA) {
    TimeStretcher ts;
    ts.init(44100.0, 4096, TimeStretcher::Algorithm::WSOLA);
    ts.setSpeedRatio(2.0); // double speed = half length output
    
    const int inputSize = 44100;
    std::vector<float> input(inputSize);
    for (int i = 0; i < inputSize; ++i) {
        input[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }
    
    std::vector<float> output(inputSize, 0.0f);
    int consumed = 0;
    int written = ts.process(input.data(), inputSize, output.data(), inputSize, consumed);
    
    EXPECT_GT(written, 0);
}

TEST(TimeStretcher, IdentityPhaseVocoder) {
    TimeStretcher ts;
    ts.init(44100.0, 4096, TimeStretcher::Algorithm::PhaseVocoder);
    ts.setSpeedRatio(1.0);
    
    const int inputSize = 44100;
    std::vector<float> input(inputSize);
    for (int i = 0; i < inputSize; ++i) {
        input[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }
    
    std::vector<float> output(inputSize * 2, 0.0f);
    int consumed = 0;
    int written = ts.process(input.data(), inputSize, output.data(), inputSize * 2, consumed);
    
    EXPECT_GT(written, 0);
}

TEST(TimeStretcher, SpeedRatioClamped) {
    TimeStretcher ts;
    ts.init(44100.0, 4096);
    
    ts.setSpeedRatio(0.1); // below minimum 0.25
    EXPECT_DOUBLE_EQ(ts.speedRatio(), 0.25);
    
    ts.setSpeedRatio(10.0); // above maximum 4.0
    EXPECT_DOUBLE_EQ(ts.speedRatio(), 4.0);
    
    ts.setSpeedRatio(1.5);
    EXPECT_DOUBLE_EQ(ts.speedRatio(), 1.5);
}

TEST(TimeStretcher, ResetClearsState) {
    TimeStretcher ts;
    ts.init(44100.0, 4096, TimeStretcher::Algorithm::WSOLA);
    ts.setSpeedRatio(1.0);
    
    // Process some data
    std::vector<float> input(4096, 0.5f);
    std::vector<float> output(8192, 0.0f);
    int consumed = 0;
    ts.process(input.data(), 4096, output.data(), 8192, consumed);
    
    // Reset and process again — should work without issues
    ts.reset();
    consumed = 0;
    std::fill(output.begin(), output.end(), 0.0f);
    int written = ts.process(input.data(), 4096, output.data(), 8192, consumed);
    EXPECT_GE(written, 0);
}

TEST(TimeStretcher, WSOLAOutputNotSilent) {
    TimeStretcher ts;
    ts.init(44100.0, 4096, TimeStretcher::Algorithm::WSOLA);
    ts.setSpeedRatio(1.0);
    
    const int inputSize = 44100;
    std::vector<float> input(inputSize);
    for (int i = 0; i < inputSize; ++i) {
        input[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }
    
    std::vector<float> output(inputSize * 2, 0.0f);
    int consumed = 0;
    int written = ts.process(input.data(), inputSize, output.data(), inputSize * 2, consumed);
    
    // Check output is not all silence
    float maxAbs = 0.0f;
    for (int i = 0; i < written; ++i) {
        maxAbs = std::max(maxAbs, std::abs(output[i]));
    }
    EXPECT_GT(maxAbs, 0.01f);
}

TEST(TimeStretcher, FFTRoundTrip) {
    // Test that FFT → IFFT recovers original signal
    const int N = 1024;
    std::vector<float> real(N), imag(N);
    std::vector<float> origReal(N);
    
    for (int i = 0; i < N; ++i) {
        real[i] = std::sin(2.0 * M_PI * 3.0 * i / N);
        origReal[i] = real[i];
        imag[i] = 0.0f;
    }
    
    // Forward FFT
    TimeStretcher::fft(real.data(), imag.data(), N, false);
    // Inverse FFT
    TimeStretcher::fft(real.data(), imag.data(), N, true);
    
    // Should recover original
    for (int i = 0; i < N; ++i) {
        EXPECT_NEAR(real[i], origReal[i], 1e-4f) << "at index " << i;
        EXPECT_NEAR(imag[i], 0.0f, 1e-4f) << "at index " << i;
    }
}

// ==================== Warp-Aware Playback Tests ====================

class WarpPlaybackTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_transport.setSampleRate(44100.0);
        m_transport.setBPM(120.0);
        m_engine.setTransport(&m_transport);
        m_engine.setSampleRate(44100.0);
        m_engine.setQuantizeMode(QuantizeMode::None);
    }

    Transport m_transport;
    ClipEngine m_engine;
};

TEST_F(WarpPlaybackTest, NoWarpPlaysNormally) {
    auto clip = makeWarpClip(1, 100);
    float* data = clip->buffer->channelData(0);
    for (int i = 0; i < 100; ++i) data[i] = static_cast<float>(i);
    clip->warpMode = WarpMode::Off;
    
    m_engine.scheduleClip(0, 0, clip.get());
    
    std::vector<float> output(20, 0.0f);
    m_engine.processTrackToBuffer(0, output.data(), 10, 1);
    
    // Should read samples with fade-in applied (values increase)
    // First sample has fadeGain starting at ~kFadeIncrement
    for (int i = 1; i < 10; ++i) {
        EXPECT_GE(output[i], output[i - 1] - 0.01f);
    }
}

TEST_F(WarpPlaybackTest, RepitchDoubleSpeed) {
    auto clip = makeWarpClip(1, 2000);
    float* data = clip->buffer->channelData(0);
    for (int i = 0; i < 2000; ++i) data[i] = static_cast<float>(i);
    clip->loopEnd = 2000;
    clip->warpMode = WarpMode::Repitch;
    clip->originalBPM = 120.0;
    
    m_transport.setBPM(60.0); // half project BPM → speed ratio = 120/60 = 2.0
    m_engine.scheduleClip(0, 0, clip.get());
    
    // Process enough frames for fade-in to complete (~500 frames)
    std::vector<float> output(1200, 0.0f);
    m_engine.processTrackToBuffer(0, output.data(), 600, 1);
    
    // After fade completes (~500 frames), samples should reflect double-speed.
    // At frame 550, fractional position ≈ 1100, value ≈ 1100
    // With gain=1.0 (fade complete), output should be close to source value.
    EXPECT_GT(output[550], 500.0f);
    
    // Also verify output is generally increasing after fade-in
    bool increasing = true;
    for (int i = 502; i < 600; ++i) {
        if (output[i] < output[i - 1] - 1.0f) { increasing = false; break; }
    }
    EXPECT_TRUE(increasing);
}

TEST_F(WarpPlaybackTest, RepitchHalfSpeed) {
    auto clip = makeWarpClip(1, 100);
    float* data = clip->buffer->channelData(0);
    for (int i = 0; i < 100; ++i) data[i] = static_cast<float>(i);
    clip->warpMode = WarpMode::Repitch;
    clip->originalBPM = 120.0;
    
    m_transport.setBPM(240.0); // speed ratio = 120/240 = 0.5
    m_engine.scheduleClip(0, 0, clip.get());
    
    std::vector<float> output(20, 0.0f);
    m_engine.processTrackToBuffer(0, output.data(), 10, 1);
    
    // At speed ratio 0.5, advances half a sample per frame
    // Values should increase more slowly
    EXPECT_GT(output[9], 0.0f);
    // At half speed, after 10 frames we should be around sample position 5
    // (with fade-in reducing early values)
}

TEST_F(WarpPlaybackTest, NonLoopingWarpedClipStops) {
    auto clip = makeWarpClip(1, 10);
    float* data = clip->buffer->channelData(0);
    for (int i = 0; i < 10; ++i) data[i] = 1.0f;
    clip->looping = false;
    clip->warpMode = WarpMode::Beats;
    clip->originalBPM = 120.0;
    
    m_transport.setBPM(60.0); // speed ratio = 2.0 → finishes in ~5 frames
    m_engine.scheduleClip(0, 0, clip.get());
    
    std::vector<float> output(40, 0.0f);
    m_engine.processTrackToBuffer(0, output.data(), 20, 1);
    
    // Track should become inactive after reaching end
    EXPECT_FALSE(m_engine.isTrackPlaying(0));
}

TEST_F(WarpPlaybackTest, StereoWarpedPlayback) {
    auto clip = makeWarpClip(2, 100);
    float* left = clip->buffer->channelData(0);
    float* right = clip->buffer->channelData(1);
    for (int i = 0; i < 100; ++i) {
        left[i] = 1.0f;
        right[i] = 0.5f;
    }
    clip->warpMode = WarpMode::Repitch;
    clip->originalBPM = 120.0;
    
    m_transport.setBPM(120.0); // 1.0x
    m_engine.scheduleClip(0, 0, clip.get());
    
    // Interleaved stereo output
    std::vector<float> output(40, 0.0f);
    m_engine.processTrackToBuffer(0, output.data(), 10, 2);
    
    // Both channels should have non-zero output (after fade-in)
    bool leftNonZero = false, rightNonZero = false;
    for (int i = 0; i < 10; ++i) {
        if (std::abs(output[i * 2]) > 0.01f) leftNonZero = true;
        if (std::abs(output[i * 2 + 1]) > 0.01f) rightNonZero = true;
    }
    EXPECT_TRUE(leftNonZero);
    EXPECT_TRUE(rightNonZero);
}

TEST_F(WarpPlaybackTest, WarpSameBPMMatchesNonWarp) {
    // When warp ratio is 1.0, warped playback should produce same results
    auto clip1 = makeWarpClip(1, 200);
    auto clip2 = makeWarpClip(1, 200);
    float* data1 = clip1->buffer->channelData(0);
    float* data2 = clip2->buffer->channelData(0);
    for (int i = 0; i < 200; ++i) {
        float v = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * i / 44100.0f);
        data1[i] = v;
        data2[i] = v;
    }
    clip1->warpMode = WarpMode::Off;
    clip2->warpMode = WarpMode::Repitch;
    clip2->originalBPM = 120.0;
    
    m_transport.setBPM(120.0); // ratio = 1.0
    
    ClipEngine engine1, engine2;
    engine1.setTransport(&m_transport);
    engine1.setSampleRate(44100.0);
    engine1.setQuantizeMode(QuantizeMode::None);
    engine2.setTransport(&m_transport);
    engine2.setSampleRate(44100.0);
    engine2.setQuantizeMode(QuantizeMode::None);
    
    engine1.scheduleClip(0, 0, clip1.get());
    engine2.scheduleClip(0, 0, clip2.get());
    
    std::vector<float> out1(200, 0.0f), out2(200, 0.0f);
    engine1.processTrackToBuffer(0, out1.data(), 100, 1);
    engine2.processTrackToBuffer(0, out2.data(), 100, 1);
    
    // At ratio 1.0, both paths should produce identical output
    for (int i = 0; i < 100; ++i) {
        EXPECT_NEAR(out1[i], out2[i], 0.001f) << "at frame " << i;
    }
}
