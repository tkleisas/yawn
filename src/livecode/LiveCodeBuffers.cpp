#include "livecode/LiveCodeBuffers.h"

#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "audio/AudioBuffer.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace livecode {

namespace {

// Same registry convention as the engine's getManager.
LiveCodeManager* getManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "yawn_livecode_manager");
    auto* mgr = static_cast<LiveCodeManager*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return mgr;
}

audio::AudioEngine* engineOf(lua_State* L) {
    auto* mgr = getManager(L);
    return mgr ? mgr->audioEngine() : nullptr;
}

// Bound so a runaway synthesis spec can't exhaust the budget/h heap with
// one allocation: 4 channels × 2 minutes at 48 kHz ≈ 23 M samples.
constexpr int64_t kMaxBufferFrames = 6'000'000;
constexpr int kMaxBufferChannels = 16;

// ------------------------------------------------------------------
// Creation / inspection
// ------------------------------------------------------------------

// yawn.new_buffer{ frames = n, channels = c,
//                  fill = fn(frame, ch, frames, channels) -> number
//                | data = { {ch1...}, {ch2...} } } -> handle
static int l_new_buffer(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) { lua_pushinteger(L, 0); return 1; }
    luaL_checktype(L, 1, LUA_TTABLE);
    const int spec = lua_gettop(L);

    lua_getfield(L, spec, "frames");
    const int64_t frames = lua_isinteger(L, -1)
        ? static_cast<int64_t>(lua_tointeger(L, -1))
        : static_cast<int64_t>(luaL_checknumber(L, -1));
    lua_pop(L, 1);
    luaL_argcheck(L, frames > 0 && frames <= kMaxBufferFrames, 1,
                  "frames must be 1..6000000");
    lua_getfield(L, spec, "channels");
    const int channels = static_cast<int>(
        lua_isnil(L, -1) ? 1.0 : lua_tonumber(L, -1));
    lua_pop(L, 1);
    luaL_argcheck(L, channels >= 1 && channels <= kMaxBufferChannels, 1,
                  "channels must be 1..16");

    auto buf = std::make_shared<audio::AudioBuffer>(channels, (int)frames);

    lua_getfield(L, spec, "data");
    if (lua_istable(L, -1)) {
        // data = { {ch1...}, ... } — copied frame-wise, zero-filled gaps.
        for (int ch = 0; ch < channels; ++ch) {
            lua_rawgeti(L, -1, ch + 1);
            if (lua_istable(L, -1)) {
                float* dst = buf->channelData(ch);
                for (int64_t f = 0; f < frames; ++f) {
                    lua_rawgeti(L, -1, (int)(f + 1));
                    if (!lua_isnil(L, -1)) dst[f] = (float)lua_tonumber(L, -1);
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);   // data
        lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(buf)));
        return 1;
    }
    lua_pop(L, 1);   // non-table data

    lua_getfield(L, spec, "fill");
    if (lua_isfunction(L, -1)) {
        for (int ch = 0; ch < channels; ++ch) {
            float* dst = buf->channelData(ch);
            for (int64_t f = 0; f < frames; ++f) {
                lua_pushvalue(L, -1);            // fn
                lua_pushinteger(L, (int)(f + 1));    // frame (1-based)
                lua_pushinteger(L, ch + 1);      // channel (1-based)
                lua_pushnumber(L, (double)frames);
                lua_pushnumber(L, (double)channels);
                lua_call(L, 4, 1);
                dst[f] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);   // fill
        lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(buf)));
        return 1;
    }
    lua_pop(L, 1);

    // No fill / no data → silent buffer (still useful as a container).
    lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(buf)));
    return 1;
}

// yawn.buffer_info(handle) -> { frames = n, channels = c, sr = rate } | nil
static int l_buffer_info(lua_State* L) {
    const std::shared_ptr<audio::AudioBuffer> buf =
        getManager(L) ? getManager(L)->bufferHandle(
                            (uint64_t)luaL_checkinteger(L, 1))
                      : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, buf->numFrames());  lua_setfield(L, -2, "frames");
    lua_pushinteger(L, buf->numChannels());lua_setfield(L, -2, "channels");
    lua_pushinteger(L, engineOf(L) ? engineOf(L)->sampleRate() : 48000);
    lua_setfield(L, -2, "sr");
    return 1;
}

