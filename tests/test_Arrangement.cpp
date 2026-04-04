#include <gtest/gtest.h>
#include "app/ArrangementClip.h"
#include "app/Project.h"

using namespace yawn;

// ── ArrangementClip ────────────────────────────────────────────────────────

TEST(ArrangementClip, EndBeat) {
    ArrangementClip c;
    c.startBeat = 4.0;
    c.lengthBeats = 8.0;
    EXPECT_DOUBLE_EQ(c.endBeat(), 12.0);
}

TEST(ArrangementClip, OverlapsTrue) {
    ArrangementClip c;
    c.startBeat = 4.0;
    c.lengthBeats = 4.0;
    EXPECT_TRUE(c.overlaps(2.0, 6.0));
    EXPECT_TRUE(c.overlaps(5.0, 10.0));
    EXPECT_TRUE(c.overlaps(4.0, 8.0));
}

TEST(ArrangementClip, OverlapsFalse) {
    ArrangementClip c;
    c.startBeat = 4.0;
    c.lengthBeats = 4.0;
    EXPECT_FALSE(c.overlaps(0.0, 4.0));   // touching but not overlapping
    EXPECT_FALSE(c.overlaps(8.0, 12.0));
    EXPECT_FALSE(c.overlaps(10.0, 20.0));
}

// ── Track arrangement ──────────────────────────────────────────────────────

TEST(TrackArrangement, DefaultsToSessionMode) {
    Track t;
    EXPECT_FALSE(t.arrangementActive);
    EXPECT_TRUE(t.arrangementClips.empty());
}

TEST(TrackArrangement, ArrangementClipAt) {
    Track t;
    ArrangementClip c1;
    c1.startBeat = 0.0;
    c1.lengthBeats = 4.0;
    c1.name = "c1";
    ArrangementClip c2;
    c2.startBeat = 8.0;
    c2.lengthBeats = 4.0;
    c2.name = "c2";
    t.arrangementClips = {c1, c2};

    EXPECT_EQ(t.arrangementClipAt(0.0)->name, "c1");
    EXPECT_EQ(t.arrangementClipAt(3.99)->name, "c1");
    EXPECT_EQ(t.arrangementClipAt(4.0), nullptr);  // gap
    EXPECT_EQ(t.arrangementClipAt(8.0)->name, "c2");
    EXPECT_EQ(t.arrangementClipAt(12.0), nullptr);  // past end
}

TEST(TrackArrangement, SortArrangementClips) {
    Track t;
    ArrangementClip a, b, c;
    a.startBeat = 12.0; a.name = "a";
    b.startBeat = 0.0;  b.name = "b";
    c.startBeat = 4.0;  c.name = "c";
    t.arrangementClips = {a, b, c};
    t.sortArrangementClips();

    EXPECT_EQ(t.arrangementClips[0].name, "b");
    EXPECT_EQ(t.arrangementClips[1].name, "c");
    EXPECT_EQ(t.arrangementClips[2].name, "a");
}

// ── Project arrangement ────────────────────────────────────────────────────

TEST(ProjectArrangement, DefaultViewMode) {
    Project p;
    p.init();
    EXPECT_EQ(p.viewMode(), ViewMode::Session);
}

TEST(ProjectArrangement, SetViewMode) {
    Project p;
    p.init();
    p.setViewMode(ViewMode::Arrangement);
    EXPECT_EQ(p.viewMode(), ViewMode::Arrangement);
}

TEST(ProjectArrangement, ArrangementLength) {
    Project p;
    p.init();
    EXPECT_DOUBLE_EQ(p.arrangementLength(), 64.0);
    p.setArrangementLength(128.0);
    EXPECT_DOUBLE_EQ(p.arrangementLength(), 128.0);
}

TEST(ProjectArrangement, UpdateArrangementLength) {
    Project p;
    p.init(2, 2);

    ArrangementClip c;
    c.startBeat = 100.0;
    c.lengthBeats = 16.0;
    p.track(0).arrangementClips.push_back(c);

    p.updateArrangementLength();
    EXPECT_DOUBLE_EQ(p.arrangementLength(), 116.0);
}
