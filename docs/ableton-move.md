# Ableton Move controller integration

Move talks to YAWN as a standard USB-MIDI device — no Ableton Live
pairing required. Because Move's OLED screen and animated feedback
are driven by a proprietary protocol that only Live speaks, YAWN uses
the **toast system** (top-center screen banner) as the visual channel
instead. Pad LEDs, however, do accept standard Push-family MIDI, so
scale visualization and press feedback work natively on the hardware.

Everything below is observed on real hardware through YAWN's MIDI
Monitor panel. If a button does something different on your unit, the
mapping is a single-line change in
`scripts/controllers/ableton_move/init.lua`.

## What you'll see when you plug in

On connect, YAWN:

1. Scans the four MIDI input ports Move exposes, matches them against
   the `Ableton Move` substring in the manifest, and claims them all.
2. Loads `scripts/controllers/ableton_move/init.lua` and runs
   `on_connect`.
3. Paints the 32 pads with the current scale (C Major by default):
   **C pads in red**, other in-scale notes in **mid-green**, nothing
   else lit.
4. Shows an **Ableton Move connected** toast in the YAWN window.

Move's own screen keeps saying *Connect Live via USB-C*. That's
expected and unavoidable without the Live pairing protocol — but
every controller action produces a toast, so you're never left
wondering whether a press registered.

## Pad grid

**32 velocity-sensitive pads**, notes 68–99 (bottom-left to top-right,
4 rows × 8 cols). Row offset (semitones between rows) is configurable:

| Layout  | Row interval | Use case |
|---------|--------------|----------|
| 4ths    | 5 semitones  | Default; Push-style playable layout |
| 3rds    | 4 semitones  | Tight chord voicings |
| 5ths    | 7 semitones  | Wider grid, piano-like reach |
| Octaves | 12 semitones | Same scale pattern repeated up the grid |

In **scale mode**, each row steps through the current scale's degrees
(7 per octave for diatonic modes), so every pad is guaranteed to be
in-scale. In **chromatic mode**, each pad is exactly +1 semitone from
the pad to its left; out-of-scale pads light up **dim dark-blue**
(easy to see at a glance but not visually dominant).

### LED scheme

| State | Velocity | Color |
|-------|----------|-------|
| Root (C in C Major) | 2 | Red |
| In-scale | 10 | Mid-green |
| Out-of-scale (chromatic mode only) | 19 | Dim dark-blue |
| Pressed (held) | 29 | Bright yellow |
| Off | 0 | Dark |

Pad LEDs are refreshed once a second (heartbeat) because Move's
firmware resets them on its own if it doesn't keep seeing the host.
~32 MIDI messages per second is negligible for USB MIDI.

### Press ripple

On every pad press, a **concentric wave** expands outward over
~500 ms:

- Pressed pad: **bright yellow** (held as long as the pad is down)
- Ring at distance 1 (immediate neighbors): **cyan**
- Ring at distance 2: **blue**
- Ring at distance 3 and beyond: **dim brown** (final fade)

Each ring holds for five 30-Hz ticks (~166 ms) before advancing. The
ripple stays off held pads so the origin keeps its solid yellow. Tweak
`RIPPLE_COLORS` / `RIPPLE_TICKS_PER_STEP` at the top of `init.lua` for
different palettes or speeds.

## Encoders

### Eight parameter encoders (top row, smooth)

Relative MIDI on CC 71–78, channel 1. Mapped to the first 8 parameters
of the currently-selected track's instrument (page 0). Shift fine-
tunes (×0.1 speed). No LED feedback; changes surface as device-panel
knob animation in the YAWN UI.

### Main encoder (top-left, **dented**)

CC 14; touch on Note A1 (57). Each click of the detent = one unit of
navigation.

| Modifier | Turn left / right | Touch (no turn) |
|----------|-------------------|------------------|
| plain    | Selected track −1 / +1 (cycles) | Toast current track |
| Shift    | Scale index −1 / +1 (cycles `scales.catalog`) | Toast current scale |

### Master encoder (right side, **smooth**)

CC 79; touch on Note G#1 (56). Smooth so fractional values feel
natural.

| Modifier | Turn left / right | Touch (no turn) |
|----------|-------------------|------------------|
| plain    | Master volume ±1% per tick | Toast current volume |
| Shift    | BPM ±0.5 per tick | Toast current BPM |

## Buttons

All button CCs send on channel 1; press = 127, release = 0. For most
mappings only the press edge does anything.

### Transport & play state

| Button  | CC  | Action |
|---------|-----|--------|
| Play    | 85  | Toggle play/stop; toast `Play` / `Stop` |
| Record  | 86  | Toggle record arm; toast `Record armed` (warn color) or `Record off` |
| Shift   | 49  | Held modifier — see combos throughout |

### Navigation

