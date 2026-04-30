#include <gtest/gtest.h>
#include "instruments/Envelope.h"
#include "instruments/Oscillator.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/InstrumentRack.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/DrumSynth.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "instruments/Multisampler.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>

using namespace yawn::instruments;
using namespace yawn::midi;

static constexpr double kSampleRate = 44100.0;
static constexpr int    kBlockSize  = 256;
static constexpr int    kChannels   = 2;

// Helper: create a MidiBuffer with a single NoteOn
static MidiBuffer makeNoteOn(uint8_t note = 60, uint8_t vel7 = 100) {
    MidiBuffer buf;
    MidiMessage msg{};
    msg.type     = MidiMessage::Type::NoteOn;
    msg.channel  = 0;
    msg.note     = note;
    msg.velocity = Convert::vel7to16(vel7);
    buf.addMessage(msg);
    return buf;
}

static MidiBuffer makeNoteOff(uint8_t note = 60) {
    MidiBuffer buf;
    MidiMessage msg{};
    msg.type    = MidiMessage::Type::NoteOff;
    msg.channel = 0;
    msg.note    = note;
    buf.addMessage(msg);
    return buf;
}

static MidiBuffer makeEmpty() { return MidiBuffer{}; }

static float rms(const float* buf, int n) {
    float sum = 0;
    for (int i = 0; i < n; ++i) sum += buf[i] * buf[i];
    return std::sqrt(sum / n);
}

// ========================= Envelope =========================

TEST(Envelope, AttackRisesToOne) {
    Envelope env;
    env.setSampleRate(kSampleRate);
    env.setADSR(0.01f, 0.1f, 1.0f, 0.1f);
    env.gate(true);

    float peak = 0;
    for (int i = 0; i < (int)(kSampleRate * 0.02); ++i)
        peak = std::max(peak, env.process());

    EXPECT_GT(peak, 0.95f);
}

TEST(Envelope, ReleaseGoesToZero) {
    Envelope env;
    env.setSampleRate(kSampleRate);
    env.setADSR(0.001f, 0.001f, 1.0f, 0.01f);
    env.gate(true);
    for (int i = 0; i < 200; ++i) env.process();
    env.gate(false);
    for (int i = 0; i < (int)(kSampleRate * 0.05); ++i) env.process();
    EXPECT_TRUE(env.isIdle());
}

TEST(Envelope, SustainLevel) {
    Envelope env;
    env.setSampleRate(kSampleRate);
    env.setADSR(0.001f, 0.01f, 0.5f, 0.1f);
    env.gate(true);
    float val = 0;
    for (int i = 0; i < (int)(kSampleRate * 0.1); ++i)
        val = env.process();
    EXPECT_NEAR(val, 0.5f, 0.05f);
}

// ========================= Oscillator =========================

TEST(Oscillator, SineProducesOutput) {
    Oscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.setWaveform(Oscillator::Sine);
    osc.setFrequency(440.0f);

    float sum = 0;
    for (int i = 0; i < kBlockSize; ++i)
        sum += std::abs(osc.process());
    EXPECT_GT(sum, 0.1f);
}

TEST(Oscillator, AllWaveformsProduceOutput) {
    for (int w = 0; w <= 4; ++w) {
        Oscillator osc;
        osc.setSampleRate(kSampleRate);
        osc.setWaveform((Oscillator::Waveform)w);
        osc.setFrequency(440.0f);

        float sum = 0;
        for (int i = 0; i < 1024; ++i)
            sum += std::abs(osc.process());
        EXPECT_GT(sum, 0.1f) << "Waveform " << w << " silent";
    }
}

TEST(Oscillator, FrequencyChangesOutput) {
    Oscillator osc1, osc2;
    osc1.setSampleRate(kSampleRate); osc2.setSampleRate(kSampleRate);
    osc1.setWaveform(Oscillator::Sine); osc2.setWaveform(Oscillator::Sine);
    osc1.setFrequency(220.0f); osc2.setFrequency(880.0f);

    // Count zero crossings in 4096 samples
    int zc1 = 0, zc2 = 0;
    float prev1 = 0, prev2 = 0;
    for (int i = 0; i < 4096; ++i) {
        float s1 = osc1.process(), s2 = osc2.process();
        if (i > 0) {
            if ((prev1 >= 0 && s1 < 0) || (prev1 < 0 && s1 >= 0)) ++zc1;
            if ((prev2 >= 0 && s2 < 0) || (prev2 < 0 && s2 >= 0)) ++zc2;
        }
        prev1 = s1; prev2 = s2;
    }
    EXPECT_GT(zc2, zc1 * 1.5); // 880 Hz should have ~4x zero crossings of 220 Hz
}

// ========================= SubtractiveSynth =========================

TEST(SubtractiveSynth, InitAndReset) {
    SubtractiveSynth synth;
    synth.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(synth.name(), "Subtractive Synth");
    EXPECT_EQ(synth.parameterCount(), SubtractiveSynth::kNumParams);
    synth.reset();
}

TEST(SubtractiveSynth, NoteOnProducesOutput) {
    SubtractiveSynth synth;
    synth.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    synth.process(buffer, kBlockSize, kChannels, midi);

    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(SubtractiveSynth, SilenceWhenNoNotes) {
    SubtractiveSynth synth;
    synth.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto empty = makeEmpty();
    synth.process(buffer, kBlockSize, kChannels, empty);

    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 0.0001f);
}

TEST(SubtractiveSynth, NoteOffRelease) {
    SubtractiveSynth synth;
    synth.init(kSampleRate, kBlockSize);
    synth.setParameter(SubtractiveSynth::kAmpRelease, 0.01f);

    float buffer[kBlockSize * kChannels];
    auto noteOn = makeNoteOn();
    std::memset(buffer, 0, sizeof(buffer));
    synth.process(buffer, kBlockSize, kChannels, noteOn);
    float rmsOn = rms(buffer, kBlockSize * kChannels);

    auto noteOff = makeNoteOff();
    std::memset(buffer, 0, sizeof(buffer));
    synth.process(buffer, kBlockSize, kChannels, noteOff);

    // Process a few more blocks for release to finish
    for (int b = 0; b < 10; ++b) {
        std::memset(buffer, 0, sizeof(buffer));
        auto empty = makeEmpty();
        synth.process(buffer, kBlockSize, kChannels, empty);
    }

    float rmsAfter = rms(buffer, kBlockSize * kChannels);
    EXPECT_LT(rmsAfter, rmsOn * 0.1f);
}

TEST(SubtractiveSynth, ParameterGetSet) {
    SubtractiveSynth synth;
    synth.init(kSampleRate, kBlockSize);
    // Filter cutoff now stored 0..1 (log-mapped to 20..20000 Hz for
    // uniform LFO modulation). Verify round-trip on a normalized value.
    synth.setParameter(SubtractiveSynth::kFilterCutoff, 0.6f);
    EXPECT_NEAR(synth.getParameter(SubtractiveSynth::kFilterCutoff), 0.6f, 0.001f);
    synth.setParameter(SubtractiveSynth::kVolume, 0.5f);
    EXPECT_NEAR(synth.getParameter(SubtractiveSynth::kVolume), 0.5f, 0.01f);
}

TEST(SubtractiveSynth, VoiceStealing) {
    SubtractiveSynth synth;
    synth.init(kSampleRate, kBlockSize);

    // Trigger more notes than max voices
    MidiBuffer midi;
    for (int n = 40; n < 40 + SubtractiveSynth::kMaxVoices + 4; ++n) {
        MidiMessage msg{};
        msg.type = MidiMessage::Type::NoteOn;
        msg.note = (uint8_t)n;
        msg.velocity = Convert::vel7to16(80);
        midi.addMessage(msg);
    }

    float buffer[kBlockSize * kChannels] = {};
    synth.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

// ========================= FMSynth =========================

TEST(FMSynth, InitAndReset) {
    FMSynth synth;
    synth.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(synth.name(), "FM Synth");
    EXPECT_EQ(synth.parameterCount(), FMSynth::kNumParams);
    synth.reset();
}

TEST(FMSynth, NoteOnProducesOutput) {
    FMSynth synth;
    synth.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    synth.process(buffer, kBlockSize, kChannels, midi);

    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(FMSynth, AllAlgorithmsProduceOutput) {
    for (int a = 0; a < FMSynth::kNumAlgos; ++a) {
        FMSynth synth;
        synth.init(kSampleRate, kBlockSize);
        synth.setParameter(FMSynth::kAlgorithm, (float)a);

        float buffer[kBlockSize * kChannels] = {};
        auto midi = makeNoteOn(60, 100);
        synth.process(buffer, kBlockSize, kChannels, midi);

        EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f)
            << "Algorithm " << a << " silent";
    }
}

TEST(FMSynth, ParameterGetSet) {
    FMSynth synth;
    synth.init(kSampleRate, kBlockSize);
    synth.setParameter(FMSynth::kOp1Ratio, 3.5f);
    EXPECT_NEAR(synth.getParameter(FMSynth::kOp1Ratio), 3.5f, 0.01f);
    synth.setParameter(FMSynth::kAlgorithm, 5);
    EXPECT_NEAR(synth.getParameter(FMSynth::kAlgorithm), 5.0f, 0.01f);
}

// ========================= Sampler =========================

TEST(Sampler, SilenceWithoutSample) {
    Sampler sampler;
    sampler.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn();
    sampler.process(buffer, kBlockSize, kChannels, midi);

    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 0.0001f);
}

