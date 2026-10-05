# Live Coding

*Status: implementation document; phases 0–3, 5, 6 landed (see §10). Post-v1 items remain in §5.4/§7.*

YAWN gains a live-coding layer with two roles: **code as a representation of
the project** (a declarative `song` section that stays synchronized with the
UI, even for non-livecoding projects) and **code as a performer**
(an imperative `improv` section that triggers the project's samples,
generates clips and parameters, and renders audio offline on worker threads
for use as clip content or sampler/granular raw material). The audio thread
stays pure C++ — scripts never run on the PortAudio callback.

## 1. Goals and non-goals

**Goals**

- G1 — Project-as-code: a declarative, deterministic Lua description of the
  project (tracks, channels, instruments, effects, clips, mixer) that the
  UI can regenerate and that can be applied to the project by a diff
  engine. UI manipulation is turned into code; code manipulation is turned
  into edits. Works for any project, livecoding or not.
- G2 — Perform live: trigger the current project's samples/clips/instruments
  from code, with sample-accurate musical scheduling (lookahead + beat
  anchoring).
- G3 — Author from code: set MIDI clip notes, instrument and effect
  parameters (by name or index), mixer state, track/instrument/effect
  creation — always through the same edit primitives the UI itself uses.
- G4 — Prerender: programmatically render audio (a synth patch, a seeded
  generative process, an instrument+FX chain fed by generated notes) on a
  **worker** thread, with delivery into a clip slot, a Sampler/Granular/
  DrumRack pad, the samples folder, the library, or a file.
- G5 — Reproducibility: scripts live inside the project, prerendered audio
  and seeds are persisted, and every edit applied through code is a real
  project edit the engine already understands.
- G6 — Safe iteration: hot reload with state migration; a broken script
  never stops the show (last-good state keeps running, error is shown).

**Non-goals (v1)**

- N1 — No DSP-in-Lua on the audio callback (no real-time script effects or
  script voices). The offline prerender worker is the sanctioned escape hatch
  — *render then trigger*, never *render while playing*.
- N2 — No in-app code editor in v1: scripts live in project-local files,
  edited in an external editor, hot-reloaded on save (same workflow as
  visual shader clips). An fw2 code editor is a later, separate project.
- N3 — No full two-way round-trip in v1: stage 1 ships a read-only **code
  lens** (UI → code regeneration, view-only) plus runnable hand-edits
  (code → diff-apply). Comment/formatting-preserving template rewriting
  (true round-trip) is a later stage — see §5.4.
- N4 — No user-visible 4th scripting engine. Live coding reuses Lua 5.4,
  which is already vendored (`third_party/lua54`) and proven in two other
  roles (controllers: `src/controllers/LuaEngine.cpp`; 3D scene scripts:
  `src/visual/gltf/M3DSceneScript.h`).

Decisions made along the way (reversible, but keep them consistent):

- **Conductor model** — code orchestrates the engine; C++ makes sound. Every
  sound-producing and structural operation goes through existing command
  paths (`src/util/MessageQueue.h`, `App`-side helpers) rather than direct
  audio-thread access (see §2).
- **Two-layer file** — one script file, a declarative `song` section (the
  project, round-trippable, diff-applied) and an imperative `improv`
  section (performance/generative code; its *results* materialize, the code
  itself is not derived from state). See §5.1.
- **Script is the source of truth** — within the `song` layer, the code
  defines what exists; the diff engine converges project state to the
  script (idempotent re-run = no-op). User UI edits remain undoable
  ordinary edits and are reflected back into regenerated code. See §5.2.
- **Lookahead scheduling** — notes are anchored to transport beats
  (`at_beat`) and sent early from the UI scheduler; the audio thread holds
  only a bounded near-term pending queue. Sample-accurate note timing is a
  phase-1 property, not a later refinement. See §3.2.
- **Params by name or index** — string names resolve case-insensitively at
  call time (or at enqueue time for prerender specs); indices remain valid
  everywhere. See §6.2.
- **Editor-first workflow** — external editor + filesystem watch.

## 2. Model: who runs where

The DAW already has a proven conductor precedent: controller scripts run Lua
on the UI thread and talk to the engine exclusively via (a) the lock-free
command queue and (b) RT-safe getters. Live coding is that pattern plus
musical scheduling, worker-thread rendering, and project reconciliation:

```
┌──────────────────────────────────────────────────────────────────┐
│ UI/main thread                                                    │
│                                                                   │
│   App::update() ── drain AudioEvent queue ──> LiveCodeManager     │
│   (SDL main)         (beat clock msgs)          lua callbacks     │
│   Injection↑ minimal: few per bar callbacks, dispatch cost ~µs    │
│                                                                   │
│   improv performer API ──> AudioCommand queue ──> AudioEngine(RT) │
│   diff-apply (song section) ──> App-side edit primitives          │
│        (setInstrument swap, ChainBase snapshot, quiesce,          │
│         MidiClip clone-swap, graveyard)                           │
│   code lens: project snapshot ──> regenerate song section         │
│                                                                   │
│   PrerenderManager ──> worker pool threads                        │
│   (offline instrument/FX renders, file I/O; never touches the     │
│    PortAudio stream)                                              │
│                                                                   │
│ Audio/RT thread (pure C++ DSP, script-free, alloc-free —          │
│ tests/test_RtAllocFree.cpp discipline)                            │
└──────────────────────────────────────────────────────────────────┘
```

Key contract points (all already exist in the codebase — the design reuses
them rather than invents new crossing rules):

- **Commands**: `AudioCommand` variant (`src/util/MessageQueue.h:347`) —
  transport, mixer, MIDI-to-track, clip launch/swap, FX params.
- **Structural edits UI-side** — instrument replacement via
  `AudioEngine::setInstrument` publish-then-retire swap
  (`src/audio/AudioEngine.h:230`); effect-chain mutation via the same
  ChainBase RT-snapshot pattern used by all device edits; clip-slot edits via
  the Project clip-graveyard + `quiesceCommands()` handshake
  (`src/audio/AudioEngine.h:156`).
- **Sample buffers** — `Sampler::loadSample`/`publishSample`,
  `GranularSynth`, `Vocoder`, `DrumRack` pads: UI-thread builds the new
  buffer, atomically publishes, parks the old in `RtRetireList`
  (`src/instruments/Sampler.h:42`).

The scripts may also *read* project/engine state (track counts, param
values, metering, transport) — the same access points the controller API
uses today; reads stay on the UI thread, between frames.

## 3. Engine & scheduling design

### 3.1 LiveCodeEngine

