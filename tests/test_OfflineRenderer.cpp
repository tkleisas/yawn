#include <gtest/gtest.h>
#include "audio/OfflineRenderer.h"
#include "audio/AudioEngine.h"
#include "audio/Transport.h"
#include "core/Constants.h"

using namespace yawn;
using namespace yawn::audio;

// ==================== RenderConfig ====================

TEST(OfflineRenderer, RenderConfigDefaults) {
    RenderConfig cfg;
    EXPECT_DOUBLE_EQ(cfg.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(cfg.endBeat, 0.0);
    EXPECT_EQ(cfg.targetSampleRate, static_cast<int>(kDefaultSampleRate));
    EXPECT_EQ(cfg.channels, 2);
}

TEST(OfflineRenderer, RenderConfigCustom) {
    RenderConfig cfg{4.0, 16.0, 48000, 2};
    EXPECT_DOUBLE_EQ(cfg.startBeat, 4.0);
    EXPECT_DOUBLE_EQ(cfg.endBeat, 16.0);
    EXPECT_EQ(cfg.targetSampleRate, 48000);
    EXPECT_EQ(cfg.channels, 2);
}

// ==================== RenderProgress ====================

TEST(OfflineRenderer, RenderProgressInitialState) {
    RenderProgress prog;
    EXPECT_FLOAT_EQ(prog.fraction.load(), 0.0f);
    EXPECT_FALSE(prog.done.load());
    EXPECT_FALSE(prog.cancelled.load());
    EXPECT_FALSE(prog.failed.load());
}

TEST(OfflineRenderer, RenderProgressCancelFlag) {
    RenderProgress prog;
    EXPECT_FALSE(prog.cancelled.load());
    prog.cancelled.store(true);
    EXPECT_TRUE(prog.cancelled.load());
}

TEST(OfflineRenderer, RenderProgressFractionUpdate) {
    RenderProgress prog;
    prog.fraction.store(0.5f);
    EXPECT_FLOAT_EQ(prog.fraction.load(), 0.5f);
    prog.fraction.store(1.0f);
    EXPECT_FLOAT_EQ(prog.fraction.load(), 1.0f);
}

TEST(OfflineRenderer, RenderProgressDoneFlag) {
    RenderProgress prog;
    EXPECT_FALSE(prog.done.load());
    prog.done.store(true);
    EXPECT_TRUE(prog.done.load());
}

TEST(OfflineRenderer, RenderProgressFailedFlag) {
    RenderProgress prog;
    EXPECT_FALSE(prog.failed.load());
    prog.failed.store(true);
    EXPECT_TRUE(prog.failed.load());
}

// ==================== Render Failure Cases ====================

TEST(OfflineRenderer, RenderWithEmptyRange) {
    AudioEngine engine;

    RenderConfig cfg{0.0, 0.0};
    RenderProgress prog;

    auto result = OfflineRenderer::render(engine, cfg, prog);
    EXPECT_EQ(result, nullptr);
    EXPECT_TRUE(prog.failed.load());
}

TEST(OfflineRenderer, RenderWithNegativeRange) {
    AudioEngine engine;

    RenderConfig cfg{8.0, 4.0};
    RenderProgress prog;

    auto result = OfflineRenderer::render(engine, cfg, prog);
    EXPECT_EQ(result, nullptr);
    EXPECT_TRUE(prog.failed.load());
}

TEST(OfflineRenderer, RenderProgressAfterFailure) {
    AudioEngine engine;

    RenderConfig cfg{10.0, 5.0};
    RenderProgress prog;

    OfflineRenderer::render(engine, cfg, prog);

    EXPECT_FLOAT_EQ(prog.fraction.load(), 0.0f);
    EXPECT_TRUE(prog.failed.load());
    EXPECT_TRUE(prog.done.load());
}
