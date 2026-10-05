#include "livecode/LiveCodeEngine.h"
#include "livecode/LiveCodeBuffers.h"
#include "livecode/LiveCodeManager.h"
#include "audio/AudioEngine.h"
#include "util/Factory.h"
#include "util/Logger.h"

#include <algorithm>
#include <cstring>

namespace yawn {
namespace livecode {

// Budget defaults (§3.1). Whole-file runs get a generous budget; scheduled
// callback dispatches get a tight one so a runaway callback can't stall the
// UI loop for more than a couple of milliseconds.
// Whole-file runs get a generous budget: sample-forging fill callbacks
// (yawn.new_buffer) execute hundreds of thousands of Lua calls per
// buffer — the demo pad alone is ~150M instructions. Dispatches (§3)
// stay tight so a runaway callback can't stall the UI for more than a
// couple of milliseconds.
static constexpr int kBudgetRun      = 400'000'000;
static constexpr int kBudgetDispatch = 250'000;

static const char* kRegistryKey = "yawn_livecode_manager";

static LiveCodeManager* getManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRegistryKey);
    auto* mgr = static_cast<LiveCodeManager*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return mgr;
}

static void countHook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "live code instruction budget exceeded");
}

// ── improv.* scheduling API ──────────────────────────────────────────────

