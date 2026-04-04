#include <gtest/gtest.h>
#include "midi/LFO.h"
#include <cmath>

using namespace yawn::midi;

// ========================= LFO Parameter API =========================

TEST(LFO, DefaultParameters) {
    LFO lfo;
    lfo.init(44100.0);

    EXPECT_EQ(lfo.parameterCount(), LFO::kNumParams);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kShape), 0.0f);  // Sine
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kRate), 1.0f);    // 1 beat
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kSync), 1.0f);    // Synced
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kDepth), 0.5f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kPhase), 0.0f);
}

TEST(LFO, SetGetParameters) {
    LFO lfo;
    lfo.init(44100.0);

    lfo.setParameter(LFO::kShape, 2.0f);  // Saw
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kShape), 2.0f);

    lfo.setParameter(LFO::kRate, 4.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kRate), 4.0f);

    lfo.setParameter(LFO::kSync, 0.0f);   // Free-running
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kSync), 0.0f);

    lfo.setParameter(LFO::kDepth, 0.75f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kDepth), 0.75f);

    lfo.setParameter(LFO::kTargetType, 3.0f);   // Mixer
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kTargetType), 3.0f);

    lfo.setParameter(LFO::kTargetChainIndex, 2.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kTargetChainIndex), 2.0f);

    lfo.setParameter(LFO::kTargetParamIndex, 5.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kTargetParamIndex), 5.0f);
}

TEST(LFO, ParameterClamping) {
    LFO lfo;
    lfo.init(44100.0);

    lfo.setParameter(LFO::kShape, -1.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kShape), 0.0f);

    lfo.setParameter(LFO::kShape, 10.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kShape), 4.0f);  // kNumShapes - 1

    lfo.setParameter(LFO::kDepth, 5.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kDepth), 1.0f);

    lfo.setParameter(LFO::kDepth, -1.0f);
    EXPECT_FLOAT_EQ(lfo.getParameter(LFO::kDepth), 0.0f);
}

TEST(LFO, NameAndId) {
    LFO lfo;
    EXPECT_STREQ(lfo.name(), "LFO");
    EXPECT_STREQ(lfo.id(), "lfo");
}

// ========================= LFO Waveform Output =========================

TEST(LFO, SineAtZeroIsZero) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kDepth, 1.0f);
    lfo.setParameter(LFO::kPhase, 0.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.0;  // Phase = 0, sin(0) = 0

    lfo.process(buf, 128, ti);
    EXPECT_NEAR(lfo.modulationValue(), 0.0f, 0.01f);
}

TEST(LFO, SineAtQuarterBeat) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 0.0f);  // Sine
    lfo.setParameter(LFO::kRate, 1.0f);   // 1 beat cycle
    lfo.setParameter(LFO::kDepth, 1.0f);
    lfo.setParameter(LFO::kPhase, 0.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.25;  // Phase = 0.25, sin(π/2) = 1.0

    lfo.process(buf, 128, ti);
    EXPECT_NEAR(lfo.modulationValue(), 1.0f, 0.01f);
}

TEST(LFO, SquareWavePositive) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 3.0f);  // Square
    lfo.setParameter(LFO::kRate, 1.0f);
    lfo.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.1;  // Phase < 0.5 → +1

    lfo.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(lfo.modulationValue(), 1.0f);
}

TEST(LFO, SquareWaveNegative) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 3.0f);  // Square
    lfo.setParameter(LFO::kRate, 1.0f);
    lfo.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.7;  // Phase > 0.5 → -1

    lfo.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(lfo.modulationValue(), -1.0f);
}

TEST(LFO, SawWave) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 2.0f);  // Saw
    lfo.setParameter(LFO::kRate, 1.0f);
    lfo.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;

    // At phase 0.5, saw = 2*0.5 - 1 = 0
    ti.positionInBeats = 0.5;
    lfo.process(buf, 128, ti);
    EXPECT_NEAR(lfo.modulationValue(), 0.0f, 0.01f);

    // At phase 0.75, saw = 2*0.75 - 1 = 0.5
    ti.positionInBeats = 0.75;
    lfo.process(buf, 128, ti);
    EXPECT_NEAR(lfo.modulationValue(), 0.5f, 0.01f);
}

