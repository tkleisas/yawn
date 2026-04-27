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

// ── Lua API: yawn.toast(message, [duration_sec=1.5], [severity=0]) ──────────
//
// Display a transient toast in the YAWN UI. Severity: 0=info, 1=warn, 2=error.
// Replaces any currently visible toast; does not queue.

static int l_toast(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const char* msg = luaL_checkstring(L, 1);
    double dur = luaL_optnumber(L, 2, 1.5);
    int sev = static_cast<int>(luaL_optinteger(L, 3, 0));
    mgr->showToast(msg ? msg : "", static_cast<float>(dur), sev);
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

// ── Lua API: yawn.set_selected_track(track) ─────────────────────────────────

static int l_set_selected_track(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    mgr->setSelectedTrack(t);
    return 0;
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

// ── Lua API: yawn.get_instrument_id(track) ──────────────────────────────────

static int l_get_instrument_id(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->audioEngine()) { lua_pushnil(L); return 1; }
    auto* inst = mgr->audioEngine()->instrument(t);
    if (!inst) { lua_pushnil(L); return 1; }
    lua_pushstring(L, inst->id());
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
    float minVal = 0.0f;

    if (d.inst) {
        val = d.inst->getParameter(pi);
        auto& info = d.inst->parameterInfo(pi);
        unit = info.unit; labels = info.valueLabels; labelCount = info.valueLabelCount;
        minVal = info.minValue;
    } else if (d.fx) {
        val = d.fx->getParameter(pi);
        auto& info = d.fx->parameterInfo(pi);
        unit = info.unit; labels = info.valueLabels; labelCount = info.valueLabelCount;
        minVal = info.minValue;
    } else if (d.mfx) {
        val = d.mfx->getParameter(pi);
        auto& info = d.mfx->parameterInfo(pi);
        unit = info.unit; labels = info.valueLabels; labelCount = info.valueLabelCount;
        minVal = info.minValue;
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

// ── Lua API: yawn.send_pitchbend_to_track(track, value14, channel) ──────────

static int l_send_pitchbend_to_track(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    int track = static_cast<int>(luaL_checkinteger(L, 1));
    int val14 = static_cast<int>(luaL_checkinteger(L, 2));  // 0-16383, center=8192
    int channel = static_cast<int>(luaL_optinteger(L, 3, 0));

    mgr->sendCommand(audio::SendMidiToTrackMsg{
        track,
        static_cast<uint8_t>(midi::MidiMessage::Type::PitchBend),
        static_cast<uint8_t>(channel),
        0,  // note unused
        0,  // velocity unused
        midi::Convert::pb14to32(static_cast<uint16_t>(val14)),
        0
    });

    return 0;
}

// ── Lua API: yawn.send_cc_to_track(track, cc, value, channel) ───────────────

static int l_send_cc_to_track(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    int track = static_cast<int>(luaL_checkinteger(L, 1));
    int cc = static_cast<int>(luaL_checkinteger(L, 2));
    int val = static_cast<int>(luaL_checkinteger(L, 3));
    int channel = static_cast<int>(luaL_optinteger(L, 4, 0));

    mgr->sendCommand(audio::SendMidiToTrackMsg{
        track,
        static_cast<uint8_t>(midi::MidiMessage::Type::ControlChange),
        static_cast<uint8_t>(channel),
        0,  // note unused
        0,  // velocity unused
        midi::Convert::cc7to32(static_cast<uint8_t>(val)),
        static_cast<uint16_t>(cc)
    });

    return 0;
}

// ── Lua API: yawn.get_device_param_label_count(type, chain_index, param_index)
// Returns number of value labels (>0 means discrete/stepped param)

static int l_get_device_param_label_count(lua_State* L) {
    auto* mgr = getManager(L);
    auto d = resolveDevice(mgr, L);
    int pi = static_cast<int>(luaL_checkinteger(L, 3));
    int count = deviceParamCount(d);

    if (pi < 0 || pi >= count) { lua_pushinteger(L, 0); return 1; }

    int lc = 0;
    if (d.inst) lc = d.inst->parameterInfo(pi).valueLabelCount;
    else if (d.fx) lc = d.fx->parameterInfo(pi).valueLabelCount;
    else if (d.mfx) lc = d.mfx->parameterInfo(pi).valueLabelCount;

    lua_pushinteger(L, lc);
    return 1;
}

// ── Lua API: yawn.get_bpm() ─────────────────────────────────────────────────

static int l_get_bpm(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushnumber(L, 120.0); return 1; }
    lua_pushnumber(L, mgr->audioEngine()->transport().bpm());
    return 1;
}

// ── Lua API: yawn.set_bpm(bpm) ──────────────────────────────────────────────

static int l_set_bpm(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    double bpm = luaL_checknumber(L, 1);
    if (bpm < 20.0) bpm = 20.0;
    if (bpm > 999.0) bpm = 999.0;
    mgr->sendCommand(audio::TransportSetBPMMsg{bpm});
    return 0;
}

// ── Lua API: yawn.is_playing() ──────────────────────────────────────────────

static int l_is_playing(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, mgr->audioEngine()->transport().isPlaying() ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.set_playing(bool) ─────────────────────────────────────────

static int l_set_playing(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    bool play = lua_toboolean(L, 1);
    if (play)
        mgr->sendCommand(audio::TransportPlayMsg{});
    else
        mgr->sendCommand(audio::TransportStopMsg{});
    return 0;
}

// ── Lua API: yawn.get_master_volume() ───────────────────────────────────────

static int l_get_master_volume(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushnumber(L, 0.8); return 1; }
    lua_pushnumber(L, static_cast<double>(mgr->audioEngine()->mixer().master().volume));
    return 1;
}

// ── Lua API: yawn.set_master_volume(vol) ────────────────────────────────────

static int l_set_master_volume(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) return 0;
    float vol = static_cast<float>(luaL_checknumber(L, 1));
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 2.0f) vol = 2.0f;
    mgr->sendCommand(audio::SetMasterVolumeMsg{vol});
    return 0;
}

