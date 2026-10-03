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
#include <map>
#include <set>

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
        // Type: declared type wins when the track has no content (fresh
        // adoption of a default track); with content, type follows the
        // content (Project::refreshTrackType on the setters) and the
        // declaration is ignored with a note.
        if (!st.type.empty()) {
            Track::Type want = (st.type == "audio") ? Track::Type::Audio
                             : (st.type == "visual") ? Track::Type::Visual
                                                      : Track::Type::Midi;
            if (tr.type != want) {
                bool hasContent = false;
                for (int s = 0; s < project.numScenes(); ++s) {
                    if (const auto* slot = project.getSlot(tiC, s);
                        slot && !slot->empty()) { hasContent = true; break; }
                }
                hasContent |= !tr.arrangementClips.empty();
                if (hasContent) {
                    rep.warnings.push_back("track '" + tr.name +
                        "': declared type ignored — content owns the type");
                } else {
                    tr.type = want;
                    engine.sendCommand(audio::SetTrackTypeMsg{
                        tiC, static_cast<uint8_t>(want)});
                    rep.ops.push_back("track '" + tr.name + "' type → " + st.type);
                    rep.changed = true;
                }
            }
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

// ─── Template-preserving two-way round-trip (§5.4 stage 2) ──────────────
//
// patchSongSource(original, project, engine) rewrites *value literals* of
// the declarative `song` block in the script source so UI-side mutations
// flow back into the script file without clobbering the user's template:
// comments, layout, unknown keys and everything outside the block are
// preserved byte-for-byte. Structural reconcile covers tracks (insert
// new blocks / remove dead ones by uid); scalar, note and param lines
// are rewritten in place. Anything the line-recognizer cannot classify
// is left untouched and reported as a warning.

namespace {

bool isSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool isIdentCharAny(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string trimStr(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpaceChar(s[b])) ++b;
    while (e > b && isSpaceChar(s[e - 1])) --e;
    return s.substr(b, e - b);
}

struct CodeComment {
    std::string code;      // up to (not incl.) the `--`
    std::string comment;   // "--…" rest ("" when none)
};

// Splits a source line at its first `--` that is not inside a quoted
// string (naive — adequate for value lines).
CodeComment splitInlineComment(const std::string& line) {
    CodeComment out;
    bool inStr = false;
    char quote = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == quote) inStr = false;
            continue;
        }
        if (c == '"' || c == '\'') { inStr = true; quote = c; continue; }
        if (c == '-' && i + 1 < line.size() && line[i + 1] == '-') {
            out.code = line.substr(0, i);
            out.comment = line.substr(i);
            return out;
        }
    }
    out.code = line;
    return out;
}

// Net brace delta of a code line (comments/strings excluded).
int lineBraceDelta(const std::string& line) {
    const CodeComment cc = splitInlineComment(line);
    int d = 0;
    for (char c : cc.code) {
        if (c == '{') ++d;
        else if (c == '}') --d;
    }
    return d;
}

// Line (comment-split code) reads `key = …` (spaces allowed around '=').
bool startsWithKey(const std::string& code, const std::string& key) {
    const std::string t = trimStr(code);
    if (t.rfind(key, 0) != 0) return false;
    size_t j = key.size();
    while (j < t.size() && isSpaceChar(t[j])) ++j;
    return j < t.size() && t[j] == '=';
}

// Value text of `key = <value>` (with trailing comma stripped).
std::string keyValueText(const std::string& code, const std::string& key) {
    const std::string t = trimStr(code);
    if (t.rfind(key, 0) != 0) return "";
    const size_t eq = t.find('=', key.size());
    if (eq == std::string::npos) return "";
    size_t v = eq + 1;
    while (v < t.size() && isSpaceChar(t[v])) ++v;
    size_t e = t.size();
    if (e > v && t[e - 1] == ',') --e;
    return t.substr(v, e - v);
}

