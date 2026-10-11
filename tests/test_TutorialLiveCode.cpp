// test_TutorialLiveCode.cpp — the runnable Lua blocks in
// docs/tutorial-live-coding.md are executed through the REAL live-coding
// stack, the same treatment the bundled examples get
// (tests/test_LiveCodeExamples.cpp). Each ```lua run block:
//   1. runs as a fresh generation on a fresh default project (the fixture
//      wires delivery hooks exactly the way App::init does — notably with
//      NO explicit PrerenderManager::init call, so the app's prerender
//      wiring is exercised, not a test-side init),
//   2. is pumped (pumpInputForTest + manager update) so scheduled improv
//      callbacks fire, launches quantize, prerender jobs complete and
//      delivery hooks run, and
//   3. re-runs once — the Ctrl+L edit loop — and pumps again.
// A block passes when, after both runs, it
//   a) returned true from runScriptSource,
//   b) printed no console severity-2 (error) lines,
//   c) printed no severity-1 (warning) lines either — a warning IS a
//      tutorial bug: an unresolved param name or a failed delivery prints
//      one, and
//   d) satisfied its in-block `-- @assert ...` directives:
//        tracks:N | playing | bpm:X | inst:track:id
//        midinotes:track:scene:count | audioclip:track:scene | delivered:N
//      ("delivered" counts the prerender delivery reports since the run's
//      console snapshot — i.e. the count for the latest run of the block).

#include <gtest/gtest.h>
#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace yawn;
using namespace yawn::livecode;

namespace {

// Locate docs/tutorial-live-coding.md relative to this test file.
std::filesystem::path tutorialDocPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "docs" / "tutorial-live-coding.md";
}

struct Snippet {
    std::string title;
    std::string code;
    int line = 0;   // line of the block in the markdown (for messages)
};

std::vector<Snippet> parseTutorialDoc(const std::filesystem::path& doc,
                                      std::string& err) {
    std::ifstream f(doc);
    if (!f) {
        err = "cannot open " + doc.generic_string();
        return {};
    }
    std::vector<Snippet> out;
    std::string lineStr;
    int lineNo = 0;
    bool inBlock = false;
    Snippet cur;

    while (std::getline(f, lineStr)) {
        ++lineNo;
        if (!inBlock) {
            if (lineStr.rfind("```lua run", 0) == 0) {
                inBlock = true;
                cur = Snippet{};
                cur.line = lineNo + 1;
            }
            continue;
        }
        if (inBlock && lineStr == "```") {
            inBlock = false;
            cur.title = cur.title.empty()
                ? ("unnamed block at doc line " + std::to_string(cur.line))
                : cur.title;
            out.push_back(std::move(cur));
            continue;
        }
        if (cur.title.empty() && lineStr.rfind("-- ", 0) == 0) {
            std::string t = lineStr.substr(3);
            while (!t.empty() && t.back() == ' ') t.pop_back();
            cur.title = t;
        }
        cur.code += lineStr;
        cur.code += "\n";
    }
    if (inBlock) err = "unterminated ```lua run block (file truncated?)";
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// Extract `-- @assert ...` directives (they double as Lua comments, so they
// stay in the executed code as-is).
std::vector<std::string> assertDirectives(const std::string& code) {
    std::vector<std::string> out;
    std::istringstream ss(code);
    std::string lineStr;
    while (std::getline(ss, lineStr)) {
        std::size_t p = lineStr.find_first_not_of(" \t");
        std::string trimmed =
            (p == std::string::npos) ? "" : lineStr.substr(p);
        if (trimmed.rfind("-- @assert ", 0) == 0)
            out.push_back(trimmed.substr(std::string("-- @assert ").size()));
    }
    return out;
}

int countDeliveredLines(const std::deque<LiveCodeManager::ConsoleLine>& lines) {
    int n = 0;
    for (const auto& l : lines)
        if (l.text.find("prerender: delivered") != std::string::npos) ++n;
    return n;
}

int consoleProblems(const std::deque<LiveCodeManager::ConsoleLine>& lines,
                    int severity, std::string& joined) {
    joined.clear();
    for (const auto& l : lines) {
        if (l.severity >= severity) {
            if (!joined.empty()) joined += " | ";
            joined += l.text;
        }
    }
    return joined.empty() ? 0 : 1;
}

bool deliverBufOk(std::shared_ptr<audio::AudioBuffer> buf) {
    return buf != nullptr && buf->numFrames() > 0;
}

} // namespace