A new `lua_State` (separate from, and alongside, the controller engine — a
broken controller must not kill the show and vice versa) owned by a
`LiveCodeManager` mirroring `ControllerManager`'s wiring style:
engine/project pointer injection, command sender, callback getters.

Sandboxing (unlike controller scripts, live code shares the process with the
user's project):

- selective `openlibs`: base/math/table/string only; `io`, `os`, `package`,
  `debug`, `dofile`, `loadfile` removed. No `require` in v1 (scripts are
  single-file in project storage; a later include pattern can be added).
- every callback invoked via `pcall`; runtime error → keep last-good state,
  surface the message in the code console + toast. Same error contract as
  `M3DSceneScript::tick` and controller `on_midi`.
- instruction budget: `lua_sethook` count-based guard (LUA_MASKCOUNT,
  e.g. 10M instructions per dispatched callback run) so a runaway loop
  strands nothing; on breach the generation is failed hard, rollback
  semantics per generation (§3.3), console shows the error.

### 3.2 Lookahead scheduling & note timing

The engine emits `TransportPositionUpdate` at ~30 Hz from the audio thread
and `App::update()` dispatches a `std::visit` over the event variant
(`src/app/App_Frame.cpp:397`). The scheduler rides that stream:

- **UI-side long-term memory**: `every/at/after` entries live in the
  `LiveCodeManager` scheduler (generation-tagged). Each frame, entries
  whose target beat falls within the lookahead horizon are materialized
  into commands.
- **Audio-side precision insertion**: `SendMidiToTrackMsg` gains one
  optional field:

  ```cpp
  double atBeat = 0.0;   // transport-beat target; 0 = asap (next block)
  ```

  `processCommands()` (`src/audio/AudioEngine.cpp:811`) runs at block
  start and knows the block's beat window. A message whose `at_beat`
  falls inside the current block is inserted into the track's
  `midi::MidiBuffer` with `MidiMessage::frameOffset` — that field already
  exists (`src/midi/MidiTypes.h:91`) and per-block sample-accurate events
  are an established concept (`MidiBuffer::sortByFrame`). Messages
  targeting future beats go into a small preallocated pending queue on
  the audio thread (bounded ring, sized to the lookahead horizon — the
  UI side is the long-term memory, so the audio side never grows).
- **Note-off**: `dur_beats` schedules the paired NoteOff through the same
  queue (musical anchoring, not wall-clock gate).
- **Late policy**: if a command misses its lookahead window (UI hitch,
  stall), it defaults to *play now*; a global `improv.late_policy("play" |
  "drop")` switches to drop+console-warning with a per-second late count.
  Quantization sugar (`quantize="bar"|"beat"`) is resolved UI-side.
- **Transport interaction**: tempo changes do not invalidate queued notes
  (anchored to beats, not wall time); transport jumps/loops wrap handled
  by the UI scheduler computing the *next* occurrence of a pattern target;
  a manual seek or loop jump flushes the audio-side pending queue (a
  `FlushScheduledNotesMsg` command).
- **Callback-entry jitter** (≤ one UI frame) affects only *imperative*
  code that acts "now" inside `on_bar`; anything scheduled via the
  primitives above is sample-accurate. The `MusicalPositionEvent` idea
  (engine-emitted subdivision events) is therefore demoted to a
  phase-6 refinement: it improves callback entry timing and
  automation-sweep starts, not note accuracy.

### 3.3 Generations (the classic cycle)

```
run()  → captures a new "generation" id; presents it as the env for any
         callback the script registers during the run. (UI thread)
…     → old-generation callbacks persist until the next generation swaps
         them at the next bar boundary (never mid-bar).
reload → file watcher or explicit reload → re-evaluates script → bump
         generation again.
stop() → drops all scheduled callbacks (scheduled notes already enqueued
       into the command queue are allowed to fire; nothing new is
       scheduled).
```

Callbacks are closures over user data; when a generation is dropped, its
scheduled-but-not-yet-fired closures go with it (the scheduler holds only
generation-tagged wrappers). Persisted *state* survives reloads via an
explicit table:

- `yawn.state` — host-managed table, preserved across reload semantics:
  the engine serialises shallow values (numbers/strings/booleans/small
  tables) out of the old state and into the new state between gens, by
  key, with a hook `function accept_state(new_state)` for bespoke
  migration (the analogue of shader params surviving hot reload by name).

### 3.4 Scheduler primitives

```lua
improv.every(beats, fn, opts)     -- returns handle; opts { start, repetitions }
improv.at(bar, sub, fn)           -- future scheduling, absolute
improv.after(beats, fn)           -- one-shot
improv.on_bar(fn)                 -- sugar, coarse
improv.clear(handle) / improv.clear_all(genId)
improv.lookahead(seconds)        -- horizon override (default 0.10 s)
improv.late_policy("play" | "drop")
```

`every` replace semantics: repeated `every` with the same function identity
replaces the previous (common live-coding expectation), each distinct fn
yields its own transport slots. Old-generation callbacks stay valid until
their replacement generation is live, so re-running code never produces a
silent bar.

## 4. Prerender system

### 4.1 InstrumentRenderer (new, `src/audio/Prerender.h`)

A fresh-instrument renderer, deliberately independent of the live engine:

```cpp
struct PrerenderSpec {
    std::string device;                            // factory id, e.g. "granular"
    std::vector<std::pair<int,float>> params;      // (param index, value) — names
                                                   // resolved at ENQUEUE time
    std::vector<std::string> effects;              // chain of factory ids
    std::vector<PrerenderNote> notes;              // {beat, lengthBeats, pitch, vel, ch}
    std::function<bool(double beat, midi::MidiBuffer&)> noteSource;
                                                   // generative sources (offline)
    uint64_t seed = 0;                             // determinism contract
    double tempoBpm;
    double lengthBeats, tailBeats;                 // +release tail
    int sampleRate, channels;
};
```

Execution on the worker pool:

1. `Factory.h` (`createInstrument`/`createAudioEffect`) builds fresh
   instances (single source of truth — no separate Lua device table).
2. `init(sr, 256)`, then block-by-block: build `midi::MidiBuffer` for the
   block's beat range from `notes`/`noteSource`, `EffectChain::process`,
   accumulate into `AudioBuffer` (non-interleaved).
3. `RenderProgress` atomics (fraction/done/cancelled/failed) identical in
   shape to the existing `OfflineRenderer` (`src/audio/OfflineRenderer.h:20`)
   so the UI progress plumbing is a copy.
4. Finishing: normalize (optional), deliver to target on the UI thread.

This does **not** touch the PortAudio stream — offline rendering of the live
mix stays with the existing `OfflineRenderer` (which stops the stream; the
UI already does the same for bounces, and live-coding exposes it only with
a "will pause audio" flag / not at all in v1).

### 4.2 PrerenderManager + jobs

Single `PrerenderManager` (in `yawn_core`, no GL): queue + N=2 workers +
per-job cancellation, no live Lua handles — each job gets a value-copied
`PrerenderSpec` (plain data) at enqueue time and a shared completion posted
back to the UI event loop; a `yawn.on_prerender_done(id, ok, path)` optional
callback is dispatched like the musical callbacks. Jobs must be
deterministic (spec is evaluated at *enqueue* time; the seed is fixed in
the spec) because determinism is what makes "re-render this sound" and
save/load round-trips reliable.

### 4.3 Delivery targets (UI thread)

- `{kind="sampler"|"granular"|"drumrack", track=n, pad=k}` → the existing
  publish/retire loadSample path (`src/instruments/Sampler.h:42`,
  `GranularSynth::loadSample`, DrumRack pad).
- `{kind="clip", track=n, scene=m}` → make `audio::Clip` + `setClip` +
  `syncTracksToEngine` — the same path stem separation uses
  (`src/app/App.cpp:1337`) — **and** persist via `util::saveAudioBuffer`
  into the project's `samples/` dir so the project stays loadable on
  disk; the buffer file path becomes part of the declarative song section.
- `{kind="library", name=...}` → content-library sample insertion.
- `{kind="file", path=...}` → direct save (WAV/FLAC via `util::FileIO`).
  For scratch/curiosity experiments.

### 4.4 Procedural / generative sound sources (offline Lua scripts)

`PrerenderSpec` may carry `noteSource` — a **worker-thread** Lua chunk
evaluated by a *fresh* sandboxed `lua_State` on the worker thread (no
interpreter sharing, no cross-thread risk), driven in block-sized calls:
`noteSource(beat, beatsPerBlock, midiOut, rng)` fills the MidiBuffer for
its window. `rng` is the host-seeded generator, so `seed` keeps
generative renders reproducible. Because it is offline, GC pauses are
irrelevant. This satisfies "code as a sound source" without ever running
Lua on the RT callback.

### 4.5 Samples-into-track pipeline summary

```
project wants audio from live code      →  PrerenderManager::enqueue
  (1) spec built (plain data; names→indices resolved UI-side)
  (2) worker: fresh device via factory, block loop render, AudioBuffer out
  (3) UI: clip-from-buffer (saved to samples/ + setClip + engine sync)
      or loadSample publish/retire into Sampler/Granular/DrumRack
      (and the buffer path is written into the song section)
```

## 5. Project-as-code: the `song`/`improv` split

### 5.1 Two-layer file

```
project.yawn/
  project.json          # ordinary project (unchanged format + uid additions)
  livecode/
    main.lua            # song section + improv section
    *.lua               # additional scripts (each song/improv-capable)
```

```lua
-- ── song (declarative) ──────────────────────────────
-- This section IS the project. Regenerated by the UI (code lens),
-- applied by a diff engine. Deterministic data only: no closures,
-- no randomness without seeds, no wall-clock reads.
song.bpm   = 124
song.scenes = 8
song.tracks = {
  { name="Kick", uid="t1", type="midi", volume=1.0,
    instrument = { id="drumrack", params={ tune=0.5 } },
    fx  = { { id="reverb", params={ decay=0.4 } } },
    sends = { { bus=0, level=0.3, mode="post" } },
    clips = { [2] = { beats=4,
                      notes={ {0,0.5,36,1}, {1,0.5,38,0.7} } } } },
}
song.samples = { pad = "samples/pad_01.wav" }  -- refs, incl. prerendered

-- ── improv (imperative) ─────────────────────────────
-- Performance/generative code. NOT round-tripped. Results materialize
-- (recorded clips, prerendered buffers, param values), but this code
-- is not derived from project state.
improv.on_bar(function(bar)
  yawn.note("Kick", 36, 1.0, 0.5, 0, bar*4)
end)
```

- `song` contains no logic. Its Lua "execution" is parsing into a
  normalized in-memory model (loaded, not run as commands).
- `improv` is the conductor layer (§3) unchanged.
- Materialization rules: improv-initiated param/structure changes
  materialize as ordinary project state (and are reflected in regenerated
  code); live note bursts materialize only when recorded (existing
  recording paths); prerendered buffers materialize as samples + song
  refs. Generative prerender must be seeded (§4.4) to keep song refs
  reproducible.

### 5.2 Diff-apply engine (the new machinery)

Algorithm: parse `song` into the normalized model → diff against the live
`Project` snapshot → emit a minimal edit list → apply through existing
edit primitives → regenerate. Re-running an unchanged file is a **no-op**
(idempotence contract). Edit primitives already exist (see §2 contract
points): add/remove/reorder track, `setInstrument` swap, effect-chain
edits, `SetEffectParamMsg`, mixer commands, `MidiClip` clone-and-swap +
`SwapMidiClipMsg`, `audio::Clip` buffer assignment, launch commands.

**Ownership policy (script is the source of truth):**

- UI edits: always ordinary undoable edits. Undo history stays user-only.
- diff-apply runs: one coalesced "Apply live code" undo entry per run (or
  excluded from undo in v1 — console note; decided at implementation).
- If the user deletes a script-owned track in the UI, the script
  re-asserts it on the next apply (that is what source-of-truth means).
  To remove something permanently: remove it from `song`, or stop the
  script. A `freeze` affordance (convert script-owned → user-owned) can
  be added later.
- Untagged items created by improv code become user-owned once created
  (no automatic reaping); `song`-declared items are always script-owned.

### 5.3 Identity (schema prerequisite)

Everything is index-addressed today — `Track` has no uid
(`src/app/Project.h:21`), devices are `(track, chainIndex)`, clips are
`(track, scene)` — and indices shift on remove/reorder, which makes diffs
non-confluent. For reconciliation, `song` addresses *things*:

- `Track.uid` — assigned at creation, persisted in `project.json`,
  invisible to the rest of the engine (indices remain the internal
  currency).
- device chain entries get an optional uid (needed only for move/reorder
  diffs); `name` matches `DeviceDescriptor::className` already.
- clip identity stays `(track uid, scene)` — already stable.
- arrangement clips need an id field (same one-line addition).

Lua sugar resolves uids: `song.track("Kick")` → proxy carrying uid;
`improv.note("Kick", ...)` likewise.

### 5.4 Round-trip fidelity (staged)

Regenerating the `song` section clobbers hand-written comments/formatting.
Stages:

1. **Code lens (v1)** — read-only regeneration (on code-view open, on
   save, or throttled if "live sync" is on) + runnable hand-edits that
   apply via diff. Zero clobbering risk; hand edits apply and then the
   regen includes them (they are project state by then).
2. **Template-preserving rewrite (later)** — only value literals inside
   recognized key positions are rewritten; comments/structure untouched.
   Requires a real Lua parser (the declarative section has a fixed shape,
   so this is tractable but fiddly).
3. **Accepted clobbering** — explicitly rejected as a shipped UX.

## 6. Persistence & API surface

### 6.1 Persistence

```json
"livecode": {
  "format": 1,
  "enabled": true,
  "scripts": [
    { "name": "main",
      "path": "livecode/main.lua",
      "autorun": "on_project_load" }
  ]
}
```

- On load: scripts are not auto-run unless `autorun` says so; a toast +
  status chip shows "livecode: N scripts loaded, idle/running". Explicit
  RUN/STOP stays the primary gesture; autorun is opt-in per script.
- Structural edits performed via diff-apply are ordinary project edits
  (same serializers) — a saved project reproduces what the code created
  even with livecode disabled on next load.
- Prerendered audio persists as project samples; seeds persist in the
  song section (§4.4).
- Hot reload: `LiveCodeManager` polls script mtimes per frame — the same
  cheap poll as `M3DSceneScript::pollHotReload`
  (`src/visual/gltf/M3DSceneScript.h:98`).

### 6.2 Parameter addressing (name + index)

- Both accepted everywhere a param is addressed: `yawn.set_param(track,
  kind, ci, pi_or_name, value)`, `yawn.param(...)`, `yawn.param_display(...)`.
- Numeric = direct index (unchanged semantics). String = case-insensitive
  first match against `parameterInfo(i).name`.
- A miss is an error that **lists the device's valid names** (live-coding
  DX) — from `yawn.param_names(track, kind, ci)`.
