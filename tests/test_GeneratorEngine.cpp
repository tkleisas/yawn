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

namespace {

bool semisInScale(double semis, const std::vector<double>& degrees) {
    double pc = std::fmod(semis, 12.0);
    if (pc < 0) pc += 12.0;
    for (double d : degrees) {
        double dpc = std::fmod(d, 12.0);
        if (std::abs(pc - dpc) < 1e-2) return true;
        if (std::abs(pc - dpc - 12.0) < 1e-2) return true;
    }
    return false;
}

} // namespace

TEST(GeneratorEngineTest, AllBundledStylesGenerate) {
    namespace fs = std::filesystem;
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    int styles = 0;
    for (const auto& entry :
         fs::directory_iterator(fs::path(YAWN_GENERATOR_DIR) / "styles")) {
        if (entry.path().extension() != ".lua") continue;
        const std::string id = entry.path().stem().string();
        GeneratorParams p;
        p.seed = 3;
        p.styles = { { id, 1.0f } };
        p.sections = { "intro", "verse", "chorus", "outro" };
        GeneratedSong song;
        ASSERT_TRUE(engine.generate(p, song, &error))
            << id << ": " << error;
        EXPECT_GT(song.totalNotes(), 0) << id;
        ASSERT_FALSE(song.sections.empty()) << id;
        for (const auto& sec : song.sections)
            EXPECT_GE(sec.clips.size(), 3u) << id;
        ++styles;
    }
    EXPECT_GE(styles, 10) << "expected the 10-style P1 batch";
}

TEST(GeneratorEngineTest, MicrotonalScalesStayInScale) {
    struct ScaleCase {
        const char* id;
        std::vector<double> degrees;
        bool expectCents;
    };
    const std::vector<ScaleCase> cases = {
        { "rast", { 0, 2, 3.5, 5, 7, 9, 10.5 }, true },
        { "bayati", { 0, 1.5, 3, 5, 7, 8.5, 10 }, true },
        { "just_major",
          { 0, 2.0391, 3.8631, 4.9804, 7.0196, 8.8436, 10.8827 }, true },
        { "tet24",
          { 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5,
            6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5, 11, 11.5 }, true },
        { "raga_bhimpalasi", { 0, 2, 3, 5, 7, 8, 10 }, false },
    };

    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    for (const auto& sc : cases) {
        GeneratorParams p;
        p.seed = 4;
        p.styles = { { "pop", 1.0f } };
        p.scale = sc.id;
        p.root = "C";
        p.sections = { "verse", "chorus" };
        GeneratedSong song;
        ASSERT_TRUE(engine.generate(p, song, &error))
            << sc.id << ": " << error;

        int centsCount = 0;
        for (const auto& sec : song.sections) {
            for (const auto& clip : sec.clips) {
                for (const auto& n : clip.notes) {
                    if (n.channel == 9) continue;
                    if (n.cents != 0.0f) ++centsCount;
                    double semis = n.pitch + n.cents / 100.0;
                    EXPECT_TRUE(semisInScale(semis, sc.degrees))
                        << sc.id << " pitch " << n.pitch
                        << " cents " << n.cents;
                }
            }
        }
        if (sc.expectCents)
            EXPECT_GT(centsCount, 0) << sc.id;
    }
}

TEST(GeneratorEngineTest, SerialStyleUsesTwelveToneRows) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratorParams p = baseParams(21);
    p.styles = { { "serial", 1.0f } };
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(p, song, &error)) << error;
    ASSERT_FALSE(song.sections.empty());

    std::vector<bool> pc(12, false);
    for (const auto& sec : song.sections)
        for (const auto& clip : sec.clips)
            for (const auto& n : clip.notes)
                if (n.channel != 9) pc[n.pitch % 12] = true;

    int used = 0;
    for (bool b : pc) if (b) ++used;
    EXPECT_EQ(used, 12) << "a twelve-tone row should use all pitch classes";
}

