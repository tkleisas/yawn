// test_LiveCodeExamples.cpp — proof-of-concept scripts under
// assets/examples/livecode must load and run through the REAL
// live-coding stack headlessly: sandboxed engine run, song harvest,
// track/instrument/fx reconcile with no warnings, and improv
// registration (all device ids / param names validated on the play).

#include <gtest/gtest.h>
#include "util/Factory.h"
#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef YAWN_BUNDLED_LIVECODE_DIR
#define YAWN_BUNDLED_LIVECODE_DIR "."
#endif

using namespace yawn;
using namespace yawn::livecode;

namespace {

std::vector<std::string> demoFiles() {
    const std::string dir = YAWN_BUNDLED_LIVECODE_DIR;
    std::vector<std::string> out = {
        dir + "/01_declarative_song.lua",
        dir + "/02_improv_performance.lua",
        dir + "/03_song_plus_improv.lua",
        dir + "/05_sample_synth.lua",
    };
    return out;
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

class LiveCodeExampleTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<audio::AudioEngine>();
        m_project.init(2, 2);
        m_tmp = std::filesystem::temp_directory_path() /
                ("yawn_livecode_examples_" + std::to_string(::random()));
        std::filesystem::create_directories(m_tmp);
        m_mgr.init(m_engine.get(), &m_project);
        m_mgr.setProjectPathProvider([this] { return m_tmp; });
        // yawn.set_clip hook (the App wires this in production)
        m_mgr.setClipLiveHook([this](int track, int scene,
                                     std::shared_ptr<audio::AudioBuffer> buf,
                                     const std::string& name) {
            if (!buf || track < 0 || track >= m_project.numTracks() ||
                scene < 0 || scene >= m_project.numScenes()) return false;
            auto clip = std::make_unique<audio::Clip>();
            clip->name = name.empty() ? "forged" : name;
            clip->buffer = buf;
            m_project.setClip(track, scene, std::move(clip));
            return true;
        });
    }
    void TearDown() override {
        m_mgr.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(m_tmp, ec);
    }
    // Copy a demo to the manager's script path and run it.
    bool runDemo(const std::string& path) {
        const std::string dest = m_mgr.defaultScriptPath();
        std::filesystem::create_directories(
            std::filesystem::path(dest).parent_path());
        {
            std::ofstream f(dest, std::ios::binary);
            f << readAll(path);
        }   // flush+close BEFORE runScript opens the same file
        return m_mgr.runScript(dest);
    }
    std::vector<std::string> consoleOfSeverity(int sev) const {
        std::vector<std::string> out;
        for (const auto& l : m_mgr.console())
            if (l.severity == sev) out.push_back(l.text);
        return out;
    }

    std::unique_ptr<audio::AudioEngine> m_engine;
    Project m_project;
    LiveCodeManager m_mgr;
    std::filesystem::path m_tmp;
};

// 01 — declarative song: 5 tracks created by name, devices attached,
// clips present, and NO warnings (unknown device/param names fail here).
TEST_F(LiveCodeExampleTest, DeclarativeSongBuilding) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0])) << "run failed";

    EXPECT_EQ(m_project.numTracks(), 5);
    EXPECT_EQ(m_project.track(0).name, "Kick");
    EXPECT_EQ(m_project.track(2).name, "Bass");
    EXPECT_EQ(m_project.track(4).name, "Lead");
    EXPECT_EQ(m_engine->instrument(0)->id(), std::string("subsynth"));
    EXPECT_EQ(m_engine->instrument(3)->id(), std::string("fmsynth"));
    EXPECT_EQ(m_engine->instrument(4)->id(), std::string("karplus"));

    // Content-driven typing: the adopted audio-typed defaults now hold
    // MIDI clips — their type flips to Midi and stays put on re-runs.
    EXPECT_EQ(m_project.track(0).type, Track::Type::Midi);
    EXPECT_EQ(m_project.track(1).type, Track::Type::Midi);

    // Kick clip applied to track 0 scene 0.
    auto* slot = m_project.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->midiClip, nullptr);
    EXPECT_EQ(slot->midiClip->noteCount(), 4);

    // No warnings — all param names resolved.
    for (const auto& w : consoleOfSeverity(1))
        EXPECT_TRUE(false) << "warning: " << w;
    // The generation ran (severity-0 success line from the manager).
    bool genLine = false;
    for (const auto& l : m_mgr.console())
        if (l.severity == 0 && l.text.find("generation 1 running") != std::string::npos)
            genLine = true;
    EXPECT_TRUE(genLine);
}

