// test_LiveCode.cpp — live-coding layer phase 0 (docs/live-coding.md §7):
// scheduler math (beat anchoring, recurring/jump-back/realignment,
// generations, wall axis), Lua sandbox + budget, yawn.state migration,
// and the yawn.note → command dispatch path.

#include <gtest/gtest.h>
#include "livecode/LiveCodeScheduler.h"
#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"

#include <algorithm>
#include <fstream>

using namespace yawn;   // makes yawn::audio and yawn::livecode reachable
using namespace yawn::livecode;

// ─────────────────────────────────────────────────────────────────────────
// Scheduler
// ─────────────────────────────────────────────────────────────────────────

class SchedulerHarness {
public:
    SchedulerHarness() {
        m_sched.setBeatProvider([this] { return m_beat; });
        m_sched.setPlayingProvider([this] { return m_playing; });
        m_sched.setSecondsProvider([this] { return m_wall; });
        m_sched.setBeatsPerBarProvider([this] { return m_bpb; });
    }

    double m_beat = 0.0;
    bool   m_playing = false;
    double m_wall = 100.0;
    int    m_bpb = 4;
    LiveCodeScheduler m_sched;
};

TEST(LiveCodeSchedulerTest, OneShotFiresWhenBeatReachesTarget) {
    SchedulerHarness h;
    int fired = 0;
    double seenArg = -1.0;
    h.m_sched.atBeat(2.0, 1, [&](double arg) { ++fired; seenArg = arg; });

    h.m_beat = 1.0; h.m_playing = true; h.m_sched.tick();
    EXPECT_EQ(fired, 0);

    h.m_beat = 2.0; h.m_sched.tick();
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(seenArg, 2.0);           // beat-axis entries pass the target beat
    EXPECT_EQ(h.m_sched.pending(), 0); // one-shot removed after firing
}

TEST(LiveCodeSchedulerTest, LookaheadFiresEarlyWithTargetBeat) {
    SchedulerHarness h;
    int fired = 0;
    double seenArg = -1.0;
    h.m_sched.setLookaheadBeats(0.5);  // 0.5-beat schedule-ahead window
    h.m_sched.atBeat(2.0, 1, [&](double arg) { ++fired; seenArg = arg; });

    h.m_beat = 1.4; h.m_playing = true; h.m_sched.tick();
    EXPECT_EQ(fired, 0);               // 2.0 - 0.5 = 1.5 > 1.4 → not due

    h.m_beat = 1.5; h.m_sched.tick();
    EXPECT_EQ(fired, 1);               // fires BEFORE the target beat
    EXPECT_EQ(seenArg, 2.0);           // arg = the upcoming target beat
}

TEST(LiveCodeSchedulerTest, BeatAxisWaitsForPlayback) {
    SchedulerHarness h;
    int fired = 0;
    h.m_sched.atBeat(1.0, 1, [&](double) { ++fired; });

    h.m_beat = 5.0; h.m_playing = false; h.m_sched.tick();
    EXPECT_EQ(fired, 0);               // not playing → beat axis frozen

    h.m_playing = true; h.m_sched.tick();
    EXPECT_EQ(fired, 1);
}

TEST(LiveCodeSchedulerTest, LatePolicyDropRemovesOneShot) {
    SchedulerHarness h;
    int fired = 0;
    h.m_sched.setLookaheadBeats(0.1);
    h.m_sched.setLateDrop(true);
    h.m_sched.atBeat(2.0, 1, [&](double) { ++fired; });

    h.m_playing = true;
    h.m_beat = 1.0; h.m_sched.tick();
    EXPECT_EQ(fired, 0);
    // Simulate a huge UI hitch: beat jumps way past the target.
    h.m_beat = 5.0; h.m_sched.tick();
    EXPECT_EQ(fired, 0);               // dropped, not played late
    EXPECT_EQ(h.m_sched.pending(), 0u);
}

TEST(LiveCodeSchedulerTest, LatePolicyPlayFiresOneShot) {
    SchedulerHarness h;
    int fired = 0;
    double seenArg = 0.0;
    h.m_sched.setLookaheadBeats(0.1);
    h.m_sched.setLateDrop(false);      // default: play now
    h.m_sched.atBeat(2.0, 1, [&](double arg) { ++fired; seenArg = arg; });

    h.m_playing = true;
    h.m_beat = 5.0; h.m_sched.tick();  // hitch past the target
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(seenArg, 2.0);           // still reports its scheduled beat
}

TEST(LiveCodeSchedulerTest, LookaheadDoesNotDoubleFireRecurring) {
    SchedulerHarness h;
    int fired = 0;
    h.m_sched.setLookaheadBeats(0.5);  // horizon = half the interval
    h.m_playing = true;
    h.m_sched.every(1.0, 1, [&](double) { ++fired; });

    // Tick every 0.1 beats across two windows — each entry fires once per
    // interval despite the early-fire window.
    for (int i = 1; i <= 20; ++i) {
        h.m_beat = 0.1 * i;
        h.m_sched.tick();
    }
    EXPECT_EQ(fired, 2);   // targets at 1.0 and 2.0, both fired early
}

TEST(LiveCodeSchedulerTest, RecurringFiresEachInterval) {
    SchedulerHarness h;
    int fired = 0;
    h.m_playing = true;
    h.m_sched.every(1.0, 1, [&](double) { ++fired; });

    for (int step = 1; step <= 5; ++step) {
        h.m_beat = static_cast<double>(step);
        h.m_sched.tick();
    }
    EXPECT_EQ(fired, 5);
}

TEST(LiveCodeSchedulerTest, RecurringSkipsMissedOccurrences) {
    SchedulerHarness h;
    int fired = 0;
    h.m_playing = true;
    h.m_sched.every(1.0, 1, [&](double) { ++fired; });

    h.m_beat = 1.0; h.m_sched.tick();   // fires at beat 1
    h.m_beat = 8.7; h.m_sched.tick();   // long hitch → skip missed, fire once
    EXPECT_EQ(fired, 2);
    // Next fire aligns to the grid after the skip point.
    h.m_beat = 9.2; h.m_sched.tick();
    EXPECT_EQ(fired, 3);
}

TEST(LiveCodeSchedulerTest, JumpBackRealignsRecurring) {
    SchedulerHarness h;
    std::vector<double> fireBeats;
    h.m_playing = true;
    h.m_sched.every(2.0, 1, [&](double) { fireBeats.push_back(h.m_beat); });

    h.m_beat = 4.0; h.m_sched.tick();   // first tick: fires (missed @2 skipped)
    EXPECT_EQ(fireBeats.size(), 1u);
    h.m_beat = 0.0; h.m_sched.tick();   // loop wrap → realign → next = 2
    h.m_beat = 2.0; h.m_sched.tick();   // fires again on the wrapped grid
    EXPECT_EQ(fireBeats.size(), 2u);
}

TEST(LiveCodeSchedulerTest, OnBarPassesOneBasedBarNumber) {
    SchedulerHarness h;
    std::vector<double> bars;
    h.m_playing = true;
    h.m_bpb = 4;
    h.m_sched.onBar(1, [&](double bar) { bars.push_back(bar); });

    h.m_beat = 0.0; h.m_sched.tick();
    EXPECT_TRUE(bars.empty());          // next boundary is strictly after now
    h.m_beat = 4.0; h.m_sched.tick();   // bar 2 starts
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(bars[0], 2.0);
    h.m_beat = 8.0; h.m_sched.tick();
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[1], 3.0);
}

TEST(LiveCodeSchedulerTest, RepetitionsLimit) {
    SchedulerHarness h;
    int fired = 0;
    h.m_playing = true;
    h.m_sched.every(1.0, 1, [&](double) { ++fired; }, /*repetitions=*/2);

    for (int step = 1; step <= 5; ++step) {
        h.m_beat = static_cast<double>(step);
        h.m_sched.tick();
    }
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(h.m_sched.pending(), 0u);
}

TEST(LiveCodeSchedulerTest, WallAxisFiresRegardlessOfPlayback) {
    SchedulerHarness h;
    int fired = 0;
    h.m_playing = false;
    h.m_sched.afterSeconds(0.5, 1, [&](double) { ++fired; });

    h.m_wall = 100.2; h.m_sched.tick();
    EXPECT_EQ(fired, 0);
    h.m_wall = 100.5; h.m_sched.tick();
    EXPECT_EQ(fired, 1);
}

TEST(LiveCodeSchedulerTest, CancelRemovesEntry) {
    SchedulerHarness h;
    int fired = 0;
    auto handle = h.m_sched.every(1.0, 1, [&](double) { ++fired; });
    ASSERT_TRUE(h.m_sched.cancel(handle.id));
    EXPECT_FALSE(h.m_sched.cancel(handle.id));   // second cancel: no-op
    h.m_beat = 10.0; h.m_playing = true; h.m_sched.tick();
    EXPECT_EQ(fired, 0);
}

TEST(LiveCodeSchedulerTest, ClearGenerationOnlyDropsThatGeneration) {
    SchedulerHarness h;
    int firedA = 0, firedB = 0;
    h.m_playing = true;
    h.m_sched.every(1.0, 1, [&](double) { ++firedA; });
    h.m_sched.every(1.0, 2, [&](double) { ++firedB; });

    h.m_sched.clearGeneration(1);
    EXPECT_EQ(h.m_sched.pending(), 1u);

    h.m_beat = 1.0; h.m_sched.tick();
    EXPECT_EQ(firedA, 0);
    EXPECT_EQ(firedB, 1);
}