// yawn.buffer_data(handle) -> { frames, channels, sr, data = {...} } | nil
static int l_buffer_data(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> buf =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    const int channels = buf->numChannels();
    const int frames = buf->numFrames();
    lua_newtable(L);
    lua_pushinteger(L, frames);  lua_setfield(L, -2, "frames");
    lua_pushinteger(L, channels);lua_setfield(L, -2, "channels");
    lua_pushinteger(L, engineOf(L) ? engineOf(L)->sampleRate() : 48000);
    lua_setfield(L, -2, "sr");
    lua_newtable(L);                                   // data
    for (int ch = 0; ch < channels; ++ch) {
        lua_newtable(L);
        const float* src = buf->channelData(ch);
        for (int f = 0; f < frames; ++f) {
            lua_pushnumber(L, src[f]);
            lua_rawseti(L, -2, f + 1);
        }
        lua_rawseti(L, -2, ch + 1);
    }
    lua_setfield(L, -2, "data");
    return 1;
}

// yawn.free_buffer(handle) -> bool
static int l_free_buffer(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, mgr->freeBufferHandle(
        (uint64_t)luaL_checkinteger(L, 1)) ? 1 : 0);
    return 1;
}

// ------------------------------------------------------------------
// In-place transforms (each returns the handle for chaining)
// ------------------------------------------------------------------

// yawn.buffer_gain(handle, g) -> handle
static int l_buffer_gain(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> buf =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    const float g = (float)luaL_checknumber(L, 2);
    for (int ch = 0; ch < buf->numChannels(); ++ch) {
        float* d = buf->channelData(ch);
        for (int f = 0; f < buf->numFrames(); ++f) d[f] *= g;
    }
    lua_pushvalue(L, 1);
    return 1;
}

// yawn.buffer_normalize(handle, [peak = 1.0]) -> handle
static int l_buffer_normalize(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> buf =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    float peak = 1.0f;
    if (lua_gettop(L) >= 2) peak = (float)luaL_checknumber(L, 2);
    float maxAbs = 0.0f;
    for (int ch = 0; ch < buf->numChannels(); ++ch) {
        const float* d = buf->channelData(ch);
        for (int f = 0; f < buf->numFrames(); ++f)
            maxAbs = std::max(maxAbs, std::abs(d[f]));
    }
    if (maxAbs > 1e-12f) {
        const float g = peak / maxAbs;
        for (int ch = 0; ch < buf->numChannels(); ++ch) {
            float* d = buf->channelData(ch);
            for (int f = 0; f < buf->numFrames(); ++f) d[f] *= g;
        }
    }
    lua_pushvalue(L, 1);
    return 1;
}

// yawn.buffer_fade(handle, inFrames, outFrames) -> handle
static int l_buffer_fade(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> buf =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    const int inF = (int)luaL_optnumber(L, 2, 0);
    const int outF = (int)luaL_optnumber(L, 3, 0);
    const int nf = buf->numFrames();
    for (int ch = 0; ch < buf->numChannels(); ++ch) {
        float* d = buf->channelData(ch);
        for (int f = 0; f < inF && f < nf; ++f)  d[f] *= (float)f / (float)inF;
        for (int f = 0; f < outF && f < nf; ++f)
            d[nf - 1 - f] *= (float)f / (float)outF;
    }
    lua_pushvalue(L, 1);
    return 1;
}

// yawn.buffer_mix(dst, src, [level = 1.0]) -> dst
static int l_buffer_mix(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> dst =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    const std::shared_ptr<audio::AudioBuffer> src =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 2)) : nullptr;
    if (!dst || !src) { lua_pushnil(L); return 1; }
    const float level = (float)luaL_optnumber(L, 3, 1.0);
    const int channels = std::min(dst->numChannels(), src->numChannels());
    const int frames = std::min(dst->numFrames(), src->numFrames());
    for (int ch = 0; ch < channels; ++ch) {
        float* d = dst->channelData(ch);
        const float* s = src->channelData(ch);
        for (int f = 0; f < frames; ++f) d[f] += s[f] * level;
    }
    lua_pushvalue(L, 1);
    return 1;
}

