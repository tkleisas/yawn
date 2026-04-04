#include <gtest/gtest.h>
#include "automation/AutomationTypes.h"
#include "automation/AutomationEnvelope.h"
#include "automation/AutomationLane.h"
#include "automation/AutomationEngine.h"

using namespace yawn;
using namespace yawn::automation;

// ========================= AutomationTarget =========================

TEST(AutomationTarget, FactoryMethods) {
    auto inst = AutomationTarget::instrument(2, 5);
    EXPECT_EQ(inst.type, TargetType::Instrument);
    EXPECT_EQ(inst.trackIndex, 2);
    EXPECT_EQ(inst.chainIndex, 0);
    EXPECT_EQ(inst.paramIndex, 5);

    auto fx = AutomationTarget::audioEffect(1, 3, 7);
    EXPECT_EQ(fx.type, TargetType::AudioEffect);
    EXPECT_EQ(fx.trackIndex, 1);
    EXPECT_EQ(fx.chainIndex, 3);
    EXPECT_EQ(fx.paramIndex, 7);

    auto midi = AutomationTarget::midiEffect(0, 2, 4);
    EXPECT_EQ(midi.type, TargetType::MidiEffect);
    EXPECT_EQ(midi.trackIndex, 0);
    EXPECT_EQ(midi.chainIndex, 2);
    EXPECT_EQ(midi.paramIndex, 4);

    auto mix = AutomationTarget::mixer(3, MixerParam::Pan);
    EXPECT_EQ(mix.type, TargetType::Mixer);
    EXPECT_EQ(mix.trackIndex, 3);
    EXPECT_EQ(mix.paramIndex, static_cast<int>(MixerParam::Pan));
}