TEST(GeneratorEngineTest, BlendAllTenStylesBothModes) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    const std::vector<std::string> ids = {
        "pop", "rock", "classical", "techno", "house",
        "jazz", "metal", "ethnic_maqam", "raga", "serial",
    };
    for (const char* mode : { "fuse", "rotate" }) {
        GeneratorParams p = baseParams(31);
        p.styles.clear();
        for (const auto& id : ids) p.styles.push_back({ id, 1.0f });
        p.blend = mode;
        GeneratedSong song;
        ASSERT_TRUE(engine.generate(p, song, &error))
            << mode << ": " << error;
        EXPECT_GT(song.totalNotes(), 0) << mode;
        EXPECT_EQ(song.sections.size(), 4u) << mode;
    }
}

TEST(GeneratorEngineTest, ListsStylesAndScales) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    const auto styles = engine.listStyles();
    EXPECT_GE(styles.size(), 10u);
    EXPECT_NE(std::find(styles.begin(), styles.end(), "pop"), styles.end());
    EXPECT_NE(std::find(styles.begin(), styles.end(), "serial"), styles.end());

    const auto scales = engine.listScales();
    EXPECT_NE(std::find(scales.begin(), scales.end(), "rast"), scales.end());
    EXPECT_NE(std::find(scales.begin(), scales.end(), "major"), scales.end());
    EXPECT_NE(std::find(scales.begin(), scales.end(), "raga_yaman"),
              scales.end());
}

TEST(GeneratorEngineTest, ReloadTwiceIsSafe) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;
    ASSERT_TRUE(engine.reload(&error)) << error;
    EXPECT_TRUE(engine.ready());
}

namespace {

std::vector<VisualShaderInfo> testVisualCatalog() {
    VisualShaderInfo mandala;
    mandala.id = "32_mandala_seed";
    mandala.path = "assets/shaders/examples/32_mandala_seed.frag";
    mandala.params = {
        { "segments", 2, 32, 12 },
        { "petals", 2, 24, 8 },
        { "rings", 1, 8, 3 },
        { "gold", 0, 1, 0.7f },
        { "palette", 0, 1, 0.5f },
        { "spin", -2, 2, 0.5f },
        { "glow", 0, 1, 0.7f },
        { "zoom", 0.5f, 1.4f, 1.0f },
    };
    VisualShaderInfo stars;
    stars.id = "45_starfield";
    stars.path = "assets/shaders/examples/45_starfield.frag";
    stars.params = {
        { "density", 0, 1, 0.7f },
        { "drift", 0, 1, 0.2f },
        { "nebula", 0, 1, 0.4f },
        { "twinkle", 0, 1, 0.6f },
        { "palette", 0, 1, 0.5f },
    };
    return { mandala, stars };
}

} // namespace

