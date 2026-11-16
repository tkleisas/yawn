# Generative Songs — design

Status: P0 scaffold landed (engine host, Lua layout, microtuning plumbing,
CLI). UI arrives in P2.

This document is the agreed design for YAWN's generative song/pattern
feature. The generator composes **notes only** (no sound design); the
result is written into ordinary `MidiClip`s on tracks the user picks.
The composition logic lives in Lua for rapid iteration and user
extensibility; C++ hosts the Lua engine, validates its output, and
commits clips.

## Decisions (locked)

- **Session-first output.** One scene per section
  (`01 Intro`, `02 Verse`, …), one clip per generated lane per scene.
  Follow actions can chain the scenes into a full song. Arrangement
  commit plus section markers is a later phase.
- **Fill existing tracks.** The generator does not create tracks.
  The generated song declares *lanes* (drums, bass, chords, lead, …);
  the user selects which existing tracks receive the clips, in lane
  order. Different styles may emit different lane counts.
- **Separate clip per section per track.**
- **Deterministic.** Same params + seed → identical song. Randomness
  comes only from a seeded RNG provided by the host.
- **Styles are data**, bundled as Lua files; users can add styles,
  scales, and generator modules.
- **Blend dial.** Any number of styles with weights, e.g.
  `rock 0.3 / classical 0.3 / techno 0.4`, two blend modes:
  - `fuse` (default): per-section dominant style for structure/vocab
    (progression, groove, template), weighted trait averaging inside
    sections (density, note length, swing, ornamentation).
  - `rotate`: each section takes one style, picked by weight.
- **Microtuning from day one.** Scales store cents offsets per degree
  (maqams, ragas, just intonation, TET24, user scales). Notes carry a
  cents offset in the clip and play back tuned.
- **No realtime generation initially.** Generate-then-commit; the
  result is editable, undoable, and plays through the normal engine.
- **No plugin in P0.** Later: virtual MIDI port + a small CLAP
  generator plugin (MIDI out only); no VST3 plugin (SDK licensing).
- **No sound/preset selection.** Lua returns notes; instruments stay
  whatever the destination tracks already have.

## Architecture

```
GeneratorPanel (P2)      CLI --gen-song (P0)      CLAP shell (P4)
        |                        |                       |
        +----------- GeneratorEngine (C++, src/generate) -+
                                |
                    lua_State (isolated, sandboxed)
                                |
                 assets/generator/*.lua  (bundled)
                 ~/.yawn/generator/*.lua (user overrides)
                 <project>/generator/*.lua (project overrides)
                                |
                      GeneratedSong (C++ structs)
                                |
                   SongCommitter (P2): scenes + MidiClips
                   SMF export (P3)
                   MIDI out / virtual port (P3)
```

Script search order (highest priority first):

1. `<project>/generator/`
2. `~/.yawn/generator/`
3. bundled `assets/generator/`

`require()` is reimplemented by the host: only `.lua` files under those
roots, no C modules, no `io`/`os`/`package`/`debug` in the script
environment. A bad script reports a traceback and leaves the previous
result untouched.

## Lua layout

```
assets/generator/
  init.lua            generate(params) -> song table
  theory.lua          scales (cents), roots, degrees, chords
  rhythm.lua          cells, euclidean, swing, humanize, density
  melody.lua          contour walk, motifs, bass/chord/lead voices
  drums.lua           GM drum grooves, grids, fills
  sections.lua        section types + templates + energy
  blend.lua           weighted style loading/selection/trait averaging
  styles/<id>.lua     one file per style
```

### Generator API

C++ calls `require("init").generate(params)`. `params` fields:

| field         | type            | meaning                              |
|---------------|-----------------|--------------------------------------|
| `seed`        | integer         | deterministic RNG seed               |
| `styles`      | array           | `{ {id="rock", weight=0.3}, ... }`   |
| `blend`       | `"fuse"`/`"rotate"` | blend mode                       |
| `scale`       | string          | scale id, `""` = style decides       |
| `root`        | string          | root note, `""` = style decides      |
| `movement`    | string          | ascending/descending/arch/valley/random/auto |
| `density`     | number          | `-1` = style decides                 |
| `noteLength`  | number          | `-1` = style decides                 |
| `swing`       | number          | `-1` = style decides                 |
| `tempo`       | integer         | `0` = style decides                  |
| `sections`    | array of strings| empty = style template               |
| `beatsPerBar` | integer         | default 4                            |

The returned song table:

```lua
{
  title = "Untitled",           -- optional
  tempo = 120,                  -- BPM
  beatsPerBar = 4,
  sections = {
    {
      id = "verse", name = "Verse", bars = 8,
      clips = {
        {
          role = "drums",
          length = 32,          -- beats, optional (defaults to bars*beatsPerBar)
          notes = {
            { beat=0.0, dur=0.25, pitch=36, vel=100, ch=9 },
            { beat=0.0, dur=0.5,  pitch=62, vel=90, cents=50 },
          },
        },
        -- one clip per lane; role order defines lane order
      },
    },
  },
}
```

Validation (C++): 1–64 sections, 1–256 bars each, ≤32 lanes/section,
notes must lie inside the section, pitch 0–127, vel 1–127,
`-1200 ≤ cents ≤ 1200`, ≤200k notes total. Invalid output is rejected
with a section/clip/note index in the error.

### Lua example: a style file

```lua
return {
  id = "pop", name = "Pop",
  tempo = { 100, 124 },
  scales = { "major", "natural_minor", "mixolydian" },
  progressions = {
    { "I", "V", "vi", "IV" },
    { "vi", "IV", "I", "V" },
  },
  sections = { "intro", "verse", "prechorus", "chorus",
               "verse", "chorus", "outro" },
  density = { 0.45, 0.65 },
  noteLength = { 0.4, 0.8 },
  swing = 0.0,
  drums = { kick = "x---x---x---x---", snare = "----x-------x---",
            hat  = "x-x-x-x-x-x-x-x-" },
  melody = { range = 12, ornament = 0.15, contour = "arch" },
}
```

## Music theory / scales

`theory.lua` stores each scale as semitone offsets per degree, floats
allowed, so cents are first-class:

```lua
major       = { 0, 2, 4, 5, 7, 9, 11 }
hijaz       = { 0, 1, 4, 5, 7, 8, 10 }        -- 12-TET approximation
rast        = { 0, 2, 3.5, 5, 7, 9, 10.5 }    -- neutral 3rd/7th
bayati      = { 0, 1.5, 3, 5, 7, 8.5, 10 }
raga_yaman  = { 0, 2, 4, 6, 7, 9, 11 }
```

A note is `pitch = round(semitone)`, `cents = (semitone - pitch) * 100`.
Ragas get aroha/avarohana and phrase grammar in P1; maqams use their
cents tables directly.

## Microtuning playback

- `MidiNote` gains `cents` (stored/serialized; `0` = 12-TET).
- Built-in instruments: microtuned clip notes play on an MPE member
  channel (2–16) with a channel pitch bend of `cents/200` (built-in
  instruments bend ±2 semitones). Up to 15 concurrently tuned notes per
  track; beyond that the note falls back to the nearest semitone.
  Allocation is LRU over released channels to minimise release glides.
- Hosted VST3 instruments: `NoteOnEvent.tuning` / `NoteOffEvent.tuning`
  are set directly (VST3 defines 1.0 = 1 cent), independent of channels.
- External MIDI, virtual port, SMF export: MPE-style bends in P3.
- Tests: serializer round-trip + a `MidiClipEngine` test asserting the
  bend/channel pairing; audio-frequency tests via the YIN `Tuner` in P1.

## Commit model (P2)

1. User selects destination tracks (multi-select), in lane order.
2. Generated lanes map to the selected tracks; extra lanes are skipped,
   extra tracks stay empty.
3. For each section: create/replace a scene named
   `NN Section` and set the clip on each lane's track.
4. Generated clips are normal `MidiClip`s (piano-roll editable, undo
   wrapped). Regeneration only replaces unlocked lanes.
5. Optional: set follow actions so scenes chain into a full song.

No new project/section model is required for P2. Arrangement markers
and timeline placement are P3.

## Phases

- **P0 (this scaffold):** design doc; `GeneratorEngine` Lua host;
  script layout; 4 starter styles; theory/rhythm/melody/drums/sections/
  blend skeleton; cents data model + playback plumbing; CLI
  `--gen-song`; tests.
- **P1:** full theory (maqams, ragas with aroha/avarohana), blend
  engine both modes, 10 styles (pop, rock, classical, techno, house,
  jazz, metal/djent, ethnic-maqam, raga, serial), richer melody/drums/
  fills, deterministic golden tests, demo song.
- **P2:** GeneratorPanel (style weights, all params, seed, sections,
  locks, preview/commit), presets, MIDI Learn targets, docs/tutorial.
- **P3:** virtual MIDI port, per-track MIDI TX, SMF export with cents,
  arrangement commit + section markers.
- **P4:** CLAP generator plugin (MIDI out, no audio) using the same
  Lua engine and validation.

## Non-goals

- Whole-DAW-as-VST3.
- Audio-rate DSP in Lua; Lua composes notes and may later choose
  presets, but DSP stays C++.
- Auto-creating instruments/tracks on commit.
