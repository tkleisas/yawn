# Features

The full feature list. For a condensed tour, see the [README](../README.md);
for build flags that gate some of these, see [building.md](building.md).

## Audio Engine
- **Real-time Audio Engine** — Lock-free audio thread with PortAudio (ASIO/WASAPI/ALSA), zero audio-thread allocations. The AI wrote it without being able to hear audio. We're not sure if that's a superpower or a disability.
- **Clip Playback** — Audio files (WAV, FLAC, OGG, AIFF, MP3), looping, gain, fade-in/out
- **Quantized Launching** — Launch clips on beat or bar boundaries with configurable quantize resolution (Next Bar, Next Beat, Immediate, 1/2, 1/4, 1/8, 1/16)
- **Transport** — Play/stop/record, BPM control, beat-synced position tracking, loop range with draggable markers
- **Ableton Link** — Network beat/tempo sync over LAN, automatic peer discovery, drift-free phase alignment. Plays nicely with Live, Logic, Bitwig, Reason, iOS apps — anything that speaks Link. Local UI tempo edits (typing into the BPM box, encoder turns) are gated through a `localTempoChanged` flag so the next audio buffer doesn't clobber your input by reading back the stale session tempo (race condition we found, fixed, and wrote a regression test for — once)
- **Metronome** — Synthesized click track with accent on downbeats, configurable volume & time signature, count-in (0/1/2/4 bars), mode selection (Always/Record Only/Play Only/Off)
- **Follow Actions** — 8 action types (Next, Previous, First, Last, Random, Any, Play Again, Stop), dual-action with probability (A/B chance), bar-count trigger duration
- **Time Stretching** — WSOLA (rhythmic/percussive) and Phase Vocoder (tonal/texture) algorithms, per-track speed ratio (0.25×–4×), 6 warp modes (Off/Auto/Beats/Tones/Texture/Repitch)
- **Transient Detection** — Adaptive threshold onset detection with BPM estimation, configurable sensitivity
- **Warp Markers** — Map original audio positions to target beat positions for flexible time-stretching

## Mixer & Routing
- **64-track Mixer** — Per-track volume, pan, mute, solo with peak metering. The AI mixed a song once. It sounded like a spreadsheet.
- **8 Send/Return Buses** — Pre/post-fader send routing with independent return channels
- **Master Bus** — Master volume with stereo metering
- **3-point Effect Insert** — Effect chains on tracks, return buses, and master
- **Audio Input Routing** — Per-track audio input channel selection, monitor modes (Auto/In/Off)
- **MIDI Routing** — Per-track MIDI input port/channel, output port/channel

## Recording
- **Audio Recording** — Per-track audio input recording with arm/disarm, overdub mode, multi-channel capture
- **MIDI Recording** — Record from hardware MIDI keyboards with note/CC capture, proper finalization on transport stop
- **Record Quantize** — Configurable quantize on record (None, Next Beat, Next Bar)
- **Count-in** — 0, 1, 2, or 4 bar count-in before recording starts

## Integrated Audio Effects

*23 hand-crafted artisanal effects, each lovingly hallucinated by an AI that has never used a compressor but has read 47 papers about them. We doubled the count in one batch and the AI is now insufferable about it.*

