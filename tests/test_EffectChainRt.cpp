#include <gtest/gtest.h>
#include "effects/EffectChain.h"
#include "midi/MidiEffectChain.h"
#include "util/RtRetireList.h"
#include <atomic>
#include <memory>

using namespace yawn;

namespace {

// Minimal AudioEffect that counts live instances and process() calls —
// lets the tests prove a removed effect stays alive (and callable)
// through a previously published snapshot until the grace period ends.
class CountedFx : public effects::AudioEffect {
public:
    static int alive;
    CountedFx() { ++alive; }
    ~CountedFx() override { --alive; }

    void init(double, int) override {}
    void reset() override {}
    void process(float*, int, int) override { ++processCalls; }
    int parameterCount() const override { return 0; }
    const effects::ParameterInfo& parameterInfo(int) const override {
        static const effects::ParameterInfo dummy{"", 0, 0, 0, ""};
        return dummy;
    }
    float getParameter(int) const override { return 0.0f; }
    void setParameter(int, float) override {}
    const char* name() const override { return "Counted"; }
    const char* id() const override { return "counted"; }

    int processCalls = 0;
};
int CountedFx::alive = 0;

class CountedMidiFx : public midi::MidiEffect {
public:
    static int alive;
    CountedMidiFx() { ++alive; }
    ~CountedMidiFx() override { --alive; }

    void init(double) override {}
    void reset() override {}
    void process(midi::MidiBuffer&, int, const midi::TransportInfo&) override {}
    const char* name() const override { return "CountedMidi"; }
    const char* id() const override { return "countedmidi"; }
    int parameterCount() const override { return 0; }
    const midi::MidiEffectParameterInfo& parameterInfo(int) const override {
        static const midi::MidiEffectParameterInfo dummy{"", 0, 0, 0, ""};
        return dummy;
    }
    float getParameter(int) const override { return 0.0f; }
    void setParameter(int, float) override {}
};
int CountedMidiFx::alive = 0;

} // namespace

// ── EffectChain snapshot publishing ────────────────────────────────

TEST(EffectChainRt, EmptySnapshotAfterConstruction) {
    effects::EffectChain chain;
    const auto* snap = chain.rtSnapshot();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->count, 0);
}

TEST(EffectChainRt, InsertPublishesNewSnapshot) {
    effects::EffectChain chain;
    chain.init(48000.0, 256);
    chain.append(std::make_unique<CountedFx>());

    const auto* snap = chain.rtSnapshot();
    ASSERT_NE(snap, nullptr);
    ASSERT_EQ(snap->count, 1);
    EXPECT_NE(snap->ptrs[0], nullptr);
    EXPECT_EQ(chain.effectAtRt(0), snap->ptrs[0]);
    EXPECT_EQ(chain.effectAtRt(7), nullptr);
    EXPECT_EQ(chain.effectAtRt(-1), nullptr);
}

TEST(EffectChainRt, RemovedEffectStaysAliveViaRetireList) {
    std::atomic<uint64_t> seq{0};
    util::RtRetireList rl;
    rl.setHeartbeat(&seq);

    effects::EffectChain chain;
    chain.setRetireList(&rl);
    chain.init(48000.0, 256);
    chain.append(std::make_unique<CountedFx>());

    // The audio thread may be holding this snapshot mid-block.
    const auto* oldSnap = chain.rtSnapshot();
    ASSERT_EQ(oldSnap->count, 1);
    auto* fxRaw = static_cast<CountedFx*>(oldSnap->ptrs[0]);

    const int before = CountedFx::alive;
    chain.removeRetired(0);

    // New view is empty; the old snapshot still references the effect,
    // which must stay alive (and safe to call) until the grace purge.
    EXPECT_EQ(chain.rtSnapshot()->count, 0);
    EXPECT_EQ(CountedFx::alive, before);
    float buf[4] = {};
    oldSnap->ptrs[0]->process(buf, 2, 2);
    EXPECT_EQ(fxRaw->processCalls, 1);

    seq.store(util::RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(CountedFx::alive, before - 1);
}

TEST(EffectChainRt, ClearRetiresAllEffects) {
    std::atomic<uint64_t> seq{0};
    util::RtRetireList rl;
    rl.setHeartbeat(&seq);

    effects::EffectChain chain;
    chain.setRetireList(&rl);
    chain.init(48000.0, 256);
    chain.append(std::make_unique<CountedFx>());
    chain.append(std::make_unique<CountedFx>());

    const int before = CountedFx::alive;
    chain.clear();
    EXPECT_EQ(chain.rtSnapshot()->count, 0);
    EXPECT_EQ(CountedFx::alive, before) << "cleared effects must be retired, not destroyed";

    seq.store(util::RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(CountedFx::alive, before - 2);
}

TEST(EffectChainRt, MoveEffectRepublishesOrder) {
    effects::EffectChain chain;
    chain.init(48000.0, 256);
    auto* a = new CountedFx();
    auto* b = new CountedFx();
    chain.append(std::unique_ptr<CountedFx>(a));
    chain.append(std::unique_ptr<CountedFx>(b));

    chain.moveEffect(0, 1);
    const auto* snap = chain.rtSnapshot();
    ASSERT_EQ(snap->count, 2);
    EXPECT_EQ(snap->ptrs[0], b);
    EXPECT_EQ(snap->ptrs[1], a);
}

TEST(EffectChainRt, MoveAssignRetiresOverwrittenContents) {
    std::atomic<uint64_t> seq{0};
    util::RtRetireList rl;
    rl.setHeartbeat(&seq);

    effects::EffectChain a;
    a.setRetireList(&rl);
    a.init(48000.0, 256);
    a.append(std::make_unique<CountedFx>());   // will be overwritten

    effects::EffectChain b;
    b.setRetireList(&rl);
    b.init(48000.0, 256);
    b.append(std::make_unique<CountedFx>());   // moves into a

    const int before = CountedFx::alive;
    a = std::move(b);
    EXPECT_EQ(CountedFx::alive, before) << "overwritten effect must be retired";
    ASSERT_EQ(a.rtSnapshot()->count, 1);

    seq.store(util::RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(CountedFx::alive, before - 1);
}

// ── MidiEffectChain snapshot publishing ────────────────────────────

TEST(MidiEffectChainRt, AddAndRemovePublishSnapshots) {
    std::atomic<uint64_t> seq{0};
    util::RtRetireList rl;
    rl.setHeartbeat(&seq);

    midi::MidiEffectChain chain;
    chain.setRetireList(&rl);
    chain.init(48000.0);
    ASSERT_TRUE(chain.addEffect(std::make_unique<CountedMidiFx>()));

    const auto* oldSnap = chain.rtSnapshot();
    ASSERT_EQ(oldSnap->count, 1);

    const int before = CountedMidiFx::alive;
    chain.removeEffectRetired(0);
    EXPECT_EQ(chain.rtSnapshot()->count, 0);
    EXPECT_EQ(CountedMidiFx::alive, before);
    EXPECT_EQ(chain.effectRt(0), nullptr);

    seq.store(util::RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(CountedMidiFx::alive, before - 1);
}

TEST(MidiEffectChainRt, ResetIteratesPublishedSnapshot) {
    midi::MidiEffectChain chain;
    chain.init(48000.0);
    ASSERT_TRUE(chain.addEffect(std::make_unique<CountedMidiFx>()));
    // reset() is called from the audio thread in production — it must
    // not trip over the snapshot view.
    chain.reset();
    EXPECT_EQ(chain.rtSnapshot()->count, 1);
}
