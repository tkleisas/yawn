# Y.A.W.N User Manual

*Yet another Audio Workstation New — a DAW with a built-in VJ engine, written by an AI that cannot hear and a PM who can. This manual was written by the same AI, so at least the table of contents is accurate.*

This manual is organized from "I just launched it" to "I script my shows with it":

1. [Getting started](#1-getting-started)
2. [Concepts](#2-concepts)
3. [The main window](#3-the-main-window)
4. [Menus](#4-menus)
5. [Session view](#5-session-view)
6. [Arrangement view](#6-arrangement-view)
7. [Mixer & routing](#7-mixer--routing)
8. [Browser](#8-browser)
9. [Devices](#9-devices)
10. [Piano roll](#10-piano-roll)
11. [Automation & modulation](#11-automation--modulation)
12. [MIDI](#12-midi)
13. [The visual engine](#13-the-visual-engine)
14. [Recording](#14-recording)
15. [Auto-Sampler](#15-auto-sampler)
16. [Stem separation & audio→MIDI](#16-stem-separation--audiomidi)
17. [Export](#17-export)
18. [Projects & files](#18-projects--files)
19. [Preferences](#19-preferences)
20. [Keyboard & mouse reference](#20-keyboard--mouse-reference)
21. [Scripting & remote control](#21-scripting--remote-control)
22. [Troubleshooting](#22-troubleshooting)

---

## 1. Getting started

### 1.1 Launch

Run the `YAWN` binary. On first launch the app:

- opens the audio device at 48 kHz / 128 frames (change later in **Edit → Preferences → Audio**),
- creates `~/.yawn/` for settings, the preset library, the loop index and downloaded models,
- opens a default project with five tracks: two Audio, two MIDI, one Visual.

Audio runs whether or not anything is on the screen — you can play the virtual keyboard
(`Q2W3ER5T6Y7UI9O0P` = one octave of piano keys) on a MIDI track right away.

### 1.2 Make sound in 30 seconds

1. Click the **MIDI 1** track header to select the track.
2. The track already has a Subtractive Synth loaded — press `Q`..`P` on the virtual keyboard.
3. Press **Space** to start the transport; the metronome is the `MET 1` button in the transport bar.
4. Double-click an empty clip slot on MIDI 1 to open the piano roll and draw some notes (see §10).
5. Click the clip to launch it. Click the **Stop** square in the top-left corner of the grid to stop everything.

### 1.3 Load your own audio

Drag an audio file (WAV, FLAC, OGG, AIFF, MP3) from your file manager onto any clip slot
on an Audio track. Click the clip to launch it.

### 1.4 Where things are written

| What | Where |
|---|---|
| Settings | `~/.yawn/settings.json` |
| Presets (global) | `~/.yawn/presets/` |
| Loop library index | `~/.yawn/library.db` |
| Downloaded models (Demucs etc.) | `~/.yawn/models/` |
| Projects | wherever you save them — a `<name>.yawn/` **folder** |
| Log | `yawn.log` in the working directory (send it with bug reports) |

---

## 2. Concepts

**Tracks** come in three types:

- **Audio** — plays audio clips (files, recordings) and hosts audio effects.
- **MIDI** — plays MIDI clips, hosts an instrument, MIDI effects and audio effects.
- **Visual** — plays visual clips (shaders, video, 3D, live input) into the visual engine (§13).

**Scenes** are horizontal rows of clips in the session grid. Launching a scene launches
every clip in the row — the classic way to structure a live set.

**Clips** are the units of playback: audio clips (a region of a sample), MIDI clips
(notes + CC), visual clips (a shader/video/model plus parameters). Clips loop by default.

**Session vs Arrangement.** Session view is the clip-launching grid (jam / live).
Arrangement view is the linear timeline (compose / edit). The same project holds both;
a per-track [Ses|Arr] switch decides which engine plays that track (§6.6).

**Detail panel** (`D`) is the bottom area that shows the selected track's device strip
(instrument + effects) — or the Visual Params panel when a Visual track is selected.

---

## 3. The main window

```
┌────────────────────────────────────────────────────────────────┐
│ File Edit View Track Scene Tools Help                          │  menu bar
├────────────────────────────────────────────────────────────────┤
│ AUDIO 120.00 BPM  4/4  TAP MET 1   ⏮ ■ ▶ ●  AUTO ARR   LINK off │  transport
├──────────┬──────────────────────────────────────┬──────────────┤
│ ▪ Stop   │ track headers →                      │ Browser      │
│          │ clip grid (tracks × scenes)          │ Files        │
│ scenes 1-8│                                     │ Presets      │
│          │                                      │ Loops        │
├──────────┴──────────────────────────────────────┤ Models       │
│ mixer strips (M)                    │ MASTER    │ Clip / MIDI  │
├─────────────────────────────────────────────────┴──────────────┤
│ Detail panel / Visual Params (D)                               │
└────────────────────────────────────────────────────────────────┘
```

**Menu bar** — see §4. Click a title to open; Escape closes.

**Transport bar** — track-type selector for new tracks, BPM (double-click to type),
time signature, `TAP` (tap tempo), `MET 1` (metronome), home/stop/play/record,
`AUTO` (global automation record arm), `ARR` (arrangement record arm), playhead
position, CPU/MEM meters, velocity selector for the virtual keyboard, and the
**LINK** button (Ableton Link — shows `LINK off` / `LINK on` / peer count).

**Session grid** — the clip launcher (§5). The left column holds scene labels and
the **■ Stop** button (stops all clips).

**Mixer** (`M`) — channel strips (§7). The left gutter toggles extra rows:
**I/O** (input routing), **Snd** (sends), **Rtn** (return buses).

**Browser** — right-side panel with Files / Presets / Loops / Models / Clip / MIDI tabs (§8).

**Detail panel** (`D`) — device strip for the selected track. On a Visual track this
becomes the **Visual Params** panel instead (§13.2).

---

## 4. Menus

### File
| Item | Shortcut | What it does |
|---|---|---|
| New Project | `Ctrl+N` | Empty project (unsaved changes are lost — save first) |
| Open Project | `Ctrl+O` | Open a `.yawn` project folder |
| Save Project | `Ctrl+S` | Save to the current project folder |
| Save As... | `Ctrl+Shift+S` | Save to a new folder |
| Export Audio | | Offline audio render (§17.1) |
| Export Video (mp4)… | | Render arrangement + visuals to mp4 (§17.2) |
| Quit | `Ctrl+Q` | Exit |

### Edit
Undo (`Ctrl+Z`), Redo (`Ctrl+Y`), **Clear All Automation…** (with confirmation),
**Preferences** (§19).

### View
**Session View** / **Arrangement View** (`Tab` toggles), **Toggle Mixer** (`M`),
**Detail Panel** (`D`), **Reload Controller Scripts** (re-scans `scripts/controllers/`
without a restart), **Visual Output Window**, **Visual Output Fullscreen** (`F11`),
and the **Post FX** submenu for the master visual chain (§13.4).

### Track
Add Audio / MIDI / Visual Track, Rename Track, and the same track operations found
in the track-header context menu (delete, etc.).

### Scene
**Insert Scene** (`Ins`) — inserts an empty scene at the selection, shifting clips down.

### Tools
**Generate Preset Library** (balanced / alien / descriptive names) — procedurally
generates a few hundred usable presets for every device in the background; results
appear in Browser → Presets. **Generate Presets for Selected Track's Device** does
the same for just the selected device.

### Help
**About Y.A.W.N** and the **Keyboard Shortcuts** cheat sheet (scrollable).

---

## 5. Session view

The grid: columns are tracks, rows are scenes. Each cell holds one clip.

### 5.1 Launching and stopping

- **Click a clip** — launch it (or stop it if it's playing). Launch timing follows the
  clip's **launch quantize** (right-click → *Launch Quantize*: immediate, beat, bar, …;
  default comes from Preferences → Defaults).
- **Click a scene label** (the row numbers 1..8 on the left) — launch the whole scene.
- **■ Stop** (top-left corner) — stop every clip and clear launch memory.
- **Right-click a clip** — context menu: Copy / Cut / Paste / Duplicate, Launch Quantize,
  Delete, Rename, **Record Length**, **Record Loop**, plus audio-clip extras
  (**Separate Stems**, **Convert to MIDI**, see §16).

**Follow actions** chain clips automatically: a clip can, after N bars, jump to
Next / Previous / First / Last / Random / Any / Play Again / Stop — with A/B probability
splits for variation. Set them from the clip's context menu.

### 5.2 Recording into the grid

1. Arm the track (the **R** button in its mixer strip; MIDI tracks record MIDI,
   audio tracks record audio).
2. Press the transport **● record** button, then **Space** (or the ▶ button) to roll.
3. Play. When you stop, the take lands in the first empty slot of the armed track
   as a looping clip — subject to **Record Length** and **Record Loop** settings
   and the record quantize (Preferences → Defaults).

Count-in (0/1/2/4 bars) and metronome mode live in Preferences → Metronome.

---

## 6. Arrangement view

`Tab` switches between Session and Arrangement. The arrangement is a beat/bar timeline
with one lane per track, a ruler with loop markers, and optional per-track automation lanes.

### 6.1 Editing clips

| Gesture | Effect |
|---|---|
| Click | Select clip |
| Drag body | Move (same or across tracks) |
| Drag edges | Resize (snaps to grid) |
| Double-click empty space (MIDI track) | Create MIDI clip |
| `Ctrl+D` | Duplicate selection |
| `Delete` / `Backspace` | Delete selection |
| Right-click | Clip context menu (incl. **Send to Arrangement** from session, stretch, etc.) |

Snap-to-grid resolution: off / bar / beat / ½ / ¼ / ⅛. Zoom: 4–120 px/beat.

### 6.2 Ruler, loop range, playhead

- Click the ruler to move the playhead.
- `Shift+click` sets loop start; `Shift+right-click` sets loop end; drag the green
  markers to adjust; `L` toggles looping.
- `F` toggles follow-playhead auto-scroll.

### 6.3 Automation lanes

Expand a track (▶ button in its header) to reveal automation lanes. Click to add a
breakpoint, drag to move it, right-click to delete. Lanes exist for mixer params
(volume, pan, sends), instrument/effect params, and visual params (§11).

### 6.4 [Ses|Arr] — which engine plays a track

Each track header has a **[Ses|Arr]** segmented switch:

- **Ses** — the track is played by the session engine (the clip grid).
- **Arr** — the track is played by the arrangement timeline.

When you drop arrangement clips onto a track it flips to Arr automatically on the next
view switch; flip it back any time. The switch state is per track and saved with the
project (undo-wrapped, `Ctrl+Z`).

### 6.5 Recording to the arrangement

The **ARR** transport button arms arrangement recording: play and trigger clips —
everything you perform is captured as arrangement blocks. Stop to keep or discard.

---

## 7. Mixer & routing

Toggle with `M`. Each strip: name, mute/solo (**M**/**S**), record arm (**R**),
monitor mode (`Auto`), input selector, pan, fader with peak meter and dB readout.

- **64 tracks**, stereo peak metering on every strip.
- **8 send/return buses** — show sends with the **Snd** toggle in the left gutter;
  returns appear (with the **Rtn** toggle) as extra strips next to **MASTER**.
  Every return strip and the master strip host a full effect chain (right-click → Add FX).
- **Master** — volume fader + meter + the **Stop All** button (stops every clip).
- **I/O** toggle — per-track input routing rows (audio input channel, monitor In/Auto/Off).
- Right-click any fader/pan/knob for **MIDI Learn** / Reset to default (§12.2).

Level metering also feeds the visual engine's audio-reactive uniforms (§13.3).

---

## 8. Browser

Right-side panel with six tabs:

- **Files** — your sample libraries. **+ Folder** registers a folder; the tree shows
  folders and audio files, double-click or drag onto a slot to load. Right-click a
  root folder for rescan/remove.
- **Presets** — every device preset (factory-generated + your saves), grouped by
  device with a type filter and search. Double-click to load onto the selected track.
- **Loops** — the MIDI loop library (~2,600 factory loops in 24 genres plus your own).
  Filter by drums/bass/lead/chord/misc, search, double-click to drop into the selected
  slot, or **drag directly onto a session cell**. Save your own with right-click on a
  MIDI clip → **Save to Loop Library…**.
- **Models** — glTF 3D models for visual clips (§13.6).
- **Clip** — the selected clip's properties, including the clip automation envelope
  editor and clip length.
- **MIDI** — the MIDI monitor (live message log: notes, CC, pitch bend, clock, SysEx).

---

## 9. Devices

Devices live on the detail panel's device strip (`D`). Add from the track-header
context menu (right-click a track header) or the Track menu: **Set Instrument**,
**Add Audio Effect**, **Add MIDI Effect**. Reorder by dragging headers, bypass with
the On pill, remove with ×. Every device knob supports double-click text entry,
right-click MIDI Learn, and preset save/load from the device header.

### 9.1 Instruments (15)

| Device | In one line |
|---|---|
| Subtractive Synth | 2-osc analog-style, SVF filter, 16 voices |
| FM Synth | 4-operator FM, 8 algorithms with visual algo display |
| Sampler | Single-sample player with ADSR + pitch tracking |
| Multisampler | Multi-zone sample instrument (key/vel zones) — see Auto-Sampler §15 |
| Drum Rack | 128 sample pads, per-pad FX chain, choke groups, kit presets |
| Drum Synth | Fully synthesized 8-piece GM-mapped kit (no samples needed) |
| DrumSlop | Loop slicer drum machine, 16 pads |
| Karplus-Strong | Physical-model plucked string |
| Wavetable Synth | 5 wavetable types, morph, unison, sub osc |
| Granular Synth | Sample-based granular cloud engine |
| Vocoder | 4–32 band vocoder, 4 carrier types |
| String Machine | Solina-style ensemble strings (the 1974 sound) |
| Drawbar Organ | Hammond B-3, 9 drawbars, percussion, auto-rotary |
| Electric Piano | Rhodes / Wurli / Suitcase (auto-phaser) modes |
| Instrument Rack | Up to 8 layered chains (any instrument + per-chain FX) |

### 9.2 Audio effects (23 + utilities)

Reverb, Convolution Reverb (38 bundled IRs), Delay, Ping-Pong Delay, EQ, Spline EQ
(drag-node graphical EQ), Compressor, Limiter, Noise Gate, Filter, Chorus, Phaser,
Wah, Rotary (Leslie), Auto Panner, Envelope Follower (auto-wah + modulation source),
Distortion, Bitcrusher, Tape Emulation, Amp Simulator, Neural Amp (`.nam` amp captures,
4 bundled), Beat Repeat, Buffer Repeat, Resampler, Clock Drift, CD Error — plus
analysis utilities **Oscilloscope**, **Spectrum Analyzer**, **Tuner**.

### 9.3 MIDI effects (8)

**Arpeggiator** (free or transport-synced), **Chord**, **Scale**, **Note Length**,
**Velocity**, **MIDI Random**, **MIDI Pitch**, **LFO** (a modulation source you can
link to any parameter, §11.3).

---

## 10. Piano roll

Double-click a MIDI clip (or an empty slot on a MIDI track) to open it. `D` closes it.

- **Draw tool**: click = place note, drag = stretch; drag note body = move, drag right
  edge = resize; right-click a note = delete. `Delete` removes the selection.
- **Velocity lane** at the bottom: drag note bars to adjust velocity.
- **Clip-ops column** (left): **Dup** (duplicate selection), **x2** (double clip
  length repeating content), **/2** (halve), **Rev** (reverse), **Clear** (delete all
  notes), **1.1.1** (crop clip to loop start).
- On Drum Synth / Drum Rack tracks it switches to a labeled drum-row mode.
- Scroll wheel = horizontal scroll; piano-key column drag = vertical scroll;
  `Ctrl` + drag on keys = vertical zoom. `Esc` closes (or clears the selection first).

---

## 11. Automation & modulation

Three complementary systems:

### 11.1 Breakpoint automation

- **Arrangement lanes** (§6.3) — absolute envelopes on the timeline.
- **Clip automation** — envelopes stored inside the clip, relative to clip start,
  looping with it. Edited in Browser → Clip.
- **Recording** — arm **AUTO** (global) plus Touch/Latch per track, then wiggle knobs
  during playback; moves are written as breakpoints.

### 11.2 Targets

Instrument params, audio-effect params, MIDI-effect params, mixer (volume, pan, sends),
visual knobs A–H and any `@range` shader uniform. Arrangement lane overrides clip
envelope; LFOs compose on top.

### 11.3 LFOs

The **LFO** MIDI effect is a modulation source: 5 waveforms, tempo-synced rate, depth,
phase, polarity. Link it to any parameter (across tracks too) via its target picker —
links are ID-based and survive device reordering.

### 11.4 Envelope Follower as a source

The Envelope Follower audio effect publishes its live envelope as a modulation value —
usable by visual params and automation consumers (set Filter Type = Off to use it as a
pure analysis source).

---

## 12. MIDI

### 12.1 Ports

Preferences → MIDI lists every input and output port (your own app is filtered out —
you can't loop YAWN into itself). Check the ports you want open. Per-track input
port/channel filtering lives in the track's I/O row.

**MPE** is supported (per-note bend/slide/pressure with zone management).

### 12.2 MIDI Learn

Right-click almost any knob, fader or button → **MIDI Learn**, then move a CC or press
a note on your controller. Mappings persist in the project; right-click again to
remove. Works for instrument/effect params, mixer, transport and visual knobs.

### 12.3 Virtual keyboard

`Q2W3ER5T6Y7UI9O0P` plays notes on the selected track (Z/X octave, velocity from the
transport's Vel selector).

### 12.4 Hardware controllers

Lua-scripted controller support (`scripts/controllers/`) with bundled scripts for
Ableton Push 1, Ableton Move, Korg nanoKONTROL2 and Yamaha Reface DX. See
[controller-scripting.md](controller-scripting.md) for the API. **View → Reload
Controller Scripts** picks up edits without a restart.

---

## 13. The visual engine

YAWN is also a VJ tool: Visual tracks render GPU shader layers into a separate
**Visual Output** window (View → Visual Output Window; `F11` fullscreen — put it on
the projector, keep the UI on your screen).

### 13.1 Visual clips

A visual clip holds a shader (Shadertoy-style `mainImage` GLSL), a video, a 3D model,
or a live input. Create by right-clicking a Visual track slot, by dropping a video file
(`.mp4/.mov/.mkv/.webm/...` — transcoded in the background), a `.glb` model, or by
choosing **Live Input ▸** for a capture device.

Session clips launch like audio; arrangement visual clips are first-class timeline
blocks (right-click → **Send to Arrangement**). Track volume = layer opacity; layers
composite bottom-up with Normal / Add / Multiply / Screen blend modes.

### 13.2 Visual Params panel

With a Visual track selected, `D` opens Visual Params:

- **Knobs A–H** — generic performance knobs mapped to `knobA..knobH` uniforms in every
  shader. **Knobs the current shader doesn't read are dimmed**, so you can see which
  letters do something. Each knob has a beat-synced LFO and MIDI Learn (right-click).
- **Custom params** — any `uniform float x; // @range 0..1 default=0.5` in your shader
  becomes a knob automatically.
- **Shader Chain** — per-clip ordered FX passes with bypass × and drag reorder;
  **+ Add Pass** appends one.
- **Post FX** — the master chain (§13.4).

### 13.3 Audio reactivity

Shaders get `iAudioLevel`, `iAudioLow/Mid/High` (3-band analyzer), `iKick` (transient
impulse), plus an FFT texture on `iChannel0` (row 0 spectrum, row 1 waveform) and
transport uniforms (`iBeat`, `iTransportTime`, `iTransportPlaying`) — Shadertoy
conventions throughout.

### 13.4 Master Post FX

View → Post FX adds Bloom / Pixelate / Kaleidoscope / Chromatic Split / Vignette /
Invert after the compositor. Params appear at the bottom of Visual Params; the chain
persists with the project.

### 13.5 Video

Dropped videos are transcoded (ffmpeg) to an intra-frame 640×360 proxy with extracted
audio on a sibling track. Playback modes: free-running or **bar-synced** (the whole
video fits N bars), rate 0.25×–4×, trim ranges.

### 13.6 3D & Lua

`.glb/.gltf` models (incl. skeletal animation) render into `iChannel2` with standard
`modelPos/Rot/Spin/Scale` uniforms — automatable like anything else. Per-clip Lua
scripts (`tick(ctx)` returning transforms) drive multi-instance rendering; bundled
examples in `assets/examples/scripts/`.

The full shader-authoring guide is [visual.md](visual.md).

---

## 14. Recording

- **Audio** — arm an Audio track (R), choose the input channel in its I/O row, record.
  Overdub mode layers takes. Multi-channel interfaces capture all selected channels.
- **MIDI** — arm a MIDI track, play a hardware keyboard (or the virtual one).
- **Count-in** — Preferences → Metronome (0/1/2/4 bars).
- **Record quantize** — Preferences → Defaults (None / Beat / Bar).
- **Arrangement recording** — the **ARR** transport button (§6.5).

---

## 15. Auto-Sampler

Build a Multisampler instrument out of any MIDI sound source — hardware synth, VST3,
or another app — automatically:

1. Put a **Multisampler** on a track, click **Auto-Sample…** in its panel.
2. Pick the MIDI port/channel to drive and the audio input to capture (on Windows you
   can capture the synth's own output via a `[loopback]` device — no virtual cables).
3. Set the note grid (default C2–C7 in major thirds, 4 velocity layers).
4. **Test Note** checks the path with a live VU meter; set Level to peak just below 0 dBFS.
5. **Capture** — a few minutes later the zones populate the Multisampler: silence-trimmed,
   midpoint-split, ready to play. Save as a preset to keep the whole instrument (zones
   included) portable.

---

## 16. Stem separation & audio→MIDI

Right-click an audio clip:

- **Separate Stems** (Demucs v4) — splits the clip into drums / bass / other / vocals
  on four new tracks. CPU-only and genuinely slow (minutes per song); progress toasts,
  `Esc` cancels. The model ships with release builds (or downloads on first use).
- **Convert to MIDI** (Basic Pitch) — polyphonic transcription onto a new MIDI track
  in the same scene.

---

## 17. Export

### 17.1 Audio (File → Export Audio)

Offline (faster-than-realtime) render of the full arrangement or the loop region to
WAV / FLAC / OGG, with bit depth (16 / 24 / float32) and sample-rate selection,
progress display and cancellation. Put a **Limiter** on the master if you want a
safety ceiling.

### 17.2 Video (File → Export Video (mp4)…)

Renders the arrangement *with the visual engine output* to an mp4 — audio mix +
composited visuals. Requires ffmpeg on the system.

---

## 18. Projects & files

A project is a **folder** `<name>.yawn/` containing `project.json` plus everything it
references: `samples/`, `media/` (transcoded video), `shaders/`, `models/`, `scripts/`,
`presets/`. Moving the folder moves the whole project. Undo (`Ctrl+Z`) / Redo (`Ctrl+Y`)
cover essentially every edit, with action merging.

---

## 19. Preferences

Edit → Preferences. Five tabs:

| Tab | Contents |
|---|---|
| **Audio** | Output/input devices, sample rate, buffer size, latency compensation (PDC), master oversampling |
| **MIDI** | Input/output port checklists |
| **Defaults** | Default launch quantize, default record quantize, UI font size |
| **Metronome** | Mode (Always/Record/Play/Off), count-in bars, volume, visual style |
| **Link** | Enable Ableton Link, transport start/stop sync, live peer count |

OK applies (audio engine restarts if the device/rate/buffer changed); Cancel discards.

---

## 20. Keyboard & mouse reference

### Keys

| Key | Action |
|---|---|
| `Space` | Play / Stop (launches default clips) |
| `Home` | Return to zero |
| `+` / `=` | Tempo +1 BPM |
| `-` / `_` | Tempo −1 BPM |
| `Tab` | Switch Session / Arrangement |
| `M` | Toggle mixer |
| `D` | Toggle detail panel |
| `F11` | Visual output fullscreen |
| `Ctrl+N/O/S/Shift+S/Q` | New / Open / Save / Save As / Quit |
| `Ctrl+Z / Ctrl+Y` | Undo / Redo |
| Arrows | Move clip selection (session) |
| `Shift+Arrows` | Move controller grid region |
| `Enter` | Launch / stop selected clip |
| `Delete` / `Bksp` | Clear selected clip |
| `Ctrl+C/X/V` | Copy / cut / paste clip |
| `Ctrl+D` | Duplicate clip (session: to next empty slot; arrangement: in place) |
| `G` | Toggle controller grid overlay |
| `Ins` | Insert scene below selection |
| `L` / `F` / `[` / `]` | Arrangement: loop / follow / set loop start / set loop end |
| `Q2W3ER5T6Y7UI9O0P` | Virtual keyboard notes |
| `Z` / `X` | Octave down / up |
| `Esc` | Close menu / exit fullscreen / quit |

### Mouse

| Gesture | Action |
|---|---|
| Left-click clip | Launch (session) / select (arrangement) |
| Right-click clip / track header / scene label | Context menus |
| Right-click knob/fader | MIDI Learn / reset |
| Double-click knob | Type a precise value |
| Drag clip body / edges | Move / resize (arrangement) |
| Click ruler / `Shift+click` / `Shift+right-click` | Playhead / loop start / loop end |
| Drag & drop audio file | Load clip into slot |
| Scroll wheel | Scroll lists/menus; zoom (with Ctrl where noted) |

---

## 21. Scripting & remote control

### 21.1 Controller scripts

`scripts/controllers/` — Lua API for hardware controllers; see
[controller-scripting.md](controller-scripting.md).

### 21.2 The UI command channel (`YAWN_CMD`)

For UI automation, testing and remote control, launch with the `YAWN_CMD` environment
variable and the app listens on `127.0.0.1:<port>` for line-based commands:

```bash
YAWN_CMD=8765 ./YAWN          # or YAWN_CMD=1 for an ephemeral port (logged)
python3 scripts/ui_probe.py 8765 ping        # → OK pong
python3 scripts/ui_probe.py 8765 menu Scene  # opens the Scene menu
python3 scripts/ui_probe.py 8765 shot /tmp/ui.png   # screenshot
```

Verbs: `ping`, `shot <path.png>` (GL back-buffer capture — works under Wayland),
`key <name>`, `click|rclick|dclick|mousemove <x> <y>`, `view session|arrangement`,
`menu <title>`, `menuitem <label>`, `addtrack audio|midi|visual`,
`addinstrument <track> <name>`, `addeffect <track> <name>`,
`setparam <track> <paramIndex> <value>`, `dialog preferences|about|shortcuts|export`,
`dbg` (input-dispatch diagnostics), `quit [code]`.

**State queries** (every reply is `OK <value>`): `get tracks`, `get scenes`,
`get tracktype <i>`, `get instrument <i>`, `get param <track> <index>`,
`get playing`, `get bpm`, `get view`, `get modal`, `get undo` / `get redo`
(stack state + description). **Actions**: `undo`, `redo` (the real app path),
`new` (fresh project, no dirty-check dialog), `wait <frames>` (deferred ack —
deterministic frame sync, no blind sleeps). `quit <code>` sets the process exit
code, so a harness can fail CI through the app itself. `scripts/ui_probe.py`
is the reference client (plain sockets — any language works).

**Smoke harness**: `scripts/ui_smoke.py [binary]` launches the app, runs ~30
assertions through the channel (queries, undo/redo roundtrip, screenshot PNG
validation, modal open/close, clean exit) and exits non-zero on any failure.
Runs headless under Xvfb (`xvfb-run -a python3 scripts/ui_smoke.py build/bin/YAWN`)
and is wired into CI as the "UI smoke (Xvfb)" step of the Linux build job.

---

## 22. Troubleshooting

- **No sound** — Preferences → Audio: right output device? Track not muted? Mixer
  master up? On Linux, check the app isn't competing with another ALSA client.
- **MIDI keyboard silent** — Preferences → MIDI: enable the input port. Arm the track
  (R) or play the virtual keyboard to compare.
- **Visual output black** — View → Visual Output Window must be open; the track must
  be a Visual track with a launched clip; check the layer's track volume.
- **High CPU / crackles** — raise the buffer size (Preferences → Audio), disable
  latency compensation to compare, check the CPU meter in the transport.
- **A plugin crashes the editor** — VST3 editors run in a separate process
  (`yawn_vst3_host`); a dying plugin shouldn't take the DAW down. Reopen the editor.
- **Something looks off** — `yawn.log` (working directory) has the gory details;
  attach it when reporting.

---

*Manual for Y.A.W.N v0.87.x. Feature reference: [features.md](features.md). Build
instructions: [building.md](building.md). Shader guide: [visual.md](visual.md).
Controller API: [controller-scripting.md](controller-scripting.md).*
