#include <gtest/gtest.h>

#include "instruments/PolivoksSynth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace yawn::instruments;
using namespace yawn::midi;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 256;
constexpr int    kChannels   = 2;

MidiBuffer noteOn(uint8_t note, uint8_t vel = 100)
{
    MidiBuffer b;
    b.addMessage(MidiMessage::noteOn(0, note, vel));
    return b;
}

MidiBuffer noteOff(uint8_t note)
{
    MidiBuffer b;
    b.addMessage(MidiMessage::noteOff(0, note));
    return b;
}

float rms(const float* x, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += (double)x[i] * x[i];
    return (float)std::sqrt(sum / std::max(1, n));
}

} // namespace

TEST(PolivoksSynth, Meta)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);
    EXPECT_STREQ(synth.name(), "Polivoks 1982");
    EXPECT_STREQ(synth.id(), "polivoks");
    EXPECT_EQ(synth.parameterCount(), PolivoksSynth::kNumParams);
    EXPECT_STREQ(synth.parameterInfo(PolivoksSynth::kProgram).name, "Program");
    EXPECT_EQ(synth.parameterInfo(PolivoksSynth::kProgram).valueLabelCount, 8);
}

TEST(PolivoksSynth, FactoryTestDefaults)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);
    EXPECT_EQ((int)std::lround(synth.getParameter(PolivoksSynth::kProgram)), 0);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kCutoff), 1.0f, 1e-4f);
    EXPECT_EQ((int)std::lround(synth.getParameter(PolivoksSynth::kVco1Oct)), 0);
    EXPECT_EQ((int)std::lround(synth.getParameter(PolivoksSynth::kVco1Wave)), 1);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kVco1Level), 0.5f, 1e-4f);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kVolume), -10.0f, 1e-4f);
}

TEST(PolivoksSynth, ProgramSwitchAppliesValues)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);
    synth.setParameter(PolivoksSynth::kProgram, 1.0f);
    EXPECT_EQ((int)std::lround(synth.getParameter(PolivoksSynth::kProgram)), 1);
    EXPECT_EQ((int)std::lround(synth.getParameter(PolivoksSynth::kVco1Oct)), 1);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kVco2Level), 0.95f, 1e-4f);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kCutoff),
                PolivoksSynth::cutoffHzToNorm(250.0f), 1e-4f);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kVolume), -12.0f, 1e-4f);
}

TEST(PolivoksSynth, NoteOnProducesAudio)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    auto midi = noteOn(60, 100);
    synth.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
}

TEST(PolivoksSynth, SilentWithoutNotes)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);

    float buffer[kBlockSize * kChannels] = {};
    synth.process(buffer, kBlockSize, kChannels, MidiBuffer{});
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), 1e-4f);
}

TEST(PolivoksSynth, NoteOffReleases)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);
    synth.setParameter(PolivoksSynth::kARelease, PolivoksSynth::timeMsToNorm(10.0f));

    float buffer[kBlockSize * kChannels] = {};
    auto on = noteOn(60);
    synth.process(buffer, kBlockSize, kChannels, on);
    const float onRms = rms(buffer, kBlockSize * kChannels);

    auto off = noteOff(60);
    for (int b = 0; b < 12; ++b)
    {
        std::memset(buffer, 0, sizeof(buffer));
        synth.process(buffer, kBlockSize, kChannels, b == 0 ? off : MidiBuffer{});
    }
    EXPECT_LT(rms(buffer, kBlockSize * kChannels), onRms * 0.1f);
}

TEST(PolivoksSynth, ParameterRoundTrip)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);
    synth.setParameter(PolivoksSynth::kCutoff, 0.4f);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kCutoff), 0.4f, 1e-5f);
    synth.setParameter(PolivoksSynth::kVco2Level, 0.3f);
    EXPECT_NEAR(synth.getParameter(PolivoksSynth::kVco2Level), 0.3f, 1e-5f);
    synth.setParameter(PolivoksSynth::kFilterMode, 5.0f);
    EXPECT_EQ((int)std::lround(synth.getParameter(PolivoksSynth::kFilterMode)), 1);
}

TEST(PolivoksSynth, DuophonicTwoNotesRender)
{
    PolivoksSynth synth;
    synth.init(kSampleRate, kBlockSize);
    synth.setParameter(PolivoksSynth::kVoiceMode, 1.0f);
    synth.setParameter(PolivoksSynth::kVco1Level, 1.0f);
    synth.setParameter(PolivoksSynth::kVco2Level, 1.0f);
    synth.setParameter(PolivoksSynth::kCutoff, 1.0f);
    synth.setParameter(PolivoksSynth::kResonance, 0.0f);

    MidiBuffer midi;
    midi.addMessage(MidiMessage::noteOn(0, 48, 100));
    midi.addMessage(MidiMessage::noteOn(0, 60, 100));

    float buffer[kBlockSize * kChannels] = {};
    synth.process(buffer, kBlockSize, kChannels, midi);
    EXPECT_GT(rms(buffer, kBlockSize * kChannels), 0.001f);
    for (float v : buffer) EXPECT_TRUE(std::isfinite(v));
}