// improv.every(intervalBeats, fn, [repetitions]) -> handle
static int l_improv_every(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    double interval = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int reps = static_cast<int>(luaL_optinteger(L, 3, 0));
    lua_pushvalue(L, 2);                       // fn on top for luaL_ref
    uint64_t id = mgr->scheduleEvery(interval, reps);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// improv.after(deltaBeats, fn) -> handle
static int l_improv_after(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    double delta = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    uint64_t id = mgr->scheduleAfterBeats(delta);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// improv.at(beat, fn) or improv.at(bar, subBeat, fn) -> handle
static int l_improv_at(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    double beat;
    int fnIdx;
    if (lua_gettop(L) >= 3) {
        double bar = luaL_checknumber(L, 1);   // 1-based bar number
        double sub = luaL_checknumber(L, 2);
        luaL_checktype(L, 3, LUA_TFUNCTION);
        fnIdx = 3;
        int bpb = 4;
        if (mgr->audioEngine()) bpb = std::max(mgr->audioEngine()->transport().beatsPerBar(), 1);
        beat = (bar - 1.0) * bpb + sub;
    } else {
        beat = luaL_checknumber(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        fnIdx = 2;
    }
    lua_pushvalue(L, fnIdx);
    uint64_t id = mgr->scheduleAtBeat(beat);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// improv.on_bar(fn) -> handle
static int l_improv_on_bar(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    luaL_checktype(L, 1, LUA_TFUNCTION);
    uint64_t id = mgr->scheduleOnBar();
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// improv.clear(handle)
static int l_improv_clear(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    mgr->cancelEntry(id);
    return 0;
}

// improv.clear_all() — drop the current generation's entries.
static int l_improv_clear_all(lua_State* L) {
    auto* mgr = getManager(L);
    if (mgr) mgr->clearCurrentGeneration();
    return 0;
}

// improv.lookahead(seconds) — schedule-ahead horizon (default 0.10 s).
static int l_improv_lookahead(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    double sec = luaL_checknumber(L, 1);
    mgr->setLookahead(sec);
    return 0;
}

// improv.late_policy("play"|"drop") — what happens to notes whose
// scheduled beat was missed (UI hitch longer than the lookahead).
static int l_improv_late_policy(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const char* pol = luaL_checkstring(L, 1);
    mgr->setLatePolicy(std::strcmp(pol, "drop") == 0);
    return 0;
}

// ── yawn.set_notes — MIDI clip authoring (phase 4) ───────────────────────

// yawn.set_notes(track, scene, notes, [opts])
//   notes: array of {start, dur, pitch, vel(0..1), [ch]} (positional or named)
//   opts:  { beats = <length> }  (default: existing clip's length, else 4)
// Replaces the slot's MIDI clip atomically (App::setMidiClipLive path).
static int l_set_notes(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));   // Lua 1-based
    luaL_checktype(L, 3, LUA_TTABLE);

    double beats = 0.0;
    if (lua_gettop(L) >= 4 && lua_istable(L, 4)) {
        lua_getfield(L, 4, "beats");
        if (lua_isnumber(L, -1)) beats = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    auto* eng = mgr->audioEngine();
    auto* proj = mgr->project();
    if (!eng || !proj || track < 0 || track >= proj->numTracks()) return 0;
    const int sceneIdx = scene1 - 1;
    if (sceneIdx < 0 || sceneIdx >= proj->numScenes()) return 0;

    // Default length: existing clip's, else 4.
    if (beats <= 0.0) {
        auto* slot = proj->getSlot(track, sceneIdx);
        beats = (slot && slot->midiClip) ? slot->midiClip->lengthBeats() : 4.0;
    }

    std::vector<yawn::livecode::SongNote> notes;
    if (!yawn::livecode::parseNotesInto(L, 3, notes))
        return 0;

    auto clip = std::make_unique<midi::MidiClip>(std::max(beats, 0.25));
    for (const auto& n : notes) {
        midi::MidiNote note;
        note.startBeat = n.start;
        note.duration = std::max(n.dur, 0.01);
        note.pitch = static_cast<uint8_t>(std::clamp(n.pitch, 0.0, 127.0));
        note.channel = static_cast<uint8_t>(std::clamp(n.ch, 0, 15));
        note.velocity = static_cast<uint16_t>(
            std::clamp(n.vel, 0.0, 1.0) * 65535.0);
        clip->addNote(note);
    }
    if (mgr->setMidiClipLive(track, sceneIdx, std::move(clip)))
        mgr->pushConsole(0, "notes: " + std::to_string(notes.size()) +
                            " note(s) @ track " + std::to_string(track));
    return 0;
}

// yawn.get_notes(track, scene) -> array of {start, dur, pitch, vel, ch}
static int l_get_notes(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));
    auto* proj = mgr->project();
    if (!proj) { lua_pushnil(L); return 1; }
    const int sceneIdx = scene1 - 1;
    auto* slot = proj->getSlot(track, sceneIdx);
    if (!slot || !slot->midiClip) { lua_pushnil(L); return 1; }

    lua_createtable(L, slot->midiClip->noteCount(), 0);
    for (int i = 0; i < slot->midiClip->noteCount(); ++i) {
        const auto& n = slot->midiClip->note(i);
        lua_createtable(L, 0, 5);
        lua_pushnumber(L, n.startBeat);   lua_setfield(L, -2, "start");
        lua_pushnumber(L, n.duration);    lua_setfield(L, -2, "dur");
        lua_pushnumber(L, n.pitch);       lua_setfield(L, -2, "pitch");
        lua_pushnumber(L, n.velocity / 65535.0); lua_setfield(L, -2, "vel");
        lua_pushnumber(L, n.channel);     lua_setfield(L, -2, "ch");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// yawn.clear_notes(track, scene) — empty the clip, keep its length.
static int l_clear_notes(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));
    auto* eng = mgr->audioEngine();
    auto* proj = mgr->project();
    if (!eng || !proj || track < 0 || track >= proj->numTracks()) return 0;
    const int sceneIdx = scene1 - 1;
    auto* slot = proj->getSlot(track, sceneIdx);
    if (!slot) return 0;
    const double beats = slot->midiClip ? slot->midiClip->lengthBeats() : 4.0;
    mgr->setMidiClipLive(track, sceneIdx,
                         std::make_unique<midi::MidiClip>(beats));
    return 0;
}

// yawn.new_midi_clip(track, scene, [beats=4])
static int l_new_midi_clip(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));
    double beats = luaL_optnumber(L, 3, 4.0);
    auto* proj = mgr->project();
    if (!proj || track < 0 || track >= proj->numTracks()) return 0;
    const int sceneIdx = scene1 - 1;
    if (sceneIdx < 0 || sceneIdx >= proj->numScenes()) return 0;
    if (mgr->setMidiClipLive(track, sceneIdx,
                             std::make_unique<midi::MidiClip>(std::max(beats, 0.25))))
        mgr->pushConsole(0, "notes: new empty clip @ track " +
                            std::to_string(track));
    return 0;
}

// ── yawn.load_audio_file / save_audio_buffer / load_sample (phase 4) ─────

// yawn.load_audio_file(path) -> handle (>0) or 0. Buffer is resampled to
// the engine rate on load (same rule as file import).
static int l_load_audio_file(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) { lua_pushinteger(L, 0); return 1; }
    const char* path = luaL_checkstring(L, 1);
    const uint64_t h = mgr->loadAudioFileHandle(path ? path : "");
    lua_pushinteger(L, static_cast<lua_Integer>(h));
    return 1;
}

// yawn.save_audio_buffer(handle, path, [opts { format = "wav"|"flac"|"ogg",
//                                           depth = "f32"|"i16"|"i24" }])
static int l_save_audio_buffer(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    uint64_t handle = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    const char* path = luaL_checkstring(L, 2);
    std::string format, depth;
    if (lua_gettop(L) >= 3 && lua_istable(L, 3)) {
        lua_getfield(L, 3, "format");
        if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) format = s; }
        lua_pop(L, 1);
        lua_getfield(L, 3, "depth");
        if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) depth = s; }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, mgr->saveAudioBufferHandle(handle, path ? path : "",
                                                  format, depth) ? 1 : 0);
    return 1;
}

