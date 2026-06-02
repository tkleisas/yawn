# Project Structure

```
yawn/
├── CMakeLists.txt              # Main build configuration
├── cmake/
│   └── Dependencies.cmake      # FetchContent (SDL3, glad, PortAudio, libsndfile, RtMidi, stb,
│                               #  gtest, NeuralAmpModelerCore + Eigen, ONNX Runtime, KissFFT,
│                               #  Ableton Link, tinygltf)
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
│   │   ├── AudioEffect.h       # Effect base class + sidechain plumbing +
│   │   │                       #  modulation-source hooks + saveExtraState/loadExtraState
│   │   ├── EffectChain.h       # Ordered chain of up to 8 effects
│   │   ├── Biquad.h            # Biquad filter primitives
│   │   ├── Reverb.h            # Algorithmic reverb
│   │   ├── Delay.h             # Stereo delay with tempo sync
│   │   ├── PingPongDelay.h     # Dedicated ping-pong with independent L/R
│   │   │                       #  times + cross-feedback + width
│   │   ├── EQ.h                # 3-band parametric EQ
│   │   ├── SplineEQ.h          # 8-node parametric EQ — drag-edit display
│   │   │                       #  panel with dual pre/post FFT overlay
│   │   ├── Compressor.h        # Dynamics compressor
│   │   ├── Limiter.h           # Look-ahead brickwall limiter
│   │   ├── Filter.h            # Multi-mode SVF filter
│   │   ├── Chorus.h            # Modulated delay chorus
│   │   ├── Phaser.h            # All-pass cascade with LFO + feedback
│   │   ├── Wah.h               # Standalone resonant bandpass
│   │   ├── Rotary.h            # Leslie-style rotary speaker simulation
│   │   ├── Distortion.h        # Waveshaper distortion
│   │   ├── Bitcrusher.h        # Bit-depth quantize + sample-rate decimation
│   │   │                       #  + AA pre-filter + TPDF dither
│   │   ├── NoiseGate.h         # Expander / gate with hysteresis,
│   │   │                       #  attack/hold/release SM, sidechain detect
│   │   ├── EnvelopeFollower.h  # Audio → control signal + optional auto-wah
│   │   │                       #  + routable modulation source
│   │   ├── Convolution.h       # Uniformly-partitioned FFT block convolver
│   │   ├── ConvolutionReverb.h # IR-based reverb — atomic-engine-swap loader,
│   │   │                       #  pre-delay, low/high cut on the wet, IR resample
│   │   ├── NeuralAmp.h         # Neural Amp Modeler shell (PIMPL — C++17 clean)
│   │   ├── NeuralAmp.cpp       # NAM integration (only C++20 TU; isolated by PIMPL,
│   │   │                       #  RCU-lite atomic DSP swap on model load)
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
│   │   ├── DrumSynth.h/.cpp    # Fully-synthesised 8-piece kit (sample-free,
│   │   │                       #  GM-mapped, per-drum tune/decay/vol/pan, choke)
│   │   ├── WavetableSynth.h    # 5 wavetable types with morphing
│   │   ├── GranularSynth.h     # Sample-based granular synthesis
│   │   ├── KarplusStrong.h     # Physical modelling string synth
│   │   ├── Vocoder.h           # Band-based vocoder
│   │   ├── StringMachine.h/cpp # Solina-style ensemble strings (9-saw stack
│   │   │                       #  + paraphonic LP + 3-tap BBD chorus)
│   │   ├── DrawbarOrgan.h/cpp  # Hammond B-3: 9 drawbars + key click +
│   │   │                       #  percussion (auto-pairs with Rotary fx)
│   │   └── ElectricPiano.h/cpp # 2-op FM e-piano (Rhodes/Wurli/Suitcase)
│   │                           #  with hammer noise + pan/amp tremolo;
│   │                           #  Suitcase mode auto-pairs with Phaser
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
│   ├── transcribe/             # ONNX-Runtime audio analysis (opt-in flags)
│   │   ├── AudioToMidi.h/cpp   # Basic Pitch polyphonic transcription wrapper
│   │   └── StemSeparation.h/cpp # Demucs v4 stem separation; bundled-or-download
│   │                           #  model resolution, sinc resample, cancellable
│   ├── link/
│   │   └── LinkManager.h/cpp   # Ableton Link wrapper — gated on YAWN_HAS_LINK
│   ├── ui/
│   │   ├── Font.h/cpp          # stb_truetype font atlas (used by FontAdapter)
│   │   ├── Renderer.h/cpp      # Batched 2D OpenGL renderer
│   │   ├── VirtualKeyboard.h   # QWERTY-to-MIDI keyboard
│   │   ├── Theme.h             # Ableton-dark color scheme + DPI scaling
│   │   ├── ToastManager.h      # Top-center status banner (thread-safe)
│   │   ├── Window.h/cpp        # SDL3 + OpenGL window wrapper
│   │   ├── framework/v2/       # The framework. Used to be split v1/v2; v1 deleted.
│   │   │   ├── Widget.h/cpp    # Base widget — cached two-pass layout, gesture SM,
│   │   │   │                   # capture-stomp guard, single global capture slot
│   │   │   ├── FlexBox.h/cpp   # Row/column layout — own-dispatch container
│   │   │   ├── ContentGrid.h   # 4-quadrant draggable-divider grid
│   │   │   ├── Types.h         # Geometric types (Point/Size/Rect/Insets/Constraints/SizePolicy)
│   │   │   ├── UIContext.h     # Per-process render context (renderer, textMetrics,
│   │   │   │                   # layerStack, viewport, epoch)
│   │   │   ├── FontAdapter.h   # Font → fw2 TextMetrics shim
│   │   │   ├── LayerStack.h    # Floating overlay layers (modal/dropdown/tooltip/toast)
│   │   │   ├── Painter.h       # Per-typeid painter registry (separates logic + paint)
│   │   │   ├── Fw2Painters.h/cpp # Renderer impls registered at startup
│   │   │   ├── MenuBar.h       # FwMenuBar (application title strip + dropdown popup)
│   │   │   ├── ContextMenu.h   # fw2::ContextMenu (LayerStack-hosted, native MenuEntry)
│   │   │   ├── Dialog.h        # fw2 modal dialog + ConfirmDialog (scrollable)
│   │   │   ├── TextInputDialog.h
│   │   │   ├── ExportDialog.h
│   │   │   ├── Tooltip.h       # Hover-tracked tooltips with viewport clamp
│   │   │   ├── DeviceWidget.h  # Composite device panel (header + knob grid + viz)
│   │   │   ├── DeviceHeaderWidget.h
│   │   │   ├── FwGrid.h        # Row-major grid layout container
│   │   │   ├── SnapScrollContainer.h # Horizontal snap-scroll with nav buttons
│   │   │   ├── WaveformWidget.h      # Scrollable/zoomable waveform display
│   │   │   ├── AutomationEnvelope.h  # Breakpoint envelope editor widget
│   │   │   ├── Knob.h / Fader.h / Pan.h / Meter.h / Toggle.h / Button.h /
│   │   │   ├── DropDown.h / Scrollbar.h / Checkbox.h / TextInput.h / NumberInput.h
│   │   │   ├── GroupedKnobBody.h     # Section-grouped knob layout for synth bodies
│   │   │   └── *DisplayPanel.h        # Per-instrument inline visualisations
│   │   │                              #  (FM algo, ADSR curves, filter response, etc.)
│   │   └── panels/
│   │       ├── SessionPanel.h/cpp     # Session view (clip grid, scene management)
│   │       ├── ArrangementPanel.h/cpp # Arrangement timeline (clips, automation, loop)
│   │       ├── MixerPanel.h/cpp       # Mixer (faders, metering, sends)
│   │       ├── ReturnMasterPanel.h/cpp# Return + master strips
│   │       ├── DetailPanelWidget.h/cpp# Device chain panel (composite widgets)
│   │       ├── TransportPanel.h/cpp   # Transport controls with MIDI Learn
│   │       ├── PianoRollPanel.h/cpp   # MIDI piano roll editor
│   │       ├── BrowserPanel.h/cpp     # Files / Presets / Clip / MIDI tabs
│   │       ├── VisualParamsPanel.h    # Per-track visual knobs + shader chain editor
│   │       └── PreferencesDialog.h/cpp# Preferences (Audio, MIDI, Defaults, Metronome, Link)
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
│       ├── ableton_push1/      # Ableton Push 1 — encoders, 4-line SysEx LCD,
│       │   ├── manifest.lua    #  64-pad note + session modes, scale editor
│       │   ├── init.lua        #  Touch strip → pitchbend / mod wheel
│       │   ├── pads.lua
│       │   └── scales.lua
│       ├── ableton_move/       # Ableton Move — 32 velocity pads, 4 layouts,
│       │   ├── manifest.lua    #  scale visualisation, ripple LEDs, toast
│       │   ├── init.lua        #  channel (no native screen)
│       │   ├── pads.lua
│       │   └── scales.lua
│       ├── korg_nanokontrol2/  # Korg nanoKONTROL2 — 8 faders/knobs/banks,
│       │   ├── manifest.lua    #  mute/solo/arm with LED feedback,
│       │   └── init.lua        #  transport row, cycle = loop
│       └── yamaha_reface_dx/   # Yamaha Reface DX — touch strip → instrument
│           ├── manifest.lua    #  param 0, expression → track vol,
│           └── init.lua        #  CC 7 → master, notes routed natively
├── tests/                      # Unit & integration tests (Google Test, fw2-only)
│   ├── CMakeLists.txt
│   ├── test_Arrangement.cpp    # Arrangement clips, playback, transport loop
│   ├── test_AudioBuffer.cpp    # Audio buffer operations
│   ├── test_Automation.cpp     # Automation engine, envelopes, LFO
│   ├── test_Clip.cpp / test_ClipEngine.cpp
│   ├── test_Effects.cpp        # The audio effects
│   ├── test_FileIO.cpp / test_Serialization.cpp
│   ├── test_FollowAction.cpp
│   ├── test_FrameworkTypes.cpp # Geometric types only (Point/Size/Rect/etc.)
│   ├── test_Instruments.cpp    # The instruments
│   ├── test_Integration.cpp    # Cross-component integration (DetailPanel + synth,
│   │                           #  piano roll + transport, mixer, etc.)
│   ├── test_LFO.cpp / test_LinkManager.cpp
│   ├── test_MessageQueue.cpp / test_RingBuffer.cpp
│   ├── test_Metronome.cpp / test_Transport.cpp
│   ├── test_MidiClip.cpp / test_MidiClipEngine.cpp / test_MidiEffects.cpp
│   ├── test_MidiMapping.cpp / test_MidiTypes.cpp
│   ├── test_Mixer.cpp
│   ├── test_PanelAnimation.cpp
│   ├── test_PianoRoll.cpp
│   ├── test_Project.cpp / test_Theme.cpp / test_TrackControls.cpp
│   ├── test_UndoManager.cpp
│   ├── test_Warping.cpp        # Time stretching (WSOLA, Phase Vocoder)
│   └── test_fw2_*.cpp          # Per-widget fw2 tests — Button, Checkbox, Dialog,
│                               #  DropDown, Fader, FlexBox, FwGrid, Knob, MenuBar,
│                               #  Pan, Scrollbar, SnapScrollContainer, Toggle, ...
├── third_party/
│   ├── lua54/                  # Lua 5.4 vendored source
│   ├── sqlite3/                # SQLite3 vendored source
│   ├── basicpitch/             # MIT C++ port of Spotify Basic Pitch (ONNX)
│   └── demucs/                 # MIT C++ port of Demucs v4 stem separation (ONNX)
└── assets/                     # Runtime assets (copied to build dir)
    ├── shaders/                # Bundled MIT-licensed visual shaders + post-FX
    ├── examples/3d/            # Khronos sample glTF models (CC-BY 4.0)
    ├── reverbs/voxengo/        # 38 royalty-free Voxengo IRs (license + attribution
    │                           #  in NOTICES.md) — Convolution Reverb default folder
    └── nam/                    # 4 NAM amp captures from pelennor2170/NAM_models
                                #  (GPL v3, capturer attribution in filenames)
                                #  — Neural Amp default folder
```

> The Demucs stem-separation model is **not** in the source tree. Official
> release downloads bundle it in a `models/` folder beside the binary; source
> builds fetch it on demand to `~/.yawn/models/`. See [building.md](building.md).