TEST(Sampler, PlaysLoadedSample) {
    Sampler sampler;
    sampler.init(kSampleRate, kBlockSize);

    // Generate a test tone (440Hz sine, 1 sec, mono)
    const int sampleLen = (int)kSampleRate;
    std::vector<float> testSample(sampleLen);
    for (int i = 0; i < sampleLen; ++i)
        testSample[i] = std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);
    sampler.loadSample(testSample.data(), sampleLen, 1, 60);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    sampler.process(buffer, kBlockSize, kChannels, midi);

    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.01f);
}

TEST(Sampler, PitchTracking) {
    Sampler sampler;
    sampler.init(kSampleRate, kBlockSize);

    const int sampleLen = (int)kSampleRate;
    std::vector<float> testSample(sampleLen);
    for (int i = 0; i < sampleLen; ++i)
        testSample[i] = std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);
    sampler.loadSample(testSample.data(), sampleLen, 1, 60);

    // Play at root note (C4=60) and one octave up (C5=72)
    float bufRoot[4096 * kChannels] = {};
    auto midiRoot = makeNoteOn(60, 100);
    sampler.process(bufRoot, 4096, kChannels, midiRoot);

    sampler.reset();
    float bufHigh[4096 * kChannels] = {};
    auto midiHigh = makeNoteOn(72, 100);
    sampler.process(bufHigh, 4096, kChannels, midiHigh);

    // Count zero crossings (higher note should have more)
    auto countZC = [](const float* buf, int n) {
        int zc = 0;
        for (int i = 2; i < n; i += 2) // step by channels
            if ((buf[i-2] >= 0 && buf[i] < 0) || (buf[i-2] < 0 && buf[i] >= 0))
                ++zc;
        return zc;
    };
    int zcRoot = countZC(bufRoot, 4096 * kChannels);
    int zcHigh = countZC(bufHigh, 4096 * kChannels);
    EXPECT_GT(zcHigh, zcRoot * 1.5); // Should be ~2x
}

// ========================= InstrumentRack =========================

TEST(InstrumentRack, EmptyRackIsSilent) {
    InstrumentRack rack;
    rack.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn();
    rack.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 0.0001f);
}

TEST(InstrumentRack, SingleChainProducesOutput) {
    InstrumentRack rack;
    auto synth = std::make_unique<SubtractiveSynth>();
    rack.addChain(std::move(synth));
    rack.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    rack.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(InstrumentRack, KeyRangeFiltering) {
    InstrumentRack rack;
    auto synth = std::make_unique<SubtractiveSynth>();
    rack.addChain(std::move(synth), 48, 72); // only C3-C5
    rack.init(kSampleRate, kBlockSize);

    // Note inside range
    float buf1[kBlockSize * kChannels] = {};
    auto midiIn = makeNoteOn(60, 100);
    rack.process(buf1, kBlockSize, kChannels, midiIn);
    float rmsIn = rms(buf1, kBlockSize * kChannels);

    // Note outside range
    rack.reset();
    float buf2[kBlockSize * kChannels] = {};
    auto midiOut = makeNoteOn(36, 100);
    rack.process(buf2, kBlockSize, kChannels, midiOut);
    float rmsOut = rms(buf2, kBlockSize * kChannels);

    EXPECT_GT(rmsIn, 0.001f);
    EXPECT_LT(rmsOut, 0.0001f);
}

TEST(InstrumentRack, LayeringTwoChains) {
    InstrumentRack rack;
    rack.addChain(std::make_unique<SubtractiveSynth>());
    rack.addChain(std::make_unique<SubtractiveSynth>());
    rack.init(kSampleRate, kBlockSize);
    EXPECT_EQ(rack.chainCount(), 2);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    rack.process(buffer, kBlockSize, kChannels, midi);

    // Should produce output (two layers)
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(InstrumentRack, RemoveChain) {
    InstrumentRack rack;
    rack.addChain(std::make_unique<SubtractiveSynth>());
    rack.addChain(std::make_unique<FMSynth>());
    EXPECT_EQ(rack.chainCount(), 2);
    auto removed = rack.removeChain(0);
    EXPECT_NE(removed, nullptr);
    EXPECT_EQ(rack.chainCount(), 1);
}

TEST(InstrumentRack, MaxChains) {
    InstrumentRack rack;
    for (int i = 0; i < InstrumentRack::kMaxChains; ++i)
        EXPECT_TRUE(rack.addChain(std::make_unique<SubtractiveSynth>()));
    EXPECT_FALSE(rack.addChain(std::make_unique<SubtractiveSynth>()));
    EXPECT_EQ(rack.chainCount(), InstrumentRack::kMaxChains);
}

// ========================= DrumRack =========================

TEST(DrumRack, EmptyRackSilent) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    rack.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 0.0001f);
}

TEST(DrumRack, LoadedPadPlays) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    // Load a click sample on pad 36 (kick drum)
    std::vector<float> click(512);
    for (int i = 0; i < 512; ++i)
        click[i] = std::sin(2.0 * M_PI * 100.0 * i / kSampleRate)
                   * std::exp(-(float)i / 200.0f);
    rack.loadPad(36, click.data(), 512, 1);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(36, 100);
    rack.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(DrumRack, DifferentPadsIndependent) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    std::vector<float> click(256, 0.5f); // constant value
    rack.loadPad(36, click.data(), 256, 1);

    // Trigger pad 36 — should produce output
    float buf1[kBlockSize * kChannels] = {};
    auto midi1 = makeNoteOn(36, 100);
    rack.process(buf1, kBlockSize, kChannels, midi1);
    EXPECT_GT(rms(buf1, kBlockSize * kChannels), 0.001f);

    // Trigger pad 37 (empty) — should be silent
    rack.reset();
    float buf2[kBlockSize * kChannels] = {};
    auto midi2 = makeNoteOn(37, 100);
    rack.process(buf2, kBlockSize, kChannels, midi2);
    EXPECT_LT(rms(buf2, kBlockSize * kChannels), 0.0001f);
}

TEST(DrumRack, Retrigger) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    std::vector<float> click(512);
    for (int i = 0; i < 512; ++i)
        click[i] = std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);
    rack.loadPad(36, click.data(), 512, 1);

    // Trigger, advance, trigger again
    float buf[kBlockSize * kChannels] = {};
    auto midi1 = makeNoteOn(36, 100);
    rack.process(buf, kBlockSize, kChannels, midi1);

    // Second trigger should reset position
    std::memset(buf, 0, sizeof(buf));
    auto midi2 = makeNoteOn(36, 100);
    rack.process(buf, kBlockSize, kChannels, midi2);
    EXPECT_GT(rms(buf, kBlockSize * kChannels), 0.001f);
}

TEST(DrumRack, PadPitchAdjust) {
    DrumRack rack;
    rack.init(kSampleRate, 4096);

    // Long sample for counting zero crossings
    const int len = 4096;
    std::vector<float> tone(len);
    for (int i = 0; i < len; ++i)
        tone[i] = std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);
    rack.loadPad(36, tone.data(), len, 1);

    // Normal pitch
    float buf1[4096 * kChannels] = {};
    auto midi1 = makeNoteOn(36, 100);
    rack.process(buf1, 4096, kChannels, midi1);

    // +12 semitones (1 octave up)
    rack.reset();
    rack.setPadPitch(36, 12.0f);
    float buf2[4096 * kChannels] = {};
    auto midi2 = makeNoteOn(36, 100);
    rack.process(buf2, 4096, kChannels, midi2);

    // Higher pitch = more zero crossings
    auto countZC = [](const float* buf, int n) {
        int zc = 0;
        for (int i = 2; i < n; i += 2)
            if ((buf[i-2] >= 0 && buf[i] < 0) || (buf[i-2] < 0 && buf[i] >= 0))
                ++zc;
        return zc;
    };
    EXPECT_GT(countZC(buf2, 4096 * kChannels), countZC(buf1, 4096 * kChannels));
}

TEST(DrumRack, SelectedPadTracking) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    EXPECT_EQ(rack.selectedPad(), 36); // default is C2

    rack.setSelectedPad(60);
    EXPECT_EQ(rack.selectedPad(), 60);

    // Clamped to valid range
    rack.setSelectedPad(-1);
    EXPECT_EQ(rack.selectedPad(), 0);
    rack.setSelectedPad(200);
    EXPECT_EQ(rack.selectedPad(), 127);
}

