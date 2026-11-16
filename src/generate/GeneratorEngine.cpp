// GeneratorEngine.cpp — sandboxed Lua host for the generative song layer.

#include "generate/GeneratorEngine.h"

#include "util/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace yawn {
namespace generate {

namespace {

constexpr const char* kEngineKey = "yawn.generator.engine";
constexpr const char* kCacheKey  = "yawn.generator.modules";
constexpr const char* kInitKey   = "yawn.generator.init";

GeneratorEngine* engineFromLua(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kEngineKey);
    auto* e = static_cast<GeneratorEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return e;
}

int l_log(lua_State* L) {
    std::string msg;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) msg += ' ';
        msg.append(s, len);
        lua_pop(L, 1);  // tolstring pushes a copy
    }
    LOG_INFO("Generate", "%s", msg.c_str());
    return 0;
}

int l_rng_float(lua_State* L) {
    auto* e = engineFromLua(L);
    lua_pushnumber(L, e ? e->rngUnit() : 0.0);
    return 1;
}

int l_rng_int(lua_State* L) {
    auto* e = engineFromLua(L);
    lua_Integer n = luaL_checkinteger(L, 1);
    if (n <= 0) return luaL_error(L, "rng.int(n) needs n > 0");
    double u = e ? e->rngUnit() : 0.0;
    lua_Integer v = static_cast<lua_Integer>(u * static_cast<double>(n));
    if (v >= n) v = n - 1;
    lua_pushinteger(L, v);
    return 1;
}

int l_rng_pick(lua_State* L) {
    auto* e = engineFromLua(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer n = luaL_len(L, 1);
    if (n <= 0) return luaL_error(L, "rng.pick(table) needs a non-empty table");
    double u = e ? e->rngUnit() : 0.0;
    lua_Integer v = static_cast<lua_Integer>(u * static_cast<double>(n));
    if (v >= n) v = n - 1;
    lua_rawgeti(L, 1, v + 1);
    return 1;
}

int l_rng_chance(lua_State* L) {
    auto* e = engineFromLua(L);
    double p = luaL_checknumber(L, 1);
    lua_pushboolean(L, (e ? e->rngUnit() : 0.0) < p);
    return 1;
}

// Only .lua modules under the engine's script roots; no C modules, no
// package.searchpath, no filesystem escape (`..`, absolute paths).
//
// Errors are returned through `error` and raised by the thin wrapper
// after every C++ object has been destroyed: luaL_error longjmps, and
// longjmping with a live std::string/filesystem::path skips their
// destructors (LeakSanitizer caught exactly that in the error paths).
int requireImpl(lua_State* L, std::string& error) {
    auto* engine = engineFromLua(L);
    if (!engine) {
        error = "generator engine not available";
        return -1;
    }

    const int base = lua_gettop(L);
    const char* cname = luaL_checkstring(L, 1);
    std::string name = cname;
    if (name.empty() || name[0] == '/' || name[0] == '\\' ||
        name.find("..") != std::string::npos ||
        name.find('\\') != std::string::npos) {
        error = "invalid generator module name '" + name + "'";
        return -1;
    }

    // Cache lookup. A "loading" marker detects require cycles.
    lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
    lua_getfield(L, -1, name.c_str());
    if (lua_isstring(L, -1) &&
        std::strcmp(lua_tostring(L, -1), "loading") == 0) {
        error = "circular require of module '" + name + "'";
        lua_settop(L, base);
        return -1;
    }
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);  // drop the cache table, keep the value
        return 1;
    }
    lua_settop(L, base);

    std::string rel = name;
    std::replace(rel.begin(), rel.end(), '.', '/');

    std::filesystem::path found;
    for (const auto& root : engine->scriptRoots()) {
        std::error_code ec;
        auto flat = root / (rel + ".lua");
        if (std::filesystem::is_regular_file(flat, ec)) { found = flat; break; }
        auto nested = root / rel / "init.lua";
        if (std::filesystem::is_regular_file(nested, ec)) { found = nested; break; }
    }
    if (found.empty()) {
        error = "generator module '" + name + "' not found in " +
                std::to_string(engine->scriptRoots().size()) +
                " script root(s)";
        return -1;
    }

    // Mark as loading before executing so cycles fail cleanly.
    lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
    lua_pushstring(L, "loading");
    lua_setfield(L, -2, name.c_str());
    lua_pop(L, 1);

    if (luaL_loadfilex(L, found.string().c_str(), "t") != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        error = "cannot load generator module '" + name + "': " +
                (msg ? msg : "unknown error");
        lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
        lua_pushnil(L);
        lua_setfield(L, -2, name.c_str());
        lua_settop(L, base);
        return -1;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        error = "error in generator module '" + name + "': " +
                (msg ? msg : "unknown error");
        lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
        lua_pushnil(L);
        lua_setfield(L, -2, name.c_str());
        lua_settop(L, base);
        return -1;
    }
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }

    lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, name.c_str());
    lua_pop(L, 1);
    return 1;
}

