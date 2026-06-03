#include <gtest/gtest.h>

#include "visual/VisualNoteBus.h"

using namespace yawn::visual;

namespace {
// Drain whatever a prior test left in the singleton so each case starts
// from empty (the bus is process-global).
void drain() {
    VisualNoteEvent e;
    while (VisualNoteBus::instance().pop(e)) {}
}
}

TEST(VisualNoteBusTest, FifoRoundTrip) {
    drain();
    auto& bus = VisualNoteBus::instance();
    bus.push(1, 9, 36, 100);
    bus.push(2, 0, 60, 64);

    VisualNoteEvent e;
    ASSERT_TRUE(bus.pop(e));
    EXPECT_EQ(e.track, 1); EXPECT_EQ(e.channel, 9);
    EXPECT_EQ(e.pitch, 36); EXPECT_EQ(e.vel7, 100);
    ASSERT_TRUE(bus.pop(e));
    EXPECT_EQ(e.track, 2); EXPECT_EQ(e.pitch, 60); EXPECT_EQ(e.vel7, 64);
    EXPECT_FALSE(bus.pop(e));   // empty
}

TEST(VisualNoteBusTest, OverrunDropsWithoutBlocking) {
    drain();
    auto& bus = VisualNoteBus::instance();
    // Push more than capacity; the ring must accept exactly kCapacity and
    // drop the rest rather than overwrite/block.
    const uint32_t cap = VisualNoteBus::kCapacity;
    for (uint32_t i = 0; i < cap + 50; ++i)
        bus.push(0, 0, static_cast<uint8_t>(i & 0x7F), 1);

    uint32_t got = 0;
    VisualNoteEvent e;
    while (bus.pop(e)) ++got;
    EXPECT_EQ(got, cap);
}
