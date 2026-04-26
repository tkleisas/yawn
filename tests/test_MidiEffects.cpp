#include "midi/MidiEffect.h"
#include "midi/MidiEffectChain.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include <gtest/gtest.h>
#include <memory>

using namespace yawn::midi;

static constexpr double kSR = 44100.0;

static TransportInfo makeTransport(double bpm = 120.0, double posBeats = 0.0,
                                   bool playing = true) {
    TransportInfo t;
    t.bpm = bpm;
    t.sampleRate = kSR;
    t.positionInBeats = posBeats;
    t.positionInSamples = (int64_t)(posBeats * kSR * 60.0 / bpm);
    t.samplesPerBeat = kSR * 60.0 / bpm;
    t.playing = playing;
    t.beatsPerBar = 4;
    return t;
}

// ---------- MidiEffectChain ----------

TEST(MidiEffectChain, AddAndRemove) {
    MidiEffectChain chain;
    chain.init(kSR);
    EXPECT_EQ(chain.count(), 0);

    chain.addEffect(std::make_unique<MidiPitch>());
    EXPECT_EQ(chain.count(), 1);
    EXPECT_STREQ(chain.effect(0)->id(), "pitch");

    auto removed = chain.removeEffect(0);
    EXPECT_EQ(chain.count(), 0);
    EXPECT_NE(removed, nullptr);
}

TEST(MidiEffectChain, ProcessOrder) {
    MidiEffectChain chain;
    chain.init(kSR);

    // Pitch +12, then Pitch +7 = total +19
    auto p1 = std::make_unique<MidiPitch>();
    p1->setParameter(MidiPitch::kSemitones, 12.0f);
    auto p2 = std::make_unique<MidiPitch>();
    p2->setParameter(MidiPitch::kSemitones, 7.0f);
    chain.addEffect(std::move(p1));
    chain.addEffect(std::move(p2));

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    chain.process(buf, 256, makeTransport());

    EXPECT_EQ(buf.count(), 1);
    EXPECT_EQ(buf[0].note, 79); // 60 + 19
}

TEST(MidiEffectChain, BypassSkipsEffect) {
    MidiEffectChain chain;
    chain.init(kSR);

    auto p = std::make_unique<MidiPitch>();
    p->setParameter(MidiPitch::kSemitones, 12.0f);
    p->setBypassed(true);
    chain.addEffect(std::move(p));

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    chain.process(buf, 256, makeTransport());

    EXPECT_EQ(buf[0].note, 60); // Bypassed — no change
}

// ---------- MidiPitch ----------

TEST(MidiPitch, TransposeSemitones) {
    MidiPitch pitch;
    pitch.init(kSR);
    pitch.setParameter(MidiPitch::kSemitones, 5.0f);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    buf.addMessage(MidiMessage::noteOff(0, 60, 0, 10));
    pitch.process(buf, 256, makeTransport());

    EXPECT_EQ(buf[0].note, 65);
    EXPECT_EQ(buf[1].note, 65);
}

TEST(MidiPitch, TransposeOctave) {
    MidiPitch pitch;
    pitch.init(kSR);
    pitch.setParameter(MidiPitch::kOctave, -1.0f);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    pitch.process(buf, 256, makeTransport());

    EXPECT_EQ(buf[0].note, 48);
}

TEST(MidiPitch, OutOfRangeSilences) {
    MidiPitch pitch;
    pitch.init(kSR);
    pitch.setParameter(MidiPitch::kSemitones, 48.0f);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 100, 100, 0)); // 100+48 > 127
    pitch.process(buf, 256, makeTransport());

    EXPECT_EQ(buf[0].velocity, 0); // Silenced
}

TEST(MidiPitch, ZeroOffsetNoChange) {
    MidiPitch pitch;
    pitch.init(kSR);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    pitch.process(buf, 256, makeTransport());

    EXPECT_EQ(buf[0].note, 60);
}

// ---------- Chord ----------

