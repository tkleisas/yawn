# Live Coding — the tutorial

*A progressive introduction to coding music as music: from your first scheduled
note to the full two-layer model, offline sound-forging, and the workflow
around it. No prior YAWN knowledge assumed; no prior Lua assumed either —
everything you need is explained as it appears.*

> **This tutorial is tested.** Every `Lua` snippet marked **run** in this file
> is executed headlessly by the suite (`tests/test_TutorialLiveCode.cpp`),
> exactly like the bundled demo songs in `assets/examples/livecode/`: the
> snippet runs as a real generation on a real default project, the transport
> is pumped so scheduled callbacks fire, the snippet is re-run (the Ctrl+L
> edit loop), and the test fails if anything errors or even prints a warning
> to the `~` console. If this document says it works, it works.
>
> The reference for every detail (design, scheduling model, full API surface)
> is [live-coding.md](live-coding.md). This tutorial is the staged path
> through it.

**You will need**

- A running YAWN ([docs/building.md](building.md) or a release download)
- Fresh default project (File → New — two empty tracks, two scenes)
- Ctrl+L and Ctrl+Shift+L on your keyboard, and the **`~`** (backtick) key

**Read this once — the three facts that make everything else click:**

| Fact | Meaning |
|---|---|
| **Tracks count from 0, scenes count from 1** | `yawn.note(0, …)` plays track 1; `clips = { [1] = … }` is scene 1. This is the single most common source of confusion. |
| **Velocities differ by API** | `yawn.note` takes MIDI velocity **0..127**. Clip notes (`yawn.set_notes`, `song` clips, `yawn.render`) take **0.0..1.0**. |
| **Two layers, one file** | `song` is the declarative project (the DAW converges to it). `improv` is the performer (callbacks fired on the musical grid). Until Lesson 5, you'll mostly live in the second. |

Also worth knowing up front: the **audio thread never runs your code**. Lua
runs on the UI thread; sounds and edits are handed to the audio engine
through the same paths the UI itself uses. You cannot crash the audio
stream from a script — the worst a bad line can do is print an error.

## Lesson 1 — first sound