// yawn.buffer_reverse(handle) -> handle
static int l_buffer_reverse(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> buf =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    for (int ch = 0; ch < buf->numChannels(); ++ch) {
        float* d = buf->channelData(ch);
        std::reverse(d, d + buf->numFrames());
    }
    lua_pushvalue(L, 1);
    return 1;
}

// ------------------------------------------------------------------
// New-buffer transforms
// ------------------------------------------------------------------

// yawn.buffer_slice(handle, firstFrame, len) -> newHandle
static int l_buffer_slice(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> src =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!src) { lua_pushnil(L); return 1; }
    const int first = (int)luaL_checknumber(L, 2) - 1;   // 1-based
    const int len = (int)luaL_checknumber(L, 3);
    luaL_argcheck(L, len > 0, 3, "len must be > 0");
    const int frames = src->numFrames();
    if (first < 0 || first >= frames) { lua_pushnil(L); return 1; }
    const int n = std::min(len, frames - first);
    auto out = std::make_shared<audio::AudioBuffer>(src->numChannels(), n);
    for (int ch = 0; ch < src->numChannels(); ++ch)
        std::memcpy(out->channelData(ch), src->channelData(ch) + first,
                    (size_t)n * sizeof(float));
    lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(out)));
    return 1;
}

// yawn.buffer_concat(a, b) -> newHandle (same channel count required)
static int l_buffer_concat(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> a =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    const std::shared_ptr<audio::AudioBuffer> b =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 2)) : nullptr;
    if (!a || !b) { lua_pushnil(L); return 1; }
    if (a->numChannels() != b->numChannels()) {
        if (mgr) mgr->pushConsole(1, "buffer: concat needs equal channels");
        lua_pushnil(L); return 1;
    }
    const int channels = a->numChannels();
    auto out = std::make_shared<audio::AudioBuffer>(
        channels, a->numFrames() + b->numFrames());
    for (int ch = 0; ch < channels; ++ch) {
        std::memcpy(out->channelData(ch), a->channelData(ch),
                    (size_t)a->numFrames() * sizeof(float));
        std::memcpy(out->channelData(ch) + a->numFrames(), b->channelData(ch),
                    (size_t)b->numFrames() * sizeof(float));
    }
    lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(out)));
    return 1;
}

// yawn.buffer_repeat(handle, n) -> newHandle
static int l_buffer_repeat(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> src =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!src) { lua_pushnil(L); return 1; }
    const int n = (int)luaL_checknumber(L, 2);
    luaL_argcheck(L, n > 0 && n <= 1024, 2, "n must be 1..1024");
    const int channels = src->numFrames() ? src->numChannels() : 1;
    auto out = std::make_shared<audio::AudioBuffer>(
        channels, (int64_t)src->numFrames() * n);
    for (int ch = 0; ch < channels; ++ch) {
        float* dst = out->channelData(ch);
        const float* s = src->numChannels() > 0 ? src->channelData(ch) : nullptr;
        for (int r = 0; r < n; ++r)
            std::memcpy(dst + (size_t)r * src->numFrames(), s,
                        (size_t)src->numFrames() * sizeof(float));
    }
    lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(out)));
    return 1;
}

// yawn.buffer_mixdown(handle) -> newHandle (mono)
static int l_buffer_mixdown(lua_State* L) {
    auto* mgr = getManager(L);
    const std::shared_ptr<audio::AudioBuffer> src =
        mgr ? mgr->bufferHandle((uint64_t)luaL_checkinteger(L, 1)) : nullptr;
    if (!src) { lua_pushnil(L); return 1; }
    const int channels = src->numChannels();
    const int frames = src->numFrames();
    auto out = std::make_shared<audio::AudioBuffer>(1, frames);
    float* d = out->channelData(0);
    for (int f = 0; f < frames; ++f) {
        float acc = 0.0f;
        for (int ch = 0; ch < channels; ++ch) acc += src->channelData(ch)[f];
        d[f] = channels ? acc / (float)channels : acc;
    }
    lua_pushinteger(L, (lua_Integer)mgr->newBufferHandle(std::move(out)));
    return 1;
}

