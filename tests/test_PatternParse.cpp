// PatternParse — the yawn.midi / yawn.drums notation parsers.

#include "livecode/PatternParse.h"

#include <gtest/gtest.h>

#include <map>

using namespace yawn::livecode;

// ── Melodic phrases ──────────────────────────────────────────────────────

TEST(PatternParse, SequentialNotesUserExample) {
    PatternClip pc;
    std::string err;
    // The user's exact example — no separators at all.
    ASSERT_TRUE(parseMelodicPhrase("A1_16B1_16D#3_8", pc, err)) << err;
    ASSERT_EQ(pc.notes.size(), 3u);
    EXPECT_EQ(pc.notes[0].pitch, 33);   // A1 (C4 = 60)
    EXPECT_EQ(pc.notes[1].pitch, 35);   // B1
    EXPECT_EQ(pc.notes[2].pitch, 51);   // D#3
    EXPECT_DOUBLE_EQ(pc.notes[0].start, 0.0);
    EXPECT_DOUBLE_EQ(pc.notes[1].start, 0.25);
    EXPECT_DOUBLE_EQ(pc.notes[2].start, 0.5);
    EXPECT_DOUBLE_EQ(pc.notes[2].dur, 0.5);
    EXPECT_DOUBLE_EQ(pc.beats, 1.0);    // 16th + 16th + 8th
}

TEST(PatternParse, MultiplierStar) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("C2_16*8", pc, err)) << err;
    ASSERT_EQ(pc.notes.size(), 1u);
    EXPECT_DOUBLE_EQ(pc.notes[0].dur, 2.0);   // 8 × 16th = 2 beats
    EXPECT_DOUBLE_EQ(pc.beats, 2.0);
}

TEST(PatternParse, RestAdvancesCursor) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("C2_8 R_8 E2_8", pc, err)) << err;
    ASSERT_EQ(pc.notes.size(), 2u);
    EXPECT_DOUBLE_EQ(pc.notes[0].start, 0.0);
    EXPECT_DOUBLE_EQ(pc.notes[1].start, 1.0);   // after the 8th rest
    EXPECT_DOUBLE_EQ(pc.beats, 1.5);
}

TEST(PatternParse, VelocitySuffix) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("C2_8@1 C2_8@0.5 C2_8", pc, err)) << err;
    ASSERT_EQ(pc.notes.size(), 3u);
    EXPECT_DOUBLE_EQ(pc.notes[0].vel, 1.0);
    EXPECT_DOUBLE_EQ(pc.notes[1].vel, 0.5);
    EXPECT_DOUBLE_EQ(pc.notes[2].vel, 0.8);    // default
}

TEST(PatternParse, VelocityClamped) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("C2_8@5 C2_8@0.2", pc, err)) << err;
    EXPECT_DOUBLE_EQ(pc.notes[0].vel, 1.0);   // clamped high
    EXPECT_DOUBLE_EQ(pc.notes[1].vel, 0.2);
    // A negative velocity is a syntax error, not a clamp.
    EXPECT_FALSE(parseMelodicPhrase("C2_8@-1", pc, err));
    EXPECT_NE(err.find("missing velocity"), std::string::npos) << err;
}

TEST(PatternParse, Chords) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("[C3E3G3]_2", pc, err)) << err;
    ASSERT_EQ(pc.notes.size(), 3u);
    for (const auto& n : pc.notes) {
        EXPECT_DOUBLE_EQ(n.start, 0.0);
        EXPECT_DOUBLE_EQ(n.dur, 2.0);
    }
    EXPECT_EQ(pc.notes[0].pitch, 48);   // C3
    EXPECT_EQ(pc.notes[1].pitch, 52);   // E3
    EXPECT_EQ(pc.notes[2].pitch, 55);   // G3
}

TEST(PatternParse, BarSeparatorsAreReadabilityOnly) {
    PatternClip a, b;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("C2_4 D2_4 | E2_4 G2_4", a, err)) << err;
    ASSERT_TRUE(parseMelodicPhrase("C2_4 D2_4 E2_4 G2_4", b, err)) << err;
    EXPECT_EQ(a.notes.size(), b.notes.size());
    EXPECT_DOUBLE_EQ(a.beats, b.beats);
}

TEST(PatternParse, FlatsAndOctaveEdges) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("Bb3_4 C-1_4 G9_4", pc, err)) << err;
    EXPECT_EQ(pc.notes[0].pitch, 58);   // Bb3
    EXPECT_EQ(pc.notes[1].pitch, 0);    // C-1
    EXPECT_EQ(pc.notes[2].pitch, 127);  // G9
}