TEST(LiveCodeSchedulerTest, RemoveListenerSeesEveryRemoval) {
    SchedulerHarness h;
    std::vector<uint64_t> removed;
    h.m_sched.setRemoveListener([&](uint64_t id) { removed.push_back(id); });

    auto a = h.m_sched.every(1.0, 1, [](double) {});
    auto b = h.m_sched.atBeat(5.0, 1, [](double) {});
    h.m_sched.cancel(a.id);
    h.m_sched.clearGeneration(1);
    ASSERT_EQ(removed.size(), 2u);
    EXPECT_EQ(removed[0], a.id);
    EXPECT_EQ(removed[1], b.id);
}

// ─────────────────────────────────────────────────────────────────────────
// LiveCodeManager (sandbox, budget, state migration, dispatch)
// ─────────────────────────────────────────────────────────────────────────

class LiveCodeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Heap per AudioEngine's size warning. No init()/start() — tests
        // drive transport state directly; sendCommand works without a
        // stream. Scripts go to a temp "project" dir, not the real ~/.yawn.
        m_engine = std::make_unique<audio::AudioEngine>();
        m_project.init(2, 2);
        m_tmp = std::filesystem::temp_directory_path() /
                ("yawn_livecode_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::create_directories(m_tmp);
        m_mgr.init(m_engine.get(), &m_project);
        m_mgr.setProjectPathProvider([this] { return m_tmp; });
    }
    void TearDown() override {
        m_mgr.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(m_tmp, ec);
    }

    // Write the default script (creates the livecode/ dir first — the
    // template-creation path in runScript does this, plain ofstream doesn't).
    void writeScript(const std::string& content) {
        const std::string path = m_mgr.defaultScriptPath();
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream f(path, std::ios::binary);
        f << content;
        return;
    }

    std::unique_ptr<audio::AudioEngine> m_engine;
    yawn::Project m_project;
    LiveCodeManager m_mgr;
    std::filesystem::path m_tmp;
};

TEST_F(LiveCodeManagerTest, SandboxStripsUnsafeGlobals) {
    lua_State* L = m_mgr.luaEngine()->state();
    ASSERT_NE(L, nullptr);
    for (const char* g : {"io", "os", "package", "debug",
                          "dofile", "loadfile", "require"}) {
        lua_getglobal(L, g);
        EXPECT_TRUE(lua_isnil(L, -1)) << g << " should be sandboxed away";
        lua_pop(L, 1);
    }
    // Kept libraries:
    for (const char* g : {"string", "table", "math"}) {
        lua_getglobal(L, g);
        EXPECT_TRUE(lua_istable(L, -1)) << g << " should be available";
        lua_pop(L, 1);
    }
}

TEST_F(LiveCodeManagerTest, FirstRunCreatesTemplate) {
    const std::string path = m_mgr.defaultScriptPath();
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(m_mgr.runScript());
    EXPECT_TRUE(std::filesystem::exists(path));
    // Template created but NOT run (no generation active).
    EXPECT_FALSE(m_mgr.isActive());
}

TEST_F(LiveCodeManagerTest, FailedRunDropsOnlyNewGeneration) {
    std::vector<audio::AudioCommand> sent;
    m_mgr.setCommandSender([&sent](const audio::AudioCommand& cmd) {
        sent.push_back(cmd);
    });

    const std::string path = m_mgr.defaultScriptPath();
    writeScript("improv.every(1, function() yawn.note(0, 60, 100, 0, 0) end)\n");
    ASSERT_TRUE(m_mgr.runScript(path));
    ASSERT_EQ(m_mgr.luaEngine()->generation(), 1u);

    m_engine->transport().play();
    // Scheduled at beat 0.5 → first fire at 1.5.
    m_engine->transport().setPositionInSamples(
        static_cast<int64_t>(1.5 * m_engine->transport().samplesPerBeat()));
    m_mgr.update();
    ASSERT_EQ(sent.size(), 1u);   // gen 1 fires

    // Generation 2 fails to parse → gen 1 keeps performing.
    writeScript("improv.every(1, function(  end)\n");
    EXPECT_FALSE(m_mgr.runScript(path));
    EXPECT_EQ(m_mgr.luaEngine()->generation(), 2u);   // bumped, but dropped

    m_engine->transport().setPositionInSamples(
        static_cast<int64_t>(2.5 * m_engine->transport().samplesPerBeat()));
    m_mgr.update();
    ASSERT_EQ(sent.size(), 2u);   // gen 1 still alive
    auto note = std::get_if<audio::SendMidiToTrackMsg>(&sent[1]);
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->note, 60);
}

TEST_F(LiveCodeManagerTest, BudgetBreachFailsTheRun) {
    const std::string path = m_mgr.defaultScriptPath();
    writeScript("while true do end\n");
    EXPECT_FALSE(m_mgr.runScript(path));
    bool sawBudgetError = false;
    for (const auto& line : m_mgr.console())
        if (line.text.find("budget") != std::string::npos) sawBudgetError = true;
    EXPECT_TRUE(sawBudgetError);
    EXPECT_FALSE(m_mgr.isActive());   // nothing ran before the breach
}

TEST_F(LiveCodeManagerTest, StateSurvivesReload) {
    const std::string path = m_mgr.defaultScriptPath();
    // Append-style: each run adds 7 to whatever migrated in. Run 1 → 7;
    // reload harvests 7, injects it, re-runs → 14. If migration broke,
    // the re-run would produce 7 again.
    writeScript("yawn.state.total = (yawn.state.total or 0) + 7\n");
    ASSERT_TRUE(m_mgr.runScript(path));
    m_mgr.reload();
    ASSERT_TRUE(m_mgr.luaEngine());
    lua_State* L = m_mgr.luaEngine()->state();
    lua_getglobal(L, "yawn");
    lua_getfield(L, -1, "state");
    lua_getfield(L, -1, "total");
    ASSERT_TRUE(lua_isnumber(L, -1));
    EXPECT_EQ(lua_tonumber(L, -1), 14.0);
    lua_pop(L, 3);
}

TEST_F(LiveCodeManagerTest, HarvestStateKeepsPrimitivesDropsDeepTables) {
    const std::string path = m_mgr.defaultScriptPath();
    writeScript("yawn.state.a = 1.5\n"
                "yawn.state.b = \"hello\"\n"
                "yawn.state.c = true\n"
                "yawn.state.t = { 10, 20, 30 }\n"
                "yawn.state.nested = { deep = {} }\n");
    ASSERT_TRUE(m_mgr.runScript(path));
    ASSERT_TRUE(m_mgr.luaEngine());
    const auto state = m_mgr.luaEngine()->harvestState();

    ASSERT_EQ(state.count("a"), 1u);
    EXPECT_EQ(state.at("a").kind, LiveCodeEngine::StateVal::Kind::Num);
    EXPECT_EQ(state.at("a").num, 1.5);

    ASSERT_EQ(state.count("b"), 1u);
    EXPECT_EQ(state.at("b").kind, LiveCodeEngine::StateVal::Kind::Str);
    EXPECT_EQ(state.at("b").str, "hello");

    ASSERT_EQ(state.count("c"), 1u);
    EXPECT_EQ(state.at("c").kind, LiveCodeEngine::StateVal::Kind::Bool);
    EXPECT_TRUE(state.at("c").b);

    ASSERT_EQ(state.count("t"), 1u);
    EXPECT_EQ(state.at("t").kind, LiveCodeEngine::StateVal::Kind::Table);
    ASSERT_EQ(state.at("t").table.size(), 3u);
    EXPECT_EQ(state.at("t").table[1].second.num, 20.0);

    // nested (sub-table value) is not a primitive → whole entry dropped.
    EXPECT_EQ(state.count("nested"), 0u);
}

TEST_F(LiveCodeManagerTest, StateArrayTableSurvivesReload) {
    const std::string path = m_mgr.defaultScriptPath();
    // Array-table arithmetic: run 1 → t={10}; reload migrates t (shallow,
    // integer keys); re-run sees it → t={10,20}. Broken migration → {10}.
    writeScript("yawn.state.t = yawn.state.t or {}\n"
                "table.insert(yawn.state.t, 10 + #yawn.state.t)\n");
    ASSERT_TRUE(m_mgr.runScript(path));
    m_mgr.reload();
    ASSERT_TRUE(m_mgr.luaEngine());
    lua_State* L = m_mgr.luaEngine()->state();
    lua_getglobal(L, "yawn");
    lua_getfield(L, -1, "state");
    lua_getfield(L, -1, "t");
    ASSERT_TRUE(lua_istable(L, -1));
    EXPECT_EQ(lua_rawlen(L, -1), 2);   // {10, 20}
    lua_pop(L, 3);
}

