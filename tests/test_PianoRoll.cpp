#include <gtest/gtest.h>
#include "midi/MidiClip.h"
#include "audio/Transport.h"

using namespace yawn;
using namespace yawn::midi;

class PianoRollDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_clip.setLengthBeats(4.0);
        m_clip.setName("Test Clip");
        m_clip.setLoop(true);
    }

    MidiClip m_clip;
};

TEST_F(PianoRollDataTest, ClipBasicProperties) {
    EXPECT_DOUBLE_EQ(m_clip.lengthBeats(), 4.0);
    EXPECT_EQ(m_clip.name(), "Test Clip");
    EXPECT_TRUE(m_clip.loop());
    EXPECT_EQ(m_clip.noteCount(), 0);
}

TEST_F(PianoRollDataTest, SetClipLength) {
    m_clip.setLengthBeats(8.0);
    EXPECT_DOUBLE_EQ(m_clip.lengthBeats(), 8.0);
    // Minimum length
    m_clip.setLengthBeats(0.0);
    EXPECT_DOUBLE_EQ(m_clip.lengthBeats(), 0.25);
}

TEST_F(PianoRollDataTest, SingleNoteAdd) {
    MidiNote n{};
    n.startBeat = 0.0;
    n.duration = 0.25;
    n.pitch = 60;
    n.velocity = 32512;
    m_clip.addNote(n);

    EXPECT_EQ(m_clip.noteCount(), 1);
    EXPECT_DOUBLE_EQ(m_clip.note(0).startBeat, 0.0);
    EXPECT_DOUBLE_EQ(m_clip.note(0).duration, 0.25);
    EXPECT_EQ(m_clip.note(0).pitch, 60);
}

TEST_F(PianoRollDataTest, NoteAddRemove) {
    MidiNote n1{};
    n1.startBeat = 0.0; n1.pitch = 60; n1.duration = 0.5;
    MidiNote n2{};
    n2.startBeat = 1.0; n2.pitch = 64; n2.duration = 0.25;
    MidiNote n3{};
    n3.startBeat = 2.0; n3.pitch = 67; n3.duration = 1.0;

    m_clip.addNote(n1);
    m_clip.addNote(n2);
    m_clip.addNote(n3);
    EXPECT_EQ(m_clip.noteCount(), 3);

    // Notes should be sorted by startBeat
    EXPECT_DOUBLE_EQ(m_clip.note(0).startBeat, 0.0);
    EXPECT_DOUBLE_EQ(m_clip.note(1).startBeat, 1.0);
    EXPECT_DOUBLE_EQ(m_clip.note(2).startBeat, 2.0);

    // Remove middle note
    m_clip.removeNote(1);
    EXPECT_EQ(m_clip.noteCount(), 2);
    EXPECT_DOUBLE_EQ(m_clip.note(0).startBeat, 0.0);
    EXPECT_DOUBLE_EQ(m_clip.note(1).startBeat, 2.0);
}

TEST_F(PianoRollDataTest, AutoExtendClip) {
    // When a note extends past the clip end, clip should accommodate it
    m_clip.setLengthBeats(4.0);
    MidiNote n{};
    n.startBeat = 3.5;
    n.duration = 2.0; // ends at 5.5, past clip end
    n.pitch = 60;
    m_clip.addNote(n);

    // Simulate what the piano roll does
    double noteEnd = n.startBeat + n.duration;
    if (noteEnd > m_clip.lengthBeats())
        m_clip.setLengthBeats(noteEnd);

    EXPECT_DOUBLE_EQ(m_clip.lengthBeats(), 5.5);
}

TEST_F(PianoRollDataTest, NoteSortingAfterMove) {
    MidiNote n1{}; n1.startBeat = 0.0; n1.pitch = 60;
    MidiNote n2{}; n2.startBeat = 1.0; n2.pitch = 64;
    MidiNote n3{}; n3.startBeat = 2.0; n3.pitch = 67;
    m_clip.addNote(n1);
    m_clip.addNote(n2);
    m_clip.addNote(n3);

    // Simulate moving note at index 0 to beat 3.0 (remove and re-add)
    auto moved = m_clip.note(0);
    m_clip.removeNote(0);
    moved.startBeat = 3.0;
    m_clip.addNote(moved);

    EXPECT_EQ(m_clip.noteCount(), 3);
    // Should now be sorted: 1.0, 2.0, 3.0
    EXPECT_DOUBLE_EQ(m_clip.note(0).startBeat, 1.0);
    EXPECT_DOUBLE_EQ(m_clip.note(1).startBeat, 2.0);
    EXPECT_DOUBLE_EQ(m_clip.note(2).startBeat, 3.0);
    EXPECT_EQ(m_clip.note(2).pitch, 60); // moved note
}

TEST_F(PianoRollDataTest, GetNotesInRange) {
    MidiNote n1{}; n1.startBeat = 0.0; n1.pitch = 60;
    MidiNote n2{}; n2.startBeat = 1.0; n2.pitch = 64;
    MidiNote n3{}; n3.startBeat = 2.0; n3.pitch = 67;
    MidiNote n4{}; n4.startBeat = 3.0; n4.pitch = 72;
    m_clip.addNote(n1);
    m_clip.addNote(n2);
    m_clip.addNote(n3);
    m_clip.addNote(n4);

    std::vector<int> indices;
    m_clip.getNotesInRange(1.0, 3.0, indices);
    EXPECT_EQ(indices.size(), 2u);
    EXPECT_EQ(indices[0], 1); // beat 1.0
    EXPECT_EQ(indices[1], 2); // beat 2.0
}

TEST_F(PianoRollDataTest, GetActiveNotesAt) {
    MidiNote n1{}; n1.startBeat = 0.0; n1.duration = 2.0; n1.pitch = 60;
    MidiNote n2{}; n2.startBeat = 1.0; n2.duration = 0.5; n2.pitch = 64;
    MidiNote n3{}; n3.startBeat = 3.0; n3.duration = 1.0; n3.pitch = 67;
    m_clip.addNote(n1);
    m_clip.addNote(n2);
    m_clip.addNote(n3);

    std::vector<int> indices;
    m_clip.getActiveNotesAt(1.25, indices);
    // n1 (0-2) is active, n2 (1.0-1.5) is active, n3 (3-4) not
    EXPECT_EQ(indices.size(), 2u);
}

TEST_F(PianoRollDataTest, NoteVelocityRange) {
    MidiNote n{};
    n.startBeat = 0.0;
    n.pitch = 60;

    // Minimum useful velocity
    n.velocity = 1;
    m_clip.addNote(n);
    EXPECT_EQ(m_clip.note(0).velocity, 1);

    // Maximum velocity
    m_clip.removeNote(0);
    n.velocity = 65535;
    m_clip.addNote(n);
    EXPECT_EQ(m_clip.note(0).velocity, 65535);
}

TEST_F(PianoRollDataTest, EmptyClipDefaultLength) {
    MidiClip empty(8.0);
    EXPECT_DOUBLE_EQ(empty.lengthBeats(), 8.0);
    EXPECT_EQ(empty.noteCount(), 0);
    EXPECT_TRUE(empty.loop());
}
