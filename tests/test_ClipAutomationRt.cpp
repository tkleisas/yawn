#include <gtest/gtest.h>
#include "app/Project.h"
#include "audio/AudioEngine.h"
#include "instruments/SubtractiveSynth.h"
#include "midi/MidiPitch.h"
#include <memory>

using namespace yawn;

namespace {
automation::AutomationLane makeLane(int track) {
    automation::AutomationLane lane;
    lane.target = automation::AutomationTarget::instrument(track, 0);
    lane.envelope.addPoint(0.0, 0.5f);
    return lane;
}
} // namespace

// ── Clip automation boxing ─────────────────────────────────────────

TEST(ClipAutomationBoxing, ReplaceSwapsBoxAndRetiresOld) {
    Project p;
    p.init(1, 1);
    auto* slot = p.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);

    p.replaceSlotAutomation(*slot, {makeLane(0)});
    ASSERT_EQ(slot->clipAutomation->lanes.size(), 1u);

    // The audio thread may hold this pointer from a playing clip.
    const auto* oldLanes = &slot->clipAutomation->lanes;

    p.replaceSlotAutomation(*slot, {});
    EXPECT_TRUE(slot->clipAutomation->lanes.empty());
    EXPECT_NE(&slot->clipAutomation->lanes, oldLanes)
        << "expected a fresh box, not an in-place clear";

    // Old box is graveyard-protected and still readable.
    EXPECT_GE(p.graveyardSize(), 1);
    EXPECT_EQ(oldLanes->size(), 1u);
}

TEST(ClipAutomationBoxing, MoveSlotPreservesBoxIdentity) {
    Project p;
    p.init(1, 2);
    auto* src = p.getSlot(0, 0);
    p.replaceSlotAutomation(*src, {makeLane(0)});
    const auto* lanesBefore = &src->clipAutomation->lanes;

    p.moveSlot(0, 0, 0, 1);

    auto* dst = p.getSlot(0, 1);
    EXPECT_EQ(&dst->clipAutomation->lanes, lanesBefore)
        << "moving a slot should move the box, not copy it";
    EXPECT_EQ(dst->clipAutomation->lanes.size(), 1u);
    // Source got a fresh empty box.
    EXPECT_TRUE(src->clipAutomation->lanes.empty());
}

TEST(ClipAutomationBoxing, DeleteSceneRetiresBoxesAndClips) {
    Project p;
    p.init(1, 2);
    auto* slot = p.getSlot(0, 1);
    slot->audioClip = std::make_unique<audio::Clip>();
    p.replaceSlotAutomation(*slot, {makeLane(0)});
    const auto* oldLanes = &slot->clipAutomation->lanes;

    const int graveBefore = p.graveyardSize();
    p.deleteScene(1);   // destroys slot(0,1) with its clip + box

    // The erased slot's clip + automation box were retired, not destroyed.
    EXPECT_GE(p.graveyardSize(), graveBefore + 2);
    EXPECT_EQ(oldLanes->size(), 1u) << "retired box must stay readable";
}

TEST(ClipAutomationBoxing, ClearClipAutomationSwapsBox) {
    Project p;
    p.init(1, 1);
    auto* slot = p.getSlot(0, 0);
    p.replaceSlotAutomation(*slot, {makeLane(0)});
    const auto* oldLanes = &slot->clipAutomation->lanes;

    p.clearClipAutomation(0, 0);
    EXPECT_TRUE(slot->clipAutomation->lanes.empty());
    EXPECT_NE(&slot->clipAutomation->lanes, oldLanes);
    EXPECT_GE(p.graveyardSize(), 1);
}

// ── Engine-side lifetime integration ───────────────────────────────

TEST(EngineLifetime, SetInstrumentSwapRetiresOld) {
    audio::AudioEngine engine;   // no stream — heartbeat wired, no callbacks
    engine.setInstrument(0, std::make_unique<instruments::SubtractiveSynth>());
    ASSERT_NE(engine.instrument(0), nullptr);

    engine.setInstrument(0, std::make_unique<instruments::SubtractiveSynth>());
    EXPECT_EQ(engine.retireList().size(), 1u)
        << "the swapped-out instrument should be parked in the retire list";

    // No stream → pollRetirements clears immediately (no audio thread).
    engine.pollRetirements();
    EXPECT_EQ(engine.retireList().size(), 0u);
}

TEST(EngineLifetime, MidiChainRemovalUsesEngineRetireList) {
    audio::AudioEngine engine;
    ASSERT_TRUE(engine.midiEffectChain(0).addEffect(
        std::make_unique<midi::MidiPitch>()));

    engine.midiEffectChain(0).removeEffectRetired(0);
    EXPECT_EQ(engine.midiEffectChain(0).count(), 0);
    EXPECT_GE(engine.retireList().size(), 1u);

    engine.pollRetirements();
    EXPECT_EQ(engine.retireList().size(), 0u);
}