- Name→index map: lazy per device instance, built once (tables are
  static); UI thread only.
- Prerender specs resolve names → indices at enqueue time on the UI
  thread; worker specs stay integer-only.
- Internal consumers (automation, MIDI Learn) stay index-based; names are
  scripting sugar only.

### 6.3 API surface (v1 target)

Naming follows the controller-script table style (docs/controller-scripting.md).
New or changed functions, grouped:

```lua
── song (declarative layer)
song.bpm / scenes / tracks / samples        -- table, diff-applied
yawn.apply_song()                            -- run diff-apply explicitly
yawn.song() -> current song model (read-only mirror)

── improv scheduling
improv.every / at / after / on_bar / clear / clear_all
improv.lookahead(seconds) / late_policy("play"|"drop")
yawn.state                                   -- persistent-across-reload table

── notes & midi (improv)
yawn.note(track, pitch, vel, dur_beats, ch, at_beat)   -- schedule or fire
yawn.note_off(track, pitch, ch, at_beat)               -- explicit release
yawn.set_notes(track, scene, table, opts)              -- author a MIDI clip
yawn.clear_notes(track, scene)                         -- empty the clip

── devices & params (performer)
yawn.set_instrument(track, id)               -- swap, Factory ids
yawn.add_effect(track, id, [chain])          -- audio effect chain append
yawn.remove_effect(track, chainIndex)
yawn.param(track, ["instrument"|"fx"], ci, pi_or_name) -> value
yawn.set_param(track, kind, ci, pi_or_name, value)
yawn.param_names(track, kind, ci) -> table   -- discovery
yawn.param_display(track, kind, ci, pi_or_name) -> string
yawn.set_track_volume/pan/mute/solo/send (mostly existing controller API)

── midi clip authoring
yawn.new_midi_clip(track, scene, length_beats)
yawn.midi_clip(track, scene) -> handle {notes(), set_notes(), clear(), ...}

── pattern notations (strings → clip specs)
yawn.midi("A1_16B1_16D#3_8")                 -- melodic phrase → {beats, notes}
yawn.drums{ BD="x---x---x---x---", ... }     -- step grid → {beats, notes}

── prerender
yawn.render(spec, [callback]) -> job_id      -- spec params by name or index
yawn.cancel_render(job_id)

── structure (session)
yawn.add_track(name, type, [uid_tag]) / remove_track / set_track_name
yawn.launch_clip / launch_scene / stop_clip  -- existing controller API

── transport (from controller API, same semantics)
yawn.is_playing / set_playing / get_bpm / set_bpm / get_loop / set_loop

── metadata & IO
yawn.log / toast                             -- existing
yawn.project_samples_dir() / yawn.load_audio_file(path) -> handle
yawn.save_audio_buffer(handle, path, opts)
```