TEST(DrumRack, HasSampleAndPlaying) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    EXPECT_FALSE(rack.hasSample(36));
    EXPECT_FALSE(rack.isPadPlaying(36));

    float sample[] = {0.5f, 0.3f, 0.1f};
    rack.loadPad(36, sample, 3, 1);
    EXPECT_TRUE(rack.hasSample(36));
    EXPECT_FALSE(rack.hasSample(37));

    // Trigger note and check playing state
    auto midi = makeNoteOn(36, 100);
    float buf[kBlockSize * kChannels] = {};
    rack.process(buf, kBlockSize, kChannels, midi);
    // Pad finishes quickly (3 frames), should be done
    EXPECT_FALSE(rack.isPadPlaying(36));

    // With a longer sample it should still be playing
    std::vector<float> longSample(kBlockSize * 2, 0.5f);
    rack.loadPad(37, longSample.data(), kBlockSize * 2, 1);
    auto midi2 = makeNoteOn(37, 100);
    float buf2[kBlockSize * kChannels] = {};
    rack.process(buf2, kBlockSize, kChannels, midi2);
    EXPECT_TRUE(rack.isPadPlaying(37));

    // Out of range
    EXPECT_FALSE(rack.hasSample(-1));
    EXPECT_FALSE(rack.isPadPlaying(200));
}

TEST(DrumRack, PerPadParameterAPI) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    EXPECT_EQ(rack.parameterCount(), 4);

    // Global volume
    EXPECT_STREQ(rack.parameterInfo(0).name, "Volume");

    // Per-pad params operate on selected pad
    rack.setSelectedPad(60);

    // Default pad values
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadVolume), 1.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadPan), 0.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadPitch), 0.0f);

    // Set via parameter API
    rack.setParameter(DrumRack::kPadVolume, 1.5f);
    rack.setParameter(DrumRack::kPadPan, -0.5f);
    rack.setParameter(DrumRack::kPadPitch, 7.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadVolume), 1.5f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadPan), -0.5f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadPitch), 7.0f);

    // Verify it changed the correct pad's data
    EXPECT_FLOAT_EQ(rack.pad(60).volume, 1.5f);
    EXPECT_FLOAT_EQ(rack.pad(60).pan, -0.5f);
    EXPECT_FLOAT_EQ(rack.pad(60).pitchAdjust, 7.0f);

    // Other pad unchanged
    EXPECT_FLOAT_EQ(rack.pad(61).volume, 1.0f);

    // Switch pad — see different values
    rack.setSelectedPad(61);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadVolume), 1.0f);

    // Clamping
    rack.setParameter(DrumRack::kPadVolume, 5.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadVolume), 2.0f);
    rack.setParameter(DrumRack::kPadPitch, -30.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(DrumRack::kPadPitch), -24.0f);
}

TEST(DrumRack, PadSampleDataAccessors) {
    DrumRack rack;
    rack.init(kSampleRate, kBlockSize);

    EXPECT_EQ(rack.padSampleData(36), nullptr);
    EXPECT_EQ(rack.padSampleFrames(36), 0);
    EXPECT_EQ(rack.padSampleChannels(36), 1);

    float sample[] = {0.5f, 0.3f, 0.1f, -0.2f};
    rack.loadPad(36, sample, 2, 2); // 2 frames, stereo
    EXPECT_NE(rack.padSampleData(36), nullptr);
    EXPECT_EQ(rack.padSampleFrames(36), 2);
    EXPECT_EQ(rack.padSampleChannels(36), 2);

    // Out of range
    EXPECT_EQ(rack.padSampleData(-1), nullptr);
    EXPECT_EQ(rack.padSampleFrames(200), 0);
}

// ========================= Integration: Rack composability =========================

TEST(InstrumentRack, NestedRack) {
    // InstrumentRack containing another InstrumentRack
    auto inner = std::make_unique<InstrumentRack>();
    inner->addChain(std::make_unique<SubtractiveSynth>());

    InstrumentRack outer;
    outer.addChain(std::move(inner));
    outer.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    outer.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(InstrumentRack, RackWithDrumRack) {
    // InstrumentRack chain containing a DrumRack
    auto drums = std::make_unique<DrumRack>();
    std::vector<float> click(256, 0.5f);
    drums->loadPad(36, click.data(), 256, 1);

    InstrumentRack rack;
    rack.addChain(std::move(drums));
    rack.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(36, 100);
    rack.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(InstrumentRack, SelectedChainTracking) {
    InstrumentRack rack;
    EXPECT_EQ(rack.selectedChain(), 0);
    rack.setSelectedChain(3);
    EXPECT_EQ(rack.selectedChain(), 3);
    rack.setSelectedChain(-1);
    EXPECT_EQ(rack.selectedChain(), 0);
    rack.setSelectedChain(100);
    EXPECT_EQ(rack.selectedChain(), 7);
}

TEST(InstrumentRack, PerChainParameterAPI) {
    InstrumentRack rack;
    rack.init(kSampleRate, kBlockSize);

    auto synth1 = std::make_unique<SubtractiveSynth>();
    auto synth2 = std::make_unique<SubtractiveSynth>();
    rack.addChain(std::move(synth1), 0, 60);
    rack.addChain(std::move(synth2), 61, 127);

    EXPECT_EQ(rack.parameterCount(), 7);
    EXPECT_STREQ(rack.parameterInfo(InstrumentRack::kVolume).name, "Volume");
    EXPECT_STREQ(rack.parameterInfo(InstrumentRack::kChainVol).name, "Chain Vol");
    EXPECT_STREQ(rack.parameterInfo(InstrumentRack::kChainPan).name, "Chain Pan");
    EXPECT_STREQ(rack.parameterInfo(InstrumentRack::kChainKeyLow).name, "Key Low");

    // Select chain 0 and set params
    rack.setSelectedChain(0);
    rack.setParameter(InstrumentRack::kChainVol, 0.5f);
    rack.setParameter(InstrumentRack::kChainPan, -0.3f);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainVol), 0.5f);
    EXPECT_NEAR(rack.getParameter(InstrumentRack::kChainPan), -0.3f, 0.001f);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainKeyLow), 0.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainKeyHi), 60.0f);

    // Select chain 1 — params should reflect that chain
    rack.setSelectedChain(1);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainVol), 1.0f); // default
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainKeyLow), 61.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainKeyHi), 127.0f);

    // Setting key range via parameter API
    rack.setParameter(InstrumentRack::kChainVelLow, 10.0f);
    rack.setParameter(InstrumentRack::kChainVelHi, 100.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainVelLow), 10.0f);
    EXPECT_FLOAT_EQ(rack.getParameter(InstrumentRack::kChainVelHi), 100.0f);
}

TEST(InstrumentRack, ChainEnableDisable) {
    InstrumentRack rack;
    rack.init(kSampleRate, kBlockSize);

    auto synth = std::make_unique<SubtractiveSynth>();
    rack.addChain(std::move(synth));

    // Chain is enabled by default — should produce output
    float buf1[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(60, 100);
    rack.process(buf1, kBlockSize, kChannels, midi);
    float enabled_rms = rms(buf1, kBlockSize * kChannels);
    EXPECT_GT(enabled_rms, 0.001f);

    // Disable chain — should be silent
    rack.chain(0).enabled = false;
    float buf2[kBlockSize * kChannels] = {};
    rack.process(buf2, kBlockSize, kChannels, midi);
    float disabled_rms = rms(buf2, kBlockSize * kChannels);
    EXPECT_LT(disabled_rms, 0.001f);
}

// ========================= DrumSlop =========================

// Helper: generate a mono test loop (sine wave)
static std::vector<float> makeSineLoop(int frames = 44100, float freq = 440.0f) {
    std::vector<float> loop(frames);
    for (int i = 0; i < frames; ++i)
        loop[i] = std::sin(2.0 * M_PI * freq * i / kSampleRate);
    return loop;
}

TEST(DrumSlop, InitAndReset) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(ds.name(), "DrumSlop");
    EXPECT_STREQ(ds.id(), "drumslop");
    EXPECT_EQ(ds.parameterCount(), DrumSlop::kNumParams);
    ds.reset();
}

TEST(DrumSlop, SilentWithoutLoop) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(36, 100);
    ds.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 0.0001f);
}

TEST(DrumSlop, EvenSlicing) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1); // Even
    ds.setParameter(DrumSlop::kSliceCount, 4);
    ds.loadLoop(loop.data(), 44100, 1);

    EXPECT_EQ(ds.sliceCount(), 4);
    EXPECT_EQ(ds.sliceBoundary(0), 0);
    EXPECT_EQ(ds.sliceBoundary(1), 11025);
    EXPECT_EQ(ds.sliceBoundary(2), 22050);
    EXPECT_EQ(ds.sliceBoundary(3), 33075);
    EXPECT_EQ(ds.sliceBoundary(4), 44100);
}