TEST(Chord, AddsIntervals) {
    Chord chord;
    chord.init(kSR);
    chord.setParameter(Chord::kInterval1, 4.0f);  // Major third
    chord.setParameter(Chord::kInterval2, 7.0f);  // Perfect fifth

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    chord.process(buf, 256, makeTransport());

    EXPECT_EQ(buf.count(), 3);
    // Original + 2 chord tones (sorted by frame, all frame 0)
    bool has60 = false, has64 = false, has67 = false;
    for (int i = 0; i < buf.count(); ++i) {
        if (buf[i].note == 60) has60 = true;
        if (buf[i].note == 64) has64 = true;
        if (buf[i].note == 67) has67 = true;
    }
    EXPECT_TRUE(has60);
    EXPECT_TRUE(has64);
    EXPECT_TRUE(has67);
}

TEST(Chord, NoteOffMatchesNoteOn) {
    Chord chord;
    chord.init(kSR);
    chord.setParameter(Chord::kInterval1, 7.0f);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOff(0, 60, 0, 10));
    chord.process(buf, 256, makeTransport());

    EXPECT_EQ(buf.count(), 2);
    bool has60 = false, has67 = false;
    for (int i = 0; i < buf.count(); ++i) {
        if (buf[i].note == 60) has60 = true;
        if (buf[i].note == 67) has67 = true;
        EXPECT_TRUE(buf[i].isNoteOff());
    }
    EXPECT_TRUE(has60);
    EXPECT_TRUE(has67);
}

TEST(Chord, OutOfRangeSkipped) {
    Chord chord;
    chord.init(kSR);
    chord.setParameter(Chord::kInterval1, 20.0f);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 120, 100, 0)); // 120+20 > 127
    chord.process(buf, 256, makeTransport());

    EXPECT_EQ(buf.count(), 1); // Only original, chord tone out of range
    EXPECT_EQ(buf[0].note, 120);
}

// ---------- Scale ----------

TEST(Scale, CMajorPassesThrough) {
    Scale scale;
    scale.init(kSR);
    scale.setParameter(Scale::kRoot, 0.0f);      // C
    scale.setParameter(Scale::kScaleType, 1.0f);  // Major

    // C(60), D(62), E(64) are all in C Major
    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    buf.addMessage(MidiMessage::noteOn(0, 62, 100, 1));
    buf.addMessage(MidiMessage::noteOn(0, 64, 100, 2));
    scale.process(buf, 256, makeTransport());

    EXPECT_EQ(buf[0].note, 60);
    EXPECT_EQ(buf[1].note, 62);
    EXPECT_EQ(buf[2].note, 64);
}

TEST(Scale, CMajorQuantizesSharp) {
    Scale scale;
    scale.init(kSR);
    scale.setParameter(Scale::kRoot, 0.0f);
    scale.setParameter(Scale::kScaleType, 1.0f);

    // C#(61) → quantized to C(60) or D(62)
    uint8_t q = scale.quantize(61);
    EXPECT_TRUE(q == 60 || q == 62);
}

TEST(Scale, ChromaticNoChange) {
    Scale scale;
    scale.init(kSR);
    scale.setParameter(Scale::kScaleType, 0.0f); // Chromatic

    for (uint8_t n = 0; n < 128; ++n)
        EXPECT_EQ(scale.quantize(n), n);
}

// ---------- VelocityEffect ----------

TEST(VelocityEffect, LinearPassthrough) {
    VelocityEffect vel;
    vel.init(kSR);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    vel.process(buf, 256, makeTransport());

    // Min=1, Max=127, Linear: output ≈ original
    EXPECT_GT(buf[0].velocity, 0);
}

TEST(VelocityEffect, ExponentialCurve) {
    VelocityEffect vel;
    vel.init(kSR);
    vel.setParameter(VelocityEffect::kCurve, 1.0f); // Exponential

    MidiBuffer buf;
    // Medium velocity (64/127 ≈ 0.5 normalized → 0.25 after exp)
    buf.addMessage(MidiMessage::noteOn(0, 60, 64, 0));
    vel.process(buf, 256, makeTransport());

    // Exponential reduces mid-range velocity
    float v = (float)buf[0].velocity / 65535.0f;
    EXPECT_LT(v, 0.5f);
}

TEST(VelocityEffect, MinMaxClamp) {
    VelocityEffect vel;
    vel.init(kSR);
    vel.setParameter(VelocityEffect::kMinOut, 60.0f);
    vel.setParameter(VelocityEffect::kMaxOut, 60.0f);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 127, 0));
    vel.process(buf, 256, makeTransport());

    uint8_t v7 = Convert::vel16to7(buf[0].velocity);
    EXPECT_NEAR(v7, 60, 2);
}

