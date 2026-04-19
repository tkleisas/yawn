// VisualClip round-trip (clone) tests — covers all the visual state we
// persist through the serializer so a regression here flags broken
// clone() or a missing field in clone().

#include <gtest/gtest.h>
#include "visual/VisualClip.h"

using namespace yawn::visual;

TEST(VisualClipTest, DefaultsAreSane) {
    VisualClip c;
    EXPECT_TRUE(c.shaderPath.empty());
    EXPECT_EQ(c.colorIndex, 0);
    EXPECT_EQ(c.audioSource, -1);
    EXPECT_TRUE(c.paramValues.empty());
    EXPECT_TRUE(c.text.empty());
    EXPECT_TRUE(c.videoPath.empty());
    EXPECT_TRUE(c.thumbnailPath.empty());
    EXPECT_TRUE(c.videoSourcePath.empty());
    EXPECT_EQ(c.videoLoopBars, 0);
    EXPECT_FLOAT_EQ(c.videoRate, 1.0f);
    EXPECT_FLOAT_EQ(c.videoIn,  0.0f);
    EXPECT_FLOAT_EQ(c.videoOut, 1.0f);
    for (auto& l : c.knobLFOs) {
        EXPECT_FALSE(l.enabled);
    }
}

TEST(VisualClipTest, ClonePreservesAllFields) {
    VisualClip c;
    c.shaderPath       = "/a/b.frag";
    c.name             = "demo";
    c.colorIndex       = 7;
    c.lengthBeats      = 8.0;
    c.audioSource      = 3;
    c.paramValues      = {{"speed", 0.75f}, {"knobA", 0.2f}};
    c.text             = "LIVE";
    c.videoPath        = "/media/x.mp4";
    c.thumbnailPath    = "/media/x_thumb.jpg";
    c.videoSourcePath  = "/orig/clip.mov";
    c.videoLoopBars    = 4;
    c.videoRate        = 2.0f;
    c.videoIn          = 0.25f;
    c.videoOut         = 0.75f;
    c.knobLFOs[0].enabled = true;
    c.knobLFOs[0].shape   = 2;
    c.knobLFOs[0].rate    = 1.0f;
    c.knobLFOs[0].depth   = 0.5f;
    c.knobLFOs[0].sync    = true;

    auto clone = c.clone();
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->shaderPath,       c.shaderPath);
    EXPECT_EQ(clone->name,             c.name);
    EXPECT_EQ(clone->colorIndex,       c.colorIndex);
    EXPECT_DOUBLE_EQ(clone->lengthBeats, c.lengthBeats);
    EXPECT_EQ(clone->audioSource,      c.audioSource);
    ASSERT_EQ(clone->paramValues.size(), c.paramValues.size());
    for (size_t i = 0; i < c.paramValues.size(); ++i) {
        EXPECT_EQ(clone->paramValues[i].first,  c.paramValues[i].first);
        EXPECT_FLOAT_EQ(clone->paramValues[i].second, c.paramValues[i].second);
    }
    EXPECT_EQ(clone->text,             c.text);
    EXPECT_EQ(clone->videoPath,        c.videoPath);
    EXPECT_EQ(clone->thumbnailPath,    c.thumbnailPath);
    EXPECT_EQ(clone->videoSourcePath,  c.videoSourcePath);
    EXPECT_EQ(clone->videoLoopBars,    c.videoLoopBars);
    EXPECT_FLOAT_EQ(clone->videoRate,  c.videoRate);
    EXPECT_FLOAT_EQ(clone->videoIn,    c.videoIn);
    EXPECT_FLOAT_EQ(clone->videoOut,   c.videoOut);
    EXPECT_TRUE (clone->knobLFOs[0].enabled);
    EXPECT_EQ   (clone->knobLFOs[0].shape, 2);
    EXPECT_FLOAT_EQ(clone->knobLFOs[0].rate,  c.knobLFOs[0].rate);
    EXPECT_FLOAT_EQ(clone->knobLFOs[0].depth, c.knobLFOs[0].depth);
    EXPECT_TRUE (clone->knobLFOs[0].sync);
    // Unchanged slots stay disabled.
    EXPECT_FALSE(clone->knobLFOs[1].enabled);
}
