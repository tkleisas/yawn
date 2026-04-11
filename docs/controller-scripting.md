# Controller Scripting

YAWN includes an embedded Lua 5.4 scripting engine for MIDI controller integration. Controllers are supported through scripts that define how hardware buttons, encoders, pads, and displays interact with the DAW.

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

### Tracks & Instruments

| Function | Description |
|----------|-------------|
| `yawn.get_selected_track()` | Returns 0-based index of selected track |
| `yawn.get_track_count()` | Returns number of tracks |
| `yawn.get_track_name(track)` | Returns track name string (or nil) |
| `yawn.get_instrument_name(track)` | Returns instrument display name (or nil) |
| `yawn.get_instrument_id(track)` | Returns instrument type ID (e.g. `"drumrack"`, `"fmsynth"`) |

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

### Utility

| Function | Description |
|----------|-------------|
| `yawn.log(message)` | Log message to YAWN's console (appears as `[Lua]`) |

---

## Ableton Push 1

### Button & CC Map

| CC | Button | Function |
|----|--------|----------|
| 3 | Tap Tempo | Tap tempo with LED flash |
| 9 | Metronome | Toggle metronome (LED synced) |
| 14 | Tempo Encoder | BPM adjust (Shift = fine: 0.1 BPM) |
| 44 | Left Arrow | Previous parameter page |
| 45 | Right Arrow | Next parameter page |
| 46 | Up Arrow | Drum bank up (drum mode only) |
| 47 | Down Arrow | Drum bank down (drum mode only) |
| 49 | Shift | Held modifier for fine control |
| 50 | Note | Switch to Note mode |
| 51 | Session | Switch to Session mode |
| 54 | Octave Down | Lower pad octave |
| 55 | Octave Up | Raise pad octave |
| 58 | Scale | Enter/exit scale editor; Shift+Scale toggles chromatic/scale |
| 71-78 | Encoders 1-8 | Device parameters (paged) / Scale edit params |
| 79 | Master Encoder | Master volume (Shift = fine) |
| 85 | Play | Play/Stop (LED synced) |
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

#### Session Mode (Stub)

Pressing the Session button enters Session mode. Clip launching is planned for a future update.

### Touch Strip

The touch strip sends pitch bend messages and is mapped to two functions:

- **Default**: Pitch bend — sent to the selected track. Snaps back to center when released.
- **Shift held**: Mod wheel (CC 1) — sent to the selected track. Stays at the last position when released.

### SysEx Display

The Push 1's 4-line, 68-character LCD is driven via SysEx:

| Line | Content |
|------|---------|
| 1 | Parameter names (8 columns) |
| 2 | Parameter values (8 columns) |
| 3 | Track name + page indicator |
| 4 | Instrument name + pad mode info (or tap tempo flash) |

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