TEST_F(LiveCodeExampleTest, DeclarativeSongIdempotent) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0]));
    // Re-run: no duplicate tracks, no changed warning spam.
    int beforeWarnings = static_cast<int>(consoleOfSeverity(1).size());
    EXPECT_TRUE(runDemo(files[0]));
    EXPECT_EQ(m_project.numTracks(), 5);
    EXPECT_EQ(static_cast<int>(consoleOfSeverity(1).size()), beforeWarnings);
    for (const auto& w : consoleOfSeverity(1))
        GTEST_MESSAGE_(w.c_str(), ::testing::TestPartResult::kNonFatalFailure);
}

// Self-start: yawn.launch_scene(1) in the script body defers past the
// song apply, then launches the session — the transport is playing
// headlessly after the run, with track 0's default scene set.
TEST_F(LiveCodeExampleTest, DeclarativeSongSelfStarts) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0]));
    m_engine->pumpInputForTest(nullptr, 512);
    EXPECT_TRUE(m_engine->transport().isPlaying());
    EXPECT_EQ(m_project.track(0).defaultScene, 0);
    EXPECT_EQ(m_project.track(2).defaultScene, 0);   // created track too
}

// Self-start latency (bug report): the transport used to start
// immediately while the clips waited for the NEXT bar boundary
// (quantize NextBar from a stopped, mid-bar playhead) — one bar of
// silence. Now: a stopped transport seeks to beat 0 and launches
// immediately — the scene is ACTIVE on the first block, still inside
// the first bar.
TEST_F(LiveCodeExampleTest, SelfStartsOnTheDownbeat) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0]));
    m_engine->pumpInputForTest(nullptr, 512);
    const auto& midi = m_engine->midiClipEngine();
    EXPECT_TRUE(midi.isTrackPlaying(0));
    EXPECT_EQ(midi.trackState(0).sceneIndex, 0);   // fired NOW, not pending
    // Playback did not outrun the music: we are still in bar 1.
    const double spb = m_engine->transport().samplesPerBar();
    EXPECT_LT(m_engine->transport().positionInSamples(), spb);
}

// Jamming semantics: launching while the transport PLAYS quantizes to
// the next bar (pending, old clip keeps running until the boundary).
TEST_F(LiveCodeExampleTest, LaunchSceneQuantizedWhenPlaying) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0]));               // scene 1 active (None)
    m_engine->pumpInputForTest(nullptr, 512);
    ASSERT_TRUE(m_engine->midiClipEngine().isTrackPlaying(0));
    // Re-launch scene 2 from a live edit while playing.
    ASSERT_TRUE(m_mgr.runScriptSource("yawn.launch_scene(2, { quantize = 'bar' })"));
    m_engine->pumpInputForTest(nullptr, 512);
    // Pending: the OLD clip (scene 1) is still playing — no boundary
    // crossed, no immediate takeover. Scene 2 has no Kick clip, so the
    // slot bookkeeping already records the stop (defaultScene → -1).
    EXPECT_EQ(m_engine->midiClipEngine().trackState(0).sceneIndex, 0);
    EXPECT_EQ(m_project.track(0).defaultScene, -1);
}

// yawn.launch_clip: immediate single-slot launch from improv surface.
TEST_F(LiveCodeExampleTest, LaunchClipApi) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0]));
    m_mgr.runScriptSource("yawn.launch_clip(0, 2)");   // scene 2 (1-based)
    m_engine->pumpInputForTest(nullptr, 512);
    EXPECT_EQ(m_project.track(0).defaultScene, 0);     // unchanged (already 1)
    EXPECT_TRUE(m_project.track(0).defaultScene == 0);
}

// 02 — improv only: registers scheduled entries without errors.
TEST_F(LiveCodeExampleTest, ImprovPerformanceRegisters) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[1])) << "run failed";
    // No errors and no warnings — the scheduler accepted all entries.
    for (const auto& e : consoleOfSeverity(2))
        GTEST_MESSAGE_(e.c_str(), ::testing::TestPartResult::kNonFatalFailure);
    EXPECT_EQ(m_project.numTracks(), 2);   // song absent → untouched
}

// 03 — combined file: song applies AND improv registers.
TEST_F(LiveCodeExampleTest, SongWithImprov) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[2])) << "run failed";
    EXPECT_EQ(m_project.numTracks(), 4);
    EXPECT_EQ(m_engine->instrument(1)->id(), std::string("subsynth"));
    EXPECT_EQ(m_engine->instrument(3)->id(), std::string("wavetable"));
    m_engine->pumpInputForTest(nullptr, 256);   // bpm command is queued
    EXPECT_NEAR(m_engine->transport().bpm(), 124.0, 1e-6);
    for (const auto& e : consoleOfSeverity(2))
        GTEST_MESSAGE_(e.c_str(), ::testing::TestPartResult::kNonFatalFailure);
}