Everything is additive to the controller API namespace **except** that the
live engine is a separate `lua_State` with a *wider* surface (controller
scripts keep the narrower, sandboxed surface they have today).

## 7. Phases

| Phase | Ships | Rough size |
|---|---|---|
| 0 | `LiveCodeEngine` + manager, scheduler core on the transport event stream, project-attached script file + RUN/STOP keybind, console + error toasts, sandbox+hooks | S-M |
| 1 | Performer API v1: transport, notes with `at_beat` lookahead scheduling (sample-accurate via audio-side pending queue), device params, mixer, launch_* | M |
| 2 | Diff-apply engine + declarative `song` section; Track/device/arrangement-clip uids; reconcile = idempotent no-op on unchanged files | M-L |
| 3 | Prerender: `InstrumentRenderer`, `PrerenderManager`, delivery targets (sampler/clip/library/file), progress UI, seeded generative sources | M-L |
| 4 | MIDI clip authoring (clone-swap), sampler/granular/drumrack loaders, `load_audio_file/save_audio_buffer` | M |
| 5 | Persistence (`livecode/` in projects), reload lifecycle + state migration, autorun | S-M |
| 6 | Code lens: read-only `song` regeneration + code console polish (gen indicator, queue state, per-callback errors), `MusicalPositionEvent` refinement if callback-entry timing needs it | M |
| 7 | Template-preserving two-way round-trip (parser-rewrite of value literals) | L — post-v1 |
| 8 | In-app code editor (fw2) | L — post-v1 |

