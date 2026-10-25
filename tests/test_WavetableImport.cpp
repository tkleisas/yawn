#include <gtest/gtest.h>

#include "instruments/WavetableSynth.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string base64Encode(const uint8_t* data, size_t size) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < size) {
        const uint32_t value = (static_cast<uint32_t>(data[i]) << 16) |
                               (static_cast<uint32_t>(data[i + 1]) << 8) |
                               static_cast<uint32_t>(data[i + 2]);
        out.push_back(alphabet[(value >> 18) & 0x3f]);
        out.push_back(alphabet[(value >> 12) & 0x3f]);
        out.push_back(alphabet[(value >> 6) & 0x3f]);
        out.push_back(alphabet[value & 0x3f]);
        i += 3;
    }
    if (i < size) {
        uint32_t value = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < size) value |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(alphabet[(value >> 18) & 0x3f]);
        out.push_back(alphabet[(value >> 12) & 0x3f]);
        out.push_back(i + 1 < size ? alphabet[(value >> 6) & 0x3f] : '=');
        out.push_back('=');
    }
    return out;
}

TEST(WavetableImport, LoadsVWaveV2AndSelectsCustomTable) {
    constexpr int frameSize = 256;
    constexpr int frameCount = 2;
    std::vector<float> samples(
        static_cast<size_t>(frameSize * frameCount), 0.0f);
    for (int frame = 0; frame < frameCount; ++frame) {
        for (int i = 0; i < frameSize; ++i) {
            const double phase = 2.0 * 3.14159265358979323846 *
                                 static_cast<double>(i) / frameSize;
            samples[static_cast<size_t>(frame) * frameSize + i] =
                static_cast<float>((0.4 + 0.1 * frame) * std::sin(phase));
        }
    }
    nlohmann::json root;
    root["format"] = "vwave-wavetable";
    root["version"] = 2;
    root["frameSize"] = frameSize;
    root["frameCount"] = frameCount;
    root["sampleRate"] = 48000;
    root["name"] = "imported";
    root["rootNote"] = 60.0;
    root["morphMode"] = "linear";
    root["data"] = base64Encode(
        reinterpret_cast<const uint8_t*>(samples.data()),
        samples.size() * sizeof(float));

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "vwave_import_test.vwt";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << root.dump(2);
    }

    yawn::instruments::WavetableSynth synth;
    synth.init(44100.0, 256);
    std::string error;
    ASSERT_TRUE(synth.loadCustomWavetable(path.string(), &error)) << error;
    EXPECT_TRUE(synth.hasCustomWavetable());
    EXPECT_EQ(synth.currentTable(), yawn::instruments::WavetableSynth::kCustomTable);
    EXPECT_EQ(synth.frameCount(yawn::instruments::WavetableSynth::kCustomTable),
              frameCount);
    const float* loaded = synth.frameData(
        yawn::instruments::WavetableSynth::kCustomTable, 0);
    ASSERT_NE(loaded, nullptr);
    EXPECT_NEAR(loaded[64], 0.4f, 0.001f);

    synth.setParameter(yawn::instruments::WavetableSynth::kPosition, 0.5f);
    float buffer[256 * 2] = {};
    yawn::midi::MidiBuffer midi;
    yawn::midi::MidiMessage message{};
    message.type = yawn::midi::MidiMessage::Type::NoteOn;
    message.note = 60;
    message.velocity = yawn::midi::Convert::vel7to16(100);
    message.channel = 0;
    midi.addMessage(message);
    synth.process(buffer, 256, 2, midi);
    double energy = 0.0;
    for (float value : buffer) energy += value * value;
    EXPECT_GT(energy, 1e-6);

    const std::filesystem::path tablePath =
        std::filesystem::temp_directory_path() / "vwave_export_test.vwt";
    ASSERT_TRUE(synth.saveCustomWavetable(tablePath.string(), &error)) << error;
    yawn::instruments::WavetableSynth restored;
    restored.init(44100.0, 256);
    ASSERT_TRUE(restored.loadCustomWavetable(tablePath.string(), &error)) << error;
    EXPECT_TRUE(restored.hasCustomWavetable());
    EXPECT_EQ(restored.frameCount(yawn::instruments::WavetableSynth::kCustomTable),
              frameCount);

    const std::filesystem::path presetDir =
        std::filesystem::temp_directory_path() / "yawn_wavetable_preset_test";
    const auto state = synth.saveExtraState(presetDir);
    ASSERT_FALSE(state.empty());
    yawn::instruments::WavetableSynth presetRestored;
    presetRestored.init(44100.0, 256);
    presetRestored.loadExtraState(state, presetDir);
    EXPECT_TRUE(presetRestored.hasCustomWavetable());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(tablePath, ec);
    std::filesystem::remove_all(presetDir, ec);
}

} // namespace
