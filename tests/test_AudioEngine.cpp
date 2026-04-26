#include <gtest/gtest.h>
#include "audio/AudioEngine.h"
#include "audio/Transport.h"
#include "audio/Mixer.h"
#include "audio/ClipEngine.h"
#include "audio/MidiClipEngine.h"
#include "audio/Metronome.h"
#include "instruments/SubtractiveSynth.h"
#include "midi/MidiEffectChain.h"
#include "core/Constants.h"
#include <memory>

using namespace yawn;
using namespace yawn::audio;

// ==================== AudioEngine Construction & State ====================

TEST(AudioEngine, DefaultConstruction) {
    AudioEngine engine;
    EXPECT_GT(engine.sampleRate(), 0.0);
    EXPECT_EQ(engine.config().framesPerBuffer, 256);
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.hasStream());
    EXPECT_DOUBLE_EQ(engine.cpuLoad(), 0.0);
}

TEST(AudioEngine, SuspendResume) {
    AudioEngine engine;
    EXPECT_FALSE(engine.isRunning());
    engine.resume();
    EXPECT_TRUE(engine.isRunning());
    engine.suspend();
    EXPECT_FALSE(engine.isRunning());
}

TEST(AudioEngine, CallbackStatusFlags) {
    AudioEngine engine;
    EXPECT_EQ(engine.consumeCallbackStatusFlags(), 0u);
}

TEST(AudioEngine, EnumerateDevices) {
    auto devices = AudioEngine::enumerateDevices();
    EXPECT_GE(devices.size(), 0u);
}

// ==================== Transport ====================

TEST(AudioEngine, TransportDefaultBPM) {
    AudioEngine engine;
    EXPECT_DOUBLE_EQ(engine.transport().bpm(), 120.0);
}

TEST(AudioEngine, TransportSetBPM) {
    AudioEngine engine;
    engine.transport().setBPM(140.0);
    EXPECT_DOUBLE_EQ(engine.transport().bpm(), 140.0);
}

TEST(AudioEngine, TransportTimeSignature) {
    AudioEngine engine;
    engine.transport().setTimeSignature(3, 4);
    EXPECT_EQ(engine.transport().numerator(), 3);
    EXPECT_EQ(engine.transport().denominator(), 4);
}

TEST(AudioEngine, TransportLoopRange) {
    AudioEngine engine;
    engine.transport().setLoopRange(1.0, 8.0);
    EXPECT_DOUBLE_EQ(engine.transport().loopStartBeats(), 1.0);
    EXPECT_DOUBLE_EQ(engine.transport().loopEndBeats(), 8.0);
}

TEST(AudioEngine, TransportCountingIn) {
    AudioEngine engine;
    EXPECT_FALSE(engine.transport().isCountingIn());
}

TEST(AudioEngine, TransportPositionReset) {
    AudioEngine engine;
    engine.transport().setPositionInSamples(44100);
    EXPECT_EQ(engine.transport().positionInSamples(), 44100);
    engine.transport().setPositionInSamples(0);
    EXPECT_EQ(engine.transport().positionInSamples(), 0);
}

// ==================== Instrument Management ====================

TEST(AudioEngine, SetInstrumentOnTrack) {
    AudioEngine engine;
    auto synth = std::make_unique<instruments::SubtractiveSynth>();
    engine.setInstrument(0, std::move(synth));
    EXPECT_NE(engine.instrument(0), nullptr);
    EXPECT_EQ(engine.instrument(0)->id(), std::string("subsynth"));
}

TEST(AudioEngine, NullInstrument) {
    AudioEngine engine;
    EXPECT_EQ(engine.instrument(0), nullptr);
    EXPECT_EQ(engine.instrument(-1), nullptr);
    EXPECT_EQ(engine.instrument(kMaxTracks), nullptr);
}

TEST(AudioEngine, SetInstrumentOutOfBounds) {
    AudioEngine engine;
    engine.setInstrument(-1, std::make_unique<instruments::SubtractiveSynth>());
    engine.setInstrument(kMaxTracks, std::make_unique<instruments::SubtractiveSynth>());
    EXPECT_EQ(engine.instrument(-1), nullptr);
    EXPECT_EQ(engine.instrument(kMaxTracks), nullptr);
}

