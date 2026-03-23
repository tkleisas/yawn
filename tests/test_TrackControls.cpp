#include <gtest/gtest.h>
#include "util/MessageQueue.h"
#include "app/Project.h"

using namespace yawn;
using namespace yawn::audio;

// --- MonitorMode model tests ---

TEST(TrackControls, MonitorModeDefaultAuto) {
    Track t;
    EXPECT_EQ(t.monitorMode, Track::MonitorMode::Auto);
}

TEST(TrackControls, MonitorModeCycle) {
    Track t;
    EXPECT_EQ(t.monitorMode, Track::MonitorMode::Auto);
    t.monitorMode = Track::MonitorMode::In;
    EXPECT_EQ(t.monitorMode, Track::MonitorMode::In);
    t.monitorMode = Track::MonitorMode::Off;
    EXPECT_EQ(t.monitorMode, Track::MonitorMode::Off);
    t.monitorMode = Track::MonitorMode::Auto;
    EXPECT_EQ(t.monitorMode, Track::MonitorMode::Auto);
}

TEST(TrackControls, MonitorModeEnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(Track::MonitorMode::Auto), 0);
    EXPECT_EQ(static_cast<uint8_t>(Track::MonitorMode::In), 1);
    EXPECT_EQ(static_cast<uint8_t>(Track::MonitorMode::Off), 2);
}

// --- SetTrackMonitorMsg tests ---

TEST(TrackControls, SetTrackMonitorMsg) {
    CommandQueue q;
    q.push(AudioCommand{SetTrackMonitorMsg{3, 1}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<SetTrackMonitorMsg>(out));
    auto& msg = std::get<SetTrackMonitorMsg>(out);
    EXPECT_EQ(msg.trackIndex, 3);
    EXPECT_EQ(msg.mode, 1);
}

// --- SetTrackTypeMsg tests ---

TEST(TrackControls, SetTrackTypeMsgAudio) {
    CommandQueue q;
    q.push(AudioCommand{SetTrackTypeMsg{0, 0}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<SetTrackTypeMsg>(out));
    auto& msg = std::get<SetTrackTypeMsg>(out);
    EXPECT_EQ(msg.trackIndex, 0);
    EXPECT_EQ(msg.type, 0); // Audio
}

TEST(TrackControls, SetTrackTypeMsgMidi) {
    CommandQueue q;
    q.push(AudioCommand{SetTrackTypeMsg{5, 1}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<SetTrackTypeMsg>(out));
    auto& msg = std::get<SetTrackTypeMsg>(out);
    EXPECT_EQ(msg.trackIndex, 5);
    EXPECT_EQ(msg.type, 1); // Midi
}

// --- Track type defaults ---

TEST(TrackControls, TrackDefaultTypeAudio) {
    Track t;
    EXPECT_EQ(t.type, Track::Type::Audio);
}

TEST(TrackControls, TrackTypeMidi) {
    Track t;
    t.type = Track::Type::Midi;
    EXPECT_EQ(t.type, Track::Type::Midi);
}

// --- Project track type enforcement ---

TEST(TrackControls, ProjectTrackTypePreserved) {
    Project p;
    p.init(2, 2);
    p.track(0).type = Track::Type::Audio;
    p.track(1).type = Track::Type::Midi;
    EXPECT_EQ(p.track(0).type, Track::Type::Audio);
    EXPECT_EQ(p.track(1).type, Track::Type::Midi);
}

TEST(TrackControls, AddTrackWithType) {
    Project p;
    p.init(0, 4);
    p.addTrack("Audio 1", Track::Type::Audio);
    p.addTrack("MIDI 1", Track::Type::Midi);
    EXPECT_EQ(p.numTracks(), 2);
    EXPECT_EQ(p.track(0).type, Track::Type::Audio);
    EXPECT_EQ(p.track(1).type, Track::Type::Midi);
}

// --- Monitoring mode in project ---

TEST(TrackControls, ProjectMonitorModePerTrack) {
    Project p;
    p.init(3, 2);
    p.track(0).monitorMode = Track::MonitorMode::Auto;
    p.track(1).monitorMode = Track::MonitorMode::In;
    p.track(2).monitorMode = Track::MonitorMode::Off;
    EXPECT_EQ(p.track(0).monitorMode, Track::MonitorMode::Auto);
    EXPECT_EQ(p.track(1).monitorMode, Track::MonitorMode::In);
    EXPECT_EQ(p.track(2).monitorMode, Track::MonitorMode::Off);
}

// --- Clip slot icon zone ---

TEST(TrackControls, ClipSlotEmptyState) {
    Project p;
    p.init(1, 1);
    auto* slot = p.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->empty());
}

TEST(TrackControls, ClipSlotArmedRecordReady) {
    Project p;
    p.init(1, 1);
    p.track(0).armed = true;
    auto* slot = p.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->empty());
    EXPECT_TRUE(p.track(0).armed);
    // When armed + global record + empty slot → record-ready state
}
