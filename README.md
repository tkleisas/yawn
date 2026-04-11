<p align="center">
  <img src="yawn_ico512x512.png" alt="Y.A.W.N Logo" width="128" />
</p>

<h1 align="center">Y.A.W.N</h1>
<h3 align="center">Yetanother Audio Workstation New</h3>

<p align="center">
  A cross-platform digital audio workstation inspired by Ableton Live.<br/>
  Session View · Arrangement · Mixer · VST3 · Instruments · Effects · MIDI · Recording · Automation · Presets · Controller Scripting<br/><br/>
  <em>Made with AI-Sloptronic™ technology</em><br/>
  <sub>Where "it compiles" is the new "it works" and every bug is a ✨feature request✨</sub>
</p>

---

> **⚠️ Disclaimer:** No human engineers were mass-employed in the making of this software.
> The entire codebase was produced through the ancient art of describing what you want to a machine
> and then spending twice as long explaining why that's not what you meant.
> Side effects may include: spontaneous filter resonance, existential questions about who actually wrote this,
> and an unshakeable feeling that the AI is just gaslighting you into thinking the bug is fixed.
>
> **⚠️ VST3 Disclaimer:** We have successfully taught an AI to host third-party plugins inside a DAW
> that was itself written by an AI. This is either the future of music production or the opening scene
> of a techno-horror film. The VST3 editors run in a separate process because JUCE plugins install
> Win32 hooks that freeze our event loop — a bug we diagnosed after 3 hours of "why is the window frozen"
> followed by the AI saying "Ah, I see the issue!" for the 47th time.

## Features

### Audio Engine
- **Real-time Audio Engine** — Lock-free audio thread with PortAudio (ASIO/WASAPI/ALSA), zero audio-thread allocations. The AI wrote it without being able to hear audio. We're not sure if that's a superpower or a disability.
- **Clip Playback** — Audio files (WAV, FLAC, OGG, AIFF, MP3), looping, gain, fade-in/out
- **Quantized Launching** — Launch clips on beat or bar boundaries with configurable quantize resolution (Next Bar, Next Beat, Immediate, 1/2, 1/4, 1/8, 1/16)
- **Transport** — Play/stop/record, BPM control, beat-synced position tracking, loop range with draggable markers
- **Metronome** — Synthesized click track with accent on downbeats, configurable volume & time signature, count-in (0/1/2/4 bars), mode selection (Always/Record Only/Play Only/Off)
- **Follow Actions** — 8 action types (Next, Previous, First, Last, Random, Any, Play Again, Stop), dual-action with probability (A/B chance), bar-count trigger duration
- **Time Stretching** — WSOLA (rhythmic/percussive) and Phase Vocoder (tonal/texture) algorithms, per-track speed ratio (0.25×–4×), 6 warp modes (Off/Auto/Beats/Tones/Texture/Repitch)
- **Transient Detection** — Adaptive threshold onset detection with BPM estimation, configurable sensitivity
- **Warp Markers** — Map original audio positions to target beat positions for flexible time-stretching

### Mixer & Routing
- **64-track Mixer** — Per-track volume, pan, mute, solo with peak metering. The AI mixed a song once. It sounded like a spreadsheet.
- **8 Send/Return Buses** — Pre/post-fader send routing with independent return channels
- **Master Bus** — Master volume with stereo metering
- **3-point Effect Insert** — Effect chains on tracks, return buses, and master
- **Audio Input Routing** — Per-track audio input channel selection, monitor modes (Auto/In/Off)
- **MIDI Routing** — Per-track MIDI input port/channel, output port/channel

### Recording
- **Audio Recording** — Per-track audio input recording with arm/disarm, overdub mode, multi-channel capture
- **MIDI Recording** — Record from hardware MIDI keyboards with note/CC capture, proper finalization on transport stop
- **Record Quantize** — Configurable quantize on record (None, Next Beat, Next Bar)
- **Count-in** — 0, 1, 2, or 4 bar count-in before recording starts

### Integrated Audio Effects

*14 hand-crafted artisanal effects, each lovingly hallucinated by an AI that has never used a compressor but has read 47 papers about them.*

- **Reverb** — Schroeder/Moorer algorithmic reverb (4 comb + 2 allpass filters)
- **Delay** — Stereo delay with tempo sync, feedback, and ping-pong mode
- **EQ** — 3-band parametric EQ (low shelf, mid peak, high shelf)
- **Compressor** — Dynamics compressor with threshold, ratio, attack, release, makeup gain
- **Filter** — Multi-mode SVF filter (lowpass, highpass, bandpass, notch) with 2× oversampled stability
- **Chorus** — Modulated delay with multiple voices
- **Distortion** — Waveshaper with soft clip, hard clip, and tube saturation modes
- **Tape Emulation** — Analog tape simulation with asymmetric saturation, wow/flutter, tape hiss, and tone rolloff
- **Amp Simulator** — Guitar/bass amp modelling with 4 amp types (Clean/Crunch/Lead/High Gain), 3-band tone stack, cabinet simulation
- **Tuner** — YIN pitch detection with frequency/cents/note display, reference pitch control (420–460 Hz), confidence indicator
- **Oscilloscope** — Real-time waveform visualizer (non-destructive analysis effect)
- **Spectrum Analyzer** — FFT-based frequency spectrum display (non-destructive analysis effect)

### VST3 Plugin Hosting

*The AI built a plugin host before learning what a plugin sounds like. It correctly implemented the entire VST3 COM interface on the first try. We're terrified.*

- **Plugin Scanning** — Automatic discovery in standard system paths (Program Files/Common Files/VST3), class enumeration with vendor/category info
- **VST3 Instruments** — Load third-party VST3 synths as track instruments with full parameter automation
- **VST3 Audio Effects** — Load VST3 effects in any effect chain slot (track, return, master)
- **Process-Isolated Editor** — Plugin GUIs run in a separate process (`yawn_vst3_host.exe`) via bidirectional IPC, because JUCE plugins install process-wide Win32 message hooks that would freeze our event loop
- **Parameter Sync** — Full bidirectional parameter sync between host and editor process
- **State Persistence** — Processor + controller state serialized with project (hex-encoded binary)
- **Generic Knob Grid** — Automatic parameter knob UI for plugins without custom editors

