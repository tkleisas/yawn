#include "livecode/LiveCodeSong.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"
#include "midi/MidiClip.h"
#include "util/Factory.h"
#include "util/Logger.h"
#include "util/MessageQueue.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace yawn {
namespace livecode {

// ── Parser helpers ───────────────────────────────────────────────────────

namespace {

bool fieldPresent(lua_State* L, int tableIdx, const char* key) {
    lua_getfield(L, tableIdx, key);
    const bool present = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return present;
}

// Parse "uid" as a decimal string or number; empty string = unset.
bool parseUid(lua_State* L, int tableIdx, std::string& out, std::string& err) {
    lua_getfield(L, tableIdx, "uid");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return true; }
    if (lua_isnumber(L, -1)) {
        out = std::to_string(static_cast<long long>(lua_tonumber(L, -1)));
    } else if (lua_isstring(L, -1)) {
        const char* s = lua_tostring(L, -1);
        if (s) {
            out = s;
            for (char c : out)
                if (c < '0' || c > '9') {
                    err = "song track uid must be a decimal number";
                    lua_pop(L, 1);
                    return false;
                }
        }
    } else {
        err = "song track uid must be a number or decimal string";
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);
    return true;
}

bool parseParams(lua_State* L, int tableIdx, std::vector<SongParam>& out,
                 std::string& err) {
    if (!lua_istable(L, tableIdx)) {
        err = "params must be a table";
        return false;
    }
    lua_pushnil(L);
    while (lua_next(L, tableIdx) != 0) {
        // key at -2, value at -1
        SongParam p;
        p.value = lua_tonumber(L, -1);
        if (lua_isinteger(L, -2)) {
            p.byIndex = true;
            p.index = static_cast<int>(lua_tointeger(L, -2));
        } else if (lua_isstring(L, -2)) {
            const char* k = lua_tostring(L, -2);
            if (k) p.name = k;
        } else {
            err = "params keys must be names or indices";
            lua_pop(L, 2);
            return false;
        }
        out.push_back(std::move(p));
        lua_pop(L, 1);
    }
    return true;
}

bool parseDevice(lua_State* L, int tableIdx, SongDevice& out, std::string& err) {
    if (!lua_istable(L, tableIdx)) {
        err = "device must be a table { id=..., params={...} }";
        return false;
    }
    lua_getfield(L, tableIdx, "id");
    if (lua_isstring(L, -1)) {
        const char* s = lua_tostring(L, -1);
        if (s) out.id = s;
    }
    lua_pop(L, 1);
    if (fieldPresent(L, tableIdx, "params")) {
        lua_getfield(L, tableIdx, "params");
        if (!parseParams(L, lua_gettop(L), out.params, err)) {
            lua_pop(L, 1);
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

bool parseNotes(lua_State* L, int tableIdx, std::vector<SongNote>& out,
                std::string& err) {
    if (!lua_istable(L, tableIdx)) {
        err = "notes must be an array of {start, dur, pitch, vel, [ch]}";
        return false;
    }
    lua_pushnil(L);
    while (lua_next(L, tableIdx) != 0) {
        // value at -1: array {start, dur, pitch, vel, [ch]} or named table
        SongNote n;
        if (lua_istable(L, -1)) {
            if (fieldPresent(L, -1, "pitch")) {
                // Named form.
                lua_getfield(L, -1, "start"); n.start = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "dur");   n.dur   = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "pitch"); n.pitch = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "vel");   n.vel   = lua_isnil(L, -1) ? 0.8 : lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "ch");    n.ch    = lua_isnil(L, -1) ? 0 : static_cast<int>(lua_tonumber(L, -1)); lua_pop(L, 1);
            } else {
                // Positional {start, dur, pitch, vel, [ch]}.
                lua_rawgeti(L, -1, 1); n.start = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_rawgeti(L, -1, 2); n.dur   = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_rawgeti(L, -1, 3); n.pitch = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_rawgeti(L, -1, 4);
                n.vel = lua_isnil(L, -1) ? 0.8 : lua_tonumber(L, -1);
                lua_pop(L, 1);
                lua_rawgeti(L, -1, 5);
                n.ch = lua_isnil(L, -1) ? 0 : static_cast<int>(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            out.push_back(std::move(n));
        }
        lua_pop(L, 1);   // value (array key stays for lua_next)
    }
    return true;
}

} // namespace

bool parseNotesInto(lua_State* L, int idx, std::vector<SongNote>& out) {
    std::string err;
    return parseNotes(L, idx, out, err);
}

bool parseSongModel(lua_State* L, int idx, SongModel& out, std::string& err) {
    if (!lua_istable(L, idx)) {
        err = "song must be a table";
        return false;
    }

    if (fieldPresent(L, idx, "bpm")) {
        lua_getfield(L, idx, "bpm");
        if (!lua_isnumber(L, -1)) { lua_pop(L, 1); err = "song.bpm must be a number"; return false; }
        out.bpm = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    if (fieldPresent(L, idx, "scenes")) {
        lua_getfield(L, idx, "scenes");
        if (!lua_isnumber(L, -1)) { lua_pop(L, 1); err = "song.scenes must be a number"; return false; }
        out.scenes = static_cast<int>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    if (!fieldPresent(L, idx, "tracks")) return true;   // bpm/scenes only
    lua_getfield(L, idx, "tracks");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); err = "song.tracks must be an array"; return false; }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        // value at -1: track table
        SongTrack t;
        bool ok = true;
        if (!lua_istable(L, -1)) {
            err = "song.tracks entries must be tables";
            ok = false;
        } else {
            const int ti = lua_gettop(L);
            if (!parseUid(L, ti, t.uid, err)) ok = false;
            if (ok && fieldPresent(L, ti, "name")) {
                lua_getfield(L, ti, "name");
                if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) t.name = s; t.hasName = true; }
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "type")) {
                lua_getfield(L, ti, "type");
                if (lua_isstring(L, -1)) { const char* s = lua_tostring(L, -1); if (s) t.type = s; }
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "volume")) {
                lua_getfield(L, ti, "volume");
                t.volume = lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "pan")) {
                lua_getfield(L, ti, "pan");
                t.pan = lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "mute")) {
                lua_getfield(L, ti, "mute");
                t.mute = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "solo")) {
                lua_getfield(L, ti, "solo");
                t.solo = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "instrument")) {
                t.hasInstrument = true;
                lua_getfield(L, ti, "instrument");
                if (!parseDevice(L, lua_gettop(L), t.instrument, err)) ok = false;
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "fx")) {
                t.hasFx = true;
                lua_getfield(L, ti, "fx");
                if (!lua_istable(L, -1)) {
                    err = "fx must be an array of device tables";
                    ok = false;
                } else {
                    lua_pushnil(L);
                    while (lua_next(L, -2) != 0) {
                        SongDevice d;
                        if (!parseDevice(L, lua_gettop(L), d, err)) { ok = false; lua_pop(L, 1); break; }
                        t.fx.push_back(std::move(d));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            if (ok && fieldPresent(L, ti, "clips")) {
                t.hasClips = true;
                lua_getfield(L, ti, "clips");
                if (!lua_istable(L, -1)) {
                    err = "clips must be a table keyed by 1-based scene";
                    ok = false;
                } else {
                    lua_pushnil(L);
                    while (lua_next(L, -2) != 0) {
                        if (lua_isinteger(L, -2)) {
                            const int scene = static_cast<int>(lua_tointeger(L, -2));
                            SongClip c;
                            lua_getfield(L, -1, "beats");
                            if (lua_isnumber(L, -1)) { c.beats = lua_tonumber(L, -1); }
                            lua_pop(L, 1);
                            if (fieldPresent(L, -1, "notes")) {
                                c.notes.clear();
                                lua_getfield(L, -1, "notes");
                                if (!parseNotes(L, lua_gettop(L), c.notes, err)) {
                                    lua_pop(L, 1);
                                    ok = false;
                                    break;
                                }
                                lua_pop(L, 1);
                            }
                            t.clips[scene] = std::move(c);
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);   // track value (array key stays)
        if (!ok) { lua_pop(L, 1); return false; }
        out.tracks.push_back(std::move(t));
    }
    lua_pop(L, 1);   // tracks table
    return true;
}

// ── Applier ──────────────────────────────────────────────────────────────

namespace {

// Does the slot's existing MIDI clip already match the declaration?
// (Idempotence: identical content → no re-set → no graveyard churn.)
bool midiClipMatches(const midi::MidiClip* existing, const SongClip& sc) {
    if (!existing) return false;
    if (std::abs(existing->lengthBeats() - sc.beats) > 1e-4) return false;
    if (existing->noteCount() != static_cast<int>(sc.notes.size())) return false;
    // Compare in sorted order (MidiClip stores sorted; the declaration
    // may be written in any order).
    std::vector<SongNote> notes = sc.notes;
    std::sort(notes.begin(), notes.end(),
              [](const SongNote& a, const SongNote& b) {
                  if (a.start != b.start) return a.start < b.start;
                  if (a.pitch != b.pitch) return a.pitch < b.pitch;
                  return a.ch < b.ch;
              });
    for (int i = 0; i < existing->noteCount(); ++i) {
        const auto& n = existing->note(i);
        const auto& s = notes[i];
        if (std::abs(n.startBeat - s.start) > 1e-4) return false;
        if (std::abs(n.duration - s.dur) > 1e-4) return false;
        if (n.pitch != static_cast<uint8_t>(std::clamp(s.pitch, 0.0, 127.0)))
            return false;
        if (n.channel != static_cast<uint8_t>(std::clamp(s.ch, 0, 15)))
            return false;
        // 16-bit velocity from float mul — ±1/256 LSB tolerance.
        const uint16_t want = static_cast<uint16_t>(
            std::clamp(s.vel, 0.0, 1.0) * 65535.0);
        if (std::abs(static_cast<int>(n.velocity) - static_cast<int>(want)) > 256)
            return false;
    }
    return true;
}

} // namespace

// Param name→index resolution + diffed set against any device with the
// standard parameter API. Returns resolved count; unknown names produce
// warnings listing the valid ones.
template <typename Device>
int applyDeviceParams(Device* dev, const std::vector<SongParam>& params,
                      std::vector<std::string>& warnings) {
    if (!dev) return 0;
    int applied = 0;
    for (const auto& p : params) {
        int idx = -1;
        if (p.byIndex) {
            idx = p.index;
        } else {
            for (int i = 0; i < dev->parameterCount(); ++i) {
                const auto& info = dev->parameterInfo(i);
                if (p.name.size() == std::strlen(info.name) &&
                    std::equal(p.name.begin(), p.name.end(), info.name,
                               [](char a, char b) {
                                   return std::tolower(static_cast<unsigned char>(a)) ==
                                          std::tolower(static_cast<unsigned char>(b));
                               })) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                std::string names;
                for (int i = 0; i < dev->parameterCount(); ++i)
                    names += (i ? ", " : "") + std::string(dev->parameterInfo(i).name);
                warnings.push_back("unknown param '" + p.name + "' (valid: " + names + ")");
                continue;
            }
        }
        if (idx < 0 || idx >= dev->parameterCount()) {
            warnings.push_back("param index " + std::to_string(idx) + " out of range");
            continue;
        }
        const float current = dev->getParameter(idx);
        if (std::abs(current - static_cast<float>(p.value)) > 1e-5f) {
            dev->setParameter(idx, static_cast<float>(p.value));
            ++applied;
        }
    }
    return applied;
}

SongApplyReport applySongModel(const SongModel& song,
                               std::set<uint64_t>& owned,
                               const SongApplyContext& ctx) {
    SongApplyReport rep;
    if (!ctx.project || !ctx.engine) {
        rep.warnings.push_back("no project/engine attached");
        return rep;
    }
    Project& project = *ctx.project;
    audio::AudioEngine& engine = *ctx.engine;

    // ── BPM ──
    if (song.bpm) {
        const double target = std::clamp(*song.bpm, 20.0, 999.0);
        if (std::abs(engine.transport().bpm() - target) > 1e-6) {
            engine.sendCommand(audio::TransportSetBPMMsg{target});
            rep.ops.push_back("bpm → " + std::to_string(target));
        }
    }

    // ── Scenes (grow-only in v1) ──
    if (song.scenes && *song.scenes > project.numScenes()) {
        while (project.numScenes() < *song.scenes) project.addScene();
        rep.ops.push_back("scenes → " + std::to_string(*song.scenes));
    } else if (song.scenes && *song.scenes < project.numScenes()) {
        rep.warnings.push_back("scene shrink not supported yet (kept " +
                               std::to_string(project.numScenes()) + ")");
    }

    // ── Tracks ──
    std::set<uint64_t> declared;
    for (const auto& st : song.tracks) {
        // Resolve the target track: uid > name > create.
        int ti = -1;
        uint64_t uid = 0;
        if (!st.uid.empty()) uid = std::strtoull(st.uid.c_str(), nullptr, 10);
        if (uid != 0) ti = project.findTrackByUid(uid);
        if (ti < 0 && st.hasName) ti = project.findTrackByName(st.name);
        if (ti < 0) {
            // Create.
            Track::Type type = Track::Type::Midi;
            if (st.type == "audio") type = Track::Type::Audio;
            else if (st.type == "visual") type = Track::Type::Visual;
            const std::string name =
                st.hasName ? st.name : ("Song " + std::to_string(project.numTracks() + 1));
            if (project.numTracks() >= kMaxTracks) {
                rep.warnings.push_back("track limit reached — '" + name + "' skipped");
                continue;
            }
            project.addTrack(name, type);
            ti = project.numTracks() - 1;
            project.assignTrackUid(project.track(ti));   // backfills st.uid if given
            if (uid != 0) {
                // Adopt the song's explicit uid (monotonic bump inside).
                project.track(ti).uid = uid;
                project.assignTrackUid(project.track(ti));
            }
            rep.ops.push_back("track + '" + name + "'");
            rep.changed = true;
        } else {
            project.assignTrackUid(project.track(ti));
        }

        const int tiC = ti;
        Track& tr = project.track(tiC);
        uid = tr.uid;
        declared.insert(uid);
        if (!owned.count(uid)) {
            owned.insert(uid);   // adopted → script-owned from now on
        }

        // Name
        if (st.hasName && tr.name != st.name) {
            const std::string oldName = tr.name;
            tr.name = st.name;
            rep.ops.push_back("track '" + oldName + "' name → " + st.name);
            rep.changed = true;
        }
        // Type: creation-only in v1.
        if (!st.type.empty()) {
            Track::Type want = (st.type == "audio") ? Track::Type::Audio
                             : (st.type == "visual") ? Track::Type::Visual
                                                      : Track::Type::Midi;
            if (tr.type != want)
                rep.warnings.push_back("track '" + tr.name +
                    "': type change not supported yet (keep UI)");
        }
        // Mixer-ish fields — Project copy + command (same as UI paths).
        if (st.volume && std::abs(tr.volume - *st.volume) > 1e-5) {
            tr.volume = static_cast<float>(*st.volume);
            engine.sendCommand(audio::SetTrackVolumeMsg{tiC, tr.volume});
            rep.ops.push_back("track '" + tr.name + "' volume → " + std::to_string(tr.volume));
            rep.changed = true;
        }
        if (st.pan) {
            // Pan lives in the mixer only (no Project copy) — diff against
            // the mixer channel and converge via the command.
            const float want = static_cast<float>(std::clamp(*st.pan, -1.0, 1.0));
            const float have = engine.mixer().trackChannel(tiC).pan;
            if (std::abs(have - want) > 1e-5f) {
                engine.sendCommand(audio::SetTrackPanMsg{tiC, want});
                rep.ops.push_back("track '" + tr.name + "' pan → " +
                                  std::to_string(want));
                rep.changed = true;
            }
        }
        if (st.mute && tr.muted != *st.mute) {
            tr.muted = *st.mute;
            engine.sendCommand(audio::SetTrackMuteMsg{tiC, tr.muted});
            rep.ops.push_back("track '" + tr.name + "' mute → " +
                              (tr.muted ? "on" : "off"));
            rep.changed = true;
        }
        if (st.solo && tr.soloed != *st.solo) {
            tr.soloed = *st.solo;
            engine.sendCommand(audio::SetTrackSoloMsg{tiC, tr.soloed});
            rep.ops.push_back("track '" + tr.name + "' solo → " +
                              (tr.soloed ? "on" : "off"));
            rep.changed = true;
        }

        // ── Instrument ──
        if (st.hasInstrument) {
            auto* inst = engine.instrument(tiC);
            const bool idMatches = inst && st.instrument.id == inst->id();
            if (!st.instrument.id.empty() && !idMatches) {
                auto fresh = createInstrument(st.instrument.id);
                if (!fresh) {
                    rep.warnings.push_back("unknown instrument id '" +
                                           st.instrument.id + "'");
                } else {
                    engine.setInstrument(tiC, std::move(fresh));
                    inst = engine.instrument(tiC);
                    rep.ops.push_back("track '" + tr.name + "' instrument → " +
                                      st.instrument.id);
                    rep.changed = true;
                }
            } else if (st.instrument.id.empty() && inst) {
                // Empty id = remove instrument.
                engine.setInstrument(tiC, nullptr);
                inst = nullptr;
                rep.ops.push_back("track '" + tr.name + "' instrument removed");
                rep.changed = true;
            }
            if (inst && !st.instrument.params.empty()) {
                applyDeviceParams(inst, st.instrument.params, rep.warnings);
            }
        }

        // ── FX chain (prefix management; declared list present) ──
        if (st.hasFx) {
            auto& chain = engine.mixer().trackEffects(tiC);
            const int declaredCount = static_cast<int>(st.fx.size());
            if (declaredCount == 0) {
                // Empty declared list → clear the whole chain.
                for (int i = chain.count() - 1; i >= 0; --i)
                    chain.removeRetired(i);
                if (chain.count() != 0) {
                    rep.ops.push_back("track '" + tr.name + "' fx cleared");
                    rep.changed = true;
                }
            } else {
                for (int i = 0; i < declaredCount; ++i) {
                    const auto& sd = st.fx[i];
                    auto* live = chain.effectAt(i);
                    const bool idMatches = live && live->id() == sd.id;
                    if (!idMatches) {
                        auto fresh = createAudioEffect(sd.id);
                        if (!fresh) {
                            rep.warnings.push_back("unknown effect id '" + sd.id + "'");
                            continue;
                        }
                        chain.insert(i, std::move(fresh));   // replaces occupant
                        live = chain.effectAt(i);
                        rep.ops.push_back("track '" + tr.name + "' fx[" +
                                          std::to_string(i) + "] → " + sd.id);
                        rep.changed = true;
                    }
                    if (live && !sd.params.empty())
                        applyDeviceParams(live, sd.params, rep.warnings);
                }
            }
        }

        // ── Clips ──
        if (st.hasClips) {
            for (const auto& [scene1, sc] : st.clips) {
                const int sceneIdx = scene1 - 1;   // Lua 1-based → project 0-based
                if (sceneIdx < 0 || sceneIdx >= project.numScenes()) {
                    rep.warnings.push_back("clip scene " + std::to_string(scene1) +
                                           " out of range");
                    continue;
                }
                // TODO(phase 4b): audio clips (sample refs). v1: MIDI clips.
                auto* slot = project.getSlot(tiC, sceneIdx);
                if (slot && midiClipMatches(slot->midiClip.get(), sc))
                    continue;   // already converged

                auto clip = std::make_unique<midi::MidiClip>(std::max(sc.beats, 0.25));
                for (const auto& n : sc.notes) {
                    midi::MidiNote note;
                    note.startBeat = n.start;
                    note.duration = std::max(n.dur, 0.01);
                    note.pitch = static_cast<uint8_t>(std::clamp(n.pitch, 0.0, 127.0));
                    note.channel = static_cast<uint8_t>(std::clamp(n.ch, 0, 15));
                    note.velocity = static_cast<uint16_t>(
                        std::clamp(n.vel, 0.0, 1.0) * 65535.0);
                    clip->addNote(note);
                }
                midi::MidiClip* np = nullptr;
                if (ctx.setMidiClipLive) {
                    np = ctx.setMidiClipLive(tiC, sceneIdx, std::move(clip));
                } else {
                    np = project.setMidiClip(tiC, sceneIdx, std::move(clip));
                }
                if (np) {
                    rep.ops.push_back("track '" + tr.name + "' clip@" +
                                      std::to_string(scene1) + " (" +
                                      std::to_string(sc.notes.size()) + " notes)");
                    rep.changed = true;
                }
            }
        }
    }

    // ── Track removal: owned by a previous apply, no longer declared ──
    std::vector<uint64_t> removeUids;
    for (uint64_t uid : owned)
        if (!declared.count(uid)) removeUids.push_back(uid);
    for (uint64_t uid : removeUids) {
        const int ti = project.findTrackByUid(uid);
        owned.erase(uid);
        if (ti < 0) continue;
        if (engine.transport().isPlaying()) {
            rep.warnings.push_back("track '" + project.track(ti).name +
                "' removed from song — deletion deferred (transport playing)");
            continue;
        }
        if (ti != project.numTracks() - 1) {
            rep.warnings.push_back("track '" + project.track(ti).name +
                "' removed from song — v1 removes only the last track");
            continue;
        }
        // Stop + quiesce, then remove (mirrors the UI delete path).
        engine.sendCommand(audio::StopClipMsg{ti, audio::QuantizeMode::None});
        engine.sendCommand(audio::StopMidiClipMsg{ti});
        engine.quiesceCommands(std::chrono::milliseconds(250));
        engine.removeTrackSlot(ti, project.numTracks());
        project.removeLastTrack();
        if (ctx.engineSync) ctx.engineSync();
        if (ctx.markDirty) ctx.markDirty();
        rep.ops.push_back("track removed");
        rep.changed = true;
    }

    if (rep.changed) {
        if (ctx.engineSync) ctx.engineSync();
        if (ctx.markDirty) ctx.markDirty();
    }
    return rep;
}

// ── Code lens (phase 6) ──────────────────────────────────────────────────

namespace {

std::string escapeLuaString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

template <typename Device>
void appendDevice(std::string& out, const std::string& id, const Device* dev) {
    out += "{ id = \"" + id + "\", params = {";
    bool first = true;
    for (int i = 0; i < dev->parameterCount(); ++i) {
        out += first ? " " : ", ";
        first = false;
        out += "[\"" + escapeLuaString(dev->parameterInfo(i).name) +
               "\"] = " + num(dev->getParameter(i));
    }
    out += first ? "}" : " }";
    out += " }";
}

} // namespace

std::string generateSongSource(const Project& project,
                               const audio::AudioEngine& engine) {
    std::string out;
    out += "-- Code lens — regenerated from the live project.\n";
    out += "-- Read-only projection of the current state (docs/live-coding.md §5.4).\n";
    out += "song = {\n";
    out += "  bpm = " + num(engine.transport().bpm()) + ",\n";
    out += "  scenes = " + std::to_string(project.numScenes()) + ",\n";
    out += "  tracks = {\n";

    for (int t = 0; t < project.numTracks(); ++t) {
        const Track& tr = project.track(t);
        out += "    { uid = " + std::to_string(tr.uid) + ", name = \"" +
               escapeLuaString(tr.name) + "\", type = \"" +
               (tr.type == Track::Type::Audio  ? "audio"
              : tr.type == Track::Type::Midi   ? "midi" : "visual") + "\"";
        out += ", volume = " + num(tr.volume);
        if (tr.muted) out += ", mute = true";
        if (tr.soloed) out += ", solo = true";
        out += ",\n";

        // Instrument (const_cast mirrors ProjectSerializer's read-only
        // traversal of the engine's non-const accessors).
        audio::AudioEngine& eng = const_cast<audio::AudioEngine&>(engine);
        auto* inst = eng.instrument(t);
        if (inst) {
            out += "      instrument = ";
            appendDevice(out, inst->id(), inst);
            out += ",\n";
        }

        // Audio FX chain
        const auto& chain = eng.mixer().trackEffects(t);
        if (chain.count() > 0) {
            out += "      fx = {\n";
            for (int i = 0; i < chain.count(); ++i) {
                auto* fx = chain.effectAt(i);
                if (!fx) continue;
                out += "        ";
                appendDevice(out, fx->id(), fx);
                out += ",\n";
            }
            out += "      },\n";
        }

        // MIDI clips (session grid)
        bool wroteClips = false;
        for (int s = 0; s < project.numScenes(); ++s) {
            auto* slot = project.getSlot(t, s);
            if (!slot) continue;
            if (slot->midiClip) {
                if (!wroteClips) { out += "      clips = {\n"; wroteClips = true; }
                out += "        [" + std::to_string(s + 1) + "] = { beats = " +
                       num(slot->midiClip->lengthBeats()) + ", notes = {";
                for (int n = 0; n < slot->midiClip->noteCount(); ++n) {
                    const auto& note = slot->midiClip->note(n);
                    out += (n ? ", " : " ");
                    out += "{" + num(note.startBeat) + ", " + num(note.duration) +
                           ", " + std::to_string(note.pitch) + ", " +
                           num(note.velocity / 65535.0);
                    if (note.channel != 0)
                        out += ", " + std::to_string(note.channel);
                    out += "}";
                }
                out += " } },\n";
            } else if (slot->audioClip) {
                out += "      -- audio clip @scene " + std::to_string(s + 1) +
                       ": " + escapeLuaString(slot->audioClip->name) + "\n";
            }
        }
        if (wroteClips) out += "      },\n";
        out += "    },\n";
    }

    out += "  },\n";
    out += "}\n";
    return out;
}

} // namespace livecode
} // namespace yawn