int l_require(lua_State* L) {
    const int base = lua_gettop(L);
    std::string error;
    const int result = requireImpl(L, error);
    if (result < 0) {
        lua_settop(L, base);
        lua_pushlstring(L, error.data(), error.size());
        std::string().swap(error);  // free before lua_error longjmps
        return lua_error(L);
    }
    return result;
}

bool getIntField(lua_State* L, const char* field, int lo, int hi,
                 int& out, std::string& err, const std::string& ctx,
                 bool required, int fallback) {
    lua_getfield(L, -1, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        if (required) {
            err = ctx + " is missing '" + field + "'";
            return false;
        }
        out = fallback;
        return true;
    }
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        err = ctx + "." + field + " must be a number";
        return false;
    }
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(v) || v < lo || v > hi) {
        err = ctx + "." + field + " out of range";
        return false;
    }
    out = static_cast<int>(std::lround(v));
    return true;
}

bool getNumberField(lua_State* L, const char* field, double lo, double hi,
                    double& out, std::string& err, const std::string& ctx,
                    bool required, double fallback) {
    lua_getfield(L, -1, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        if (required) {
            err = ctx + " is missing '" + field + "'";
            return false;
        }
        out = fallback;
        return true;
    }
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        err = ctx + "." + field + " must be a number";
        return false;
    }
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(v) || v < lo || v > hi) {
        err = ctx + "." + field + " out of range";
        return false;
    }
    out = v;
    return true;
}

bool getStringField(lua_State* L, const char* field, std::string& out,
                    std::string& err, const std::string& ctx,
                    bool required, const std::string& fallback) {
    lua_getfield(L, -1, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        if (required) {
            err = ctx + " is missing '" + field + "'";
            return false;
        }
        out = fallback;
        return true;
    }
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        err = ctx + "." + field + " must be a string";
        return false;
    }
    out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return true;
}

} // namespace

GeneratorEngine::GeneratorEngine() = default;

GeneratorEngine::~GeneratorEngine() { closeState(); }

void GeneratorEngine::setScriptRoots(std::vector<std::filesystem::path> roots) {
    m_roots = std::move(roots);
}

void GeneratorEngine::closeState() {
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
    m_ready = false;
}

double GeneratorEngine::rngUnit() {
    uint64_t x = m_rngState;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    m_rngState = x;
    uint64_t r = x * 2685821657736338717ull;
    return (r >> 11) * (1.0 / 9007199254740992.0);
}

std::string GeneratorEngine::scriptsDir() const {
    for (const auto& root : m_roots) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(root / "init.lua", ec))
            return root.string();
        if (std::filesystem::is_regular_file(root / "init" / "init.lua", ec))
            return root.string();
    }
    return {};
}

std::vector<std::string> GeneratorEngine::listStyles() const {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const auto& root : m_roots) {
        std::error_code ec;
        const auto dir = root / "styles";
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".lua") continue;
            const std::string id = entry.path().stem().string();
            if (seen.insert(id).second) out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> GeneratorEngine::listScales() const {
    std::vector<std::string> out;
    if (!m_ready || !m_L) return out;

    lua_State* L = m_L;
    const int base = lua_gettop(L);
    lua_getglobal(L, "require");
    lua_pushstring(L, "theory");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_settop(L, base);
        return out;
    }
    if (!lua_istable(L, -1)) {
        lua_settop(L, base);
        return out;
    }
    lua_getfield(L, -1, "scaleIds");
    if (lua_istable(L, -1)) {
        const int n = static_cast<int>(luaL_len(L, -1));
        for (int i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_isstring(L, -1))
                out.emplace_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    std::sort(out.begin(), out.end());
    lua_settop(L, base);
    return out;
}