### Integrated Instruments
- **Subtractive Synth** — 2-oscillator analog-style synth with SVF filter, 23 parameters, 16-voice polyphony
- **FM Synth** — 4-operator FM synthesizer with 8 algorithm presets, 19 parameters
- **Sampler** — Sample playback with pitch tracking, linear interpolation, ADSR envelope
- **Karplus-Strong** — Physical modelling string synth with 4 exciter types, damping, body resonance, string stretch
- **Wavetable Synth** — 5 algorithmic wavetable types with position morphing, SVF filter, LFO modulation, sub oscillator, unison
- **Granular Synth** — Sample-based granular synthesis with 4 window shapes, position/spread/spray, scan, pitch jitter, stereo width
- **Vocoder** — Band-based vocoder with 4 carrier types (Saw/Square/Pulse/Noise), 4–32 bands, envelope followers, formant shift
- **Multisampler** — Multi-zone sample player with key/velocity mapping, per-zone tuning/volume/pan/loop, velocity crossfade, dual ADSR
- **Instrument Rack** — Multi-chain container (up to 8 chains) with key/velocity zones, per-chain volume/pan, chain enable/disable toggle, visual zone bars, add/remove chain UI
- **Drum Rack** — 128 pads with 4×4 grid display, 8-page navigation, per-pad sample loading via drag & drop, per-pad volume/pan/pitch knobs, waveform preview, playing/sample indicators
- **DrumSlop** — Loop slicer drum machine: auto/even/manual slicing, 16 pads with ADSR, SVF filter, per-pad effect chains, configurable MIDI base note

### MIDI
- **MIDI Engine** — Internal 16-bit velocity, 32-bit CC resolution (MIDI 2.0 ready)
- **MIDI I/O** — Hardware MIDI via RtMidi (WinMM/ALSA), multi-port input/output
- **MPE Support** — Per-note pitch bend, slide, pressure via zone management
- **8 MIDI Effects** — Arpeggiator (free-running & transport-synced), Chord, Scale, Note Length, Velocity, Random, Pitch, LFO
- **MIDI Learn** — Map any CC or Note to any parameter (instrument, effect, mixer, transport), learn mode with visual feedback, per-channel or omni, JSON persistence
- **MIDI Monitor** — Lock-free 65K-event ring buffer tracking all message types (Note, CC, PitchBend, Pressure, Clock, SysEx), port identification, millisecond timestamps

### Automation & Modulation
- **Automation Engine** — Per-parameter breakpoint envelopes with Read/Touch/Latch modes
- **Track Automation** — Automation lanes in arrangement view with click to add/drag/right-click delete breakpoints
- **Clip Automation** — Per-clip automation lanes (relative to clip start, loops with clip)
- **Automation Recording** — Touch/Latch parameter recording from UI knob interaction
- **LFO Device** — Per-track LFO with 5 waveforms (sine, triangle, saw, square, S&H), tempo sync, depth, phase, polarity
- **LFO Linking** — Stable ID-based linking to any instrument/effect/mixer parameter across tracks, survives reordering
- **Automation Targets** — Instrument params, audio effect params, MIDI effect params, mixer (volume, pan, sends)

### Session View
- **Clip Grid** — 8 visible tracks × 8 scenes, scrollable, clip launching with quantized triggers
- **Scene Management** — Insert, duplicate, delete scenes with undo support, automatic renumbering
- **Scene Launching** — Click scene label to launch all clips in a scene simultaneously
- **Follow Actions** — Per-clip chained actions with dual-action probability
- **Track Management** — Add, delete tracks with confirmation dialog (stops engine, shifts all arrays)
- **Context Menus** — Right-click track headers for type/instruments/effects, right-click scenes for insert/duplicate/delete, right-click clips for stop

### Arrangement View
- **Timeline Grid** — Horizontal beat/bar grid with zoom (4–120 px/beat), scroll, snap-to-grid (off/bar/beat/half/quarter/eighth)
- **Clip Placement** — Click to select, drag body to move (same + cross-track), drag edges to resize, double-click to create, Ctrl+D to duplicate, Delete to remove
- **Arrangement Playback Engine** — Per-track clip rendering (audio + MIDI) with fade-in/out, thread-safe clip submission
- **Session/Arrangement Toggle** — Per-track S/A button, auto-activates on view switch when clips exist
- **Automation Lanes** — Expandable per-track lanes showing breakpoint envelopes, visual curve rendering
- **Loop Range** — Green markers in ruler, Shift+click to set, drag to adjust, L key to toggle
- **Auto-Scroll** — Playhead stays visible during playback (F key to toggle)
- **Waveform Display** — Audio clip waveform rendering in arrangement blocks

### Project Management
- **Project Save/Load** — JSON-based `.yawn` format with full round-trip serialization
- **Serialized State** — Tracks, scenes, clip grid, instruments, effects, MIDI effects, mixer state, automation, arrangement clips, MIDI Learn mappings
- **Sample Management** — Referenced audio samples copied to project folder
- **Audio Export** — Offline render to WAV/FLAC/OGG with bit depth (Int16/Int24/Float32) and sample rate selection, scope (full arrangement or loop region), progress tracking with cancellation
- **Undo/Redo** — Full undo/redo system with action merging (Ctrl+Z / Ctrl+Y)