// yawn.load_sample(track, handle, [kind = "sampler"|"granular"|"drumslop"|"vocoder"])
static int l_load_sample(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) { lua_pushboolean(L, 0); return 1; }
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    uint64_t handle = static_cast<uint64_t>(luaL_checkinteger(L, 2));
    const char* kind = luaL_optstring(L, 3, "");
    // Deferred: the song's instruments often only exist after the body
    // ran (declarative songs are harvested post-body) — queue the
    // delivery; failures surface through the console on flush.
    mgr->requestSampleLoad(track, handle, kind ? kind : "");
    lua_pushboolean(L, 1);
    return 1;
}

// yawn.set_clip(track, scene, handle, [name]) — place a forged/loaded
// buffer into a session clip slot (deferred like load_sample; the App
// hook mirrors the deliverClip path).
static int l_set_clip(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));
    uint64_t handle = static_cast<uint64_t>(luaL_checkinteger(L, 3));
    const char* name = luaL_optstring(L, 4, "forged");
    mgr->requestSetClip(track, scene1, handle, name ? name : "forged");
    return 0;
}

// ── yawn.render — offline prerender (phase 3, §4) ────────────────────────

// Spec table:
//   yawn.render{ device="granular", params={...}, fx={...}, notes={...},
//                beats=4, tail=1, sr=48000, target={...} }
// Notes: array of {beat, dur, pitch, vel(0..1), [ch]}.
// Params/FX params by name or index — names resolved HERE (enqueue time,
// UI thread) via a factory prototype so the worker spec stays integer.
// Target: {kind="clip", track=n, scene=n} | {kind="sampler"|"granular",
// track=n} | {kind="file", path="..."}. Returns job id (integer, >0).
static audio::PrerenderSpec::Fx parseRenderFx(
        LiveCodeManager* mgr, lua_State* L, int idx,
        std::vector<std::string>& warnings) {
    audio::PrerenderSpec::Fx out;
    lua_getfield(L, idx, "id");
    if (lua_isstring(L, -1)) {
        const char* s = lua_tostring(L, -1);
        if (s) out.id = s;
    }
    lua_pop(L, 1);

    lua_getfield(L, idx, "params");
    if (lua_istable(L, -1)) {
        // Prototype the device to resolve names → indices.
        std::unique_ptr<effects::AudioEffect> proto;
        if (!out.id.empty()) proto = createAudioEffect(out.id);
        if (!proto && !out.id.empty())
            warnings.push_back("render: unknown effect id '" + out.id + "'");
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            const float val = static_cast<float>(lua_tonumber(L, -1));
            if (lua_isinteger(L, -2)) {
                out.params.emplace_back(static_cast<int>(lua_tointeger(L, -2)), val);
            } else if (lua_isstring(L, -2) && proto) {
                const char* name = lua_tostring(L, -2);
                int found = -1;
                for (int i = 0; i < proto->parameterCount(); ++i)
                    if (name && mgr->nameEquals(proto->parameterInfo(i).name, name))
                    { found = i; break; }
                if (found >= 0) out.params.emplace_back(found, val);
                else warnings.push_back(std::string("render: fx '") + out.id +
                                        "' has no param '" + (name ? name : "?") + "'");
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return out;
}

static int l_render(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->prerenderManager()) { lua_pushinteger(L, 0); return 1; }
    luaL_checktype(L, 1, LUA_TTABLE);
    const int specIdx = lua_gettop(L);

    audio::PrerenderSpec spec;
    std::vector<std::string> warnings;

    lua_getfield(L, specIdx, "device");
    if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) spec.device = s; }
    lua_pop(L, 1);

    // Params — resolved against a fresh prototype of the instrument.
    std::unique_ptr<instruments::Instrument> protoInst;
    if (!spec.device.empty()) protoInst = createInstrument(spec.device);
    if (!protoInst && !spec.device.empty())
        warnings.push_back("render: unknown device id '" + spec.device + "'");
    lua_getfield(L, specIdx, "params");
    if (lua_istable(L, -1) && protoInst) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            const float val = static_cast<float>(lua_tonumber(L, -1));
            if (lua_isinteger(L, -2)) {
                spec.params.emplace_back(static_cast<int>(lua_tointeger(L, -2)), val);
            } else if (lua_isstring(L, -2)) {
                const char* name = lua_tostring(L, -2);
                int found = -1;
                for (int i = 0; i < protoInst->parameterCount(); ++i)
                    if (name && mgr->nameEquals(protoInst->parameterInfo(i).name, name))
                    { found = i; break; }
                if (found >= 0) spec.params.emplace_back(found, val);
                else warnings.push_back(std::string("render: device '") +
                                        spec.device + "' has no param '" +
                                        (name ? name : "?") + "'");
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, specIdx, "fx");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            spec.fx.push_back(parseRenderFx(mgr, L, lua_gettop(L), warnings));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, specIdx, "notes");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_istable(L, -1)) {
                audio::PrerenderNote n;
                lua_rawgeti(L, -1, 1); n.beat = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_rawgeti(L, -1, 2); n.durBeats = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_rawgeti(L, -1, 3); n.pitch = static_cast<int>(lua_tonumber(L, -1)); lua_pop(L, 1);
                lua_rawgeti(L, -1, 4);
                n.vel = lua_isnil(L, -1) ? 0.8f : static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
                lua_rawgeti(L, -1, 5);
                n.channel = lua_isnil(L, -1) ? 0 : static_cast<int>(lua_tonumber(L, -1));
                lua_pop(L, 1);
                spec.notes.push_back(n);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, specIdx, "beats");
    if (lua_isnumber(L, -1)) spec.lengthBeats = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, specIdx, "tail");
    if (lua_isnumber(L, -1)) spec.tailBeats = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, specIdx, "sr");
    if (lua_isnumber(L, -1)) spec.sampleRate = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    if (mgr->audioEngine())
        spec.sampleRate = static_cast<int>(mgr->audioEngine()->sampleRate());
    // Seed: accept "seed" for reproducible C++ generative sources.
    lua_getfield(L, specIdx, "seed");
    if (lua_isnumber(L, -1)) spec.seed = static_cast<uint64_t>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    // Target
    PrerenderManager::Target target;
    lua_getfield(L, specIdx, "target");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "kind");
        if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) target.kind = s; }
        lua_pop(L, 1);
        lua_getfield(L, -1, "track");
        if (lua_isnumber(L, -1)) target.track = static_cast<int>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, -1, "scene");
        if (lua_isnumber(L, -1)) target.scene = static_cast<int>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, -1, "pad");
        if (lua_isnumber(L, -1)) target.pad = static_cast<int>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, -1, "path");
        if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) target.path = s; }
        lua_pop(L, 1);
        lua_getfield(L, -1, "name");
        if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) target.name = s; }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    for (const auto& w : warnings) mgr->pushConsole(1, w);
    const uint64_t id = mgr->prerender(std::move(spec), std::move(target));
    if (id == 0) {
        mgr->pushConsole(2, "render: prerender system not running");
        lua_pushinteger(L, 0);
        return 1;
    }
    mgr->pushConsole(0, "render: job " + std::to_string(id) +
                        " queued (" + spec.device + ")");
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// Defer the launch to after the song apply (the script body runs before
// the harvest, so an immediate launch would target the stale state).
// opts (optional): { quantize = "none"|"beat"|"bar", from_start = bool }.
// Defaults resolve at fire time: quantize none when the transport is
// stopped (self-start in lockstep), bar when playing; from_start true
// when stopped (seek to beat 0 first).
static int l_launch_scene(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 1));
    std::string quantize;
    bool fromStart = true;
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        lua_getfield(L, 2, "quantize");
        if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) quantize = s; }
        lua_pop(L, 1);
        lua_getfield(L, 2, "from_start");
        if (lua_isboolean(L, -1)) fromStart = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    mgr->requestLaunchSceneOpts(scene1, quantize, fromStart);
    return 0;
}