TEST(DrumSlop, TriggerPadProducesOutput) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 4);
    ds.loadLoop(loop.data(), 44100, 1);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(36, 100);
    ds.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(DrumSlop, PadOutOfRangeIsSilent) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 4);
    ds.loadLoop(loop.data(), 44100, 1);

    // MIDI note 40 = pad 4 (only 4 slices)
    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(40, 100);
    ds.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 0.0001f);
}

TEST(DrumSlop, DifferentPadsPlayDifferentSlices) {
    DrumSlop ds;
    ds.init(kSampleRate, 4096);

    const int halfLen = 4096;
    std::vector<float> loop(halfLen * 2);
    for (int i = 0; i < halfLen; ++i)
        loop[i] = std::sin(2.0 * M_PI * 220.0 * i / kSampleRate);
    for (int i = 0; i < halfLen; ++i)
        loop[halfLen + i] = std::sin(2.0 * M_PI * 880.0 * i / kSampleRate);

    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 2);
    ds.loadLoop(loop.data(), halfLen * 2, 1);

    float buf0[4096 * kChannels] = {};
    auto midi0 = makeNoteOn(36, 100);
    ds.process(buf0, 4096, kChannels, midi0);

    ds.reset();
    float buf1[4096 * kChannels] = {};
    auto midi1 = makeNoteOn(37, 100);
    ds.process(buf1, 4096, kChannels, midi1);

    auto countZC = [](const float* buf, int n) {
        int zc = 0;
        for (int i = 2; i < n; i += 2)
            if ((buf[i-2] >= 0 && buf[i] < 0) || (buf[i-2] < 0 && buf[i] >= 0))
                ++zc;
        return zc;
    };
    EXPECT_GT(countZC(buf1, 4096 * kChannels), countZC(buf0, 4096 * kChannels) * 1.5);
}

TEST(DrumSlop, NoteOffReleasesEnvelope) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 4);
    ds.loadLoop(loop.data(), 44100, 1);

    ds.setPadRelease(0, 0.01f);

    float buffer[kBlockSize * kChannels];
    std::memset(buffer, 0, sizeof(buffer));
    auto noteOn = makeNoteOn(36, 100);
    ds.process(buffer, kBlockSize, kChannels, noteOn);
    float rmsOn = rms(buffer, kBlockSize * kChannels);

    auto noteOff = makeNoteOff(36);
    std::memset(buffer, 0, sizeof(buffer));
    ds.process(buffer, kBlockSize, kChannels, noteOff);

    for (int b = 0; b < 20; ++b) {
        std::memset(buffer, 0, sizeof(buffer));
        auto empty = makeEmpty();
        ds.process(buffer, kBlockSize, kChannels, empty);
    }
    float rmsAfter = rms(buffer, kBlockSize * kChannels);
    EXPECT_LT(rmsAfter, rmsOn * 0.1f);
}

TEST(DrumSlop, PadPitchAdjust) {
    DrumSlop ds;
    ds.init(kSampleRate, 4096);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 2);
    ds.loadLoop(loop.data(), 44100, 1);

    float buf1[4096 * kChannels] = {};
    auto midi1 = makeNoteOn(36, 100);
    ds.process(buf1, 4096, kChannels, midi1);

    ds.reset();
    ds.setPadPitch(0, 12.0f);
    float buf2[4096 * kChannels] = {};
    auto midi2 = makeNoteOn(36, 100);
    ds.process(buf2, 4096, kChannels, midi2);

    auto countZC = [](const float* buf, int n) {
        int zc = 0;
        for (int i = 2; i < n; i += 2)
            if ((buf[i-2] >= 0 && buf[i] < 0) || (buf[i-2] < 0 && buf[i] >= 0))
                ++zc;
        return zc;
    };
    EXPECT_GT(countZC(buf2, 4096 * kChannels), countZC(buf1, 4096 * kChannels));
}

TEST(DrumSlop, ConfigurableBaseNote) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 4);
    ds.loadLoop(loop.data(), 44100, 1);

    ds.setParameter(DrumSlop::kBaseNote, 60);

    // MIDI note 36 should now be silent
    float buf1[kBlockSize * kChannels] = {};
    auto midi1 = makeNoteOn(36, 100);
    ds.process(buf1, kBlockSize, kChannels, midi1);
    EXPECT_LT(rms(buf1, kBlockSize * kChannels), 0.0001f);

    // MIDI note 60 should produce output
    ds.reset();
    float buf2[kBlockSize * kChannels] = {};
    auto midi2 = makeNoteOn(60, 100);
    ds.process(buf2, kBlockSize, kChannels, midi2);
    EXPECT_GT(rms(buf2, kBlockSize * kChannels), 0.001f);
}

TEST(DrumSlop, AutoSlicingWithTransients) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    const int clickLen = 256;
    const int gapLen = 4096;
    const int numClicks = 8;
    int totalFrames = numClicks * (clickLen + gapLen);
    std::vector<float> loop(totalFrames, 0.0f);

    for (int c = 0; c < numClicks; ++c) {
        int start = c * (clickLen + gapLen);
        for (int i = 0; i < clickLen; ++i)
            loop[start + i] = std::sin(2.0 * M_PI * 1000.0 * i / kSampleRate)
                               * std::exp(-(float)i / 50.0f);
    }

    ds.setParameter(DrumSlop::kSliceMode, 0); // Auto
    ds.setParameter(DrumSlop::kSliceCount, 4);
    ds.loadLoop(loop.data(), totalFrames, 1);

    EXPECT_EQ(ds.sliceBoundary(0), 0);
    EXPECT_EQ(ds.sliceBoundary(4), totalFrames);
}

TEST(DrumSlop, SelectedPad) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    EXPECT_EQ(ds.selectedPad(), 0);
    ds.setSelectedPad(5);
    EXPECT_EQ(ds.selectedPad(), 5);
    ds.setSelectedPad(99);
    EXPECT_EQ(ds.selectedPad(), 15);
}

TEST(DrumSlop, ParameterGetSet) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    ds.setParameter(DrumSlop::kSliceCount, 12);
    EXPECT_EQ((int)ds.getParameter(DrumSlop::kSliceCount), 12);

    ds.setParameter(DrumSlop::kVolume, 0.5f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kVolume), 0.5f, 0.01f);

    ds.setParameter(DrumSlop::kBaseNote, 48);
    EXPECT_EQ((int)ds.getParameter(DrumSlop::kBaseNote), 48);
}

TEST(DrumSlop, PadADSR) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    auto loop = makeSineLoop(44100);
    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 2);
    ds.loadLoop(loop.data(), 44100, 1);

    ds.setPadAttack(0, 0.1f);
    ds.setPadSustain(0, 0.5f);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = makeNoteOn(36, 100);
    ds.process(buffer, kBlockSize, kChannels, midi);
    float rmsFirst = rms(buffer, kBlockSize * kChannels);

    for (int b = 0; b < 20; ++b) {
        std::memset(buffer, 0, sizeof(buffer));
        auto empty = makeEmpty();
        ds.process(buffer, kBlockSize, kChannels, empty);
    }
    float rmsLater = rms(buffer, kBlockSize * kChannels);
    EXPECT_GT(rmsLater, rmsFirst);
}

TEST(DrumSlop, ReversePadPlayback) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    const int loopLen = 4096;
    std::vector<float> loop(loopLen);
    for (int i = 0; i < loopLen; ++i)
        loop[i] = (float)i / loopLen;

    ds.setParameter(DrumSlop::kSliceMode, 1);
    ds.setParameter(DrumSlop::kSliceCount, 2);
    ds.loadLoop(loop.data(), loopLen, 1);

    float buf1[kBlockSize * kChannels] = {};
    auto midi1 = makeNoteOn(36, 100);
    ds.process(buf1, kBlockSize, kChannels, midi1);
    float firstFwd = std::abs(buf1[0]);

    ds.reset();
    ds.setPadReverse(0, true);
    float buf2[kBlockSize * kChannels] = {};
    auto midi2 = makeNoteOn(36, 100);
    ds.process(buf2, kBlockSize, kChannels, midi2);
    float firstRev = std::abs(buf2[0]);

    EXPECT_GT(firstRev, firstFwd);
}