namespace {
class TutorialStack {
public:
    TutorialStack() {
        m_dir = std::filesystem::temp_directory_path() /
                ("yawn_tutorial_" +
                 std::to_string(reinterpret_cast<std::uintptr_t>(this)) +
                 "_" + std::to_string(++s_seq));
        std::filesystem::create_directories(m_dir);
        m_engine = std::make_unique<audio::AudioEngine>();
        m_project.init(2, 2);
        m_mgr.init(m_engine.get(), &m_project);
        m_mgr.setProjectPathProvider([this]() { return m_dir; });

        // yawn.set_clip / render kind="clip" — mirrors the App hook
        // (Project::setClip is graveyard-protected in production).
        m_mgr.setClipLiveHook(
            [this](int track, int scene,
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
        // yawn.set_visual — the app resolves shader paths and installs a
        // visual clip; the headless twin accepts (a shader never compiles
        // here — no GL context in this harness).
        m_mgr.setVisualLiveHook(
            [](int, int, const std::string&, const std::string&) {
                return true;
            });
        // Prerender delivery — App::init binds every kind to the real
        // publish/load paths; the tutorial stack accepts + lets the report
        // line through, so the "delivered" console lines are exercised.
        if (auto* pm = m_mgr.prerenderManager()) {
            pm->deliverClip = [this](const std::shared_ptr<audio::AudioBuffer>& buf,
                                     int track, int scene, const std::string& name) {
                if (!buf || track < 0 || track >= m_project.numTracks() ||
                    scene < 0 || scene >= m_project.numScenes()) return false;
                auto clip = std::make_unique<audio::Clip>();
                clip->name = name;
                clip->buffer = buf;
                m_project.setClip(track, scene, std::move(clip));
                return true;
            };
            pm->deliverSampler = deliverOk;
            pm->deliverGranular = deliverOk;
            pm->deliverDrumSlop = deliverOk;
            pm->deliverVocoder = deliverOk;
            pm->deliverDrumRackPad =
                [](std::shared_ptr<audio::AudioBuffer> buf, const std::string&,
                   int, int) { return deliverBufOk(buf); };
            pm->deliverLibrary =
                [](std::shared_ptr<audio::AudioBuffer> buf,
                   const std::string&) { return deliverBufOk(buf); };
        }
    }

    ~TutorialStack() {
        m_mgr.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }

    TutorialStack(const TutorialStack&) = delete;
    TutorialStack& operator=(const TutorialStack&) = delete;

    // Run a block through its full verified lifetime.
    void loadBlock(const std::string& code, const std::string& title,
                   int lineNo) {
        SCOPED_TRACE("block '" + title + "' (doc line " +
                     std::to_string(lineNo) + ")");

        const auto problemsAt = [&](const std::string& phase) {
            std::string joined;
            if (consoleProblems(m_mgr.console(), 2, joined)) {
                ADD_FAILURE() << phase << " printed errors: " << joined;
            } else if (consoleProblems(m_mgr.console(), 1, joined)) {
                ADD_FAILURE() << phase << " printed warnings: " << joined;
            }
        };

        // Phase 1 — the initial run.
        m_deliveredBaseline = countDeliveredLines(m_mgr.console());
        ASSERT_TRUE(m_mgr.runScriptSource(code)) << "run failed";
        pump(kPumpBlocks);
        drainPrerenders();
        problemsAt("the run");
        for (const auto& d : assertDirectives(code))
            checkDirective(d, title, lineNo);

        // Phase 2 — the Ctrl+L edit loop on the same live state.
        m_deliveredBaseline = countDeliveredLines(m_mgr.console());
        ASSERT_TRUE(m_mgr.runScriptSource(code)) << "re-run failed";
        pump(kPumpBlocks);
        drainPrerenders();
        problemsAt("the re-run");
    }

    void pump(int blocks) {
        for (int i = 0; i < blocks; ++i) {
            m_engine->pumpInputForTest(nullptr, 512);
            m_mgr.update();
        }
    }

    void drainPrerenders() {
        for (int spin = 0; spin < 2000 && m_mgr.prerenderActive() > 0; ++spin) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            m_mgr.update();
        }
    }

    void checkDirective(const std::string& directive,
                        const std::string& title, int lineNo) {
        SCOPED_TRACE("@assert " + directive);
        const auto parts = split(directive, ':');
        if (parts.empty()) return;
        if (parts[0] == "tracks") {
            ASSERT_GE(parts.size(), 2);
            EXPECT_EQ(m_project.numTracks(), std::stoi(parts[1]));
        } else if (parts[0] == "playing") {
            EXPECT_TRUE(m_engine->transport().isPlaying());
        } else if (parts[0] == "bpm") {
            ASSERT_GE(parts.size(), 2);
            EXPECT_NEAR(m_engine->transport().bpm(), std::stod(parts[1]), 1e-6);
        } else if (parts[0] == "inst") {
            ASSERT_GE(parts.size(), 3);
            const int track = std::stoi(parts[1]);
            auto* inst = m_engine->instrument(track);
            ASSERT_NE(inst, nullptr) << "track " << track;
            EXPECT_EQ(inst->id(), parts[2]);
        } else if (parts[0] == "midinotes") {
            ASSERT_GE(parts.size(), 4);
            const int track = std::stoi(parts[1]);
            const int scene = std::stoi(parts[2]);
            const auto* slot = m_project.getSlot(track, scene - 1);
            ASSERT_NE(slot, nullptr);
            ASSERT_NE(slot->midiClip, nullptr)
                << "no MIDI clip @ track " << track << " scene " << scene;
            EXPECT_EQ(slot->midiClip->noteCount(), std::stoi(parts[3]));
        } else if (parts[0] == "audioclip") {
            ASSERT_GE(parts.size(), 3);
            const int track = std::stoi(parts[1]);
            const int scene = std::stoi(parts[2]);
            const auto* slot = m_project.getSlot(track, scene - 1);
            ASSERT_NE(slot, nullptr);
            ASSERT_NE(slot->audioClip, nullptr)
                << "no audio clip @ track " << track << " scene " << scene;
        } else if (parts[0] == "delivered") {
            ASSERT_GE(parts.size(), 2);
            const int seen = countDeliveredLines(m_mgr.console()) -
                             m_deliveredBaseline;
            EXPECT_EQ(seen, std::stoi(parts[1]))
                << "block '" << title << "' (doc line " << lineNo << "): "
                << "expected " << parts[1] << " 'prerender: delivered' "
                                           "console lines after this run";
        } else {
            FAIL() << "unknown @assert directive '" << directive
                   << "' (test harness and tutorial are out of sync)";
        }
    }

    audio::AudioEngine* engine() { return m_engine.get(); }
    Project* project() { return &m_project; }
    LiveCodeManager* mgr() { return &m_mgr; }

private:
    static int s_seq;
    static constexpr int kPumpBlocks = 800;

    std::filesystem::path m_dir;
    std::unique_ptr<audio::AudioEngine> m_engine;
    Project m_project;
    LiveCodeManager m_mgr;

    int m_deliveredBaseline = 0;

    std::function<bool(std::shared_ptr<audio::AudioBuffer>, const std::string&,
                       int)>
        deliverOk = [](std::shared_ptr<audio::AudioBuffer> buf,
                       const std::string&, int) {
            return buf != nullptr && buf->numFrames() > 0;
        };
};

int TutorialStack::s_seq = 0;

} // namespace

// The contract stated in docs/tutorial-live-coding.md's preface: "every Lua
// snippet marked run in this file is executed headlessly by the test
// suite". Per-block state, per-block verdicts.
TEST(TutorialLiveCode, EveryRunnableBlockRunsClean) {
    std::string parseErr;
    const auto doc = tutorialDocPath();
    const auto snippets = parseTutorialDoc(doc, parseErr);
    ASSERT_TRUE(parseErr.empty()) << parseErr;
    ASSERT_FALSE(snippets.empty())
        << "no ```lua run blocks found — tutorial doc drifted?";

    int ran = 0;
    for (const auto& s : snippets) {
        SCOPED_TRACE("block '" + s.title + "' (doc line " +
                     std::to_string(s.line) + ")");
        TutorialStack stack;
        stack.loadBlock(s.code, s.title, s.line);
        ++ran;
    }
    EXPECT_GE(ran, 7);   // the tutorial currently carries 7 runnable blocks
}

// The tutorial's Lesson 3 teaches the *field-level* fallback idiom because
// yawn.state is pre-seeded as an (empty, truthy) table — pin that semantic
// so nobody "simplifies" the engine seed back into trap territory.
TEST(TutorialLiveCode, StateTableIsPreSeededTruthy) {
    TutorialStack stack;
    ASSERT_TRUE(stack.mgr()->runScriptSource("yawn.state = yawn.state or {}"));
    lua_State* L = stack.mgr()->luaEngine()->state();
    ASSERT_NE(L, nullptr);
    lua_getglobal(L, "yawn");
    lua_getfield(L, -1, "state");
    ASSERT_TRUE(lua_istable(L, -1));
    EXPECT_EQ(lua_rawlen(L, -1), 0);   // empty = truthy = `or {}` never fired
    lua_getfield(L, -1, "nothing_was_ever_assigned");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 3);
}

// App-shaped prerender wiring: yawn.render jobs must complete and deliver
// with the SAME wiring the app ships (no test-only PrerenderManager::init).
// This pins the manager-side worker start; the runnable-block loop above
// exercises the same path from the lesson 6/7 snippets.
TEST(TutorialLiveCode, RenderDeliversUnderAppWiring) {
    TutorialStack stack;
    const char* script =
        "local kick = yawn.render{\n"
        "    device = 'subsynth', params = {},\n"
        "    notes = { {0.0, 0.3, 36, 1.0} }, beats = 0.5, tail = 0.5,\n"
        "    target = { kind = 'clip', track = 0, scene = 1,\n"
        "               name = 'wire check' } }\n"
        "assert(kick > 0, 'render enqueue failed')\n";
    ASSERT_TRUE(stack.mgr()->runScriptSource(script));
    stack.drainPrerenders();
    const int seen =
        countDeliveredLines(stack.mgr()->console());
    EXPECT_GE(seen, 1) << "yawn.render never delivered — prerender workers "
                          "are not running under app wiring";
    const auto* slot = stack.project()->getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->audioClip, nullptr);
    EXPECT_GT(slot->audioClip->buffer->numFrames(), 0);
}
