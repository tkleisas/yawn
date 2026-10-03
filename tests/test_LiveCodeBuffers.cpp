// test_LiveCodeBuffers.cpp — programmatic sample creation + math for the
// live-coding layer (src/livecode/LiveCodeBuffers): buffer synthesis via
// Lua callbacks/copy-in, in-place vector ops, slice/concat/repeat/mixdown,
// radix-2 FFT/IFFT round-trips, and polyBLEP-2 continuity. Each case
// drives the real Lua API through the manager harness.

#include <gtest/gtest.h>
#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"

#include <cmath>

using namespace yawn;
using namespace yawn::livecode;

class LiveCodeBufTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<audio::AudioEngine>();
        m_project.init(2, 2);
        m_mgr.init(m_engine.get(), &m_project);
    }
    void TearDown() override { m_mgr.shutdown(); }

    // Run source through the manager's evaluation path.
    bool run(const std::string& src) {
        return m_mgr.runScriptSource(src);
    }

    std::unique_ptr<audio::AudioEngine> m_engine;
    int m_deliveredFrames = -1;
    Project m_project;
    LiveCodeManager m_mgr;
};

// Lua state access for result inspection (scripts leave yawn.* globals).
static lua_State* L(LiveCodeManager& mgr) { return mgr.luaEngine()->state(); }

static double numGlobal(LiveCodeManager& mgr, const char* name) {
    lua_getglobal(L(mgr), "yawn");
    lua_getfield(L(mgr), -1, name);
    double v = -777.0;
    if (lua_isnumber(L(mgr), -1)) v = lua_tonumber(L(mgr), -1);
    else if (lua_isboolean(L(mgr), -1)) v = lua_toboolean(L(mgr), -1) ? 1 : 0;
    lua_pop(L(mgr), 2);
    return v;
}

// ── new_buffer: fill callback ────────────────────────────────────────────

static std::string debugState(LiveCodeManager& mgr) {
    std::string out;
    for (const auto& l : mgr.console())
        out += "[" + std::to_string(l.severity) + "] " + l.text + "\n";
    lua_getglobal(L(mgr), "yawn");
    lua_getfield(L(mgr), -1, "h1");
    out += "yawn.h1 type: " + std::string(lua_typename(L(mgr), lua_type(L(mgr), -1)));
    lua_pop(L(mgr), 1);
    lua_getfield(L(mgr), -1, "dbg_d");
    out += " dbg_d=" +
           std::string(lua_typename(L(mgr), lua_type(L(mgr), -1)));
    lua_pop(L(mgr), 1);
    for (const char* g : { "s1", "s2", "s3" }) {
        lua_getfield(L(mgr), -1, g);
        out += " " + std::string(g) + "=" +
               std::string(lua_typename(L(mgr), lua_type(L(mgr), -1))) +
               (lua_isstring(L(mgr), -1)
                    ? "/" + std::string(lua_tostring(L(mgr), -1))
                    : "");
        lua_pop(L(mgr), 1);
    }
    lua_pop(L(mgr), 1);
    return out;
}

TEST_F(LiveCodeBufTest, NewBufferFillCallback) {
    // Tone: sin(2π·(f/sr)·frame) on ch1; ch2 = frame index scaled.
    ASSERT_TRUE(run(
        "local n = 8\n"
        "yawn.h1 = yawn.new_buffer{ frames = n, channels = 2,\n"
        "  fill = function(f, c) return (c == 1) and"
        "         math.sin((f - 1) * 0.25) or (f - 1) * 0.125 end }\n"
        "yawn.s1 = type(yawn.h1)\n"
        "local d = yawn.buffer_data(yawn.h1)\n"
        "yawn.s2 = type(d) .. '/' .. tostring(d and d.frames)\n"
        "yawn.ok1 = d.frames == 8 and d.channels == 2 and"
        " (math.abs(d.data[1][5] - math.sin(0.25 * 4)) < 1e-5)\n"
        "yawn.s3 = 'reached'\n"
        "yawn.ok2 = math.abs(d.data[2][4] - 0.375) < 1e-5\n"));
    if (numGlobal(m_mgr, "ok1") != 1.0 || numGlobal(m_mgr, "ok2") != 1.0)
        GTEST_MESSAGE_(debugState(m_mgr).c_str(),
                       ::testing::TestPartResult::kNonFatalFailure);
    EXPECT_EQ(numGlobal(m_mgr, "ok1"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "ok2"), 1.0);
}