Live code runs from a script file. Press **Ctrl+L** on a fresh project:
YAWN creates a commented template the first time (for an unsaved project
it lives at `~/.yawn/livecode/main.lua`; for a saved project, inside the
project's `livecode/` folder) and tells you to edit it. Press Ctrl+L again
(or `~` → Edit tab → Run to run straight from the editor) and it executes.

Replace the template with this and run:

```lua run
-- Lesson 1 — a heartbeat
song = {
    tracks = {
        { uid = 1, name = "Kick", type = "midi",
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.6,
              ["Filter Cutoff"] = 0.2, ["Filter Env"] = 0.8,
              ["Amp Decay"] = 0.2 } } },
    },
}

yawn.set_playing(true)            -- improv callbacks ride transport time

improv.every(1, function(beat)    -- every beat, from now on
    yawn.note(0, 36, 127, 0.3, 0, beat)   -- track 1 (0-based), C2, loudest
end)

-- @assert tracks:2
-- @assert playing
-- @assert inst:0:subsynth
```

You should hear a kick drum on every beat. What just happened, line by line:

- **`song`** declared one track. A fresh project starts with two default
  tracks; `uid = 1` *adopts* the first one (renames it "Kick", flips it to a
  MIDI track because you asked for `type = "midi"`, and attaches a **Sub
  Synth** from the device factory). Track indices don't move: the adopted
  track is still track 0.
- **`yawn.set_playing(true)`** starts the transport. The improv layer is
  transport-anchored — no play, no callbacks.
- **`improv.every(1, fn)`** — every 1 beat, first fire one beat from now
  (`every(0.5, …)` would be eighths, `every(0.25, …)` sixteenths).
- **`yawn.note(track, pitch, vel, dur, ch, at_beat)`** — the payload
  primitive. Here: track 1 (0-based), MIDI 36 (C2), velocity 127, gate
  0.3 beats, channel 0, *on* transport beat `beat`.

That last argument is the important one. The scheduler fires callbacks
**slightly before their target moment** (lookahead — more in Lesson 2) and
passes the upcoming target beat as `beat`. Passing it to `yawn.note` lets
the audio engine place the note *sample-accurately inside the right block*
instead of "somewhere this frame". Rule of thumb: **when a callback is fed
a beat, feed that beat to the note.**

Try this now, with the transport still playing:

1. Change `36` to `43` — Run again (Ctrl+L, or Ctrl+Enter in the Edit tab
   to run the editor buffer). Each run replaces the previous *generation*
   at the next bar boundary, so the change lands musically, mid-groove,
   with no silence.
2. Change the rhythm: `improv.every(1, …)` → `improv.every(0.25, …)`.
3. Watch the console: each run logs `live code: generation N running`. The
   generation number in the `~` console header tells you which layer is
   live right now.

One safety property to internalize before moving on: **a failed run does
not stop the show**. Introduce a deliberate typo and run — the previous
generation keeps performing while the console prints the Lua error with
line information. (An entry that errors three times in a row is
auto-cancelled, so one broken callback can't spam the log forever.)

## Lesson 2 — musical time

`improv.every` is for things that repeat on a fixed subdivision: hats,
pulses, ticks. Musical *structure* — where the downbeat goes, how the bass
moves bar-to-bar — wants a different shape: plan once per bar, fire many
notes at exact beats. The scheduler has four primitives:

| Primitive | Fires | Receives |
|---|---|---|
| `improv.every(beats, fn, [reps])` | every `beats`, starting one interval from now | the upcoming target beat |
| `improv.on_bar(fn)` | on every bar boundary (grid origin at beat 0) | the 1-based bar number |
| `improv.after(deltaBeats, fn)` | once, `deltaBeats` from now | the target beat |
| `improv.at(bar, sub, fn)` or `improv.at(beat, fn)` | once, at an absolute position | the target beat |

A full groove — the same drum kit as Lesson 1 plus a bass — where each
voice plays the way that voice wants to be written:

```lua run
-- Lesson 2 — a groove
song = {
    bpm = 122,
    tracks = {
        { uid = 1, name = "Kick", type = "midi",
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.6,
              ["Filter Cutoff"] = 0.18, ["Filter Env"] = 0.9,
              ["Amp Decay"] = 0.18 } } },
        { uid = 2, name = "Hats", type = "midi", volume = 0.55,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"] = 4, ["Noise Level"] = 1.0,
              ["Osc1 Level"] = 0.0, ["Filter Cutoff"] = 0.85,
              ["Filter Type"] = 2, ["Filter Reso"] = 0.15,
              ["Amp Attack"] = 0.001, ["Amp Decay"] = 0.045 } } },
        { name = "Bass", type = "midi", volume = 0.8,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"] = 2, ["Osc1 Level"] = 0.9,
              ["Sub Level"] = 0.5, ["Filter Cutoff"] = 0.24,
              ["Filter Type"] = 0, ["Filter Env"] = 0.55,
              ["Amp Attack"] = 0.002, ["Amp Decay"] = 0.5 } } },
    },
}

-- Kick: where THIS groove lives is a decision, not a tick — plan it per bar.
improv.on_bar(function(bar)
    local b = (bar - 1) * 4                 -- bar 1 spans beats 0..4
    yawn.note(0, 36, 127, 0.3, 0, b)        -- downbeat, loud
    yawn.note(0, 36, 102, 0.15, 0, b + 2.5) -- pickup off the grid
end)

-- Hats: nobody formats a hat on a spreadsheet — let the grid carry it.
improv.every(0.5, function(beat)
    local open = (beat % 2 == 0.5)          -- every second eighth opens
    yawn.note(1, open and 46 or 42, open and 92 or 56, 0.05, 0, beat)
end)

-- Bass: root movement once per bar; notes dropped at exact beats.
local roots = { 33, 36, 41, 43 }            -- A, C, F, G (low)
improv.on_bar(function(bar)
    local root = roots[((bar - 1) % #roots) + 1]
    local b = (bar - 1) * 4
    yawn.note(2, root,     100, 0.45, 0, b)
    yawn.note(2, root + 7,  95, 0.30, 0, b + 2)
end)

-- One-shots: a fill lands exactly once, at 12.5 beats in:
improv.after(12.5, function(beat)
    yawn.note(1, 46, 80, 0.06, 0, beat)
    yawn.log("fill landed on beat " .. beat)
end)

-- And an absolute appointment: bar 3, sub-beat 1.5:
improv.at(3, 1.5, function(beat)
    yawn.note(0, 36, 70, 0.1, 0, beat)      -- a ghost kick
end)

yawn.set_playing(true)

-- @assert bpm:122
-- @assert tracks:3
-- @assert playing
-- @assert inst:0:subsynth
```

Detail worth understanding — **what happens if a beat is missed**. The
scheduler runs on the UI thread; if the machine hitches longer than the
schedule-ahead window, a note can reach the audio engine *after* its beat.
The default is graceful: play it now. A strict performer prefers silence
over bad timing:

```lua
improv.late_policy("drop")   -- "play" (default) or "drop" + a console note
improv.lookahead(0.15)       -- widen the schedule-ahead window (default 0.10 s)
```

Both stay cheap to change live. Lookahead is converted to beats at the
current tempo every frame, so a queued note measures *musical distance*,
not wall time: drag the BPM and the plan stretches with it instead of
piling up.

Two footnotes for completeness: `improv.every(interval, fn, reps)` — with
`reps` set, the pattern stops after that many fires (`reps` unset or 0 =
forever). `improv.clear(handle)` + `improv.clear_all()` un-schedule things
(`every`/`at`/… return a handle). In practice you rarely need either
because a re-run replaces the whole layer — that's the design.

## Lesson 3 — state that survives the loop

Live coding is the art of changing one thing and keeping everything else.
The **`yawn.state`** table exists so your parameters live outside the
callback code: assignments to it persist across runs (the Lua state itself
survives Ctrl+L), and it is even harvested across a full reload
(Ctrl+Shift+L, which recreates the Lua state and migrates `yawn.state`
field-by-field).

One idiom matters: **field-level fallbacks**:

```lua run
-- Lesson 3 — one knob, many hats
song = {
    tracks = {
        { uid = 1, name = "Kick", type = "midi",
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.6,
              ["Filter Cutoff"] = 0.2, ["Filter Env"] = 0.8,
              ["Amp Decay"] = 0.2 } } },
        { uid = 2, name = "Hats", type = "midi", volume = 0.6,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"] = 4, ["Noise Level"] = 1.0,
              ["Osc1 Level"] = 0.0, ["Filter Cutoff"] = 0.85,
              ["Filter Type"] = 2, ["Amp Attack"] = 0.001,
              ["Amp Decay"] = 0.045 } } },
    },
}

-- yawn.state is pre-seeded as a (truthy, empty) table by the engine, so
-- the *table-level* idiom `yawn.state = yawn.state or {}` is a no-op —
-- always fall back per FIELD:
yawn.state.density = yawn.state.density or 0.5

improv.lookahead(0.15)

improv.every(1, function(beat)              -- the kick keeps the time
    yawn.note(0, 36, 127, 0.3, 0, beat)
end)

improv.every(0.5, function(beat)            -- the hats take the weather
    local open = (beat % 2 == 0.5)
    if open or math.random() < yawn.state.density then
        yawn.note(1, open and 46 or 42,
                  open and 95 or math.floor(50 + 40 * math.random()),
                  0.05, 0, beat)
    end
end)

improv.on_bar(function(bar)                 -- and the system breathes
    if bar % 8 == 0 then
        yawn.state.density = 0.4 + 0.5 * math.random()
        yawn.toast(("evolution: density → %.2f"):format(yawn.state.density))
    end
end)

yawn.set_playing(true)

-- @assert tracks:2
-- @assert playing
-- @assert inst:1:subsynth
```

Now, with it running, perform some states:

1. Open the `~` console's **Edit** tab, type `yawn.state.density = 0.95`
   anywhere in the script and hit **Run** (Ctrl+Enter). The pattern mutates
   on the spot — no stop, no restart.
2. Press **Ctrl+Shift+L**: the script re-loads from disk *and* carries
   `yawn.state.density` across the reload. The hats don't change character
   because of a reload — that's the point.
3. Press **Ctrl+L** once to stop: scheduled callbacks are dropped, already
   triggered notes are released, and the Lua state keeps its values. Press
   it again: you're back on, mid-tune, with the same density.

The console also shows the **gen** counter and the pending-schedule count;
the `Reload`, `Stop`, `Run` and `Clear` buttons up top do exactly what the
keys do. The 256-line console ring is your timeline of what happened.

## Lesson 4 — typed music: clips and pattern notation

Everything so far performed *live* without leaving a trace in the project.
Clips are the durable form — and they can be authored from code, including
with two text notations that turn patterns into single lines:

- **`yawn.midi(phrase)`** — melodic notation: `pitch_octave_duration`.
  `C4_8` is an eighth-note C4 (MIDI 60 = C4, matching the rest of YAWN);
  durations `_1 _2 _4 _8 _16 _32` are whole-note fractions (4 / 2 / 1 /
  0.5 / 0.25 / 0.125 beats); each note begins where the previous
  ends. `*N` multiplies a duration (`_16*16` = exactly one bar of 16ths),
  `@vel` sets velocity 0..1 (default 0.8), `R` is a rest, `[C3E3G3]_2` is
  a chord, `|` is a cosmetic bar line, and `!pattern` arpeggiates a chord
  (`up`, `down`, `updown`, `downup`, `random`, `asplayed`, optional
  octave count: `[C3E3G3]_16*8!up2` sweeps two octaves).
- **`yawn.drums{lanes}`** — a step grid, one character per 16th step, one
  lane per voice on the GM drum channel: `X` accent (0.88), `x` normal
  (0.76), `o` soft (0.6), `g` ghost (0.31), `.`/`-` rest. Lanes may have
  different lengths (each loops; the clip is the longest lane, or
  `{beats=N}` forces it). Lane names: `kick/bd`, `snare/sn`, `clap/cp`,
  `rim/rs`, `hihat/hh`, `openhh/oh`, `ride/rd`, `crash/cr`, `hightom/ht`,
  `tommid/tm`, `lowtom/lt`, `floortom/ft` (case-insensitive).

Both return a **clip spec** `{ beats = …, notes = … }` — the same table the
`song` layer's `clips` accept, so notation composes everywhere. Put it to
work:

```lua run
-- Lesson 4 — clips from code
song = {
    tracks = {
        { uid = 1, name = "Drums", type = "midi", volume = 0.9,
          instrument = { id = "drumrack" },
          clips = { [1] = yawn.drums{         -- the kit, as a grid
              BD = "x---x---x---x---",
              SN = "----x-------x---",
              HH = "x-x-x-x-x-x-x-x-",
          } } },
        { uid = 2, name = "Bass", type = "midi", volume = 0.8,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"] = 2, ["Osc1 Level"] = 0.9, ["Sub Level"] = 0.5,
              ["Filter Cutoff"] = 0.24, ["Filter Env"] = 0.55,
              ["Amp Decay"] = 0.5 } },
          clips = {
              [1] = yawn.midi("C2_8 C2_8 D#2_8*2 G2_4"),   -- exactly 3 beats
          } },
        { name = "Keys", type = "midi", volume = 0.6,
          instrument = { id = "karplus", params = { ["Damping"] = 0.45 } },
          clips = {
              [1] = yawn.midi("[C4E4G4]_16*16!up@0.45"),   -- a typed arp
          } },
    },
}

-- set_notes is the live-authoring tool: here, a scene-2 bass variant
-- written out as plain note tuples, without touching scene 1.
-- (Fresh pads on a new Drum Rack are empty until you load samples into
-- them — drag some from the browser now, or forge them in Lesson 6.
-- The grid keeps firing at the pads once they're loaded.)
yawn.set_notes(1, 2, { { 0, 0.5, 33, 0.9 }, { 1, 0.5, 40, 0.7 },
                       { 2, 0.5, 45, 0.85 }, { 3, 0.5, 36, 0.65 } })

yawn.launch_scene(1)     -- the whole scene, quantized to the downbeat

-- @assert tracks:3
-- @assert playing
-- @assert inst:0:drumrack
-- @assert midinotes:0:1:14
-- @assert midinotes:1:1:4
-- @assert midinotes:2:1:16
-- @assert midinotes:1:2:4
```

(`tracks` here counts the two adopted default tracks plus the one created
`Keys` track — see Lesson 5 for the ownership rules behind that.)

`yawn.launch_scene(1)` is the self-start idiom: it defers the launch until
after the song apply, seeks to the downbeat (a stopped transport starts
immediately, in lockstep; a running one quantizes to the next bar), and
starts the transport. RUN becomes *music*, not "and then you have to press
play".

Clips have a full authoring API beyond the notation:

```lua
yawn.set_notes(track, scene, notes, [opts])  -- install/replace a clip's
                                             -- notes: {start, dur, pitch,
                                             -- vel(0..1), [ch]} tuples,
                                             -- positional or named
yawn.get_notes(track, scene)   -> normalized note table (or nil)
yawn.new_midi_clip(track, scene, [beats=4])  -- empty clip
yawn.clear_notes(track, scene)               -- empty it, keep the length
yawn.launch_clip(track, scene, { quantize = "none"|"beat"|"bar" })
```

`set_notes` defaults a clip's length to whatever's in the slot (or 4
beats); in the `song` layer you set `beats` explicitly per clip. To
override the default from code, pass an options table — for instance
`yawn.set_notes(0, 1, notes, { beats = 4 })`.

The notation also has hardware support: the `~` console's Edit tab has a
**notation keyboard** strip (one octave of piano keys, `_N` length
buttons, a `×N` multiplier, and a **MIDI** toggle that arms your physical
keyboard so played notes type themselves into the editor as tokens).

## Lesson 5 — the project as code: the `song` layer

So far the `song` block was scaffolding. It is the actual feature: a
declarative, diff-applied description of the project. On every run the
engine parses `song`, diffs it against the live project, and applies the
*minimal* edit list through the same edit primitives the UI uses
(instrument swaps, effect chains, mixer commands, clip installs). Two
consequences follow, and both matter daily:

1. **Re-running unchanged code is a no-op.** Nothing duplicates, nothing
   is re-created. You can hammer Ctrl+L.
2. **The script is the source of truth for what it declares.** Rename or
   delete a script-owned track in the UI, and the next run re-asserts or
   removes it (to delete something permanently, delete it from `song`).

The contract on each field is **presence = ownership**: a key you declare
is managed by the diff (and re-converged); a key you omit is left alone.
That's why you can write a minimal script on top of a manually-set-up
project, and why `instrument = { id = "drumrack" }` with no `params` key
installs the device and then never touches its knobs.

```lua run
-- Lesson 5 — a whole sketch, as data
song = {
    bpm = 116,
    scenes = 3,                      -- grow-only: never trims your scenes
    tracks = {
        { uid = 1, name = "Kick", type = "midi", volume = 1.0,
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.6,
              ["Filter Cutoff"] = 0.2, ["Filter Env"] = 0.85,
              ["Amp Decay"] = 0.2 } } },
        { uid = 2, name = "Keys", type = "midi", volume = 0.5,
          pan = -0.25,
          instrument = { id = "electricpiano", params = { ["Bell"] = 0.3 } },
          fx = { { id = "chorus", params = { ["Wet/Dry"] = 0.2 } },
                 { id = "delay",
                   params = { ["Wet/Dry"] = 0.2, ["Feedback"] = 0.3 } } },
          clips = {
              [1] = { beats = 4, notes = {
                  { 0.0, 1.5, 53, 0.5 }, { 0.0, 1.5, 60, 0.45 },
                  { 1.0, 1.2, 57, 0.35 },
                  { 2.0, 1.5, 55, 0.5 }, { 2.0, 1.5, 62, 0.45 },
                  { 3.0, 1.2, 59, 0.35 } } },
              [2] = { beats = 4, notes = {
                  { 0.0, 1.5, 50, 0.5 }, { 0.0, 1.5, 57, 0.45 },
                  { 2.0, 1.5, 52, 0.5 }, { 2.0, 1.5, 59, 0.45 } } } } },
        { name = "Pluck", type = "midi", volume = 0.55, pan = 0.25,
          instrument = { id = "karplus", params = { ["Damping"] = 0.4 } },
          fx = { { id = "plate", params = { ["Wet/Dry"] = 0.25 } } },
          clips = {
              [2] = { beats = 4, notes = {
                  { 0.0, 0.5, 84, 0.7 },  { 0.75, 0.25, 89, 0.55 },
                  { 2.0, 0.5, 82, 0.6 },  { 3.25, 0.5, 80, 0.5 } } } } },
        { name = "Kick line", type = "midi", volume = 1.0,
          clips = { [1] = { beats = 4, notes = {
              { 0.0, 0.25, 36, 1.0 }, { 2.5, 0.25, 36, 0.8 } } } } },
    },
}

yawn.set_playing(true)
improv.every(1, function(beat)       -- a pulse on the Keys track, under
    yawn.note(1, 41, 40, 0.2, 0, beat)   -- everything, from code
end)

-- @assert bpm:116
-- @assert tracks:4
-- @assert playing
-- @assert inst:1:electricpiano
-- @assert midinotes:1:1:6
-- @assert midinotes:1:2:4
-- @assert midinotes:2:2:4
-- @assert midinotes:3:1:2
```

(case check: the instrument-less "Kick line" is silent until an
instrument lands on it — the sketch's point is the *data shape*, not the
mix.)

Notes inside `clips` are `{start, dur, pitch, vel, [ch]}` tuples in beats
(velocities 0..1) — positional, or named (`{ start = 0, dur = 0.5, pitch
= 36, vel = 1.0, ch = 0 }`). Devices are factory **ids** (`subsynth`,
`fmsynth`, `karplus`, `wavetable`, `sampler`, `granular`, `drumrack`,
`drumsynth`, `drumslop`, `vocoder`, `drawbarorgan`, `electricpiano` …;
effects `reverb`, `plate`, `eq`, `compressor`, `chorus`, `delay`,
`filter`, `distortion`, `phaser` … — the browser's device list is the
same catalog). **Params by name or index** both work:
`["Filter Cutoff"] = 0.2` or `[8] = 0.2` — names are matched
case-insensitively at apply time, and a typo is a *warning listing the
device's actual parameter names*, not a mystery (the drum-kit examples
above lean on that check).

Editing flow, concretely: open `~` → **Code** tab. It shows the `song`
block **regenerated from the live project** — your UI hand-edits flow back
into readable code. Click **Sync** and the tool patches the script's own
`song` block in place (comments survive byte-for-byte) so the file the
next run reads is the project you played with. This is round-trip on
purpose: tweak a cutoff on-screen, watch those param names turn into a
certain line of Lua.

## Lesson 6 — forging sound: offline renders

Sometimes you want a *sound as a file*: a snapshot of an instrument
playing your notes, with your FX on it, ready to load into a sampler pad
or clip. `yawn.render` does exactly that — on a worker thread, offline,
never touching the audio stream — and delivers the buffer wherever a
`target` says:

```lua run
-- Lesson 6 — the sample forge
song = {
    bpm = 116,
    tracks = {
        { uid = 1, name = "Kit", type = "midi",
          instrument = { id = "drumrack" } },
        { uid = 2, name = "Sampler", type = "midi",
          instrument = { id = "sampler" } },
    },
}

local jobKick = yawn.render{
    device = "subsynth",                 -- any Factory instrument id
    params = { ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.5,
               ["Filter Env"] = 0.9, ["Amp Decay"] = 0.4 },
    notes  = { { 0.0, 0.4, 36, 1.0 } },  -- one note: the pitch shapes it
    beats  = 1.0,                        -- rendered length (at 116 BPM)
    tail   = 1.5,                        -- +release tail on top
    target = { kind = "drumrack", track = 0, pad = 60, name = "logo kick" },
}

local jobZap = yawn.render{
    device = "karplus", params = { Damping = 0.2 },
    notes  = { { 0.0, 0.3, 60, 0.9 } },
    beats  = 0.5, tail = 1.0,
    target = { kind = "sampler", track = 1, name = "zap" },
}

yawn.log("forge jobs " .. jobKick .. " and " .. jobZap .. " rendering...")

-- @assert tracks:2
-- @assert inst:0:drumrack
-- @assert inst:1:sampler
-- @assert delivered:2
```

The console reports back when each job lands
(`prerender: delivered 'logo kick' (N frames → drumrack)`), and the kit's
pad 60 / the Sampler's sample tab now hold the material. `track`/`scene` targets put rendered audio straight into
a clip slot.

Two things to know about a render job:

- **Determinism.** The spec is fixed at *enqueue* time (params resolve
  against a fresh prototype the moment you call `yawn.render`, and the
  whole spec is a value copy). Same spec, same audio — every time. Add
  `seed = 42` when using generative sources for the same guarantee.
- **Job lifecycle.** `yawn.render` returns a job id; `yawn.cancel_render(id)`
  cancels; failures (unknown device, bad param name) surface in the
  console immediately with the reason, not three layers down.

Other targets: `{ kind = "granular" | "vocoder" | "drumslop", track = n }`,
`{ kind = "drumrack", track = n, pad = k }` (explicit pad — no side
effects on whatever pad is selected), `{ kind = "clip", track = n, scene
= m }`, `{ kind = "library", name = "..." }` (indexed into the content
browser), and `{ kind = "file", path = "..." }` (straight to disk, your
scratch experiments).

`yawn.render` composes with everything else: a routed render can be the
*result* of a generative process, and its file/clip/sampler delivery then
pays off in Lesson 7 — but you can just as well use it as a "print this
sound" button.

## Lesson 7 — sound from math: forging buffers

You can also synthesize samples *in the script* — sample by sample, or
shaped in the frequency domain — and place them straight into the
project:

```lua run
-- Lesson 7 — the buffer forge
song = {
    bpm = 110,
    tracks = {
        { uid = 1, name = "Pad", type = "audio", volume = 0.8 },
        { uid = 2, name = "Ticks", type = "midi",
          instrument = { id = "sampler", params = {
              ["Root Note"] = 0.3125 } },        -- pitch 60 = C4
        }
    },
}

local sr = yawn.buffer_info(yawn.new_buffer{ frames = 1 }).sr
local twoPi = 2 * math.pi

-- ── an additive pad: one sine voice per harmonic, stereo-detuned ──
local HARMONICS = 8
local PAD = yawn.new_buffer{
    frames = sr * 4, channels = 2,
    fill = function(f, ch)
        local t = (f - 1) / sr
        local acc = 0.0
        for k = 1, HARMONICS do
            acc = acc + (1.0 / (k * k)) *
                  math.sin(twoPi * 110 * k * (ch == 1 and 1.0 or 1.003) * t)
        end
        return acc * math.exp(-t * 1.2)
    end,
}
yawn.buffer_normalize(PAD, 0.8)

-- ── a shaped-noise tick: white noise through an exponent decay ──
local NF = math.floor(sr * 0.08)
local HAT = yawn.new_buffer{
    frames = NF, channels = 1,
    fill = function(f)
        local t = (f - 1) / sr
        return 0.8 * math.exp(-t * 90) * (math.random() * 2 - 1)
    end,
}
yawn.buffer_fade(HAT, 4, NF // 4)        -- 4-frame fade-in, quarter fade-out

-- ── spectral surgery: FFT → bin mask → IFFT ──
local mono = yawn.buffer_mixdown(PAD)
local raw  = yawn.buffer_data(mono).data[1]
local n = 65536
local x = {}                               -- yawn.fft needs a power of two:
for i = 1, n do x[i] = raw[i] end          -- copy the first 65536 frames
local spec = yawn.fft(x)
local knee = n // 8                        -- everything above rolls off 24 dB/oct
for k = knee + 1, n do
    local g = 0.5 ^ ((k - knee) / (n // 24))
    spec[2 * k - 1] = (spec[2 * k - 1] or 0) * g
    spec[2 * k]     = (spec[2 * k]     or 0) * g
end
local SHAPED = yawn.new_buffer{
    frames = n, channels = 1, data = { yawn.ifft(spec) } }
yawn.buffer_normalize(SHAPED, 0.8)

-- ── the placements ──
yawn.set_clip(0, 1, PAD, "forged pad")     -- 4-s stereo loop in scene 1
yawn.load_sample(1, HAT, "sampler")        -- the tick, playable live

improv.every(0.5, function(beat)           -- ghost ticks over the pad
    if math.random() < 0.6 then
        yawn.note(1, 60, 100, 0.05, 0, beat)
    end
end)

yawn.launch_scene(1)

-- @assert tracks:2
-- @assert playing
-- @assert audioclip:0:1
```

The full shaping vocabulary: `yawn.buffer_gain`, `buffer_normalize`,
`buffer_fade(in, out)`, `buffer_mix(dst, src, level)`, `buffer_reverse`,
plus the new-handle constructors `buffer_slice`, `buffer_concat`,
`buffer_repeat`, `buffer_mixdown`; and the math helpers `yawn.fft` /
`yawn.ifft` (interleaved re/im tables, power-of-two length) and
`yawn.polyblep(phase, dt)` (alias-free saw edges — the classic fix for a
naive oscillator). Handles are freeable (`yawn.free_buffer`), and
`buffer_data` round-trips samples back out as Lua tables for inspection.

Two placement functions differ in one respect worth internalizing:
`yawn.set_clip(track, scene, handle, [name])` puts a buffer into a session
slot as an *audio clip* (launchable, loopable, arrangement-able), while
`yawn.load_sample(track, handle, [kind])` publishes it into a
sampler/granular/drumrack-family instrument so *notes* can trigger it.
The first is a song; the second is a voice. (Sustained forges usually want
the clip; one-shots usually want the sampler — and nothing stops you from
doing both, which is exactly what Lesson 6's kick job could also do.)

If the visual engine is more your stage: `yawn.set_visual(track, scene,
"31_ghost_accent.frag", "ghost accents")` puts one of YAWN's bundled
shaders on a visual track as a clip — and the improv layer's upcoming
fires are published to shaders as ghost uniforms (`iGhost0..7`,
`iBeatBarFrac`), which is how the ghost-accent shader pre-glows notes just
before they sound. See [visual.md](visual.md); see example
`05_sample_synth.lua` for a full workstation script that synthesizes an
entire kit plus visuals from math.

## Lesson 8 — the show, and the house it runs in

A handful of platform facts that round out the picture:

**The sandbox.** Live code runs in a separate Lua state from everything
else, confined to `base`/`math`/`table`/`string` — `io`, `os`, `package`,
`debug`, `dofile`, `loadfile` and `require` don't exist. A count-based
instruction budget (a generous 400M instructions for a full script run,
250k per scheduled callback) turns runaway loops into a clean console
error instead of a frozen UI. And the engines don't strangle each other:
live code and controller scripts are independent Lua states — one crashing
takes neither down.

**Ownership and persistence.** `song`-declared items are script-owned and
re-asserted on every apply; imperatively-created items (a param you set
live, an instrument you picked in the UI) are ordinary project state.
Everything materialized — tracks, instruments, effect chains, clips, even
rendered samples — is saved into `project.json` normally, so a project
built half-by-code plays back perfectly with live coding switched off.
(The raw notes the improv layer fires are the exception — the Freeze
button below is how they become durable.) The script file itself lives in the
project's `livecode/` folder (the manager scans it on save; more `.lua`
files, each runnable; `autorun` is opt-in in the project's `livecode`
section).

**The improv ledger — freezing takes.** The improv layer is performance
*only* by design: nothing it fires changes the project (that's why a
re-run doesn't erase your previous notes). When a take is good enough to
keep, click **Freeze** on any `~` console tab: every note the layer has
fired since the last freeze is serialized into a normalized MIDI clip on
a new sidecar `Take N` track — and sidecar tracks are invisible to the
song reconciler, so your freeze survives re-runs, reloads, and save/load.

**Failure modes, catalogued.**

| Console message | Meaning, and the fix |
|---|---|
| `unknown param 'Cutoff' (valid: …)` | A param name didn't resolve; the valid list is right there. Match it (case doesn't matter). |
| `render: unknown device id 'synt'` | Factory ids are exact (`subsynth`, not `synth`). |
| `audio: track 2 has no matching sampler instrument` | A `load_sample`/sampler delivery targeted a track without that instrument — declare it in `song` and run again. |
| `live code: run failed — previous generation continues` | A script error; the last-good layer is still performing. Read the error just above, fix, Ctrl+L. |
| `live code instruction budget exceeded` | A loop ran past the budget (400M instructions per run / 250k per callback). Move heavy per-sample math into `yawn.new_buffer` fill functions (they get the big budget) — callbacks must stay small. |
| A callback stopped firing silently | After 3 consecutive dispatch errors that callback is auto-cancelled; the console recorded them. Check the log, clear it, re-run. |
| `late drop` counts climbing | Your machine hitched past the lookahead. Thicken the pattern, raise `improv.lookahead` (up to 0.5 s), or accept `late_policy("drop")`'s silence-better-than-late trade. |

**Keep going:** the five demo songs in `assets/examples/livecode/` are the
next rung (`01` declarative sketch, `02` improv-only performer, `03` the
two-layer convention, `04` a drum-kit forge, `05` the buffer-from-math
workstation) — the suite runs them, so you can read them as executable
documentation. [live-coding.md](live-coding.md) is the full API surface
and scheduling model; [visual.md](visual.md) takes shaders and ghosts
from here. The notations keyboard, the code lens and Ctrl+Enter are worth
fingering: the tool chain — *console to run, run to console* — is the
muscle this whole feature exercises.
