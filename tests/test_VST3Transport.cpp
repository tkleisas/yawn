#include <gtest/gtest.h>
#ifdef YAWN_HAS_VST3
#include "vst3/VST3Transport.h"
#include "audio/AudioEngine.h"
#include "instruments/SubtractiveSynth.h"
#include <thread>

using namespace yawn;
using Context = Steinberg::Vst::ProcessContext;

namespace {
class TransportProbe : public instruments::SubtractiveSynth {
public:
    Context observed{};
    void process(float*, int, int, const midi::MidiBuffer&) override {
        observed = vst3::processContext(m_sampleRate);
    }
};
}

TEST(VST3Transport, EnginePublishesBlockStartAndAdvancesClockWhileStopped) {
    audio::AudioEngine engine;
    engine.initHeadlessForTest();
    auto probe = std::make_unique<TransportProbe>();
    auto* observed = probe.get();
    engine.setInstrument(0, std::move(probe));
    engine.transport().setBPM(137);
    engine.transport().setPositionInSamples(48000);
    engine.transport().play();
    float output[512]{};
    engine.renderBuffer(output, 256);
    EXPECT_DOUBLE_EQ(observed->observed.tempo, 137);
    EXPECT_EQ(observed->observed.projectTimeSamples, 48000);
    EXPECT_EQ(observed->observed.continousTimeSamples, 0);
    EXPECT_TRUE(observed->observed.state & Context::kPlaying);
    engine.transport().stop();
    engine.renderBuffer(output, 256);
    EXPECT_FALSE(observed->observed.state & Context::kPlaying);
    EXPECT_EQ(observed->observed.continousTimeSamples, 256);
    EXPECT_EQ(vst3::currentTransport, nullptr);
}

TEST(VST3Transport, ReportsTempoPositionAndNonQuarterTimeSignature) {
    audio::Transport transport;
    transport.setSampleRate(48000);
    transport.setBPM(137);
    transport.setTimeSignature(7, 8);
    transport.setPositionInSamples(105109); // five quarter notes, in bar two
    transport.setLoopRange(3.5, 7);
    transport.setLoopEnabled(true);
    transport.play();
    transport.startRecording();
    auto context = vst3::makeProcessContext(transport, 123456);
    EXPECT_DOUBLE_EQ(context.tempo, 137);
    EXPECT_EQ(context.projectTimeSamples, 105109);
    EXPECT_EQ(context.continousTimeSamples, 123456);
    EXPECT_NEAR(context.projectTimeMusic, 5, 0.001);
    EXPECT_DOUBLE_EQ(context.barPositionMusic, 3.5);
    EXPECT_EQ(context.timeSigNumerator, 7);
    EXPECT_EQ(context.timeSigDenominator, 8);
    EXPECT_DOUBLE_EQ(context.cycleStartMusic, 3.5);
    EXPECT_DOUBLE_EQ(context.cycleEndMusic, 7);
    EXPECT_TRUE(context.state & Context::kPlaying);
    EXPECT_TRUE(context.state & Context::kRecording);
    EXPECT_TRUE(context.state & Context::kCycleActive);
    EXPECT_TRUE(context.state & Context::kProjectTimeMusicValid);
}

TEST(VST3Transport, FollowsStopSeekTempoChangesAndLoopWrap) {
    audio::Transport transport;
    transport.setSampleRate(48000);
    transport.setBPM(120);
    transport.setLoopRange(0, 4);
    transport.setLoopEnabled(true);
    transport.setPositionInSamples(95900);
    transport.play();
    transport.advance(256);
    const auto wrapped = vst3::makeProcessContext(transport, 256);
    EXPECT_LT(wrapped.projectTimeMusic, 0.1);
    transport.stop();
    transport.setPositionInSamples(48000);
    transport.setBPM(150);
    const auto stopped = vst3::makeProcessContext(transport, 512);
    EXPECT_FALSE(stopped.state & Context::kPlaying);
    EXPECT_FALSE(stopped.state & Context::kRecording);
    EXPECT_DOUBLE_EQ(stopped.tempo, 150);
    EXPECT_DOUBLE_EQ(stopped.projectTimeMusic, 2.5);
    EXPECT_EQ(stopped.projectTimeSamples, 48000);
}

TEST(VST3Transport, CountInDoesNotStartPluginTransport) {
    audio::Transport transport;
    transport.setCountInBars(1);
    transport.beginCountIn();
    transport.play();
    transport.startRecording();
    auto context = vst3::makeProcessContext(transport, 0);
    EXPECT_FALSE(context.state & Context::kPlaying);
    EXPECT_FALSE(context.state & Context::kRecording);
}

TEST(VST3Transport, OversampledEffectsReceiveTheirOwnSampleClock) {
    audio::Transport transport;
    transport.setSampleRate(48000);
    transport.setBPM(137);
    transport.setPositionInSamples(48000);
    vst3::TransportScope scope(transport, 96000);
    const auto context = vst3::processContext(96000);
    EXPECT_DOUBLE_EQ(context.sampleRate, 96000);
    EXPECT_EQ(context.projectTimeSamples, 96000);
    EXPECT_EQ(context.continousTimeSamples, 192000);
    EXPECT_DOUBLE_EQ(context.projectTimeMusic, transport.positionInBeats());
    EXPECT_DOUBLE_EQ(context.tempo, 137);
}

TEST(VST3Transport, ScopeRestoresContextAndDoesNotLeakAcrossThreads) {
    audio::Transport outer, inner;
    outer.setBPM(137);
    inner.setBPM(155);
    {
        vst3::TransportScope first(outer, 0);
        EXPECT_DOUBLE_EQ(vst3::processContext(48000).tempo, 137);
        {
            vst3::TransportScope second(inner, 1);
            EXPECT_DOUBLE_EQ(vst3::processContext(48000).tempo, 155);
        }
        EXPECT_DOUBLE_EQ(vst3::processContext(48000).tempo, 137);
        std::thread independent([] {
            auto context = vst3::processContext(44100);
            EXPECT_DOUBLE_EQ(context.sampleRate, 44100);
            EXPECT_FALSE(context.state & Context::kPlaying);
            EXPECT_DOUBLE_EQ(context.tempo, 120);
        });
        independent.join();
    }
    EXPECT_EQ(vst3::currentTransport, nullptr);
}
#endif