TEST(LFO, DepthScaling) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 3.0f);  // Square
    lfo.setParameter(LFO::kRate, 1.0f);
    lfo.setParameter(LFO::kDepth, 0.5f);  // Half depth

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.1;  // Square positive half

    lfo.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(lfo.modulationValue(), 0.5f);  // 1.0 * 0.5 depth
}

TEST(LFO, ZeroDepthOutputsZero) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kDepth, 0.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.25;

    lfo.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(lfo.modulationValue(), 0.0f);
}

TEST(LFO, PhaseOffset) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 3.0f);  // Square
    lfo.setParameter(LFO::kRate, 1.0f);
    lfo.setParameter(LFO::kDepth, 1.0f);
    lfo.setParameter(LFO::kPhase, 0.5f);  // 180° offset

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.1;  // Phase 0.1 + 0.5 offset = 0.6 → square negative

    lfo.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(lfo.modulationValue(), -1.0f);
}

// ========================= LFO Modulation Interface =========================

TEST(LFO, ModulationOutputInterface) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kDepth, 1.0f);
    lfo.setParameter(LFO::kTargetType, 1.0f);       // AudioEffect
    lfo.setParameter(LFO::kTargetChainIndex, 2.0f);
    lfo.setParameter(LFO::kTargetParamIndex, 3.0f);

    EXPECT_TRUE(lfo.hasModulationOutput());
    EXPECT_EQ(lfo.modulationTargetType(), 1);
    EXPECT_EQ(lfo.modulationTargetChain(), 2);
    EXPECT_EQ(lfo.modulationTargetParam(), 3);
}

TEST(LFO, NoModulationWhenDepthZero) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kDepth, 0.0f);

    EXPECT_FALSE(lfo.hasModulationOutput());
}

TEST(LFO, BypassedOutputsZero) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 3.0f);  // Square
    lfo.setParameter(LFO::kDepth, 1.0f);
    lfo.setBypassed(true);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.1;

    lfo.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(lfo.modulationValue(), 0.0f);
}

TEST(LFO, RateChangesFrequency) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 0.0f);  // Sine
    lfo.setParameter(LFO::kRate, 2.0f);   // 2 beats per cycle
    lfo.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;

    // At beat 0.5 with rate 2: phase = 0.5/2 = 0.25, sin(π/2) = 1
    ti.positionInBeats = 0.5;
    lfo.process(buf, 128, ti);
    EXPECT_NEAR(lfo.modulationValue(), 1.0f, 0.01f);
}

// ========================= LFO Phase Linking =========================

TEST(LFO, InstanceIdUnique) {
    LFO a, b, c;
    EXPECT_NE(a.instanceId(), b.instanceId());
    EXPECT_NE(b.instanceId(), c.instanceId());
    EXPECT_NE(a.instanceId(), c.instanceId());
    EXPECT_GT(a.instanceId(), 0u);
}

TEST(LFO, DefaultUnlinked) {
    LFO lfo;
    lfo.init(44100.0);

    EXPECT_FALSE(lfo.isLinkedToSource());
    EXPECT_EQ(lfo.linkTargetId(), 0u);
    EXPECT_EQ(lfo.linkSourceId(), 0u);
}

TEST(LFO, SetLinkTargetId) {
    LFO leader, follower;
    leader.init(44100.0);
    follower.init(44100.0);

    follower.setLinkTargetId(leader.instanceId());

    EXPECT_TRUE(follower.isLinkedToSource());
    EXPECT_EQ(follower.linkTargetId(), leader.instanceId());
    EXPECT_EQ(follower.linkSourceId(), leader.instanceId());
}

TEST(LFO, ClearLink) {
    LFO leader, follower;
    follower.setLinkTargetId(leader.instanceId());
    EXPECT_TRUE(follower.isLinkedToSource());

    follower.setLinkTargetId(0);
    EXPECT_FALSE(follower.isLinkedToSource());
}