// Reads an integer `key = N` anywhere in `code` (word-bounded key),
// returning false when absent.
bool keyIntValue(const std::string& code, const std::string& key, int& out) {
    const CodeComment cc = splitInlineComment(code);
    const std::string& s = cc.code;
    size_t pos = s.find(key);
    while (pos != std::string::npos) {
        const bool boundedBefore =
            pos == 0 || !isIdentCharAny(s[pos - 1]);
        size_t j = pos + key.size();
        const bool boundedAfter =
            j >= s.size() || !isIdentCharAny(s[j]);
        if (boundedBefore && boundedAfter) {
            while (j < s.size() && isSpaceChar(s[j])) ++j;
            if (j < s.size() && s[j] == '=') {
                ++j;
                while (j < s.size() && isSpaceChar(s[j])) ++j;
                if (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) ||
                                     s[j] == '-' || s[j] == '+')) {
                    out = static_cast<int>(std::strtol(s.c_str() + j, nullptr, 10));
                    return true;
                }
            }
        }
        pos = s.find(key, pos + 1);
    }
    return false;
}

// ─── Script track-block segmentation ────────────────────────────────────

struct ScriptTrack {
    long start = -1;   // line index of the "{" opener
    long end   = -1;   // line index of the matching close
    int  uid   = -1;
};

// Finds `tracks = {` in [begin,end) and segments its track blocks.
std::vector<ScriptTrack> findScriptTracks(const std::vector<std::string>& lines,
                                          long begin, long end,
                                          long& tracksOpen, long& tracksClose) {
    std::vector<ScriptTrack> out;
    tracksOpen = tracksClose = -1;
    for (long i = begin; i <= end; ++i) {
        const std::string code = splitInlineComment(lines[i]).code;
        if (startsWithKey(code, "tracks")) {
            if (code.find('{') != std::string::npos) tracksOpen = i;
            break;
        }
    }
    if (tracksOpen < 0) return out;
    int depth = 0;
    for (long i = tracksOpen; i <= end; ++i) {
        depth += lineBraceDelta(lines[i]);
        if (depth <= 0) { tracksClose = i; break; }
    }
    if (tracksClose < 0) return out;

    // Track blocks: first non-ws char is '{' while at depth 1 inside
    // `tracks` (clip keys start with '[' — no clash).
    ScriptTrack cur;
    int d = 0;
    for (long i = tracksOpen; i < tracksClose; ++i) {
        const std::string tcode = trimStr(splitInlineComment(lines[i]).code);
        const int before = d;
        d += lineBraceDelta(lines[i]);
        // Start: '{'-leading line reached while inside the array.
        if (before == 1 && !tcode.empty() && tcode[0] == '{' &&
            cur.start < 0) {
            cur = ScriptTrack{};
            cur.start = i;
        }
        // Close: depth back at array level after the line — covers
        // multi-line blocks and one-line `{ … },` blocks alike.
        if (cur.start >= 0 && cur.end < 0 && d <= 1) {
            cur.end = i;
            for (long k = cur.start; k <= i; ++k)
                if (keyIntValue(lines[k], "uid", cur.uid)) break;
            out.push_back(cur);
            cur = ScriptTrack{};
        }
    }
    // Unterminated final block (script being written): clamp to close.
    if (cur.start >= 0 && cur.end < 0) {
        cur.end = tracksClose;
        for (long k = cur.start; k <= tracksClose; ++k) {
            const std::string v =
                keyValueText(splitInlineComment(lines[k]).code, "uid");
            if (!v.empty()) { cur.uid = (int)std::strtol(v.c_str(), nullptr, 10); break; }
        }
        out.push_back(cur);
    }
    return out;
}