Testing (gtest, tests/): scheduler math (beat anchoring, generation
replacement, late policy, loop wrap), `SendMidiToTrackMsg` at-beat merge +
frameOffset ordering, diff engine confluence (repeated apply = no-op;
reorder/remove diffs), uid stability across save/load + track removal,
prerender determinism (fixed tempo + note set + seed → bit-identical
buffer), sampler publish/retire during prerender delivery, project
save/load round trip of `livecode`, sandbox escape tests (io/os removed,
budget breach), name→index resolution (ambiguous/missing names), zero-alloc
audit of the delivery path unchanged.

## 8. Integration with the existing scripting layers

- Controller scripts and live code are independent `lua_State`s; a crashed
  controller script never kills the show and vice versa.
- Shared device/track/param functions should be factored into a common
  registration builder so the two engines cannot drift (controllers get
  the narrower surface selected at registration).
- Visual `M3DSceneScript` keeps its own per-clip state untouched; if a live
  script later wants to drive visuals, the existing `VisualNoteBus` /
  `VisualModBus` singletons are the sanctioned read path — no new plumbing.

## 9. Risks and mitigations

- **Script hogs the UI thread** → instruction budget + short callbacks;
  heavy work moved to prerender workers.
- **Diff engine divergence (code ≠ state)** → idempotence contract is a
  tested invariant (repeated apply = no-op); regen always runs from the
  live Project snapshot, never from a cached model.
- **Uid migration** — existing projects lack uids; loader assigns uid on
  first save after upgrade (transparent, one-time).
- **Hand-edit regen clobbering** → staged round-trip (§5.4): code lens
  first, template rewrite later; never ship accepted-clobbering.
- **Two Lua engines drift** → shared `registerAPI` core functions factored
  into a common builder so tracks/device/param functions exist in exactly
  one implementation.
- **Scheduling feels late at high subdivision densities** → lookahead
  scheduling is the mechanism; late-policy counters make misses visible;
  measure with a test-tone + oscilloscope device before promising musical
  guarantees beyond v1 (§3.2).
- **Prerender jobs eating the machine during perform** → N=2 workers,
  per-job cancel, SCHED_OTHER semantics; explicitly not throttled to audio
  (offline is offline).
- **Decision drift in the doc itself** — this file should be updated when
  implementation confirms or refutes a design assumption (esp. §3.2
  timing numbers and §5 schema), so it stays the honest reference.

## 10. Implementation status

**Phase 0 — landed.** What exists:

- `src/livecode/LiveCodeScheduler.{h,cpp}` — beat-anchored scheduler:
  one-shot absolute/relative, recurring with missed-batch skipping,
  bar-grid entries (1-based bar number passed to the callback), wall-axis
  entries (held-note releases while stopped), jump-back realignment
  (loop wrap/seek), generation clearing, remove-listener for Lua-ref
  release. Fully unit-tested without transport dependencies.
- `src/livecode/LiveCodeEngine.{h,cpp}` — sandboxed Lua 5.4 state
  (`io/os/package/debug/dofile/loadfile/require` removed; base/math/table/
  string kept), count-based instruction budget (50M per file run, 250k per
  callback dispatch) via `lua_sethook`, `yawn.*` subset (`log`, `toast`,
  `note`, `note_off`, transport get/set) and `improv.*` scheduling table,
  `yawn.state` harvest/inject (shallow: primitives + one-level tables).
- `src/livecode/LiveCodeManager.{h,cpp}` — RUN/STOP/reload lifecycle,
  generation swap at next bar boundary (immediate when stopped), failed
  run keeps the previous generation (last-good state), note-off entries
  are generation-0 (system) so STOP doesn't strand held notes, auto-cancel
  of callbacks after 3 consecutive dispatch errors, console ring (256
  lines) + toasts.
- Wiring: `App` owns `m_liveCode`; `update()` ticks it next to
  `m_controllerManager.update()`; keybinds **Ctrl+L** (run/stop toggle),
  **Ctrl+Shift+L** (reload from disk); Live Code menu; shortcuts dialog
  entry. Script path: `<project>/livecode/main.lua` for saved projects,
  else `~/.yawn/livecode/main.lua`; first RUN creates a commented template.
- Tests: `tests/test_LiveCode.cpp` (21 cases — scheduler math, sandbox,
  budget breach, state migration arithmetic, note dispatch, stop
  semantics). Full suite green (1493 tests).

Divergences from the design doc as written:

- The Lua state persists across RUN gestures (closures/locals survive) —
  `yawn.state` migration applies only on explicit reload / state
  recreation. §3.3's "generations swap at next bar" applies to scheduled
  callbacks only, which the scheduler enforces directly.
- Error toasts for repeated callback errors are rate-limited by
  auto-cancel (3 consecutive failures disable the callback) rather than a
  one-per-second toast throttle; the console records every occurrence.

**Phase 1 — landed.** Sample-accurate at-beat note scheduling (§3.2 as
designed, with one refinement):

- `SendMidiToTrackMsg` gained `double atBeat` (0 = fire-now, as before).
  `processCommands()` classifies each message against the current block's
  beat window: inside → inserted into the track's `MidiBuffer` with the
  matching sub-block `frameOffset`; future → parked in a bounded
  (1024-entry, heap-backed) pending queue on the audio thread; past (UI
  hitch longer than the lookahead) → late policy. Counter overflow /
  late-drop / flush counts are atomics with test accessors.
- The pending queue drains after the Link tempo sync (so at-beat → sample
  conversion uses the settled tempo), merges into `m_liveInputMidi` (so
  scheduled notes flow through the same pre-effect recording capture as
  keyboard input), and is flushed on seeks (`TransportSetPositionMsg`)
  and loop wraps (beats restarted — parked notes are stale).
- `LiveCodeScheduler` gained the schedule-ahead window
  (`improv.lookahead(seconds)`, default 0.10 s, converted to beats per
  tick): beat-axis callbacks now fire *early*, receiving their upcoming
  target beat (every/at/after) or bar number (on_bar) so scripts can pass
  it to `yawn.note`'s new 6th `at_beat` parameter for sample-accurate
  placement. Late policy `improv.late_policy("play"|"drop")` covers both
  the scheduler (skip/realign) and the audio queue (play-now/drop) via
  `SetSchedLatePolicyMsg`.
- Note-offs for fire-now notes with a duration are beat-anchored in the
  audio queue when playing (replacing the phase-0 scheduler beat entry);
  the wall-axis fallback remains for stopped transports. STOP still
  releases already-triggered notes (generation-0 semantics preserved —
  audio-queue note-offs are generation-free by construction).
- Tests: +9 cases (scheduler lookahead/late/target-beat args,
  headless `pumpInputForTest` parking/draining/flush/late-policy E2E,
  manager→queue note flow). Full suite green (1502 tests).

