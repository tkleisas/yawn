#include <gtest/gtest.h>
#include "util/RtRetireList.h"
#include <atomic>
#include <memory>

using namespace yawn::util;

namespace {
struct Tracked {
    static int alive;
    Tracked() { ++alive; }
    ~Tracked() { --alive; }
    int value = 0;
};
int Tracked::alive = 0;
} // namespace

TEST(RtRetireList, ImmediateDestroyWithoutHeartbeat) {
    // No heartbeat set (single-threaded use): retire() destroys on the spot.
    RtRetireList rl;
    const int before = Tracked::alive;
    rl.retire(std::make_unique<Tracked>());
    EXPECT_EQ(Tracked::alive, before);
    EXPECT_EQ(rl.size(), 0u);
}

TEST(RtRetireList, DeferredUntilGraceElapses) {
    std::atomic<uint64_t> seq{0};
    RtRetireList rl;
    rl.setHeartbeat(&seq);

    const int before = Tracked::alive;
    rl.retire(std::make_unique<Tracked>());
    EXPECT_EQ(Tracked::alive, before + 1) << "retired object must stay alive";
    EXPECT_EQ(rl.size(), 1u);

    // One callback past retire — still within the grace window.
    seq.store(1);
    rl.purge();
    EXPECT_EQ(Tracked::alive, before + 1);
    EXPECT_EQ(rl.size(), 1u);

    // Heartbeat advanced past the grace period — object is freed.
    seq.store(RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(Tracked::alive, before);
    EXPECT_EQ(rl.size(), 0u);
}

TEST(RtRetireList, GraceIsMeasuredFromRetireTime) {
    std::atomic<uint64_t> seq{100};
    RtRetireList rl;
    rl.setHeartbeat(&seq);

    rl.retire(std::make_unique<Tracked>());   // retiredAt = 100
    seq.store(100 + RtRetireList::kGraceCallbacks - 1);
    rl.purge();
    EXPECT_EQ(rl.size(), 1u) << "purged one callback too early";

    seq.store(100 + RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(rl.size(), 0u);
}

TEST(RtRetireList, MultipleEntriesPurgeIndependently) {
    std::atomic<uint64_t> seq{0};
    RtRetireList rl;
    rl.setHeartbeat(&seq);

    const int before = Tracked::alive;
    rl.retire(std::make_unique<Tracked>());   // retiredAt = 0
    seq.store(10);
    rl.retire(std::make_unique<Tracked>());   // retiredAt = 10

    // At seq 12 the first entry's grace has elapsed (12-0 >= 3) but
    // the second's has not (12-10 < 3).
    seq.store(10 + RtRetireList::kGraceCallbacks - 1);
    rl.purge();
    EXPECT_EQ(Tracked::alive, before + 1) << "only the newer entry should remain";
    EXPECT_EQ(rl.size(), 1u);

    seq.store(10 + RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(rl.size(), 0u);
}

TEST(RtRetireList, ClearDropsEverythingImmediately) {
    std::atomic<uint64_t> seq{0};
    RtRetireList rl;
    rl.setHeartbeat(&seq);

    const int before = Tracked::alive;
    rl.retire(std::make_unique<Tracked>());
    rl.retire(std::make_unique<Tracked>());
    EXPECT_EQ(Tracked::alive, before + 2);
    rl.clear();   // engine-stopped path: no audio thread can exist
    EXPECT_EQ(Tracked::alive, before);
    EXPECT_EQ(rl.size(), 0u);
}

TEST(RtRetireList, SharedPtrOverload) {
    std::atomic<uint64_t> seq{0};
    RtRetireList rl;
    rl.setHeartbeat(&seq);

    const int before = Tracked::alive;
    rl.retire(std::shared_ptr<Tracked>(std::make_shared<Tracked>()));
    EXPECT_EQ(Tracked::alive, before + 1);
    seq.store(RtRetireList::kGraceCallbacks);
    rl.purge();
    EXPECT_EQ(Tracked::alive, before);
}

TEST(RtRetireList, NullptrIsIgnored) {
    RtRetireList rl;
    rl.retire(std::unique_ptr<Tracked>{});
    EXPECT_EQ(rl.size(), 0u);
}
