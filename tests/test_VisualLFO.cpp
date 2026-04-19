// VisualLFO — exercise each waveform shape plus beat-sync vs free-running
// behaviour. Pure math, no GL, runs on CI.

#include <gtest/gtest.h>
#include "visual/VisualLFO.h"

#include <cmath>

using namespace yawn::visual;

TEST(VisualLFOTest, DisabledReturnsZero) {
    VisualLFO lfo;
    lfo.enabled = false;
    EXPECT_FLOAT_EQ(lfo.evaluate(0.0, 0.0), 0.0f);
    EXPECT_FLOAT_EQ(lfo.evaluate(3.14, 17.5), 0.0f);
}

TEST(VisualLFOTest, ZeroDepthReturnsZero) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.depth   = 0.0f;
    EXPECT_FLOAT_EQ(lfo.evaluate(1.0, 1.0), 0.0f);
}

TEST(VisualLFOTest, SineBeatSyncFullCycle) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::Sine;
    lfo.rate    = 4.0f;  // 4 beats per cycle
    lfo.depth   = 1.0f;
    lfo.sync    = true;
    // 0 beats → 0, 1 beat (quarter cycle) → +1, 2 beats → 0,
    // 3 beats → -1, 4 beats → back to 0.
    EXPECT_NEAR(lfo.evaluate(0.0, 0.0),  0.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(1.0, 0.0),  1.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(2.0, 0.0),  0.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(3.0, 0.0), -1.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(4.0, 0.0),  0.0f, 1e-4f);
}

TEST(VisualLFOTest, SquareBipolar) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::Square;
    lfo.rate    = 4.0f;
    lfo.depth   = 1.0f;
    lfo.sync    = true;
    // First half of the cycle = +1, second half = -1.
    EXPECT_FLOAT_EQ(lfo.evaluate(0.5, 0.0),  1.0f);
    EXPECT_FLOAT_EQ(lfo.evaluate(1.9, 0.0),  1.0f);
    EXPECT_FLOAT_EQ(lfo.evaluate(2.1, 0.0), -1.0f);
    EXPECT_FLOAT_EQ(lfo.evaluate(3.9, 0.0), -1.0f);
}

TEST(VisualLFOTest, SawRamps) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::Saw;
    lfo.rate    = 1.0f;  // 1 cycle per beat
    lfo.depth   = 1.0f;
    lfo.sync    = true;
    EXPECT_NEAR(lfo.evaluate(0.0,  0.0), -1.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(0.5,  0.0),  0.0f, 1e-4f);
    // Cycle boundary: fract(1.0) = 0 → back to -1.
    EXPECT_NEAR(lfo.evaluate(1.0,  0.0), -1.0f, 1e-4f);
}

TEST(VisualLFOTest, TriangleSymmetric) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::Triangle;
    lfo.rate    = 1.0f;
    lfo.depth   = 1.0f;
    lfo.sync    = true;
    EXPECT_NEAR(lfo.evaluate(0.00, 0.0), -1.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(0.25, 0.0),  0.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(0.50, 0.0),  1.0f, 1e-4f);
    EXPECT_NEAR(lfo.evaluate(0.75, 0.0),  0.0f, 1e-4f);
}

TEST(VisualLFOTest, SampleAndHoldChangesAtBoundary) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::SampleAndHold;
    lfo.rate    = 1.0f;
    lfo.depth   = 1.0f;
    lfo.sync    = true;
    // Two samples inside the same cycle must return the same value —
    // S&H only jumps at integer cycle boundaries.
    float a = lfo.evaluate(0.10, 0.0);
    float b = lfo.evaluate(0.90, 0.0);
    EXPECT_FLOAT_EQ(a, b);
    // Crossing to the next cycle should flip to a new deterministic sample.
    float c = lfo.evaluate(1.10, 0.0);
    EXPECT_NE(a, c);
    // And that next-cycle value is also held.
    EXPECT_FLOAT_EQ(c, lfo.evaluate(1.90, 0.0));
}

TEST(VisualLFOTest, FreeModeUsesWallClock) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::Sine;
    lfo.rate    = 1.0f;   // in free mode: Hz
    lfo.depth   = 1.0f;
    lfo.sync    = false;
    // Quarter-second at 1 Hz is a quarter cycle → +1.
    EXPECT_NEAR(lfo.evaluate(0.0, 0.25), 1.0f, 1e-4f);
    // transportBeats parameter is ignored in free mode.
    EXPECT_NEAR(lfo.evaluate(100.0, 0.25), 1.0f, 1e-4f);
}

TEST(VisualLFOTest, PhaseOffsetShifts) {
    VisualLFO lfo;
    lfo.enabled = true;
    lfo.shape   = VisualLFO::Sine;
    lfo.rate    = 1.0f;
    lfo.depth   = 1.0f;
    lfo.sync    = true;
    lfo.phase   = 0.25f;  // +quarter cycle — sine at 0 becomes +1.
    EXPECT_NEAR(lfo.evaluate(0.0, 0.0), 1.0f, 1e-4f);
}
