// URL-scheme parsing for LiveVideoSource. The rest of the class is
// I/O heavy and gets covered manually; the parser is pure logic so
// we pin its behaviour here.

#include <gtest/gtest.h>
#include "visual/LiveVideoSource.h"

using namespace yawn::visual;

TEST(LiveVideoSourceTest, V4L2UrlSplit) {
    auto p = parseLiveUrl("v4l2:///dev/video0");
    EXPECT_EQ(p.inputFormat, "v4l2");
    EXPECT_EQ(p.filename,    "/dev/video0");
}

TEST(LiveVideoSourceTest, DshowUrlSplit) {
    auto p = parseLiveUrl("dshow://video=USB Camera");
    EXPECT_EQ(p.inputFormat, "dshow");
    EXPECT_EQ(p.filename,    "video=USB Camera");
}

TEST(LiveVideoSourceTest, AVFoundationUrlSplit) {
    auto p = parseLiveUrl("avfoundation://0:none");
    EXPECT_EQ(p.inputFormat, "avfoundation");
    EXPECT_EQ(p.filename,    "0:none");
}

TEST(LiveVideoSourceTest, RtspPassesThrough) {
    auto p = parseLiveUrl("rtsp://cam.local:554/stream");
    EXPECT_TRUE(p.inputFormat.empty());
    EXPECT_EQ(p.filename, "rtsp://cam.local:554/stream");
}

TEST(LiveVideoSourceTest, HttpPassesThrough) {
    auto p = parseLiveUrl("http://example.com/live.ts");
    EXPECT_TRUE(p.inputFormat.empty());
    EXPECT_EQ(p.filename, "http://example.com/live.ts");
}

TEST(LiveVideoSourceTest, BarePathPassesThrough) {
    auto p = parseLiveUrl("/dev/video1");
    EXPECT_TRUE(p.inputFormat.empty());
    EXPECT_EQ(p.filename, "/dev/video1");
}

TEST(LiveVideoSourceTest, EmptyUrl) {
    auto p = parseLiveUrl("");
    EXPECT_TRUE(p.inputFormat.empty());
    EXPECT_TRUE(p.filename.empty());
}