**Phase 2 — landed.** The declarative `song` layer and diff-apply engine
(§5 as designed, with v1-scope limitations listed below):

- **Stable identity (§5.3)**: `Track.uid` (`src/app/Project.h`) assigned
  at creation (`addTrack`/`init`), persisted in `project.json`
  (`serializeTracks`/load loop), and backfilled one-time on load for
  legacy projects. `findTrackByUid/findTrackByName` accessors.
- **Model + parser**: `src/livecode/LiveCodeSong.{h,cpp}` — `SongModel`/
  `SongTrack`/`SongDevice`/`SongClip` + `parseSongModel` (name-index
  mixed params, positional-or-named notes, 1-based scenes). Key
  *presence* = field ownership, absence = untouched (instrument, fx,
  clips, volume/pan/mute/solo/name).
- **Diff-apply (§5.2)**: `applySongModel` — track match by uid > name >
  create (adopted tracks become script-owned; owned-but-undeclared
  tracks are deleted — transport stopped, last-track only in v1, with
  stop+quiesce), BPM via `TransportSetBPMMsg`, mixer fields via Project
  copy + commands, instrument via the `setInstrument` swap + Factory
  ids, FX prefix management via `EffectChain::insert/removeRetired`
  (declared empty list clears the chain), MIDI clips via
  `App::setMidiClipLive` hook (project fallback). Param addressing by
  name (case-insensitive) or index — misses warn with the valid-name
  list (§6.2). Applies bypass undo in v1 (console-noted).
- **Integration**: after each successful RUN the manager harvests the
  `song` global and reconciles before scheduling the generation swap;
  improv-only scripts (no `song`) skip the layer entirely. App wires
  `syncTracksToEngine`/`markDirty`/`setMidiClipLive` hooks.
- Tests: +9 cases (uid round-trip + legacy backfill, parse basics and
  errors, apply idempotence/confluence, adopt-by-name, owned-track
  removal, clip replace + authoritative clear, manager E2E). Full suite
  green (1511 tests).

Phase-2 v1 limitations (tracked for 2b):

- Track removal only when transport is stopped and only the last track;
  scene count is grow-only; type changes on existing tracks unsupported;
  sends not yet parsed (warned); `song.samples` audio-clip refs land in
  phase 4.

**Phase 3 — landed.** The prerender system (§4):

- **`src/audio/Prerender.{h,cpp}`** — `PrerenderSpec` (device id,
  resolved `(index,value)` params, FX chain with per-effect params,
  note list, optional C++ `noteSource` generative callback, seed, tempo,
  length/tail beats) + `renderDevice()`: fresh instrument via the
  Factory tables, standalone `EffectChain`, 256-frame block loop with
  note-on/off placement at sub-block `frameOffset`s, de-interleaved
  `AudioBuffer` accumulation, atomic progress + cancel per block. No
  PortAudio involvement — the live mix is untouched.
- **`src/livecode/PrerenderManager.{h,cpp}`** — mutex/cv job queue +
  N workers (default 2), value-copied specs, cancel via the progress
  atomics, UI-thread `poll()` per frame delivering completed jobs:
  `clip` (stem-separation pattern: in-memory buffer + `setClip` +
  engine sync; persisted on project save), `sampler`/`granular` (the
  existing `loadBufferTo*` publish/retire paths), `file` (WAV via
  `util::saveAudioBuffer`). Library target deferred (phase 4b).
- **Lua**: `yawn.render{...}` / `yawn.cancel_render(id)`; params and FX
  params by name or index, resolved at ENQUEUE time against factory
  prototypes so the worker spec stays integer-only (§6.2); sample rate
  pinned to the engine's; completions reported to the console (+ toast)
  via the manager's report hook. Deterministic scripts generate their
  note lists themselves (seeded math.random in-script); the spec's
  `seed` field serves C++ generative sources (§4.4).
- Tests: +8 cases (audible render, byte-identical determinism, cancel,
  unknown device, FX chain differs, seeded generative source, manager
  E2E delivery to clip, Lua API with name resolution). Full suite green
  (1519 tests).

Deferred from §4 as written: `noteSource` as a worker-thread Lua chunk
(v1: C++ callback only — the machinery is in place), DrumRack pad
delivery, library insertion.

**Phase 5 — landed** (before phase 4; it's the smaller one and makes
sessions reproducible):

- `Project::LiveCodeScriptDef {name, path, autorun}` +
  `liveCodeScripts()/setLiveCodeScripts()` (`src/app/Project.h`).
- `project.json` gains a sparse `livecode` section
  (format/enabled/scripts with per-script `autorun`) — written by
  `ProjectSerializer::saveToFolder`, read (validated) by `loadFromFolder`.
- `LiveCodeManager::scanProjectScripts(projectPath)` — lists
  `<project>/livecode/*.lua` preserving previously recorded autorun
  flags; `App::doSaveProject` populates the project's defs from the scan
  before saving, so scripts become part of the project automatically.
- `LiveCodeManager::onProjectOpened(...)` queues autorun scripts;
  `update()` runs one queued autorun per frame (missing files warned in
  the console). Explicit RUN/STOP stays the primary gesture.

**Phase 4 — landed.** MIDI clip authoring + audio material:

- `yawn.set_notes(track, scene, notes, [opts {beats=...}])` — replaces
  the slot's MIDI clip atomically (positional or named note tuples,
  vel 0..1 → 16-bit; default length = existing clip's length, else 4).
  `yawn.get_notes(track, scene)` reads back normalized tables;
  `yawn.clear_notes(track, scene)` empties while keeping the length;
  `yawn.new_midi_clip(track, scene, [beats])` creates an empty clip.
  All installs go through the same `setMidiClipLive` hook the piano
  roll uses (graveyard + live re-point of a playing slot).
- Buffer handles: `yawn.load_audio_file(path) -> handle` (resampled to
  the engine rate on load, matching file-import behavior),
  `yawn.save_audio_buffer(handle, path, {format="wav"|"flac"|"ogg",
  depth="f32"|"i16"|"i24"})`, and `yawn.load_sample(track, handle,
  [kind="sampler"|"granular"|"drumslop"|"vocoder"])` — the last routes
  through the prerender delivery hooks (App-provided publish/retire
  paths) so scripts can push material into instruments directly.
  Handles live in the manager's registry (`m_bufferHandles`).
- Tests: +5 cases (set/get/clear round-trip, buffer handle save/load
  round-trip, sampler delivery, script scan, autorun-on-open). Full
  suite green (1523 tests).