// ── Lua API: yawn.get_metronome_enabled() ───────────────────────────────────

static int l_get_metronome_enabled(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, mgr->audioEngine()->metronome().enabled() ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.tap_tempo() ────────────────────────────────────────────────

static int l_tap_tempo(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    mgr->tapTempo();
    return 0;
}

// ── Lua API: yawn.set_metronome_enabled(bool) ──────────────────────────────

static int l_set_metronome_enabled(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    bool enabled = lua_toboolean(L, 1);
    mgr->sendCommand(audio::MetronomeToggleMsg{enabled});
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Session workflow API
// ═══════════════════════════════════════════════════════════════════════════

// ── Lua API: yawn.get_track_type(track) → "audio" | "midi" ────────────────

static int l_get_track_type(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->project() || t < 0 || t >= mgr->project()->numTracks()) {
        lua_pushnil(L);
        return 1;
    }
    auto type = mgr->project()->track(t).type;
    const char* s = (type == Track::Type::Audio)  ? "audio"
                  : (type == Track::Type::Midi)   ? "midi"
                                                   : "visual";
    lua_pushstring(L, s);
    return 1;
}

// ── Lua API: yawn.get_num_scenes() ─────────────────────────────────────────

static int l_get_num_scenes(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->project()) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, mgr->project()->numScenes());
    return 1;
}

// ── Lua API: yawn.is_track_armed(track) ────────────────────────────────────