| Button        | CC  | Plain action | Shift action |
|---------------|-----|--------------|--------------|
| +             | 55  | Octave +1    | Root note +1 semitone |
| −             | 54  | Octave −1    | Root note −1 semitone |
| >             | 63  | Selected track +1 (cycles) | Scale +1 (cycles) |
| <             | 62  | Selected track −1 (cycles) | Scale −1 (cycles) |
| Track/Session | 50  | Toggle scale ↔ chromatic submode | Cycle layout preset (4ths → 3rds → 5ths → Octaves) |

### Direct track select (four buttons, channel 1)

The CC numbers are inverted vs. visual order (track 1 has the highest
CC, track 4 the lowest).

| Button  | CC  | Action |
|---------|-----|--------|
| Track 1 | 43  | `set_selected_track(0)` |
| Track 2 | 42  | `set_selected_track(1)` |
| Track 3 | 41  | `set_selected_track(2)` |
| Track 4 | 40  | `set_selected_track(3)` |

### Scene launch (numbered buttons)

The 16 numbered buttons send **notes** (not CCs), range E0–G1
(MIDI 28–43).

| Button  | Note | Action |
|---------|------|--------|
| 1–8     | 28–35 (E0–B0) | `launch_scene(0..7)`, toast `Scene N` |
| 9–16    | 36–43 (C1–G1) | Logged as unmapped; available for future mapping |

### Known-but-unwired buttons

These buttons are recognised but YAWN doesn't yet have a Lua API for
the underlying feature. Pressing them produces a `<Name> (not
implemented)` toast in warn color so you always know the press
registered.

| Button  | CC  | Missing API |
|---------|-----|-------------|
| Undo    | 56  | `yawn.undo()` / `yawn.redo()` |
| Loop    | 58  | transport loop toggle |
| Capture | 52  | retroactive capture of recent MIDI |
| Sample  | 118 | sampler record arm |
| Mute    | 88  | per-track mute |
| Copy    | 60  | clip copy/paste |
| X       | 119 | clip delete |
| < Back  | 51  | navigate back in a modal flow |

Anyone who adds those Lua bindings gets a button mapping for free —
just wire the corresponding CC in `init.lua`.

## Toast notifications

Because Move's screen is unavailable, YAWN surfaces state changes as a
top-center banner in its own window. Every controller action that
changes state emits a toast:

- **Scale / root / octave changes** show the full current scale name
  (`C Dorian`, `F# Penta Min`)
- **Track changes** show `Track 2: Bass` (or just the index if the
  track has no name)
- **Layout / submode changes** show the preset name
- **Volume / BPM** show the numeric value with units
- **Play / record / scene launch** show plain labels
- **Unmapped presses** show `<Name> (not implemented)` in warn color

Toasts replace each other instead of queuing, so fast encoder spins
just update the text in place rather than flooding the screen. Default
hold is 1.5 s followed by a 200 ms fade. See `src/ui/ToastManager.h`.

## Quick reference card

```
┌─ plain ─────────────────────────┐  ┌─ Shift + ───────────────────────┐
│  +  octave up                   │  │  +  root note +1 semitone       │
│  −  octave down                 │  │  −  root note −1 semitone       │
│  >  selected track +1           │  │  >  scale +1                    │
│  <  selected track −1           │  │  <  scale −1                    │
│  Track/Session  scale↔chromatic │  │  Track/Session  cycle layout    │
│                                 │  │                                 │
│  Main enc.  track ±1 (dented)   │  │  Main enc.   scale ±1           │
│  Master enc. master volume      │  │  Master enc. BPM                │
│                                 │  │                                 │
│  Play / Record  transport       │  │                                 │
│  Track 1..4     jump to track   │  │                                 │
│  Button 1..8    launch scene    │  │                                 │
└─────────────────────────────────┘  └─────────────────────────────────┘
```

## Files

- `scripts/controllers/ableton_move/manifest.lua` — port matching
- `scripts/controllers/ableton_move/init.lua` — all the logic above
- `scripts/controllers/ableton_move/pads.lua` — pad-grid layout +
  scale walking
- `scripts/controllers/ableton_move/scales.lua` — 30+ scale definitions
  (shared catalog with Push 1)

Hot reload: **View → Reload Controller Scripts** disconnects the
current controller, rescans all manifests, and reconnects without
restarting YAWN. Handy while tuning colors or sensitivities.

## What's not implemented (and what it would take)

- **OLED screen updates** — requires reverse-engineering Ableton's
  pairing handshake + framebuffer protocol over USB bulk endpoints.
  Not MIDI. Weeks of work; the toast system is the pragmatic
  substitute.
- **Per-button LEDs** — Move has additional LEDs (Play, Record, Shift,
  Track 1–4, etc.) that may also respond to NoteOn-back. Not yet
  probed. Worth testing as a follow-up.
- **Velocity curves** — current pad velocity is passed through
  unmodified. Could be linearised / curved if needed.
- **Mute / Undo / Copy / etc.** — need C++ Lua bindings added to
  `LuaEngine`. Once those exist the button mappings are one-line
  additions.
