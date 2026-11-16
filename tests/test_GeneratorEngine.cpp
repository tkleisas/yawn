#include <gtest/gtest.h>

#include "generate/GeneratorEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef YAWN_GENERATOR_DIR
#define YAWN_GENERATOR_DIR "."
#endif

using namespace yawn::generate;

namespace {

GeneratorParams baseParams(uint64_t seed = 1) {
    GeneratorParams p;
    p.seed = seed;
    p.styles = { {"pop", 1.0f} };
    p.sections = { "intro", "verse", "chorus", "outro" };
    return p;
}

std::vector<double> flatNotes(const GeneratedSong& song) {
    std::vector<double> out;
    for (const auto& s : song.sections)
        for (const auto& c : s.clips)
            for (const auto& n : c.notes)
                out.push_back(n.startBeat);
    return out;
}

bool sameNote(const GeneratedNote& a, const GeneratedNote& b) {
    return a.startBeat == b.startBeat && a.duration == b.duration &&
           a.pitch == b.pitch && a.cents == b.cents &&
           a.velocity == b.velocity && a.channel == b.channel;
}

} // namespace

TEST(GeneratorEngineTest, LoadsBundledScripts) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;
    EXPECT_TRUE(engine.ready());
    EXPECT_FALSE(engine.scriptsDir().empty());
}

TEST(GeneratorEngineTest, GeneratesDeterministically) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratedSong a, b;
    ASSERT_TRUE(engine.generate(baseParams(42), a, &error)) << error;
    ASSERT_TRUE(engine.generate(baseParams(42), b, &error)) << error;

    ASSERT_EQ(a.sections.size(), b.sections.size());
    for (size_t si = 0; si < a.sections.size(); ++si) {
        ASSERT_EQ(a.sections[si].clips.size(), b.sections[si].clips.size());
        for (size_t ci = 0; ci < a.sections[si].clips.size(); ++ci) {
            const auto& ca = a.sections[si].clips[ci].notes;
            const auto& cb = b.sections[si].clips[ci].notes;
            ASSERT_EQ(ca.size(), cb.size());
            for (size_t ni = 0; ni < ca.size(); ++ni)
                EXPECT_TRUE(sameNote(ca[ni], cb[ni]))
                    << "section " << si << " clip " << ci << " note " << ni;
        }
    }
}

TEST(GeneratorEngineTest, DifferentSeedsProduceDifferentNotes) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratedSong a, b;
    ASSERT_TRUE(engine.generate(baseParams(1), a, &error)) << error;
    ASSERT_TRUE(engine.generate(baseParams(2), b, &error)) << error;

    EXPECT_NE(flatNotes(a), flatNotes(b));
}

TEST(GeneratorEngineTest, EmitsSectionsAndLanes) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratedSong song;
    ASSERT_TRUE(engine.generate(baseParams(7), song, &error)) << error;

    ASSERT_EQ(song.sections.size(), 4u);
    EXPECT_EQ(song.sections[0].id, "intro");
    EXPECT_EQ(song.sections[1].id, "verse");
    EXPECT_EQ(song.sections[2].id, "chorus");
    EXPECT_EQ(song.sections[3].id, "outro");
    EXPECT_GT(song.totalNotes(), 0);

    for (const auto& sec : song.sections) {
        ASSERT_EQ(sec.clips.size(), 4u);
        EXPECT_EQ(sec.clips[0].role, "drums");
        EXPECT_EQ(sec.clips[1].role, "bass");
        EXPECT_EQ(sec.clips[2].role, "chords");
        EXPECT_EQ(sec.clips[3].role, "lead");
    }
}

TEST(GeneratorEngineTest, ClassicalStyleUsesNoDrumLane) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratorParams p = baseParams(3);
    p.styles = { {"classical", 1.0f} };
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(p, song, &error)) << error;

    ASSERT_FALSE(song.sections.empty());
    for (const auto& sec : song.sections) {
        ASSERT_EQ(sec.clips.size(), 3u);
        EXPECT_EQ(sec.clips[0].role, "bass");
        EXPECT_EQ(sec.clips[1].role, "chords");
        EXPECT_EQ(sec.clips[2].role, "lead");
    }
}

TEST(GeneratorEngineTest, MicrotonalScaleEmitsCentsInScale) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratorParams p = baseParams(11);
    p.scale = "rast";
    p.root = "C";
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(p, song, &error)) << error;

    const std::vector<double> rast = { 0, 2, 3.5, 5, 7, 9, 10.5 };
    int microtonal = 0;
    for (const auto& sec : song.sections) {
        for (const auto& clip : sec.clips) {
            for (const auto& n : clip.notes) {
                if (n.channel == 9) continue;
                if (n.cents != 0.0f) ++microtonal;
                double semis = n.pitch + n.cents / 100.0;
                double pc = std::fmod(semis, 12.0);
                if (pc < 0) pc += 12.0;
                bool inScale = false;
                for (double d : rast)
                    if (std::abs(pc - d) < 1e-3) inScale = true;
                EXPECT_TRUE(inScale)
                    << "pitch " << n.pitch << " cents " << n.cents;
            }
        }
    }
    EXPECT_GT(microtonal, 0) << "rast should produce tuned (cents) notes";
}

TEST(GeneratorEngineTest, BlendWeightsAndRotateModeWork) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratorParams p = baseParams(5);
    p.styles = { {"rock", 0.3f}, {"classical", 0.3f}, {"techno", 0.4f} };
    p.blend = "rotate";
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(p, song, &error)) << error;
    EXPECT_EQ(song.sections.size(), 4u);
    EXPECT_GT(song.totalNotes(), 0);
}

TEST(GeneratorEngineTest, UnknownStyleReportsError) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratorParams p = baseParams(1);
    p.styles = { {"does_not_exist", 1.0f} };
    GeneratedSong song;
    EXPECT_FALSE(engine.generate(p, song, &error));
    EXPECT_NE(error.find("unknown style"), std::string::npos) << error;
}

TEST(GeneratorEngineTest, UserRootOverridesBundledScripts) {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / "yawn_generator_override_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    {
        std::ofstream f(dir / "init.lua");
        f << "return { generate = function(params)\n"
             "  return { tempo = 100, beatsPerBar = 4, sections = {\n"
             "    { id = 'test', name = 'Test', bars = 2, clips = {\n"
             "      { role = 'lead', notes = {\n"
             "        { beat = 0, dur = 1, pitch = 60 } } } } } } }\n"
             "end }\n";
    }

    GeneratorEngine engine;
    engine.setScriptRoots({ dir, YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(baseParams(1), song, &error)) << error;
    ASSERT_EQ(song.sections.size(), 1u);
    EXPECT_EQ(song.sections[0].id, "test");

    fs::remove_all(dir, ec);
}

TEST(GeneratorEngineTest, ScriptSyntaxErrorIsReported) {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / "yawn_generator_broken_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream f(dir / "init.lua");
        f << "error('boom from test script')\n";
    }

    GeneratorEngine engine;
    engine.setScriptRoots({ dir });
    std::string error;
    EXPECT_FALSE(engine.reload(&error));
    EXPECT_NE(error.find("boom from test script"), std::string::npos)
        << error;

    fs::remove_all(dir, ec);
}
