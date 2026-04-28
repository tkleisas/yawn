# Controller Scripting

YAWN includes an embedded Lua 5.4 scripting engine for MIDI controller integration. Controllers are supported through scripts that define how hardware buttons, encoders, pads, and displays interact with the DAW.

## Supported controllers

| Controller | Surface | Where |
|---|---|---|
| **Ableton Push 1** | 8×8 pads, 8 encoders, 4-line LCD, transport | [§ Ableton Push 1](#ableton-push-1) |
| **Ableton Move** | 4×8 pads, 2 encoders (touch-sensitive), scene & nav buttons | [docs/ableton-move.md](ableton-move.md) |
| **Korg nanoKONTROL2** | 8 faders, 8 knobs, 24 channel buttons, transport | [§ Korg nanoKONTROL2](#korg-nanokontrol2) |
| **Yamaha Reface DX** | 37-key FM synth, touch strip, pitch bend, sustain | [§ Yamaha Reface DX](#yamaha-reface-dx) |

Adding a new controller is a Lua script + manifest — see [§ Writing a New Controller Script](#writing-a-new-controller-script). Scripts hot-reload via **View → Reload Controller Scripts**.

## Architecture

```
Push 1 (USB MIDI)
    |
    v
ControllerMidiPort (RtMidi, per-controller I/O)
    |
    v
RawMidiRingBuffer (SPSC, byte-oriented, 16KB)
    |
    v
ControllerManager::update()  [UI thread, 60Hz]
    |-- drain ring buffer --> call on_midi(data) per message
    |-- every 2nd frame ----> call on_tick()  [~30Hz]
    |
    v
LuaEngine (Lua 5.4, vendored amalgamation)
    |
    v
yawn.* API --> AudioEngine command queue / direct param access
```

### Script Discovery

Scripts live in `scripts/controllers/<name>/` with two required files:

- **`manifest.lua`** — Declares the controller name and port matching patterns
- **`init.lua`** — Main script, loaded after manifest matches

Example manifest:
```lua
return {
    name = "Ableton Push 1",
    port_patterns = {"Ableton Push"},  -- substring match against MIDI port names
}
```

On startup, YAWN scans all script directories, matches port patterns against available MIDI ports, and auto-connects the first match. Multiple MIDI ports matching the same pattern are merged into a single input stream.

### Lua Callbacks

Scripts implement these global functions:

| Callback | When | Notes |
|----------|------|-------|
| `on_connect()` | Controller connected | Initialize display, LEDs |
| `on_disconnect()` | Controller disconnected | Clear display, cleanup |
| `on_midi(data)` | Each MIDI message received | `data` is a table of bytes |
| `on_tick()` | ~30Hz (every 2nd frame) | Display updates, LED sync, animations |

### Module System

Scripts can use `require()` to load sibling Lua files. For example, the Push 1 script uses:

```lua
local scales = require("scales")  -- loads scales.lua from same directory
local pads   = require("pads")    -- loads pads.lua from same directory
```

---

## Lua API Reference

### Transport

| Function | Description |
|----------|-------------|
| `yawn.is_playing()` | Returns `true` if transport is playing |
| `yawn.set_playing(bool)` | Start/stop playback via command queue |
| `yawn.get_bpm()` | Returns current BPM |
| `yawn.set_bpm(bpm)` | Set BPM (20-999) via command queue |
| `yawn.tap_tempo()` | Trigger tap tempo (same as UI button, with visual feedback) |
| `yawn.get_metronome_enabled()` | Returns metronome state |
| `yawn.set_metronome_enabled(bool)` | Toggle metronome via command queue |
| `yawn.get_master_volume()` | Returns master volume (0.0-2.0) |
| `yawn.set_master_volume(vol)` | Set master volume via command queue |
| `yawn.get_loop()` | Returns `true` if loop is enabled |
| `yawn.set_loop(bool)` | Enable/disable transport loop |

### Tracks & Instruments

| Function | Description |
|----------|-------------|
| `yawn.get_selected_track()` | Returns 0-based index of selected track |
| `yawn.set_selected_track(track)` | Set the selected track (0-based) |
| `yawn.get_track_count()` | Returns number of tracks |
| `yawn.get_track_name(track)` | Returns track name string (or nil) |
| `yawn.get_track_volume(track)` | Returns track volume (0.0–2.0) from mixer |
| `yawn.set_track_volume(track, vol)` | Set track volume via command queue |
| `yawn.get_track_pan(track)` | Returns track pan (-1.0–1.0) from mixer |
| `yawn.set_track_pan(track, pan)` | Set track pan via command queue |
| `yawn.get_track_mute(track)` | Returns `true` if track is muted |
| `yawn.set_track_mute(track, muted)` | Mute/unmute a track via command queue |
| `yawn.get_track_solo(track)` | Returns `true` if track is soloed |
| `yawn.set_track_solo(track, soloed)` | Solo/unsolo a track via command queue |
| `yawn.get_track_type(track)` | Returns `"audio"` or `"midi"` |
| `yawn.get_track_color(track)` | Returns track color index |
| `yawn.get_instrument_name(track)` | Returns instrument display name (or nil) |
| `yawn.get_instrument_id(track)` | Returns instrument type ID (e.g. `"drumrack"`, `"fmsynth"`) |
| `yawn.is_track_armed(track)` | Returns `true` if track is record-armed |
| `yawn.set_track_armed(track, armed)` | Arm/disarm a track for recording |
| `yawn.is_recording()` | Returns `true` if transport is in record mode |
| `yawn.set_recording(bool, flags)` | Enable/disable transport recording |
| `yawn.get_record_length_bars(track)` | Returns fixed record length in bars (0 = unlimited) |

#### Instrument Type IDs

| ID | Instrument |
|----|------------|
| `drumrack` | Drum Rack (128-pad sampler) |
| `drumslop` | Drum Slop |
| `subsynth` | Subtractive Synth |
| `fmsynth` | FM Synth |
| `wavetable` | Wavetable Synth |
| `granular` | Granular Synth |
| `karplus` | Karplus-Strong |
| `sampler` | Sampler |
| `multisampler` | Multisampler |
| `vocoder` | Vocoder |
| `instrack` | Instrument Rack |

### Device Parameters

All device parameter functions take `(device_type, chain_index, param_index)`:
- `device_type`: `"instrument"`, `"audio_effect"`, or `"midi_effect"`
- `chain_index`: 0-based index within the effect chain
- `param_index`: 0-based parameter index

| Function | Description |
|----------|-------------|
| `yawn.get_device_param_count(type, ci)` | Number of parameters |
| `yawn.get_device_param_name(type, ci, pi)` | Parameter name string |
| `yawn.get_device_param_value(type, ci, pi)` | Current value (float) |
| `yawn.get_device_param_min(type, ci, pi)` | Minimum value |
| `yawn.get_device_param_max(type, ci, pi)` | Maximum value |
| `yawn.get_device_param_display(type, ci, pi)` | Formatted display string (with unit or label) |
| `yawn.get_device_param_label_count(type, ci, pi)` | Number of value labels (>0 = stepped/discrete) |
| `yawn.set_device_param(type, ci, pi, value)` | Set parameter value directly |

### MIDI Output

| Function | Description |
|----------|-------------|
| `yawn.midi_send(b1, b2, ...)` | Send raw MIDI bytes to controller |
| `yawn.midi_send_sysex(table)` | Send SysEx message (table of bytes) |
| `yawn.send_note_to_track(track, note, velocity, channel)` | Send note to a track's instrument via command queue |
| `yawn.send_pitchbend_to_track(track, value14, channel)` | Send 14-bit pitch bend (0-16383, center=8192) |
| `yawn.send_cc_to_track(track, cc, value, channel)` | Send CC message (0-127) to track |

### Session / Clip Launching

| Function | Description |
|----------|-------------|
| `yawn.get_num_scenes()` | Returns number of scenes |
| `yawn.get_clip_slot_state(track, scene)` | Returns table `{type, playing, recording, armed}` — `type` is `"empty"`, `"audio"`, or `"midi"` |
| `yawn.launch_clip(track, scene)` | Launch or stop the clip at the given slot |
| `yawn.stop_clip(track)` | Stop the playing clip on a track |
| `yawn.launch_scene(scene)` | Launch all clips in a scene row |
| `yawn.start_record(track, scene)` | Start recording into a clip slot (auto-detects audio/MIDI track type) |
| `yawn.stop_record(track)` | Stop recording on a track |
| `yawn.set_session_focus(trackOff, sceneOff, active)` | Set the controller's visible grid region (draws outline in UI) |

### Utility

| Function | Description |
|----------|-------------|
| `yawn.log(message)` | Log message to YAWN's console (appears as `[Lua]`) |
| `yawn.toast(text, duration_sec)` | Show a top-center status banner in the YAWN window. Duration is in seconds (default ~1.5). Designed as a screen substitute for controllers without their own display (Move, nanoKONTROL2, Reface DX). Thread-safe — fire from any callback |

---

## Ableton Push 1

### Button & CC Map

| CC | Button | Function |
|----|--------|----------|
| 3 | Tap Tempo | Tap tempo with LED flash |
| 9 | Metronome | Toggle metronome (LED synced) |
| 14 | Tempo Encoder | BPM adjust (Shift = fine: 0.1 BPM) |
| 20-27 | Track Select (top row) | Select track (session mode); LED on for selected track |
| 36-43 | Scene Launch (right column) | Launch scene row (session mode); LED color reflects scene state |
| 44 | Left Arrow | Param page prev / session grid navigate left |
| 45 | Right Arrow | Param page next / session grid navigate right |
| 46 | Up Arrow | Drum bank up / session grid navigate up |
| 47 | Down Arrow | Drum bank down / session grid navigate down |
| 49 | Shift | Held modifier for fine control |
| 50 | Note | Switch to Note mode |
| 51 | Session | Switch to Session mode |
| 54 | Octave Down | Lower pad octave |
| 55 | Octave Up | Raise pad octave |
| 58 | Scale | Enter/exit scale editor; Shift+Scale toggles chromatic/scale |
| 71-78 | Encoders 1-8 | Device parameters (paged) / Scale edit params |
| 79 | Master Encoder | Master volume (Shift = fine) |
| 85 | Play | Play/Stop (LED synced) |
| 86 | Record | Toggle transport record; Shift+Record = arm selected track |
| 102-109 | Track State (bottom row) | Toggle track arm (session mode); LED on when armed |
| PB (0xE0) | Touch Strip | Pitch bend (default) / Mod wheel (Shift held) |

### Pad Modes

#### Note Mode (Chromatic)

Default mode for melodic instruments. Pads map directly to MIDI notes 36-99 (8x8 grid). All pads produce sound.

#### Note Mode (Scale)

Activated via the Scale button. Pads only play notes within the selected scale. Layout:
- **Columns** (left to right) = ascending scale degrees
- **Rows** (bottom to top) = shift by configurable interval (default: perfect 4th = 5 semitones)
- **Root note pads** are highlighted with a brighter LED color

Scale edit mode (press Scale button):
| Encoder | Control | Range |
|---------|---------|-------|
| 1 | Root note | C, C#, D, ... B |
| 2 | Scale type | Cycles through 30+ scales |
| 3 | Row interval | 1-7 semitones |
| 4 | Octave | 0-8 |

#### Drum Mode (4x4)

Auto-activates when the selected track has a DrumRack or DrumSlop instrument. The bottom-left 4x4 pads (16 pads) are active, mapping to MIDI notes 36-51 (GM drum range). Remaining pads are dark/inactive.

- **Up/Down arrows** page through drum banks (+/- 16 notes)
- Switching to a melodic instrument auto-switches back to Note mode

#### Session Mode

Pressing the Session button enters Session mode. The 8x8 pad grid maps to clip slots in the session view:

- **Columns** (left to right) = tracks
- **Rows** (top to bottom) = scenes (top row = first visible scene)
- **Pressing a pad** launches or stops the clip in that slot; armed empty slots start recording

**Pad LED colors**:
| Color | Meaning |
|-------|---------|
| Off | Empty slot |
| Dim red | Armed empty slot (ready to record) |
| Amber | Clip present (stopped) |
| Bright green | Clip playing |
| Red | Clip recording |

**Navigation**: Use the cursor keys (Left/Right/Up/Down) to scroll the 8x8 grid window across the session. The YAWN UI shows a red outline indicating which region the Push pads currently control.

**Top row buttons (CC 20-27)**: Select the corresponding track. The selected track's button LED is lit.

**Bottom row buttons (CC 102-109)**: Toggle record arm on the corresponding track. Armed tracks show a lit LED.

**Scene launch buttons (CC 36-43, right column)**: Launch all clips in a scene row. LED colors reflect scene state (green = playing, amber = has clips, red = recording, dim = empty).

### Touch Strip

The touch strip sends pitch bend messages and is mapped to two functions:

- **Default**: Pitch bend — sent to the selected track. Snaps back to center when released.
- **Shift held**: Mod wheel (CC 1) — sent to the selected track. Stays at the last position when released.

### SysEx Display

The Push 1's 4-line, 68-character LCD is driven via SysEx:

| Line | Content (Note/Drum mode) | Content (Session mode) |
|------|--------------------------|------------------------|
| 1 | Parameter names (8 columns) | Track names (8 columns) |
| 2 | Parameter values (8 columns) | Track armed/type status |
| 3 | Track name + page indicator | Scene & track range |
| 4 | Instrument name + pad mode | Transport & recording status |

In scale edit mode, all 4 lines show the scale editor UI.

### Available Scales

#### Western
Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Whole Tone, Diminished HW, Augmented

#### Pentatonic
Major Pentatonic, Minor Pentatonic, Blues

#### Maqam / Eastern (12-TET Approximations)
Hijaz, Bayati, Rast, Nahawand, Kurd, Saba, Ajam, Nikriz, Sikah, Husseini, Phrygian Dominant, Double Harmonic, Hungarian Minor, Hicaz Kar, Ussak

> **Note**: Several maqamat (Bayati, Rast, Sikah) traditionally use quarter-tones that cannot be perfectly represented in 12-TET. These are the closest semitone approximations. The scale data structure supports `tones_per_octave` for future TET24 (quarter-tone) implementation.

---

## Korg nanoKONTROL2

The nanoKONTROL2 is a hands-on bank of 8 faders, 8 knobs, 24 channel
buttons, and a transport row. No display, no encoders — just tactile
mixer control. YAWN drives the channel-button LEDs for visual feedback
on mute/solo/arm state.

> **Setup**: The script assumes the unit is in **CC mode** (the
> power-on default factory setting). If buttons behave unexpectedly,
> reset to factory via the Korg Kontrol Editor or set all controls to
> **CC Momentary** mode.

### CC Map

| CC | Control | Function |
|----|---------|----------|
| 0–7 | Faders 1–8 | Track volume (0–127 → 0.0–2.0) |
| 16–23 | Knobs 1–8 | Track pan (0–127 → −1.0..+1.0) |
| 8–15 | Solo 1–8 | Toggle solo (LED on while soloed) |
| 48–55 | Mute 1–8 | Toggle mute (LED on while muted) |
| 56–63 | Rec 1–8 | Toggle record-arm (LED on while armed) |
| 41 | Play | Toggle play/stop |
| 42 | Stop | Stop transport |
| 45 | Rec | Toggle transport record |
| 46 | Cycle | Toggle loop (LED synced) |
| 43 / 58 | Rew / Track ◀ | Select previous track |
| 44 / 59 | Fwd / Track ▶ | Select next track |
| 60 | Marker Set | Re-sync all LEDs from engine state |
| 61 | Marker ◀ | Bank left (shift visible 8-channel window by −8) |
| 62 | Marker ▶ | Bank right (shift visible 8-channel window by +8) |

### Banking

The 8 channel strips map to a sliding window over YAWN's 64 tracks.
**Marker ◀ / ▶** shift the window by 8 channels at a time. The
selected track follows the bank shift, and a toast confirms the new
range (e.g. *Bank: T9-T16*).

### LED feedback

- Mute / solo / arm LEDs reflect engine state. Pressing a button
  toggles the state and updates the LED.
- The **Cycle** button LED tracks the transport loop state.
- The **Marker Set** button forces a full LED resync — useful if
  state has drifted (e.g. you muted from the YAWN UI while the
  controller was disconnected).
- Fader / knob LEDs are managed by the controller's firmware.

---

## Yamaha Reface DX

The Reface DX is a 37-key FM synth with a touch strip, pitch bend
wheel, and sustain. The script handles the **CC surface only** —
notes, pitch bend, and sustain are routed through YAWN's standard
MIDI engine via per-track MIDI input, with no script involvement.

### CC Map

| CC | Control | Function |
|----|---------|----------|
| 1 | Modulation / Touch Strip | Selected track's instrument param 0 (rate-limited toast on change) |
| 11 | Expression | Selected track volume |
| 7 | Volume | Master volume |
| 64 | Sustain | Handled natively by the MIDI engine |

### Touch Strip Behavior

The Reface touch strip rests at center (~64) when released. The script
maps it to instrument param 0 of the currently selected track:

- **Left edge** (CC 1 ≈ 0) → param min
- **Right edge** (CC 1 ≈ 127) → param max
- **Center** (released) → param midpoint

Use this to drive a synth's primary expression parameter (filter
cutoff, FM amount, etc.) live from the touch strip during play. The
script shows the parameter's current display value as a toast each
time it changes (rate-limited to one toast per unique value).

> **Per-track instruments**: Switch the selected track in YAWN to
> retarget the touch strip to a different instrument's param 0. Combine
> with the nanoKONTROL2's track navigation buttons for hands-free
> retargeting.

---

## Writing a New Controller Script

### 1. Create the Directory

```
scripts/controllers/my_controller/
    manifest.lua
    init.lua
```

### 2. Write the Manifest

```lua
return {
    name = "My Controller",
    port_patterns = {"My Controller"},  -- matches MIDI port names containing this string
}
```

### 3. Implement Callbacks

```lua
function on_connect()
    yawn.log("My Controller connected!")
end

function on_midi(data)
    local status = data[1]
    local d1 = data[2] or 0
    local d2 = data[3] or 0
    local msg_type = status & 0xF0

    if msg_type == 0xB0 then  -- CC
        -- Handle encoder/button CC messages
        yawn.log(string.format("CC %d = %d", d1, d2))
    end

    if msg_type == 0x90 then  -- Note On
        local track = yawn.get_selected_track()
        yawn.send_note_to_track(track, d1, d2, 0)
    end
end

function on_tick()
    -- Update display, sync LEDs, etc. (~30Hz)
end

function on_disconnect()
    yawn.log("My Controller disconnected")
end
```

### 4. Test

1. Connect your controller
2. Launch YAWN -- auto-detection will match the port pattern
3. Check the log for `[Lua]` messages
4. Use **View > Reload Controller Scripts** to reload without restarting

### Tips

- Use `yawn.log()` liberally during development
- MIDI bytes in `on_midi(data)` are 1-indexed Lua integers (not 0-indexed)
- Relative encoders typically send 1-63 for clockwise, 65-127 for counter-clockwise
- Use the command queue (`yawn.set_playing()`, `yawn.set_bpm()`, etc.) for state changes that should update the UI
- Direct parameter access (`yawn.set_device_param()`) is fine for real-time encoder control