TEST_F(LiveCodeBufTest, NewBufferDataIn) {
    ASSERT_TRUE(run(
        "yawn.h2 = yawn.new_buffer{ frames = 4, channels = 1,\n"
        "  data = { { 0.0, 0.25, 0.5, 0.75 } } }\n"
        "local d = yawn.buffer_data(yawn.h2)\n"
        "yawn.ok = d.data[1][2] == 0.25 and d.frames == 4\n"));
    EXPECT_EQ(numGlobal(m_mgr, "ok"), 1.0);
}

TEST_F(LiveCodeBufTest, BufferInfoAndFree) {
    ASSERT_TRUE(run(
        "yawn.h3 = yawn.new_buffer{ frames = 10, channels = 3 }\n"
        "local i = yawn.buffer_info(yawn.h3)\n"
        "yawn.ok = i.frames == 10 and i.channels == 3\n"
        "yawn.freed = yawn.free_buffer(yawn.h3)\n"
        "yawn.gone = yawn.buffer_info(yawn.h3) == nil\n"));
    EXPECT_EQ(numGlobal(m_mgr, "ok"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "freed"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "gone"), 1.0);
}

// ── In-place vector ops ──────────────────────────────────────────────────

TEST_F(LiveCodeBufTest, GainNormalizeFadeMix) {
    ASSERT_TRUE(run(
        "yawn.g  = yawn.new_buffer{ frames = 4, channels = 1,\n"
        "  data = { { 0.5, 0.5, 0.5, 0.5 } } }\n"
        "yawn.buffer_gain(yawn.g, 2.0)\n"
        "local d = yawn.buffer_data(yawn.g)\n"
        "yawn.gained = d.data[1][1] == 1.0\n"
        "yawn.n  = yawn.new_buffer{ frames = 2, channels = 1,\n"
        "  data = { { 0.8, 0.4 } } }\n"
        "yawn.buffer_normalize(yawn.n)\n"
        "yawn.normalized = yawn.buffer_data(yawn.n).data[1][1] == 1.0\n"
        "yawn.m  = yawn.new_buffer{ frames = 2, channels = 1,\n"
        "  data = { { 1.0, 1.0 } } }\n"
        "yawn.s  = yawn.new_buffer{ frames = 2, channels = 1,\n"
        "  data = { { 1.0, -1.0 } } }\n"
        "yawn.buffer_mix(yawn.m, yawn.s, 0.5)\n"
        "yawn.mixed = yawn.buffer_data(yawn.m).data[1][2] == 0.5\n"));
    EXPECT_EQ(numGlobal(m_mgr, "gained"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "normalized"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "mixed"), 1.0);
}

// ── slice / concat / repeat / mixdown ────────────────────────────────────

TEST_F(LiveCodeBufTest, StructuralOps) {
    ASSERT_TRUE(run(
        "yawn.a = yawn.new_buffer{ frames = 6, channels = 1,\n"
        "  data = { { 0, 1, 2, 3, 4, 5 } } }\n"
        "yawn.b = yawn.buffer_slice(yawn.a, 3, 2)\n"
        "yawn.sliced = yawn.buffer_data(yawn.b).data[1][2] == 3\n"
        "yawn.c = yawn.buffer_concat(yawn.b, yawn.b)\n"
        "yawn.concat = yawn.buffer_info(yawn.c).frames == 4 and"
        " yawn.buffer_data(yawn.c).data[1][4] == 3\n"
        "yawn.r = yawn.buffer_repeat(yawn.b, 3)\n"
        "yawn.repeatd = yawn.buffer_info(yawn.r).frames == 6\n"
        "yawn.st = yawn.new_buffer{ frames = 2, channels = 2,\n"
        "  data = { { 0.5, 0.5 }, { 1.5, -0.5 } } }\n"
        "yawn.mn = yawn.buffer_mixdown(yawn.st)\n"
        "yawn.mixdown = yawn.buffer_data(yawn.mn).channels == 1 and"
        " yawn.buffer_data(yawn.mn).data[1][1] == 1.0\n"));
    EXPECT_EQ(numGlobal(m_mgr, "sliced"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "concat"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "repeatd"), 1.0);
    EXPECT_EQ(numGlobal(m_mgr, "mixdown"), 1.0);
}

// ── FFT / IFFT ───────────────────────────────────────────────────────────