// ---------- MidiRandom ----------

TEST(MidiRandom, ProbabilityFilters) {
    MidiRandom rng;
    rng.init(kSR);
    rng.setParameter(MidiRandom::kProbability, 0.0f); // Block all

    MidiBuffer buf;
    for (int i = 0; i < 10; ++i)
        buf.addMessage(MidiMessage::noteOn(0, 60, 100, i));
    rng.process(buf, 256, makeTransport());

    // All NoteOns should be filtered
    int noteOns = 0;
    for (int i = 0; i < buf.count(); ++i)
        if (buf[i].isNoteOn()) ++noteOns;
    EXPECT_EQ(noteOns, 0);
}

TEST(MidiRandom, FullProbabilityPassThrough) {
    MidiRandom rng;
    rng.init(kSR);
    rng.setParameter(MidiRandom::kProbability, 1.0f);

    MidiBuffer buf;
    for (int i = 0; i < 10; ++i)
        buf.addMessage(MidiMessage::noteOn(0, 60, 100, i));
    rng.process(buf, 256, makeTransport());

    int noteOns = 0;
    for (int i = 0; i < buf.count(); ++i)
        if (buf[i].isNoteOn()) ++noteOns;
    EXPECT_EQ(noteOns, 10);
}

TEST(MidiRandom, PitchRangeApplied) {
    MidiRandom rng;
    rng.init(kSR);
    rng.setParameter(MidiRandom::kPitchRange, 12.0f);

    MidiBuffer buf;
    for (int i = 0; i < 50; ++i)
        buf.addMessage(MidiMessage::noteOn(0, 60, 100, i % 256));
    rng.process(buf, 256, makeTransport());

    bool anyDifferent = false;
    for (int i = 0; i < buf.count(); ++i)
        if (buf[i].isNoteOn() && buf[i].note != 60) anyDifferent = true;
    EXPECT_TRUE(anyDifferent);
}

// ---------- NoteLength ----------

TEST(NoteLength, ForcesLength) {
    NoteLength nl;
    nl.init(kSR);
    nl.setParameter(NoteLength::kMode, 0.0f);    // Beat division
    nl.setParameter(NoteLength::kLength, 0.5f);   // Eighth note
    nl.setParameter(NoteLength::kGate, 1.0f);

    auto t = makeTransport(120.0, 0.0);
    int numFrames = 44100; // 1 second = 2 beats @ 120BPM

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    buf.addMessage(MidiMessage::noteOff(0, 60, 0, 100)); // Original NoteOff at frame 100

    nl.process(buf, numFrames, t);

    // Should have NoteOn + our NoteOff (original NoteOff suppressed)
    int noteOns = 0, noteOffs = 0;
    for (int i = 0; i < buf.count(); ++i) {
        if (buf[i].isNoteOn()) ++noteOns;
        if (buf[i].isNoteOff()) ++noteOffs;
    }
    EXPECT_EQ(noteOns, 1);
    EXPECT_EQ(noteOffs, 1);

    // NoteOff should be at ~0.5 beat = 11025 samples from start
    for (int i = 0; i < buf.count(); ++i) {
        if (buf[i].isNoteOff()) {
            EXPECT_NEAR(buf[i].frameOffset, 11025, 5);
        }
    }
}

// ---------- Arpeggiator ----------

TEST(Arpeggiator, Init) {
    Arpeggiator arp;
    arp.init(kSR);
    EXPECT_STREQ(arp.name(), "Arpeggiator");
    EXPECT_FLOAT_EQ(arp.getParameter(Arpeggiator::kRate), 0.25f);
}