TEST(DrumSlop, PerPadParameterAPI) {
    DrumSlop ds;
    ds.init(kSampleRate, kBlockSize);

    // Per-pad params read/write the selected pad
    ds.setSelectedPad(0);
    ds.setParameter(DrumSlop::kPadVolume, 0.7f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadVolume), 0.7f, 0.01f);

    ds.setParameter(DrumSlop::kPadPan, -0.5f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadPan), -0.5f, 0.01f);

    ds.setParameter(DrumSlop::kPadPitch, 7.0f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadPitch), 7.0f, 0.01f);

    ds.setParameter(DrumSlop::kPadReverse, 1.0f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadReverse), 1.0f, 0.01f);

    // Switch pad — values should be defaults (pad 3 untouched)
    ds.setSelectedPad(3);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadVolume), 1.0f, 0.01f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadPan), 0.0f, 0.01f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadReverse), 0.0f, 0.01f);

    // Modify pad 3 envelope
    ds.setParameter(DrumSlop::kPadAttack, 0.5f);
    ds.setParameter(DrumSlop::kPadSustain, 0.3f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadAttack), 0.5f, 0.01f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadSustain), 0.3f, 0.01f);

    // Switch back to pad 0 — should still have original values
    ds.setSelectedPad(0);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadVolume), 0.7f, 0.01f);
    EXPECT_NEAR(ds.getParameter(DrumSlop::kPadAttack), 0.001f, 0.01f);

    // isPerVoice flag is set on per-pad params
    EXPECT_TRUE(ds.parameterInfo(DrumSlop::kPadVolume).isPerVoice);
    EXPECT_FALSE(ds.parameterInfo(DrumSlop::kVolume).isPerVoice);
}

// ========================= KarplusStrong =========================

TEST(KarplusStrong, InitAndReset) {
    KarplusStrong ks;
    ks.init(kSampleRate, kBlockSize);
    EXPECT_EQ(ks.parameterCount(), KarplusStrong::kParamCount);
    EXPECT_STREQ(ks.name(), "Karplus-Strong");
    EXPECT_STREQ(ks.id(), "karplus");
    ks.reset();
}

TEST(KarplusStrong, NoteOnProducesOutput) {
    KarplusStrong ks;
    ks.init(kSampleRate, kBlockSize);

    // Process several blocks with note on to let the string ring
    float buf[kBlockSize * kChannels];
    auto noteOn = makeNoteOn(60, 100);
    std::memset(buf, 0, sizeof(buf));
    ks.process(buf, kBlockSize, kChannels, noteOn);

    // Second block should have signal from the ringing string
    float buf2[kBlockSize * kChannels];
    std::memset(buf2, 0, sizeof(buf2));
    ks.process(buf2, kBlockSize, kChannels, makeEmpty());

    float r = rms(buf2, kBlockSize * kChannels);
    EXPECT_GT(r, 1e-5f);
}

TEST(KarplusStrong, SilenceWhenNoNotes) {
    KarplusStrong ks;
    ks.init(kSampleRate, kBlockSize);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    ks.process(buf, kBlockSize, kChannels, makeEmpty());

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_NEAR(r, 0.0f, 1e-10f);
}

TEST(KarplusStrong, DifferentPitchesDiffer) {
    KarplusStrong ks;
    ks.init(kSampleRate, kBlockSize);

    // Note C4 (60)
    float buf1[kBlockSize * kChannels];
    std::memset(buf1, 0, sizeof(buf1));
    ks.process(buf1, kBlockSize, kChannels, makeNoteOn(60, 100));
    std::memset(buf1, 0, sizeof(buf1));
    ks.process(buf1, kBlockSize, kChannels, makeEmpty());

    ks.reset();

    // Note C5 (72)
    float buf2[kBlockSize * kChannels];
    std::memset(buf2, 0, sizeof(buf2));
    ks.process(buf2, kBlockSize, kChannels, makeNoteOn(72, 100));
    std::memset(buf2, 0, sizeof(buf2));
    ks.process(buf2, kBlockSize, kChannels, makeEmpty());

    // Outputs should differ (different frequencies)
    float diffSum = 0.0f;
    for (int i = 0; i < kBlockSize * kChannels; ++i)
        diffSum += std::abs(buf1[i] - buf2[i]);
    EXPECT_GT(diffSum, 0.01f);
}

TEST(KarplusStrong, NoteOffDecays) {
    KarplusStrong ks;
    ks.init(kSampleRate, kBlockSize);

    // Note on
    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    ks.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

    // Let it ring for a bit
    for (int b = 0; b < 4; ++b) {
        std::memset(buf, 0, sizeof(buf));
        ks.process(buf, kBlockSize, kChannels, makeEmpty());
    }
    float rmsBeforeOff = rms(buf, kBlockSize * kChannels);

    // Note off
    std::memset(buf, 0, sizeof(buf));
    ks.process(buf, kBlockSize, kChannels, makeNoteOff(60));

    // Wait for release
    for (int b = 0; b < 20; ++b) {
        std::memset(buf, 0, sizeof(buf));
        ks.process(buf, kBlockSize, kChannels, makeEmpty());
    }
    float rmsAfterRelease = rms(buf, kBlockSize * kChannels);

    EXPECT_LT(rmsAfterRelease, rmsBeforeOff);
}

TEST(KarplusStrong, AllExcitersProduceOutput) {
    for (int excType = 0; excType < 4; ++excType) {
        KarplusStrong ks;
        ks.init(kSampleRate, kBlockSize);
        ks.setParameter(KarplusStrong::kExciter, static_cast<float>(excType));

        float buf[kBlockSize * kChannels];
        std::memset(buf, 0, sizeof(buf));
        ks.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

        // Process another block
        std::memset(buf, 0, sizeof(buf));
        ks.process(buf, kBlockSize, kChannels, makeEmpty());

        float r = rms(buf, kBlockSize * kChannels);
        EXPECT_GT(r, 1e-6f) << "Exciter type " << excType << " produced silence";
    }
}

TEST(KarplusStrong, ParameterGetSet) {
    KarplusStrong ks;
    ks.init(kSampleRate, kBlockSize);

    ks.setParameter(KarplusStrong::kDamping, 0.6f);
    EXPECT_FLOAT_EQ(ks.getParameter(KarplusStrong::kDamping), 0.6f);

    ks.setParameter(KarplusStrong::kDecay, 0.95f);
    EXPECT_FLOAT_EQ(ks.getParameter(KarplusStrong::kDecay), 0.95f);

    // Defaults
    KarplusStrong ks2;
    ks2.init(kSampleRate, kBlockSize);
    EXPECT_FLOAT_EQ(ks2.getParameter(KarplusStrong::kVolume),
                    ks2.parameterInfo(KarplusStrong::kVolume).defaultValue);
}

// ========================= WavetableSynth =========================

TEST(WavetableSynth, InitAndReset) {
    WavetableSynth wt;
    wt.init(kSampleRate, kBlockSize);
    EXPECT_EQ(wt.parameterCount(), WavetableSynth::kParamCount);
    EXPECT_STREQ(wt.name(), "Wavetable Synth");
    EXPECT_STREQ(wt.id(), "wavetable");
    wt.reset();
}

TEST(WavetableSynth, NoteOnProducesOutput) {
    WavetableSynth wt;
    wt.init(kSampleRate, kBlockSize);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    wt.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_GT(r, 1e-5f);
}

TEST(WavetableSynth, SilenceWhenNoNotes) {
    WavetableSynth wt;
    wt.init(kSampleRate, kBlockSize);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    wt.process(buf, kBlockSize, kChannels, makeEmpty());

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_NEAR(r, 0.0f, 1e-10f);
}

TEST(WavetableSynth, AllTablesProduceOutput) {
    for (int t = 0; t < WavetableSynth::kNumTables; ++t) {
        WavetableSynth wt;
        wt.init(kSampleRate, kBlockSize);
        wt.setParameter(WavetableSynth::kTable, static_cast<float>(t));

        float buf[kBlockSize * kChannels];
        std::memset(buf, 0, sizeof(buf));
        wt.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

        float r = rms(buf, kBlockSize * kChannels);
        EXPECT_GT(r, 1e-6f) << "Table " << t << " produced silence";
    }
}

TEST(WavetableSynth, PositionMorphsSound) {
    WavetableSynth wt1, wt2;
    wt1.init(kSampleRate, kBlockSize);
    wt2.init(kSampleRate, kBlockSize);

    wt1.setParameter(WavetableSynth::kPosition, 0.0f);
    wt2.setParameter(WavetableSynth::kPosition, 1.0f);

    float buf1[kBlockSize * kChannels], buf2[kBlockSize * kChannels];
    std::memset(buf1, 0, sizeof(buf1));
    std::memset(buf2, 0, sizeof(buf2));
    wt1.process(buf1, kBlockSize, kChannels, makeNoteOn(60, 100));
    wt2.process(buf2, kBlockSize, kChannels, makeNoteOn(60, 100));

    float diffSum = 0.0f;
    for (int i = 0; i < kBlockSize * kChannels; ++i)
        diffSum += std::abs(buf1[i] - buf2[i]);
    EXPECT_GT(diffSum, 0.01f);
}