// Immediate single-slot launch (token engine-state; used once the song
// state is stable — e.g. from improv callbacks). Same opts as above;
// defaults: quantize from the slot's launchQuantize (session semantics).
static int l_launch_clip(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->project()) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));
    const int scene = scene1 - 1;
    audio::QuantizeMode q{};
    bool haveQ = false;
    if (lua_gettop(L) >= 3 && lua_istable(L, 3)) {
        lua_getfield(L, 3, "quantize");
        if (lua_isstring(L, -1)) {
            const char* s = lua_tostring(L, -1);
            if (s) {
                haveQ = true;
                if (std::strcmp(s, "none") == 0)      q = audio::QuantizeMode::None;
                else if (std::strcmp(s, "beat") == 0) q = audio::QuantizeMode::NextBeat;
                else if (std::strcmp(s, "bar") == 0)  q = audio::QuantizeMode::NextBar;
                else haveQ = false;
            }
        }
        lua_pop(L, 1);
    }
    auto* slot = mgr->project()->getSlot(track, scene);
    if (!slot) return 0;
    if (slot->midiClip) {
        mgr->pushCommand(audio::LaunchMidiClipMsg{
            track, scene, slot->midiClip.get(),
            haveQ ? q : slot->launchQuantize,
            &slot->clipAutomation->lanes, slot->followAction});
    } else if (slot->audioClip) {
        mgr->pushCommand(audio::LaunchClipMsg{
            track, scene, slot->audioClip.get(),
            haveQ ? q : slot->launchQuantize,
            &slot->clipAutomation->lanes, slot->followAction});
    }
    return 0;
}

