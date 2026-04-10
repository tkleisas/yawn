#include "controllers/LuaEngine.h"
#include "controllers/ControllerManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"
#include "instruments/Instrument.h"
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "midi/MidiTypes.h"
#include "midi/MidiEffect.h"
#include "midi/MidiEffectChain.h"
#include "util/Logger.h"
#include "util/MessageQueue.h"

#include <cstring>
#include <cstdio>

namespace yawn {
namespace controllers {

// Registry key for storing the ControllerManager pointer in Lua
static const char* kRegistryKey = "yawn_controller_manager";

// Helper: retrieve ControllerManager* from Lua registry
static ControllerManager* getManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRegistryKey);
    auto* mgr = static_cast<ControllerManager*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return mgr;
}

// ── Lua API: yawn.log(message) ──────────────────────────────────────────────

static int l_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    LOG_INFO("Lua", "%s", msg);
    return 0;
}

// ── Lua API: yawn.midi_send(b1, b2, ...) ────────────────────────────────────

static int l_midi_send(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    int n = lua_gettop(L);
    if (n <= 0) return 0;

    uint8_t buf[256];
    int len = (n > 256) ? 256 : n;
    for (int i = 0; i < len; ++i)
        buf[i] = static_cast<uint8_t>(luaL_checkinteger(L, i + 1));

    mgr->sendMidiToController(buf, len);
    return 0;
}

// ── Lua API: yawn.midi_send_sysex(table) ────────────────────────────────────

static int l_midi_send_sysex(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    luaL_checktype(L, 1, LUA_TTABLE);
    int len = static_cast<int>(luaL_len(L, 1));
    if (len <= 0 || len > 1024) return 0;

    uint8_t buf[1024];
    for (int i = 0; i < len; ++i) {
        lua_rawgeti(L, 1, i + 1);
        buf[i] = static_cast<uint8_t>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }

    mgr->sendMidiToController(buf, len);
    return 0;
}

// ── Lua API: yawn.get_selected_track() ──────────────────────────────────────

static int l_get_selected_track(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, mgr->selectedTrack());
    return 1;
}

// ── Lua API: yawn.get_track_count() ─────────────────────────────────────────

static int l_get_track_count(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->project()) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, mgr->project()->numTracks());
    return 1;
}

// ── Lua API: yawn.get_track_name(track) ─────────────────────────────────────

static int l_get_track_name(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->project() || t < 0 || t >= mgr->project()->numTracks()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, mgr->project()->track(t).name.c_str());
    return 1;
}

// ── Lua API: yawn.get_instrument_name(track) ────────────────────────────────

static int l_get_instrument_name(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->audioEngine()) { lua_pushnil(L); return 1; }
    auto* inst = mgr->audioEngine()->instrument(t);
    if (!inst) { lua_pushnil(L); return 1; }
    lua_pushstring(L, inst->name());
    return 1;
}

// ── Device parameter helpers ────────────────────────────────────────────────

// Resolve device_type string + chain_index + track to a param-accessible device.
// Returns pointers: at most one will be non-null.
struct DevicePtrs {
    instruments::Instrument* inst = nullptr;
    effects::AudioEffect* fx = nullptr;
    midi::MidiEffect* mfx = nullptr;
};

static DevicePtrs resolveDevice(ControllerManager* mgr, lua_State* L) {
    DevicePtrs d;
    if (!mgr || !mgr->audioEngine()) return d;

    const char* type = luaL_checkstring(L, 1);
    int ci = static_cast<int>(luaL_checkinteger(L, 2));
    int track = mgr->selectedTrack();

    if (std::strcmp(type, "instrument") == 0) {
        d.inst = mgr->audioEngine()->instrument(track);
    } else if (std::strcmp(type, "audio_effect") == 0) {
        d.fx = mgr->audioEngine()->mixer().trackEffects(track).effectAt(ci);
    } else if (std::strcmp(type, "midi_effect") == 0) {
        d.mfx = mgr->audioEngine()->midiEffectChain(track).effect(ci);
    }
    return d;
}

static int deviceParamCount(const DevicePtrs& d) {
    if (d.inst) return d.inst->parameterCount();
    if (d.fx)   return d.fx->parameterCount();
    if (d.mfx)  return d.mfx->parameterCount();
    return 0;
}

// ── Lua API: yawn.get_device_param_count(type, chain_index) ─────────────────

static int l_get_device_param_count(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    lua_pushinteger(L, deviceParamCount(d));
    return 1;
}

// ── Lua API: yawn.get_device_param_name(type, chain_index, param_index) ─────

