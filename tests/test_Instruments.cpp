#include <gtest/gtest.h>
#include "instruments/Envelope.h"
#include "instruments/Oscillator.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/InstrumentRack.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
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
    synth.setParameter(SubtractiveSynth::kFilterCutoff, 1000.0f);
    EXPECT_NEAR(synth.getParameter(SubtractiveSynth::kFilterCutoff), 1000.0f, 1.0f);
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

    wt.setParameter(WavetableSynth::kFilterCutoff, 2000.0f);
    EXPECT_FLOAT_EQ(wt.getParameter(WavetableSynth::kFilterCutoff), 2000.0f);

    WavetableSynth wt2;
    wt2.init(kSampleRate, kBlockSize);
    EXPECT_FLOAT_EQ(wt2.getParameter(WavetableSynth::kVolume),
                    wt2.parameterInfo(WavetableSynth::kVolume).defaultValue);
}