// Regression (audit report): top-level integer-keyed yawn.state entries
// used to come back as STRING keys ("42") after migration — #yawn.state
// returned 0 and ipairs broke. injectState now decides on intKey >= 0
// alone and restores with lua_seti.
TEST_F(LiveCodeManagerTest, StateTopLevelIntKeysSurviveReload) {
    const std::string path = m_mgr.defaultScriptPath();
    writeScript(
        "yawn.state = { [1] = 10, [2] = 20, [3] = 30, label = \"L\" }\n");
    ASSERT_TRUE(m_mgr.runScript(path));
    m_mgr.reload();
    ASSERT_TRUE(m_mgr.luaEngine());

    lua_State* L = m_mgr.luaEngine()->state();
    lua_getglobal(L, "yawn");
    lua_getfield(L, -1, "state");
    ASSERT_TRUE(lua_istable(L, -1));   // migrated table exists
    EXPECT_EQ(lua_rawlen(L, -1), 3);   // sequence border intact: {10,20,30}

    // Values live under TRUE integer keys (t[1/2/3]), not strings.
    for (int i = 1; i <= 3; ++i) {
        lua_geti(L, -1, i);
        ASSERT_TRUE(lua_isnumber(L, -1)) << "integer key " << i
                                          << " did not survive";
        EXPECT_EQ(lua_tonumber(L, -1), 10.0 * i);
        lua_pop(L, 1);
    }
    lua_getfield(L, -1, "label");
    ASSERT_TRUE(lua_isstring(L, -1));  // string key untouched by the fix
    EXPECT_STREQ(lua_tostring(L, -1), "L");
    lua_pop(L, 1);

    // ipairs walks the sequence after migration (3 iterations → 30).
    EXPECT_EQ(luaL_dostring(m_mgr.luaEngine()->state(),
        "local n = 0; local last = nil\n"
        "for _, v in ipairs(yawn.state) do n = n + 1; last = v end\n"
        "if last ~= 30 then error('ipairs walked ' .. tostring(n) ..\n"
        "    ' entries, last=' .. tostring(last)) end\n"),
        LUA_OK) << "ipairs broke after migration";
    lua_pop(L, 2);   // pop state + yawn
}

TEST_F(LiveCodeManagerTest, NoteDispatchesNoteOnAndScheduledNoteOff) {
    std::vector<audio::AudioCommand> sent;
    m_mgr.setCommandSender([&sent](const audio::AudioCommand& cmd) {
        sent.push_back(cmd);
    });

    // Deterministic wall clock (transport NOT playing → wall-axis fallback).
    double wall = 500.0;
    m_mgr.setWallSecondsProvider([&wall] { return wall; });
    m_engine->transport().setBPM(120.0);   // 1 beat = 0.5 s

    const std::string path = m_mgr.defaultScriptPath();
    writeScript("yawn.note(0, 60, 100, 2.0, 0)\n");   // 2-beat note = 1.0 s
    ASSERT_TRUE(m_mgr.runScript(path));
    ASSERT_EQ(sent.size(), 1u);                    // immediate note-on
    auto on = std::get_if<audio::SendMidiToTrackMsg>(&sent[0]);
    ASSERT_NE(on, nullptr);
    EXPECT_EQ(on->trackIndex, 0);
    EXPECT_EQ(on->note, 60);
    EXPECT_EQ(on->type, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn));

    m_mgr.update();                                // wall entry pending
    wall = 500.9;
    m_mgr.update();
    EXPECT_EQ(sent.size(), 1u);                    // not yet due
    wall = 501.0;
    m_mgr.update();
    ASSERT_EQ(sent.size(), 2u);                    // note-off fired
    auto off = std::get_if<audio::SendMidiToTrackMsg>(&sent[1]);
    ASSERT_NE(off, nullptr);
    EXPECT_EQ(off->note, 60);
    EXPECT_EQ(off->type, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff));
}

TEST_F(LiveCodeManagerTest, EveryFiresNotesWhileTransportPlays) {
    std::vector<audio::AudioCommand> sent;
    m_mgr.setCommandSender([&sent](const audio::AudioCommand& cmd) {
        sent.push_back(cmd);
    });

    const std::string path = m_mgr.defaultScriptPath();
    writeScript("improv.every(1, function() yawn.note(0, 60, 100, 0, 0) end)\n");
    ASSERT_TRUE(m_mgr.runScript(path));
    m_engine->transport().play();

    const double spb = m_engine->transport().samplesPerBeat();
    for (int step = 1; step <= 3; ++step) {
        m_engine->transport().setPositionInSamples(
            static_cast<int64_t>(step * spb));
        m_mgr.update();
    }
    EXPECT_EQ(sent.size(), 3u);
    for (const auto& cmd : sent) {
        auto note = std::get_if<audio::SendMidiToTrackMsg>(&cmd);
        ASSERT_NE(note, nullptr);
        EXPECT_EQ(note->note, 60);
    }
}

TEST_F(LiveCodeManagerTest, StopKeepsPendingNoteOffs) {
    std::vector<audio::AudioCommand> sent;
    m_mgr.setCommandSender([&sent](const audio::AudioCommand& cmd) {
        sent.push_back(cmd);
    });
    double wall = 100.0;
    m_mgr.setWallSecondsProvider([&wall] { return wall; });
    m_engine->transport().setBPM(120.0);

    const std::string path = m_mgr.defaultScriptPath();
    writeScript("yawn.note(0, 48, 100, 1.0, 0)\n");   // 0.5 s hold
    ASSERT_TRUE(m_mgr.runScript(path));
    m_mgr.update();
    m_mgr.stop();                                  // drop callbacks

    wall = 100.4; m_mgr.update();
    EXPECT_EQ(sent.size(), 1u);
    wall = 100.5; m_mgr.update();
    EXPECT_EQ(sent.size(), 2u);                    // note-off still released
}

// ─────────────────────────────────────────────────────────────────────────
// Audio-side scheduled-note queue (headless processAudio via
// pumpInputForTest — docs/live-coding.md §3.2)
// ─────────────────────────────────────────────────────────────────────────

class LiveCodeAudioQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<audio::AudioEngine>();
        m_engine->initHeadlessForTest();
        m_engine->transport().setBPM(120.0);   // 1 beat = 0.5 s = 24000 frames
        m_engine->transport().play();
    }

    std::unique_ptr<audio::AudioEngine> m_engine;
    static constexpr int kBlock = 256;         // frames per pump
    static constexpr double kBeatsPerPump = kBlock * 120.0 / 60.0 / 48000.0;

    void pumpBeats(double beats) {
        const int pumps = static_cast<int>(std::ceil(beats / kBeatsPerPump));
        for (int i = 0; i < pumps; ++i)
            m_engine->pumpInputForTest(nullptr, kBlock);
    }
};

TEST_F(LiveCodeAudioQueueTest, FutureNoteParksThenFiresSampleAccurately) {
    audio::SendMidiToTrackMsg msg{
        0, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
        0, 60, midi::Convert::vel7to16(127), 0, 0, /*atBeat=*/1.0};
    m_engine->sendCommand(msg);
    m_engine->pumpInputForTest(nullptr, kBlock);   // beat 0 → parked
    EXPECT_EQ(m_engine->scheduledPendingForTest(), 1);

    // Advance most of the way; the note is still parked.
    pumpBeats(0.90);                               // beat ≈ 0.96
    EXPECT_EQ(m_engine->scheduledPendingForTest(), 1);

    // Pump until its block arrives (block window contains beat 1.0).
    bool drained = false;
    for (int i = 0; i < 20 && !drained; ++i) {
        m_engine->pumpInputForTest(nullptr, kBlock);
        drained = (m_engine->scheduledPendingForTest() == 0);
    }
    EXPECT_TRUE(drained);
}

TEST_F(LiveCodeAudioQueueTest, SeekFlushesPendingNotes) {
    audio::SendMidiToTrackMsg msg{
        0, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
        0, 60, midi::Convert::vel7to16(127), 0, 0, /*atBeat=*/10.0};
    m_engine->sendCommand(msg);
    m_engine->pumpInputForTest(nullptr, kBlock);   // parked
    EXPECT_EQ(m_engine->scheduledPendingForTest(), 1);

    // A seek invalidates queued notes (musical stale).
    m_engine->sendCommand(audio::TransportSetPositionMsg{0});
    m_engine->pumpInputForTest(nullptr, kBlock);
    EXPECT_EQ(m_engine->scheduledPendingForTest(), 0);
    EXPECT_GT(m_engine->schedFlushedForTest(), 0);
}

TEST_F(LiveCodeAudioQueueTest, LatePolicyDropDiscards) {
    // Late = the command ARRIVES after its beat already passed (UI hitch
    // longer than the lookahead). Simulate headlessly: overtake the beat,
    // then send the beat-anchored message.
    m_engine->transport().setPositionInSamples(
        static_cast<int64_t>(6.0 * m_engine->transport().samplesPerBeat()));
    m_engine->pumpInputForTest(nullptr, kBlock);

    audio::SendMidiToTrackMsg msg{
        0, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
        0, 60, midi::Convert::vel7to16(127), 0, 0, /*atBeat=*/5.0};
    m_engine->sendCommand(audio::SetSchedLatePolicyMsg{true});
    m_engine->sendCommand(msg);
    m_engine->pumpInputForTest(nullptr, kBlock);
    EXPECT_EQ(m_engine->schedLateDroppedForTest(), 1);
    EXPECT_EQ(m_engine->scheduledPendingForTest(), 0);
}

TEST_F(LiveCodeAudioQueueTest, LatePolicyDefaultPlaysNow) {
    m_engine->transport().setPositionInSamples(
        static_cast<int64_t>(6.0 * m_engine->transport().samplesPerBeat()));
    m_engine->pumpInputForTest(nullptr, kBlock);

    audio::SendMidiToTrackMsg msg{
        0, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
        0, 60, midi::Convert::vel7to16(127), 0, 0, /*atBeat=*/5.0};
    m_engine->sendCommand(msg);   // late policy default: play
    m_engine->pumpInputForTest(nullptr, kBlock);
    EXPECT_EQ(m_engine->schedLateDroppedForTest(), 0);   // played, not dropped
}