std::string GeneratorEngine::tracebackFromTop(const char* context) {
    std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1)
                                            : "unknown error";
    luaL_traceback(m_L, m_L, msg.c_str(), 1);
    std::string full = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1)
                                             : msg;
    lua_pop(m_L, 1);
    return std::string(context) + ": " + full;
}

bool GeneratorEngine::initState(std::string* error) {
    m_L = luaL_newstate();
    if (!m_L) {
        if (error) *error = "failed to create Lua state";
        return false;
    }

    luaL_openlibs(m_L);

    // Sandbox: no filesystem/process access, no dynamic C loading. Our
    // require() only loads .lua modules from the configured roots.
    static const char* const kRemove[] = {
        "io", "os", "package", "debug", "dofile", "loadfile", "require",
    };
    for (const char* name : kRemove) {
        lua_pushnil(m_L);
        lua_setglobal(m_L, name);
    }
    lua_register(m_L, "require", l_require);
    lua_register(m_L, "print", l_log);

    lua_pushlightuserdata(m_L, this);
    lua_setfield(m_L, LUA_REGISTRYINDEX, kEngineKey);
    lua_newtable(m_L);
    lua_setfield(m_L, LUA_REGISTRYINDEX, kCacheKey);

    // yawn.rng / yawn.log / yawn.lua_version
    lua_newtable(m_L);
    lua_newtable(m_L);
    lua_pushcfunction(m_L, l_rng_float);
    lua_setfield(m_L, -2, "float");
    lua_pushcfunction(m_L, l_rng_int);
    lua_setfield(m_L, -2, "int");
    lua_pushcfunction(m_L, l_rng_pick);
    lua_setfield(m_L, -2, "pick");
    lua_pushcfunction(m_L, l_rng_chance);
    lua_setfield(m_L, -2, "chance");
    lua_setfield(m_L, -2, "rng");
    lua_pushcfunction(m_L, l_log);
    lua_setfield(m_L, -2, "log");
    lua_pushstring(m_L, LUA_VERSION);
    lua_setfield(m_L, -2, "lua_version");
    lua_setglobal(m_L, "yawn");

    // Determinism: only yawn.rng is allowed to produce randomness.
    lua_getglobal(m_L, "math");
    if (lua_istable(m_L, -1)) {
        lua_pushnil(m_L);
        lua_setfield(m_L, -2, "random");
        lua_pushnil(m_L);
        lua_setfield(m_L, -2, "randomseed");
    }
    lua_pop(m_L, 1);

    lua_getglobal(m_L, "require");
    lua_pushstring(m_L, "init");
    if (lua_pcall(m_L, 1, 1, 0) != LUA_OK) {
        if (error) *error = tracebackFromTop("cannot load generator init.lua");
        lua_pop(m_L, 1);
        return false;
    }
    if (!lua_istable(m_L, -1)) {
        if (error) *error = "generator init.lua must return a table";
        lua_pop(m_L, 1);
        return false;
    }
    lua_setfield(m_L, LUA_REGISTRYINDEX, kInitKey);

    lua_getfield(m_L, LUA_REGISTRYINDEX, kInitKey);
    lua_getfield(m_L, -1, "generate");
    bool hasGenerate = lua_isfunction(m_L, -1);
    lua_pop(m_L, 2);
    if (!hasGenerate) {
        if (error) *error = "generator init.lua must expose generate(params)";
        return false;
    }

    return true;
}

bool GeneratorEngine::reload(std::string* error) {
    closeState();
    if (m_roots.empty()) {
        if (error) *error = "no generator script roots configured";
        return false;
    }
    if (!initState(error)) {
        closeState();
        return false;
    }
    m_ready = true;
    LOG_INFO("Generate", "generator scripts loaded from %s",
             scriptsDir().c_str());
    return true;
}

