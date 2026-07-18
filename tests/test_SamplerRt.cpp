#include <gtest/gtest.h>
#include "instruments/Sampler.h"
#include "util/RtRetireList.h"
#include <atomic>
#include <memory>
#include <vector>

using namespace yawn;

static std::vector<float> makeRamp(int frames, int channels, float scale) {
    std::vector<float> v(static_cast<size_t>(frames) * channels);
    for (int i = 0; i < frames * channels; ++i)
        v[static_cast<size_t>(i)] = scale * ((i % 64) / 63.0f - 0.5f);
    return v;
}

TEST(SamplerRt, LoadPublishesBufferToGetters) {
    instruments::Sampler s;
    s.init(48000.0, 256);
    EXPECT_FALSE(s.hasSample());

    auto data = makeRamp(128, 2, 1.0f);
    s.loadSample(data.data(), 128, 2);
    EXPECT_TRUE(s.hasSample());
    EXPECT_EQ(s.sampleFrames(), 128);
    EXPECT_EQ(s.sampleChannels(), 2);
    ASSERT_NE(s.sampleData(), nullptr);
}

TEST(SamplerRt, SwapRetiresOldBufferWithRetireList) {
    std::atomic<uint64_t> seq{0};
    util::RtRetireList rl;
    rl.setHeartbeat(&seq);

    instruments::Sampler s;
    s.setRetireList(&rl);
    s.init(48000.0, 256);

    auto a = makeRamp(128, 1, 1.0f);
    s.loadSample(a.data(), 128, 1);
    const float* oldData = s.sampleData();

    auto b = makeRamp(256, 1, 0.5f);
    s.loadSample(b.data(), 256, 1);

    // Getters see the new buffer; the old one is parked in the retire
    // list (an in-flight audio block may still be reading it).
    EXPECT_EQ(s.sampleFrames(), 256);
    EXPECT_NE(s.sampleData(), oldData);
    EXPECT_EQ(rl.size(), 1u);

    // Old buffer is still valid memory until the grace purge.
    EXPECT_NO_FATAL_FAILURE({ (void)oldData[0]; });

    seq.store(util::RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(rl.size(), 0u);
}

TEST(SamplerRt, ProcessAfterSwapProducesAudio) {
    instruments::Sampler s;
    s.init(48000.0, 256);

    auto a = makeRamp(512, 2, 1.0f);
    s.loadSample(a.data(), 512, 2);

    // Swap mid-"playback" (no retire list → immediate destroy; the
    // block-local pointer discipline is what makes this safe).
    auto b = makeRamp(512, 2, 1.0f);
    s.loadSample(b.data(), 512, 2);

    midi::MidiBuffer midi;
    midi::MidiMessage noteOn{};
    noteOn.type = midi::MidiMessage::Type::NoteOn;
    noteOn.note = 60;
    noteOn.velocity = 100u << 9;
    midi.addMessage(noteOn);

    std::vector<float> buf(256 * 2, 0.0f);
    s.process(buf.data(), 256, 2, midi);

    float peak = 0.0f;
    for (float x : buf) peak = std::max(peak, std::abs(x));
    EXPECT_GT(peak, 0.0f) << "expected the new sample to render";
}

TEST(SamplerRt, ClearSampleSilencesAndUnpublishes) {
    instruments::Sampler s;
    s.init(48000.0, 256);
    auto a = makeRamp(128, 1, 1.0f);
    s.loadSample(a.data(), 128, 1);
    EXPECT_TRUE(s.hasSample());

    s.clearSample();
    EXPECT_FALSE(s.hasSample());
    EXPECT_EQ(s.sampleFrames(), 0);
    EXPECT_EQ(s.sampleData(), nullptr);

    std::vector<float> buf(64 * 2, 0.0f);
    midi::MidiBuffer midi;
    s.process(buf.data(), 64, 2, midi);
    for (float x : buf) EXPECT_EQ(x, 0.0f);
}