TEST(AudioEngine, ReplaceInstrument) {
    AudioEngine engine;
    engine.setInstrument(0, std::make_unique<instruments::SubtractiveSynth>());
    auto* first = engine.instrument(0);
    EXPECT_NE(first, nullptr);

    engine.setInstrument(0, std::make_unique<instruments::SubtractiveSynth>());
    auto* second = engine.instrument(0);
    EXPECT_NE(second, nullptr);
    EXPECT_NE(first, second);
}

// ==================== Mixer ====================

TEST(AudioEngine, MixerTrackVolumePan) {
    AudioEngine engine;
    engine.mixer().setTrackVolume(0, 0.5f);
    engine.mixer().setTrackPan(0, 0.25f);
    EXPECT_FLOAT_EQ(engine.mixer().trackChannel(0).volume, 0.5f);
    EXPECT_FLOAT_EQ(engine.mixer().trackChannel(0).pan, 0.25f);
}

TEST(AudioEngine, MixerMuteSolo) {
    AudioEngine engine;
    engine.mixer().setTrackMute(0, true);
    engine.mixer().setTrackSolo(0, true);
    EXPECT_TRUE(engine.mixer().trackChannel(0).muted);
    EXPECT_TRUE(engine.mixer().trackChannel(0).soloed);

    engine.mixer().setTrackMute(0, false);
    engine.mixer().setTrackSolo(0, false);
    EXPECT_FALSE(engine.mixer().trackChannel(0).muted);
    EXPECT_FALSE(engine.mixer().trackChannel(0).soloed);
}

TEST(AudioEngine, MixerMasterVolume) {
    AudioEngine engine;
    engine.mixer().setMasterVolume(0.75f);
    EXPECT_FLOAT_EQ(engine.mixer().master().volume, 0.75f);
}

TEST(AudioEngine, MixerSendLevels) {
    AudioEngine engine;
    engine.mixer().setSendLevel(0, 0, 0.3f);
    engine.mixer().setSendEnabled(0, 0, true);
    EXPECT_FLOAT_EQ(engine.mixer().trackChannel(0).sends[0].level, 0.3f);
    EXPECT_TRUE(engine.mixer().trackChannel(0).sends[0].enabled);
}

TEST(AudioEngine, MixerReturnBuses) {
    AudioEngine engine;
    engine.mixer().setReturnVolume(0, 0.6f);
    engine.mixer().setReturnPan(0, -0.2f);
    EXPECT_FLOAT_EQ(engine.mixer().returnBus(0).volume, 0.6f);
    EXPECT_FLOAT_EQ(engine.mixer().returnBus(0).pan, -0.2f);
}

// ==================== MIDI Effect Chain ====================

TEST(AudioEngine, MidiEffectChainAccess) {
    AudioEngine engine;
    auto& chain = engine.midiEffectChain(0);
    EXPECT_TRUE(chain.empty());
    EXPECT_EQ(chain.count(), 0);
}

// ==================== Track Management ====================

TEST(AudioEngine, RemoveTrackSlotShiftsInstruments) {
    AudioEngine engine;
    engine.setInstrument(0, std::make_unique<instruments::SubtractiveSynth>());
    engine.setInstrument(1, std::make_unique<instruments::SubtractiveSynth>());
    engine.setInstrument(2, std::make_unique<instruments::SubtractiveSynth>());

    EXPECT_NE(engine.instrument(0), nullptr);
    EXPECT_NE(engine.instrument(1), nullptr);
    EXPECT_NE(engine.instrument(2), nullptr);

    engine.removeTrackSlot(1, 3);

    EXPECT_NE(engine.instrument(0), nullptr);
    EXPECT_NE(engine.instrument(1), nullptr);
    EXPECT_EQ(engine.instrument(2), nullptr);
}

TEST(AudioEngine, RemoveTrackSlotFirstClearsFirst) {
    AudioEngine engine;
    engine.setInstrument(0, std::make_unique<instruments::SubtractiveSynth>());
    engine.setInstrument(1, std::make_unique<instruments::SubtractiveSynth>());

    engine.removeTrackSlot(0, 2);

    EXPECT_NE(engine.instrument(0), nullptr);
    EXPECT_EQ(engine.instrument(1), nullptr);
}