TEST(Arpeggiator, UpDirection) {
    Arpeggiator arp;
    arp.init(kSR);
    arp.setParameter(Arpeggiator::kDirection, 0.0f); // Up
    arp.setParameter(Arpeggiator::kRate, 0.25f);     // 16th note
    arp.setParameter(Arpeggiator::kGate, 0.8f);

    // Hold C4 and E4
    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    buf.addMessage(MidiMessage::noteOn(0, 64, 100, 1));

    // Process 2 beats worth of audio (at 120 BPM = 44100 samples)
    auto t = makeTransport(120.0, 0.0);
    int numFrames = 44100;
    arp.process(buf, numFrames, t);

    // Should have generated arp notes
    EXPECT_GT(buf.count(), 0);

    // Check that arp alternates between C4 and E4 (up direction)
    std::vector<uint8_t> pitches;
    for (int i = 0; i < buf.count(); ++i)
        if (buf[i].isNoteOn()) pitches.push_back(buf[i].note);

    EXPECT_GE(pitches.size(), 2u);
    // First arp notes should be 60, 64, 60, 64...
    if (pitches.size() >= 2) {
        EXPECT_EQ(pitches[0], 60);
        EXPECT_EQ(pitches[1], 64);
    }
}

TEST(Arpeggiator, NoOutputWithoutHeldNotes) {
    Arpeggiator arp;
    arp.init(kSR);

    MidiBuffer buf;
    auto t = makeTransport(120.0, 0.0);
    arp.process(buf, 44100, t);

    EXPECT_EQ(buf.count(), 0);
}

TEST(Arpeggiator, ReleaseStopsArp) {
    Arpeggiator arp;
    arp.init(kSR);
    arp.setParameter(Arpeggiator::kRate, 0.25f);

    // Hold and release
    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0));
    buf.addMessage(MidiMessage::noteOff(0, 60, 0, 10));

    auto t = makeTransport(120.0, 0.0);
    arp.process(buf, 44100, t);

    // After release, any active arp note should get NoteOff
    int noteOns = 0, noteOffs = 0;
    for (int i = 0; i < buf.count(); ++i) {
        if (buf[i].isNoteOn()) ++noteOns;
        if (buf[i].isNoteOff()) ++noteOffs;
    }
    EXPECT_GE(noteOffs, noteOns);
}

TEST(Arpeggiator, PassesThroughCC) {
    Arpeggiator arp;
    arp.init(kSR);

    MidiBuffer buf;
    buf.addMessage(MidiMessage::cc(0, 1, 64, 5));
    arp.process(buf, 256, makeTransport());

    bool foundCC = false;
    for (int i = 0; i < buf.count(); ++i)
        if (buf[i].isCC()) foundCC = true;
    EXPECT_TRUE(foundCC);
}

TEST(Arpeggiator, FreeRunsWhenTransportStopped) {
    Arpeggiator arp;
    arp.init(kSR);
    // 1/8 note rate at 120 BPM = 0.5 beat per step
    arp.setParameter(Arpeggiator::kRate, 2.0f);

    // Hold C4
    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn16(0, 60, 32000, 0));
    auto ti = makeTransport(120.0, 0.0, false);  // transport NOT playing
    arp.process(buf, 256, ti);

    // Pump several buffers and collect NoteOn events
    int noteOns = 0;
    for (int i = 0; i < buf.count(); ++i)
        if (buf[i].isNoteOn()) ++noteOns;

    for (int pump = 0; pump < 200; ++pump) {
        MidiBuffer b2;
        arp.process(b2, 256, ti);
        for (int i = 0; i < b2.count(); ++i)
            if (b2[i].isNoteOn()) ++noteOns;
    }

    // Should have generated arp steps even though transport is stopped
    EXPECT_GT(noteOns, 0);
}

// ---------- Integration: Chain ordering ----------

TEST(MidiEffectChain, PitchThenScale) {
    MidiEffectChain chain;
    chain.init(kSR);

    // Pitch +1 (C→C#), then Scale quantize to C Major (C#→C or D)
    auto pitch = std::make_unique<MidiPitch>();
    pitch->setParameter(MidiPitch::kSemitones, 1.0f);
    auto scale = std::make_unique<Scale>();
    scale->setParameter(Scale::kRoot, 0.0f);
    scale->setParameter(Scale::kScaleType, 1.0f); // Major

    chain.addEffect(std::move(pitch));
    chain.addEffect(std::move(scale));

    MidiBuffer buf;
    buf.addMessage(MidiMessage::noteOn(0, 60, 100, 0)); // C4 → C#4 → C4 or D4
    chain.process(buf, 256, makeTransport());

    // Result should be C(60) or D(62), not C#(61)
    EXPECT_NE(buf[0].note, 61);
    EXPECT_TRUE(buf[0].note == 60 || buf[0].note == 62);
}
