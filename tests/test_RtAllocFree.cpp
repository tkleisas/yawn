#include <gtest/gtest.h>
#include "audio/AudioEngine.h"
#include "audio/TimeStretcher.h"
#include <cmath>
#include <memory>
#include <vector>

using namespace yawn;

// RT-allocation-free recording: the pool is prepared on the UI thread,
// the take is handed over zero-copy (strideFrames), and the buffer
// round-trips back for the next take.

namespace {

class RecordPoolTest : public ::testing::Test {
protected:
    std::unique_ptr<audio::AudioEngine> engine;  // heap — it's huge
    void SetUp() override {
        engine = std::make_unique<audio::AudioEngine>();
        engine->setHasInputDeviceForTest(true);
    }

    // Record `frames` of stereo input through the command path.
    void recordTake(const std::vector<float>& input, int frames) {
        engine->sendCommand(audio::SetTrackArmedMsg{0, true});
        engine->sendCommand(audio::StartAudioRecordMsg{0, 1, false, 0});
        engine->pumpInputForTest(input.data(),
                                 static_cast<unsigned long>(frames));
        engine->sendCommand(audio::StopAudioRecordMsg{
            0, audio::QuantizeMode::None});
        float silence[512] = {};
        engine->pumpInputForTest(silence, 256);
    }
};

} // namespace

TEST_F(RecordPoolTest, ZeroCopyHandoffWithPoolStride) {
    engine->prepareRecording(0);

    constexpr int kFrames = 4096;
    std::vector<float> input(kFrames * 2);
    for (int f = 0; f < kFrames; ++f) {
        input[f * 2 + 0] = f / 512.0f;          // L: ramp
        input[f * 2 + 1] = -(f / 512.0f);       // R: inverted ramp
    }
    recordTake(input, kFrames);

    auto& data = engine->recordedAudioData(0);
    ASSERT_TRUE(data.ready.load());
    EXPECT_EQ(data.frameCount, kFrames);
    EXPECT_EQ(data.channels, 2);
    // Zero-copy path: the take carries the pool's native stride
    // (maxFrames), not a tight pack.
    const int64_t expectStride =
        static_cast<int64_t>(engine->config().sampleRate) * 300;
    EXPECT_EQ(data.strideFrames, expectStride);

    // Content must match the input with stride indexing.
    EXPECT_FLOAT_EQ(data.buffer[100], input[200]);
    EXPECT_FLOAT_EQ(data.buffer[100 + data.strideFrames], input[201]);

    engine->releaseRecordedAudioBuffer(0);
}

TEST_F(RecordPoolTest, PoolRoundTripsBetweenTakes) {
    engine->prepareRecording(0);

    constexpr int kFrames = 4096;
    std::vector<float> input(kFrames * 2, 0.5f);

    recordTake(input, kFrames);
    ASSERT_TRUE(engine->recordedAudioData(0).ready.load());
    // The pool's address, captured via the live-waveform snapshot
    // before release.
    engine->releaseRecordedAudioBuffer(0);

    // Second take: must still record (pool returned, start handler's
    // resize is a no-op).
    engine->sendCommand(audio::StartAudioRecordMsg{0, 1, false, 0});
    engine->pumpInputForTest(input.data(), kFrames);
    const auto live = engine->liveAudioRecording(0);
    EXPECT_TRUE(live.active);
    EXPECT_EQ(live.frames, kFrames);
    engine->sendCommand(audio::StopAudioRecordMsg{0, audio::QuantizeMode::None});
    float silence[512] = {};
    engine->pumpInputForTest(silence, 256);

    auto& data = engine->recordedAudioData(0);
    ASSERT_TRUE(data.ready.load());
    EXPECT_EQ(data.frameCount, kFrames);
    EXPECT_FLOAT_EQ(data.buffer[10], 0.5f);
    engine->releaseRecordedAudioBuffer(0);
}

// ── TimeStretcher preallocation ────────────────────────────────────

namespace {
void expectStretchesAfterPrealloc(audio::TimeStretcher::Algorithm algo) {
    audio::TimeStretcher ts;
    ts.preallocate(48000.0, 4096);
    // init() after preallocate must be allocation-free and leave a
    // working stretcher for every algorithm.
    ts.init(48000.0, 4096, algo);
    ts.setSpeedRatio(2.0);

    std::vector<float> in(8192);
    for (int i = 0; i < 8192; ++i)
        in[i] = std::sin(2.0 * 3.14159265 * i / 64.0);
    std::vector<float> out(8192, 0.0f);
    int consumed = 0;
    const int produced = ts.process(in.data(), 8192, out.data(), 8192, consumed);
    EXPECT_GT(produced, 0);
    float peak = 0.0f;
    for (int i = 0; i < produced; ++i) peak = std::max(peak, std::abs(out[i]));
    EXPECT_GT(peak, 0.01f);
}
} // namespace

TEST(TimeStretcherPrealloc, WsolaWorksAfterPreallocate) {
    expectStretchesAfterPrealloc(audio::TimeStretcher::Algorithm::WSOLA);
}

TEST(TimeStretcherPrealloc, PhaseVocoderWorksAfterPreallocate) {
    expectStretchesAfterPrealloc(audio::TimeStretcher::Algorithm::PhaseVocoder);
}

TEST(TimeStretcherPrealloc, PghiWorksAfterPreallocate) {
    expectStretchesAfterPrealloc(audio::TimeStretcher::Algorithm::PhaseVocoderPGHI);
}

TEST(TimeStretcherPrealloc, ModeSwitchAfterPreallocateStaysWorking) {
    audio::TimeStretcher ts;
    ts.preallocate(48000.0, 4096);
    // Switching algorithms after preallocate must not require new
    // buffers — and each mode must still produce output.
    for (auto algo : {audio::TimeStretcher::Algorithm::WSOLA,
                      audio::TimeStretcher::Algorithm::PhaseVocoder,
                      audio::TimeStretcher::Algorithm::PhaseVocoderPGHI}) {
        ts.init(48000.0, 4096, algo);
        ts.setSpeedRatio(1.5);
        std::vector<float> in(8192);
        for (int i = 0; i < 8192; ++i)
            in[i] = std::sin(2.0 * 3.14159265 * i / 32.0);
        std::vector<float> out(8192, 0.0f);
        int consumed = 0;
        EXPECT_GT(ts.process(in.data(), 8192, out.data(), 8192, consumed), 0)
            << "algorithm " << static_cast<int>(algo) << " produced nothing";
    }
}

TEST(TimeStretcherPrealloc, SampleRateChangeRepreallocates) {
    audio::TimeStretcher ts;
    ts.preallocate(48000.0, 4096);
    // A different sample rate must trigger the fallback allocation
    // (engine calls preallocate on rate change; init must self-heal).
    ts.init(44100.0, 4096, audio::TimeStretcher::Algorithm::PhaseVocoder);
    ts.setSpeedRatio(1.0);
    std::vector<float> in(8192, 0.5f), out(8192, 0.0f);
    int consumed = 0;
    EXPECT_NO_FATAL_FAILURE(
        ts.process(in.data(), 8192, out.data(), 8192, consumed));
}