TEST(AutomationTarget, Equality) {
    auto a = AutomationTarget::instrument(0, 1);
    auto b = AutomationTarget::instrument(0, 1);
    auto c = AutomationTarget::instrument(0, 2);
    auto d = AutomationTarget::audioEffect(0, 0, 1);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(AutomationTarget, Ordering) {
    auto a = AutomationTarget::instrument(0, 0);
    auto b = AutomationTarget::audioEffect(0, 0, 0);
    auto c = AutomationTarget::instrument(1, 0);

    EXPECT_TRUE(a < b);  // Instrument < AudioEffect
    EXPECT_TRUE(a < c);  // track 0 < track 1
}

TEST(AutomationTarget, Hash) {
    std::hash<AutomationTarget> hasher;
    auto a = AutomationTarget::instrument(0, 1);
    auto b = AutomationTarget::instrument(0, 1);
    auto c = AutomationTarget::instrument(0, 2);

    EXPECT_EQ(hasher(a), hasher(b));
    EXPECT_NE(hasher(a), hasher(c)); // very likely different
}

// ========================= AutomationEnvelope =========================

TEST(AutomationEnvelope, EmptyEnvelope) {
    AutomationEnvelope env;
    EXPECT_TRUE(env.empty());
    EXPECT_EQ(env.pointCount(), 0);
    EXPECT_FLOAT_EQ(env.valueAt(0.0, 0.5f), 0.5f); // returns default
    EXPECT_FLOAT_EQ(env.valueAt(10.0, 0.75f), 0.75f);
}

TEST(AutomationEnvelope, SinglePoint) {
    AutomationEnvelope env;
    env.addPoint(4.0, 0.8f);

    EXPECT_EQ(env.pointCount(), 1);
    EXPECT_FLOAT_EQ(env.valueAt(0.0), 0.8f);  // before = first value
    EXPECT_FLOAT_EQ(env.valueAt(4.0), 0.8f);  // at point
    EXPECT_FLOAT_EQ(env.valueAt(10.0), 0.8f); // after = last value
}

TEST(AutomationEnvelope, LinearInterpolation) {
    AutomationEnvelope env;
    env.addPoint(0.0, 0.0f);
    env.addPoint(4.0, 1.0f);

    EXPECT_FLOAT_EQ(env.valueAt(0.0), 0.0f);
    EXPECT_FLOAT_EQ(env.valueAt(1.0), 0.25f);
    EXPECT_FLOAT_EQ(env.valueAt(2.0), 0.5f);
    EXPECT_FLOAT_EQ(env.valueAt(3.0), 0.75f);
    EXPECT_FLOAT_EQ(env.valueAt(4.0), 1.0f);
}

TEST(AutomationEnvelope, MultipleSegments) {
    AutomationEnvelope env;
    env.addPoint(0.0, 0.0f);
    env.addPoint(2.0, 1.0f);
    env.addPoint(4.0, 0.5f);

    EXPECT_FLOAT_EQ(env.valueAt(1.0), 0.5f);   // midpoint of first segment
    EXPECT_FLOAT_EQ(env.valueAt(3.0), 0.75f);   // midpoint of second segment
    EXPECT_FLOAT_EQ(env.valueAt(-1.0), 0.0f);   // before first = first
    EXPECT_FLOAT_EQ(env.valueAt(5.0), 0.5f);    // after last = last
}

TEST(AutomationEnvelope, InsertionOrder) {
    AutomationEnvelope env;
    // Add out of order — should be sorted
    env.addPoint(4.0, 0.5f);
    env.addPoint(0.0, 0.0f);
    env.addPoint(2.0, 1.0f);

    EXPECT_EQ(env.pointCount(), 3);
    EXPECT_DOUBLE_EQ(env.point(0).time, 0.0);
    EXPECT_DOUBLE_EQ(env.point(1).time, 2.0);
    EXPECT_DOUBLE_EQ(env.point(2).time, 4.0);
}

TEST(AutomationEnvelope, DuplicateTimeUpdatesValue) {
    AutomationEnvelope env;
    env.addPoint(2.0, 0.5f);
    env.addPoint(2.0, 0.8f);

    EXPECT_EQ(env.pointCount(), 1);
    EXPECT_FLOAT_EQ(env.point(0).value, 0.8f);
}

TEST(AutomationEnvelope, RemovePoint) {
    AutomationEnvelope env;
    env.addPoint(0.0, 0.0f);
    env.addPoint(1.0, 0.5f);
    env.addPoint(2.0, 1.0f);

    env.removePoint(1); // remove middle
    EXPECT_EQ(env.pointCount(), 2);
    EXPECT_DOUBLE_EQ(env.point(0).time, 0.0);
    EXPECT_DOUBLE_EQ(env.point(1).time, 2.0);
}

TEST(AutomationEnvelope, MovePoint) {
    AutomationEnvelope env;
    env.addPoint(0.0, 0.0f);
    env.addPoint(2.0, 1.0f);
    env.addPoint(4.0, 0.5f);

    env.movePoint(1, 3.0, 0.75f); // move point from time 2→3
    EXPECT_EQ(env.pointCount(), 3);
    EXPECT_DOUBLE_EQ(env.point(1).time, 3.0);
    EXPECT_FLOAT_EQ(env.point(1).value, 0.75f);
}

TEST(AutomationEnvelope, TimeRange) {
    AutomationEnvelope env;
    EXPECT_DOUBLE_EQ(env.startTime(), 0.0);
    EXPECT_DOUBLE_EQ(env.endTime(), 0.0);

    env.addPoint(2.0, 0.5f);
    env.addPoint(8.0, 1.0f);
    EXPECT_DOUBLE_EQ(env.startTime(), 2.0);
    EXPECT_DOUBLE_EQ(env.endTime(), 8.0);
}

TEST(AutomationEnvelope, Clear) {
    AutomationEnvelope env;
    env.addPoint(0.0, 0.0f);
    env.addPoint(1.0, 1.0f);
    env.clear();

    EXPECT_TRUE(env.empty());
    EXPECT_EQ(env.pointCount(), 0);
}

TEST(AutomationEnvelope, JsonRoundTrip) {
    AutomationEnvelope env;
    env.addPoint(0.0, 0.0f);
    env.addPoint(1.5, 0.75f);
    env.addPoint(4.0, 1.0f);

    auto j = env.toJson();
    auto restored = AutomationEnvelope::fromJson(j);

    EXPECT_EQ(restored.pointCount(), 3);
    EXPECT_DOUBLE_EQ(restored.point(0).time, 0.0);
    EXPECT_FLOAT_EQ(restored.point(0).value, 0.0f);
    EXPECT_DOUBLE_EQ(restored.point(1).time, 1.5);
    EXPECT_FLOAT_EQ(restored.point(1).value, 0.75f);
    EXPECT_DOUBLE_EQ(restored.point(2).time, 4.0);
    EXPECT_FLOAT_EQ(restored.point(2).value, 1.0f);
}

TEST(AutomationEnvelope, RemoveOutOfBounds) {
    AutomationEnvelope env;
    env.addPoint(0.0, 1.0f);
    env.removePoint(-1); // no-op
    env.removePoint(5);  // no-op
    EXPECT_EQ(env.pointCount(), 1);
}

TEST(AutomationEnvelope, MoveOutOfBounds) {
    AutomationEnvelope env;
    env.addPoint(0.0, 1.0f);
    env.movePoint(-1, 1.0, 0.5f); // no-op
    env.movePoint(5, 1.0, 0.5f);  // no-op
    EXPECT_EQ(env.pointCount(), 1);
    EXPECT_FLOAT_EQ(env.point(0).value, 1.0f);
}

// ========================= AutomationLane Serialization =========================

TEST(AutomationLane, TargetJsonRoundTrip) {
    auto tgt = AutomationTarget::audioEffect(2, 1, 3);
    auto j = automation::targetToJson(tgt);
    auto restored = automation::targetFromJson(j);

    EXPECT_EQ(restored.type, TargetType::AudioEffect);
    EXPECT_EQ(restored.trackIndex, 2);
    EXPECT_EQ(restored.chainIndex, 1);
    EXPECT_EQ(restored.paramIndex, 3);
}

TEST(AutomationLane, LaneJsonRoundTrip) {
    AutomationLane lane;
    lane.target = AutomationTarget::mixer(0, MixerParam::Volume);
    lane.envelope.addPoint(0.0, 0.0f);
    lane.envelope.addPoint(4.0, 1.0f);
    lane.armed = true;

    auto j = automation::laneToJson(lane);
    auto restored = automation::laneFromJson(j);

    EXPECT_EQ(restored.target, lane.target);
    EXPECT_EQ(restored.envelope.pointCount(), 2);
    EXPECT_FLOAT_EQ(restored.envelope.valueAt(2.0), 0.5f);
    EXPECT_TRUE(restored.armed);
}

TEST(AutomationLane, LanesVectorJsonRoundTrip) {
    std::vector<AutomationLane> lanes;

    AutomationLane l1;
    l1.target = AutomationTarget::instrument(0, 2);
    l1.envelope.addPoint(0.0, 0.5f);
    l1.envelope.addPoint(8.0, 1.0f);
    lanes.push_back(l1);

    AutomationLane l2;
    l2.target = AutomationTarget::midiEffect(1, 0, 1);
    l2.envelope.addPoint(1.0, 0.0f);
    l2.armed = true;
    lanes.push_back(l2);

    auto j = automation::lanesToJson(lanes);
    auto restored = automation::lanesFromJson(j);

    EXPECT_EQ(restored.size(), 2u);
    EXPECT_EQ(restored[0].target, l1.target);
    EXPECT_EQ(restored[0].envelope.pointCount(), 2);
    EXPECT_EQ(restored[1].target, l2.target);
    EXPECT_EQ(restored[1].envelope.pointCount(), 1);
    EXPECT_TRUE(restored[1].armed);
}

// ========================= AutomationEngine =========================

TEST(AutomationEngine, TrackAutoModeDefault) {
    automation::AutomationEngine engine;
    for (int t = 0; t < 8; ++t)
        EXPECT_EQ(engine.trackAutoMode(t), AutoMode::Off);
}

TEST(AutomationEngine, SetTrackAutoMode) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Read);
    engine.setTrackAutoMode(1, AutoMode::Touch);
    engine.setTrackAutoMode(2, AutoMode::Latch);

    EXPECT_EQ(engine.trackAutoMode(0), AutoMode::Read);
    EXPECT_EQ(engine.trackAutoMode(1), AutoMode::Touch);
    EXPECT_EQ(engine.trackAutoMode(2), AutoMode::Latch);
}