### UI Framework
- **Composable Widget Tree** — FlexBox layout engine with measure/layout two-pass system, stretch/flex/fixed size policies
- **Session Panel** — Ableton-style clip grid with scrollable tracks and scenes
- **Arrangement Panel** — Horizontal timeline with track headers, clip blocks, automation lanes, ruler, playhead, loop markers
- **Mixer Panel** — Channel strips with interactive faders, pan knobs, mute/solo buttons, peak metering
- **Device Chain Panel** — Composite widget architecture: DeviceWidget (header + grid + knobs + visualizer), SnapScrollContainer, neon arc knobs with 24-segment rendering
- **Grouped Instrument Layouts** — Instruments display knobs in logical sections (Global, Op 1–4, Filter, Amp, etc.) with inline graphical displays instead of flat grids
- **Instrument Display Widgets** — FM algorithm routing diagram, ADSR envelope curves, oscillator waveform previews, filter response curves, composite synth panels
- **Waveform Widget** — Interactive waveform display with zoom/scroll, overview bar, playhead tracking, transient markers, warp marker editing (create/drag/delete), loop region overlay
- **Piano Roll Editor** — MIDI note editing with draw/select/erase tools, zoom/scroll, velocity, snap-to-grid, follow-playhead mode, clip operations (duplicate, double, halve, reverse, clear, set 1.1.1 here)
- **Export Dialog** — Format/bit depth/sample rate selectors, scope selection, progress bar with cancellation
- **Preferences Dialog** — Audio devices, MIDI ports, default quantize, metronome settings
- **Primitive Widgets** — FwButton, FwToggle, FwKnob (with double-click text entry, step snapping, format callbacks), FwFader, Label, FwTextInput, FwNumberInput, FwDropDown with hover animations
- **Dialog System** — fw::Dialog base class with title bar, OK/Cancel, drag-to-move, Escape/Enter handling; AboutDialog, ConfirmDialog, ExportDialog, PreferencesDialog
- **Menu Bar** — File, Edit, View, Track, MIDI, Help menus with keyboard accelerators
- **Context Menus** — Right-click track headers, scene labels, clips, transport buttons, knobs for MIDI Learn
- **DPI Scaling** — Auto-detect display scale (SDL3), user override, scaled() helper for all layout constants
- **Panel Animations** — Smooth exponential-lerp height transitions on panel collapse/expand
- **Virtual Keyboard** — QWERTY-to-MIDI mapping (Q2W3ER5T6Y7UI9O0P), Z/X octave switching, per-key note tracking
- **Track Selection** — Click to select tracks, highlight in session & mixer views
- **Track Type Icons** — Waveform icon for audio tracks, DIN circle icon for MIDI tracks
- **Targeted Drag & Drop** — Drop audio files onto specific clip slots
- **Custom 2D Renderer** — Batched OpenGL 3.3 rendering with font atlas (stb_truetype)
- **Crash Handler** — Signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL) with stack traces (Windows: SymFromAddr, Unix: backtrace), crash log to `yawn.log`
- **Multi-window Ready** — Built on SDL3 for future detachable panels

### Controller Scripting

*The AI embedded a scripting engine inside a DAW it wrote, so you can control the DAW it wrote with scripts it wrote. We're three layers deep and the Push display actually works.*

