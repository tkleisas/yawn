#pragma once

// LiveCodeSong — the declarative `song` layer (docs/live-coding.md §5):
// model, Lua parser, and the diff-apply engine that reconciles project
// state to the script's declaration.
//
// Field-level ownership contract: a key's PRESENCE in a track declaration
// means "the song owns this field"; its absence means "don't touch".
//   * instrument present (even with empty id) → the instrument is managed
//     (empty id removes it); absent → untouched
//   * fx present → the chain's first N slots are managed to match the
//     declared list; an EMPTY declared list clears the whole chain;
//     slots beyond N are left alone (v1)
//   * clips present → the declared scenes' slots are managed; absent
//     scenes untouched. Scene numbers are 1-based in Lua.
//   * volume/pan/mute/solo/name → managed when the key is present
//
// Track matching: uid (exact) > name > create-new. Adopted tracks become
// script-owned (the caller's owned-uid set tracks them; a track owned by
// a previous apply but no longer declared is removed — transport stopped
// only, and v1 only supports removing the last project track).
//
// Everything the applier emits goes through the same edit primitives the
// UI uses (commands, setInstrument swap, Project setters, graveyards) —
// see the §5.2 table. Undo: song applies are excluded from undo in v1
// (the code IS the undo mechanism — re-run converges).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct lua_State;

namespace yawn {

class Project;
namespace audio { class AudioEngine; }
namespace midi { class MidiClip; }

namespace livecode {

// ── Model ────────────────────────────────────────────────────────────────

struct SongParam {
    bool byIndex = false;
    int index = 0;
    std::string name;
    double value = 0.0;
};

struct SongDevice {
    std::string id;              // Factory id ("granular"); "" = remove
    std::vector<SongParam> params;
};

struct SongNote {
    double start = 0.0;          // beats from clip start
    double dur = 0.25;           // beats
    double pitch = 60.0;
    double vel = 0.8;            // 0..1 (→ 16-bit)
    int ch = 0;
};

struct SongClip {
    double beats = 4.0;
    std::vector<SongNote> notes; // presence of the notes key = authoritative
};

struct SongTrack {
    std::string uid;             // decimal string of the uint64 uid; "" = none
    std::string name;
    bool hasName = false;
    std::string type;            // "audio"|"midi"|"visual"; "" = don't touch
    std::optional<double> volume;
    std::optional<double> pan;
    std::optional<bool> mute;
    std::optional<bool> solo;
    bool hasInstrument = false;
    SongDevice instrument;       // valid when hasInstrument
    bool hasFx = false;
    std::vector<SongDevice> fx;  // valid when hasFx (empty list = clear chain)
    bool hasClips = false;
    std::map<int, SongClip> clips;   // key: 1-based scene number
};

struct SongModel {
    std::optional<double> bpm;
    std::optional<int> scenes;   // grow-only in v1
    std::vector<SongTrack> tracks;
};

// ── Parser (Lua side) ────────────────────────────────────────────────────

// Parses the `song` global table (at stack index `idx`) into a model.
// Returns false with `err` set on malformed input (type errors, bad ids).
bool parseSongModel(lua_State* L, int idx, SongModel& out, std::string& err);

// Parses a notes array ({start, dur, pitch, vel, [ch]} tuples, positional
// or named) — shared by the song layer and the yawn.set_notes API.
bool parseNotesInto(lua_State* L, int idx, std::vector<SongNote>& out);

// ── Applier (C++ side) ───────────────────────────────────────────────────

struct SongApplyContext {
    Project* project = nullptr;
    audio::AudioEngine* engine = nullptr;
    // App-provided hooks — the paths that need App-level coordination.
    std::function<void()> engineSync;             // App::syncTracksToEngine
    std::function<void()> markDirty;
    // Live re-point of a MIDI clip (App::setMidiClipLive). When unset,
    // falls back to Project::setMidiClip (safe only when the slot isn't
    // playing).
    std::function<midi::MidiClip*(int, int, std::unique_ptr<midi::MidiClip>)>
        setMidiClipLive;
};

struct SongApplyReport {
    std::vector<std::string> ops;       // human-readable applied ops
    std::vector<std::string> warnings;  // unsupported/skipped
    bool changed = false;
};

// Reconcile the project to `song`. `owned` is the caller-owned set of
// script-owned track uids, updated in place (created/adopted uids added;
// removed tracks' uids dropped).
SongApplyReport applySongModel(const SongModel& song,
                               std::set<uint64_t>& owned,
                               const SongApplyContext& ctx);

// ── Code lens (phase 6, §5.4 stage 1) ───────────────────────────────────
// Regenerate the declarative `song = {...}` Lua source from live project
// state. Read-only projection: parse(generate(project)) applied back is
// a no-op (tested confluence). Emits: bpm, scene count, and per track:
// uid, name, type, volume/pan/mute/solo (only when meaningful), the
// instrument (all params by name), the FX chain (all params), and MIDI
// clips (notes as positional tuples). Audio/visual clips are listed as
// comments (their content lands in code in later phases).
std::string generateSongSource(const Project& project,
                               const audio::AudioEngine& engine);

} // namespace livecode
} // namespace yawn