// yawn.set_visual(track, scene, shaderPath, [name]) — place a shader in
// a session slot as a visual clip (deferred like set_clip).
static int l_set_visual(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const int track = static_cast<int>(luaL_checkinteger(L, 1));
    const int scene1 = static_cast<int>(luaL_checkinteger(L, 2));
    const char* path = luaL_checkstring(L, 3);
    const char* name = luaL_optstring(L, 4, "");
    mgr->requestSetVisual(track, scene1, path ? path : "", name ? name : "");
    return 0;
}

static int l_cancel_render(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    mgr->cancelPrerender(id);
    return 0;
}

// ── yawn.* performer API subset ─────────────────────────────────────────

static int l_log(lua_State* L) {
    auto* mgr = getManager(L);
    const char* msg = luaL_checkstring(L, 1);
    if (mgr) mgr->pushConsole(0, msg ? msg : "");
    return 0;
}

static int l_toast(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    const char* msg = luaL_checkstring(L, 1);
    double dur = luaL_optnumber(L, 2, 1.5);
    int sev = static_cast<int>(luaL_optinteger(L, 3, 0));
    mgr->showToast(msg ? msg : "", static_cast<float>(dur), sev);
    return 0;
}

// yawn.note(track, pitch, [vel=127], [durBeats=0], [channel=0], [atBeat=0])
// atBeat > 0 → note-on (and note-off at atBeat+dur when dur > 0) are parked
// in the audio thread's pending queue and land sample-accurately on that
// transport beat (docs/live-coding.md §3.2). atBeat <= 0 fires now.
static int l_note(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;

    int track = static_cast<int>(luaL_checkinteger(L, 1));
    int pitch = static_cast<int>(luaL_checkinteger(L, 2));
    int vel   = static_cast<int>(luaL_optinteger(L, 3, 127));
    double dur = luaL_optnumber(L, 4, 0.0);
    int ch    = static_cast<int>(luaL_optinteger(L, 5, 0));
    double atBeat = luaL_optnumber(L, 6, 0.0);

    if (track < 0 || pitch < 0 || pitch > 127) return 0;
    vel = std::clamp(vel, 0, 127);

    auto* eng = mgr->audioEngine();
    if (!eng) return 0;

    // Ghost-note ledger (improv visualization): note-ons only, before
    // the command dispatch so failed sends can never be displayed as
    // pending (defense in depth for the earlier state-root bug class).
    mgr->trackGhostNote(track, atBeat, pitch, vel);
    // Freeze-take capture: absolute-beat ledger of everything the layer
    // plays (immediate notes anchor to the live transport position).
    mgr->captureLiveNote(track,
        atBeat > 0.0 ? atBeat : eng->transport().positionInBeats(),
        dur, pitch, vel, ch);

    if (atBeat > 0.0) {
        // Scheduled: both messages beat-anchored in the audio queue.
        if (vel > 0) {
            mgr->pushCommand(audio::SendMidiToTrackMsg{
                track, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
                static_cast<uint8_t>(ch), static_cast<uint8_t>(pitch),
                midi::Convert::vel7to16(static_cast<uint8_t>(vel)), 0, 0,
                atBeat});
        }
        if (dur > 0.0) {
            mgr->pushCommand(audio::SendMidiToTrackMsg{
                track, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff),
                static_cast<uint8_t>(ch), static_cast<uint8_t>(pitch),
                0, 0, 0, atBeat + dur});
        }
        return 0;
    }

    if (vel > 0) {
        mgr->pushCommand(audio::SendMidiToTrackMsg{
            track, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
            static_cast<uint8_t>(ch), static_cast<uint8_t>(pitch),
            midi::Convert::vel7to16(static_cast<uint8_t>(vel)), 0, 0});
    }
    if (dur > 0.0) {
        if (eng->transport().isPlaying()) {
            // Sample-accurate off: beat-anchored in the audio queue.
            mgr->scheduleNoteOff(track, pitch, ch,
                                 eng->transport().positionInBeats() + dur,
                                 /*absoluteBeat=*/true);
        } else {
            const double bpm = eng->transport().bpm();
            mgr->scheduleNoteOff(track, pitch, ch, dur * 60.0 / bpm,
                                 /*absoluteBeat=*/false);
        }
    }
    return 0;
}