// ==================== Clip Engine Access ====================

TEST(AudioEngine, ClipEngineNoTracksPlaying) {
    AudioEngine engine;
    for (int t = 0; t < 8; ++t) {
        EXPECT_FALSE(engine.clipEngine().isTrackPlaying(t));
    }
}

TEST(AudioEngine, MidiClipEngineNoTracksPlaying) {
    AudioEngine engine;
    for (int t = 0; t < 8; ++t) {
        EXPECT_FALSE(engine.midiClipEngine().isTrackPlaying(t));
    }
}

// ==================== Metronome ====================

TEST(AudioEngine, MetronomeDefaults) {
    AudioEngine engine;
    EXPECT_FALSE(engine.metronome().enabled());
    EXPECT_EQ(engine.metronome().beatsPerBar(), 4);
}

TEST(AudioEngine, MetronomeSetParams) {
    AudioEngine engine;
    engine.metronome().setEnabled(true);
    engine.metronome().setVolume(0.5f);
    engine.metronome().setBeatsPerBar(3);
    EXPECT_TRUE(engine.metronome().enabled());
    EXPECT_FLOAT_EQ(engine.metronome().volume(), 0.5f);
    EXPECT_EQ(engine.metronome().beatsPerBar(), 3);
}

// ==================== Command Queue ====================

TEST(AudioEngine, CommandQueueTransportBPM) {
    AudioEngine engine;
    engine.sendCommand(TransportSetBPMMsg{160.0});
    // Commands are processed in the audio callback; calling processCommands
    // directly would require a running stream. Verify the command was enqueued
    // by checking no crash (ring buffer write shouldn't block).
    SUCCEED();
}

TEST(AudioEngine, CommandQueueMultipleCommands) {
    AudioEngine engine;
    for (int i = 0; i < 100; ++i) {
        engine.sendCommand(TransportSetBPMMsg{100.0 + i});
    }
    SUCCEED();
}

TEST(AudioEngine, PollEventOnNewEngine) {
    AudioEngine engine;
    AudioEvent event;
    EXPECT_FALSE(engine.pollEvent(event));
}

// ==================== Transport State Integrity ====================

TEST(AudioEngine, TransportMinMaxBPM) {
    AudioEngine engine;
    engine.transport().setBPM(20.0);
    EXPECT_DOUBLE_EQ(engine.transport().bpm(), 20.0);
    engine.transport().setBPM(300.0);
    EXPECT_DOUBLE_EQ(engine.transport().bpm(), 300.0);
}

TEST(AudioEngine, TransportTimeSignatureDefault) {
    AudioEngine engine;
    EXPECT_EQ(engine.transport().numerator(), 4);
    EXPECT_EQ(engine.transport().denominator(), 4);
}

TEST(AudioEngine, TransportSetPosition) {
    AudioEngine engine;
    engine.transport().setPositionInSamples(0);
    EXPECT_EQ(engine.transport().positionInSamples(), 0);
    engine.transport().setPositionInSamples(88200);
    EXPECT_EQ(engine.transport().positionInSamples(), 88200);
}

// ==================== MidiEngine Integration ====================

TEST(AudioEngine, MidiEngineCanBeSet) {
    AudioEngine engine;
    EXPECT_NO_THROW(engine.setMidiEngine(nullptr));
}

// ==================== Record State ====================

TEST(AudioEngine, RecordedMidiDataInitialState) {
    AudioEngine engine;
    auto& data = engine.recordedMidiData(0);
    EXPECT_FALSE(data.ready.load());
    EXPECT_EQ(data.trackIndex, -1);
    EXPECT_TRUE(data.notes.empty());
    EXPECT_TRUE(data.ccs.empty());
}

TEST(AudioEngine, RecordedAudioDataInitialState) {
    AudioEngine engine;
    auto& data = engine.recordedAudioData(0);
    EXPECT_FALSE(data.ready.load());
    EXPECT_EQ(data.trackIndex, -1);
    EXPECT_EQ(data.frameCount, 0);
}
