# Generative Songs

YAWN can compose editable songs and patterns from a set of musical
parameters. The generator is deterministic (same seed + settings →
same song), session-first, and writes ordinary MIDI clips — so the
result can be edited in the piano roll, automated, and played through
any instrument or hosted VST3.

The implementation is split between a C++ host
(`src/generate/`) and a user-extensible Lua composition layer
(`assets/generator/`). See
[generative-song-design.md](generative-song-design.md) for the
architecture.

## Quick start

1. Open **Tools → Generative Song…**
2. Set style blend weights (e.g. rock 0.3, classical 0.3, techno 0.4).
3. Adjust parameters (optionally press **Reroll Seed**). Use **Section length** and **Repeats** for a longer song; repeats are newly generated variations.
4. Press **Generate**. The panel reports sections, notes, tempo, lanes.
5. Assign each lane (drums, bass, chords, lead, …) to a destination
   track. Lane counts vary by style — classical and serial have no
   drum lane.
6. Press **Commit**. YAWN appends one scene per section
   (`01 Intro`, `02 Verse`, …), writes one clip per lane, and sets
   follow actions so the scenes chain when the first one is launched.
   The last section stops.
7. The generator closes and the Session view scrolls to the new
   scenes. Undo removes all appended scenes; redo restores them.

The generated clips are normal `MidiClip`s: double-click them to edit
notes, drag them, change loop length, etc. Regenerating with a
different seed and committing again appends a second set of scenes.

## Parameters

| Parameter | Meaning |
|---|---|
| Style weights | Blend any number of styles; 0 disables a style |
| Blend | `Fuse` mixes weighted traits inside sections; `Rotate` assigns each section to a style |
| Scale | Western modes, maqams, ragas, just intonation, TET24, auto |
| Root | Root note, auto |
| Movement | Ascending, descending, arch, valley, random, auto |
| Seed | Deterministic RNG seed |
| Mix | When **Auto mix** is on, density/note length/swing follow the style blend. Turn it off to override them |
| Density, Note length, Swing | Per-section note generation biases |
| Tempo | 0 = style decides |
| Section length | Scales every section's bar count (0.25×…4×) |
| Repeats | Repeats the whole section sequence 1–8 times; each pass is regenerated with the same style blend, so the second/third pass varies |
| Presets | Save/load the complete parameter set |

## Styles

Bundled: pop, rock, classical, techno, house, jazz, metal/djent,
ethnic-maqam, raga, serial.

- **Maqam/raga** styles use cents-accurate scale degrees; the raga
  style also honours aroha/avarohana (ascending/descending note sets).
- **Serial** composes twelve-tone rows with prime/inversion/
  retrograde/retrograde-inversion forms and has no drum lane.
- **Classical** omits the drum lane.
- Styles can be blended freely; the dominant style for each section is
  chosen by weight, and continuous traits are averaged.

## Microtuning

Scales are stored in semitones with fractional values (e.g. maqam Rast
uses 3.5 semitones for its neutral third). Generated notes carry a
`cents` offset which is saved with the clip:

- Built-in instruments play tuned notes on MPE member channels with a
  channel pitch bend.
- Hosted VST3 instruments receive the cents in the note event's tuning
  field.
- Up to 15 simultaneously tuned notes per track; beyond that a note
  falls back to the nearest semitone.

## Visuals

Enable **Visuals** in the GeneratorPanel. The generator creates one
visual track per generated audio lane (drums / bass / chords / lead,
named **V Drums**, **V Bass**, …) and reuses them on later commits.
Each lane reacts to its matching audio track and the lanes are mixed
together: drums drive the full-strength background, bass/chords/lead
add over it with Add/Screen blend and lower opacity. The generator:
- chooses a theme per song automatically from the style blend
  (sacred / cosmic / ink / abstract) or uses the theme you pick;
- picks a shader per section by energy — calm for intro/outro, driving
  for verses, brighter for chorus/drop;
- sets shader parameters from the section energy (spin, drift, glow,
  kick response) and the theme palette, within each shader's `@range`;