TEST(WavetableSynth, ParameterGetSet) {
    WavetableSynth wt;
    wt.init(kSampleRate, kBlockSize);

    wt.setParameter(WavetableSynth::kPosition, 0.5f);
    EXPECT_FLOAT_EQ(wt.getParameter(WavetableSynth::kPosition), 0.5f);

    const float wtCutNorm = WavetableSynth::cutoffHzToNorm(2000.0f);
    wt.setParameter(WavetableSynth::kFilterCutoff, wtCutNorm);
    EXPECT_NEAR(wt.getParameter(WavetableSynth::kFilterCutoff), wtCutNorm, 0.001f);

    WavetableSynth wt2;
    wt2.init(kSampleRate, kBlockSize);
    EXPECT_FLOAT_EQ(wt2.getParameter(WavetableSynth::kVolume),
                    wt2.parameterInfo(WavetableSynth::kVolume).defaultValue);
}

// ─── GranularSynth Tests ─────────────────────────────────────

static std::vector<float> makeTestSample(int frames, float freq = 440.0f,
                                          double sr = 44100.0) {
    std::vector<float> data(frames);
    for (int i = 0; i < frames; ++i)
        data[i] = std::sin(2.0 * 3.14159265 * freq * i / sr);
    return data;
}

TEST(GranularSynth, InitAndReset) {
    GranularSynth gs;
    gs.init(kSampleRate, kBlockSize);
    EXPECT_EQ(gs.parameterCount(), GranularSynth::kParamCount);
    EXPECT_STREQ(gs.name(), "Granular Synth");
    EXPECT_STREQ(gs.id(), "granular");
    EXPECT_FALSE(gs.hasSample());
    gs.reset();
}

TEST(GranularSynth, SilenceWithoutSample) {
    GranularSynth gs;
    gs.init(kSampleRate, kBlockSize);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    gs.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_NEAR(r, 0.0f, 1e-10f);
}

TEST(GranularSynth, ProducesOutputWithSample) {
    GranularSynth gs;
    gs.init(kSampleRate, kBlockSize);

    auto sample = makeTestSample(44100);
    gs.loadSample(sample.data(), 44100, 1);
    EXPECT_TRUE(gs.hasSample());

    // Set fast density and short attack for immediate output
    gs.setParameter(GranularSynth::kDensity, 40.0f);
    gs.setParameter(GranularSynth::kAttack, 1.0f);
    gs.setParameter(GranularSynth::kGrainSize, 50.0f);

    // Process several blocks to allow grains to spawn and ramp up
    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 8; ++i) {
        auto midi = (i == 0) ? makeNoteOn(60, 100) : makeEmpty();
        gs.process(buf, kBlockSize, kChannels, midi);
    }

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_GT(r, 1e-6f);
}

TEST(GranularSynth, SilenceWhenNoNotes) {
    GranularSynth gs;
    gs.init(kSampleRate, kBlockSize);

    auto sample = makeTestSample(44100);
    gs.loadSample(sample.data(), 44100, 1);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    gs.process(buf, kBlockSize, kChannels, makeEmpty());

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_NEAR(r, 0.0f, 1e-10f);
}

TEST(GranularSynth, DifferentShapesProduceOutput) {
    auto sample = makeTestSample(44100);
    for (int shape = 0; shape < 4; ++shape) {
        GranularSynth gs;
        gs.init(kSampleRate, kBlockSize);
        gs.loadSample(sample.data(), 44100, 1);
        gs.setParameter(GranularSynth::kShape, static_cast<float>(shape));
        gs.setParameter(GranularSynth::kDensity, 40.0f);
        gs.setParameter(GranularSynth::kAttack, 1.0f);

        float buf[kBlockSize * kChannels];
        std::memset(buf, 0, sizeof(buf));
        for (int i = 0; i < 8; ++i) {
            auto midi = (i == 0) ? makeNoteOn(60, 100) : makeEmpty();
            gs.process(buf, kBlockSize, kChannels, midi);
        }
        float r = rms(buf, kBlockSize * kChannels);
        EXPECT_GT(r, 1e-7f) << "Shape " << shape << " produced silence";
    }
}

TEST(GranularSynth, ParameterGetSet) {
    GranularSynth gs;
    gs.init(kSampleRate, kBlockSize);

    gs.setParameter(GranularSynth::kPosition, 0.7f);
    EXPECT_FLOAT_EQ(gs.getParameter(GranularSynth::kPosition), 0.7f);

    gs.setParameter(GranularSynth::kGrainSize, 200.0f);
    EXPECT_FLOAT_EQ(gs.getParameter(GranularSynth::kGrainSize), 200.0f);

    const float gsCutNorm = GranularSynth::cutoffHzToNorm(5000.0f);
    gs.setParameter(GranularSynth::kFilterCutoff, gsCutNorm);
    EXPECT_NEAR(gs.getParameter(GranularSynth::kFilterCutoff), gsCutNorm, 0.001f);

    // Out-of-range clamped
    gs.setParameter(GranularSynth::kVolume, 5.0f);
    EXPECT_FLOAT_EQ(gs.getParameter(GranularSynth::kVolume), 1.0f);
}

TEST(GranularSynth, StereoSample) {
    // Create stereo test sample
    std::vector<float> stereo(44100 * 2);
    for (int i = 0; i < 44100; ++i) {
        float s = std::sin(2.0 * 3.14159265 * 440.0 * i / 44100.0);
        stereo[i * 2]     = s;
        stereo[i * 2 + 1] = s * 0.5f;
    }

    GranularSynth gs;
    gs.init(kSampleRate, kBlockSize);
    gs.loadSample(stereo.data(), 44100, 2);
    EXPECT_EQ(gs.sampleChannels(), 2);
    gs.setParameter(GranularSynth::kDensity, 40.0f);
    gs.setParameter(GranularSynth::kAttack, 1.0f);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 8; ++i) {
        auto midi = (i == 0) ? makeNoteOn(60, 100) : makeEmpty();
        gs.process(buf, kBlockSize, kChannels, midi);
    }
    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_GT(r, 1e-7f);
}

// ─── Vocoder Tests ─────────────────────────────────────

TEST(Vocoder, InitAndReset) {
    Vocoder vc;
    vc.init(kSampleRate, kBlockSize);
    EXPECT_EQ(vc.parameterCount(), Vocoder::kParamCount);
    EXPECT_STREQ(vc.name(), "Vocoder");
    EXPECT_STREQ(vc.id(), "vocoder");
    EXPECT_FALSE(vc.hasModulatorSample());
    vc.reset();
}

TEST(Vocoder, NoteOnProducesOutput) {
    Vocoder vc;
    vc.init(kSampleRate, kBlockSize);
    // Use internal formant modulator (default modSource = 1)
    vc.setParameter(Vocoder::kAmpAttack, 1.0f);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 4; ++i) {
        auto midi = (i == 0) ? makeNoteOn(60, 100) : makeEmpty();
        vc.process(buf, kBlockSize, kChannels, midi);
    }
    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_GT(r, 1e-6f);
}

TEST(Vocoder, SilenceWhenNoNotes) {
    Vocoder vc;
    vc.init(kSampleRate, kBlockSize);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    vc.process(buf, kBlockSize, kChannels, makeEmpty());

    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_NEAR(r, 0.0f, 1e-10f);
}

TEST(Vocoder, AllCarrierTypesProduceOutput) {
    for (int ct = 0; ct < 4; ++ct) {
        Vocoder vc;
        vc.init(kSampleRate, kBlockSize);
        vc.setParameter(Vocoder::kCarrierType, static_cast<float>(ct));
        vc.setParameter(Vocoder::kAmpAttack, 1.0f);

        float buf[kBlockSize * kChannels];
        std::memset(buf, 0, sizeof(buf));
        for (int i = 0; i < 4; ++i) {
            auto midi = (i == 0) ? makeNoteOn(60, 100) : makeEmpty();
            vc.process(buf, kBlockSize, kChannels, midi);
        }
        float r = rms(buf, kBlockSize * kChannels);
        EXPECT_GT(r, 1e-7f) << "Carrier type " << ct << " produced silence";
    }
}

TEST(Vocoder, WithModulatorSample) {
    Vocoder vc;
    vc.init(kSampleRate, kBlockSize);

    // Load a test sample as modulator
    std::vector<float> mod(44100);
    for (int i = 0; i < 44100; ++i)
        mod[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / 44100.0);
    vc.loadModulatorSample(mod.data(), 44100, 1);
    EXPECT_TRUE(vc.hasModulatorSample());

    vc.setParameter(Vocoder::kModSource, 0.0f); // Use sample
    vc.setParameter(Vocoder::kAmpAttack, 1.0f);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 4; ++i) {
        auto midi = (i == 0) ? makeNoteOn(60, 100) : makeEmpty();
        vc.process(buf, kBlockSize, kChannels, midi);
    }
    float r = rms(buf, kBlockSize * kChannels);
    EXPECT_GT(r, 1e-7f);
}