// Emits one canonical track block at the given indentation (shared with
// generateSongSource so inserts match its shape exactly).
void appendTrackBlock(std::string& out, const Project& project,
                      audio::AudioEngine& engine, int t,
                      const std::string& indent) {
    const Track& tr = project.track(t);
    out += indent + "{ uid = " + std::to_string(tr.uid) + ", name = \"" +
           escapeLuaString(tr.name) + "\", type = \"" +
           (tr.type == Track::Type::Audio  ? "audio"
          : tr.type == Track::Type::Midi   ? "midi" : "visual") + "\"";
    out += ", volume = " + num(tr.volume);
    if (tr.muted) out += ", mute = true";
    if (tr.soloed) out += ", solo = true";
    out += ",\n";

    auto* inst = engine.instrument(t);
    if (inst) {
        out += indent + "  instrument = ";
        appendDevice(out, inst->id(), inst);
        out += ",\n";
    }

    const auto& chain = engine.mixer().trackEffects(t);
    if (chain.count() > 0) {
        out += indent + "  fx = {\n";
        for (int i = 0; i < chain.count(); ++i) {
            auto* fx = chain.effectAt(i);
            if (!fx) continue;
            out += indent + "    ";
            appendDevice(out, fx->id(), fx);
            out += ",\n";
        }
        out += indent + "  },\n";
    }

    bool wroteClips = false;
    for (int s = 0; s < project.numScenes(); ++s) {
        auto* slot = project.getSlot(t, s);
        if (!slot) continue;
        if (slot->midiClip) {
            if (!wroteClips) { out += indent + "  clips = {\n"; wroteClips = true; }
            out += indent + "    [" + std::to_string(s + 1) + "] = { beats = " +
                   num(slot->midiClip->lengthBeats()) + ", notes = {";
            for (int n = 0; n < slot->midiClip->noteCount(); ++n) {
                const auto& note = slot->midiClip->note(n);
                out += (n ? ", " : " ");
                out += "{" + num(note.startBeat) + ", " + num(note.duration)
                     + ", " + std::to_string(note.pitch) + ", "
                     + num(note.velocity / 65535.0);
                if (note.channel != 0)
                    out += ", " + std::to_string(note.channel);
                out += "}";
            }
            out += " } },\n";
        }
    }
    if (wroteClips) out += indent + "  },\n";
    out += indent + "},\n";
}

} // namespace