static int l_get_device_param_name(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    int count = deviceParamCount(d);

    if (pi < 0 || pi >= count) { lua_pushnil(L); return 1; }

    const char* name = nullptr;
    if (d.inst) name = d.inst->parameterInfo(pi).name;
    else if (d.fx) name = d.fx->parameterInfo(pi).name;
    else if (d.mfx) name = d.mfx->parameterInfo(pi).name;

    lua_pushstring(L, name ? name : "");
    return 1;
}

// ── Lua API: yawn.get_device_param_value(type, chain_index, param_index) ────

static int l_get_device_param_value(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    int count = deviceParamCount(d);

    if (pi < 0 || pi >= count) { lua_pushnumber(L, 0.0); return 1; }

    float val = 0.0f;
    if (d.inst) val = d.inst->getParameter(pi);
    else if (d.fx) val = d.fx->getParameter(pi);
    else if (d.mfx) val = d.mfx->getParameter(pi);

    lua_pushnumber(L, static_cast<double>(val));
    return 1;
}

// ── Lua API: yawn.get_device_param_min(type, chain_index, param_index) ──────

static int l_get_device_param_min(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    int count = deviceParamCount(d);

    if (pi < 0 || pi >= count) { lua_pushnumber(L, 0.0); return 1; }

    float val = 0.0f;
    if (d.inst) val = d.inst->parameterInfo(pi).minValue;
    else if (d.fx) val = d.fx->parameterInfo(pi).minValue;
    else if (d.mfx) val = d.mfx->parameterInfo(pi).minValue;

    lua_pushnumber(L, static_cast<double>(val));
    return 1;
}

// ── Lua API: yawn.get_device_param_max(type, chain_index, param_index) ──────

static int l_get_device_param_max(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    int count = deviceParamCount(d);

    if (pi < 0 || pi >= count) { lua_pushnumber(L, 1.0); return 1; }

    float val = 1.0f;
    if (d.inst) val = d.inst->parameterInfo(pi).maxValue;
    else if (d.fx) val = d.fx->parameterInfo(pi).maxValue;
    else if (d.mfx) val = d.mfx->parameterInfo(pi).maxValue;

    lua_pushnumber(L, static_cast<double>(val));
    return 1;
}

// ── Lua API: yawn.get_device_param_display(type, chain_index, param_index) ──

static int l_get_device_param_display(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    int count = deviceParamCount(d);

    if (pi < 0 || pi >= count) { lua_pushstring(L, ""); return 1; }

    float val = 0.0f;
    const char* unit = "";
    const char* const* labels = nullptr;
    int labelCount = 0;
    float minVal = 0.0f, maxVal = 1.0f;

    if (d.inst) {
        val = d.inst->getParameter(pi);
        auto& info = d.inst->parameterInfo(pi);
        unit = info.unit; labels = info.valueLabels; labelCount = info.valueLabelCount;
        minVal = info.minValue; maxVal = info.maxValue;
    } else if (d.fx) {
        val = d.fx->getParameter(pi);
        auto& info = d.fx->parameterInfo(pi);
        unit = info.unit; labels = info.valueLabels; labelCount = info.valueLabelCount;
        minVal = info.minValue; maxVal = info.maxValue;
    } else if (d.mfx) {
        val = d.mfx->getParameter(pi);
        auto& info = d.mfx->parameterInfo(pi);
        unit = info.unit; labels = info.valueLabels; labelCount = info.valueLabelCount;
        minVal = info.minValue; maxVal = info.maxValue;
    }

    // If value labels are available, use the corresponding label
    if (labels && labelCount > 0) {
        int idx = static_cast<int>(val - minVal + 0.5f);
        if (idx >= 0 && idx < labelCount) {
            lua_pushstring(L, labels[idx]);
            return 1;
        }
    }

    // Format as number + unit
    char buf[64];
    if (unit && unit[0] != '\0')
        std::snprintf(buf, sizeof(buf), "%.2f %s", val, unit);
    else
        std::snprintf(buf, sizeof(buf), "%.2f", val);

    lua_pushstring(L, buf);
    return 1;
}

// ── Lua API: yawn.set_device_param(type, chain_index, param_index, value) ───

static int l_set_device_param(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    // Resolve device and set directly (same pattern as DetailPanelWidget::DeviceRef::setParam)
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    float val = static_cast<float>(luaL_checknumber(L, 4));
    int count = deviceParamCount(d);

    if (pi >= 0 && pi < count) {
        if (d.inst)      d.inst->setParameter(pi, val);
        else if (d.fx)   d.fx->setParameter(pi, val);
        else if (d.mfx)  d.mfx->setParameter(pi, val);
    }

    return 0;
}

// ── Lua API: yawn.send_note_to_track(track, note, velocity, channel) ────────