TEST(LFO, LinkSurvivesReorder) {
    // Simulate: leader has a stable ID, follower references it
    LFO leader;
    leader.init(44100.0);
    uint32_t leaderId = leader.instanceId();

    LFO follower;
    follower.init(44100.0);
    follower.setLinkTargetId(leaderId);

    // Even if leader moves to a different slot, follower still references the correct ID
    EXPECT_EQ(follower.linkSourceId(), leaderId);
}

TEST(LFO, BasePhaseExposed) {
    LFO lfo;
    lfo.init(44100.0);
    lfo.setParameter(LFO::kShape, 0.0f);  // Sine
    lfo.setParameter(LFO::kRate, 1.0f);
    lfo.setParameter(LFO::kDepth, 1.0f);
    lfo.setParameter(LFO::kPhase, 0.3f);  // Has offset

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.25;

    lfo.process(buf, 128, ti);

    // Base phase = 0.25/1.0 = 0.25 (before offset)
    EXPECT_NEAR(lfo.currentPhase(), 0.25, 0.001);
}

TEST(LFO, OverridePhaseRecomputesOutput) {
    // Leader: square wave at phase 0.1 (positive half)
    LFO leader;
    leader.init(44100.0);
    leader.setParameter(LFO::kShape, 3.0f);  // Square
    leader.setParameter(LFO::kRate, 1.0f);
    leader.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.1;

    leader.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(leader.modulationValue(), 1.0f);   // phase 0.1 → positive
    EXPECT_NEAR(leader.currentPhase(), 0.1, 0.001);

    // Follower: square wave, initially at a different position (phase 0.7 → negative)
    LFO follower;
    follower.init(44100.0);
    follower.setParameter(LFO::kShape, 3.0f);  // Square
    follower.setParameter(LFO::kRate, 1.0f);
    follower.setParameter(LFO::kDepth, 1.0f);
    follower.setParameter(LFO::kPhase, 0.0f);  // No offset

    ti.positionInBeats = 0.7;
    follower.process(buf, 128, ti);
    EXPECT_FLOAT_EQ(follower.modulationValue(), -1.0f); // phase 0.7 → negative

    // Override follower with leader's phase → should become positive
    follower.overridePhase(leader.currentPhase());
    EXPECT_FLOAT_EQ(follower.modulationValue(), 1.0f);
}

TEST(LFO, LinkedFollowerWithPhaseOffset) {
    LFO leader;
    leader.init(44100.0);
    leader.setParameter(LFO::kShape, 3.0f);  // Square
    leader.setParameter(LFO::kRate, 1.0f);
    leader.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.1;  // Leader base phase = 0.1

    leader.process(buf, 128, ti);
    EXPECT_NEAR(leader.currentPhase(), 0.1, 0.001);

    // Follower with 0.5 phase offset: 0.1 + 0.5 = 0.6 → negative half of square
    LFO follower;
    follower.init(44100.0);
    follower.setParameter(LFO::kShape, 3.0f);
    follower.setParameter(LFO::kDepth, 1.0f);
    follower.setParameter(LFO::kPhase, 0.5f);

    follower.overridePhase(leader.currentPhase());
    EXPECT_FLOAT_EQ(follower.modulationValue(), -1.0f);  // Inverted
}

TEST(LFO, LinkedFollowerDifferentShape) {
    // Leader: sine at beat 0.25 → base phase 0.25
    LFO leader;
    leader.init(44100.0);
    leader.setParameter(LFO::kShape, 0.0f);  // Sine
    leader.setParameter(LFO::kRate, 1.0f);
    leader.setParameter(LFO::kDepth, 1.0f);

    MidiBuffer buf;
    TransportInfo ti;
    ti.bpm = 120.0;
    ti.sampleRate = 44100.0;
    ti.samplesPerBeat = 22050.0;
    ti.playing = true;
    ti.positionInBeats = 0.25;

    leader.process(buf, 128, ti);

    // Follower: saw wave, linked to leader's phase 0.25
    // saw(0.25) = 2*0.25 - 1 = -0.5
    LFO follower;
    follower.init(44100.0);
    follower.setParameter(LFO::kShape, 2.0f);  // Saw
    follower.setParameter(LFO::kDepth, 1.0f);

    follower.overridePhase(leader.currentPhase());
    EXPECT_NEAR(follower.modulationValue(), -0.5f, 0.01f);
}