TEST_F(LiveCodeBufTest, FftFindsToneBin) {
    // 8-sample sine at bin 1: FFT peak at k=1 with |X| = N/2, imag < 0.
    ASSERT_TRUE(run(
        "local n = 8\n"
        "local x = {}\n"
        "for i = 1, n do x[i] = math.sin(2 * math.pi * (i - 1) / n) end\n"
        "local spec = yawn.fft(x)\n"
        "local re1 = spec[3]   -- X[1].re = spec[2*1+1]\n"
        "local im1 = spec[4]\n"
        "yawn.mag = math.sqrt(re1 * re1 + im1 * im1)\n"
        "yawn.ok = math.abs(yawn.mag - n / 2) < 1e-9\n"));
    EXPECT_EQ(numGlobal(m_mgr, "ok"), 1.0);
    EXPECT_NEAR(numGlobal(m_mgr, "mag"), 4.0, 1e-9);
}

TEST_F(LiveCodeBufTest, IfftRoundTrip) {
    // x - fft - ifft ≈ x (1/n scale handled by the implementation).
    ASSERT_TRUE(run(
        "local n = 16\n"
        "local x = {}\n"
        "for i = 1, n do x[i] = math.sin(0.3 * i) + 0.5 * math.cos(1.7 * i) end\n"
        "local spec = yawn.fft(x)\n"
        "local y = yawn.ifft(spec)\n"
        "local e = 0\n"
        "for i = 1, n do e = e + math.abs(y[i] - x[i]) end\n"
        "yawn.err = e\n"
        "yawn.ok = e < 1e-9\n"));
    EXPECT_EQ(numGlobal(m_mgr, "ok"), 1.0);
    EXPECT_LT(numGlobal(m_mgr, "err"), 1e-9);
}

TEST_F(LiveCodeBufTest, FftRejectsNonPowerOfTwo) {
    ASSERT_TRUE(run(
        "local ok, err = pcall(yawn.fft, { 1, 2, 3 })\n"
        "yawn.ok = (ok == false)\n"));
    EXPECT_EQ(numGlobal(m_mgr, "ok"), 1.0);
}

// ── polyBLEP-2 ───────────────────────────────────────────────────────────

TEST_F(LiveCodeBufTest, PolyblepContinuity) {
    // Standard polyBLEP-2: the correction near the wrap (t ≈ 0) is -1
    // and approaches +1 as t → 1 — added to the naive saw this removes
    // both the discontinuity and the overshoot. Far from the edges → 0.
    ASSERT_TRUE(run(
        "local dt = 0.25\n"
        "yawn.a = yawn.polyblep(0.00001, dt)\n"
        "yawn.b = yawn.polyblep(0.99999, dt)\n"
        "yawn.mid = yawn.polyblep(0.5, dt)\n"));
    EXPECT_EQ(numGlobal(m_mgr, "mid"), 0.0);
    EXPECT_NEAR(numGlobal(m_mgr, "a"), -1.0, 1e-4);
    EXPECT_NEAR(numGlobal(m_mgr, "b"), 1.0, 1e-4);
}

// ── Synthesis - deliver round-trip (buffer consumed by the kit) ──────────

TEST_F(LiveCodeBufTest, ForgedBufferLoadsIntoSampler) {
    // 0.25 s of the "synth buzz" — load_sample into a sampler track.
    // uid = 1 adopts the default first track, so the target index is 0.
    ASSERT_TRUE(run("song = { tracks = { { uid = 1, name = 'T1',"
                     " type = 'midi', instrument = { id = 'sampler' } } } }\n"));
    m_engine->pumpInputForTest(nullptr, 256);
    ASSERT_TRUE(m_engine->instrument(0));
    // load_sample routes through the App-provided delivery hooks — wire
    // the sampler one in the fixture (record the delivered frame count).
    m_deliveredFrames = -1;
    if (auto* pm = m_mgr.prerenderManager())
        pm->deliverSampler = [this](std::shared_ptr<audio::AudioBuffer> buf,
                                    const std::string&, int) {
            m_deliveredFrames = buf ? buf->numFrames() : -1;
            return true;
        };
    ASSERT_TRUE(run(
        "local sr = yawn.buffer_info(yawn.new_buffer{frames=1}).sr\n"
        "local f = math.floor(sr * 0.25)\n"
        "yawn.h = yawn.new_buffer{ frames = f, channels = 1,\n"
        "  fill = function(fr) return 0.4 * math.sin(2 * math.pi *"
        " 220.0 * (fr - 1) / sr) end }\n"
        "yawn.loaded = yawn.load_sample(0, yawn.h, 'sampler')\n"));
    EXPECT_EQ(numGlobal(m_mgr, "loaded"), 1.0);
    EXPECT_EQ(m_deliveredFrames, (int)(48000 * 0.25));   // buffer reached the kit
}
