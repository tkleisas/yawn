#include <gtest/gtest.h>

#include "util/MidiFileIO.h"
#include "midi/MidiClip.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

using namespace yawn;

namespace {

std::filesystem::path tempPath(const char* name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p;
}

// Build a small known clip: kick + snare on ch9, plus a melody on ch0.
midi::MidiClip makeKnownClip() {
    midi::MidiClip c(4.0);                 // 4 beats long
    c.setName("known");
    c.setLoop(true);

    auto add = [&](double start, double dur, uint8_t pitch, uint8_t ch, uint16_t v16) {
        midi::MidiNote n;
        n.startBeat = start;
        n.duration  = dur;
        n.pitch     = pitch;
        n.channel   = ch;
        n.velocity  = v16;
        c.addNote(n);
    };
    // Drum-ish hits on channel 9.
    add(0.0,  0.25, 36, 9, 100 << 9);      // kick
    add(1.0,  0.25, 38, 9,  90 << 9);      // snare
    add(2.0,  0.25, 36, 9,  85 << 9);      // kick
    add(3.0,  0.25, 38, 9,  95 << 9);      // snare
    // Melody on channel 0.
    add(0.0,  0.5,  60, 0,  80 << 9);      // C4
    add(0.5,  0.5,  64, 0,  80 << 9);      // E4
    add(1.0,  1.0,  67, 0,  80 << 9);      // G4 (held)
    return c;
}

} // namespace

TEST(MidiFileIO, SaveAndLoadRoundTrip) {
    auto src = makeKnownClip();
    auto file = tempPath("yawn_test_midifileio.mid");

    ASSERT_TRUE(util::saveMidiFile(file, src, 128.0, 4, 4));
    ASSERT_TRUE(std::filesystem::exists(file));

    util::MidiFileInfo info;
    auto dst = util::loadMidiFile(file, &info);
    ASSERT_NE(dst, nullptr);

    EXPECT_NEAR(info.tempoBPM, 128.0, 0.5);
    EXPECT_EQ(info.timeSigNum, 4);
    EXPECT_EQ(info.timeSigDen, 4);

    EXPECT_EQ(dst->noteCount(), src.noteCount());
    EXPECT_GE(dst->lengthBeats(), 4.0);  // snaps up to a whole beat

    // Each source note should round-trip with sub-tick (PPQ=480) timing
    // tolerance. Match by (channel, pitch, ~startBeat) and check duration.
    for (int i = 0; i < src.noteCount(); ++i) {
        const auto& s = src.note(i);
        bool found = false;
        for (int j = 0; j < dst->noteCount(); ++j) {
            const auto& d = dst->note(j);
            if (d.channel == s.channel && d.pitch == s.pitch &&
                std::abs(d.startBeat - s.startBeat) < 0.005) {
                EXPECT_NEAR(d.duration, s.duration, 0.005);
                // Velocity is reduced to 7-bit on write and re-expanded
                // on read; round-trip exactly for multiples of 512.
                uint8_t srcVel7 = static_cast<uint8_t>(s.velocity >> 9);
                uint8_t dstVel7 = static_cast<uint8_t>(d.velocity >> 9);
                EXPECT_EQ(srcVel7, dstVel7);
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "note " << i << " (pitch=" << (int)s.pitch
                            << " ch=" << (int)s.channel
                            << " start=" << s.startBeat << ") not found after round-trip";
    }

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(MidiFileIO, LoadNonexistentReturnsNull) {
    auto missing = std::filesystem::temp_directory_path() / "yawn_test_does_not_exist.mid";
    std::error_code ec;
    std::filesystem::remove(missing, ec);   // be sure
    auto clip = util::loadMidiFile(missing);
    EXPECT_EQ(clip, nullptr);
}

TEST(MidiFileIO, LoadEmptyFileReturnsNull) {
    auto p = tempPath("yawn_test_empty.mid");
    { std::ofstream f(p, std::ios::binary | std::ios::trunc); /* zero bytes */ }
    auto clip = util::loadMidiFile(p);
    EXPECT_EQ(clip, nullptr);
    std::error_code ec;
    std::filesystem::remove(p, ec);
}