// yawn.note_off(track, pitch, [channel=0])
static int l_note_off(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr) return 0;
    int track = static_cast<int>(luaL_checkinteger(L, 1));
    int pitch = static_cast<int>(luaL_checkinteger(L, 2));
    int ch    = static_cast<int>(luaL_optinteger(L, 3, 0));
    if (track < 0 || pitch < 0 || pitch > 127) return 0;
    mgr->pushCommand(audio::SendMidiToTrackMsg{
        track, static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff),
        static_cast<uint8_t>(ch), static_cast<uint8_t>(pitch), 0, 0, 0});
    return 0;
}

static int l_is_playing(lua_State* L) {
    auto* mgr = getManager(L);
    bool p = mgr && mgr->audioEngine() && mgr->audioEngine()->transport().isPlaying();
    lua_pushboolean(L, p ? 1 : 0);
    return 1;
}

static int l_set_playing(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) return 0;
    luaL_checkany(L, 1);
    bool on = lua_toboolean(L, 1) != 0;
    if (on) mgr->pushCommand(audio::TransportPlayMsg{});
    else    mgr->pushCommand(audio::TransportStopMsg{});
    return 0;
}

static int l_get_bpm(lua_State* L) {
    auto* mgr = getManager(L);
    lua_pushnumber(L, mgr && mgr->audioEngine() ? mgr->audioEngine()->transport().bpm() : 120.0);
    return 1;
}

static int l_set_bpm(lua_State* L) {
    auto* mgr = getManager(L);
    if (!mgr || !mgr->audioEngine()) return 0;
    double bpm = luaL_checknumber(L, 1);
    bpm = std::clamp(bpm, 20.0, 999.0);
    mgr->pushCommand(audio::TransportSetBPMMsg{bpm});
    return 0;
}

static int l_get_beat(lua_State* L) {
    auto* mgr = getManager(L);
    lua_pushnumber(L, mgr && mgr->audioEngine()
        ? mgr->audioEngine()->transport().positionInBeats() : 0.0);
    return 1;
}

static int l_get_bar(lua_State* L) {
    auto* mgr = getManager(L);
    double bar = 0.0;
    if (mgr && mgr->audioEngine()) {
        const auto& t = mgr->audioEngine()->transport();
        const int bpb = std::max(t.beatsPerBar(), 1);
        bar = std::floor(t.positionInBeats() / bpb) + 1.0;
    }
    lua_pushnumber(L, bar);
    return 1;
}

// ── LiveCodeEngine ───────────────────────────────────────────────────────

bool LiveCodeEngine::init(LiveCodeManager* mgr) {
    m_mgr = mgr;

    m_L = luaL_newstate();
    if (!m_L) {
        LOG_ERROR("LiveCode", "Failed to create Lua state");
        return false;
    }

    luaL_openlibs(m_L);

    // Sandbox: strip everything that touches the outside world or lets a
    // script escape the budget (§3.1). base's dofile/loadfile/require are
    // the escape hatches; io/os/package are filesystem+process; debug
    // could unwind our hooks.
    static const char* const kRemove[] = {
        "io", "os", "package", "debug", "dofile", "loadfile", "require",
    };
    for (const char* name : kRemove) {
        lua_pushnil(m_L);
        lua_setglobal(m_L, name);
    }

    // Register the API (creates yawn + improv tables).
    registerAPI();

    // Host-managed persistent state table (migrated across recreations).
    // NOTE: pre-seeded empty = truthy — scripts wanting defaults must use
    // FIELD-level fallbacks (yawn.state.x = yawn.state.x or v); the
    // table-level idiom `yawn.state = yawn.state or {...}` never fires.
    lua_getglobal(m_L, "yawn");
    lua_newtable(m_L);
    lua_setfield(m_L, -2, "state");
    lua_pop(m_L, 1);

    LOG_INFO("LiveCode", "Lua %s live-coding engine initialized", LUA_VERSION);
    return true;
}

void LiveCodeEngine::shutdown() {
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
    m_mgr = nullptr;
}

