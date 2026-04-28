#include <gtest/gtest.h>
#include "util/NoteNames.h"

using yawn::util::midiNoteName;
using yawn::util::parseRootFromFilename;

// ─── midiNoteName ───────────────────────────────────────────────────

TEST(NoteName, Anchors) {
    EXPECT_EQ(midiNoteName(60), "C4");    // MIDI 60 = C4 (Yamaha-style octave)
    EXPECT_EQ(midiNoteName(69), "A4");    // A4 = 440 Hz
    EXPECT_EQ(midiNoteName(0),  "C-1");
    EXPECT_EQ(midiNoteName(127), "G9");
}

TEST(NoteName, SharpVsFlat) {
    EXPECT_EQ(midiNoteName(61, true),  "C#4");
    EXPECT_EQ(midiNoteName(61, false), "Db4");
    EXPECT_EQ(midiNoteName(70, true),  "A#4");
    EXPECT_EQ(midiNoteName(70, false), "Bb4");
}

TEST(NoteName, OutOfRange) {
    EXPECT_EQ(midiNoteName(-1),  "??");
    EXPECT_EQ(midiNoteName(128), "??");
}

// ─── parseRootFromFilename ──────────────────────────────────────────

TEST(NoteParse, BasicNaturals) {
    EXPECT_EQ(parseRootFromFilename("Bass_C2.wav"), 36);
    EXPECT_EQ(parseRootFromFilename("Pad_A4.wav"),  69);
    EXPECT_EQ(parseRootFromFilename("Sub_C-1.wav"), 0);
    EXPECT_EQ(parseRootFromFilename("Bell_G9.wav"), 127);
}

TEST(NoteParse, Accidentals) {
    EXPECT_EQ(parseRootFromFilename("Pad_F#3.wav"), 54);  // F#3 = 54
    EXPECT_EQ(parseRootFromFilename("Pad_Bb3.wav"), 58);  // Bb3 = 58
    EXPECT_EQ(parseRootFromFilename("Lead_C#5.wav"), 73);
}

TEST(NoteParse, ExtraSuffixesIgnored) {
    // Velocity / round-robin / dynamic markings after the note still parse
    EXPECT_EQ(parseRootFromFilename("Bass_C2_v90.wav"),    36);
    EXPECT_EQ(parseRootFromFilename("Pad_F#3_pp_rr1.wav"), 54);
    EXPECT_EQ(parseRootFromFilename("Drum_D2-velocity100-take2.wav"), 38);
}

TEST(NoteParse, NoExtensionStillParses) {
    EXPECT_EQ(parseRootFromFilename("Bass_C2"), 36);
    EXPECT_EQ(parseRootFromFilename("Pad_F#3"), 54);
}

TEST(NoteParse, PathStrippedBeforeParse) {
    EXPECT_EQ(parseRootFromFilename("/home/user/samples/Bass_C2.wav"), 36);
    EXPECT_EQ(parseRootFromFilename("C:\\Music\\Pad_F#3_v90.wav"),     54);
}

TEST(NoteParse, NoMatchReturnsMinusOne) {
    EXPECT_EQ(parseRootFromFilename(""),                -1);
    EXPECT_EQ(parseRootFromFilename("Foo.wav"),         -1);
    EXPECT_EQ(parseRootFromFilename("MyDrumLoop.wav"),  -1);
    EXPECT_EQ(parseRootFromFilename(".wav"),            -1);
}

TEST(NoteParse, DoesNotMatchInsideWords) {
    // "Bass" must not parse as B + "ass". The letter-before guard
    // only allows note matches at word boundaries (start of string,
    // after underscore, after digit, etc.).
    EXPECT_EQ(parseRootFromFilename("Bass.wav"),       -1);
    EXPECT_EQ(parseRootFromFilename("Cello.wav"),      -1);
    EXPECT_EQ(parseRootFromFilename("FrenchHorn.wav"), -1);
}

TEST(NoteParse, DoesNotMatchTrailingLetter) {
    // "C4x" / "C4key" should NOT match as C4 — note tokens must be
    // followed by punctuation or end-of-string. Otherwise a filename
    // like "Bass_C4key.wav" would incorrectly snap to C4.
    EXPECT_EQ(parseRootFromFilename("Bass_C4key.wav"), -1);
    EXPECT_EQ(parseRootFromFilename("Pad_C5x.wav"),    -1);
}

TEST(NoteParse, PrefersLastNoteWhenMultiple) {
    // If the filename contains multiple plausible note tokens, the
    // last one wins (since the convention is "name then pitch").
    EXPECT_EQ(parseRootFromFilename("C2_to_E4.wav"), 64);  // E4 = 64, not C2 = 36
}