static int l_is_track_armed(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->project() || t < 0 || t >= mgr->project()->numTracks()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, mgr->project()->track(t).armed ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.set_track_armed(track, armed) ───────────────────────────

static int l_set_track_armed(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    bool armed = lua_toboolean(L, 2);
    mgr->sendCommand(audio::SetTrackArmedMsg{t, armed});
    // Also update project state immediately for UI feedback
    if (mgr->project() && t >= 0 && t < mgr->project()->numTracks())
        mgr->project()->track(t).armed = armed;
    return 0;
}

// ── Lua API: yawn.is_recording() → transport-level recording ──────────────

static int l_is_recording(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, mgr->audioEngine()->transport().isRecording() ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.set_recording(armed, scene) ─────────────────────────────

static int l_set_recording(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    bool arm = lua_toboolean(L, 1);
    int scene = static_cast<int>(luaL_optinteger(L, 2, 0));
    mgr->sendCommand(audio::TransportRecordMsg{arm, scene});
    return 0;
}

// ── Lua API: yawn.get_clip_slot_state(track, scene) → table ──────────────
// Returns { type="empty"|"audio"|"midi", playing=bool, recording=bool, armed=bool }

static int l_get_clip_slot_state(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    int s = static_cast<int>(luaL_checkinteger(L, 2));
    if (!mgr) { lua_pushnil(L); return 1; }

    auto state = mgr->getClipSlotState(t, s);

    lua_newtable(L);
    const char* typeStr = "empty";
    if (state.type == 1) typeStr = "audio";
    else if (state.type == 2) typeStr = "midi";
    lua_pushstring(L, typeStr);
    lua_setfield(L, -2, "type");
    lua_pushboolean(L, state.playing ? 1 : 0);
    lua_setfield(L, -2, "playing");
    lua_pushboolean(L, state.recording ? 1 : 0);
    lua_setfield(L, -2, "recording");
    lua_pushboolean(L, state.armed ? 1 : 0);
    lua_setfield(L, -2, "armed");
    return 1;
}

// ── Lua API: yawn.launch_clip(track, scene) ───────────────────────────────

static int l_launch_clip(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    int s = static_cast<int>(luaL_checkinteger(L, 2));
    mgr->launchClip(t, s);
    return 0;
}

// ── Lua API: yawn.stop_clip(track) ────────────────────────────────────────

static int l_stop_clip(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    // Stop both audio and MIDI clips on this track
    mgr->sendCommand(audio::StopClipMsg{t});
    mgr->sendCommand(audio::StopMidiClipMsg{t});
    // Clear default scene
    if (mgr->project() && t >= 0 && t < mgr->project()->numTracks())
        mgr->project()->track(t).defaultScene = -1;
    return 0;
}

// ── Lua API: yawn.launch_scene(scene) ─────────────────────────────────────

static int l_launch_scene(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int s = static_cast<int>(luaL_checkinteger(L, 1));
    mgr->launchScene(s);
    return 0;
}

// ── Lua API: yawn.start_record(track, scene, overdub) ─────────────────────

static int l_start_record(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->project()) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    int s = static_cast<int>(luaL_checkinteger(L, 2));
    bool overdub = lua_toboolean(L, 3);
    if (t < 0 || t >= mgr->project()->numTracks()) return 0;

    auto* slot = mgr->project()->getSlot(t, s);
    const int rlb = slot ? slot->recordLengthBars : 0;
    auto trackType = mgr->project()->track(t).type;

    if (trackType == Track::Type::Midi)
        mgr->sendCommand(audio::StartMidiRecordMsg{t, s, overdub, rlb});
    else if (trackType == Track::Type::Audio)
        mgr->sendCommand(audio::StartAudioRecordMsg{t, s, overdub, rlb});
    // Visual tracks don't record.
    return 0;
}

// ── Lua API: yawn.stop_record(track) ──────────────────────────────────────

static int l_stop_record(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->project()) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (t < 0 || t >= mgr->project()->numTracks()) return 0;

    auto recQ = mgr->project()->track(t).recordQuantize;
    auto trackType = mgr->project()->track(t).type;

    if (trackType == Track::Type::Midi)
        mgr->sendCommand(audio::StopMidiRecordMsg{t, recQ});
    else if (trackType == Track::Type::Audio)
        mgr->sendCommand(audio::StopAudioRecordMsg{t, recQ});
    // Visual tracks don't record.
    return 0;
}

// ── Lua API: yawn.get_track_color(track) → int (color index) ──────────────

static int l_get_track_color(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->project() || t < 0 || t >= mgr->project()->numTracks()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, mgr->project()->track(t).colorIndex);
    return 1;
}

// ── Lua API: yawn.set_session_focus(track_offset, scene_offset, active) ───

static int l_set_session_focus(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int tOff = static_cast<int>(luaL_checkinteger(L, 1));
    int sOff = static_cast<int>(luaL_checkinteger(L, 2));
    bool active = lua_toboolean(L, 3);
    mgr->setSessionFocus(tOff, sOff, active);
    return 0;
}

// ── Lua API: yawn.get_record_length_bars(track, scene) → int (0=unlimited)
// Record-length is per-slot (each cell can target a different loop
// length). The API signature changed when we moved the field — scripts
// now pass both track and scene indices.

static int l_get_record_length_bars(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    int s = static_cast<int>(luaL_optinteger(L, 2, 0));
    if (!mgr || !mgr->project()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    auto* slot = mgr->project()->getSlot(t, s);
    lua_pushinteger(L, slot ? slot->recordLengthBars : 0);
    return 1;
}

// ── Lua API: yawn.get_track_mute(track) → bool ──────────────────────────

static int l_get_track_mute(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->project() || t < 0 || t >= mgr->project()->numTracks()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, mgr->project()->track(t).muted ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.set_track_mute(track, muted) ──────────────────────────

static int l_set_track_mute(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    bool muted = lua_toboolean(L, 2);
    mgr->sendCommand(audio::SetTrackMuteMsg{t, muted});
    if (mgr->project() && t >= 0 && t < mgr->project()->numTracks())
        mgr->project()->track(t).muted = muted;
    return 0;
}

// ── Lua API: yawn.set_track_solo(track, soloed) ────────────────────────

static int l_set_track_solo(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    bool soloed = lua_toboolean(L, 2);
    mgr->sendCommand(audio::SetTrackSoloMsg{t, soloed});
    return 0;
}

// ── Lua API: yawn.get_track_solo(track) → bool ─────────────────────────

// ── Lua API: yawn.get_loop() → bool ─────────────────────────────────────

static int l_get_loop(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, mgr->audioEngine()->transport().isLoopEnabled() ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.set_loop(enabled) ─────────────────────────────────────

static int l_set_loop(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) return 0;
    bool on = lua_toboolean(L, 1);
    mgr->audioEngine()->transport().setLoopEnabled(on);
    return 0;
}

// ── Lua API: yawn.set_track_volume(track, volume) ──────────────────────

static int l_set_track_volume(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    float vol = static_cast<float>(luaL_checknumber(L, 2));
    mgr->sendCommand(audio::SetTrackVolumeMsg{t, vol});
    return 0;
}

// ── Lua API: yawn.set_track_pan(track, pan) ────────────────────────────

static int l_set_track_pan(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    float pan = static_cast<float>(luaL_checknumber(L, 2));
    mgr->sendCommand(audio::SetTrackPanMsg{t, pan});
    return 0;
}

// ── Lua API: yawn.get_track_solo(track) → bool ─────────────────────────

static int l_get_track_solo(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->project() || t < 0 || t >= mgr->project()->numTracks()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, mgr->project()->track(t).soloed ? 1 : 0);
    return 1;
}

// ── Lua API: yawn.get_track_volume(track) → float ──────────────────────

static int l_get_track_volume(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->audioEngine() || t < 0 || t >= kMaxTracks) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, mgr->audioEngine()->mixer().trackChannel(t).volume);
    return 1;
}