// ------------------------------------------------------------------
// Math
// ------------------------------------------------------------------

// Iterative radix-2 FFT (in place). n must be a power of two.
void fftRadix2(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    if (n <= 1) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2.0 : -2.0) * 3.14159265358979323846
                           / (double)len;
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double)n;
}

static bool isPow2(size_t n) { return n != 0 && (n & (n - 1)) == 0; }

// yawn.fft(table_of_reals) -> interleaved {re0, im0, re1, im1, ...}
// (or nil, err when the length is not a power of two)
static int l_fft(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const int n = (int)lua_rawlen(L, 1);
    luaL_argcheck(L, n > 1 && isPow2((size_t)n), 1,
                  "length must be a power of two");
    std::vector<std::complex<double>> a((size_t)n);
    for (int i = 0; i < n; ++i) {
        lua_rawgeti(L, 1, i + 1);
        a[(size_t)i] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    fftRadix2(a, false);
    lua_createtable(L, 2 * n, 0);
    for (int i = 0; i < n; ++i) {
        lua_pushnumber(L, a[(size_t)i].real());
        lua_rawseti(L, -2, 2 * i + 1);
        lua_pushnumber(L, a[(size_t)i].imag());
        lua_rawseti(L, -2, 2 * i + 2);
    }
    return 1;
}

// yawn.ifft(interleaved) -> table of reals (1/n scaled)
static int l_ifft(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const int n2 = (int)lua_rawlen(L, 1);
    luaL_argcheck(L, n2 > 2 && n2 % 2 == 0, 1, "even interleaved table");
    const int n = n2 / 2;
    luaL_argcheck(L, isPow2((size_t)n), 1, "half-length must be a power of two");
    std::vector<std::complex<double>> a((size_t)n);
    for (int i = 0; i < n; ++i) {
        lua_rawgeti(L, 1, 2 * i + 1);
        const double re = lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, 1, 2 * i + 2);
        const double im = lua_tonumber(L, -1);
        lua_pop(L, 1);
        a[(size_t)i] = {re, im};
    }
    fftRadix2(a, true);
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; ++i) {
        lua_pushnumber(L, a[(size_t)i].real());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// yawn.polyblep(phase, dt) -> polyBLEP-2 correction for a naive
// sawtooth discontinuity at phase 0/1 (phase and dt in cycles).
static int l_polyblep(lua_State* L) {
    double t = luaL_checknumber(L, 1);
    const double dt = luaL_checknumber(L, 2);
    if (dt <= 0.0) { lua_pushnumber(L, 0.0); return 1; }
    t -= std::floor(t);
    if (t < dt) {
        t /= dt;
        lua_pushnumber(L, t + t - t * t - 1.0);
    } else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        lua_pushnumber(L, t * t + 2.0 * t + 1.0);
    } else {
        lua_pushnumber(L, 0.0);
    }
    return 1;
}

} // namespace

void registerBufferAPI(lua_State* L) {
    lua_getglobal(L, "yawn");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    static const luaL_Reg funcs[] = {
        {"new_buffer",     l_new_buffer},
        {"buffer_info",    l_buffer_info},
        {"buffer_data",    l_buffer_data},
        {"free_buffer",    l_free_buffer},
        {"buffer_gain",    l_buffer_gain},
        {"buffer_normalize", l_buffer_normalize},
        {"buffer_fade",    l_buffer_fade},
        {"buffer_mix",     l_buffer_mix},
        {"buffer_reverse", l_buffer_reverse},
        {"buffer_slice",   l_buffer_slice},
        {"buffer_concat",  l_buffer_concat},
        {"buffer_repeat",  l_buffer_repeat},
        {"buffer_mixdown", l_buffer_mixdown},
        {"fft",            l_fft},
        {"ifft",           l_ifft},
        {"polyblep",       l_polyblep},
        {nullptr, nullptr}
    };
    luaL_setfuncs(L, funcs, 0);
    lua_pop(L, 1);
}

} // namespace livecode
} // namespace yawn