static int l_send_note_to_track(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    int track = static_cast<int>(luaL_checkinteger(L, 1));
    int note = static_cast<int>(luaL_checkinteger(L, 2));
    int velocity = static_cast<int>(luaL_optinteger(L, 3, 127));
    int channel = static_cast<int>(luaL_optinteger(L, 4, 0));

    uint8_t type = (velocity > 0)
        ? static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn)
        : static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff);

    mgr->sendCommand(audio::SendMidiToTrackMsg{
        track, type, static_cast<uint8_t>(channel),
        static_cast<uint8_t>(note),
        midi::Convert::vel7to16(static_cast<uint8_t>(velocity)),
        0, 0
    });

    return 0;
}

// ── Lua API: yawn.get_bpm() ─────────────────────────────────────────────────

static int l_get_bpm(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushnumber(L, 120.0); return 1; }
    lua_pushnumber(L, mgr->audioEngine()->transport().bpm());
    return 1;
}

// ── Lua API: yawn.is_playing() ──────────────────────────────────────────────

static int l_is_playing(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, mgr->audioEngine()->transport().isPlaying() ? 1 : 0);
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════
// LuaEngine implementation
// ═══════════════════════════════════════════════════════════════════════════

bool LuaEngine::init(ControllerManager* mgr) {
    m_manager = mgr;

    m_L = luaL_newstate();
    if (!m_L) {
        LOG_ERROR("Lua", "Failed to create Lua state");
        return false;
    }

    luaL_openlibs(m_L);
    registerAPI();

    LOG_INFO("Lua", "Lua %s engine initialized", LUA_VERSION);
    return true;
}

void LuaEngine::shutdown() {
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
    m_manager = nullptr;
}

bool LuaEngine::loadFile(const std::string& path) {
    if (!m_L) return false;

    if (luaL_loadfile(m_L, path.c_str()) != LUA_OK) {
        LOG_ERROR("Lua", "Failed to load %s: %s", path.c_str(), lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }

    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
        LOG_ERROR("Lua", "Error running %s: %s", path.c_str(), lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }

    LOG_INFO("Lua", "Loaded script: %s", path.c_str());
    return true;
}

bool LuaEngine::callOnConnect() { return callGlobal("on_connect"); }
bool LuaEngine::callOnDisconnect() { return callGlobal("on_disconnect"); }
bool LuaEngine::callOnTick() { return callGlobal("on_tick"); }

bool LuaEngine::callOnMidi(const uint8_t* data, int length) {
    if (!m_L) return false;

    lua_getglobal(m_L, "on_midi");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return false;
    }

    // Pass MIDI data as a Lua table of integers
    lua_createtable(m_L, length, 0);
    for (int i = 0; i < length; ++i) {
        lua_pushinteger(m_L, data[i]);
        lua_rawseti(m_L, -2, i + 1);
    }

    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK) {
        LOG_ERROR("Lua", "on_midi error: %s", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }

    return true;
}

bool LuaEngine::callGlobal(const char* name) {
    if (!m_L) return false;

    lua_getglobal(m_L, name);
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return false;  // function not defined, that's OK
    }

    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
        LOG_ERROR("Lua", "%s error: %s", name, lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }

    return true;
}

void LuaEngine::registerAPI() {
    if (!m_L) return;

    // Store ControllerManager* in registry
    lua_pushlightuserdata(m_L, m_manager);
    lua_setfield(m_L, LUA_REGISTRYINDEX, kRegistryKey);

    // Create yawn table
    lua_newtable(m_L);

    // Register functions
    static const luaL_Reg funcs[] = {
        {"log",                     l_log},
        {"midi_send",               l_midi_send},
        {"midi_send_sysex",         l_midi_send_sysex},
        {"get_selected_track",      l_get_selected_track},
        {"get_track_count",         l_get_track_count},
        {"get_track_name",          l_get_track_name},
        {"get_instrument_name",     l_get_instrument_name},
        {"get_device_param_count",  l_get_device_param_count},
        {"get_device_param_name",   l_get_device_param_name},
        {"get_device_param_value",  l_get_device_param_value},
        {"get_device_param_min",    l_get_device_param_min},
        {"get_device_param_max",    l_get_device_param_max},
        {"get_device_param_display",l_get_device_param_display},
        {"set_device_param",        l_set_device_param},
        {"send_note_to_track",      l_send_note_to_track},
        {"get_bpm",                 l_get_bpm},
        {"is_playing",              l_is_playing},
        {nullptr, nullptr}
    };

    luaL_setfuncs(m_L, funcs, 0);

    // Set as global "yawn"
    lua_setglobal(m_L, "yawn");
}

} // namespace controllers
} // namespace yawn