static int l_get_track_pan(lua_State* L) {
    auto* mgr = getManager(L);
    int t = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mgr || !mgr->audioEngine() || t < 0 || t >= kMaxTracks) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, mgr->audioEngine()->mixer().trackChannel(t).pan);
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

    // Set package.path so require() finds sibling Lua modules
    auto lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        std::string dir = path.substr(0, lastSlash);
        // Convert backslashes to forward slashes (backslashes are Lua escape chars)
        for (auto& c : dir) {
            if (c == '\\') c = '/';
        }
        std::string code = "package.path = '" + dir + "/?.lua;' .. package.path";
        luaL_dostring(m_L, code.c_str());
    }

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
        {"toast",                   l_toast},
        {"midi_send",               l_midi_send},
        {"midi_send_sysex",         l_midi_send_sysex},
        {"get_selected_track",      l_get_selected_track},
        {"set_selected_track",      l_set_selected_track},
        {"get_track_count",         l_get_track_count},
        {"get_track_name",          l_get_track_name},
        {"get_instrument_name",     l_get_instrument_name},
        {"get_instrument_id",       l_get_instrument_id},
        {"get_device_param_count",  l_get_device_param_count},
        {"get_device_param_name",   l_get_device_param_name},
        {"get_device_param_value",  l_get_device_param_value},
        {"get_device_param_min",    l_get_device_param_min},
        {"get_device_param_max",    l_get_device_param_max},
        {"get_device_param_display",l_get_device_param_display},
        {"set_device_param",        l_set_device_param},
        {"get_device_param_label_count", l_get_device_param_label_count},
        {"send_note_to_track",      l_send_note_to_track},
        {"send_pitchbend_to_track", l_send_pitchbend_to_track},
        {"send_cc_to_track",        l_send_cc_to_track},
        {"get_bpm",                 l_get_bpm},
        {"set_bpm",                 l_set_bpm},
        {"is_playing",              l_is_playing},
        {"set_playing",             l_set_playing},
        {"get_master_volume",       l_get_master_volume},
        {"set_master_volume",       l_set_master_volume},
        {"get_metronome_enabled",   l_get_metronome_enabled},
        {"set_metronome_enabled",   l_set_metronome_enabled},
        {"tap_tempo",               l_tap_tempo},
        // Session workflow
        {"get_track_type",          l_get_track_type},
        {"get_num_scenes",          l_get_num_scenes},
        {"is_track_armed",          l_is_track_armed},
        {"set_track_armed",         l_set_track_armed},
        {"is_recording",            l_is_recording},
        {"set_recording",           l_set_recording},
        {"get_clip_slot_state",     l_get_clip_slot_state},
        {"launch_clip",             l_launch_clip},
        {"stop_clip",               l_stop_clip},
        {"launch_scene",            l_launch_scene},
        {"start_record",            l_start_record},
        {"stop_record",             l_stop_record},
        {"get_track_color",         l_get_track_color},
        {"get_record_length_bars",  l_get_record_length_bars},
        {"set_session_focus",       l_set_session_focus},
        {"get_track_mute",          l_get_track_mute},
        {"set_track_mute",          l_set_track_mute},
        {"set_track_solo",          l_set_track_solo},
        {"get_track_solo",          l_get_track_solo},
        {"get_loop",                l_get_loop},
        {"set_loop",                l_set_loop},
        {"set_track_volume",        l_set_track_volume},
        {"set_track_pan",           l_set_track_pan},
        {"get_track_volume",        l_get_track_volume},
        {"get_track_pan",           l_get_track_pan},
        {nullptr, nullptr}
    };

    luaL_setfuncs(m_L, funcs, 0);

    // Set as global "yawn"
    lua_setglobal(m_L, "yawn");
}

} // namespace controllers
} // namespace yawn