void LiveCodeEngine::registerAPI() {
    if (!m_L) return;

    // Manager pointer in the registry.
    lua_pushlightuserdata(m_L, m_mgr);
    lua_setfield(m_L, LUA_REGISTRYINDEX, kRegistryKey);

    // yawn table
    lua_newtable(m_L);
    static const luaL_Reg yawnFuncs[] = {
        {"log",          l_log},
        {"toast",        l_toast},
        {"note",         l_note},
        {"note_off",     l_note_off},
        {"set_notes",    l_set_notes},
        {"get_notes",    l_get_notes},
        {"clear_notes",  l_clear_notes},
        {"new_midi_clip",l_new_midi_clip},
        {"load_audio_file", l_load_audio_file},
        {"save_audio_buffer", l_save_audio_buffer},
        {"load_sample",  l_load_sample},
        {"set_clip",     l_set_clip},
        {"render",       l_render},
        {"cancel_render",l_cancel_render},
        {"launch_scene", l_launch_scene},
        {"launch_clip",  l_launch_clip},
        {"set_visual",   l_set_visual},
        {"is_playing",   l_is_playing},
        {"set_playing",  l_set_playing},
        {"get_bpm",      l_get_bpm},
        {"set_bpm",      l_set_bpm},
        {"get_beat",     l_get_beat},
        {"get_bar",      l_get_bar},
        {nullptr, nullptr}
    };
    luaL_setfuncs(m_L, yawnFuncs, 0);
    lua_setglobal(m_L, "yawn");

    // improv table
    lua_newtable(m_L);
    static const luaL_Reg improvFuncs[] = {
        {"every",       l_improv_every},
        {"after",       l_improv_after},
        {"at",          l_improv_at},
        {"on_bar",      l_improv_on_bar},
        {"clear",       l_improv_clear},
        {"clear_all",   l_improv_clear_all},
        {"lookahead",   l_improv_lookahead},
        {"late_policy", l_improv_late_policy},
        {nullptr, nullptr}
    };
    luaL_setfuncs(m_L, improvFuncs, 0);
    lua_setglobal(m_L, "improv");

    // Buffer/synthesis extension (LiveCodeBuffers): new_buffer, vector
    // ops, FFT/IFFT, polyblep — appended into the yawn table.
    registerBufferAPI(m_L);
}

bool LiveCodeEngine::protectedCall(int nargs, int budgetInstructions) {
    lua_sethook(m_L, countHook, LUA_MASKCOUNT, budgetInstructions);
    int rc = lua_pcall(m_L, nargs, 0, 0);
    lua_sethook(m_L, countHook, 0, 0);
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(m_L, -1);
        pushConsoleError(msg ? msg : "<no error message>");
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

bool LiveCodeEngine::runFile(const std::string& path) {
    if (!m_L) return false;
    if (luaL_loadfile(m_L, path.c_str()) != LUA_OK) {
        const char* msg = lua_tostring(m_L, -1);
        pushConsoleError(msg ? msg : "<no error message>");
        lua_pop(m_L, 1);
        return false;
    }
    return protectedCall(0, kBudgetRun);
}

bool LiveCodeEngine::runString(const std::string& code) {
    if (!m_L) return false;
    if (luaL_loadbuffer(m_L, code.c_str(), code.size(), "=editor") != LUA_OK) {
        const char* msg = lua_tostring(m_L, -1);
        pushConsoleError(msg ? msg : "<no error message>");
        lua_pop(m_L, 1);
        return false;
    }
    return protectedCall(0, kBudgetRun);
}

bool LiveCodeEngine::harvestSong(SongModel& out, std::string& err) const {
    if (!m_L) return true;
    lua_getglobal(m_L, "song");
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        return true;   // improv-only script — nothing to apply
    }
    const bool ok = parseSongModel(m_L, -1, out, err);
    lua_pop(m_L, 1);
    return ok;
}

bool LiveCodeEngine::callRef(int ref, double arg) {
    if (!m_L || ref == LUA_NOREF || ref == LUA_REFNIL) return true;
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return true;
    }
    lua_pushnumber(m_L, arg);
    return protectedCall(1, kBudgetDispatch);
}

