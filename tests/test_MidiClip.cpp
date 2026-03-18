#include <gtest/gtest.h>
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"

using namespace yawn::midi;

// ===========================================================================
// MidiClip basic operations
// ===========================================================================

TEST(MidiClip, DefaultState) {
    MidiClip clip;
    EXPECT_EQ(clip.noteCount(), 0);
    EXPECT_EQ(clip.ccCount(), 0);
    EXPECT_DOUBLE_EQ(clip.lengthBeats(), 4.0);
    EXPECT_TRUE(clip.loop());
}

TEST(MidiClip, AddNotesSorted) {
    MidiClip clip;

    MidiNote n1;
    n1.startBeat = 2.0;
    n1.pitch = 60;
    n1.velocity = Convert::vel7to16(100);

    MidiNote n2;
    n2.startBeat = 1.0;
    n2.pitch = 64;
    n2.velocity = Convert::vel7to16(80);

    clip.addNote(n1);
    clip.addNote(n2);

    EXPECT_EQ(clip.noteCount(), 2);
    EXPECT_DOUBLE_EQ(clip.note(0).startBeat, 1.0); // n2 first (sorted)
    EXPECT_DOUBLE_EQ(clip.note(1).startBeat, 2.0); // n1 second
}

TEST(MidiClip, RemoveNote) {
    MidiClip clip;
    MidiNote n;
    n.startBeat = 0.0;
    n.pitch = 60;
    clip.addNote(n);
    EXPECT_EQ(clip.noteCount(), 1);

    clip.removeNote(0);
    EXPECT_EQ(clip.noteCount(), 0);
}

TEST(MidiClip, GetNotesInRange) {
    MidiClip clip;
    for (int i = 0; i < 8; ++i) {
        MidiNote n;
        n.startBeat = static_cast<double>(i) * 0.5;
        n.pitch = 60 + i;
        clip.addNote(n);
    }

    std::vector<int> indices;
    clip.getNotesInRange(1.0, 2.5, indices);

    // Notes at beats 1.0, 1.5, 2.0 should be returned (2.5 is exclusive)
    EXPECT_EQ(indices.size(), 3u);
    EXPECT_EQ(clip.note(indices[0]).pitch, 62); // beat 1.0
    EXPECT_EQ(clip.note(indices[1]).pitch, 63); // beat 1.5
    EXPECT_EQ(clip.note(indices[2]).pitch, 64); // beat 2.0
}

TEST(MidiClip, GetActiveNotesAt) {
    MidiClip clip;

    MidiNote n1;
    n1.startBeat = 0.0;
    n1.duration = 2.0;
    n1.pitch = 60;
    clip.addNote(n1);

    MidiNote n2;
    n2.startBeat = 1.0;
    n2.duration = 1.0;
    n2.pitch = 64;
    clip.addNote(n2);

    std::vector<int> indices;

    clip.getActiveNotesAt(0.5, indices);
    EXPECT_EQ(indices.size(), 1u); // Only n1 active

    clip.getActiveNotesAt(1.5, indices);
    EXPECT_EQ(indices.size(), 2u); // Both active

    clip.getActiveNotesAt(2.5, indices);
    EXPECT_EQ(indices.size(), 0u); // Neither active
}

// ===========================================================================
// CC Automation
// ===========================================================================

TEST(MidiClip, AddCCSorted) {
    MidiClip clip;

    MidiCCEvent cc1;
    cc1.beat = 3.0;
    cc1.ccNumber = 1;
    cc1.value = Convert::cc7to32(100);

    MidiCCEvent cc2;
    cc2.beat = 1.0;
    cc2.ccNumber = 1;
    cc2.value = Convert::cc7to32(50);

    clip.addCC(cc1);
    clip.addCC(cc2);

    EXPECT_EQ(clip.ccCount(), 2);
    EXPECT_DOUBLE_EQ(clip.ccEvent(0).beat, 1.0);
    EXPECT_DOUBLE_EQ(clip.ccEvent(1).beat, 3.0);
}

TEST(MidiClip, GetCCInRange) {
    MidiClip clip;
    for (int i = 0; i < 4; ++i) {
        MidiCCEvent cc;
        cc.beat = static_cast<double>(i);
        cc.ccNumber = 7;
        cc.value = Convert::cc7to32(static_cast<uint8_t>(i * 30));
        clip.addCC(cc);
    }

    std::vector<int> indices;
    clip.getCCInRange(1.0, 3.0, indices);
    EXPECT_EQ(indices.size(), 2u); // beats 1.0 and 2.0
}

// ===========================================================================
// Properties
// ===========================================================================

TEST(MidiClip, Properties) {
    MidiClip clip(8.0);
    EXPECT_DOUBLE_EQ(clip.lengthBeats(), 8.0);

    clip.setLengthBeats(16.0);
    EXPECT_DOUBLE_EQ(clip.lengthBeats(), 16.0);

    clip.setLoop(false);
    EXPECT_FALSE(clip.loop());

    clip.setName("Bass Line");
    EXPECT_EQ(clip.name(), "Bass Line");
}

// ===========================================================================
// Per-note expression (MPE / MIDI 2.0)
// ===========================================================================

TEST(MidiClip, PerNoteExpression) {
    MidiClip clip;
    MidiNote n;
    n.startBeat = 0.0;
    n.duration = 1.0;
    n.pitch = 60;
    n.velocity = Convert::vel7to16(100);
    n.releaseVelocity = Convert::vel7to16(64);
    n.pressure = 0.8f;
    n.slide = 0.3f;
    n.pitchBendOffset = -0.5f;

    clip.addNote(n);

    const auto& stored = clip.note(0);
    EXPECT_FLOAT_EQ(stored.pressure, 0.8f);
    EXPECT_FLOAT_EQ(stored.slide, 0.3f);
    EXPECT_FLOAT_EQ(stored.pitchBendOffset, -0.5f);
    EXPECT_EQ(stored.releaseVelocity, Convert::vel7to16(64));
}