TEST(AutomationEngine, TrackAutoModeBoundsCheck) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(-1, AutoMode::Read); // no crash
    engine.setTrackAutoMode(999, AutoMode::Read); // no crash
    EXPECT_EQ(engine.trackAutoMode(-1), AutoMode::Off);
    EXPECT_EQ(engine.trackAutoMode(999), AutoMode::Off);
}

TEST(AutomationEngine, DoesNothingWhenNotPlaying) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Read);

    std::vector<AutomationLane> lanes;
    AutomationLane lane;
    lane.target = AutomationTarget::mixer(0, MixerParam::Volume);
    lane.envelope.addPoint(0.0, 0.0f);
    lane.envelope.addPoint(4.0, 1.0f);
    lanes.push_back(lane);

    automation::AutomationEngine::Context ctx;
    ctx.positionInBeats = 2.0;
    ctx.isPlaying = false;
    ctx.trackLanes[0] = &lanes;

    // Should not crash and should not apply anything (no mixer set)
    engine.process(ctx);
}

TEST(AutomationEngine, DoesNothingWhenModeOff) {
    automation::AutomationEngine engine;
    // Mode is Off by default

    std::vector<AutomationLane> lanes;
    AutomationLane lane;
    lane.target = AutomationTarget::mixer(0, MixerParam::Volume);
    lane.envelope.addPoint(0.0, 0.5f);
    lanes.push_back(lane);

    automation::AutomationEngine::Context ctx;
    ctx.positionInBeats = 0.0;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    engine.process(ctx); // Off mode, should not apply
}

