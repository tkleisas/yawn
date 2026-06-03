#include "visual/gltf/M3DSceneScript.h"
#include "util/Logger.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstring>

namespace yawn {
namespace visual {
namespace fs = std::filesystem;

namespace {

// Fill a pre-pushed table index with a read-only view of the inputs.
// Keeps the Lua surface tight: no globals, just the ctx argument.
void pushContext(lua_State* L, const M3DSceneScript::Inputs& in) {
    lua_newtable(L);

    lua_pushnumber(L, in.time);    lua_setfield(L, -2, "time");
    lua_pushnumber(L, in.beat);    lua_setfield(L, -2, "beat");
    lua_pushboolean(L, in.playing ? 1 : 0); lua_setfield(L, -2, "playing");

    // ctx.audio
    lua_newtable(L);
    lua_pushnumber(L, in.audioLevel); lua_setfield(L, -2, "level");
    lua_pushnumber(L, in.audioLow);   lua_setfield(L, -2, "low");
    lua_pushnumber(L, in.audioMid);   lua_setfield(L, -2, "mid");
    lua_pushnumber(L, in.audioHigh);  lua_setfield(L, -2, "high");
    lua_pushnumber(L, in.kick);       lua_setfield(L, -2, "kick");
    lua_setfield(L, -2, "audio");

    // ctx.knobs (keyed A..H + also 1..8 so both idioms work)
    lua_newtable(L);
    for (int i = 0; i < 8; ++i) {
        char key[2] = { static_cast<char>('A' + i), 0 };
        lua_pushnumber(L, in.knobs[i]);
        lua_setfield(L, -2, key);
        lua_pushnumber(L, in.knobs[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "knobs");

    // ctx.notes — array (1..N) of { track, channel, pitch, vel, age }.
    lua_createtable(L, static_cast<int>(in.notes.size()), 0);
    for (size_t i = 0; i < in.notes.size(); ++i) {
        const auto& n = in.notes[i];
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, n.track);   lua_setfield(L, -2, "track");
        lua_pushinteger(L, n.channel); lua_setfield(L, -2, "channel");
        lua_pushinteger(L, n.pitch);   lua_setfield(L, -2, "pitch");
        lua_pushnumber(L, n.vel);      lua_setfield(L, -2, "vel");
        lua_pushnumber(L, n.age);      lua_setfield(L, -2, "age");
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    lua_setfield(L, -2, "notes");
}

// Read an optional numeric field, returning `fallback` if absent or
// non-numeric. Pops the field value off the stack before returning.
float readNumberField(lua_State* L, int tableIdx, const char* key,
                       float fallback) {
    lua_getfield(L, tableIdx, key);
    float v = fallback;
    if (lua_type(L, -1) == LUA_TNUMBER) {
        v = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
    return v;
}

// Read an optional 3-element numeric array (used for position/rotation).
// Accepts either {1,2,3} (rawseti 1..3) or {x=1,y=2,z=3}. Returns true
// if anything was read (partial is ok — missing components keep fallback).
bool readVec3Field(lua_State* L, int tableIdx, const char* key,
                    float out[3]) {
    lua_getfield(L, tableIdx, key);
    if (lua_type(L, -1) != LUA_TTABLE) { lua_pop(L, 1); return false; }
    int t = lua_gettop(L);

    // Try indexed first — most natural for {x,y,z}.
    lua_rawgeti(L, t, 1);
    if (lua_type(L, -1) == LUA_TNUMBER) out[0] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_rawgeti(L, t, 2);
    if (lua_type(L, -1) == LUA_TNUMBER) out[1] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_rawgeti(L, t, 3);
    if (lua_type(L, -1) == LUA_TNUMBER) out[2] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    // Named fields override indexed (x/y/z if explicitly present).
    lua_getfield(L, t, "x");
    if (lua_type(L, -1) == LUA_TNUMBER) out[0] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, t, "y");
    if (lua_type(L, -1) == LUA_TNUMBER) out[1] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, t, "z");
    if (lua_type(L, -1) == LUA_TNUMBER) out[2] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_pop(L, 1);  // the vec table
    return true;
}

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────

M3DSceneScript::~M3DSceneScript() { shutdown(); }

void M3DSceneScript::shutdown() {
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
    m_error.clear();
    m_mtimeValid = false;
}

bool M3DSceneScript::load(const std::string& path) {
    shutdown();
    m_path = path;

    m_L = luaL_newstate();
    if (!m_L) {
        m_error = "failed to create Lua state";
        LOG_ERROR("M3DScene", "%s", m_error.c_str());
        return false;
    }

    // Open a trimmed stdlib: math / table / string / utf8 are useful
    // for scene logic; io / os / debug / package open a needless
    // attack surface for a script that lives inside a project file.
    luaL_requiref(m_L, LUA_GNAME,        luaopen_base,     1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_MATHLIBNAME,  luaopen_math,     1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_TABLIBNAME,   luaopen_table,    1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_STRLIBNAME,   luaopen_string,   1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_UTF8LIBNAME,  luaopen_utf8,     1); lua_pop(m_L, 1);

    if (luaL_dofile(m_L, path.c_str()) != LUA_OK) {
        m_error = lua_tostring(m_L, -1);
        LOG_ERROR("M3DScene", "Load failed: %s", m_error.c_str());
        lua_pop(m_L, 1);
        // Keep the state alive but flagged invalid, so the user can
        // edit the file and hot-reload will pick up the fix.
        return false;
    }

    // Stamp the mtime so pollHotReload() only triggers on actual edits.
    std::error_code ec;
    m_mtime = fs::last_write_time(path, ec);
    m_mtimeValid = !ec;
    m_error.clear();
    LOG_INFO("M3DScene", "Loaded %s", path.c_str());
    return true;
}

void M3DSceneScript::pollHotReload() {
    if (m_path.empty()) return;
    std::error_code ec;
    auto cur = fs::last_write_time(m_path, ec);
    if (ec) return;
    if (m_mtimeValid && cur == m_mtime) return;
    LOG_INFO("M3DScene", "Hot-reload %s", m_path.c_str());
    load(m_path);
}

// ── tick() ────────────────────────────────────────────────────────────────

namespace {

// True if the table at `idx` carries any recognized instance field —
// used to tell a single-instance shorthand ({position=...}) from an
// empty list ({} = draw nothing).
bool hasInstanceField(lua_State* L, int idx) {
    static const char* kKeys[] = { "position", "rotation", "scale",
                                   "model", "color", "emissive",
                                   "opacity", "anim" };
    for (const char* k : kKeys) {
        lua_getfield(L, idx, k);
        bool present = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (present) return true;
    }
    return false;
}

// Read one instance table (at absolute stack index `idx`) into `inst`.
void readInstance(lua_State* L, int idx, M3DInstance& inst) {
    float v[3] = { 0, 0, 0 };
    if (readVec3Field(L, idx, "position", v)) {
        inst.position[0] = v[0]; inst.position[1] = v[1]; inst.position[2] = v[2];
    }
    v[0] = v[1] = v[2] = 0;
    if (readVec3Field(L, idx, "rotation", v)) {
        inst.rotationDeg[0] = v[0]; inst.rotationDeg[1] = v[1]; inst.rotationDeg[2] = v[2];
    }
    // scale: a number → uniform; a table → per-axis multiplier.
    lua_getfield(L, idx, "scale");
    if (lua_type(L, -1) == LUA_TNUMBER) {
        inst.scale = static_cast<float>(lua_tonumber(L, -1));
    } else if (lua_istable(L, -1)) {
        float s[3] = { 1, 1, 1 };
        readVec3Field(L, idx, "scale", s);   // re-read named/indexed components
        inst.scale3[0] = s[0]; inst.scale3[1] = s[1]; inst.scale3[2] = s[2];
    }
    lua_pop(L, 1);

    float col[3] = { 1, 1, 1 };
    if (readVec3Field(L, idx, "color", col)) {
        inst.color[0] = col[0]; inst.color[1] = col[1]; inst.color[2] = col[2];
    }
    inst.emissive = readNumberField(L, idx, "emissive", 0.0f);
    inst.opacity  = readNumberField(L, idx, "opacity",  1.0f);
    inst.model    = static_cast<int>(readNumberField(L, idx, "model", 0.0f));

    // anim = { clip = N, time = t } (both optional).
    lua_getfield(L, idx, "anim");
    if (lua_istable(L, -1)) {
        int animIdx = lua_gettop(L);
        inst.animClip = static_cast<int>(readNumberField(L, animIdx, "clip", -1.0f));
        inst.animTime = readNumberField(L, animIdx, "time", -1.0f);
    }
    lua_pop(L, 1);
}

} // anonymous namespace

bool M3DSceneScript::tick(const Inputs& in,
                          std::vector<M3DInstance>& out,
                          M3DCamera* outCamera) {
    out.clear();
    if (!m_L) return false;

    lua_getglobal(m_L, "tick");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        m_error = "script has no global function `tick`";
        return false;
    }

    pushContext(m_L, in);

    // Two results: (1) the instance list, (2) optional scene options
    // (currently just `camera`). Missing results come back as nil.
    if (lua_pcall(m_L, 1, 2, 0) != LUA_OK) {
        m_error = lua_tostring(m_L, -1);
        LOG_WARN("M3DScene", "tick() error: %s", m_error.c_str());
        lua_pop(m_L, 1);
        return false;
    }

    const int optsIdx = lua_gettop(m_L);       // 2nd return (may be nil)
    const int listIdx = optsIdx - 1;           // 1st return

    // ── Camera (from the optional 2nd return's `camera` field) ──
    if (outCamera && lua_istable(m_L, optsIdx)) {
        lua_getfield(m_L, optsIdx, "camera");
        if (lua_istable(m_L, -1)) {
            int camIdx = lua_gettop(m_L);
            readVec3Field(m_L, camIdx, "pos",    outCamera->pos);
            readVec3Field(m_L, camIdx, "target", outCamera->target);
            outCamera->fov = readNumberField(m_L, camIdx, "fov", outCamera->fov);
            outCamera->explicitCam = true;
        }
        lua_pop(m_L, 1);
    }

    // ── Instances (from the 1st return) ──
    if (lua_isnil(m_L, listIdx)) {
        lua_pop(m_L, 2);
        m_error.clear();
        return true;   // nil → draw nothing
    }
    if (!lua_istable(m_L, listIdx)) {
        m_error = "tick() must return a table (or list of tables)";
        lua_pop(m_L, 2);
        return false;
    }

    const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(m_L, listIdx));
    if (n >= 1) {
        // List form — iterate the array part in order.
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(m_L, listIdx, i);
            if (lua_istable(m_L, -1)) {
                M3DInstance inst;
                readInstance(m_L, lua_gettop(m_L), inst);
                out.push_back(inst);
            }
            lua_pop(m_L, 1);
        }
    } else if (hasInstanceField(m_L, listIdx)) {
        // Single-instance shorthand (no array part but instance fields).
        M3DInstance inst;
        readInstance(m_L, listIdx, inst);
        out.push_back(inst);
    }
    // else: empty table → draw nothing.

    lua_pop(m_L, 2);   // list + opts
    m_error.clear();
    return true;
}

} // namespace visual
} // namespace yawn