- loads `seed_emblem.png` for the seed-picture shaders;
- scales how hard each lane reacts to audio via the **Reactivity**
  dial (0 = static, 1 = normal, 2 = exaggerated), with per-lane
  weighting (drums strongest, chords subtler). This multiplies the
  shader's `iAudioLevel` / `iAudioLow/Mid/High` / `iKick` and also
  applies to scene-script inputs. While the panel is open it applies
  live to every visual track; committed clips store the value.

Commit writes one tempo-synced `VisualClip` per lane per section into
the same scenes as the audio/MIDI clips, with visual follow actions.
Launch the first scene and all lanes switch with the song: each visual
listens to its own audio lane, and the layered blend gives the
interplay. Undo/redo covers the visual clips as part of the generated
scenes; the auto-created visual tracks remain (they are reused).

The visual plan is deterministic with the song seed. The shader set is
the bundled `assets/shaders/examples` plus any project `.frag` files;
custom shaders are available to the catalog but the curated theme
lists drive selection.

CLI preview:

```
YAWN --gen-song /tmp/song.mid --style rock:0.5 --style techno:0.5      --visuals --visual-theme cosmic
```

This prints the per-section shader plan (the SMF remains audio-only).

## MIDI Learn

Right-click a style-weight knob, Density, Note Length, Swing, or Tempo
and choose **MIDI Learn…**, then move a CC. Mappings persist with the
project. Incoming CC values update the panel live; Auto mix is switched
off when a mapped density/length/swing value arrives.

## Presets

**Save Preset** stores the current parameter set under
`~/.yawn/generator/presets/<name>.json`. Project presets can also live
in `<project>.yawn/generator/presets/`. The Presets dropdown lists both
(project entries win).

## Extending the generator

The Lua host searches, in order:

1. `<project>.yawn/generator/`
2. `~/.yawn/generator/`
3. bundled `assets/generator/`

Scripts are sandboxed: no `io`, `os`, `package`, `debug`, no C
modules, and only `.lua` files under those roots. Randomness must come
from `yawn.rng` (`float`, `int`, `pick`, `chance`) so seeds stay
deterministic. Generator script errors show the Lua traceback in the
panel; the previous version keeps working.

### Adding a style

Create `styles/mystyle.lua`:

```lua
return {
  id = "mystyle", name = "My Style",
  tempo = { 110, 130 },
  scales = { "dorian", "natural_minor" },
  roots = { "D", "A" },
  progressions = { { "i", "VII", "VI", "VII" } },
  sections = { "intro", "verse", "chorus", "verse", "outro" },
  density = { 0.5, 0.7 },
  noteLength = { 0.3, 0.6 },
  swing = 0.0,
  chordVoicing = "drone",        -- "power" / "drone" / omit
  chordRhythm = "stab",          -- whole/half/quarter/offbeat/stab/16th/drone
  bassStyle = "offbeat",         -- root/eighths/walk/arp/drone/chug/offbeat
  motifRepeat = 0.3,
  drums = {
    kick = "x---x---x---x---",
    snare = "----x-------x---",
    hat = "x-x-x-x-x-x-x-x-",
  },
  melody = { contour = "arch", ornament = 0.2 },
}
```

Drum grids are 16ths (`X`/`x`/`o`/`g` = velocity levels, `-` = rest);
`fill` and `crash` grids are optional. Style ids are file stems.

### Adding scales and ragas

`theory.lua` holds plain scales as semitone arrays and ragas as
`{ degrees, asc, desc }` tables. Fractional semitones express cents;
degree lists for ragas index into the scale (0-based).

## CLI

The same engine has a headless preview harness:

```
YAWN --gen-song /tmp/song.mid --style rock:0.3 --style techno:0.7 \
     --seed 42 --scale rast --root D --movement ascending
```

The CLI writes a single-track SMF preview. Cents offsets are kept in
the clip model but not yet written to SMF (MPE export lands in P3).

## Current limitations

- Generate-then-commit only; there is no realtime generative streamer.
- SMF export does not yet encode microtuning.
- MIDI Learn targets the fixed parameter slots; style slots follow the
  sorted style list.
- Virtual MIDI output and the CLAP plugin are future phases.
- Undo removes generated scenes (and their visual clips) but leaves
  the auto-created `V …` visual tracks in place for reuse.