- **Lua 5.4 Engine** — Embedded Lua scripting for MIDI controller integration, vendored amalgamation with yawn.* API
- **Auto-Detection** — Manifest-based controller matching: scripts declare port name patterns, YAWN auto-connects on startup
- **Multi-Port Support** — Controllers with multiple MIDI ports (e.g. Push 1's Live + User ports) are handled seamlessly via a shared ring buffer
- **yawn.* Lua API** — Full read/write access to device parameters, track/instrument info, MIDI output, SysEx, transport state, and logging
- **Device Parameter Control** — Read param count/name/value/min/max/display, set values via lock-free audio command queue
- **Hot Reload** — Menu → Reload Controller Scripts to disconnect, rescan, and reconnect without restarting
- **Port Exclusivity** — Controller-claimed MIDI ports are automatically excluded from the general MIDI engine (Windows exclusive access)

#### Ableton Push 1

- **Pad Modes** — Note mode (chromatic & scale), Drum mode (4×4 auto-switch for DrumRack), Session mode (stub)
- **30+ Scales** — Western modes, pentatonic, blues, and Maqam/Eastern scales (Hijaz, Bayati, Rast, Nahawand, Saba, and more)
- **Scale Editor** — Select root note, scale type, row interval, and octave directly from Push encoders
- **8 Encoders** — Relative-encoded CC 71–78 mapped to device parameters with paging, coarse/fine (Shift), and stepped param support
- **Transport Controls** — Play, Metronome, Tap Tempo, BPM encoder, Master Volume — all with button LED feedback
- **SysEx Display** — 4-line text display: param names/values, track name, instrument, scale/mode info
- **LED Ripple Animation** — Expanding ring animation on pad press with held-pad persistence
- **Auto-Detection** — Drum instruments auto-switch to 4×4 pad layout; melodic instruments restore note mode

> See [docs/controller-scripting.md](docs/controller-scripting.md) for the full Lua API reference, Push 1 button map, and guide to writing controller scripts.

### Quality
- **Test-Driven Development** — 844 unit & integration tests across 39 test suites via Google Test (because the AI doesn't trust itself either)
- **Zero audio-thread allocations** — All memory preallocated at startup
- **All instruments handle CC 123** (All Notes Off) for clean MIDI effect removal
- **Sloptronic-grade stability** — Filters clamped, state variables leashed, resonance domesticated

### Planned

- 🎛️ More controller scripts (Ableton Move, Novation Launchpad, etc.)
- 🐛 Whatever bugs the PM discovers by wiggling knobs at 3 AM

## Screenshots

![Y.A.W.N v0.1 — Session View with device chain panel](images/yawn_v.0.1.png)
*v0.1 — Session View showing the clip grid, mixer, and device chain panel with Arpeggiator → Subtractive Synth → Filter → Oscilloscope → EQ → Spectrum Analyzer.*

![Y.A.W.N v0.4.1 — Arrangement View](images/yawn_v.0.4.1.png)
*v0.4.1 — Arrangement View with timeline clips, automation lanes, loop markers, and piano roll editor.*

![Y.A.W.N — FM Synth](images/yawn_fm_synth_01.png)
*FM Synth with 4-operator algorithm routing diagram and grouped parameter knobs.*

![Y.A.W.N — Piano Roll](images/yawn_piano_roll.png)
*Piano Roll editor with draw/select/erase tools, velocity bars, and snap-to-grid.*

## Tech Stack

| Component | Technology |
|---|---|
| Language | C++17 |
| UI / Windowing | SDL3 + OpenGL 3.3 |
| Audio I/O | PortAudio |
| MIDI I/O | RtMidi 6.0 |
| Controller Scripting | Lua 5.4 (vendored) |
| Audio Files | libsndfile |
| Font Rendering | stb_truetype |
| Build System | CMake 3.20+ |
| Testing | Google Test 1.14 |
| Platforms | Windows, Linux |

All dependencies are fetched automatically via CMake FetchContent — no manual installs needed. Lua 5.4 and SQLite3 are vendored as source amalgamations. The AI insisted on this because it can't `apt-get` and refused to write installation instructions longer than 3 lines.

## Building

> **Fun fact:** This project has been rebuilt approximately 1,247 times. 
> The AI broke the build 312 of those times. The PM broke it 0 times because the PM doesn't touch C++.
> The remaining 935 rebuilds were "just to be sure."

### Prerequisites

- **CMake 3.20+**
- **C++17 compiler** — MSVC 2019+ (Windows), GCC 8+ or Clang 8+ (Linux)
- **Python 3 + jinja2** — required by glad2 (OpenGL loader generator)
- **Git** — for FetchContent dependency downloads

```bash
# Install jinja2 if not already present
pip install jinja2
```

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Run

```bash
# Windows
build\bin\Release\YAWN.exe

# Linux
./build/bin/YAWN
```

### Run Tests

```bash
# Windows
build\bin\Release\yawn_tests.exe

# Linux
./build/bin/yawn_tests

# Or via CTest
cd build && ctest --output-on-failure -C Release
```

## Controls

*The PM learned all of these by pressing random keys until something happened. The AI learned all of these by implementing them and then immediately forgetting.*

| Key | Action |
|---|---|
| `Space` | Play / Stop |
| `Up` / `Down` | BPM +/- 1 |
| `Home` | Reset position to 0 |
| `Tab` | Toggle Session / Arrangement view |
| `M` | Toggle mixer view |
| `D` | Toggle detail panel |
| `L` | Toggle loop on/off (arrangement) |
| `F` | Toggle auto-scroll / follow playhead (arrangement) |
| `Delete` / `Backspace` | Delete selected clip (arrangement) |
| `Ctrl+D` | Duplicate selected clip (arrangement) |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` | Redo |
| `Ctrl+S` | Save project |
| `Ctrl+Shift+E` | Export audio |
| `Q` `2` `W` `3` `E` `R` ... `P` | Virtual keyboard (MIDI notes) |
| `Z` / `X` | Octave down / up |
| `Esc` | Close menu / dialog / Quit |
| **Left click clip** | Launch clip (session) / Select clip (arrangement) |
| **Right click clip** | Stop track (session) |
| **Right click scene label** | Scene context menu (insert/duplicate/delete) |
| **Right click track header** | Track context menu (type, instruments, effects, delete) |
| **Right click transport** | MIDI Learn context menu |
| **Right click knob/fader** | MIDI Learn / Reset to default |
| **Click ruler** | Set playhead position (arrangement) |
| **Shift+click ruler** | Set loop start (arrangement) |
| **Shift+right-click ruler** | Set loop end (arrangement) |
| **Double-click empty** | Create MIDI clip (arrangement, MIDI track) |
| **Double-click knob** | Text entry for precise value |
| **Drag clip body** | Move clip, cross-track (arrangement) |
| **Drag clip edges** | Resize clip with snap (arrangement) |
| **Mouse drag on fader** | Adjust volume |
| **Mouse drag on pan** | Adjust panning |
| **Drag & drop audio file** | Load clip into slot under cursor |

## Architecture

*Designed by an AI that has read every audio programming tutorial on the internet but has never actually heard a sound.*

```
┌──────────────────────────────────────────────────────────────┐
│                   UI Layer (SDL3 + OpenGL)                   │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌───────────┐ │
│  │  Session    │ │ Arrangement │ │  Detail   │ │  Piano    │ │
│  │   Panel     │ │   Panel     │ │  Panel    │ │  Roll     │ │
│  └─────────────┘ └─────────────┘ └──────────┘ └───────────┘ │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌───────────┐ │
│  │   Mixer     │ │  Waveform   │ │ Renderer │ │ Font/DPI  │ │
│  │   Panel     │ │  Widget     │ │    2D    │ │  & Theme  │ │
│  └─────────────┘ └─────────────┘ └──────────┘ └───────────┘ │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌───────────┐ │
│  │  FlexBox    │ │  Dialogs &  │ │ Context  │ │  MIDI     │ │
│  │  & Widgets  │ │  Menus      │ │  Menus   │ │  Learn    │ │
│  └─────────────┘ └─────────────┘ └──────────┘ └───────────┘ │
├──────────────────────────────────────────────────────────────┤
│                   Application Core                           │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌────────────────┐ │
│  │ Project  │ │ Transport │ │  Undo    │ │  Message Queue │ │
│  │  Model   │ │  & Loop   │ │ Manager  │ │  (lock-free)   │ │
│  └──────────┘ └───────────┘ └──────────┘ └────────────────┘ │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌────────────────┐ │
│  │ Project  │ │   MIDI    │ │  MIDI    │ │  Crash         │ │
│  │ Serial.  │ │  Mapping  │ │ Monitor  │ │  Handler       │ │
│  └──────────┘ └───────────┘ └──────────┘ └────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│                 Controller Scripting (Lua 5.4)               │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌────────────────┐ │
│  │Controller│ │   Lua     │ │Controller│ │   yawn.* API   │ │
│  │ Manager  │ │  Engine   │ │ MidiPort │ │ (param/MIDI/…) │ │
│  └──────────┘ └───────────┘ └──────────┘ └────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│                   Audio Engine (real-time thread)            │
│  ┌──────────┐ ┌───────────┐ ┌───────────┐ ┌──────────────┐ │
│  │PortAudio │ │   Clip    │ │Arrangement│ │  Metronome   │ │
│  │ Callback │ │  Engine   │ │ Playback  │ │              │ │
│  └──────────┘ └───────────┘ └───────────┘ └──────────────┘ │
│  ┌──────────┐ ┌───────────┐ ┌───────────┐ ┌──────────────┐ │
│  │  Mixer   │ │  Effects  │ │Instruments│ │  Automation  │ │
│  │ /Router  │ │  Chains   │ │ (Synths)  │ │ Engine + LFO │ │
│  └──────────┘ └───────────┘ └───────────┘ └──────────────┘ │
│  ┌──────────┐ ┌───────────┐ ┌───────────┐                  │
│  │  MIDI    │ │   Time    │ │ Transient │                  │
│  │  Engine  │ │ Stretcher │ │ Detector  │                  │
│  └──────────┘ └───────────┘ └───────────┘                  │
└──────────────────────────────────────────────────────────────┘
```

**Thread model:** UI thread (SDL main loop) + Audio thread (PortAudio callback). Communication is entirely via lock-free SPSC ring buffers — no mutexes or allocations on the audio thread. We asked the AI to explain lock-free programming and it wrote a 200-line ring buffer. We asked it again and it wrote a different 200-line ring buffer. Both passed tests. We don't ask questions anymore.

**Audio signal flow:**
```
                    ┌─────────────┐
 Audio Input ──────→│  Recording  │──→ Recorded Audio/MIDI Data
                    └─────────────┘
                          │
 MIDI Input ──────────────────→ MIDI Effect Chain → Instrument → Track Buffer
 Controller (Lua) ─── notes ──→↑         params ──→ Device Parameters
                                                    ↓
 Clip Engine (session) ──────────────────→ Track Buffer (summed)
          or                                        ↓
 Arrangement Playback (timeline) ────────→ Track Buffer (per-track S/A)
                                                    ↓
           Time Stretcher (WSOLA/PhaseVocoder) ────→↓
                                                    ↓
 Track Fader/Pan/Mute/Solo → Sends → Return Buses → Master Output
                                                        ↓
 Automation Engine (envelopes + LFOs) ────────→ Parameter modulation
                                                        ↓
                                               Metronome (added)
```

## Project Structure

```
yawn/
├── CMakeLists.txt              # Main build configuration
├── cmake/
│   └── Dependencies.cmake      # FetchContent (SDL3, glad, PortAudio, libsndfile, RtMidi, stb, gtest)
├── src/
│   ├── main.cpp                # Entry point, crash handler, stdout/stderr redirect
│   ├── app/
│   │   ├── App.h/cpp           # Application lifecycle, event loop, undo, MIDI learn
│   │   ├── ArrangementClip.h   # Arrangement clip data model
│   │   └── Project.h           # Track/Scene/Clip grid model, scene/track management
│   ├── audio/
│   │   ├── AudioBuffer.h       # Non-interleaved multi-channel buffer
│   │   ├── AudioEngine.h/cpp   # PortAudio lifecycle, callback, routing, recording
│   │   ├── ArrangementPlayback.h/cpp # Per-track arrangement clip rendering
│   │   ├── Clip.h              # Clip data model and play state
│   │   ├── ClipEngine.h/cpp    # Multi-track quantized clip playback + follow actions
│   │   ├── FollowAction.h      # Follow action types and dual-action config
│   │   ├── Metronome.h         # Synthesized click track
│   │   ├── Mixer.h             # 64-track mixer with sends/returns/master
│   │   ├── TimeStretcher.h     # WSOLA + Phase Vocoder time stretching
│   │   ├── TransientDetector.h # Onset detection and BPM estimation
│   │   ├── Transport.h         # Play/stop, BPM, position, loop range (atomics)
│   │   └── WarpMarker.h        # Warp points and warp modes
│   ├── automation/
│   │   ├── AutomationTypes.h   # TargetType, MixerParam, AutomationTarget
│   │   ├── AutomationEnvelope.h # Breakpoint envelope (addPoint/movePoint/valueAt)
│   │   ├── AutomationLane.h    # Lane (target + envelope + armed flag)
│   │   └── AutomationEngine.h  # Real-time automation parameter application
│   ├── controllers/
│   │   ├── ControllerManager.h/cpp  # Script discovery, port matching, lifecycle
│   │   ├── ControllerMidiPort.h     # Multi-port MIDI I/O with byte ring buffer
│   │   └── LuaEngine.h/cpp         # Lua state, yawn.* API registration
│   ├── core/
│   │   └── Constants.h         # Global limits (tracks, buses, buffer sizes)
│   ├── effects/
│   │   ├── AudioEffect.h       # Effect base class + parameter system
│   │   ├── EffectChain.h       # Ordered chain of up to 8 effects
│   │   ├── Biquad.h            # Biquad filter primitives
│   │   ├── Reverb.h            # Algorithmic reverb
│   │   ├── Delay.h             # Stereo delay with tempo sync
│   │   ├── EQ.h                # 3-band parametric EQ
│   │   ├── Compressor.h        # Dynamics compressor
│   │   ├── Filter.h            # Multi-mode SVF filter
│   │   ├── Chorus.h            # Modulated delay chorus
│   │   ├── Distortion.h        # Waveshaper distortion
│   │   ├── TapeEmulation.h     # Analog tape simulation
│   │   ├── AmpSimulator.h      # Guitar/bass amp + cabinet modelling
│   │   ├── Tuner.h             # YIN pitch detection tuner
│   │   ├── Oscilloscope.h      # Real-time waveform visualizer
│   │   └── SpectrumAnalyzer.h  # FFT-based spectrum display
│   ├── instruments/
│   │   ├── Instrument.h        # Instrument base class
│   │   ├── Envelope.h          # ADSR envelope generator
│   │   ├── Oscillator.h        # polyBLEP oscillator (5 waveforms)
│   │   ├── SubtractiveSynth.h  # 2-osc analog synth + SVF filter
│   │   ├── FMSynth.h           # 4-operator FM synth (8 algorithms)
│   │   ├── Sampler.h           # Sample playback with pitch tracking
│   │   ├── Multisampler.h      # Multi-zone sample player
│   │   ├── InstrumentRack.h    # Multi-chain container (key/vel zones)
│   │   ├── DrumRack.h          # 128-pad drum machine
│   │   ├── DrumSlop.h          # Loop slicer drum machine (16 pads)
│   │   ├── WavetableSynth.h    # 5 wavetable types with morphing
│   │   ├── GranularSynth.h     # Sample-based granular synthesis
│   │   ├── KarplusStrong.h     # Physical modelling string synth
│   │   └── Vocoder.h           # Band-based vocoder
│   ├── midi/
│   │   ├── MidiTypes.h         # MidiMessage, MidiBuffer, converters
│   │   ├── MidiClip.h          # MIDI clip data model
│   │   ├── MidiClipEngine.h    # MIDI clip playback engine
│   │   ├── MidiPort.h          # Hardware MIDI I/O (RtMidi)
│   │   ├── MidiEngine.h        # MIDI routing and device management
│   │   ├── MidiEffect.h        # MIDI effect base class
│   │   ├── MidiEffectChain.h   # Ordered chain of MIDI effects
│   │   ├── MidiMapping.h       # MIDI Learn manager (CC + Note mapping)
│   │   ├── MidiMonitorBuffer.h # Lock-free MIDI event ring buffer
│   │   ├── Arpeggiator.h       # Beat-synced arpeggiator (6 modes)
│   │   ├── Chord.h             # Parallel interval generator
│   │   ├── Scale.h             # Note quantization (9 scale types)
│   │   ├── NoteLength.h        # Forced note duration
│   │   ├── VelocityEffect.h    # Velocity curve remapping
│   │   ├── MidiRandom.h        # Pitch/velocity/timing randomization
│   │   ├── MidiPitch.h         # Transpose by semitones/octaves
│   │   └── LFO.h               # Modulation LFO (5 waveforms, tempo sync)
│   ├── ui/
│   │   ├── Font.h/cpp          # stb_truetype font atlas
│   │   ├── Renderer.h/cpp      # Batched 2D OpenGL renderer
│   │   ├── MenuBar.h           # Application menu bar
│   │   ├── ContextMenu.h       # Right-click popup menus with submenus
│   │   ├── VirtualKeyboard.h   # QWERTY-to-MIDI keyboard
│   │   ├── Theme.h             # Ableton-dark color scheme + DPI scaling
│   │   ├── Window.h/cpp        # SDL3 + OpenGL window wrapper
│   │   ├── framework/
│   │   │   ├── Widget.h        # Base widget class (measure/layout/paint/events)
│   │   │   ├── FlexBox.h       # Flexbox layout container (row/column)
│   │   │   ├── Primitives.h    # FwButton, FwToggle, FwKnob, FwFader, Label, TextInput, etc.
│   │   │   ├── Dialog.h        # Modal dialog base class
│   │   │   ├── AboutDialog.h   # About dialog widget
│   │   │   ├── ConfirmDialog.h # Confirmation dialog widget
│   │   │   ├── ExportDialog.h  # Audio export dialog (format, depth, scope, progress)
│   │   │   ├── DeviceWidget.h  # Composite device panel (header + grid + knobs + viz)
│   │   │   ├── DeviceHeaderWidget.h  # Color-coded device header with buttons
│   │   │   ├── FwGrid.h        # Row-major grid layout container
│   │   │   ├── VisualizerWidget.h    # Oscilloscope/spectrum display widget
│   │   │   ├── WaveformWidget.h      # Scrollable/zoomable waveform display
│   │   │   ├── InstrumentDisplayWidget.h # FM algo, ADSR, osc, filter display + GroupedKnobBody
│   │   │   └── SnapScrollContainer.h # Horizontal snap-scroll with nav buttons
│   │   └── panels/
│   │       ├── SessionPanel.h/cpp     # Session view (clip grid, scene management)
│   │       ├── ArrangementPanel.h/cpp # Arrangement timeline (clips, automation, loop)
│   │       ├── MixerPanel.h           # Mixer view (faders, metering)
│   │       ├── DetailPanelWidget.h    # Device chain panel (composite widgets)
│   │       ├── TransportPanel.h/cpp   # Transport controls with MIDI Learn
│   │       ├── PianoRollPanel.h       # MIDI piano roll editor
│   │       ├── BrowserPanel.h         # File browser + MIDI monitor display
│   │       └── PreferencesDialog.cpp  # Preferences (Audio, MIDI, Defaults, Metronome)
│   ├── util/
│   │   ├── FileIO.h/cpp        # Audio file loading/saving (libsndfile)
│   │   ├── MessageQueue.h      # Typed command/event variants
│   │   ├── ProjectSerializer.h/cpp # JSON project save/load
│   │   ├── OfflineRenderer.h   # Offline audio export engine
│   │   ├── UndoManager.h       # Undo/redo with action merging
│   │   └── RingBuffer.h        # Lock-free SPSC ring buffer
│   └── WidgetHint.h            # Widget type hints
├── scripts/
│   └── controllers/
│       └── ableton_push1/      # Ableton Push 1 controller script
│           ├── manifest.lua    # Port matching metadata
│           └── init.lua        # Encoder/display/pad logic
├── tests/                      # 844 unit & integration tests (Google Test)
│   ├── CMakeLists.txt
│   ├── test_Arrangement.cpp    # Arrangement clips, playback, transport loop
│   ├── test_AudioBuffer.cpp    # Audio buffer operations
│   ├── test_Automation.cpp     # Automation engine, envelopes, LFO
│   ├── test_Clip.cpp           # Clip data model
│   ├── test_ClipEngine.cpp     # Clip playback engine
│   ├── test_DeviceHeaderWidget.cpp # Device header UI
│   ├── test_DeviceWidget.cpp   # Composite device widget
│   ├── test_Effects.cpp        # All audio effects
│   ├── test_FileIO.cpp         # File I/O, sample loading
│   ├── test_FlexBox.cpp        # Flexbox layout
│   ├── test_FollowAction.cpp   # Follow action logic
│   ├── test_FrameworkComponents.cpp # UI framework
│   ├── test_FrameworkTypes.cpp # Framework types
│   ├── test_FwGrid.cpp         # Grid layout
│   ├── test_Instruments.cpp    # All instruments
│   ├── test_Integration.cpp    # Cross-component integration
│   ├── test_LFO.cpp            # LFO waveforms, sync, linking
│   ├── test_MessageQueue.cpp   # Inter-thread communication
│   ├── test_Metronome.cpp      # Click track
│   ├── test_MidiClip.cpp       # MIDI clip data
│   ├── test_MidiClipEngine.cpp # MIDI playback engine
│   ├── test_MidiEffects.cpp    # MIDI effects
│   ├── test_MidiMapping.cpp    # MIDI Learn (CC + Note mapping)
│   ├── test_MidiTypes.cpp      # MIDI types
│   ├── test_Mixer.cpp          # Mixer routing
│   ├── test_PanelAnimation.cpp # Panel animations
│   ├── test_PianoRoll.cpp      # Piano roll editor
│   ├── test_Primitives.cpp     # Widget primitives
│   ├── test_Project.cpp        # Project structure
│   ├── test_RingBuffer.cpp     # Lock-free buffers
│   ├── test_Serialization.cpp  # Project save/load
│   ├── test_SnapScrollContainer.cpp # Scroll container
│   ├── test_Theme.cpp          # DPI scaling
│   ├── test_TrackControls.cpp  # Track UI controls
│   ├── test_Transport.cpp      # Transport logic
│   ├── test_UndoManager.cpp    # Undo/redo system
│   ├── test_VisualizerWidget.cpp # Waveform visualization
│   ├── test_Warping.cpp        # Time stretching (WSOLA, Phase Vocoder)
│   ├── test_Widget.cpp         # Widget tree & event dispatch
│   └── test_Widgets.cpp        # Widget tests
├── third_party/
│   ├── lua54/                  # Lua 5.4 vendored source
│   └── sqlite3/                # SQLite3 vendored source
└── assets/                     # Runtime assets (copied to build dir)
```

## Implementation Phases

*Each phase was implemented by saying "do this" and then saying "no, not like that" between 2 and 47 times.*

| Phase | Status | Description |
|---|---|---|
| 1. Project Scaffolding | ✅ Done | CMake build system, SDL3+OpenGL window, directory structure |
| 2. Audio Engine | ✅ Done | PortAudio callback, transport, lock-free ring buffers |
| 3. Clip Playback | ✅ Done | libsndfile loading, quantized clip launching, looping |
| 4. Session View UI | ✅ Done | Clip grid, transport bar, waveform thumbnails, theme |
| 5. Mixer & Routing | ✅ Done | 64-track mixer, 8 send/return buses, master, metering |
| 6. MIDI Engine | ✅ Done | MIDI 2.0-res internals, RtMidi I/O, MPE zones, MIDI clips |
| 7. Metronome | ✅ Done | Synthesized click track, beat-synced, configurable |
| 8. Audio Effects | ✅ Done | 12 built-in effects (+ 2 visualizers), effect chains, drag-to-reorder, 3-point insert |
| 9. Integrated Instruments | ✅ Done | 11 instruments with full UI (SubSynth, FM, Sampler, Karplus-Strong, Wavetable, Granular, Vocoder, Multisampler, InstrumentRack, DrumRack, DrumSlop) |
| 10. MIDI Effects | ✅ Done | 8 MIDI effects (Arp, Chord, Scale, NoteLength, Velocity, Random, Pitch, LFO) |
| 11. Interactive UI | ✅ Done | Widget system, menu bar, mixer controls, detail panel, virtual keyboard, context menus |
| 12. UI Framework | ✅ Done | Widget tree, FlexBox layout, primitive widgets, dialog system, panel migration |
| 13. Piano Roll | ✅ Done | MIDI note editor with draw/select/erase tools, zoom/scroll, clip integration |
| 14. Composite Widgets | ✅ Done | DeviceWidget, DeviceHeader, FwGrid, VisualizerWidget, SnapScrollContainer, neon knobs |
| 15. Animations & DPI | ✅ Done | Hover animations, panel collapse/expand animations, DPI auto-detection & scaling |
| 16. Arrangement View | ✅ Done | Timeline, clip placement, automation lanes, loop range, waveform display |
| 17. Recording & I/O | ✅ Done | Audio/MIDI recording, MIDI Learn, audio export (WAV/FLAC/OGG), project save/load |
| 18. Session Management | ✅ Done | Scene insert/duplicate/delete, track deletion, follow actions, undo/redo, time stretching |
| 19. VST3 Hosting | ✅ Done | VST3 SDK, plugin scanning, process-isolated editors, parameter sync, state persistence |
| 20. Controller Scripting | ✅ Done | Lua 5.4, controller auto-detection, yawn.* API, Ableton Push 1 (encoders, display, pads, LEDs) |

### Phase 16: Arrangement View (Done)

The Arrangement View provides a linear timeline for composing full tracks:

- **Timeline grid** — Beat/bar grid with zoom (4–120 px/beat) and scroll, snap-to-grid with 6 resolution levels
- **Clip placement** — Select, move (same/cross-track), resize edges, double-click create, Ctrl+D duplicate, Delete remove
- **Arrangement playback** — Per-track audio + MIDI clip rendering with fade crossfades, thread-safe clip submission
- **Session/Arrangement toggle** — Per-track S/A button, independent mode switching, auto-activation on view switch
- **Automation lanes** — Expandable per-track lanes with breakpoint envelopes, visual curve rendering, click/drag/delete breakpoints
- **Loop range** — Green markers in ruler with drag handles, Shift+click to set, L key to toggle, wraps playback position
- **Auto-scroll** — Playhead follow mode (F key), keeps playhead visible during playback
- **Playhead** — Click ruler to seek, triangle indicator + vertical line, renders in real-time
- **Waveform display** — Audio clip waveform rendering in arrangement blocks

### Phase 17: Recording, MIDI Learn & Audio Export (Done)

Full recording and I/O capabilities:

- **Audio recording** — Per-track input recording with arm/disarm, overdub, stereo capture, monitor modes
- **MIDI recording** — Real-time note/CC capture from hardware keyboards with proper finalization
- **MIDI Learn** — Map any CC or Note to any parameter via right-click context menu, visual feedback during learn, JSON persistence
- **Audio export** — Offline render to WAV/FLAC/OGG with configurable bit depth and sample rate, export dialog with progress
- **Project serialization** — Full save/load to `.yawn` JSON format with sample management

### Phase 18: Session Management & Track Operations (Done)

Scene and track management for a complete workflow:

- **Scene management** — Insert, duplicate, delete scenes via right-click context menu with full undo support
- **Track deletion** — Delete tracks with confirmation dialog, engine array shifting across all sub-engines
- **Follow actions** — 8 action types with dual-action probability for clip chaining
- **Undo/redo** — Full undo/redo system with action merging (Ctrl+Z / Ctrl+Y)
- **Time stretching** — WSOLA and Phase Vocoder algorithms for tempo-independent playback
- **Crash handling** — Signal handlers with stack traces for debugging

### Phase 19: VST3 Plugin Hosting

Full VST3 plugin support for third-party effects and instruments:

- **VST3 SDK integration** — Compile and link the official Steinberg VST3 SDK
- **Plugin scanning** — Discover VST3 plugins in standard system paths
- **Audio effects** — Load VST3 effects into track/return/master effect chains
- **Instruments** — Load VST3 instruments as MIDI track sound generators
- **Plugin editor windows** — Embed native plugin GUIs in secondary SDL3 windows
- **Parameter mapping** — Generic knob grid for plugins without custom GUIs
- **Preset management** — Save/load plugin state with project

### Phase 20: Controller Scripting (Done)

Lua-based MIDI controller integration with auto-detection and hot reload:

- **Lua 5.4 engine** — Vendored amalgamation, embedded with yawn.* API for device parameters, MIDI I/O, SysEx, and transport
- **Controller Manager** — Scans `scripts/controllers/*/manifest.lua`, substring-matches MIDI port names, opens all matching I/O ports
- **Multi-port architecture** — Controllers with multiple MIDI ports (Push 1 Live + User) feed a single byte-oriented SPSC ring buffer
- **Lua callbacks** — `on_connect()`, `on_disconnect()`, `on_midi(data)` (per-message), `on_tick()` (30Hz)
- **Ableton Push 1 script** — 8 relative encoders mapped to device params, 4-line SysEx display (param names/values/track/instrument), 64-pad note forwarding with LED ripple animation
- **Port exclusivity** — Claimed ports skipped by MidiEngine to avoid Windows exclusive-access conflicts
- **Hot reload** — View → Reload Controller Scripts disconnects, rescans, and reconnects without restart

## The Team

| Role | Entity | Responsibilities |
|---|---|---|
| **Project Manager** | Tasos Kleisas | Vision, QA, yelling "it still doesn't work", changing requirements mid-sentence, clicking things really fast to find bugs, discovering that resonance + fast cutoff sweep = pain |
| **Chief Engineer** | Claude (Anthropic) | Writing code, rewriting code, explaining why the code was wrong, rewriting it again, apologizing, "I see the issue!", writing commit messages longer than the actual fix |

### Development Methodology

```
while (true) {
    PM: "Add feature X"
    AI: *writes 200 lines*
    PM: "It doesn't work"
    AI: "Ah, I see the issue!" *rewrites 200 lines*
    PM: "Now Y is broken"
    AI: "Ah, I see the issue!" *rewrites 150 lines*
    PM: "OK it works. But..."
    AI: *sweating in tokens*
    PM: "...can we also—"
    AI: "Of course!"  // narrator: it could not
}
```

### Lessons Learned

1. **"It compiles" ≠ "It works"** — But it's a great start when your engineer has no ears
2. **Filter resonance is the QA department** — Crank it up, sweep fast, watch things explode
3. **The AI will always say "Fixed!"** — Statistically, it's right 60% of the time, every time
4. **Lock-free programming is easy** — If you let someone who can't experience race conditions write it
5. **844 tests and counting** — Because when your codebase is written by autocomplete on steroids, trust but verify
6. **The best bug reports are just vibes** — "After a while the arpeggiator produces notes without me pressing any key" → *chef's kiss*
7. **Track deletion requires stopping the world** — Ableton does it too, so it's a feature not a limitation
8. **MIDI Learn is just "wiggle something, click something"** — The AI understood this perfectly on the 4th attempt
9. **SysEx is where bytes go to hide** — The Push 1 display didn't work for hours because of one missing column offset byte. The PM dug up his own 10-year-old code to prove the AI wrong
10. **Controllers have multiple MIDI ports** — Push 1 sends pads on the "User" port, not the main one. The AI opened the wrong port and wondered why pads were silent

*This is what software development looks like in 2026. One human with opinions and one AI with infinite patience. The future is sloppy, it ships, and honestly? It kinda slaps.*

## License

[MIT](LICENSE.txt) © Tasos Kleisas
