#include <gtest/gtest.h>
#include "audio/AutoSampler.h"

using yawn::audio::sanitizeCaptureName;
using yawn::audio::defaultCaptureName;
using yawn::audio::velocityLayerPreset;

// ─── sanitizeCaptureName ────────────────────────────────────────────

TEST(AutoSampleSanitize, BasicLowercaseAndUnderscores) {
    EXPECT_EQ(sanitizeCaptureName("Piano Capture"), "piano_capture");
    EXPECT_EQ(sanitizeCaptureName("MIDI 1"),         "midi_1");
    EXPECT_EQ(sanitizeCaptureName("My Synth"),       "my_synth");
}

TEST(AutoSampleSanitize, IllegalCharsCollapse) {
    // Multiple illegal chars in a row collapse to a single underscore.
    EXPECT_EQ(sanitizeCaptureName("foo!!@#bar"), "foo_bar");
    EXPECT_EQ(sanitizeCaptureName("a/b\\c:d*e"), "a_b_c_d_e");
}

TEST(AutoSampleSanitize, TrimTrailingPunctuation) {
    EXPECT_EQ(sanitizeCaptureName("Piano!!!"),  "piano");
    EXPECT_EQ(sanitizeCaptureName("Piano___"),  "piano");
    EXPECT_EQ(sanitizeCaptureName("Piano..."),  "piano");
}

TEST(AutoSampleSanitize, EmptyAndAllIllegalFallToCapture) {
    EXPECT_EQ(sanitizeCaptureName(""),     "capture");
    EXPECT_EQ(sanitizeCaptureName("   "),  "capture");
    EXPECT_EQ(sanitizeCaptureName("///"),  "capture");
}

TEST(AutoSampleSanitize, KeepsSafePunctuation) {
    // Hyphen, underscore, dot are filename-safe.
    EXPECT_EQ(sanitizeCaptureName("yamaha-cp70"),     "yamaha-cp70");
    EXPECT_EQ(sanitizeCaptureName("cs-80_v2.0"),     "cs-80_v2.0");
}

TEST(AutoSampleSanitize, NumericNamesPreserved) {
    EXPECT_EQ(sanitizeCaptureName("808"),   "808");
    EXPECT_EQ(sanitizeCaptureName("Track 4"), "track_4");
}

// ─── defaultCaptureName ─────────────────────────────────────────────

TEST(AutoSampleDefaultName, NormalTrackName) {
    EXPECT_EQ(defaultCaptureName("MIDI 1"),         "midi_1_capture");
    EXPECT_EQ(defaultCaptureName("Piano"),          "piano_capture");
    EXPECT_EQ(defaultCaptureName("Yamaha CP-70"),   "yamaha_cp-70_capture");
}

TEST(AutoSampleDefaultName, EmptyOrUnusableTrackName) {
    EXPECT_EQ(defaultCaptureName(""),     "capture");
    EXPECT_EQ(defaultCaptureName("///"),  "capture");
}

// ─── velocityLayerPreset ────────────────────────────────────────────

TEST(AutoSampleVelLayers, OneLayer) {
    auto v = velocityLayerPreset(1);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0], 120);
}

TEST(AutoSampleVelLayers, MultipleLayersAscendAndCapAt127) {
    for (int n : {2, 3, 4, 8}) {
        auto v = velocityLayerPreset(n);
        ASSERT_EQ(static_cast<int>(v.size()), n);
        // Strictly ascending
        for (size_t i = 1; i < v.size(); ++i)
            EXPECT_GT(v[i], v[i - 1])
                << "n=" << n << " not ascending at i=" << i;
        // Top layer hits 127 (the loudest sample)
        EXPECT_EQ(v.back(), 127) << "n=" << n;
        // Bottom layer is non-trivial (>= 16)
        EXPECT_GE(v.front(), 16) << "n=" << n;
    }
}

TEST(AutoSampleVelLayers, ClampsOutOfRangeInputs) {
    EXPECT_EQ(velocityLayerPreset(0).size(), 1u);   // clamped up to 1
    EXPECT_EQ(velocityLayerPreset(-5).size(), 1u);
    EXPECT_EQ(velocityLayerPreset(20).size(), 8u);   // clamped down to 8
}