SongPatchReport patchSongSource(const std::string& original,
                                const Project& project,
                                const audio::AudioEngine& engine) {
    SongPatchReport rep;

    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : original) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else           cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    // Locate `song = { … }`.
    long songOpen = -1, songClose = -1;
    int depth = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trimStr(splitInlineComment(lines[i]).code);
        if (songOpen < 0) {
            if (t.rfind("song", 0) == 0 &&
                t.find('=', 4) != std::string::npos &&
                t.find('{') != std::string::npos &&
                t.find("[[") == std::string::npos) {
                songOpen = (long)i;
                depth = lineBraceDelta(lines[i]);
            }
            continue;
        }
        depth += lineBraceDelta(lines[i]);
        if (depth <= 0) { songClose = (long)i; break; }
    }
    if (songOpen < 0 || songClose <= songOpen) {
        rep.warnings.push_back("no `song = { … }` block found");
        return rep;
    }

    auto setLine = [&](size_t i, const std::string& replacement) {
        if (lines[i] != replacement) { lines[i] = replacement; rep.changed = true; }
    };
    auto indentOf = [&](size_t i) {
        size_t n = 0;
        while (n < lines[i].size() && isSpaceChar(lines[i][n])) ++n;
        return lines[i].substr(0, n);
    };

    // ── Scalar lines: bpm / scenes ──
    for (size_t i = (size_t)songOpen + 1; i <= (size_t)songClose; ++i) {
        const CodeComment cc = splitInlineComment(lines[i]);
        const std::string key = (trimStr(cc.code).rfind("bpm", 0) == 0 &&
                                 trimStr(cc.code).find('=') == 4) ? "bpm" : "";
        if (key == "bpm") {
            std::string rebuilt = indentOf(i) + "bpm = " +
                                  num(engine.transport().bpm()) + ",";
            if (!cc.comment.empty()) rebuilt += " " + cc.comment;
            setLine(i, rebuilt);
        } else if (startsWithKey(cc.code, "scenes")) {
            std::string rebuilt = indentOf(i) + "scenes = " +
                                  std::to_string(project.numScenes()) + ",";
            if (!cc.comment.empty()) rebuilt += " " + cc.comment;
            setLine(i, rebuilt);
        }
    }

    // ── Track blocks ──
    long tracksOpen = -1, tracksClose = -1;
    const std::vector<ScriptTrack> scriptTracks =
        findScriptTracks(lines, songOpen, songClose, tracksOpen, tracksClose);

    std::map<uint64_t, int> modelByUid;   // uid → track index
    std::set<uint64_t> modelUids;
    for (int t = 0; t < project.numTracks(); ++t) {
        const uint64_t uid = project.track(t).uid;
        modelByUid.emplace(uid, t);
        modelUids.insert(uid);
    }

    // Patch a single, canonically-shaped track block: scalar mixer
    // lines + single-line clip declarations.
    auto patchTrackBlock = [&](int trackIndex, long a, long b) {
        const Track& tr = project.track(trackIndex);
        const auto emit = [&](size_t i, std::string body) {
            if (!splitInlineComment(lines[i]).comment.empty())
                body += " " + splitInlineComment(lines[i]).comment;
            setLine(i, body);
        };
        for (size_t i = (size_t)a; i <= (size_t)b; ++i) {
            const CodeComment cc = splitInlineComment(lines[i]);
            const std::string pad = indentOf(i);
            // Generator-shaped opener: `{ uid = N, name = "…", type = "T",
            // volume = V[, mute = true][, solo = true],` — rebuild from
            // the model when nothing else rides on the line.
            if (startsWithKey(cc.code, "{ uid =")) {
                const std::string code = trimStr(cc.code);
                if (code.find("instrument") != std::string::npos ||
                    code.find("fx") != std::string::npos ||
                    code.find("clips") != std::string::npos ||
                    code.find("params") != std::string::npos)
                    continue;   // extended opening line: untouched
                std::string rebuilt = pad + "{ uid = " +
                    std::to_string(tr.uid) + ", name = \"" +
                    escapeLuaString(tr.name) + "\", type = \"" +
                    (tr.type == Track::Type::Audio  ? "audio"
                   : tr.type == Track::Type::Midi   ? "midi" : "visual") +
                    "\", volume = " + num(tr.volume);
                if (tr.muted) rebuilt += ", mute = true";
                if (tr.soloed) rebuilt += ", solo = true";
                rebuilt += ",";
                if (!cc.comment.empty()) rebuilt += " " + cc.comment;
                setLine(i, rebuilt);
                continue;
            }
            if (startsWithKey(cc.code, "volume")) {
                emit(i, pad + "volume = " + num(tr.volume) + ",");
            } else if (startsWithKey(cc.code, "mute")) {
                emit(i, pad + "mute = " + (tr.muted ? "true" : "false") + ",");
            } else if (startsWithKey(cc.code, "solo")) {
                emit(i, pad + "solo = " + (tr.soloed ? "true" : "false") + ",");

            } else if (!trimStr(cc.code).empty() &&
                       trimStr(cc.code)[0] == '[' &&
                       cc.code.find("] = {") != std::string::npos) {
                // `[N] = { beats = …, notes = { … } } }` single-line shape.
                if (cc.code.find("notes") == std::string::npos ||
                    cc.code.find('{') == std::string::npos)
                    continue;   // multi-line clip group: untouched, v1
                // When the block closes on the same line we can patch it.
                int bdelta = 0;
                for (char c : trimStr(splitInlineComment(cc.code).code))
                    (c == '{') ? ++bdelta : (c == '}' ? --bdelta : bdelta);
                if (bdelta != 0) {
                    rep.warnings.push_back(
                        "multi-line clip groups are left untouched (rewrite by hand or regenerate)");
                    continue;
                }
                const std::string keyPart = trimStr(
                    cc.code.substr(0, cc.code.find('{')));
                const size_t p1 = keyPart.find('['), p2 = keyPart.find(']');
                if (p1 == std::string::npos || p2 == std::string::npos || p2 <= p1)
                    continue;
                const int scene = (int)std::strtol(
                    keyPart.substr(p1 + 1, p2 - p1).c_str(), nullptr, 10) - 1;
                if (scene < 0 || scene >= project.numScenes()) continue;
                auto* slot = project.getSlot(trackIndex, scene);
                if (!slot || !slot->midiClip) continue;
                const auto& mc = *slot->midiClip;
                std::string clip = pad + "[" + std::to_string(scene + 1) +
                                   "] = { beats = " + num(mc.lengthBeats()) +
                                   ", notes = {";
                for (int n = 0; n < mc.noteCount(); ++n) {
                    const auto& note = mc.note(n);
                    clip += (n ? ", " : " ") + std::string("{") +
                            num(note.startBeat) + ", " + num(note.duration) +
                            ", " + std::to_string(note.pitch) + ", " +
                            num(note.velocity / 65535.0);
                    if (note.channel != 0) clip += ", " + std::to_string(note.channel);
                    clip += "}";
                }
                clip += " } },";
                emit(i, clip);
            }
        }
    };

    for (const auto& st : scriptTracks) {
        if (st.uid < 0 || st.start < 0) {
            rep.warnings.push_back("track block without uid left untouched");
            continue;
        }
        auto it = modelByUid.find((uint64_t)st.uid);
        if (it == modelByUid.end()) continue;
        patchTrackBlock(it->second, st.start, st.end);
    }

    // ── Structural: drop dead blocks, insert missing tracks ──
    std::set<uint64_t> present;
    for (const auto& st : scriptTracks)
        if (st.uid >= 0 && modelUids.count((uint64_t)st.uid))
            present.insert((uint64_t)st.uid);

    std::set<size_t> kill;
    for (const auto& st : scriptTracks)
        if (st.uid >= 0 && !present.count((uint64_t)st.uid))
            for (long k = st.start; k <= st.end; ++k) kill.insert((size_t)k);

    std::vector<std::string> additions;
    for (int t = 0; t < project.numTracks(); ++t) {
        const uint64_t uid = project.track(t).uid;
        if (present.count(uid)) continue;
        std::string block;
        appendTrackBlock(block, project, const_cast<audio::AudioEngine&>(engine), t, "    ");
        additions.push_back(block);
    }
    if (!kill.empty() || !additions.empty()) rep.changed = true;

    if (!kill.empty() || !additions.empty()) {
        // Insertion point: before the tracks-close line, after all
        // surviving track blocks.
        std::vector<std::string> result;
        result.reserve(lines.size());
        const size_t insAt = tracksClose >= 0 ? (size_t)tracksClose : lines.size();
        size_t ai = 0;
        (void)ai;
        auto pushBlock = [&](const std::string& block) {
            std::string cur;
            for (char c : block) {
                if (c == '\n') { result.push_back(cur); cur.clear(); }
                else           cur += c;
            }
            if (!cur.empty()) result.push_back(cur);
        };
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i == insAt && !additions.empty())
                for (const auto& a : additions) pushBlock(a);
            if (!kill.count(i)) result.push_back(lines[i]);
        }
        if (insAt >= lines.size() && !additions.empty())
            for (const auto& a : additions) pushBlock(a);
        lines.swap(result);
    }

    // Serialize back (patched is always the full new text so callers
    // can use it uniformly; changed signals whether bytes differ).
    rep.patched.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) rep.patched += '\n';
        rep.patched += lines[i];
    }
    // Preserve the original trailing newline semantics.
    if (!rep.patched.empty() &&
        (original.empty() || original.back() != '\n'))
        rep.patched.pop_back();
    return rep;
}

} // namespace livecode
} // namespace yawn