TEST(PatternParse, WholeBar) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("C2_16*16", pc, err)) << err;
    EXPECT_DOUBLE_EQ(pc.beats, 4.0);
}

TEST(PatternParse, LowerCasePitches) {
    PatternClip pc;
    std::string err;
    ASSERT_TRUE(parseMelodicPhrase("a1_8 c#2_8", pc, err)) << err;
    EXPECT_EQ(pc.notes[0].pitch, 33);
    EXPECT_EQ(pc.notes[1].pitch, 37);
}

TEST(PatternParse, Errors) {
    std::string err;
    PatternClip pc;
    EXPECT_FALSE(parseMelodicPhrase("H3_4", pc, err));
    EXPECT_NE(err.find("expected a note"), std::string::npos) << err;

    EXPECT_FALSE(parseMelodicPhrase("A1", pc, err));
    EXPECT_NE(err.find("missing duration"), std::string::npos) << err;

    EXPECT_FALSE(parseMelodicPhrase("A1_5", pc, err));
    EXPECT_NE(err.find("unsupported duration"), std::string::npos) << err;

    EXPECT_FALSE(parseMelodicPhrase("A1_16*x", pc, err));
    EXPECT_NE(err.find("missing multiplier"), std::string::npos) << err;

    EXPECT_FALSE(parseMelodicPhrase("[C3E3_4", pc, err));
    EXPECT_NE(err.find("unterminated chord"), std::string::npos) << err;

    EXPECT_FALSE(parseMelodicPhrase("[]_4", pc, err));

    EXPECT_FALSE(parseMelodicPhrase("   |  | ", pc, err));
    EXPECT_NE(err.find("no notes"), std::string::npos) << err;
}

// ── Drum grids ───────────────────────────────────────────────────────────

TEST(PatternParse, DrumGridUserExample) {
    PatternClip pc;
    std::string err;
    std::map<std::string, std::string> lanes = {
        {"BD", "x---x---x---x---"},
        {"SN", "--x---x---x---x-"},
        {"HH", "x-x-x-x-x-x-x-x-"},
    };
    ASSERT_TRUE(parseDrumGrid(lanes, 0.0, pc, err)) << err;
    EXPECT_DOUBLE_EQ(pc.beats, 4.0);   // 16 steps × 0.25
    // 4 BD + 4 SN + 8 HH = 16 notes.
    ASSERT_EQ(pc.notes.size(), 16u);

    int bd = 0, sn = 0, hh = 0;
    for (const auto& n : pc.notes) {
        EXPECT_EQ(n.ch, 9);
        EXPECT_EQ(n.dur, 0.25);
        switch (n.pitch) {
            case 36: ++bd; break;
            case 38: ++sn; break;
            case 42: ++hh; break;
            default: ADD_FAILURE() << "unexpected pitch " << n.pitch;
        }
    }
    EXPECT_EQ(bd, 4);
    EXPECT_EQ(sn, 4);
    EXPECT_EQ(hh, 8);

    // First BD on step 0, first SN on step 2, x velocity = 96/127.
    for (const auto& n : pc.notes) {
        if (n.pitch == 36 && n.start == 0.0)
            EXPECT_NEAR(n.vel, 96.0 / 127.0, 1e-9);
        if (n.pitch == 38) {
            EXPECT_DOUBLE_EQ(n.start, 0.5);
            break;
        }
    }
}

TEST(PatternParse, DrumGridAccentsAndGhosts) {
    PatternClip pc;
    std::string err;
    std::map<std::string, std::string> lanes = {
        {"BD", "Xo.g-"},   // mixed velocities + rest
    };
    ASSERT_TRUE(parseDrumGrid(lanes, 0.0, pc, err)) << err;
    ASSERT_EQ(pc.notes.size(), 3u);                      // . and - are rests
    EXPECT_NEAR(pc.notes[0].vel, 112.0 / 127.0, 1e-9);   // X
    EXPECT_NEAR(pc.notes[1].vel, 76.0  / 127.0, 1e-9);   // o
    EXPECT_NEAR(pc.notes[2].vel, 40.0  / 127.0, 1e-9);   // g
    EXPECT_DOUBLE_EQ(pc.notes[0].start, 0.0);
    EXPECT_DOUBLE_EQ(pc.notes[1].start, 0.25);
    EXPECT_DOUBLE_EQ(pc.notes[2].start, 0.75);           // '.' rest skipped
}