**Phase 4.5 — landed.** Programmatic sample creation (§4.5):
`src/livecode/LiveCodeBuffers.{h,cpp}` — buffers are forgeable,
inspectable and transformable from the script and share the manager's
handle registry with `load_audio_file`/`yawn.render` results.

- **`yawn.new_buffer{...}`** — `{frames=, channels=, fill=fn(frame, ch,
  frames, channels)}` or `{data = { {ch1...}, ... }}` (zero-filled
  gaps). Bounds: frames ≤ 6M, channels ≤ 16. Returns a handle usable
  by `load_sample` / `render` delivery / `save_audio_buffer`.
- **Inspection** — `yawn.buffer_data(h)` (full sample round-trip as
  Lua tables + `frames/channels/sr`), `yawn.buffer_info(h)`,
  `yawn.free_buffer(h)`.
- **In-place vector ops** — `buffer_gain`, `buffer_normalize`,
  `buffer_fade(in,out)`, `buffer_mix(dst, src, level)`,
  `buffer_reverse`; new-handle shapes: `buffer_slice/concat/repeat/
  mixdown`. `load_sample` now returns true/false.
- **Math** — `yawn.fft(t)` / `yawn.ifft(t)` (iterative radix-2,
  power-of-two length, interleaved re/im tables) and
  `yawn.polyblep(phase, dt)` (polyBLEP-2). `math.sin/cos/exp/...`
  are available — the sandbox keeps `math/string/table`.
- Tests: +10 (`LiveCodeBufTest`): fill/data/round-trip, vector ops,
  structural ops, FFT bin, FFT→IFFT identity, pcall error path,
  polyBLEP edge values, forged-buffer→sampler delivery.

**Phase 6 — landed.** Code lens + console (§5.4 stage 1):

- **`generateSongSource(project, engine)`** (`LiveCodeSong.cpp`) —
  read-only regeneration of the declarative `song = {...}` block from
  live state: bpm, scene count, per-track uid/name/type/volume/mute/
  solo, instrument (all params by name, escaped), FX chain, and MIDI
  clips as positional note tuples; audio clips appear as comments.
- **`LiveCodeConsole`** (`src/ui/panels/LiveCodeConsole.{h,cpp}`, main
  exe) — `~` Quake-style drop-down overlay on the fw2 LayerStack
  (non-modal, dismissOnOutsideClick=false, Escape closes): Console tab
  (severity-colored ring, wheel scroll) / Code tab (code lens,
  regenerated ~1 Hz while visible), plus header status (gen, pending
  schedules, prerender count) and Run/Stop, Reload, Clear buttons.
- **Wiring**: `App` owns `m_liveConsole` (init next to the manager,
  `tick()` in update()); `SDLK_GRAVE` toggles; Live Code menu +
  shortcuts dialog updated; manager gained `activeGeneration()`,
  `pendingSchedules()`, `clearConsole()`.
- Tests: +1 case — the round-trip confluence test: materialize a known
  state → generate the lens → re-parse it in a fresh Lua state → apply
  → **no-op with identical values** (name escaping, index params, note
  channels, velocity rounding). Full suite green (1524 tests).

**Phase 8 — landed.** In-app editor (Edit tab of the `~` console):

- **`LiveCodeEditorKernel`** (`src/ui/panels/LiveCodeEditorKernel.h`,
  header-only, framework-free, test-driven): line buffer + caret
  (line/UTF-8-col, continuation-aware backspace/move), column-goal
  navigation, and the completion state machine — dotted-prefix
  matching (`yawn.n` → `yawn.note`) over a static symbol table (Lua
  keywords + `yawn.*`/`improv.*` API + song keys), ≤2 chars or >32
  matches closes the popup, accept inserts only the remainder.
- **`LiveCodeEditor`** (`src/ui/panels/LiveCodeEditor.{h,cpp}`): fw2
  face — Lua tokenizer for syntax coloring (keywords pink, strings
  amber, numbers cyan, comments slate, `yawn.*`/`improv.*` green,
  `--[[ ]]` state crossing lines), gutter + line numbers, caret,
  completion popup (8 rows, keyboard navigation), pixel hit-testing
  for click-to-caret, caret-keeping vertical scroll.
- **Console Edit tab**: loads the script file fresh from disk unless
  the buffer has unsaved edits; Run evaluates the **buffer** as a new
  generation (`LiveCodeManager::runScriptSource` →
  `LiveCodeEngine::runString` — same gen semantics as file runs, song
  harvest included); Save writes the buffer to the script path;
  Ctrl+Enter evaluates without touching the mouse.
- **App routing**: fw2 overlay keys first (sdlKeyToFw2 mapping
  already in place) → editor consumes nav/commit keys; TEXT_INPUT
  forwards to the console when open; virtual-keyboard guard extended
  so typing doesn't play MIDI notes. `~` still toggles.
- Tests: +8 LiveCodeEditorKernelTest cases (round-trip, edits, joins,
  column goal, completion filter/dotted-accept/limits, UTF-8
  backspace). Full suite green (1532 tests).

**Phase 9 — landed.** Remaining post-v1 items:

- **Render targets completed** (§4.3): `drumslop` and `vocoder` now
  reach the render path (they were only reachable via handle-based
  `load_sample` before); `drumrack` gained a `pad=n` field with a new
  `deliverDrumRackPad` hook (`App::loadBufferToDrumRackPad`, explicit
  pad — no selected-pad side effect); `library` target: the render is
  saved as WAV under the first configured library root (creating
  `<home>/YAWN Samples` and registering it when none exists — filename
  sanitized + numbered for uniqueness) and the root is rescanned so
  the file surfaces in the browser. `noteSource` as a worker-thread
  Lua chunk stays the only deferred §4 item (thread-ownership of the
  Lua state needs its own design; C++ callback machinery is in place).
- **Template-preserving two-way round-trip (§5.4 stage 2)**:
  `patchSongSource(original, project, engine)` — line-oriented value
  rewrite of the `song` block: bpm/scenes scalars, the generator-shaped
  track opener line (`{ uid = N, name = …, volume = …,`), standalone
  `volume/mute/solo` lines, and single-line clip declarations are
  rewritten in place; comments (inline + anywhere outside the block)
  survive byte-for-byte; track blocks are inserted (canonical shape via
  the shared `appendTrackBlock` used by `generateSongSource`) / removed
  by uid; unrecognizable lines (e.g. multi-line clip groups) are left
  untouched and reported as warnings. `patched` is always the full new
  text; `changed` flags byte differences (idempotence: patching the
  patched text is a no-op — tested). Console: **Sync** button on the
  Code tab reads the script file, patches it, writes it back, and
  reloads the Edit tab buffer.