TEST_F(LiveCodeAudioQueueTest, ManagerSchedulesAtBeatNoteThroughQueue) {
    // yawn.note(..., at_beat) → command with atBeat → parked → drained.
    audio::AudioEngine* e = m_engine.get();
    std::vector<audio::AudioCommand> sent;
    yawn::Project project;
    project.init(2, 2);
    livecode::LiveCodeManager mgr;
    mgr.init(e, &project);
    mgr.setCommandSender([&sent](const audio::AudioCommand& cmd) {
        sent.push_back(cmd);
    });
    mgr.setProjectPathProvider([this] {
        return std::filesystem::temp_directory_path() / "yawn_livecode_audioq";
    });

    const std::string path = mgr.defaultScriptPath();
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    {
        std::ofstream f(path, std::ios::binary);
        f << "yawn.note(0, 60, 100, 1.0, 0, 2.0)\n";   // note at beat 2
    }
    ASSERT_TRUE(mgr.runScript(path));

    // Forward the scripted command into the engine's queue.
    for (const auto& cmd : sent) e->sendCommand(cmd);

    m_engine->pumpInputForTest(nullptr, kBlock);   // beat 0 → parked
    EXPECT_EQ(m_engine->scheduledPendingForTest(), 2);   // on + off (dur 1)
    // Note-off lands at beat 3 — pump past it.
    bool drained = false;
    for (int i = 0; i < 350 && !drained; ++i) {
        m_engine->pumpInputForTest(nullptr, kBlock);
        drained = (m_engine->scheduledPendingForTest() == 0);
    }
    EXPECT_TRUE(drained);
    mgr.shutdown();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "yawn_livecode_audioq", ec);
}

// ─────────────────────────────────────────────────────────────────────────
// Phase 2: song layer (uid identity, parser, diff-apply)
// ─────────────────────────────────────────────────────────────────────────

#include "util/ProjectSerializer.h"
#include "util/Factory.h"
#include "livecode/LiveCodeSong.h"
#include "midi/MidiClip.h"

using yawn::livecode::SongModel;
using yawn::livecode::SongApplyReport;
using yawn::livecode::applySongModel;
using yawn::livecode::SongApplyContext;

TEST(LiveCodeUid, AssignedOnCreateAndStableAcrossRoundTrip) {
    auto engine = std::make_unique<audio::AudioEngine>();
    yawn::Project p;
    p.init(1, 1);
    p.addTrack("Kick", yawn::Track::Type::Midi);
    p.addTrack("Bass", yawn::Track::Type::Midi);
    const uint64_t uid0 = p.track(0).uid;
    const uint64_t uid1 = p.track(1).uid;
    EXPECT_NE(uid0, 0u);
    EXPECT_NE(uid0, uid1);
    EXPECT_EQ(p.findTrackByUid(uid1), 1);

    // Save + load → uids preserved.
    const auto dir = std::filesystem::temp_directory_path() / "yawn_uid_rt";
    std::filesystem::remove_all(dir);
    ASSERT_TRUE(yawn::ProjectSerializer::saveToFolder(dir, p, *engine.get(), nullptr));
    yawn::Project p2;
    p2.init(1, 1);
    // (loadFromFolder signature: folder, project, engine, projectDirForAssets)
    ASSERT_TRUE(yawn::ProjectSerializer::loadFromFolder(dir, p2, *engine.get(), nullptr));
    EXPECT_EQ(p2.track(0).uid, uid0);
    EXPECT_EQ(p2.track(1).uid, uid1);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(LiveCodeUid, LegacyProjectsBackfillOnLoad) {
    // Serialize a project, strip the uids from the JSON, reload → uids
    // assigned (one-time migration).
    auto engine = std::make_unique<audio::AudioEngine>();
    yawn::Project p;
    p.init(1, 1);
    p.addTrack("Legacy", yawn::Track::Type::Midi);
    const auto dir = std::filesystem::temp_directory_path() / "yawn_uid_legacy";
    std::filesystem::remove_all(dir);
    ASSERT_TRUE(yawn::ProjectSerializer::saveToFolder(dir, p, *engine.get(), nullptr));
    {   // strip uid
        std::ifstream f(dir / "project.json");
        nlohmann::json j = nlohmann::json::parse(f);
        for (auto& tj : j["tracks"]) tj.erase("uid");
        std::ofstream o(dir / "project.json");
        o << j.dump();
    }
    yawn::Project p2;
    p2.init(1, 1);
    ASSERT_TRUE(yawn::ProjectSerializer::loadFromFolder(dir, p2, *engine.get(), nullptr));
    EXPECT_NE(p2.track(0).uid, 0u);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(LiveCodeSongTest, ParseBasics) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    const char* code =
        "song = {\n"
        "  bpm = 128,\n"
        "  scenes = 4,\n"
        "  tracks = {\n"
        "    { name = 'Kick', type = 'midi', volume = 0.9, pan = -0.5,\n"
        "      mute = false, solo = true,\n"
        "      instrument = { id = 'subsynth', params = { ['Filter Cutoff'] = 0.7, [3] = 0.2 } },\n"
        "      fx = { { id = 'reverb', params = { decay = 0.4 } } },\n"
        "      clips = { [1] = { beats = 4, notes = { {0, 0.5, 36, 1}, {1, 0.5, 38, 0.7} } } },\n"
        "    },\n"
        "  },\n"
        "}\n";
    ASSERT_EQ(luaL_dostring(L, code), LUA_OK);
    lua_getglobal(L, "song");
    SongModel m;
    std::string err;
    ASSERT_TRUE(yawn::livecode::parseSongModel(L, lua_gettop(L), m, err)) << err;
    ASSERT_TRUE(m.bpm); EXPECT_EQ(*m.bpm, 128.0);
    ASSERT_TRUE(m.scenes); EXPECT_EQ(*m.scenes, 4);
    ASSERT_EQ(m.tracks.size(), 1u);
    const auto& t = m.tracks[0];
    EXPECT_EQ(t.name, "Kick"); EXPECT_TRUE(t.hasName);
    EXPECT_EQ(t.type, "midi");
    ASSERT_TRUE(t.volume); EXPECT_EQ(*t.volume, 0.9);
    ASSERT_TRUE(t.pan); EXPECT_EQ(*t.pan, -0.5);
    ASSERT_TRUE(t.mute); EXPECT_FALSE(*t.mute);
    ASSERT_TRUE(t.solo); EXPECT_TRUE(*t.solo);
    ASSERT_TRUE(t.hasInstrument);
    EXPECT_EQ(t.instrument.id, "subsynth");
    ASSERT_EQ(t.instrument.params.size(), 2u);
    // Lua table order is unspecified — find by key shape.
    const yawn::livecode::SongParam* named = nullptr;
    const yawn::livecode::SongParam* indexed = nullptr;
    for (const auto& p : t.instrument.params) {
        if (p.byIndex) indexed = &p;
        else if (p.name == "Filter Cutoff") named = &p;
    }
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->value, 0.7);
    ASSERT_NE(indexed, nullptr);
    EXPECT_EQ(indexed->index, 3);
    EXPECT_EQ(indexed->value, 0.2);
    ASSERT_TRUE(t.hasFx);
    ASSERT_EQ(t.fx.size(), 1u);
    EXPECT_EQ(t.fx[0].id, "reverb");
    ASSERT_TRUE(t.hasClips);
    ASSERT_EQ(t.clips.count(1), 1u);
    EXPECT_EQ(t.clips.at(1).beats, 4.0);
    ASSERT_EQ(t.clips.at(1).notes.size(), 2u);
    EXPECT_EQ(t.clips.at(1).notes[1].pitch, 38.0);
    EXPECT_EQ(t.clips.at(1).notes[1].vel, 0.7);
    lua_close(L);
}

TEST(LiveCodeSongTest, ParseErrorsReported) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaL_dostring(L, "song = { tracks = { { uid = 'bad-uid!' } } }");
    lua_getglobal(L, "song");
    SongModel m;
    std::string err;
    EXPECT_FALSE(yawn::livecode::parseSongModel(L, lua_gettop(L), m, err));
    EXPECT_NE(err.find("uid"), std::string::npos);
    lua_close(L);
}

class LiveCodeApplyTest : public LiveCodeManagerTest {};

