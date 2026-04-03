<p align="center">
  <img src="yawn_ico512x512.png" alt="Y.A.W.N Logo" width="128" />
</p>

<h1 align="center">Y.A.W.N</h1>
<h3 align="center">Yetanother Audio Workstation New</h3>

<p align="center">
  A cross-platform digital audio workstation inspired by Ableton Live.<br/>
  Session View · Mixer · Instruments · Effects · MIDI · Clip Launching<br/><br/>
  <em>Made with AI-Sloptronic™ technology</em><br/>
  <sub>Where "it compiles" is the new "it works" and every bug is a ✨feature request✨</sub>
</p>

---

> **⚠️ Disclaimer:** No human engineers were mass-employed in the making of this software.
> The entire codebase was produced through the ancient art of describing what you want to a machine
> and then spending twice as long explaining why that's not what you meant.
> Side effects may include: spontaneous filter resonance, existential questions about who actually wrote this,
> and an unshakeable feeling that the AI is just gaslighting you into thinking the bug is fixed.

## Features

### Audio Engine
- **Real-time Audio Engine** — Lock-free audio thread with PortAudio (ASIO/WASAPI/ALSA)
- **Clip Playback** — Audio files (WAV, FLAC, OGG, AIFF, MP3), looping, gain, fade-in/out
- **Quantized Launching** — Launch clips on beat or bar boundaries
- **Transport** — Play/stop, BPM control, beat-synced position tracking
- **Metronome** — Synthesized click track with accent on downbeats, configurable volume & time signature, count-in, recording/playback mode selection

### Mixer & Routing
- **64-track Mixer** — Per-track volume, pan, mute, solo
- **8 Send/Return Buses** — Pre/post-fader send routing with independent return channels
- **Master Bus** — Master volume with metering
- **3-point Effect Insert** — Effect chains on tracks, return buses, and master

### Native Audio Effects
- **Reverb** — Schroeder/Moorer algorithmic reverb (4 comb + 2 allpass filters)
- **Delay** — Stereo delay with tempo sync, feedback, and ping-pong mode
- **EQ** — 3-band parametric EQ (low shelf, mid peak, high shelf)
- **Compressor** — Dynamics compressor with threshold, ratio, attack, release, makeup gain
- **Filter** — Multi-mode SVF filter (lowpass, highpass, bandpass, notch) with 2x oversampled stability
- **Chorus** — Modulated delay with multiple voices
- **Distortion** — Waveshaper with soft clip, hard clip, and tube saturation modes
- **Tape Emulation** — Analog tape simulation with asymmetric saturation, wow/flutter, tape hiss, and tone rolloff
- **Amp Simulator** — Guitar/bass amp modelling with 4 amp types (Clean/Crunch/Lead/High Gain), 3-band tone stack, cabinet simulation
- **Oscilloscope** — Real-time waveform visualizer (non-destructive analysis effect)
- **Spectrum Analyzer** — FFT-based frequency spectrum display (non-destructive analysis effect)

### Native Instruments
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
- **MIDI I/O** — Hardware MIDI via RtMidi (WinMM/ALSA)
- **MPE Support** — Per-note pitch bend, slide, pressure via zone management
- **7 MIDI Effects** — Arpeggiator (free-running & transport-synced), Chord, Scale, Note Length, Velocity, Random, Pitch