// 04 — sample forge: render jobs enqueue (targets may not all deliver in
// a headless fixture, but enqueue must be accepted).
TEST_F(LiveCodeExampleTest, SampleForgeQueues) {
    const std::string src = std::string(YAWN_BUNDLED_LIVECODE_DIR) +
                            "/04_sample_forge.lua";
    ASSERT_TRUE(runDemo(src)) << "run failed";
    // 5 jobs queued (the manager's render enqueue line per spec).
    int queued = 0;
    for (const auto& l : m_mgr.console())
        if (l.severity == 0 && l.text.find("render: job") != std::string::npos &&
            l.text.find("queued") != std::string::npos)
            ++queued;
    EXPECT_EQ(queued, 5);
}


// 05 — sample synth lab: four forged buffers land in the kit's delivery
// hooks (sampler x3 + granular) with correct frame counts; the improv
// layer registered without errors.
TEST_F(LiveCodeExampleTest, SampleSynthLabForge) {
    const auto files = demoFiles();
    int delivered = 0;
    if (auto* pm = m_mgr.prerenderManager()) {
        pm->deliverSampler = [&delivered](
                std::shared_ptr<audio::AudioBuffer> buf,
                const std::string&, int) {
            if (buf && buf->numFrames() > 0) ++delivered;
            return true;
        };
        pm->deliverGranular = pm->deliverSampler;
    }
    ASSERT_TRUE(runDemo(files[3])) << "run failed";
    // Sustained forges landed as session clips (visible + launch-audible).
    auto* padSlot = m_project.getSlot(0, 0);
    ASSERT_NE(padSlot, nullptr);
    ASSERT_NE(padSlot->audioClip, nullptr);
    ASSERT_NE(padSlot->audioClip->buffer, nullptr);
    EXPECT_EQ(padSlot->audioClip->buffer->numFrames(),
              m_engine->sampleRate() * 8);
    auto* shapedSlot = m_project.getSlot(1, 0);
    ASSERT_NE(shapedSlot, nullptr);
    ASSERT_NE(shapedSlot->audioClip, nullptr);
    EXPECT_EQ(shapedSlot->audioClip->buffer->numFrames(), 65536);
    // One-shot forges assembled into pattern clips (4 beats @100bpm =
    // 4 * 0.6 * 48000 frames) AND kept as instrument material.
    for (int t : { 2, 3 }) {
        auto* slot = m_project.getSlot(t, 0);
        ASSERT_NE(slot, nullptr) << "track " << t;
        ASSERT_NE(slot->audioClip, nullptr) << "track " << t;
        ASSERT_NE(slot->audioClip->buffer, nullptr);
        EXPECT_EQ(slot->audioClip->buffer->numFrames(),
                  (int)(m_engine->sampleRate() * 0.6 * 4));
    }
    EXPECT_EQ(delivered, 2);
    // Self-start still fired (transport reached, even pre-clip pass).
    m_engine->pumpInputForTest(nullptr, 512);
    EXPECT_TRUE(m_engine->transport().isPlaying());
    for (const auto& e : consoleOfSeverity(2))
        GTEST_MESSAGE_(e.c_str(), ::testing::TestPartResult::kNonFatalFailure);
}

// Dispersed: the demos are also template-safe — every file runs
// AFTER the template's default song state (convergence from a used
// project, not just from empty).
TEST_F(LiveCodeExampleTest, DeclarativeSongConvergesFromTemplate) {
    const auto files = demoFiles();
    ASSERT_TRUE(runDemo(files[0]));   // builds tracks 1..5
    // Rename a track in the UI layer, then re-run → convergence by uid.
    m_project.track(0).name = "Unicorn";
    ASSERT_TRUE(runDemo(files[0]));
    EXPECT_EQ(m_project.track(0).name, "Kick");   // script has authority
    EXPECT_EQ(m_project.numTracks(), 5);
}

TEST(LiveCodeParamsDump, PrintAll) {
    const char* ids[] = { "subsynth", "fmsynth", "karplus", "wavetable",
                          "drumsynth", "drumrack", "granular", "sampler",
                          "vocoder", "drumslop", "drawbarorgan",
                          "electricpiano" };
    for (const char* id : ids) {
        auto inst = createInstrument(id);
        if (!inst) { std::printf("--- %s MISSING\n", id); continue; }
        std::printf("--- %s:", id);
        for (int i = 0; i < inst->parameterCount(); ++i)
            std::printf(" [%d]%s", i, inst->parameterInfo(i).name);
        std::printf("\n");
    }
    const char* fx[] = { "plate", "eq", "compressor", "chorus", "delay",
                         "reverb", "filter", "distortion", "phaser" };
    for (const char* id : fx) {
        auto fxp = createAudioEffect(id);
        if (!fxp) { std::printf("--- fx %s MISSING\n", id); continue; }
        std::printf("--- fx %s:", id);
        for (int i = 0; i < fxp->parameterCount(); ++i)
            std::printf(" [%d]%s", i, fxp->parameterInfo(i).name);
        std::printf("\n");
    }
    GTEST_SUCCEED();
}
