#include <gtest/gtest.h>

// Smoke test for the Basic Pitch audio-to-MIDI pipeline (ONNX inference +
// note creation). Only built when YAWN_HAS_BASIC_PITCH is enabled; a
// no-op placeholder test keeps the suite green otherwise.

#ifdef YAWN_HAS_BASIC_PITCH

#include "bp_api.h"
#include <cmath>
#include <vector>

namespace {
// Generate a sustained harmonic tone (fundamental + a couple harmonics,
// so it reads more like an instrument than a bare sinusoid) at 22050 Hz.
std::vector<float> harmonicTone(float freq, float seconds) {
    const int sr = basic_pitch::kInputSampleRate;
    const int n  = static_cast<int>(seconds * sr);
    std::vector<float> out(n);
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / sr;
        float s = std::sin(twoPi * freq * t)
                + 0.5f * std::sin(twoPi * 2.0f * freq * t)
                + 0.25f * std::sin(twoPi * 3.0f * freq * t);
        out[i] = 0.4f * s;
    }
    return out;
}
} // namespace

// A 440 Hz tone (A4) should transcribe to at least one note near MIDI 69.
TEST(BasicPitch, TranscribesA4Tone) {
    auto audio = harmonicTone(440.0f, 2.0f);
    auto notes = basic_pitch::transcribe(audio.data(),
                                         static_cast<int>(audio.size()));
    ASSERT_FALSE(notes.empty()) << "expected at least one detected note";

    bool foundA4 = false;
    for (const auto& n : notes) {
        EXPECT_GE(n.endSec, n.startSec);
        EXPECT_GE(n.pitch, 0);
        EXPECT_LE(n.pitch, 127);
        if (std::abs(n.pitch - 69) <= 2) foundA4 = true;
    }
    EXPECT_TRUE(foundA4)
        << "expected a note within 2 semitones of A4 (MIDI 69)";
}

// Degenerate / silent input must return no notes (and not crash).
TEST(BasicPitch, EmptyOrSilentInputNoNotes) {
    // null + too-short are rejected by the minimum-length guard.
    EXPECT_TRUE(basic_pitch::transcribe(nullptr, 0).empty());
    std::vector<float> tiny(10, 0.0f);
    EXPECT_TRUE(basic_pitch::transcribe(tiny.data(),
                                        static_cast<int>(tiny.size())).empty());
    // A half-second of silence runs the full model but yields no notes.
    std::vector<float> silence(basic_pitch::kInputSampleRate / 2, 0.0f);
    EXPECT_TRUE(basic_pitch::transcribe(silence.data(),
                                        static_cast<int>(silence.size())).empty());
}

#else

TEST(BasicPitch, DisabledPlaceholder) {
    GTEST_SKIP() << "YAWN_HAS_BASIC_PITCH not enabled in this build";
}

#endif // YAWN_HAS_BASIC_PITCH
