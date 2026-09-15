// PlateReverb tests: impulse tail, silence gate, parameter extremes.
#include <gtest/gtest.h>
#include "effects/PlateReverb.h"
#include <cmath>
#include <vector>

using namespace yawn::effects;

namespace {
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

std::vector<float> makeBlock() { return std::vector<float>(kBlock * 2, 0.0f); }

float blockPeak(const std::vector<float>& b) {
    float p = 0.0f;
    for (float v : b) p = std::max(p, std::fabs(v));
    return p;
}
bool blockFinite(const std::vector<float>& b) {
    for (float v : b)
        if (!std::isfinite(v)) return false;
    return true;
}
} // namespace

TEST(PlateReverb, Init) {
    PlateReverb p;
    p.init(kSr, kBlock);
    EXPECT_STREQ(p.name(), "Plate Reverb");
    EXPECT_STREQ(p.id(), "plate");
    EXPECT_EQ(p.parameterCount(), 6);
    // formatFns present on all params (normalized 0..1 display)
    for (int i = 0; i < p.parameterCount(); ++i)
        EXPECT_NE(p.parameterInfo(i).formatFn, nullptr) << "param " << i;
}

TEST(PlateReverb, BurstProducesDecayingTail) {
    PlateReverb p;
    p.init(kSr, kBlock);
    p.setParameter(PlateReverb::kWetDry, 1.0f);
    p.setParameter(PlateReverb::kPreDelay, 0.0f);
    p.setParameter(PlateReverb::kDecay, 0.677f); // ~2 s RT60 target

    // A 256-frame burst (a plate is built for this, not single-sample
    // impulses — diffusion smears those below the tap floor).
    auto burst = makeBlock();
    for (int i = 0; i < kBlock; ++i) {
        const float v = float(0.5 * std::sin(2.0 * 3.14159265 * 440.0 *
                                             double(i) / kSr));
        burst[size_t(i) * 2] = burst[size_t(i) * 2 + 1] = v;
    }
    p.process(burst.data(), kBlock, 2);
    EXPECT_TRUE(blockFinite(burst));

    // ~2 s of silence; the tank must sound well past the burst, decay,
    // and never go NaN/Inf. (First tank output arrives ~75-125 ms
    // after the input: input diffusers + modulated APF + long delay.)
    float early = 0.0f, late = 0.0f;
    const int blocks = int(kSr * 2.0) / kBlock;
    for (int b = 0; b < blocks; ++b) {
        auto buf = makeBlock();
        p.process(buf.data(), kBlock, 2);
        ASSERT_TRUE(blockFinite(buf)) << "non-finite at block " << b;
        const float pk = blockPeak(buf);
        if (b >= 6 && b <= 18)   early = std::max(early, pk); // 30-200 ms
        if (b >= blocks - 12)    late  = std::max(late, pk);  // ~1.8-2.0 s
    }
    EXPECT_GT(early, 1e-3f) << "no audible tail after burst";
    EXPECT_GT(late, 1e-6f) << "tail died long before the RT60 target";
    EXPECT_GT(early, late) << "tail did not decay";
}

TEST(PlateReverb, SilenceGateOutputsExactZeros) {
    PlateReverb p;
    p.init(kSr, kBlock);
    p.setParameter(PlateReverb::kWetDry, 1.0f);
    p.setParameter(PlateReverb::kPreDelay, 0.0f);
    p.setParameter(PlateReverb::kDecay, 0.0f); // shortest tail (~0.2 s)
    p.setParameter(PlateReverb::kDamping, 1.0f);

    auto impulse = makeBlock();
    impulse[0] = 1.0f;
    p.process(impulse.data(), kBlock, 2);

    // 4 s of silence: the gate (input < -100 dBFS past the predelay,
    // tail proxy < -100 dBFS) must engage and output exact zeros.
    const int blocks = int(kSr * 4.0) / kBlock;
    for (int b = 0; b < blocks; ++b) {
        auto buf = makeBlock();
        p.process(buf.data(), kBlock, 2);
        if (b >= blocks / 2) // last 2 s: gated
            for (float v : buf)
                EXPECT_EQ(v, 0.0f) << "gate leak at block " << b;
    }
}

TEST(PlateReverb, ParameterExtremesDontCrash) {
    PlateReverb p;
    p.init(kSr, kBlock);
    for (int i = 0; i < p.parameterCount(); ++i) p.setParameter(i, 0.0f);
    for (int b = 0; b < 16; ++b) {
        auto buf = makeBlock();
        for (int i = 0; i < kBlock * 2; ++i)
            buf[size_t(i)] = (i % 7 == 0) ? 0.5f : 0.0f; // sparse noise
        p.process(buf.data(), kBlock, 2);
        ASSERT_TRUE(blockFinite(buf)) << "params=0, block " << b;
    }
    for (int i = 0; i < p.parameterCount(); ++i) p.setParameter(i, 1.0f);
    for (int b = 0; b < 16; ++b) {
        auto buf = makeBlock();
        for (int i = 0; i < kBlock * 2; ++i)
            buf[size_t(i)] = (i % 7 == 0) ? 0.5f : 0.0f;
        p.process(buf.data(), kBlock, 2);
        ASSERT_TRUE(blockFinite(buf)) << "params=1, block " << b;
    }
}

TEST(PlateReverb, BypassPassesThrough) {
    PlateReverb p;
    p.init(kSr, kBlock);
    p.setBypassed(true);
    auto buf = makeBlock();
    buf[0] = 0.25f;
    buf[1] = -0.25f;
    p.process(buf.data(), kBlock, 2);
    EXPECT_FLOAT_EQ(buf[0], 0.25f);
    EXPECT_FLOAT_EQ(buf[1], -0.25f);
}