// ========================= Automation Recording =========================

TEST(AutomationRecording, TouchModeRecordsWhileTouching) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Touch);

    auto target = AutomationTarget::instrument(0, 0);
    std::vector<AutomationLane> lanes;

    automation::AutomationEngine::Context ctx;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    // Begin touch
    engine.handleParamTouch(0, target, 0.5f, true);
    EXPECT_TRUE(engine.touchState(0).active);

    // Process at beat 1.0
    ctx.positionInBeats = 1.0;
    engine.process(ctx);
    EXPECT_EQ(lanes.size(), 1u);
    EXPECT_EQ(lanes[0].envelope.pointCount(), 1);
    EXPECT_FLOAT_EQ(lanes[0].envelope.point(0).value, 0.5f);

    // Update value and process at beat 2.0
    engine.updateTouchValue(0, 0.8f);
    ctx.positionInBeats = 2.0;
    engine.process(ctx);
    EXPECT_EQ(lanes[0].envelope.pointCount(), 2);
    EXPECT_FLOAT_EQ(lanes[0].envelope.point(1).value, 0.8f);

    // Release — Touch mode: recording stops, no latch hold
    engine.handleParamTouch(0, target, 0.8f, false);
    EXPECT_FALSE(engine.touchState(0).active);
    EXPECT_FALSE(engine.touchState(0).latchHolding);
}

TEST(AutomationRecording, LatchModeHoldsAfterRelease) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Latch);

    auto target = AutomationTarget::instrument(0, 0);
    std::vector<AutomationLane> lanes;

    automation::AutomationEngine::Context ctx;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    // Begin touch and record
    engine.handleParamTouch(0, target, 0.6f, true);
    ctx.positionInBeats = 1.0;
    engine.process(ctx);

    // Release — Latch mode: keeps recording
    engine.handleParamTouch(0, target, 0.6f, false);
    EXPECT_FALSE(engine.touchState(0).active);
    EXPECT_TRUE(engine.touchState(0).latchHolding);

    // Continue processing — latch hold still records
    ctx.positionInBeats = 2.0;
    engine.process(ctx);
    EXPECT_EQ(lanes[0].envelope.pointCount(), 2);

    // Stop latch hold explicitly
    engine.stopLatchHold(0);
    EXPECT_FALSE(engine.touchState(0).latchHolding);
}