TEST(Vocoder, ParameterGetSet) {
    Vocoder vc;
    vc.init(kSampleRate, kBlockSize);

    vc.setParameter(Vocoder::kBands, 24.0f);
    EXPECT_FLOAT_EQ(vc.getParameter(Vocoder::kBands), 24.0f);

    vc.setParameter(Vocoder::kBandwidth, 1.5f);
    EXPECT_FLOAT_EQ(vc.getParameter(Vocoder::kBandwidth), 1.5f);

    // Clamp
    vc.setParameter(Vocoder::kVolume, 5.0f);
    EXPECT_FLOAT_EQ(vc.getParameter(Vocoder::kVolume), 1.0f);

    vc.setParameter(Vocoder::kFormantShift, -20.0f);
    EXPECT_FLOAT_EQ(vc.getParameter(Vocoder::kFormantShift), -12.0f);
}

// ─── Multisampler Tests ─────────────────────────────────────

static std::vector<float> makeSineSample(int frames, float freq = 440.0f,
                                          double sr = 44100.0) {
    std::vector<float> data(frames);
    for (int i = 0; i < frames; ++i)
        data[i] = std::sin(2.0 * 3.14159265 * freq * i / sr);
    return data;
}

TEST(Multisampler, InitAndReset) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);
    EXPECT_EQ(ms.parameterCount(), Multisampler::kParamCount);
    EXPECT_STREQ(ms.name(), "Multisampler");
    EXPECT_STREQ(ms.id(), "multisampler");
    EXPECT_EQ(ms.zoneCount(), 0);
    ms.reset();
}

TEST(Multisampler, SilenceWithoutZones) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    ms.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

    EXPECT_NEAR(rms(buf, kBlockSize * kChannels), 0.0f, 1e-10f);
}

TEST(Multisampler, SingleZonePlayback) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    auto sample = makeSineSample(44100, 440.0f);
    int idx = ms.addZone(sample.data(), 44100, 1, 60, 0, 127);
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(ms.zoneCount(), 1);

    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    ms.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));

    EXPECT_GT(rms(buf, kBlockSize * kChannels), 1e-5f);
}

TEST(Multisampler, MultipleZonesKeyMapping) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    // Low zone: C2-B3 (36-59)
    auto lowSample = makeSineSample(44100, 220.0f);
    ms.addZone(lowSample.data(), 44100, 1, 48, 36, 59);

    // High zone: C4-C6 (60-84)
    auto highSample = makeSineSample(44100, 880.0f);
    ms.addZone(highSample.data(), 44100, 1, 72, 60, 84);

    EXPECT_EQ(ms.zoneCount(), 2);

    // Play note in low zone
    float buf1[kBlockSize * kChannels];
    std::memset(buf1, 0, sizeof(buf1));
    ms.process(buf1, kBlockSize, kChannels, makeNoteOn(48, 100));
    float rms1 = rms(buf1, kBlockSize * kChannels);

    // Reset and play note in high zone
    ms.reset();
    float buf2[kBlockSize * kChannels];
    std::memset(buf2, 0, sizeof(buf2));
    ms.process(buf2, kBlockSize, kChannels, makeNoteOn(72, 100));
    float rms2 = rms(buf2, kBlockSize * kChannels);

    EXPECT_GT(rms1, 1e-5f);
    EXPECT_GT(rms2, 1e-5f);
}

TEST(Multisampler, VelocityLayers) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    auto softSample = makeSineSample(44100, 440.0f);
    ms.addZone(softSample.data(), 44100, 1, 60, 0, 127, 0, 63);

    auto loudSample = makeSineSample(44100, 880.0f);
    ms.addZone(loudSample.data(), 44100, 1, 60, 0, 127, 64, 127);

    // Soft velocity (vel7 ~= 40)
    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    auto softNote = makeNoteOn(60, 40); // vel7=40 maps to low layer
    ms.process(buf, kBlockSize, kChannels, softNote);
    EXPECT_GT(rms(buf, kBlockSize * kChannels), 1e-6f);
}

TEST(Multisampler, NoteOffRelease) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    auto sample = makeSineSample(44100, 440.0f);
    ms.addZone(sample.data(), 44100, 1, 60, 0, 127);
    ms.setParameter(Multisampler::kAmpRelease, 0.001f);

    // Note on
    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    ms.process(buf, kBlockSize, kChannels, makeNoteOn(60, 100));
    EXPECT_GT(rms(buf, kBlockSize * kChannels), 1e-5f);

    // Note off + process several blocks for release
    std::memset(buf, 0, sizeof(buf));
    ms.process(buf, kBlockSize, kChannels, makeNoteOff(60));
    // Process more blocks until voice dies
    for (int i = 0; i < 20; ++i) {
        std::memset(buf, 0, sizeof(buf));
        ms.process(buf, kBlockSize, kChannels, makeEmpty());
    }
    // After very short release, should be silent
    EXPECT_LT(rms(buf, kBlockSize * kChannels), 0.01f);
}

TEST(Multisampler, ParameterGetSet) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    ms.setParameter(Multisampler::kAmpAttack, 0.5f);
    EXPECT_FLOAT_EQ(ms.getParameter(Multisampler::kAmpAttack), 0.5f);

    const float msCutNorm = Multisampler::cutoffHzToNorm(5000.0f);
    ms.setParameter(Multisampler::kFilterCutoff, msCutNorm);
    EXPECT_NEAR(ms.getParameter(Multisampler::kFilterCutoff), msCutNorm, 0.001f);

    // Clamp
    ms.setParameter(Multisampler::kVolume, 5.0f);
    EXPECT_FLOAT_EQ(ms.getParameter(Multisampler::kVolume), 1.0f);
}

TEST(Multisampler, ClearZones) {
    Multisampler ms;
    ms.init(kSampleRate, kBlockSize);

    auto sample = makeSineSample(44100);
    ms.addZone(sample.data(), 44100, 1, 60, 0, 127);
    EXPECT_EQ(ms.zoneCount(), 1);

    ms.clearZones();
    EXPECT_EQ(ms.zoneCount(), 0);
}

// ========================= DrumSynth =========================
//
// DrumSynth is a fully-synthesised 8-piece kit — these tests cover
// the contract surface (init / GM-note → slot mapping / NoteOn fires
// audio / per-pad monophony / CHH→OHH choke / CC 123 silence) plus
// a handful of regressions caught during the v0.52.x review pass:
// uint32_t pink-counter overflow safety, justTriggered semantics
// (no float-equal sentinel), oversized-block defensive return, and
// member-owned scratch buffers.

namespace {

MidiBuffer makeDrumNote(int note, uint8_t vel7 = 100) {
    MidiBuffer buf;
    MidiMessage m{};
    m.type = MidiMessage::Type::NoteOn;
    m.channel = 0;
    m.note = static_cast<uint8_t>(note);
    m.velocity = Convert::vel7to16(vel7);
    buf.addMessage(m);
    return buf;
}

MidiBuffer makeAllNotesOff() {
    MidiBuffer buf;
    buf.addMessage(MidiMessage::cc(0, 123, 0));
    return buf;
}

}  // anon

TEST(DrumSynth, SlotForNoteMapsGMNotes) {
    EXPECT_EQ(DrumSynth::slotForNote(36), DrumSynth::Kick);
    EXPECT_EQ(DrumSynth::slotForNote(38), DrumSynth::Snare);
    EXPECT_EQ(DrumSynth::slotForNote(39), DrumSynth::Clap);
    EXPECT_EQ(DrumSynth::slotForNote(41), DrumSynth::Tom1);
    EXPECT_EQ(DrumSynth::slotForNote(42), DrumSynth::ClosedHH);
    EXPECT_EQ(DrumSynth::slotForNote(46), DrumSynth::OpenHH);
    EXPECT_EQ(DrumSynth::slotForNote(50), DrumSynth::Tom2);
    EXPECT_EQ(DrumSynth::slotForNote(54), DrumSynth::Tambourine);
    // Out-of-kit notes return -1.
    EXPECT_EQ(DrumSynth::slotForNote(60), -1);
    EXPECT_EQ(DrumSynth::slotForNote(0),  -1);
    EXPECT_EQ(DrumSynth::slotForNote(127), -1);
}

TEST(DrumSynth, InitAndReset) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);
    EXPECT_EQ(ds.parameterCount(), DrumSynth::kParamCount);
    // Defaults are in-range — sanity-check a few.
    EXPECT_GE(ds.getParameter(DrumSynth::pKickTune), 0.0f);
    EXPECT_LE(ds.getParameter(DrumSynth::pKickTune), 1.0f);
    ds.reset();
    // After reset, an empty MIDI buffer should produce silence.
    float buffer[kBlockSize * kChannels] = {};
    ds.process(buffer, kBlockSize, kChannels, makeEmpty());
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 1e-6f);
}

TEST(DrumSynth, KickProducesAudibleOutput) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);
    float buffer[kBlockSize * kChannels] = {};
    ds.process(buffer, kBlockSize, kChannels, makeDrumNote(36 /*Kick*/));
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 1e-3f);
}