int LiveCodeEngine::refFunction(lua_State* L) {
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void LiveCodeEngine::unrefFunction(int ref) {
    if (m_L && ref != LUA_NOREF && ref != LUA_REFNIL)
        luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

LiveCodeEngine::StateMap LiveCodeEngine::harvestState() const {
    StateMap out;
    if (!m_L) return out;
    lua_getglobal(m_L, "yawn");
    if (!lua_istable(m_L, -1)) { lua_pop(m_L, 1); return out; }
    lua_getfield(m_L, -1, "state");
    if (!lua_istable(m_L, -1)) { lua_pop(m_L, 2); return out; }

    auto harvestPrimitive = [this](StateVal& v) {
        int t = lua_type(m_L, -1);
        switch (t) {
            case LUA_TBOOLEAN: v.kind = StateVal::Kind::Bool; v.b = lua_toboolean(m_L, -1) != 0; break;
            case LUA_TNUMBER:  v.kind = StateVal::Kind::Num;  v.num = lua_tonumber(m_L, -1); break;
            case LUA_TSTRING: {
                v.kind = StateVal::Kind::Str;
                size_t len = 0;
                const char* s = lua_tolstring(m_L, -1, &len);
                if (s) v.str.assign(s, len);
                break;
            }
            default: v.kind = StateVal::Kind::Nil; break;
        }
    };

    lua_pushnil(m_L);
    while (lua_next(m_L, -2) != 0) {
        // key at -2, value at -1
        std::string key;
        int intKey = -1;
        if (lua_isinteger(m_L, -2)) intKey = static_cast<int>(lua_tointeger(m_L, -2));
        else if (lua_isstring(m_L, -2)) {
            const char* k = lua_tostring(m_L, -2);
            if (k) key = k;
        }

        StateVal v;
        bool keep = false;
        if (lua_istable(m_L, -1)) {
            // One-level table of primitives only.
            v.kind = StateVal::Kind::Table;
            lua_pushnil(m_L);
            while (lua_next(m_L, -2) != 0) {
                std::string subKey;
                int subIntKey = -1;
                if (lua_isinteger(m_L, -2)) subIntKey = static_cast<int>(lua_tointeger(m_L, -2));
                else if (lua_isstring(m_L, -2)) {
                    const char* k = lua_tostring(m_L, -2);
                    if (k) subKey = k;
                }
                StateVal sv;
                harvestPrimitive(sv);
                const bool primitive = (sv.kind == StateVal::Kind::Bool ||
                                        sv.kind == StateVal::Kind::Num  ||
                                        sv.kind == StateVal::Kind::Str);
                if (primitive) {
                    sv.intKey = subIntKey;
                    v.table.push_back({subKey, std::move(sv)});
                }
                lua_pop(m_L, 1);   // value
            }
            keep = !v.table.empty();
        } else {
            harvestPrimitive(v);
            keep = (v.kind == StateVal::Kind::Bool ||
                    v.kind == StateVal::Kind::Num  ||
                    v.kind == StateVal::Kind::Str);
        }

        if (keep) {
            v.intKey = intKey;
            out[key.empty() ? std::to_string(intKey) : key] = std::move(v);
        }
        lua_pop(m_L, 1);   // value (key stays for lua_next)
    }

    lua_pop(m_L, 2);
    return out;
}

void LiveCodeEngine::injectState(const StateMap& harvested) {
    if (!m_L || harvested.empty()) return;
    lua_getglobal(m_L, "yawn");
    if (!lua_istable(m_L, -1)) { lua_pop(m_L, 1); return; }

    auto pushPrimitive = [this](const StateVal& sv) {
        switch (sv.kind) {
            case StateVal::Kind::Bool: lua_pushboolean(m_L, sv.b ? 1 : 0); break;
            case StateVal::Kind::Num:  lua_pushnumber(m_L, sv.num); break;
            case StateVal::Kind::Str:  lua_pushlstring(m_L, sv.str.data(), sv.str.size()); break;
            default: lua_pushnil(m_L); break;
        }
    };

    // Replace yawn.state with a fresh table, then fill it from `harvested`.
    lua_newtable(m_L);
    int stateIdx = lua_gettop(m_L);
    for (const auto& [key, v] : harvested) {
        // Top-level integer keys were normalized to stringified keys in
        // the map ("42" + intKey=42); the intKey field is the authority —
        // key.empty() can never be true here. A literal string key "42"
        // harvests with intKey == -1 and still restores as a string.
        const bool intKey = (v.intKey >= 0);
        if (v.kind == StateVal::Kind::Table) {
            lua_newtable(m_L);
            int subIdx = lua_gettop(m_L);
            for (const auto& [subKey, sv] : v.table) {
                pushPrimitive(sv);
                if (sv.intKey >= 0) lua_rawseti(m_L, subIdx, sv.intKey);
                else                lua_setfield(m_L, subIdx, subKey.c_str());
            }
            if (intKey) lua_seti(m_L, stateIdx, v.intKey);   // pops sub-table
            else        lua_setfield(m_L, stateIdx, key.c_str());
        } else {
            pushPrimitive(v);
            if (intKey) lua_seti(m_L, stateIdx, v.intKey);
            else        lua_setfield(m_L, stateIdx, key.c_str());
        }
    }
    lua_setfield(m_L, -2, "state");   // pops state table into yawn
    lua_pop(m_L, 1);                  // pop yawn
}

void LiveCodeEngine::pushConsoleError(const std::string& what) {
    if (m_mgr) m_mgr->pushConsole(2, what);
    else       LOG_ERROR("LiveCode", "%s", what.c_str());
}

} // namespace livecode
} // namespace yawn