TEST(PatternParse, DrumGridLanesLoopOverClip) {
    PatternClip pc;
    std::string err;
    std::map<std::string, std::string> lanes = {
        {"BD", "x---"},                  // 1 beat, loops
        {"HH", "x-x-x-x-x-x-x-x-"},      // 2 beats… wait: 16 steps = 4 beats
    };
    ASSERT_TRUE(parseDrumGrid(lanes, 0.0, pc, err)) << err;
    EXPECT_DOUBLE_EQ(pc.beats, 4.0);     // longest lane wins
    int bd = 0;
    for (const auto& n : pc.notes)
        if (n.pitch == 36) ++bd;
    EXPECT_EQ(bd, 4);                    // x--- loops 4× over 4 beats
}

TEST(PatternParse, DrumGridForcedLength) {
    PatternClip pc;
    std::string err;
    std::map<std::string, std::string> lanes = {
        {"BD", "x---"},
        {"beats", "8"},   // not a lane — but as a map entry it would parse
    };
    // The Lua binding filters 'beats' before it reaches us; here the map
    // path would reject it — verify that behavior so bindings stay honest.
    EXPECT_FALSE(parseDrumGrid(lanes, 0.0, pc, err));

    lanes = {{"BD", "x---"}};
    ASSERT_TRUE(parseDrumGrid(lanes, 8.0, pc, err)) << err;
    EXPECT_DOUBLE_EQ(pc.beats, 8.0);
    int bd = 0;
    for (const auto& n : pc.notes)
        if (n.pitch == 36) ++bd;
    EXPECT_EQ(bd, 8);                    // 1 hit per 4-step lane, ×8 loops
}

TEST(PatternParse, DrumGridBarSeparatorsAndSpaces) {
    PatternClip pc;
    std::string err;
    std::map<std::string, std::string> lanes = {
        {"BD", "x---x--- | x---x---"},
    };
    ASSERT_TRUE(parseDrumGrid(lanes, 0.0, pc, err)) << err;
    EXPECT_DOUBLE_EQ(pc.beats, 4.0);     // separators stripped: 16 steps
    int bd = 0;
    for (const auto& n : pc.notes)
        if (n.pitch == 36) ++bd;
    EXPECT_EQ(bd, 4);
}

TEST(PatternParse, DrumGridAliases) {
    PatternClip pc;
    std::string err;
    std::map<std::string, std::string> lanes = {
        {"kick", "x---"},
        {"Snare", "x---"},
        {"hihat", "x---"},
        {"OpenHH", "x---"},
        {"CLAP", "x---"},
        {"ride", "x---"},
    };
    ASSERT_TRUE(parseDrumGrid(lanes, 0.0, pc, err)) << err;
    // Map iteration is alphabetical by lane key — count per pitch instead.
    std::map<int, int> counts;
    for (const auto& n : pc.notes) ++counts[n.pitch];
    EXPECT_EQ(counts[36], 1);   // kick
    EXPECT_EQ(counts[38], 1);   // Snare
    EXPECT_EQ(counts[42], 1);   // hihat
    EXPECT_EQ(counts[46], 1);   // OpenHH
    EXPECT_EQ(counts[39], 1);   // CLAP
    EXPECT_EQ(counts[51], 1);   // ride
    EXPECT_EQ(pc.notes.size(), 6u);
}

TEST(PatternParse, DrumGridErrors) {
    std::string err;
    PatternClip pc;
    EXPECT_FALSE(parseDrumGrid({}, 0.0, pc, err));

    std::map<std::string, std::string> lanes = {{"BD", "x--q--"}};
    EXPECT_FALSE(parseDrumGrid(lanes, 0.0, pc, err));
    EXPECT_NE(err.find("unknown grid char 'q'"), std::string::npos) << err;

    lanes = {{"XYZ", "x---"}};
    EXPECT_FALSE(parseDrumGrid(lanes, 0.0, pc, err));
    EXPECT_NE(err.find("unknown drum lane 'XYZ'"), std::string::npos) << err;
    EXPECT_NE(err.find("valid:"), std::string::npos) << err;

    lanes = {{"BD", ""}};
    EXPECT_FALSE(parseDrumGrid(lanes, 0.0, pc, err));
    EXPECT_NE(err.find("is empty"), std::string::npos) << err;
}
