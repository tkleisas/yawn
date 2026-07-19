#include <gtest/gtest.h>
#include "ui/framework/v2/FMAlgorithmWidget.h"

using namespace yawn::ui::fw2;

namespace {

bool rectsOverlap(float x1, float y1, float w1, float h1,
                  float x2, float y2, float w2, float h2) {
    return x1 < x2 + w2 && x2 < x1 + w1 &&
           y1 < y2 + h2 && y2 < y1 + h1;
}

// Regression: the FM algorithm diagram used fixed 28x22 boxes in a
// 32 px-tall area, so stacked operators overlapped into an unreadable
// pile. The layout must never overlap boxes, for every algorithm and
// any reasonable area size.
TEST(FMAlgoLayout, NoOverlapAllAlgos) {
    const float areas[][2] = {{122, 66}, {122, 32}, {80, 40}, {200, 100}};
    for (const auto& a : areas) {
        for (int algo = 0; algo < 8; ++algo) {
            const FMAlgoLayout L = computeFMAlgoLayout(kFMAlgos[algo],
                                                        0, 0, a[0], a[1]);
            for (int i = 0; i < 4; ++i) {
                for (int j = i + 1; j < 4; ++j) {
                    EXPECT_FALSE(rectsOverlap(
                        L.x[i], L.y[i], L.boxW, L.boxH,
                        L.x[j], L.y[j], L.boxW, L.boxH))
                        << "algo " << algo << " ops " << i << "/" << j
                        << " area " << a[0] << "x" << a[1];
                }
            }
        }
    }
}

TEST(FMAlgoLayout, BoxesInsideArea) {
    const float ax = 4, ay = 18, aw = 122, ah = 66;
    for (int algo = 0; algo < 8; ++algo) {
        const FMAlgoLayout L = computeFMAlgoLayout(kFMAlgos[algo],
                                                    ax, ay, aw, ah);
        for (int op = 0; op < 4; ++op) {
            EXPECT_GE(L.x[op], ax - 0.01f) << "algo " << algo << " op " << op;
            EXPECT_LE(L.x[op] + L.boxW, ax + aw + 0.01f) << "algo " << algo << " op " << op;
            EXPECT_GE(L.y[op], ay - 0.01f) << "algo " << algo << " op " << op;
            EXPECT_LE(L.y[op] + L.boxH, ay + ah + 0.01f) << "algo " << algo << " op " << op;
        }
    }
}

TEST(FMAlgoLayout, ShrinksBoxesForDeepStacks) {
    // Algo 0 is a 4-deep serial chain; algo 4 is a single row of
    // carriers. In the same short area the deep stack must get
    // shorter boxes (down to the 8 px floor) — and both must fit.
    const FMAlgoLayout deep = computeFMAlgoLayout(kFMAlgos[0], 0, 0, 122, 40);
    const FMAlgoLayout flat = computeFMAlgoLayout(kFMAlgos[4], 0, 0, 122, 40);
    EXPECT_LT(deep.boxH, flat.boxH);
    EXPECT_LE(deep.boxH, 8.0f + 0.01f);   // floor clamp engaged
    EXPECT_EQ(flat.boxH, 22.0f);           // single row: full size
}

TEST(FMAlgoLayout, CarriersSitBelowModulators) {
    // Algo 0: serial 4→3→2→1(C) — op 1 is the carrier at the bottom.
    const FMAlgoLayout L = computeFMAlgoLayout(kFMAlgos[0], 0, 0, 122, 66);
    EXPECT_GT(L.y[0], L.y[1]);
    EXPECT_GT(L.y[1], L.y[2]);
    EXPECT_GT(L.y[2], L.y[3]);
}

TEST(FMAlgoLayout, SingleRowIsVerticallyCentered) {
    // Algo 4 (all carriers) is a single row — centered in the area
    // rather than top-aligned with dead space above the OUT marker.
    const float ah = 66;
    const FMAlgoLayout L = computeFMAlgoLayout(kFMAlgos[4], 0, 0, 122, ah);
    const float wantY = (ah - L.boxH) * 0.5f;
    for (int op = 0; op < 4; ++op)
        EXPECT_NEAR(L.y[op], wantY, 0.01f) << "op " << op;
}

} // namespace