- Tests: +5 (`PatchGeneratedSourceIsNoOp`,
  `PatchScalarsKeepComments`, `PatchInsertsAndRemovesTracks`,
  `PatchClipLinesAndIdentity`, `PatchMultiLineClipGroupsLeftAlone`).
  Full suite green (1537 tests).

**Freeze take → sidecar track (landed).** The improv layer only plays;
nothing it fires touches the project (the script is the source of
truth — auto-inserting notes into script-owned tracks would be erased
by the next Run). Instead, a **Freeze** button (any `~` console tab)
serializes the capture ledger — every `yawn.note` since the last
freeze, with absolute beats, durations, channels, velocities — into a
normalized `midi::MidiClip` (origin-anchored, bar-round length, 8192
note cap, clamped inside the loop) and hands it to a sidecar hook:
the app creates a *new* track (`Take N`, midi type), drops the clip in
scene 1, and syncs. Undeclared tracks are invisible to the song
reconciler, so the take survives re-runs, reloads and save/load.
No undo entry in v1 (the ledger + song layer are the record).

**Phase B — landed.** Improv ↔ shaders (visual channels integration):

- **`VisualGhostBus`** (`src/visual/VisualGhostBus.h`): lock-free
  UI→visual-thread mailbox (seqlock, fixed 8×4-float payload) published
  per frame from the App's tick using the improv ledger's *upcoming*
  fires (fired notes already reach shaders through the audio-thread
  `VisualNoteBus`) — payload: pitch/127, velocity, beats-until-fire,
  track (+ beats-per-bar at publish).
- **New built-in shader uniforms** (layer, composite and post-FX
  programs; declared-name-optional like every other uniform):
  `iGhostCount` (float, 0..8), `iGhost0..7` (vec4 — upcoming fires in
  schedule order), and `iBeatBarFrac` (0..1 inside the current bar, a
  partner to `iBeat`). Unused by a shader → costs nothing (loc=-1).
- **Example**: `assets/shaders/examples/31_ghost_accent.frag` — orbits
  one glyph per upcoming fire toward its bar position, glowing harder
  as the fire moment approaches.
- Tests: seqlock round-trip (payload, count shrink, max clamp, bpb) and
  the ledger→bus pipeline content check. Suite green.

**Shader corpus harness (landed, v0.88.3+).** The compile path a live
shader takes in the app is now under test: `ShaderTestKernel`
(`src/visual/ShaderTestKernel.{h,cpp}`) opens a real offscreen GL
context (SDL offscreen driver + glad), publishes the engine's exact
`GlCaps`, and `tests/test_ShaderCorpus.cpp` compiles **every bundled
shader through the production preamble and two-source `glShaderSource`
call** — failures report the driver message *with the mapped source
line* (Mesa's `0:NN(M)` attribution can point at the preamble even when
the fault is in the shader body; the mapped line is what made the
`(void)`-cast bug in `31_ghost_accent.frag` findable). Contract tests
cover the ghost uniforms and the knob uniforms end-to-end through the
same path. The root cause of the long-standing black visual channel was
also found with this harness — `loadLayer`'s byte-count probe left its
ifstream at EOF, so every shader loaded with an empty user source
(compiled preamble-only, then failed link with `unresolved reference to
mainImage`). See `docs/visual.md` → "Testing the pipeline".

**Visual render thread (landed, v0.88.4).** The visual channel now
renders on a dedicated thread (see `docs/visual.md` → "Architecture
overview") — its ~60 Hz pace is independent of the UI loop and of
compositor throttling of a backgrounded main window, which matters for
live-coding: shader saves, `set_visual` deliveries and ghost-uniform
updates keep presenting at full frame rate even when the DAW window is
minimized or parked behind the editor you're typing in. GL-touching
live-code APIs (`set_visual`, `set_clip` deliveries) marshal onto the
render thread internally; callers see unchanged signatures and returns.

## 11. Pattern notations (yawn.midi / yawn.drums)

Text notations for musical patterns, parsed by `src/livecode/PatternParse`
into the standard clip-spec table (`{beats, notes}`) that the declarative
song layer, `yawn.set_notes` and the prerender specs all consume
directly — so `clips = { [1] = yawn.midi("...") }` just works.

**Melodic** — sequential; each note starts where the previous one ends.
Pitch is `[A-G][#|b]?<octave>` (MIDI 60 = C4, matching the rest of YAWN):

```lua
yawn.midi("A1_16B1_16D#3_8")            -- the user's example, no spaces needed
yawn.midi("C2_8 C2_8 D#2_8*2 R_8 G2_4") -- *N multiplies length; R = rest
yawn.midi("C2_16@1 C2_16@0.5")          -- @vel suffix (0..1, default 0.8)
yawn.midi("[C3E3G3]_2 C4_4 | A2_2")     -- chords; | separators are cosmetic
yawn.midi("C2_16*16")                   -- exactly one bar of 16ths
```

Durations: `_1 _2 _4 _8 _16 _32` (whole-note fractions → 4/2/1/0.5/0.25/
0.125 beats). Clip length = the exact sum, so a phrase can be a partial
bar. Malformed input fails the run with the offending token named.

**Drums** — one lane per voice, one character per 16th step; lanes may
differ in length (each loops; clip = longest lane, or `{beats=N}` forces
it). Velocities reuse the factory-loop vocabulary: `X` accent (0.88),
`x` normal (0.76), `o` soft (0.6), `g` ghost (0.31), `.`/`-` rest.

```lua
yawn.drums{
  BD = "x---x---x---x---",
  SN = "----x-------x---",
  HH = "x-x-x-x-x-x-x-x-",
  OH = "------------x---",
}
```

Lane names (case-insensitive aliases): `kick/bd`, `snare/sn`,
`clap/cp`, `rim/rs`, `hihat/hh/ch/closedhat`, `openhh/oh/openhat`,
`floortom/ft`, `lowtom/lt`, `tom/tommid/tm`, `hightom/ht`,
`crash/cr/cy/cymbal`, `ride/rd`. All notes land on **GM channel 9**
at the standard GM pitches (36/38/42/46/…), so patterns fire hardware
DrumRack pads loaded at GM notes.

**Editor keyboard** — the `~` console's Edit tab has a notation
keyboard strip below the editor: one octave of piano keys, an octave
stepper, the `_N` length buttons and a `×N` multiplier cycle. Clicking
a key inserts its token at the caret; the **MIDI** button arms capture
so a real keyboard's note-ons type themselves in (played pitch, the
selected length).