TEST(GeneratorEngineTest, GenerateVisualsFromCatalog) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;
    engine.setVisualShaderCatalog(testVisualCatalog());

    GeneratorParams p = baseParams(51);
    p.generateVisuals = true;
    p.visualTheme = "sacred";
    p.visualReactivity = 1.6f;
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(p, song, &error)) << error;

    ASSERT_EQ(song.sections.size(), 4u);
    EXPECT_EQ(song.totalVisuals(), 16);   // 4 lanes × 4 sections
    for (const auto& sec : song.sections) {
        ASSERT_EQ(sec.visuals.size(), 4u);
        const std::vector<std::string> expectedRoles = {
            "drums", "bass", "chords", "lead"
        };
        for (size_t i = 0; i < sec.visuals.size(); ++i) {
            const auto& v = sec.visuals[i];
            EXPECT_FALSE(v.shaderPath.empty());
            EXPECT_EQ(v.role, expectedRoles[i]);
            EXPECT_EQ(v.audioRole, expectedRoles[i]);
            const float scale = expectedRoles[i] == "drums" ? 1.25f
                              : expectedRoles[i] == "bass" ? 0.9f
                              : expectedRoles[i] == "chords" ? 0.7f : 1.0f;
            EXPECT_NEAR(v.reactivity, 1.6f * scale, 1e-4f);
            for (const auto& kv : v.params) {
                EXPECT_TRUE(std::isfinite(kv.second));
            }
        }
    }
    // Seed-picture shaders must carry the bundled seed image.
    for (const auto& sec : song.sections)
        for (const auto& v : sec.visuals)
            if (v.shaderId == "32_mandala_seed")
                EXPECT_EQ(v.image, "seed_emblem.png");

    // Deterministic.
    GeneratedSong again;
    ASSERT_TRUE(engine.generate(p, again, &error)) << error;
    ASSERT_EQ(again.sections.size(), song.sections.size());
    for (size_t i = 0; i < song.sections.size(); ++i) {
        ASSERT_EQ(again.sections[i].visuals.size(),
                  song.sections[i].visuals.size());
        for (size_t j = 0; j < song.sections[i].visuals.size(); ++j) {
            EXPECT_EQ(again.sections[i].visuals[j].shaderId,
                      song.sections[i].visuals[j].shaderId);
            EXPECT_EQ(again.sections[i].visuals[j].role,
                      song.sections[i].visuals[j].role);
            EXPECT_EQ(again.sections[i].visuals[j].params,
                      song.sections[i].visuals[j].params);
        }
    }
}

TEST(GeneratorEngineTest, UnknownVisualShaderIsRejected) {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / "yawn_generator_bad_visual_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream f(dir / "init.lua");
        f << "return { generate = function(params)\n"
             "  return { tempo = 100, beatsPerBar = 4, sections = {\n"
             "    { id = 'test', name = 'Test', bars = 2, visuals = {\n"
             "      { shader = 'not_a_real_shader' } }, clips = {\n"
             "      { role = 'lead', notes = {\n"
             "        { beat = 0, dur = 1, pitch = 60 } } } } } } }\n"
             "end }\n";
    }

    GeneratorEngine engine;
    engine.setScriptRoots({ dir });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;
    engine.setVisualShaderCatalog(testVisualCatalog());

    GeneratorParams p = baseParams(1);
    p.generateVisuals = true;
    GeneratedSong song;
    EXPECT_FALSE(engine.generate(p, song, &error));
    EXPECT_NE(error.find("unknown visual shader"), std::string::npos)
        << error;

    fs::remove_all(dir, ec);
}

TEST(GeneratorEngineTest, SectionLengthAndRepeatsExpandSong) {
    GeneratorEngine engine;
    engine.setScriptRoots({ YAWN_GENERATOR_DIR });
    std::string error;
    ASSERT_TRUE(engine.reload(&error)) << error;

    GeneratorParams p = baseParams(77);
    p.sections = { "intro", "verse", "chorus", "outro" };
    p.sectionLength = 2.0f;
    p.repeats = 3;
    GeneratedSong song;
    ASSERT_TRUE(engine.generate(p, song, &error)) << error;

    ASSERT_EQ(song.sections.size(), 12u);   // 4 sections × 3 repeats
    EXPECT_EQ(song.sections[0].bars, 8);    // intro 4 → 8
    EXPECT_EQ(song.sections[1].bars, 16);   // verse 8 → 16
    EXPECT_EQ(song.sections[2].bars, 16);   // chorus 8 → 16
    EXPECT_EQ(song.sections[3].bars, 8);    // outro 4 → 8
    EXPECT_EQ(song.sections[4].id, "intro");
    EXPECT_EQ(song.sections[8].id, "intro");

    // Repeats are regenerated from the same RNG stream, so they vary.
    std::vector<double> first, second;
    for (const auto& c : song.sections[0].clips)
        for (const auto& n : c.notes) first.push_back(n.startBeat);
    for (const auto& c : song.sections[4].clips)
        for (const auto& n : c.notes) second.push_back(n.startBeat);
    EXPECT_FALSE(first.empty());
    EXPECT_FALSE(second.empty());
}