### UI Framework
- **Composable Widget Tree** — FlexBox layout engine with measure/layout two-pass system, stretch/flex/fixed size policies
- **Session Panel** — Ableton-style clip grid with 8 visible tracks × 8 scenes, scrollable
- **Mixer Panel** — Channel strips with interactive faders, pan knobs, mute/solo buttons, peak metering
- **Device Chain Panel** — Composite widget architecture: DeviceWidget (header + grid + knobs + visualizer), SnapScrollContainer, neon arc knobs with 24-segment rendering
- **Grouped Instrument Layouts** — Instruments display knobs in logical sections (Global, Op 1–4, Filter, Amp, etc.) with inline graphical displays instead of flat grids
- **Instrument Display Widgets** — FM algorithm routing diagram, ADSR envelope curves, oscillator waveform previews, filter response curves, composite synth panels
- **MIDI Recording** — Record from hardware MIDI keyboards to MIDI clips with proper finalization on transport stop
- **Piano Roll Editor** — MIDI note editing with draw/select/erase tools, zoom/scroll, velocity, snap-to-grid, follow-playhead mode, clip operations (duplicate, double, halve, reverse, clear, set 1.1.1 here)
- **Primitive Widgets** — FwButton, FwToggle, FwKnob (with double-click text entry, step snapping, format callbacks), FwFader, Label, FwTextInput, FwNumberInput, FwDropDown with hover animations
- **Dialog System** — fw::Dialog base class with title bar, OK/Cancel, drag-to-move, Escape/Enter handling; AboutDialog, ConfirmDialog, PreferencesDialog (Audio, Defaults, Metronome tabs)
- **Menu Bar** — File, Edit, View, Track, MIDI, Help menus with keyboard accelerators
- **Context Menus** — Right-click track headers to set type, add instruments/effects
- **DPI Scaling** — Auto-detect display scale (SDL3), user override, scaled() helper for all layout constants
- **Panel Animations** — Smooth exponential-lerp height transitions on panel collapse/expand
- **Virtual Keyboard** — QWERTY-to-MIDI mapping (Q2W3ER5T6Y7UI9O0P), Z/X octave switching, per-key note tracking
- **Track Selection** — Click to select tracks, highlight in session & mixer views
- **Track Type Icons** — Waveform icon for audio tracks, DIN circle icon for MIDI tracks
- **Targeted Drag & Drop** — Drop audio files onto specific clip slots
- **Custom 2D Renderer** — Batched OpenGL 3.3 rendering with font atlas (stb_truetype)
- **Multi-window Ready** — Built on SDL3 for future detachable panels