- **Reverb** — Schroeder/Moorer algorithmic reverb (4 comb + 2 allpass filters)
- **Delay** — Stereo delay with tempo sync, feedback, and ping-pong mode
- **Ping-Pong Delay** — Dedicated stereo bouncer with independent L/R times, explicit cross-feedback knob (vs. the original Delay's binary on/off ping-pong), width control over the dry-input split, per-side LP in the feedback path. Forked because tempo-synced 1/4-dotted-on-L + 1/8-on-R wants two Time knobs, not a workaround
- **EQ** — 3-band parametric EQ (low shelf, mid peak, high shelf)
- **Spline EQ** — 8-node parametric EQ with a custom drag-edit display panel: dual pre/post spectrum analyser overlay, RBJ-cookbook biquad response curve drawn through the cascaded filters, drag a node to set freq+gain, scroll-wheel or shift-drag for Q, click empty area to drop a new node (auto-grabs into a drag), right-click cycles type, double-click deletes. Per-node hover readout shows freq / gain / Q. The 40 underlying params remain settable via automation / preset / MIDI Learn — the panel just is the editor
- **Compressor** — Dynamics compressor with threshold, ratio, attack, release, makeup gain
- **Limiter** — Look-ahead brickwall limiter with attack/release smoothing for the master bus / pre-export safety net
- **Filter** — Multi-mode SVF filter (lowpass, highpass, bandpass, notch) with 2× oversampled stability
- **Chorus** — Modulated delay with multiple voices
- **Phaser** — Multi-stage all-pass cascade (4 / 6 / 8 stages) with LFO-modulated notches, feedback, stereo spread, dry/wet. The "swoosh" sweep effect made famous by every electric piano patch from 1973 onwards
- **Wah** — Standalone resonant bandpass with frequency / Q / mix knobs. For users who want manual / automated wah without routing through Envelope Follower's auto-wah path
- **Rotary** — Leslie-style rotary speaker simulation: dual horn + drum rotor with independent speeds (Slow / Fast / Brake), Doppler frequency modulation, AM tremolo from rotor movement, mic-position stereo spread. Auto-inserted into the chain when the user picks Drawbar Organ from the instrument menu
- **Distortion** — Waveshaper with soft clip, hard clip, and tube saturation modes
- **Bitcrusher** — Bit-depth quantization (1–16) + zero-order-hold sample-rate decimation (100 Hz – 48 kHz) + optional anti-alias pre-filter + TPDF dither toggle + dry/wet. Mid-tread quantizer so low bit depths don't add DC; aliasing is part of the sound, not a bug
- **Noise Gate** — Full expander/gate with hysteresis (open ≥ close threshold), attack / hold / release state machine, 0–10 ms lookahead (audio path is delayed; detection reads the un-delayed input so fast attacks don't clip transients), sidechain detection toggle, ducking polarity-invert mode (close when sidechain is hot — classic "kick ducks pad" pump)
- **Envelope Follower** — Audio level → control signal, optionally driving a built-in LP/HP/BP filter on the audio path (auto-wah). Sidechain input (Input/SC source toggle), Peak / RMS detection, asymmetric attack/release, depth in semitones-of-cutoff-offset, range up to 6 octaves. Doubles as a routable modulation source via `AudioEffect::hasModulationOutput / modulationValue` + an atomic `consumeEnvelope()` for cross-thread reads — set Filter Type = Off and the device exists purely to publish the envelope value for visual params or modulation depths elsewhere
- **Convolution Reverb** — IR-based reverb via uniformly-partitioned FFT block convolution — 10s max IR @ host rate, ~30 MFLOP/s vs ~11 GFLOP/s direct convolution; one-block latency from sub-block buffering, inaudible for reverb. Pre-delay 0–200 ms, low-cut + high-cut on the wet path, IR gain trim, mix. Loader handles `.wav / .flac / .aif / .aiff / .ogg / .mp3` with auto-resample to host rate (a 44.1 kHz IR on a 48 kHz host would otherwise play 9 % low and short — easy to miss). RCU-lite atomic-engine-swap on IR load (audio thread acquire-loads a stable engine pointer per block; old engines park in a retired list and destruct on the NEXT load) so reloading IRs while audio is rolling can't use-after-free. Ships with **38 bundled Voxengo reverb IRs** so the device is usable on first install — file dialog opens at the bundled folder, license + attribution in `NOTICES.md`
- **Neural Amp** — [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) (`.nam`) inference effect with input gain / output gain / mix. NeuralAmpModelerCore + Eigen vendored via `FetchContent`, built as a C++20 static lib while the rest of YAWN stays C++17 — a PIMPL split keeps the C++20 NAM headers contained to a single TU. Linker forced to `WHOLE_ARCHIVE` because MSVC's function-level DCE was silently stripping `nam::get_dsp` (cost: ~6 MB binary, benefit: the device actually does anything). Same RCU-lite atomic-DSP-swap as Conv Reverb so loading models while audio plays doesn't crash on the third load. Bundled 4 community NAM amp captures (Clean / Crunch / High-gain / Bass) from `pelennor2170/NAM_models` under GPL v3 with capturer attribution preserved in filenames; load any other `.nam` from `tonehunt.org` / `tone3000.com`
- **Tape Emulation** — Analog tape simulation with asymmetric saturation, wow/flutter, tape hiss, and tone rolloff
- **Amp Simulator** — Guitar/bass amp modelling with 4 amp types (Clean/Crunch/Lead/High Gain), 3-band tone stack, cabinet simulation
- **Tuner** — YIN pitch detection with frequency/cents/note display, reference pitch control (420–460 Hz), confidence indicator
- **Oscilloscope** — Real-time waveform visualizer (non-destructive analysis effect)
- **Spectrum Analyzer** — FFT-based frequency spectrum display (non-destructive analysis effect)

### Sidechain + modulation routing for effects

`AudioEffect` carries the same `setSidechainInput(buffer)` / `supportsSidechain()` plumbing as `Instrument` — `AudioEngine` fans the per-track sidechain pointer (set via `SetSidechainSourceMsg`, sentinel `-2` = live audio interface input, `-1` = none, `0..N-1` = source track) to BOTH the instrument AND every effect on the same track. So a Noise Gate, Envelope Follower, or future sidechain-aware compressor on track B can react to track A's audio without per-effect routing UI.

`AudioEffect::hasModulationOutput()` + `modulationValue()` mirror the existing LFO MidiEffect modulation source pattern on the audio side. Envelope Follower advertises both this and an atomic `consumeEnvelope()` accessor so the visual engine, knob displays, and any other UI/automation consumer can subscribe to the live envelope value without touching the audio thread.

`AudioEffect::saveExtraState() / loadExtraState()` parallel `Instrument`'s preset-extra-state hooks. Conv Reverb persists the IR file path across project save/load and **rehydrates the actual sample data on project open** — `App::rehydrateConvolutionIRs` walks every effect chain after `syncTracksToEngine` and re-reads any IR file referenced in extraState. Same hook is wired for NeuralAmp's `.nam` path.

## VST3 Plugin Hosting

*The AI built a plugin host before learning what a plugin sounds like. It correctly implemented the entire VST3 COM interface on the first try. We're terrified.*

- **Plugin Scanning** — Automatic discovery in standard system paths: Windows (Program Files/Common Files/VST3, user LocalAppData/Programs/Common/VST3) and Linux (`/usr/lib/vst3`, `/usr/local/lib/vst3`, `~/.vst3`) — class enumeration with vendor/category info
- **VST3 Instruments** — Load third-party VST3 synths as track instruments with full parameter automation
- **VST3 Audio Effects** — Load VST3 effects in any effect chain slot (track, return, master)
- **Process-Isolated Editor** — Plugin GUIs run in a separate process (`yawn_vst3_host`) via bidirectional IPC. On Windows this dodges JUCE plugins' process-wide Win32 message hooks that would freeze our event loop; on Linux the child embeds the plugin via X11 (`kPlatformTypeX11EmbedWindowID`) and runs a full `Steinberg::Linux::IRunLoop` with FD + timer dispatch so plugins like Surge XT render and animate correctly
- **Parameter Sync** — Full bidirectional parameter sync between host and editor process
- **State Persistence** — Processor + controller state serialized with project (hex-encoded binary)
- **Generic Knob Grid** — Automatic parameter knob UI for plugins without custom editors

## Integrated Instruments

*15 hand-crafted instruments. The AI built FM synthesis before learning that Op2 → Op1 with feedback is the entire DX7 e-piano. Then it built a 2-op e-piano on purpose. The PM keeps adding "just one more".*

- **Subtractive Synth** — 2-oscillator analog-style synth with SVF filter, 23 parameters, 16-voice polyphony
- **FM Synth** — 4-operator FM synthesizer with 8 algorithm presets, 19 parameters
- **Sampler** — Sample playback with pitch tracking, linear interpolation, ADSR envelope
- **Karplus-Strong** — Physical modelling string synth with 4 exciter types, damping, body resonance, string stretch
- **Wavetable Synth** — 5 algorithmic wavetable types with position morphing, SVF filter, LFO modulation, sub oscillator, unison
- **Granular Synth** — Sample-based granular synthesis with 4 window shapes, position/spread/spray, scan, pitch jitter, stereo width
- **Vocoder** — Band-based vocoder with 4 carrier types (Saw/Square/Pulse/Noise), 4–32 bands, envelope followers, formant shift
- **String Machine** — Solina-style ensemble strings (1974 ARP reference): 9-saw stack per voice (3 detuned saws × 16′/8′/4′ octaves), shared paraphonic LP filter (200–8000 Hz log Brightness), 3-tap BBD chorus with stereo spread (the chorus IS the sound), 16-voice polyphony, slow attack/release envelope. The whole reason "Synthstrings" exists in every preset bank since 1980.
- **Drawbar Organ** — Hammond B-3 in software: 9 drawbars (16′/5⅓′/8′/4′/2⅔′/2′/1⅗′/1⅓′/1′ at standard registration weights), key click on note-on, percussion (3rd / 2nd, normal/soft), polyphonic with proper tone-wheel additive synthesis, drives a Rotary speaker effect (auto-inserted into the chain) for the Leslie wobble that makes a B-3 a B-3
- **Electric Piano** — 2-op FM e-piano with three modes: **Rhodes** (14:1 ratio bell shimmer + pan tremolo), **Wurli** (3:1 ratio woody bark + amp tremolo, like the Wurlitzer 200A vibrato), **Suitcase** (Rhodes with slightly more mod depth + auto-inserts Phaser into the chain — the iconic Mark V / Steely Dan rig). Velocity-driven mod index (soft = pure sine, hard = metallic bell), per-strike hammer-noise transient for the percussive "thock", exponential decay envelope (no sustain stage — rolls naturally to silence the way a real EP does)
- **Multisampler** — Multi-zone sample player with key/velocity mapping, per-zone tuning/volume/pan/loop, velocity crossfade, dual ADSR, zone-list + per-zone editor UI. Build instruments in minutes via the integrated [Auto-Sampler](#auto-sampler) — no VB-CABLE, no Stereo Mix, no third-party tools
- **Instrument Rack** — Multi-chain container (up to 8 chains) with key/velocity zones, per-chain volume/pan, chain enable/disable toggle, visual zone bars, add/remove chain UI. **Per-chain instrument selection + parameter editing** — the selected chain's nested instrument appears as its own widget in the device strip, edit any synth's params without leaving the rack view; right-click any chain row → **Change Instrument →** to swap the synth (full instrument list except nested rack). **Per-chain audio fx chain** (lazy-allocated per chain, processed pre-mix) with right-click "Add Chain FX →" listing all 23 audio effects; ChainFx widgets appear after the chain instrument and swap when the user picks a different chain. Default chain auto-created on construction (full-range SubtractiveSynth) so the rack makes sound immediately
- **Drum Rack** — 128 pads with 4×4 grid display, 8-page navigation, per-pad sample loading via drag & drop. Per-pad **volume / pan / pitch / choke group (1–4) / AR envelope (attack-release shape) / region trim (start–end, end<start = reverse with REV badge on the waveform) / effect chain** (lazy-allocated per pad — zero memory when unused; right-click a pad → "Add Pad FX →" with all 23 audio effects). Waveform preview with region markers + tinted played region. **Kit presets** save/load every loaded pad's WAV plus per-pad params + per-pad fx chains as a single asset (one file = one kit)
- **DrumSlop** — Loop slicer drum machine: auto/even/manual slicing, 16 pads with ADSR, SVF filter, per-pad effect chains, configurable MIDI base note
- **Drum Synth** — Fully-synthesised 8-piece kit (Kick / Snare / Clap / Tom 1 / CHH / OHH / Tom 2 / Tamb), GM-mapped MIDI notes (C1 / D1 / E1 / F1 / F#1 / A#1 / G1 / G#1), per-drum DSP (sine + pitch sweep + click for the kick; metallic-ratio square sums for hats; tuned noise + envelope for snare/clap/toms; etc.), per-drum tune / decay / volume / pan, CHH/OHH choke group, sample-free so it travels anywhere the project does. Companion to Drum Rack for users who want a tweakable kit without managing samples

## Auto-Sampler

*Build a Multisampler instrument from any MIDI source — hardware synth, soft synth, VST3 plugin, the GS Wavetable Synth Windows ships with — by sweeping a note grid, capturing the audio response, and slicing the keymap automatically. The AI taught itself how to auto-sample a synth before learning what a synth was. Then it wrote a tuner. The numbers were wrong, the AI said "Ah, I see the issue!" 14 times, and now they're right.*

- **MIDI grid drive** — Sweep a (note × velocity-layer) matrix through any open MIDI output. Defaults: C2–C7, every major-third, **4 velocity layers** (matches Logic Sampler Auto / Redmatica / SampleRobot defaults), 2.0 s note hold + 1.5 s release tail
- **Lock-free private capture** — Per-note recording via a SPSC side-channel on the audio engine, independent of transport / clip / track recording. Captures interleaved float WAVs at the engine's running sample rate
- **WASAPI loopback (Windows)** — Capture system playback **without** VB-CABLE, Voicemeeter, or Stereo Mix. Every Windows playback endpoint shows up in YAWN's input device dropdown as `[loopback]`; pick one, point your synth at the same output, done. Stream rate is auto-negotiated against the device's mix format (typically 48 kHz) and every rate-cached subsystem (instruments, effects, transport, clip engines) is re-pinned to `Pa_GetStreamInfo()`'s truth so a 44.1 ↔ 48 kHz step doesn't pitch the whole engine 147 cents off (a bug we found, fixed, and have a story about)
- **Test Note button** — Toggles a sustained C4 v100 over the chosen MIDI port. Live **VU meter** shows input peak with the user-set **Level knob** (–24 dB to +24 dB) baked in — what you see in the meter is what hits disk, so you dial gain against the meter and ride the loudest preset just below 0 dBFS
- **Per-note silence trim** — Configurable threshold (default −60 dBFS) trims dead air at the start of each capture; preserves the attack with a 10 ms safety margin
- **Auto zone slicing** — Key ranges split at midpoints between adjacent root notes; velocity ranges split at midpoints between adjacent layers. Playback covers the whole keyboard seamlessly across zones
- **Folder layout** — `<project>.yawn/samples/<sanitized_capture_name>/<root>_v<vel>.wav` plus a `manifest.json` describing the run. Default capture name is derived from the track name (`midi_1_capture`, `piano_capture`, etc.) and sanitized to filename-safe form

### Workflow

1. Add a Multisampler to a track (or pick one)
2. Click **Auto-Sample…** in the device's display panel
3. Pick MIDI port + channel, audio input + mono/stereo, note range + step, velocity layer count, note-length / release-tail timing
4. Hit **Test Note** — verify the synth speaks and the VU meter swings; dial the **Level** knob until the loudest preset peaks just below 0 dBFS
5. **Capture** → progress bar + live "Now: C4 vel 100" status, ~3–4 minutes for a default 64-sample run
6. Done → zones populate the Multisampler, project marks dirty, samples + manifest land in the project's `samples/` folder. Save the project to keep them

### Saving a preset

- **Save Preset…** on the Multisampler captures the full instrument: 14 global params (Amp ADSR / Filter / Filt Env / Glide / Vel Crossfade / Volume) **plus** every zone's audio + keymap (root, key range, vel range, tune, vol, pan, loop). Per-zone WAVs are written to `<presets>/multisampler/<preset>/zone_NN.wav` with `sampleRate` stamped into the JSON so playback compensates for engine-rate changes
- **Project-local mirror** — When a project is open, every Save Preset writes a copy into `<project>.yawn/presets/multisampler/` so the project folder is self-contained for sharing / archiving. The preset menu unions both lists (project-local wins on name collision)
- The Browser's Presets tab refreshes on every save (no app-restart required)

## MIDI
- **MIDI Engine** — Internal 16-bit velocity, 32-bit CC resolution (MIDI 2.0 ready)
- **MIDI I/O** — Hardware MIDI via RtMidi (WinMM/ALSA), multi-port input/output
- **MPE Support** — Per-note pitch bend, slide, pressure via zone management
- **8 MIDI Effects** — Arpeggiator (free-running & transport-synced), Chord, Scale, Note Length, Velocity, Random, Pitch, LFO
- **MIDI Learn** — Map any CC or Note to any parameter (instrument, effect, mixer, transport), learn mode with visual feedback, per-channel or omni, JSON persistence
- **MIDI Monitor** — Lock-free 65K-event ring buffer tracking all message types (Note, CC, PitchBend, Pressure, Clock, SysEx), port identification, millisecond timestamps
- **Stem Separation (Demucs v4)** — Right-click an audio clip → **Separate Stems** and Meta's [Demucs v4 (hybrid transformer)](https://github.com/facebookresearch/demucs) splits it into **drums / bass / other / vocals** on four new audio tracks in the same scene. Inference runs on **ONNX Runtime** via a MIT C++ port (adapted from [sevagh/demucs.onnx](https://github.com/sevagh/demucs.onnx)) in `third_party/demucs/`, with STFT/ISTFT + 7.8 s segment chunking in C++. The ~170 MB model ships **bundled in the official release downloads** (in a `models/` folder beside the binary), so stem separation works out of the box; source / dev builds without the bundled model **download it on demand** (via `curl`) into `~/.yawn/models/` on first use. Runs on a worker thread with progress toasts; **Esc** cancels. CPU-only and genuinely slow (minutes per song — Demucs was built for GPUs), so it's opt-in behind `-DYAWN_HAS_STEM_SEPARATION=ON` (default OFF, **on in official release builds**; Linux x64 + Windows x64; macOS not wired). *The AI cannot hear the drums it is extracting, will wait five minutes to not-hear them, and calls this a feature.*
- **Audio → MIDI (Basic Pitch)** — Right-click an audio clip → **Convert to MIDI** and Spotify's [Basic Pitch](https://github.com/spotify/basic-pitch) polyphonic transcription model drops the detected notes onto a new MIDI track in the same scene. Inference runs via **ONNX Runtime**; a MIT C++ port (adapted from [sevagh/basicpitch.cpp](https://github.com/sevagh/basicpitch.cpp)) lives in `third_party/basicpitch/`, built as a C++20 static lib while the rest of YAWN stays C++17 — a plain-struct facade (`bp_api.h`) keeps Eigen + ONNX Runtime off the C++17 side, the same PIMPL trick as the Neural Amp device. The 226 KB model is embedded as a byte array so there's no runtime asset to ship. Opt-in behind `-DYAWN_HAS_BASIC_PITCH=ON` (default OFF, **on in official release builds**; Linux x64 + Windows x64, both CI-verified — macOS not wired). *The AI built a feature to turn sound it cannot hear into notes it cannot read, then wrote a unit test feeding a 440 Hz tone to assert the machine agrees that A is, in fact, A.*

## MIDI Loops

*The AI generated ~2,600 loops across 24 genres — including time signatures it cannot count to — for a feature it will never hear. It wrote a Balkan 7/8 groove and an Indian Rupak tal before learning that "4/4" is not a fraction you simplify. It then had to add a guard so its own 15/16 loops wouldn't get silently rounded up to 4 bars by its own MIDI reader. Then it generated 32 melodic loops, the PM said "why so few", and it generated 2,000 more by deriving a bassline from every single kick drum it had ever written. The AI was, in this metaphor, both the arsonist and the fire brigade.*

- **Loops Browser tab** — Browse / search / filter the MIDI loop library by category (drums, bass, lead, chord, misc). Double-click to load into the selected slot, or **drag a loop straight onto a session clip slot** — with a live drop-zone highlight (blue on valid MIDI tracks, red where it won't land).
- **Save to library** — Right-click any MIDI clip → **Save to Loop Library…**. The loop is auto-categorised from its note content and stamped with the current transport tempo + time signature; the Browser refreshes on save, no restart.
- **SMF read/write** — Minimal Standard MIDI File reader + writer (`util/MidiFileIO`, type 0/1, PPQ) round-trips construction-kit loops: 16-bit velocity ↔ 7-bit, GM drum channel preserved, tempo + time-signature meta. The reader rounds loop length up to whole beats, which is exactly why odd meters needed a sanity guard
- **Self-healing index** — SQLite `midi_loops` table populated by the async library scanner; rows for loops whose files were deleted are pruned on the next scan, so the browser never lists a loop that isn't there
- **Factory drum construction-kit** — ~526 loops seeded on first launch from authored 1-bar grooves + fills composed by arrangement templates into multi-bar patterns (favouring 4- and 8-bar phrases with fill turnarounds). 24 genres: rock, funk, disco, house, techno, hip-hop, breakbeat, dnb (amen-style), idm, waltz, plus world (Latin, salsa, Balkan 7/8, Greek 9/8, Arabic maqsum, Indian Rupak 7/8) and deliberately-awkward meters (5/4, 7/8, 9/8, 11/8, 13/8, **15/16**, 5/16, 7/4). Seeding is idempotent — delete what you don't want and it stays gone
- **Factory melodic kit (bass / lead / chord / misc)** — ~526 loops *per category*, one melodic sibling for every drum loop, derived from the **same rhythm** as its drum counterpart: bass off the kick, chords off the snare/clap, lead off the hats, misc off everything at once. In-key pitch comes from a per-genre scale + chord progression (minor / dorian / phrygian / major, plus Hijaz and Bhairav for the eastern genres), with note velocities inherited from the drum hits. Names line up (`Funk Bass 13 (8-bar fill)` sits next to `Funk 13 (8-bar fill)`), so the whole construction kit grooves together. Both kits share one rhythm engine

## Automation & Modulation
- **Automation Engine** — Per-parameter breakpoint envelopes with Read/Touch/Latch modes
- **Track Automation** — Automation lanes in arrangement view with click to add/drag/right-click delete breakpoints
- **Clip Automation** — Per-clip automation lanes (relative to clip start, loops with clip)
- **Automation Recording** — Touch/Latch parameter recording from UI knob interaction
- **LFO Device** — Per-track LFO with 5 waveforms (sine, triangle, saw, square, S&H), tempo sync, depth, phase, polarity
- **LFO Linking** — Stable ID-based linking to any instrument/effect/mixer parameter across tracks, survives reordering. A named target picker shows exactly what each LFO drives, and visual-channel devices / shader params are valid targets too
- **Automation Targets** — Instrument params, audio effect params, MIDI effect params, mixer (volume, pan, sends), visual knobs + shader uniforms

## Session View
- **Clip Grid** — 8 visible tracks × 8 scenes, scrollable, clip launching with quantized triggers
- **Scene Management** — Insert, duplicate, delete scenes with undo support, automatic renumbering
- **Scene Launching** — Click scene label to launch all clips in a scene simultaneously
- **Follow Actions** — Per-clip chained actions with dual-action probability
- **Track Management** — Add, delete tracks with confirmation dialog (stops engine, shifts all arrays)
- **Context Menus** — Right-click track headers for type/instruments/effects, right-click scenes for insert/duplicate/delete, right-click clips for stop

## Arrangement View
- **Timeline Grid** — Horizontal beat/bar grid with zoom (4–120 px/beat), scroll, snap-to-grid (off/bar/beat/half/quarter/eighth)
- **Clip Placement** — Click to select, drag body to move (same + cross-track), drag edges to resize, double-click to create, Ctrl+D to duplicate, Delete to remove
- **Arrangement Playback Engine** — Per-track clip rendering (audio + MIDI) with fade-in/out, thread-safe clip submission
- **Session/Arrangement Toggle** — Per-track S/A button, auto-activates on view switch when clips exist
- **Automation Lanes** — Expandable per-track lanes showing breakpoint envelopes, visual curve rendering
- **Loop Range** — Green markers in ruler, Shift+click to set, drag to adjust, L key to toggle
- **Auto-Scroll** — Playhead stays visible during playback (F key to toggle)
- **Waveform Display** — Audio clip waveform rendering in arrangement blocks

## Project Management
- **Project Save/Load** — JSON-based `.yawn` format with full round-trip serialization
- **Serialized State** — Tracks, scenes, clip grid, instruments, effects, MIDI effects, mixer state, automation, arrangement clips, MIDI Learn mappings
- **Sample Management** — Referenced audio samples copied to project folder
- **Audio Export** — Offline render to WAV/FLAC/OGG with bit depth (Int16/Int24/Float32) and sample rate selection, scope (full arrangement or loop region), progress tracking with cancellation
- **Undo/Redo** — Full undo/redo system with action merging (Ctrl+Z / Ctrl+Y)

## UI Framework

*Originally written as a v1 widget library. Then a v2 widget library was written next to it because the AI got bored. Then we lived with both for a while because nobody wanted to deal with it. Then we deleted the v1 library in three commits totalling −2960 lines and pretended we'd planned it that way the whole time.*

- **Single fw2 framework** — One `Widget` base class, one event type per kind, one global `capturedWidget()` slot, one `dispatchMouseDown` walking the tree. Used to be two of each running side-by-side via 766 lines of bridge wrappers. The bridge wrappers are gone. The capture-stomp class of bug is structurally impossible (only one capture slot to stomp now).
- **Cached two-pass layout** — Measure / layout pipeline with a global epoch + per-widget local-version cache. Re-layout of a stable tree is near-free; widgets opt out of the auto-relayout-boundary heuristic when their measured size depends on their children (which is most containers, as it turns out)
- **Hardening guards** — `Widget::captureMouse` warns + asserts when an own-dispatch container takes capture while a descendant of its own subtree already holds it (the recurring "dial doesn't turn" trap). MSVC `/we4717` (always-recursive function) is now a compile error after a stack-overflow took 3 hours to diagnose because we'd been ignoring the warning. `/we4715`, `/we4716`, `/we4172`, `/we4533`, `/we4701` likewise; gcc/clang counterparts via `-Werror=infinite-recursion` etc.
- **FlexBox** — Row/column layout container with stretch/flex/fixed size policies, gap, justify, align. Walks children for mouse dispatch (no children-walking-on-rails framework code; container widgets explicitly route)
- **ContentGrid** — 4-quadrant container with draggable horizontal + vertical dividers, used for the session/mixer/browser/return-master split
- **Session Panel** — Ableton-style clip grid with scrollable tracks and scenes
- **Arrangement Panel** — Horizontal timeline with track headers, clip blocks, automation lanes, ruler, playhead, loop markers
- **Mixer Panel** — Channel strips with interactive faders, pan knobs, mute/solo buttons, peak metering
- **Device Chain Panel** — Composite widget architecture: DeviceWidget (header + grid + knobs + visualizer), SnapScrollContainer, neon arc knobs with 24-segment rendering. Device headers show the loaded preset name
- **Grouped Instrument Layouts** — Instruments display knobs in logical sections (Global, Op 1–4, Filter, Amp, etc.) with inline graphical displays instead of flat grids
- **Instrument Display Widgets** — FM algorithm routing diagram, ADSR envelope curves, oscillator waveform previews, filter response curves, composite synth panels
- **Visual Params Panel** — Per-track visual-knob and shader-chain editor that docks at the bottom of the screen for visual tracks (replacing the audio detail panel)
- **Waveform Widget** — Interactive waveform display with zoom/scroll, overview bar, playhead tracking, transient markers, warp marker editing (create/drag/delete), loop region overlay
- **Piano Roll Editor** — MIDI note editing with draw/select/erase tools, zoom/scroll, velocity, snap-to-grid, follow-playhead mode, clip operations (duplicate, double, halve, reverse, clear, set 1.1.1 here)
- **Layer Stack** — Floating-overlay layer system for modal dialogs, dropdowns, context menus, tooltips, and toasts. Overlays sit above the main widget tree and intercept events with proper outside-click-dismiss semantics
- **Export Dialog** — Format/bit depth/sample rate selectors, scope selection, progress bar with cancellation
- **Preferences Dialog** — Audio devices, MIDI ports, default quantize, metronome settings, font scale, Ableton Link enable
- **Primitive Widgets** — FwButton, FwToggle, FwKnob (with double-click text entry, step snapping, format callbacks, unit-aware edit buffer), FwFader, FwPan, FwMeter, Label, FwTextInput, FwNumberInput, FwDropDown, FwScrollbar, all with hover animations and gesture state machines
- **Dialog System** — fw2 `Dialog` / `ConfirmDialog` / `FwTextInputDialog` / `FwExportDialog` / `FwPreferencesDialog` on the modal layer with title bar, OK/Cancel, drag-to-move, Escape/Enter handling. Over-tall dialogs scroll
- **Context Menus** — fw2::ContextMenu with submenus, keyboard navigation, separators, headers, checkable + radio rows. Right-click track headers, scene labels, clips, transport buttons, knobs (for MIDI Learn), visual clips, etc. All menus are native fw2::MenuEntry (the v1 menu bridge is gone)
- **Menu Bar** — File, Edit, View, Track, Scene, MIDI, Help menus with keyboard accelerators (auto-detected from menu items — type `D` and the panel toggles)
- **DPI Scaling** — Auto-detect display scale (SDL3), user override, scaled() helper for all layout constants. Theme epoch bump invalidates every widget's measure cache atomically when font-size or DPI changes
- **Panel Animations** — Smooth exponential-lerp height transitions on panel collapse/expand. Animation lives in a per-frame `tick()` method (not in `onMeasure`, because a measure cache makes "call measure 60 times to converge" silently broken — found out the hard way)
- **Toast Notifications** — Top-center status banner with replace-latest semantics, severity accent (info/warn/error), 1.5 s hold + 200 ms fade. Thread-safe; fired from controller scripts (`yawn.toast(...)`), project save/load, video import, and other async events. Designed partly as a screen substitute for controllers without their own display (e.g. Ableton Move, nanoKONTROL2, Reface DX)
- **Tooltip Manager** — Hover-tracked tooltips with delay + viewport-edge clamping
- **Virtual Keyboard** — QWERTY-to-MIDI mapping (Q2W3ER5T6Y7UI9O0P), Z/X octave switching, per-key note tracking, velocity selector on the transport bar. Yields number keys to text-input edits so typing a knob value doesn't accidentally play notes
- **Track Selection** — Click to select tracks, highlight in session & mixer views
- **Track Type Icons** — Waveform icon for audio tracks, DIN circle icon for MIDI tracks, monitor icon for visual tracks
- **Targeted Drag & Drop** — Drop audio files onto specific clip slots; drop video files onto visual tracks; drop samples onto Sampler/DrumRack/Granular; drag MIDI loops from the Loops browser onto MIDI clip slots
- **Custom 2D Renderer** — Batched OpenGL 3.3 rendering with font atlas (stb_truetype), texture atlas, scissor-stack clipping
- **Crash Handler** — Signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL) with stack traces (Windows: SymFromAddr + dbghelp, Unix: backtrace + addr2line), crash log appended to `yawn.log`
- **Multi-window Ready** — Built on SDL3 for the visual output window (and future detachable panels)

## Controller Scripting

*The AI embedded a scripting engine inside a DAW it wrote, so you can control the DAW it wrote with scripts it wrote. Now four pieces of hardware speak it. We're four layers deep and we're not coming back.*

- **Lua 5.4 Engine** — Embedded Lua scripting for MIDI controller integration, vendored amalgamation with `yawn.*` API
- **Auto-Detection** — Manifest-based controller matching: scripts declare port name patterns, YAWN auto-connects on startup
- **Multi-Port Support** — Controllers with multiple MIDI ports (Push 1's Live + User ports, Move's four-port surface) are merged into a single byte-oriented SPSC ring buffer
- **`yawn.*` Lua API** — Full read/write access to device parameters, track/instrument info, MIDI output, SysEx, transport state, master volume, loop, and toasts. Now ~50 functions. The PM keeps adding more
- **Device Parameter Control** — Read param count/name/value/min/max/display, set values via lock-free audio command queue
- **Toast Channel** — `yawn.toast(text, duration)` from any callback shows a top-center banner in the YAWN window. Designed as a screen substitute for hardware without its own display (Move, nanoKONTROL2, Reface DX)
- **Hot Reload** — Menu → Reload Controller Scripts to disconnect, rescan, and reconnect without restarting. Edit the script in any editor, save, click reload, your changes are live. The 47-step debug cycle is now a 1-step debug cycle
- **Port Exclusivity** — Controller-claimed MIDI ports are automatically excluded from the general MIDI engine (Windows' exclusive-access policy made us learn this the hard way)

### Ableton Push 1

- **Pad Modes** — Note mode (chromatic & scale), Drum mode (4×4 auto-switch for DrumRack/DrumSlop), Session mode (8×8 clip grid with armed/playing/recording LED colors)
- **30+ Scales** — Western modes, pentatonic, blues, and Maqam/Eastern scales (Hijaz, Bayati, Rast, Nahawand, Saba, and more); shared scale catalog with Move
- **Scale Editor** — Select root note, scale type, row interval, and octave directly from Push encoders
- **8 Encoders** — Relative-encoded CC 71–78 mapped to device parameters with paging, coarse/fine (Shift), and stepped/discrete param support
- **Transport Controls** — Play, Metronome, Tap Tempo, BPM encoder, Master Volume — all with button LED feedback
- **SysEx Display** — 4-line text display: param names/values, track name, instrument, scale/mode info. Stopped working for 3 hours once because of one missing column-offset byte. The PM dug up his own 10-year-old Push code to prove the AI wrong
- **Pad LED Ripple** — Expanding ring animation on pad press with held-pad persistence
- **Auto-mode Switch** — Drum instruments (DrumRack/DrumSlop) auto-switch to 4×4 pad layout; melodic instruments restore note mode
- **Touch Strip** — Pitch bend by default, mod wheel (CC 1) when Shift is held — sent to the selected track

### Ableton Move

*No OLED, no problem. Move's display is proprietary-protocol territory that only Ableton Live speaks, so YAWN drives the pad LEDs over standard Push-family MIDI (which Move accepts) and uses a top-center toast banner as the screen substitute.*

- **Full-coverage pad grid** — All 32 velocity-sensitive pads (notes 68–99) play the current scale; LED colors mark root / in-scale / out-of-scale / pressed
- **Scale visualization + layout presets** — 30+ scales (shared catalog with Push 1), cycle layouts **4ths / 3rds / 5ths / Octaves** via Shift+Track/Session
- **Shift-modifier navigation** — `+`/`−` = octave (Shift = root note ±1 semitone), `<`/`>` = track (Shift = scale), Track 1–4 buttons jump directly
- **Two encoders** — Main (dented) encoder navigates tracks/scales; Master (smooth) encoder controls volume/BPM. Touch-sensitive: tap the knob to peek the current value without changing it
- **Push-style LED ripple** — Expanding 3-ring wave on every pad press (~500 ms), cyan → blue → brown fade, configurable palette and speed
- **Scene launch** — First 8 numbered buttons launch scenes 0–7
- **1 Hz LED heartbeat** — Move's firmware clears pad state on its own without Ableton Live's pairing; YAWN re-asserts the grid once a second so the layout stays visible during a long session

### Korg nanoKONTROL2

*The flat plastic mixer that's outlived three OS versions, two USB standards, and a generation of musicians. Of course we support it.*

- **8 faders → track volume** with sliding-window banking across YAWN's 64 tracks (Marker ◀ / ▶ to shift the visible window by 8)
- **8 knobs → track pan**
- **24 channel buttons → mute / solo / record-arm**, with LED feedback synced to engine state
- **Transport row** — Play, Stop, Rec, Cycle (loop on/off, LED-synced), Rew/Fwd as track prev/next
- **Marker Set button** doubles as a "force LED resync" — handy when you've muted from the YAWN UI while the controller was unplugged and the LEDs have drifted out of sync
- **Toast feedback** on track selection and bank shifts (since the unit has no display)
- **Setup**: assumes the unit is in CC mode (factory default — no Korg Kontrol Editor needed)

### Yamaha Reface DX

*A 37-key FM synth from a company that also made keytars in 1985. We respect the bloodline.*

- **Touch strip → instrument param 0** of the selected track, scaled across the param's natural range — drive a synth's primary expression parameter live with one finger
- **Expression pedal CC (CC 11) → selected track volume**
- **Volume CC (CC 7) → master volume**
- **Sustain (CC 64), pitch bend, notes** — handled by YAWN's standard MIDI engine, no script needed
- **Toast on each touch-strip change** showing the current parameter's display value (rate-limited to one toast per unique value, so it doesn't spam during a sweep)
- **Instrument-aware**: switching the selected track in YAWN retargets the touch strip to that track's instrument param 0. Pair with the nanoKONTROL2's track navigation buttons for hands-free re-targeting

> See [ableton-move.md](ableton-move.md) for the Move's full button map, encoder behavior, LED palette, and toast scheme.
>
> See [controller-scripting.md](controller-scripting.md) for the full Lua API reference, every controller's button/CC map, and the guide to writing your own script.

## Visual / VJ Engine

*The AI wrote a DAW. Then it wrote a GPU-based VJ tool **inside** the DAW. Then it wrote an ffmpeg import pipeline so you can drop a Lumière Brothers film onto a visual track and bar-sync it to your bass line. This is how the singularity comes for techno.*

- **Secondary output window** — Separate SDL3 window with its own GL context (shared resources with the main UI context). F11 toggles fullscreen; typical workflow is main UI on display 1, fullscreen visuals on display 2.
- **Per-track GPU layers** — Each Visual track gets its own 640×360 FBO and shader program. Track volume = layer opacity, track index = compositor order (lower on bottom). Compositor uses ping-pong accumulator FBOs with four blend modes: **Normal / Add / Multiply / Screen**, source-alpha aware so partial-alpha shaders composite correctly.
- **Shadertoy-compatible shaders** — Standard `mainImage(out vec4, in vec2)` entry point, standard uniforms (`iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iMouse`, `iDate`, `iSampleRate`, `iChannel0..3`, `iChannelResolution`, `iChannelTime`) plus YAWN-specific extensions (`iBeat`, `iTransportTime`, `iTransportPlaying`, `iAudioLevel`, `iAudioLow/Mid/High`, `iKick`, `iTextWidth`, `iTextTexWidth`). Paste most Shadertoy snippets in verbatim.
- **Hot-reload shader authoring** — `.frag` files live on disk; mtime polled each frame. Save in any editor, YAWN recompiles. Compile errors keep the previous program active so the show continues.
- **8 generic playable knobs (A–H)** — Always-available `uniform float knobA..knobH` in every shader. Matches hardware encoder banks (Push/Move/APC). Per-knob LFO (Sine/Triangle/Saw/Square/S&H, beat-synced rate, 10–100% depth) and per-knob MIDI Learn via right-click menu.
- **Custom shader parameters** — Declare `uniform float speed; // @range 0..4 default=1.0` in your shader, get an auto-generated knob in the Visual Params panel. Values persist per clip.
- **Audio-reactive rendering** — 3-band biquad analyzer on the UI-thread-selected source (wiring gated, so unused tracks cost zero CPU), envelope-smoothed on the UI side. A lock-free 1024-sample master tap drives an FFT on `iChannel0` every frame (row 0 = spectrum, row 1 = waveform — Shadertoy-compatible).
- **Transient detection** — Baseline-tracking envelope detector on the low band with 80 ms refractory. Drives `iKick` as a decaying impulse (~120 ms tail) for kick-synced flash effects.
- **Text rendering on `iChannel1`** — Right-click a visual clip → Set Text. Rendered into a 2048×64 R8 alpha texture via `stb_truetype` (JetBrainsMono). Shaders get `iTextWidth` for wrap-correct scrolling. Bundled examples: marquee, kick-pulse, RGB-glitch.
- **Master post-FX chain** — Ordered list applied after compositor, same ping-pong pattern. Bundled effects: Bloom (thresholded blur), Pixelate, Kaleidoscope, Chromatic Split (audio-reactive), Vignette, Invert. Each has `@range` params exposed as knobs at the bottom of the Visual Params panel. Chain + values persist.
- **Video clip import** — Drop `.mp4/.mov/.mkv/.webm/.avi/.m4v` onto a visual track (or right-click → Set Video…). Background `ffmpeg` transcodes to 640×360 all-intra H.264 at 30 fps with aspect-preserving black padding, extracts audio to WAV, generates a thumbnail. Inline progress bar with %. Hash-keyed cache so re-imports are instant.
- **Audio sibling track** — If the source video had audio, a matching audio track is appended and the WAV loaded at the same scene row. Scene-launch fires image + audio in sync.
- **Video playback modes** — Free-running at native 30 fps, or bar-synced (1/2/4/8/16 bars — the full video stretches to fit exactly that many bars of transport time). Rate knob (0.25× / 0.5× / 1× / 2× / 4×). Trim to sub-range (First/Last half, Middle, quarters).
- **Session-grid thumbnails** — 160×90 JPEG extracted during import, lazy-loaded by SessionPanel into a GL texture cache, drawn behind the clip content.
- **Live video input** — Right-click → **Live Input ▸** for a submenu of discovered capture devices (Linux: globs `/dev/video*` with sysfs names), plus a Custom URL… fallback that accepts any libav URL (`v4l2:///dev/video0`, `rtsp://…`, `http://…`, `dshow://` on Windows, `avfoundation://` on macOS). Dedicated decode thread with drop-frames-on-overrun. Status pip on the clip cell: grey / yellow / green / red. Auto-reconnect with exponential backoff (cap 30 s) after drops; bad URLs fail after three 1/2/4-second attempts so typos surface quickly.
- **3D model clips (glTF 2.0)** — Right-click → **Set Model…** to load a `.glb` / `.gltf`. Models render into the layer's `iChannel2` via a Lambert + ambient pipeline with a dedicated 640×360 FBO + depth buffer; every existing shader that samples `iChannel2` works on 3D output with no changes. Auto-normalises model size to ~90 % of the frame regardless of the asset's authored units. Control via `modelPosX/Y/Z`, `modelRotX/Y/Z`, `modelSpinX/Y/Z` (deg/sec), `modelScale` — all standard `@range` uniforms, so A–H knobs, LFOs, and automation all work on them. **Skeletal animation** supported for standard glTF rigs (TRS channels, Step/Linear interpolation, up to 128 joints, 4-bones-per-vertex skinning) — drop a rigged + animated Fox and it walks. Bundled: `assets/examples/3d/Duck.glb`, `Fox.glb` (CC-BY 4.0, Khronos sample assets).
- **Lua scene scripts** — Opt-in per-clip script drives multi-instance rendering. Define `function tick(ctx)` returning a list of `{position, rotation, scale}` transforms; engine draws the clip's primary model once per entry into a shared depth buffer. Read-only context: `ctx.time`, `ctx.beat`, `ctx.audio.{level,low,mid,high,kick}`, `ctx.knobs.A..H`. Sandboxed stdlib (`math`, `table`, `string`, `utf8`). Hot-reload on `mtime` change. Bundled: `kick_ring.lua` (eight-copy ring breathing on the kick).
- **Arrangement-timeline visual clips** — Visual clips join audio/MIDI as first-class duration blocks on the arrangement. Right-click a session-grid clip → **Send to Arrangement** to place it at the playhead. Resize / move / delete like any other arrangement clip. On playback, crossing a clip fires the same launch path as a session click; leaving into a gap clears the layer.
- **Timeline scrubbing** — Drag the arrangement playhead and visuals seek with it. Arrangement-launched layers run on a transport-driven clock (`iTime = transportBeats − clipStartBeat` converted via current BPM), so shaders, 3D animations, and video frames all follow the scrub — forward or backward — pause-previews included. Session launches keep their wall-clock `iTime` so the existing session-performance feel is unchanged.
- **A–H knob + shader-param automation** — Per-track arrangement lanes and per-clip envelopes for visual parameters. Dropdown picks either one of the eight generic knobs or any `@range` uniform the clip's shader declares. Envelope editor in the browser panel's Clip tab; breakpoints loop with `clip.lengthBeats` (editable via the Clip Length submenu: 1/2/4/8/16/32 bars). Precedence: arrangement lane overrides clip envelope — LFO still composes on top. Audio-thread automation engine already dispatched visual-knob targets through a lock-free bus; new `TargetType::VisualParam` round-trips a uniform name for shader-param lanes.
- **Follow actions for visual session clips** — The same per-slot follow-action data audio/MIDI clips use (Stop / PlayAgain / Next / Previous / First / Last / Random / Any with barCount + chanceA probability split) now fires for visual clips too. Session-view only; main-thread polling.
- **Per-track stop gesture** — Clicking an active visual clip stops it (mirrors audio/MIDI). Transport stop clears every visual layer in lockstep with audio's `scheduleStop` so "Stop" means Stop everywhere.
- **Bundled shader pack** — 25 original MIT-licensed shaders (`assets/shaders/examples/`) covering plasma, palette sweeps, flow noise, concentric rings, spectrum/waveform visualisers, spirals, chequerboards, voronoi, tunnels, fractal circles, kaleidoscopes, aurora bands, radial EQ bars, chromatic aberration, beat strobes, kick flashes, text-overlay variants, and an audio-reactive 3D example (`25_model_audio_glow.frag`) — all using the `@range` convention. Plus `model_passthrough.frag` with the full model-transform uniform set as the default for model-only clips.
- **Project portability** — Transcoded media lives in `<project>.yawn/media/`, shaders in `<project>.yawn/shaders/`, models in `<project>.yawn/models/`, scene scripts in `<project>.yawn/scripts/`. Moving the project folder carries everything with it.

> See [visual.md](visual.md) for the full shader-authoring guide, uniform reference, video / live / 3D / Lua / automation details, and file layout.

## Quality
- **Test-Driven Development** — 1,360+ unit & integration tests across 159+ test suites via Google Test. The count goes up and down — we deleted ~80 v1-framework tests when their fw2 counterparts superseded them. The AI counts down too sometimes
- **Zero audio-thread allocations** — All memory preallocated at startup
- **All instruments handle CC 123** (All Notes Off) for clean MIDI effect removal
- **Compile-time guards** — A handful of "this code is unconditionally broken" warnings (always-recursive function, missing return, uninitialised local, etc.) are promoted to errors so they can't lurk in the build output the way `fileNameFromPath` did before it stack-overflowed during a file drop
- **Runtime guards** — Capture-stomp guard in `fw2::Widget::captureMouse` warns + asserts when an ancestor overwrites a descendant's capture; the recurring "dial doesn't turn" trap can't regress silently
- **Sloptronic-grade stability** — Filters clamped, state variables leashed, resonance domesticated

## Bundled Content

*Devices that need third-party files to be useful (Conv Reverb wants IRs;
Neural Amp wants `.nam` captures) ship with usable starter sets so the
device works on first install. Attribution + license details live in
[NOTICES.md](../NOTICES.md); third-party files are kept verbatim.*

- **38 Voxengo reverb IRs** — `assets/reverbs/voxengo/` — concert halls, plates,
  rooms, ambient spaces. Royalty-free under Voxengo's free IR redistribution
  license. Convolution Reverb's file dialog opens here by default
- **4 NAM amp captures** — `assets/nam/` — Clean / Crunch / High-gain / Bass
  amp captures from `pelennor2170/NAM_models` (GPL v3) with the original
  capturers' names preserved in filenames. Neural Amp's file dialog opens here
  by default
- **25 visual shaders** — `assets/shaders/examples/` — original MIT-licensed
  Shadertoy-style shaders covering plasma, palette sweeps, audio-reactive
  spectrum bars, kaleidoscopes, etc.
- **2 glTF 2.0 sample models** — `assets/examples/3d/Duck.glb`, `Fox.glb` —
  CC-BY 4.0, from the Khronos sample-asset repository
- **Demucs v4 stem model** — bundled in official release downloads (`models/`
  beside the binary); not in the source tree. Source builds download it on
  demand. See [building.md](building.md)

## Planned

- 🎛️ More controller scripts (Novation Launchpad, Akai APC, M-Audio whatever's-on-eBay-this-week)
- 🖥️ Move OLED display — pending reverse-engineering of Ableton's proprietary USB pairing protocol (or until someone lifts the protocol and we feel ethically OK about it)
- 🪪 Lua bindings for Undo/Mute/Copy and the remaining Move buttons that currently just no-op
- 🪟 MIDI clock send/receive (Link covers most cases but some hardware still wants the old protocol)
- 🎬 **Retrospective recording** (capture what was just played even though Record wasn't armed — the "I should've recorded that" workflow Live has had since 11). Requires a separate per-track always-on **ring buffer** that's always rolling, independent of arm state — sized for ~30 s of audio per track, sample-rate-aware, allocated once at engine init. When the user fires the "save what just happened" gesture, the engine memcpys the last N seconds (or as much as has been captured since the last transport stop) into a fresh `audio::Clip`, lands it in the targeted slot, and offers the same auto-launch path the standard record finalize uses. Current linear `ars.buffer` (post-arm, 5-min cap) stays as-is for normal recording — they're orthogonal paths. Visual / MIDI ring versions are a follow-up
- 🌍 Localisation (English / Greek / Russian / Chinese baseline)
- 🐛 Whatever bugs the PM discovers by wiggling knobs at 3 AM
