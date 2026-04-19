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

bool M3DSceneScript::tick(const Inputs& in,
                           std::vector<M3DTransform>& out) {
    out.clear();
    if (!m_L) return false;

    lua_getglobal(m_L, "tick");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        m_error = "script has no global function `tick`";
        return false;
    }

    pushContext(m_L, in);

    if (lua_pcall(m_L, 1, 1, 0) != LUA_OK) {
        m_error = lua_tostring(m_L, -1);
        LOG_WARN("M3DScene", "tick() error: %s", m_error.c_str());
        lua_pop(m_L, 1);
        return false;
    }

    // Allowed returns:
    //   (a) list of tables — drawn in order
    //   (b) single table (treated as one-element list)
    //   (c) nil / empty table — draw nothing
    if (lua_isnil(m_L, -1)) {
        lua_pop(m_L, 1);
        return true;
    }
    if (!lua_istable(m_L, -1)) {
        m_error = "tick() must return a table (or list of tables)";
        lua_pop(m_L, 1);
        return false;
    }

    // Probe: is this a single transform (has numeric/table fields at
    // keys 1..3 / position / scale / rotation) or a list of transforms?
    // Heuristic — if index [1] is itself a table, treat as list.
    lua_rawgeti(m_L, -1, 1);
    bool isList = lua_istable(m_L, -1);
    lua_pop(m_L, 1);

    auto readOne = [&](int idx) {
        M3DTransform xf;
        // Position (defaults 0).
        float v[3] = { 0, 0, 0 };
        if (readVec3Field(m_L, idx, "position", v)) {
            xf.position[0] = v[0]; xf.position[1] = v[1]; xf.position[2] = v[2];
        }
        // Rotation (euler XYZ degrees; defaults 0).
        v[0] = v[1] = v[2] = 0;
        if (readVec3Field(m_L, idx, "rotation", v)) {
            xf.rotationDeg[0] = v[0];
            xf.rotationDeg[1] = v[1];
            xf.rotationDeg[2] = v[2];
        }
        // Scale (uniform scalar). Default 1.
        xf.scale = readNumberField(m_L, idx, "scale", 1.0f);
        out.push_back(xf);
    };

    if (!isList) {
        readOne(lua_gettop(m_L));
    } else {
        int top = lua_gettop(m_L);
        lua_pushnil(m_L);
        while (lua_next(m_L, top) != 0) {
            if (lua_istable(m_L, -1)) {
                readOne(lua_gettop(m_L));
            }
            lua_pop(m_L, 1);
        }
    }
    lua_pop(m_L, 1);  // the top-level returned table

    m_error.clear();
    return true;
}

} // namespace visual
} // namespace yawn