TEST(AutomationRecording, NoRecordingInReadMode) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Read);

    auto target = AutomationTarget::instrument(0, 0);
    std::vector<AutomationLane> lanes;

    automation::AutomationEngine::Context ctx;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    engine.handleParamTouch(0, target, 0.5f, true);
    ctx.positionInBeats = 1.0;
    engine.process(ctx);

    // Read mode should not create any lanes
    EXPECT_TRUE(lanes.empty());
}

TEST(AutomationRecording, NoRecordingInOffMode) {
    automation::AutomationEngine engine;
    // Default is Off

    auto target = AutomationTarget::instrument(0, 0);
    std::vector<AutomationLane> lanes;

    automation::AutomationEngine::Context ctx;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    engine.handleParamTouch(0, target, 0.5f, true);
    ctx.positionInBeats = 1.0;
    engine.process(ctx);

    EXPECT_TRUE(lanes.empty());
}

TEST(AutomationRecording, CreatesLaneForNewTarget) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Touch);

    std::vector<AutomationLane> lanes;
    automation::AutomationEngine::Context ctx;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    // Touch instrument param 0
    auto t1 = AutomationTarget::instrument(0, 0);
    engine.handleParamTouch(0, t1, 0.5f, true);
    ctx.positionInBeats = 1.0;
    engine.process(ctx);
    engine.handleParamTouch(0, t1, 0.5f, false);
    EXPECT_EQ(lanes.size(), 1u);
    EXPECT_EQ(lanes[0].target, t1);

    // Touch instrument param 1 — new lane created
    auto t2 = AutomationTarget::instrument(0, 1);
    engine.handleParamTouch(0, t2, 0.3f, true);
    ctx.positionInBeats = 2.0;
    engine.process(ctx);
    EXPECT_EQ(lanes.size(), 2u);
    EXPECT_EQ(lanes[1].target, t2);
}

TEST(AutomationRecording, RecordsToExistingLane) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Touch);

    auto target = AutomationTarget::instrument(0, 0);

    // Pre-existing lane with a point
    std::vector<AutomationLane> lanes;
    lanes.push_back({target, {}, false});
    lanes[0].envelope.addPoint(0.0, 0.0f);

    automation::AutomationEngine::Context ctx;
    ctx.isPlaying = true;
    ctx.trackLanes[0] = &lanes;

    engine.handleParamTouch(0, target, 0.7f, true);
    ctx.positionInBeats = 1.0;
    engine.process(ctx);

    // Should add to existing lane, not create a new one
    EXPECT_EQ(lanes.size(), 1u);
    EXPECT_EQ(lanes[0].envelope.pointCount(), 2);
}

TEST(AutomationRecording, IsRecordingQuery) {
    automation::AutomationEngine engine;
    engine.setTrackAutoMode(0, AutoMode::Touch);
    engine.setTrackAutoMode(1, AutoMode::Latch);

    EXPECT_FALSE(engine.isRecording(0));
    EXPECT_FALSE(engine.isRecording(1));

    auto target = AutomationTarget::instrument(0, 0);
    engine.handleParamTouch(0, target, 0.5f, true);
    EXPECT_TRUE(engine.isRecording(0));

    engine.handleParamTouch(0, target, 0.5f, false);
    EXPECT_FALSE(engine.isRecording(0));

    // Latch keeps recording after release
    auto target2 = AutomationTarget::instrument(1, 0);
    engine.handleParamTouch(1, target2, 0.5f, true);
    engine.handleParamTouch(1, target2, 0.5f, false);
    EXPECT_TRUE(engine.isRecording(1)); // latch hold
    engine.stopLatchHold(1);
    EXPECT_FALSE(engine.isRecording(1));
}