TEST_F(LiveCodeApplyTest, ApplyCreatesTrackAndIsIdempotent) {
    SongModel song;
    auto& st = song.tracks.emplace_back();
    st.hasName = true; st.name = "Kick";
    st.type = "midi";
    st.volume = 0.8;
    st.hasInstrument = true; st.instrument.id = "subsynth";
    st.instrument.params.push_back(yawn::livecode::SongParam{false, 0, "Filter Cutoff", 0.7});
    st.hasFx = true;
    st.fx.push_back(yawn::livecode::SongDevice{"reverb", {}});

    SongApplyContext ctx;
    ctx.project = &m_project;
    ctx.engine = m_engine.get();
    std::set<uint64_t> owned;

    const SongApplyReport rep = applySongModel(song, owned, ctx);
    EXPECT_TRUE(rep.changed);
    EXPECT_TRUE(rep.warnings.empty()) << (rep.warnings.empty() ? "" : rep.warnings[0]);
    ASSERT_EQ(m_project.numTracks(), 3);   // 2 default + 1 created
    EXPECT_EQ(m_project.track(2).name, "Kick");
    EXPECT_EQ(m_project.track(2).volume, 0.8f);
    auto* inst = m_engine->instrument(2);
    ASSERT_NE(inst, nullptr);
    EXPECT_STREQ(inst->id(), "subsynth");
    // cutoff applied (find its index by name to be robust)
    int cutoffIdx = -1;
    for (int i = 0; i < inst->parameterCount(); ++i)
        if (std::string(inst->parameterInfo(i).name) == "Filter Cutoff") cutoffIdx = i;
    ASSERT_GE(cutoffIdx, 0);
    EXPECT_NEAR(inst->getParameter(cutoffIdx), 0.7f, 1e-4f);
    auto* fx = m_engine->mixer().trackEffects(2).effectAt(0);
    ASSERT_NE(fx, nullptr);
    EXPECT_STREQ(fx->id(), "reverb");
    EXPECT_EQ(owned.size(), 1u);   // the created track is script-owned

    // Re-apply the same song → no-op (idempotence contract §5.2).
    const SongApplyReport rep2 = applySongModel(song, owned, ctx);
    EXPECT_FALSE(rep2.changed);
    EXPECT_TRUE(rep2.ops.empty());
    EXPECT_EQ(m_project.numTracks(), 3);
}

TEST_F(LiveCodeApplyTest, AdoptByNameThenParamConvergence) {
    // A user track exists; the song adopts it by name and tweaks it.
    m_project.track(0).name = "Pad";
    SongModel song;
    auto& st = song.tracks.emplace_back();
    st.hasName = true; st.name = "Pad";
    st.volume = 1.5;

    SongApplyContext ctx;
    ctx.project = &m_project;
    ctx.engine = m_engine.get();
    std::set<uint64_t> owned;

    const SongApplyReport rep = applySongModel(song, owned, ctx);
    EXPECT_TRUE(rep.changed);
    EXPECT_EQ(m_project.numTracks(), 2);   // no new track
    EXPECT_EQ(m_project.track(0).volume, 1.5f);
    EXPECT_EQ(owned.size(), 1u);           // adopted → owned

    // Same song again → no-op.
    const SongApplyReport rep2 = applySongModel(song, owned, ctx);
    EXPECT_FALSE(rep2.changed);
}

TEST_F(LiveCodeApplyTest, RemoveUndeclaredOwnedTrack) {
    // Song declares Kick; apply; then remove it from the song → the
    // script-owned track is deleted (transport stopped, last track).
    const int before = m_project.numTracks();
    SongModel song;
    auto& st = song.tracks.emplace_back();
    st.hasName = true; st.name = "Kick";
    SongApplyContext ctx;
    ctx.project = &m_project;
    ctx.engine = m_engine.get();
    std::set<uint64_t> owned;
    (void)applySongModel(song, owned, ctx);
    ASSERT_EQ(m_project.numTracks(), before + 1);
    // Kick was appended last.
    const uint64_t kickUid = m_project.track(before).uid;

    SongModel song2;   // empty song → nothing declared
    const SongApplyReport rep2 = applySongModel(song2, owned, ctx);
    EXPECT_TRUE(rep2.changed);
    EXPECT_EQ(m_project.numTracks(), before);
    EXPECT_EQ(m_project.findTrackByUid(kickUid), -1);   // gone
    EXPECT_TRUE(owned.empty());
}

TEST_F(LiveCodeApplyTest, ClipsReplaceNotes) {
    // Track 0 gets a MIDI clip at scene 1 (Lua 1-based) with two notes.
    SongModel song;
    auto& st = song.tracks.emplace_back();
    st.hasName = true; st.name = m_project.track(0).name;
    st.hasClips = true;
    auto& c = st.clips[1];
    c.beats = 4.0;
    c.notes = {{0.0, 0.5, 36, 1.0, 0}, {1.0, 0.5, 38, 0.7, 0}};

    SongApplyContext ctx;
    ctx.project = &m_project;
    ctx.engine = m_engine.get();
    std::set<uint64_t> owned;
    const SongApplyReport rep = applySongModel(song, owned, ctx);
    EXPECT_TRUE(rep.changed);
    auto* slot = m_project.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->midiClip, nullptr);
    EXPECT_EQ(slot->midiClip->lengthBeats(), 4.0);
    ASSERT_EQ(slot->midiClip->noteCount(), 2);
    EXPECT_EQ(slot->midiClip->note(0).pitch, 36);
    EXPECT_EQ(slot->midiClip->note(1).velocity, static_cast<uint16_t>(0.7 * 65535.0));

    // Re-apply with an EMPTY notes list → authoritative clear (presence
    // of the notes key = ownership, §5.1).
    auto& c2 = song.tracks[0].clips[1];
    c2.notes = {};
    const SongApplyReport rep2 = applySongModel(song, owned, ctx);
    EXPECT_TRUE(rep2.changed);
    auto* slot2 = m_project.getSlot(0, 0);
    ASSERT_NE(slot2, nullptr);
    ASSERT_NE(slot2->midiClip, nullptr);
    EXPECT_EQ(slot2->midiClip->noteCount(), 0);
}