void GeneratorEngine::pushParams(const GeneratorParams& p) {
    lua_State* L = m_L;
    lua_newtable(L);

    lua_pushinteger(L, static_cast<lua_Integer>(p.seed));
    lua_setfield(L, -2, "seed");

    lua_newtable(L);
    int i = 1;
    auto pushStyle = [&](const std::string& id, float w) {
        lua_newtable(L);
        lua_pushstring(L, id.c_str());
        lua_setfield(L, -2, "id");
        lua_pushnumber(L, w);
        lua_setfield(L, -2, "weight");
        lua_rawseti(L, -2, i++);
    };
    if (p.styles.empty()) pushStyle("pop", 1.0f);
    else for (const auto& s : p.styles) pushStyle(s.id, s.weight);
    lua_setfield(L, -2, "styles");

    auto setStr = [&](const char* k, const std::string& v) {
        lua_pushstring(L, v.c_str());
        lua_setfield(L, -2, k);
    };
    setStr("blend", p.blend);
    setStr("scale", p.scale);
    setStr("root", p.root);
    setStr("movement", p.movement);
    lua_pushboolean(L, p.generateVisuals ? 1 : 0);
    lua_setfield(L, -2, "generateVisuals");
    setStr("visualTheme", p.visualTheme);
    lua_pushnumber(L, p.visualReactivity);
    lua_setfield(L, -2, "visualReactivity");

    // Shader catalog for visuals.lua: id/path + @range metadata.
    lua_newtable(L);
    int ci = 1;
    for (const auto& shader : m_visualCatalog) {
        lua_newtable(L);
        lua_pushstring(L, shader.id.c_str());
        lua_setfield(L, -2, "id");
        lua_pushstring(L, shader.path.c_str());
        lua_setfield(L, -2, "path");
        lua_newtable(L);
        int pi = 1;
        for (const auto& param : shader.params) {
            lua_newtable(L);
            lua_pushstring(L, param.name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushnumber(L, param.minValue);
            lua_setfield(L, -2, "min");
            lua_pushnumber(L, param.maxValue);
            lua_setfield(L, -2, "max");
            lua_pushnumber(L, param.defaultValue);
            lua_setfield(L, -2, "default");
            lua_rawseti(L, -2, pi++);
        }
        lua_setfield(L, -2, "params");
        lua_rawseti(L, -2, ci++);
    }
    lua_setfield(L, -2, "visualCatalog");

    lua_pushnumber(L, p.density);
    lua_setfield(L, -2, "density");
    lua_pushnumber(L, p.noteLength);
    lua_setfield(L, -2, "noteLength");
    lua_pushnumber(L, p.swing);
    lua_setfield(L, -2, "swing");
    lua_pushinteger(L, p.tempo);
    lua_setfield(L, -2, "tempo");
    lua_pushinteger(L, p.beatsPerBar);
    lua_setfield(L, -2, "beatsPerBar");

    lua_newtable(L);
    int si = 1;
    for (const auto& s : p.sections) {
        lua_pushstring(L, s.c_str());
        lua_rawseti(L, -2, si++);
    }
    lua_setfield(L, -2, "sections");
}

bool GeneratorEngine::readSong(GeneratedSong& out, std::string* error) {
    lua_State* L = m_L;
    const int base = lua_gettop(L);
    auto fail = [&](const std::string& m) {
        lua_settop(L, base);
        if (error) *error = m;
        return false;
    };

    if (!lua_istable(L, -1)) return fail("generator must return a song table");

    GeneratedSong song;
    std::string err;

    if (!getStringField(L, "title", song.title, err, "song", false, "Untitled"))
        return fail(err);

    int tempo = 120;
    if (!getIntField(L, "tempo", 20, 999, tempo, err, "song", false, 120))
        return fail(err);
    int bpb = 4;
    if (!getIntField(L, "beatsPerBar", 1, 16, bpb, err, "song", false, 4))
        return fail(err);
    song.tempo = tempo;
    song.beatsPerBar = bpb;

    lua_getfield(L, -1, "sections");
    if (!lua_istable(L, -1)) return fail("song.sections must be an array");

    const int numSections = static_cast<int>(luaL_len(L, -1));
    if (numSections < 1 || numSections > 64)
        return fail("song.sections must contain 1..64 sections");

    int totalNotes = 0;
    song.sections.reserve(static_cast<size_t>(numSections));

    for (int si = 1; si <= numSections; ++si) {
        lua_rawgeti(L, -1, si);
        if (!lua_istable(L, -1))
            return fail("song.sections[" + std::to_string(si) + "] must be a table");

        const std::string sctx = "sections[" + std::to_string(si) + "]";
        GeneratedSection sec;

        if (!getStringField(L, "id", sec.id, err, sctx, false, "section"))
            return fail(err);
        if (!getStringField(L, "name", sec.name, err, sctx, false, ""))
            return fail(err);
        if (sec.name.empty()) sec.name = sec.id;
        if (!getIntField(L, "bars", 1, 256, sec.bars, err, sctx, true, 4))
            return fail(err);

        const double secBeats =
            static_cast<double>(sec.bars) * song.beatsPerBar;

        lua_getfield(L, -1, "clips");
        if (!lua_istable(L, -1))
            return fail(sctx + ".clips must be an array");
        const int numClips = static_cast<int>(luaL_len(L, -1));
        if (numClips < 1 || numClips > 32)
            return fail(sctx + ".clips must contain 1..32 lanes");

        sec.clips.reserve(static_cast<size_t>(numClips));
        for (int ci = 1; ci <= numClips; ++ci) {
            lua_rawgeti(L, -1, ci);
            if (!lua_istable(L, -1))
                return fail(sctx + ".clips[" + std::to_string(ci) + "] must be a table");

            const std::string cctx =
                sctx + ".clips[" + std::to_string(ci) + "]";
            GeneratedClip clip;
            if (!getStringField(L, "role", clip.role, err, cctx, true, "track"))
                return fail(err);
            if (clip.role.empty()) clip.role = "track";
            double clipLen = secBeats;
            if (!getNumberField(L, "length", 0.0001, 1e6, clipLen, err,
                                cctx, false, secBeats))
                return fail(err);
            if (clipLen > secBeats + 1e-6) {
                return fail(cctx + ".length " + std::to_string(clipLen) +
                            " exceeds the section length " +
                            std::to_string(secBeats));
            }
            clip.lengthBeats = clipLen;

            lua_getfield(L, -1, "notes");
            if (!lua_istable(L, -1))
                return fail(cctx + ".notes must be an array");
            const int numNotes = static_cast<int>(luaL_len(L, -1));
            if (numNotes > 50000)
                return fail(cctx + " has more than 50000 notes");
            totalNotes += numNotes;
            if (totalNotes > 200000)
                return fail("song has more than 200000 notes");

            clip.notes.reserve(static_cast<size_t>(numNotes));
            for (int ni = 1; ni <= numNotes; ++ni) {
                lua_rawgeti(L, -1, ni);
                if (!lua_istable(L, -1))
                    return fail(cctx + ".notes[" + std::to_string(ni) +
                                "] must be a table");
                const std::string nctx =
                    cctx + ".notes[" + std::to_string(ni) + "]";

                GeneratedNote note;
                if (!getNumberField(L, "beat", 0.0, 1e6, note.startBeat,
                                    err, nctx, true, 0.0))
                    return fail(err);
                if (!getNumberField(L, "dur", 1e-6, 1e6, note.duration,
                                    err, nctx, true, 0.25))
                    return fail(err);
                int pitch = 60;
                if (!getIntField(L, "pitch", 0, 127, pitch, err, nctx, true, 60))
                    return fail(err);
                note.pitch = pitch;
                double cents = 0.0;
                if (!getNumberField(L, "cents", -1200.0, 1200.0, cents,
                                    err, nctx, false, 0.0))
                    return fail(err);
                note.cents = static_cast<float>(cents);
                int vel = 100;
                if (!getIntField(L, "vel", 1, 127, vel, err, nctx, false, 100))
                    return fail(err);
                note.velocity = vel;
                int ch = 0;
                if (!getIntField(L, "ch", 0, 15, ch, err, nctx, false, 0))
                    return fail(err);
                note.channel = ch;

                if (note.startBeat + note.duration > clipLen + 1e-6) {
                    return fail(nctx + " ends at " +
                                std::to_string(note.startBeat + note.duration) +
                                " but the clip is " +
                                std::to_string(clipLen) + " beats");
                }
                clip.notes.push_back(note);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);  // notes

            sec.clips.push_back(std::move(clip));
            lua_pop(L, 1);  // clip
        }
        lua_pop(L, 1);  // clips

        // Optional visual plan for this section (one clip for now).
        lua_getfield(L, -1, "visuals");
        if (!lua_isnil(L, -1)) {
            if (!lua_istable(L, -1))
                return fail(sctx + ".visuals must be an array");
            const int numVisuals = static_cast<int>(luaL_len(L, -1));
            if (numVisuals > 8)
                return fail(sctx + ".visuals supports at most 8 visuals");
            for (int vi = 1; vi <= numVisuals; ++vi) {
                lua_rawgeti(L, -1, vi);
                if (!lua_istable(L, -1))
                    return fail(sctx + ".visuals[" + std::to_string(vi) +
                                "] must be a table");
                const std::string vctx =
                    sctx + ".visuals[" + std::to_string(vi) + "]";
                GeneratedVisual vis;
                if (!getStringField(L, "shader", vis.shaderId, err, vctx,
                                    true, ""))
                    return fail(err);
                if (vis.shaderId.empty())
                    return fail(vctx + ".shader must not be empty");
                if (m_visualCatalog.empty())
                    return fail("visual shader catalog is empty");
                bool found = false;
                for (const auto& shader : m_visualCatalog) {
                    if (shader.id == vis.shaderId) {
                        vis.shaderPath = shader.path;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return fail(vctx + ": unknown visual shader '" +
                                vis.shaderId + "'");
                if (!getStringField(L, "name", vis.name, err, vctx, false,
                                    vis.shaderId))
                    return fail(err);
                if (!getStringField(L, "role", vis.role, err, vctx, false,
                                    "visual"))
                    return fail(err);
                if (!getStringField(L, "audioRole", vis.audioRole, err, vctx,
                                    false, vis.role))
                    return fail(err);
                if (!getStringField(L, "image", vis.image, err, vctx, false,
                                    ""))
                    return fail(err);
                double reactivity = 1.0;
                if (!getNumberField(L, "reactivity", 0.0, 4.0, reactivity,
                                    err, vctx, false, 1.0))
                    return fail(err);
                vis.reactivity = static_cast<float>(reactivity);
                if (!vis.image.empty() &&
                    (vis.image.find('/') != std::string::npos ||
                     vis.image.find('\\') != std::string::npos ||
                     vis.image.find("..") != std::string::npos))
                    return fail(vctx + ".image must be a plain filename");

                lua_getfield(L, -1, "params");
                if (!lua_isnil(L, -1)) {
                    if (!lua_istable(L, -1))
                        return fail(vctx + ".params must be a table");
                    int count = 0;
                    lua_pushnil(L);
                    while (lua_next(L, -2) != 0) {
                        if (!lua_isstring(L, -2) || !lua_isnumber(L, -1)) {
                            return fail(vctx +
                                ".params must map names to numbers");
                        }
                        const std::string key = lua_tostring(L, -2);
                        const double value = lua_tonumber(L, -1);
                        if (!std::isfinite(value) || std::abs(value) > 1e6)
                            return fail(vctx + ".params['" + key +
                                        "'] is out of range");
                        vis.params.emplace_back(key,
                            static_cast<float>(value));
                        if (++count > 64)
                            return fail(vctx + ".params has more than 64 entries");
                        lua_pop(L, 1);  // pop value, keep key
                    }
                    lua_pop(L, 1);  // params table
                } else {
                    lua_pop(L, 1);  // nil
                }

                sec.visuals.push_back(std::move(vis));
                lua_pop(L, 1);  // visual
            }
            lua_pop(L, 1);  // visuals table
        } else {
            lua_pop(L, 1);  // nil
        }

        song.sections.push_back(std::move(sec));
        lua_pop(L, 1);  // section
    }
    lua_pop(L, 1);  // sections

    out = std::move(song);
    return true;
}

bool GeneratorEngine::generate(const GeneratorParams& params,
                               GeneratedSong& out, std::string* error) {
    if (!m_ready || !m_L) {
        if (error) *error = "generator is not loaded";
        return false;
    }

    m_rngState = params.seed ^ 0x9E3779B97F4A7C15ull;
    if (m_rngState == 0) m_rngState = 0x123456789ABCDEFull;

    lua_State* L = m_L;
    lua_getfield(L, LUA_REGISTRYINDEX, kInitKey);
    lua_getfield(L, -1, "generate");
    if (!lua_isfunction(L, -1)) {
        if (error) *error = "generator init table has no generate()";
        lua_pop(L, 2);
        return false;
    }
    lua_remove(L, -2);  // drop the init table, keep the function
    pushParams(params);

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        if (error) *error = tracebackFromTop("generator failed");
        lua_pop(L, 1);
        return false;
    }

    bool ok = readSong(out, error);
    lua_pop(L, 1);  // song table
    if (ok) {
        LOG_INFO("Generate", "song '%s': %d sections, %d notes, %d bpm",
                 out.title.c_str(),
                 static_cast<int>(out.sections.size()),
                 out.totalNotes(), out.tempo);
    }
    return ok;
}

} // namespace generate
} // namespace yawn