TEST(DrumSynth, EachSlotProducesOutput) {
    // Fire every drum from a fresh instance and check non-zero RMS.
    // Catches "I added a slot but forgot to wire its render switch
    // case" and similar regressions.
    constexpr int kDrumNotes[] = { 36, 38, 39, 41, 42, 46, 50, 54 };
    for (int note : kDrumNotes) {
        DrumSynth ds;
        ds.init(kSampleRate, kBlockSize);
        float buffer[kBlockSize * kChannels] = {};
        ds.process(buffer, kBlockSize, kChannels, makeDrumNote(note));
        EXPECT_GT(rms(buffer, kBlockSize * kChannels), 1e-4f)
            << "note " << note << " produced silence";
    }
}

TEST(DrumSynth, NonKitNotesAreIgnored) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);
    float buffer[kBlockSize * kChannels] = {};
    // C4 — outside the kit — should not trigger anything.
    ds.process(buffer, kBlockSize, kChannels, makeDrumNote(60));
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 1e-6f);
}

TEST(DrumSynth, ClosedHHChokesOpenHH) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);

    // Trigger the open hi-hat — it has a long decay (350 ms default).
    float buf1[kBlockSize * kChannels] = {};
    ds.process(buf1, kBlockSize, kChannels, makeDrumNote(46 /*OpenHH*/));
    const float openRms = rms(buf1, kBlockSize * kChannels);
    EXPECT_GT(openRms, 1e-3f);

    // Now hit the closed hi-hat. The very next block should have
    // ONLY the closed-hat output (open-hat voice silenced by choke).
    // We compare the block AFTER closed-hat trigger to a baseline
    // open-hat-still-ringing block.
    DrumSynth ds2;
    ds2.init(kSampleRate, kBlockSize);
    float baseline[kBlockSize * kChannels] = {};
    ds2.process(baseline, kBlockSize, kChannels, makeDrumNote(46));
    // Continue letting open hat ring without choke (control case)…
    float ringOn[kBlockSize * kChannels] = {};
    ds2.process(ringOn, kBlockSize, kChannels, makeEmpty());
    const float ringRms = rms(ringOn, kBlockSize * kChannels);

    // …vs choke case.
    float chokedAndCHH[kBlockSize * kChannels] = {};
    ds.process(chokedAndCHH, kBlockSize, kChannels, makeDrumNote(42 /*ClosedHH*/));
    // After CHH triggers, the OPEN voice is killed. The audible
    // output is just the CHH (60 ms decay) by the end of the block,
    // not OHH (350 ms decay) ringing. Hard to assert exactly, so we
    // verify open-hat alone (block N+1, no new triggers) is silent
    // because the choke wiped the active flag.
    float afterChoke[kBlockSize * kChannels] = {};
    ds.process(afterChoke, kBlockSize, kChannels, makeEmpty());
    // The CHH itself is short (60 ms = ~2640 samples @ 44.1 kHz)
    // → it's still ringing in `afterChoke` slightly. But strictly
    // less than the un-choked open-hat ringRms.
    EXPECT_LT(rms(afterChoke, kBlockSize * kChannels), ringRms);
}

TEST(DrumSynth, OpenHHDoesNotChokeClosedHH) {
    // We dropped the OpenHH→ClosedHH choke direction in v0.52 — real
    // hi-hat pedals only choke one way (closing mutes open). This
    // test pins that behaviour so it doesn't drift back.
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);

    // Trigger the closed hat first, then the open hat. The closed
    // voice should stay active afterwards (its short envelope will
    // decay naturally — but its `active` flag isn't externally
    // observable, so we assert audible behaviour via RMS).
    float b1[kBlockSize * kChannels] = {};
    ds.process(b1, kBlockSize, kChannels, makeDrumNote(42 /*CHH*/));
    EXPECT_GT(rms(b1, kBlockSize * kChannels), 1e-4f);
    float b2[kBlockSize * kChannels] = {};
    ds.process(b2, kBlockSize, kChannels, makeDrumNote(46 /*OHH*/));
    // Both voices alive in this block — sum is bigger than either
    // alone would be (loose bound; mostly we want non-zero output).
    EXPECT_GT(rms(b2, kBlockSize * kChannels), 1e-4f);
}

TEST(DrumSynth, AllNotesOffSilencesEverything) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);

    // Fire kick + snare + open-hat — all have long-ish tails.
    float buf[kBlockSize * kChannels] = {};
    MidiBuffer chord;
    MidiMessage m{};
    m.type = MidiMessage::Type::NoteOn;
    m.channel = 0;
    m.velocity = Convert::vel7to16(120);
    for (int note : {36, 38, 46}) {
        m.note = static_cast<uint8_t>(note);
        chord.addMessage(m);
    }
    ds.process(buf, kBlockSize, kChannels, chord);
    EXPECT_GT(rms(buf, kBlockSize * kChannels), 1e-3f);

    // CC 123 — All Notes Off — should silence everything immediately.
    float silenceBuf[kBlockSize * kChannels] = {};
    ds.process(silenceBuf, kBlockSize, kChannels, makeAllNotesOff());
    EXPECT_LT(rms(silenceBuf, kBlockSize * kChannels), 1e-6f);
}

TEST(DrumSynth, RetriggerResetsEnvelope) {
    // A fresh trigger on an already-decaying voice should restart
    // the envelope from 0, not just keep the previous level. This
    // is the behaviour every drum machine ever has shipped, and
    // it's a regression-magnet because justTriggered + ageSamples
    // both need to be reset in noteOn.
    //
    // process() is ADDITIVE — we explicitly zero the buffer between
    // calls so the per-block RMS reflects just that block's output.
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);

    // Fire kick, then run silent blocks until the envelope is well
    // below its peak. Default kick decay τ = 350 ms; we wait
    // ~3 time-constants (≈1 s, ~170 blocks of 256 @ 44.1 kHz) so the
    // tail is ~5 % of peak — gives a clean ratio against the
    // re-triggered block.
    float buf[kBlockSize * kChannels];
    std::memset(buf, 0, sizeof(buf));
    ds.process(buf, kBlockSize, kChannels, makeDrumNote(36));
    for (int i = 0; i < 170; ++i) {
        std::memset(buf, 0, sizeof(buf));
        ds.process(buf, kBlockSize, kChannels, makeEmpty());
    }
    // `buf` now holds the kick's far-decayed tail — capture it.
    const float tailRms = rms(buf, kBlockSize * kChannels);

    // Retrigger and read the next block's RMS in isolation.
    std::memset(buf, 0, sizeof(buf));
    ds.process(buf, kBlockSize, kChannels, makeDrumNote(36));
    const float retrigRms = rms(buf, kBlockSize * kChannels);

    // The retriggered block should be loudly back at the attack/
    // early-decay level — at least 5× the decayed tail.
    EXPECT_GT(retrigRms, tailRms * 5.0f);
}

TEST(DrumSynth, OversizedBlockReturnsCleanly) {
    // Defence: host violates its own m_maxBlockSize contract by
    // sending a bigger block. Code should NOT write past the
    // member-owned scratch buffer; should just return early.
    // Check by size-pinning init() to 64 then handing it 256.
    DrumSynth ds;
    ds.init(kSampleRate, /*maxBlockSize=*/64);
    float buf[kBlockSize * kChannels] = {};
    // No crash → test passes. We don't assert RMS here because
    // the contract violation drops the block (and silence is the
    // correct outcome).
    ds.process(buf, kBlockSize, kChannels, makeDrumNote(36));
    SUCCEED();
}

TEST(DrumSynth, BypassedProducesNoOutput) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);
    ds.setBypassed(true);
    float buf[kBlockSize * kChannels] = {};
    ds.process(buf, kBlockSize, kChannels, makeDrumNote(36));
    EXPECT_LT(rms(buf, kBlockSize * kChannels), 1e-6f);
}

TEST(DrumSynth, ParameterClampingAndInfo) {
    DrumSynth ds;
    ds.init(kSampleRate, kBlockSize);
    // Out-of-range write gets clamped to the parameterInfo range.
    const auto& kickTune = ds.parameterInfo(DrumSynth::pKickTune);
    ds.setParameter(DrumSynth::pKickTune, 99.0f);
    EXPECT_FLOAT_EQ(ds.getParameter(DrumSynth::pKickTune), kickTune.maxValue);
    ds.setParameter(DrumSynth::pKickTune, -99.0f);
    EXPECT_FLOAT_EQ(ds.getParameter(DrumSynth::pKickTune), kickTune.minValue);
    // Out-of-range index is a no-op (no crash).
    ds.setParameter(-1, 0.5f);
    ds.setParameter(DrumSynth::kParamCount + 1, 0.5f);
    EXPECT_EQ(ds.getParameter(-1), 0.0f);
    EXPECT_EQ(ds.getParameter(DrumSynth::kParamCount + 1), 0.0f);
}