### Quality
- **Test-Driven Development** — 678 unit & integration tests via Google Test (because the AI doesn't trust itself either)
- **Zero audio-thread allocations** — All memory preallocated at startup
- **All instruments handle CC 123** (All Notes Off) for clean MIDI effect removal
- **Sloptronic-grade stability** — Filters clamped, state variables leashed, resonance domesticated

### Planned

- 🎹 Arrangement View (timeline recording)
- 🔌 VST3 plugin hosting
- 💾 Project save/load (JSON format)
- 🐛 Whatever bugs the PM discovers by wiggling knobs at 3 AM

## Screenshots

![Y.A.W.N v0.1 — Session View with device chain panel](images/yawn_v.0.1.png)
*v0.1 — Session View showing the clip grid, mixer, and device chain panel with Arpeggiator → Subtractive Synth → Filter → Oscilloscope → EQ → Spectrum Analyzer.*

## Tech Stack

| Component | Technology |
|---|---|
| Language | C++17 |
| UI / Windowing | SDL3 + OpenGL 3.3 |
| Audio I/O | PortAudio |
| MIDI I/O | RtMidi 6.0 |
| Audio Files | libsndfile |
| Font Rendering | stb_truetype |
| Build System | CMake 3.20+ |
| Testing | Google Test 1.14 |
| Platforms | Windows, Linux |

All dependencies are fetched automatically via CMake FetchContent — no manual installs needed. The AI insisted on this because it can't `apt-get` and refused to write installation instructions longer than 3 lines.

## Building

> **Fun fact:** This project has been rebuilt approximately 847 times. 
> The AI broke the build 212 of those times. The PM broke it 0 times because the PM doesn't touch C++.
> The remaining 635 rebuilds were "just to be sure."

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
| `M` | Toggle mixer view |
| `D` | Toggle detail panel |
| `Q` `2` `W` `3` `E` `R` ... `P` | Virtual keyboard (MIDI notes) |
| `Z` / `X` | Octave down / up |
| `Esc` | Close menu / Quit |
| **Left click clip** | Launch clip |
| **Right click clip** | Stop track |
| **Click track header** | Select track |
| **Right-click header** | Context menu (type, instruments, effects) |
| **Mouse drag on fader** | Adjust volume |
| **Mouse drag on pan** | Adjust panning |
| **Right-click fader/pan** | Reset to default |
| **Drag & drop audio file** | Load clip into slot under cursor |

## Architecture

*Designed by an AI that has read every audio programming tutorial on the internet but has never actually heard a sound.*

```
┌──────────────────────────────────────────────────────────────┐
│                   UI Layer (SDL3 + OpenGL)                   │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌───────────┐  │
│  │  Session    │ │   Mixer     │ │  Detail   │ │  Piano    │  │
│  │   Panel     │ │   Panel     │ │  Panel    │ │  Roll     │  │
│  └─────────────┘ └─────────────┘ └──────────┘ └───────────┘  │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌───────────┐  │
│  │  FlexBox    │ │  Primitives │ │ Renderer │ │ Font/Theme│  │
│  │  Layout     │ │  & Dialogs  │ │    2D    │ │  & DPI    │  │
│  └─────────────┘ └─────────────┘ └──────────┘ └───────────┘  │
├──────────────────────────────────────────────────────────────┤
│                   Application Core                           │
│  ┌──────────┐ ┌───────────┐ ┌──────────────────────────────┐ │
│  │ Project  │ │ Transport │ │  Message Queue (lock-free)   │ │
│  │  Model   │ │           │ │  UI ↔ Audio thread           │ │
│  └──────────┘ └───────────┘ └──────────────────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│                   Audio Engine (real-time thread)            │
│  ┌──────────┐ ┌───────────┐ ┌───────────┐ ┌──────────────┐   │
│  │PortAudio │ │   Clip    │ │   MIDI    │ │  Metronome   │   │
│  │ Callback │ │  Engine   │ │  Engine   │ │              │   │
│  └──────────┘ └───────────┘ └───────────┘ └──────────────┘   │
│  ┌──────────┐ ┌───────────┐ ┌───────────┐ ┌──────────────┐   │
│  │  Mixer   │ │  Effects  │ │Instruments│ │ MIDI Effects │   │
│  │ /Router  │ │  Chains   │ │ (Synths)  │ │ (Arp, etc.)  │   │
│  └──────────┘ └───────────┘ └───────────┘ └──────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

**Thread model:** UI thread (SDL main loop) + Audio thread (PortAudio callback). Communication is entirely via lock-free SPSC ring buffers — no mutexes or allocations on the audio thread. We asked the AI to explain lock-free programming and it wrote a 200-line ring buffer. We asked it again and it wrote a different 200-line ring buffer. Both passed tests. We don't ask questions anymore.

**Audio signal flow:**
```
MIDI Input → MIDI Effect Chain → Instrument → Track Buffer
                                                   ↓
Clip Engine ─────────────────────────────→ Track Buffer (summed)
                                                   ↓
Track Fader/Pan/Mute/Solo → Sends → Return Buses → Master Output
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
│   ├── main.cpp                # Entry point
│   ├── app/
│   │   ├── App.h/cpp           # Application lifecycle, event loop
│   │   └── Project.h           # Track/Scene/Clip grid model
│   ├── audio/
│   │   ├── AudioBuffer.h       # Non-interleaved multi-channel buffer
│   │   ├── AudioEngine.h/cpp   # PortAudio lifecycle, callback, routing
│   │   ├── Clip.h              # Clip data model and play state
│   │   ├── ClipEngine.h/cpp    # Multi-track quantized clip playback
│   │   ├── Metronome.h         # Synthesized click track
│   │   ├── Mixer.h             # 64-track mixer with sends/returns/master
│   │   └── Transport.h         # Play/stop, BPM, position (atomics)
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
│   │   ├── Oscilloscope.h      # Real-time waveform visualizer
│   │   └── SpectrumAnalyzer.h  # FFT-based spectrum display
│   ├── instruments/
│   │   ├── Instrument.h        # Instrument base class
│   │   ├── Envelope.h          # ADSR envelope generator
│   │   ├── Oscillator.h        # polyBLEP oscillator (5 waveforms)
│   │   ├── SubtractiveSynth.h  # 2-osc analog synth + SVF filter
│   │   ├── FMSynth.h           # 4-operator FM synth (8 algorithms)
│   │   ├── Sampler.h           # Sample playback with pitch tracking
│   │   ├── InstrumentRack.h    # Multi-chain container (key/vel zones)
│   │   ├── DrumRack.h          # 128-pad drum machine
│   │   └── DrumSlop.h          # Loop slicer drum machine (16 pads)
│   ├── midi/
│   │   ├── MidiTypes.h         # MidiMessage, MidiBuffer, converters
│   │   ├── MidiClip.h          # MIDI clip data model
│   │   ├── MidiPort.h          # Hardware MIDI I/O (RtMidi)
│   │   ├── MidiEngine.h        # MIDI routing and device management
│   │   ├── MidiEffect.h        # MIDI effect base class
│   │   ├── MidiEffectChain.h   # Ordered chain of MIDI effects
│   │   ├── Arpeggiator.h       # Beat-synced arpeggiator (6 modes)
│   │   ├── Chord.h             # Parallel interval generator
│   │   ├── Scale.h             # Note quantization (9 scale types)
│   │   ├── NoteLength.h        # Forced note duration
│   │   ├── VelocityEffect.h    # Velocity curve remapping
│   │   ├── MidiRandom.h        # Pitch/velocity/timing randomization
│   │   └── MidiPitch.h         # Transpose by semitones/octaves
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
│   │   │   ├── DeviceWidget.h  # Composite device panel (header + grid + knobs + viz)
│   │   │   ├── DeviceHeaderWidget.h  # Color-coded device header with buttons
│   │   │   ├── FwGrid.h        # Row-major grid layout container
│   │   │   ├── VisualizerWidget.h    # Oscilloscope/spectrum display widget
│   │   │   ├── InstrumentDisplayWidget.h # FM algo, ADSR, osc, filter display + GroupedKnobBody
│   │   │   └── SnapScrollContainer.h # Horizontal snap-scroll with nav buttons
│   │   └── panels/
│   │       ├── SessionPanel.h      # Session view (clip grid, transport)
│   │       ├── MixerPanel.h        # Mixer view (faders, metering)
│   │       ├── DetailPanelWidget.h  # Device chain panel (composite widgets)
│   │       ├── PianoRollPanel.h     # MIDI piano roll editor
│   │       └── PreferencesDialog.cpp # Preferences (Audio, Defaults, Metronome)
│   └── util/
│       ├── FileIO.h/cpp        # Audio file loading (libsndfile)
│       ├── MessageQueue.h      # Typed command/event variants
│       └── RingBuffer.h        # Lock-free SPSC ring buffer
├── tests/                      # 678 unit & integration tests (Google Test)
│   ├── CMakeLists.txt
│   ├── test_AudioBuffer.cpp
│   ├── test_Clip.cpp
│   ├── test_ClipEngine.cpp
│   ├── test_DetailPanel.cpp    # Device chain panel tests
│   ├── test_DeviceHeaderWidget.cpp
│   ├── test_DeviceWidget.cpp   # Composite device widget tests
│   ├── test_DialogFramework.cpp
│   ├── test_Effects.cpp        # Audio effect tests
│   ├── test_FileIO.cpp
│   ├── test_FlexBox.cpp        # Flexbox layout tests
│   ├── test_FrameworkComponents.cpp
│   ├── test_FrameworkTypes.cpp
│   ├── test_FwGrid.cpp
│   ├── test_Instruments.cpp    # Instrument tests
│   ├── test_Integration.cpp    # 43 cross-component integration tests
│   ├── test_MessageQueue.cpp
│   ├── test_Metronome.cpp
│   ├── test_MidiClip.cpp
│   ├── test_MidiClipEngine.cpp
│   ├── test_MidiEffects.cpp    # MIDI effect tests
│   ├── test_MidiTypes.cpp
│   ├── test_Mixer.cpp
│   ├── test_PanelAnimation.cpp
│   ├── test_PianoRoll.cpp
│   ├── test_Primitives.cpp     # Widget primitive tests
│   ├── test_Project.cpp
│   ├── test_RingBuffer.cpp
│   ├── test_Serialization.cpp
│   ├── test_SnapScrollContainer.cpp
│   ├── test_Theme.cpp          # DPI scaling tests
│   ├── test_Transport.cpp
│   ├── test_VisualizerWidget.cpp
│   ├── test_Widget.cpp         # Widget tree & event dispatch tests
│   └── test_Widgets.cpp
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
| 8. Audio Effects | ✅ Done | 11 built-in effects (+ 2 visualizers), effect chains, drag-to-reorder, 3-point insert |
| 9. Native Instruments | ✅ Done | 11 instruments with full UI (SubSynth, FM, Sampler, Karplus-Strong, Wavetable, Granular, Vocoder, Multisampler, InstrumentRack, DrumRack, DrumSlop) |
| 10. MIDI Effects | ✅ Done | 7 MIDI effects (Arp, Chord, Scale, NoteLength, Velocity, Random, Pitch) |
| 11. Interactive UI | ✅ Done | Widget system, menu bar, mixer controls, detail panel, virtual keyboard, context menus |
| 12. UI Framework | ✅ Done | Widget tree, FlexBox layout, primitive widgets, dialog system, panel migration |
| 13. Piano Roll | ✅ Done | MIDI note editor with draw/select/erase tools, zoom/scroll, clip integration |
| 14. Composite Widgets | ✅ Done | DeviceWidget, DeviceHeader, FwGrid, VisualizerWidget, SnapScrollContainer, neon knobs |
| 15. Animations & DPI | ✅ Done | Hover animations, panel collapse/expand animations, DPI auto-detection & scaling |
| 16. Arrangement View | 🔲 Next | Timeline, clip placement, recording |
| 17. VST3 Hosting | 🔲 Planned | VST3 SDK, plugin scanning, editor windows |
| 18. Save/Load & Polish | 🔲 Planned | JSON project files, undo/redo, keyboard shortcuts |

### Phase 16: Arrangement View (Next)

The Arrangement View provides a linear timeline for composing full tracks:

- **Timeline grid** — Beat/bar grid with zoom and scroll (horizontal time, vertical tracks)
- **Clip placement** — Drag audio and MIDI clips onto timeline tracks
- **Playhead** — Rendering and scrubbing with transport integration
- **MIDI Piano Roll** — Note editing within MIDI clips (velocity, duration, pitch)
- **Recording** — Record from Session View clips to Arrangement
- **Automation lanes** — Per-track parameter automation (volume, pan, effect params)

### Phase 17: VST3 Plugin Hosting

Full VST3 plugin support for third-party effects and instruments:

- **VST3 SDK integration** — Compile and link the official Steinberg VST3 SDK
- **Plugin scanning** — Discover VST3 plugins in standard system paths
- **Audio effects** — Load VST3 effects into track/return/master effect chains
- **Instruments** — Load VST3 instruments as MIDI track sound generators
- **Plugin editor windows** — Embed native plugin GUIs in secondary SDL3 windows
- **Parameter mapping** — Generic knob grid for plugins without custom GUIs
- **Preset management** — Save/load plugin state with project

### Phase 18: Project Save/Load & Polish

Final polish to make Y.A.W.N a usable production tool:

- **JSON project format** — `.yawn` files referencing audio assets and plugin state
- **Audio asset management** — Copy-to-project or external reference modes
- **Undo/redo** — Command pattern for all user actions
- **Keyboard shortcuts** — Comprehensive shortcuts for all operations
- **MIDI file import/export** — Standard MIDI file support
- **Audio export** — Offline render to WAV/FLAC
- **Drag & drop** — Audio files, MIDI files, plugins between tracks
- **Preferences** — Audio device selection, buffer size, sample rate, MIDI device config

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
5. **678 tests and counting** — Because when your codebase is written by autocomplete on steroids, trust but verify
6. **The best bug reports are just vibes** — "After a while the arpeggiator produces notes without me pressing any key" → *chef's kiss*

*This is what software development looks like in 2026. One human with opinions and one AI with infinite patience. The future is sloppy, it ships, and honestly? It kinda slaps.*

## License

[MIT](LICENSE.txt) © Tasos Kleisas