TEST_F(LiveCodeApplyTest, ApplySongThroughManager) {
    // End-to-end: a script that sets the song global; the manager applies.
    writeScript(
        "song = {\n"
        "  bpm = 132,\n"
        "  tracks = {\n"
        "    { name = 'SongKick', type = 'midi', volume = 1.2,\n"
        "      instrument = { id = 'fmsynth' } },\n"
        "  },\n"
        "}\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    ASSERT_EQ(m_project.numTracks(), 3);
    EXPECT_EQ(m_project.track(2).name, "SongKick");
    EXPECT_EQ(m_engine->instrument(2)->id(), std::string("fmsynth"));
    // bpm command is queued (not yet consumed headlessly) — pump it.
    m_engine->pumpInputForTest(nullptr, 256);
    EXPECT_NEAR(m_engine->transport().bpm(), 132.0, 1e-6);

    // Re-run the same script → converges (no duplicate track).
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    EXPECT_EQ(m_project.numTracks(), 3);
}

// ─────────────────────────────────────────────────────────────────────────
// Phase 3: prerender (InstrumentRenderer + PrerenderManager + Lua API)
// ─────────────────────────────────────────────────────────────────────────

#include "audio/Prerender.h"
#include "instruments/Sampler.h"
#include "util/FileIO.h"

using yawn::audio::PrerenderSpec;
using yawn::audio::PrerenderNote;
using yawn::audio::renderDevice;

static float bufferRms(const audio::AudioBuffer& b) {
    if (b.isEmpty()) return 0.0f;
    double acc = 0.0;
    const size_t n = b.totalSamples();
    for (size_t i = 0; i < n; ++i) {
        const float v = b.data()[i];
        acc += static_cast<double>(v) * v;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

TEST(LiveCodePrerender, RendersAudibleAudio) {
    PrerenderSpec spec;
    spec.device = "fmsynth";
    spec.notes.push_back(PrerenderNote{0.0, 1.0, 60, 1.0f, 0});
    spec.lengthBeats = 2.0;
    spec.tailBeats = 1.0;

    std::atomic<float> prog{0.0f};
    std::atomic<bool> cancel{false};
    auto buf = renderDevice(spec, prog, cancel);
    ASSERT_NE(buf, nullptr);
    EXPECT_GT(buf->numFrames(), 0);
    EXPECT_GT(bufferRms(*buf), 1e-4f);   // audible, not silence
    EXPECT_FLOAT_EQ(prog.load(), 1.0f);
}

TEST(LiveCodePrerender, DeterministicForSameSpec) {
    PrerenderSpec spec;
    spec.device = "subsynth";
    spec.notes.push_back(PrerenderNote{0.5, 0.75, 64, 0.9f, 0});
    spec.notes.push_back(PrerenderNote{2.0, 0.25, 67, 0.9f, 0});
    spec.lengthBeats = 4.0;

    std::atomic<float> prog{0.0f};
    std::atomic<bool> cancel{false};
    auto a = renderDevice(spec, prog, cancel);
    auto b = renderDevice(spec, prog, cancel);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(a->numFrames(), b->numFrames());
    EXPECT_EQ(0, std::memcmp(a->data(), b->data(),
                             a->totalSamples() * sizeof(float)));
}

TEST(LiveCodePrerender, CancelProducesNothing) {
    PrerenderSpec spec;
    spec.device = "fmsynth";
    spec.notes.push_back(PrerenderNote{0.0, 0.5, 60, 1.0f, 0});
    spec.lengthBeats = 4.0;

    std::atomic<float> prog{0.0f};
    std::atomic<bool> cancel{false};
    cancel.store(true);
    EXPECT_EQ(renderDevice(spec, prog, cancel), nullptr);
}

TEST(LiveCodePrerender, UnknownDeviceFails) {
    PrerenderSpec spec;
    spec.device = "not_a_synth";
    std::atomic<float> prog{0.0f};
    std::atomic<bool> cancel{false};
    EXPECT_EQ(renderDevice(spec, prog, cancel), nullptr);
}

TEST(LiveCodePrerender, FxChainProcessed) {
    // Delay with heavy feedback would keep ringing past the note; a
    // reverb-only chain differs from a dry render — assert the output
    // has energy beyond the note end (tail from FX).
    PrerenderSpec dry, wet;
    dry.device = wet.device = "fmsynth";
    dry.notes.push_back(PrerenderNote{0.0, 0.25, 72, 1.0f, 0});
    wet.notes = dry.notes;
    wet.fx.push_back({"delay", {}});

    std::atomic<float> prog{0.0f};
    std::atomic<bool> cancel{false};
    auto a = renderDevice(dry, prog, cancel);
    auto b = renderDevice(wet, prog, cancel);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(0, std::memcmp(a->data(), b->data(),
                             std::min(a->totalSamples(), b->totalSamples()) * sizeof(float)));
}

TEST(LiveCodePrerender, GenerativeSourceDeterministicWithSeed) {
    PrerenderSpec spec;
    spec.device = "fmsynth";
    spec.seed = 42;
    int count = 0;
    spec.noteSource = [&](double beat, int frames, midi::MidiBuffer& out) {
        // Deterministic pattern from the beat position: note every beat.
        const double b = std::floor(beat) + 1.0;
        if (b < 4.0 && !out.empty()) {}   // (guard irrelevant)
        midi::MidiMessage m = midi::MidiMessage::noteOn(0, 60 + static_cast<int>(b) % 12, 127);
        m.frameOffset = std::clamp(static_cast<int32_t>((b - beat) * 24000.0), 0, frames - 1);
        out.addMessage(m);
        ++count;
    };
    spec.lengthBeats = 4.0;

    std::atomic<float> prog{0.0f};
    std::atomic<bool> cancel{false};
    auto a = renderDevice(spec, prog, cancel);
    auto b = renderDevice(spec, prog, cancel);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(0, std::memcmp(a->data(), b->data(), a->totalSamples() * sizeof(float)));
    EXPECT_GT(count, 0);
}

TEST_F(LiveCodeManagerTest, PrerenderManagerEnqueueDeliverCancel) {
    auto* pm = m_mgr.prerenderManager();
    ASSERT_NE(pm, nullptr);
    pm->init(1);

    std::vector<std::string> reports;
    pm->report = [&](const std::string& m) { reports.push_back(m); };
    bool clipDelivered = false;
    std::string deliveredName;
    pm->deliverClip = [&](const std::shared_ptr<audio::AudioBuffer>& buf,
                          int track, int scene, const std::string& name) {
        clipDelivered = true;
        deliveredName = name;
        EXPECT_GT(buf->numFrames(), 0);
        EXPECT_EQ(track, 0);
        EXPECT_EQ(scene, 1);
        return true;
    };

    PrerenderSpec spec;
    spec.device = "fmsynth";
    spec.notes.push_back(PrerenderNote{0.0, 0.5, 60, 1.0f, 0});
    spec.lengthBeats = 1.0;

    PrerenderManager::Target target;
    target.kind = "clip";
    target.track = 0;
    target.scene = 1;
    target.name = "test_render";
    const uint64_t id = m_mgr.prerender(spec, target);
    ASSERT_GT(id, 0u);

    // Spin until delivered (worker + poll through the manager).
    for (int i = 0; i < 200 && !clipDelivered; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        m_mgr.update();
    }
    EXPECT_TRUE(clipDelivered);
    EXPECT_EQ(deliveredName, "test_render");
    bool sawDeliveryReport = false;
    for (const auto& r : reports)
        if (r.find("delivered") != std::string::npos) sawDeliveryReport = true;
    EXPECT_TRUE(sawDeliveryReport);
}

TEST_F(LiveCodeManagerTest, RenderFromLuaAppliesNameResolution) {
    auto* pm = m_mgr.prerenderManager();
    ASSERT_NE(pm, nullptr);
    pm->init(1);
    bool delivered = false;
    pm->deliverSampler = [&](std::shared_ptr<audio::AudioBuffer> buf,
                             const std::string&, int) {
        delivered = (buf != nullptr);
        return true;
    };

    writeScript(
        "yawn.render{ device = 'subsynth',\n"
        "              params = { ['Filter Cutoff'] = 0.8, Volume = 1.0 },\n"
        "              notes = { {0, 0.5, 60, 1.0} },\n"
        "              beats = 1, tail = 0.5,\n"
        "              target = { kind = 'sampler', track = 0 } }\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    EXPECT_TRUE(m_mgr.cancelPrerender(9999));   // unknown id: harmless

    for (int i = 0; i < 200 && !delivered; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        m_mgr.update();
    }
    EXPECT_TRUE(delivered);
}

// ─────────────────────────────────────────────────────────────────────────
// Phase 4: authoring API + buffer handles; Phase 5: persistence
// ─────────────────────────────────────────────────────────────────────────

TEST_F(LiveCodeManagerTest, SetNotesRoundTrip) {
    writeScript(
        "yawn.set_notes(0, 1, { {0, 0.5, 60, 1.0}, {2, 0.5, 64, 0.5} })\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    auto* slot = m_project.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->midiClip, nullptr);
    ASSERT_EQ(slot->midiClip->noteCount(), 2);
    EXPECT_EQ(slot->midiClip->note(1).pitch, 64);

    // get_notes round-trips normalized values back out.
    writeScript(
        "local ns = yawn.get_notes(0, 1)\n"
        "assert(ns and #ns == 2, 'note count')\n"
        "assert(math.abs(ns[2].vel - 0.5) < 1e-3, 'vel')\n"
        "assert(ns[1].start == 0 and ns[2].start == 2, 'starts')\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));

    // clear_notes empties, keeps length.
    writeScript("yawn.clear_notes(0, 1)\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    auto* slot2 = m_project.getSlot(0, 0);
    ASSERT_NE(slot2->midiClip, nullptr);
    EXPECT_EQ(slot2->midiClip->noteCount(), 0);
    EXPECT_GT(slot2->midiClip->lengthBeats(), 0.0);
}

TEST_F(LiveCodeManagerTest, BufferHandleRoundTripViaFile) {
    // Build a small wav, load it through the handle API from a script,
    // save it back, and compare frame counts.
    audio::AudioBuffer src(2, 4800);
    for (int f = 0; f < 4800; ++f) {
        src.sample(0, f) = 0.5f * std::sin(float(f) * 0.01f);
        src.sample(1, f) = 0.25f * std::sin(float(f) * 0.02f);
    }
    const auto dir = std::filesystem::temp_directory_path() / "yawn_buf_rt";
    std::filesystem::create_directories(dir);
    const std::string inPath = (dir / "in.wav").string();
    const std::string outPath = (dir / "out.wav").string();
    ASSERT_TRUE(util::saveAudioBuffer(inPath, src, 48000));

    writeScript(
        "local h = yawn.load_audio_file('" + inPath + "')\n"
        "assert(h > 0, 'handle')\n"
        "assert(yawn.save_audio_buffer(h, '" + outPath + "', "
        "{ format = 'wav', depth = 'f32' }), 'save')\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));

    auto reloaded = util::loadAudioFile(outPath);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->numFrames(), 4800);
    EXPECT_NEAR(bufferRms(*reloaded), bufferRms(src), 1e-5f);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_F(LiveCodeManagerTest, LoadSampleIntoSampler) {
    // Real Sampler instrument on track 0; a scripted load lands in it.
    m_engine->setInstrument(0, std::make_unique<instruments::Sampler>());
    audio::AudioBuffer buf(1, 12000);
    for (int f = 0; f < 12000; ++f) buf.sample(0, f) = std::sin(float(f) * 0.001f);
    const auto dir = std::filesystem::temp_directory_path() / "yawn_load_sample";
    std::filesystem::create_directories(dir);
    const std::string wavPath = (dir / "s.wav").string();
    ASSERT_TRUE(util::saveAudioBuffer(wavPath, buf, 48000));

    writeScript(
        "local h = yawn.load_audio_file('" + wavPath + "')\n"
        "assert(h > 0, 'handle')\n"
        "yawn.load_sample(0, h, 'sampler')\n");
    auto* pm = m_mgr.prerenderManager();
    ASSERT_NE(pm, nullptr);
    pm->deliverSampler = [&eng = m_engine](std::shared_ptr<audio::AudioBuffer> buf,
                                           const std::string&, int) {
        if (!buf) return false;
        auto* s = dynamic_cast<instruments::Sampler*>(eng->instrument(0));
        if (!s) return false;
        std::vector<float> il;
        il.reserve(buf->totalSamples());
        const int frames = buf->numFrames(), chans = buf->numChannels();
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < chans; ++c)
                il.push_back(buf->sample(c, f));
        s->loadSample(il.data(), frames, chans);
        return true;
    };
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    auto* s = dynamic_cast<instruments::Sampler*>(m_engine->instrument(0));
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->hasSample());
    EXPECT_EQ(s->sampleFrames(), 12000);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_F(LiveCodeManagerTest, ScanAndAutorun) {
    // Phase 5: a temp "project" with livecode/ scripts; scan finds them;
    // autorun defs fire on open.
    std::filesystem::create_directories(m_tmp / "livecode");
    {
        std::ofstream f(m_tmp / "livecode" / "alpha.lua", std::ios::binary);
        f << "yawn.log('autorun_alpha')\n";
    }
    {
        std::ofstream f(m_tmp / "livecode" / "beta.lua", std::ios::binary);
        f << "yawn.log('autorun_beta')\n";
    }

    auto defs = m_mgr.scanProjectScripts(m_tmp);
    ASSERT_EQ(defs.size(), 2u);
    EXPECT_EQ(defs[0].name, "alpha");
    EXPECT_EQ(defs[1].name, "beta");

    // Record autorun on alpha, re-scan (preserves), open project.
    std::vector<yawn::Project::LiveCodeScriptDef> withAuto = defs;
    withAuto[0].autorun = true;
    m_project.setLiveCodeScripts(withAuto);

    writeScript("yawn.log('autorun_alpha')\n");
    m_mgr.onProjectOpened(m_tmp, m_project);
    EXPECT_EQ(m_mgr.pendingAutorunForTest(), 1u);
    m_mgr.update();   // runs alpha
    EXPECT_EQ(m_mgr.pendingAutorunForTest(), 0u);
    bool saw = false;
    for (const auto& line : m_mgr.console())
        if (line.text.find("autorun_alpha") != std::string::npos) saw = true;
    EXPECT_TRUE(saw);
}

// ─────────────────────────────────────────────────────────────────────────
// Phase 6: code lens — generateSongSource round-trip confluence
// ─────────────────────────────────────────────────────────────────────────

TEST_F(LiveCodeManagerTest, CodeLensRoundTripConfluence) {
    // 1. Materialize a known state via applySongModel.
    SongModel song;
    song.bpm = 126.0;
    auto& st = song.tracks.emplace_back();
    st.hasName = true; st.name = "Lead \"vox\"\\n";
    st.type = "midi";
    st.volume = 1.1; st.pan = -0.25;
    st.hasInstrument = true;
    st.instrument.id = "subsynth";
    st.instrument.params.push_back({false, 0, "Filter Cutoff", 0.65});
    st.instrument.params.push_back({true, 22, "", 0.9});      // by index: Volume
    st.hasFx = true;
    st.fx.push_back({"reverb", {}});
    st.hasClips = true;
    auto& c = st.clips[2];
    c.beats = 2.0;
    c.notes = {{0.0, 0.5, 60, 1.0, 0}, {1.0, 0.25, 67, 0.8, 1}};

    SongApplyContext ctx;
    ctx.project = &m_project;
    ctx.engine = m_engine.get();
    std::set<uint64_t> owned;
    (void)applySongModel(song, owned, ctx);
    m_engine->pumpInputForTest(nullptr, 256);   // consume bpm command

    // 2. Generate the code lens from live state.
    const std::string source = livecode::generateSongSource(m_project, *m_engine);
    ASSERT_NE(source.find("song = {"), std::string::npos);
    ASSERT_NE(source.find("Lead \\\"vox\\\""), std::string::npos) << source;

    // 3. Parse it back in a fresh Lua state.
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    ASSERT_EQ(luaL_dostring(L, source.c_str()), LUA_OK) << source;
    lua_getglobal(L, "song");
    SongModel reparsed;
    std::string perr;
    ASSERT_TRUE(livecode::parseSongModel(L, lua_gettop(L), reparsed, perr)) << perr;
    lua_close(L);

    // 4. Apply the re-parsed model → must be a NO-OP (confluence).
    const SongApplyReport rep = applySongModel(reparsed, owned, ctx);
    if (rep.changed || !rep.warnings.empty()) {
        std::string dump = "round-trip diverged:\n" + source + "\n--- ops ---\n";
        for (const auto& o : rep.ops) dump += o + "\n";
        for (const auto& w : rep.warnings) dump += "WARN: " + w + "\n";
        FAIL() << dump;
    }
    EXPECT_EQ(m_project.numTracks(), 3);   // still no duplicate

    // 5. Values survived: name escaping, vel, pan.
    auto* slot = m_project.getSlot(2, 1);
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->midiClip, nullptr);
    EXPECT_EQ(slot->midiClip->note(1).channel, 1);
}

// ─────────────────────────────────────────────────────────────────────────
// Editor kernel (phase 8 — framework-free; src/ui/panels header)
// ─────────────────────────────────────────────────────────────────────────

#include "ui/panels/LiveCodeEditorKernel.h"
using K = yawn::ui::LiveCodeEditorKernel;

TEST(LiveCodeEditorKernelTest, SetGetRoundTrip) {
    K k;
    k.setText("-- hi\nsong = { bpm = 120 }\n");
    EXPECT_EQ(k.lines().size(), 2u);
    EXPECT_EQ(k.lines()[0], "-- hi");
    EXPECT_EQ(k.lines()[1], "song = { bpm = 120 }");
    EXPECT_TRUE(!k.modified());
}

TEST(LiveCodeEditorKernelTest, TypingAndNavigation) {
    K k;
    k.setText("ab");
    k.moveEnd();                            // after 'b'
    k.insertText("cd");
    EXPECT_EQ(k.lines()[0], "abcd");
    EXPECT_EQ(k.caretCol(), 4);
    k.moveLeft();
    k.insertText("X");                      // abcdX → abcd       hmm:
    EXPECT_EQ(k.lines()[0], "abcXd");
    k.backspace();
    EXPECT_EQ(k.lines()[0], "abcd");
    // Split / join.
    k.moveHome();
    k.splitLine();
    EXPECT_EQ(k.lines().size(), 2u);
    EXPECT_EQ(k.lines()[0], "");
    EXPECT_EQ(k.lines()[1], "abcd");
    k.backspace();                          // joins back
    EXPECT_EQ(k.lines().size(), 1u);
    EXPECT_EQ(k.lines()[0], "abcd");
    EXPECT_TRUE(k.modified());
}

TEST(LiveCodeEditorKernelTest, DeleteCharAcrossLineAndEnd) {
    K k;
    k.setText("one\ntwo");
    k.moveEnd();
    k.deleteChar();                         // at EOL → joins next line
    EXPECT_EQ(k.lines().size(), 1u);
    EXPECT_EQ(k.lines()[0], "onetwo");
}

TEST(LiveCodeEditorKernelTest, ColumnGoalKeptAcrossUpDown) {
    K k;
    k.setText("abcd\nxy\nlonger");
    k.moveEnd();                            // line 0 col 4
    k.moveDown();                           // line 1 (len 2) → col clamps to 2
    EXPECT_EQ(k.caretLine(), 1);
    EXPECT_EQ(k.caretCol(), 2);
    k.moveDown();                           // line 2 → col back to goal 4
    EXPECT_EQ(k.caretLine(), 2);
    EXPECT_EQ(k.caretCol(), 4);
}

TEST(LiveCodeEditorKernelTest, CompletionFiltersAndAccepts) {
    K k;
    k.setText("yan");
    k.moveEnd();
    k.updateCompletion();
    EXPECT_FALSE(k.completionOpen());   // "yan" prefixes nothing
    k.setText("yaw");                   // use a real prefix
    k.moveEnd();
    k.updateCompletion();
    EXPECT_TRUE(k.completionOpen());
    const int n = static_cast<int>(k.completionItems().size());
    EXPECT_GT(n, 5);                        // all yawn.* entries
    EXPECT_EQ(k.completionItems()[0], "yawn.log");
    k.selectNext();
    EXPECT_EQ(k.completionSelected(), 1);
    // Accept inserts only the remainder (items[1] = yawn.toast).
    EXPECT_TRUE(k.acceptCompletion());
    EXPECT_EQ(k.completionItems().size(), 0u);
    EXPECT_EQ(k.lines()[0], "yawn.toast");
}

TEST(LiveCodeEditorKernelTest, CompletionDottedPrefix) {
    K k;
    k.setText("improv.l");
    k.moveEnd();
    k.updateCompletion();
    ASSERT_TRUE(k.completionOpen());
    EXPECT_EQ(k.completionItems()[0], "improv.lookahead");
    EXPECT_EQ(k.completionItems()[1], "improv.late_policy");
    EXPECT_TRUE(k.acceptCompletion());
    EXPECT_EQ(k.lines()[0], "improv.lookahead");
}

TEST(LiveCodeEditorKernelTest, CompletionTooShortOrTooBroadCloses) {
    K k;
    k.setText("a");
    k.moveEnd();
    k.updateCompletion();
    EXPECT_FALSE(k.completionOpen());       // < 2 chars
    k.setText("");                          // empty prefix over full table
    k.updateCompletion();
    EXPECT_FALSE(k.completionOpen());
}

TEST(LiveCodeEditorKernelTest, UTF8BackspaceStepsContinuation) {
    K k;
    k.setText("\xCE\xBB");                  // λ (2 bytes)
    k.moveEnd();
    EXPECT_EQ(k.caretCol(), 2);
    k.backspace();                          // removes the whole glyph
    EXPECT_EQ(k.lines()[0], "");
}

// ─────────────────────────────────────────────────────────────────────────
// Template-preserving round-trip (phase 9 — patchSongSource)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(LiveCodeManagerTest, PatchGeneratedSourceIsNoOp) {
    const std::string generated = livecode::generateSongSource(m_project, *m_engine);
    const auto rep = livecode::patchSongSource(generated, m_project, *m_engine);
    EXPECT_FALSE(rep.changed) << rep.patched;
    EXPECT_TRUE(rep.warnings.empty());
    if (rep.changed)
        std::cout << rep.patched << std::endl;
}

TEST_F(LiveCodeManagerTest, PatchScalarsKeepComments) {
    // Hand-shaped template with comments and unusual layout.
    const std::string script = R"LUA(
-- My live set
song = {
    bpm = 120,          -- party tempo
    scenes = 2,
    tracks = {
        { uid = 1, name = "Bass", type = "midi",
          volume = 0.8, },
    },
}
-- after
)LUA";
    // UI-side mutations: bpm 124, loud track.
    (void)m_engine->sendCommand(audio::TransportSetBPMMsg{124.0});
    m_engine->pumpInputForTest(nullptr, 256);   // queued → consumed headlessly
    EXPECT_NEAR(m_engine->transport().bpm(), 124.0, 1e-6);
    m_project.track(0).volume = 1.0f;
    const auto rep = livecode::patchSongSource(script, m_project, *m_engine);
    ASSERT_TRUE(rep.changed);
    EXPECT_NE(rep.patched.find("-- party tempo"), std::string::npos);
    EXPECT_NE(rep.patched.find("-- My live set"), std::string::npos);
    EXPECT_NE(rep.patched.find("-- after"), std::string::npos);
    EXPECT_NE(rep.patched.find("bpm = 124"), std::string::npos);
    // Track block rewritten with model state.
    EXPECT_NE(rep.patched.find("volume = 1"), std::string::npos);
    // Nothing else disturbed.
    EXPECT_NE(rep.patched.find("name = \"Bass\""), std::string::npos);
}

TEST_F(LiveCodeManagerTest, PatchInsertsAndRemovesTracks) {
    const std::string script = R"LUA(
song = {
    bpm = 120,
    tracks = {
        { uid = 1, name = "One", type = "midi", volume = 1, },
        { uid = 2, name = "Two", type = "midi", volume = 1, },
        { uid = 3, name = "Ghost", type = "midi", volume = 1, },
    },
}
)LUA";
    // Project has tracks uid 1..2 (init(2,2)) — uid 3 must be dropped.
    const auto rep = livecode::patchSongSource(script, m_project, *m_engine);
    ASSERT_TRUE(rep.changed);
    EXPECT_EQ(rep.patched.find("Ghost"), std::string::npos);
    EXPECT_NE(rep.patched.find("One"), std::string::npos);
    EXPECT_NE(rep.patched.find("Two"), std::string::npos);
    // Removed exactly one block, braces still balanced.
    int depth = 0, minDepth = 0;
    for (char c : rep.patched) {
        if (c == '{') ++depth;
        else if (c == '}') { --depth; minDepth = std::min(minDepth, depth); }
    }
    EXPECT_EQ(depth, 0);
    EXPECT_GE(minDepth, 0);

    // Inverse: script with only uid 1 → uid 2 block gets inserted.
    const std::string shortScript = R"LUA(
song = {
    bpm = 120,
    tracks = {
        { uid = 1, name = "One", type = "midi", volume = 1, },
    },
}
)LUA";
    const auto rep2 = livecode::patchSongSource(shortScript, m_project, *m_engine);
    ASSERT_TRUE(rep2.changed);
    EXPECT_NE(rep2.patched.find("uid = 1"), std::string::npos);
    EXPECT_NE(rep2.patched.find("uid = 2"), std::string::npos);
}

TEST_F(LiveCodeManagerTest, PatchClipLinesAndIdentity) {
    // Give track 0 a clip.
    auto clip = std::make_unique<midi::MidiClip>(1.0);
    clip->addNote({0.0, 1.0, 60, 0, 40960});
    auto* slot = m_project.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    slot->midiClip = std::move(clip);

    const std::string script = R"LUA(
song = {
    bpm = 120,
    tracks = {
        { uid = 1, name = "One", type = "midi", volume = 1,
          clips = {
            [1] = { beats = 4, notes = { {0, 2, 36, 1} } },   -- drums
          },
        },
    },
}
)LUA";
    // Divergent clip line → regenerated to match the project (incl. comment).
    const auto rep = livecode::patchSongSource(script, m_project, *m_engine);
    ASSERT_TRUE(rep.changed);
    EXPECT_NE(rep.patched.find("-- drums"), std::string::npos);
    EXPECT_NE(rep.patched.find("beats = 1"), std::string::npos);
    EXPECT_NE(rep.patched.find("{0, 1, 60" /* 0, dur 1, pitch 60*/), std::string::npos);
    EXPECT_EQ(rep.patched.find("{0, 2, 36"), std::string::npos);

    // Re-patching the patched text converges (idempotence).
    const auto rep2 = livecode::patchSongSource(rep.patched, m_project, *m_engine);
    EXPECT_FALSE(rep2.changed);
}

TEST_F(LiveCodeManagerTest, PatchMultiLineClipGroupsLeftAlone) {
    const std::string script = R"LUA(
song = {
    bpm = 120,
    tracks = {
        { uid = 1, name = "One", type = "midi", volume = 1,
          clips = {
            [1] = {
                beats = 4,
                notes = {
                    {0, 1, 60, 1},
                    {2, 1, 67, 1},
                },
            },
          },
        },
        { uid = 2, name = "Two", type = "midi", volume = 1, },
    },
}
)LUA";
    const auto rep = livecode::patchSongSource(script, m_project, *m_engine);
    EXPECT_FALSE(rep.changed);
    EXPECT_TRUE(rep.warnings.empty())
        << (rep.warnings.empty() ? "" : rep.warnings.front());
    // The clip group (and its notes) survives byte-for-byte.
    EXPECT_NE(rep.patched.find("{2, 1, 67, 1}"), std::string::npos);
    EXPECT_NE(rep.patched.find("beats = 4"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────
// Ghost-note ledger (improv visualization — UI thread only)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(LiveCodeManagerTest, GhostNoteLedgerPendingToFired) {
    writeScript(
        "yawn.note(0, 60, 100, 0, 0, 10)   -- beat 10: pending far\n"
        "yawn.note(1, 62, 110, 0, 0, 4)    -- beat 4: fires when crossed\n"
        "yawn.note(2, 64, 90, 0, 0, 0);    -- immediate → fired now\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));

    const double t0 = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // Snapshot at beat 3: beats 4/10 pending; the immediate note fired.
    auto g = m_mgr.ghostNotes(3.0, t0);
    int pending = 0, fired = 0;
    int firedTrack = -1;
    for (const auto& n : g) {
        if (n.fired) { ++fired; firedTrack = n.track; }
        else         ++pending;
    }
    // beat 4 > 3.0 → pending; beat 10 pending; the immediate note's
    // entry (beat 0) has crossed → fired.
    EXPECT_EQ(pending, 2);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(firedTrack, 2);

    // Cross beat 4: it moves to fired; the beat-10 one stays pending.
    g = m_mgr.ghostNotes(5.0, t0 + 2.0);
    pending = 0;
    for (const auto& n : g)
        if (!n.fired) ++pending;
    EXPECT_EQ(pending, 1);

    // Fired ring prunes by age (>5 s).
    g = m_mgr.ghostNotes(5.0, t0 + 8.0);
    for (const auto& n : g)
        EXPECT_FALSE(n.fired);
}

// ─────────────────────────────────────────────────────────────────────────
// Freeze take → sidecar track (improv capture)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(LiveCodeManagerTest, FreezeTakeBuildsNormalizedClip) {
    std::unique_ptr<midi::MidiClip> taken;
    std::string takenName;
    int takenCount = -1;
    m_mgr.setFreezeTakeHook([&](std::unique_ptr<midi::MidiClip> clip,
                                const std::string& name, int count) {
        taken = std::move(clip);
        takenName = name;
        takenCount = count;
        return true;
    });

    // Script plays: an immediate note (transport stopped → beat 0),
    // a scheduled note at beat 5.5 and one at beat 9 + dur 2.
    writeScript(
        "yawn.note(0, 60, 100, 0.5, 0, 0)\n"
        "yawn.note(1, 62, 80, 1.0, 1, 5.5)\n"
        "yawn.note(2, 64, 90, 2.0, 0, 9)\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    ASSERT_TRUE(m_mgr.freezeTake());

    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(takenName, "Take 1");
    EXPECT_EQ(takenCount, 3);
    // Bar-round length: span = 9 + 2 - 0 = 11 → 12 beats (3 bars).
    EXPECT_EQ(taken->lengthBeats(), 12.0);
    ASSERT_EQ(taken->noteCount(), 3);
    // Origin-anchored, sorted by insertion order preserved; check each.
    EXPECT_EQ(taken->note(0).startBeat, 0.0);
    EXPECT_EQ(taken->note(0).pitch, 60);
    EXPECT_NEAR(taken->note(1).startBeat, 5.5, 1e-9);
    EXPECT_EQ(taken->note(1).pitch, 62);
    EXPECT_NEAR(taken->note(2).startBeat, 9.0, 1e-9);
    EXPECT_EQ(taken->note(2).duration, 2.0);
    // Velocity 7→16-bit conversion.
    EXPECT_EQ(taken->note(1).velocity, midi::Convert::vel7to16(80));

    // Capture cleared: a second freeze reports nothing.
    EXPECT_FALSE(m_mgr.freezeTake());
    // Counter advances.
    m_mgr.captureLiveNote(0, 0.0, 0.25, 60, 100, 0);
    ASSERT_TRUE(m_mgr.freezeTake());
    EXPECT_EQ(takenName, "Take 2");
}

TEST_F(LiveCodeManagerTest, FreezeTakeEmptyAndNoHook) {
    EXPECT_FALSE(m_mgr.freezeTake());          // nothing captured
    writeScript("yawn.note(0, 60, 100, 0.25, 0, 2)\n");
    ASSERT_TRUE(m_mgr.runScript(m_mgr.defaultScriptPath()));
    EXPECT_FALSE(m_mgr.freezeTake());          // no hook wired
}
