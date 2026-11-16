#include <gtest/gtest.h>

#include "generate/GeneratorPreset.h"

#include <filesystem>
#include <fstream>

using namespace yawn::generate;

TEST(GeneratorPreset, ParamsRoundTrip) {
    GeneratorParams p;
    p.seed = 123456;
    p.styles = { { "rock", 0.3f }, { "techno", 0.7f } };
    p.blend = "rotate";
    p.scale = "rast";
    p.root = "D";
    p.movement = "ascending";
    p.density = 0.62f;
    p.noteLength = 0.45f;
    p.swing = 0.18f;
    p.tempo = 132;
    p.beatsPerBar = 4;
    p.sections = { "intro", "chorus", "outro" };

    GeneratorParams back;
    std::string error;
    ASSERT_TRUE(paramsFromJson(paramsToJson(p), back, &error)) << error;
    EXPECT_EQ(back.seed, p.seed);
    ASSERT_EQ(back.styles.size(), 2u);
    EXPECT_EQ(back.styles[0].id, "rock");
    EXPECT_FLOAT_EQ(back.styles[0].weight, 0.3f);
    EXPECT_EQ(back.blend, "rotate");
    EXPECT_EQ(back.scale, "rast");
    EXPECT_EQ(back.root, "D");
    EXPECT_EQ(back.movement, "ascending");
    EXPECT_FLOAT_EQ(back.density, 0.62f);
    EXPECT_FLOAT_EQ(back.noteLength, 0.45f);
    EXPECT_FLOAT_EQ(back.swing, 0.18f);
    EXPECT_EQ(back.tempo, 132);
    ASSERT_EQ(back.sections.size(), 3u);
    EXPECT_EQ(back.sections[1], "chorus");
}

TEST(GeneratorPreset, SaveLoadListFind) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "yawn_gen_preset_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    GeneratorParams p;
    p.seed = 7;
    p.styles = { { "pop", 1.0f } };
    std::string error;
    ASSERT_TRUE(saveGeneratorPreset(dir / "Bright Pop.json", p, &error))
        << error;

    const auto names = listGeneratorPresets({ dir });
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "Bright Pop");

    const auto found = findGeneratorPreset({ dir }, "Bright Pop");
    ASSERT_FALSE(found.empty());
    GeneratorParams back;
    ASSERT_TRUE(loadGeneratorPreset(found, back, &error)) << error;
    EXPECT_EQ(back.seed, 7u);
    ASSERT_EQ(back.styles.size(), 1u);
    EXPECT_EQ(back.styles[0].id, "pop");

    // Missing preset resolves empty; malformed JSON reports an error.
    EXPECT_TRUE(findGeneratorPreset({ dir }, "nope").empty());
    {
        std::ofstream f(dir / "broken.json");
        f << "{not json";
    }
    GeneratorParams bad;
    EXPECT_FALSE(loadGeneratorPreset(dir / "broken.json", bad, &error));
    EXPECT_FALSE(error.empty());

    fs::remove_all(dir, ec);
}
