# Changelog

*Each release was implemented by saying "do this" and then saying "no, not like that" between 2 and 47 times. Versions are set automatically by the git tag.*

## Recent releases

| Version | Summary |
|---|---|
| **v0.77.2** | Fix a **heap use-after-free** that corrupted the heap and hung the app ("corrupted double linked list") — opening a context-menu submenu (e.g. a clip's Video Rate) wrote through a `Level&` reference invalidated by a `std::vector` reallocation; found with AddressSanitizer. Also: the video scaler now uses the clip's native source size (out-of-bounds reads on non-640×360 video), plus live-monitoring latency logging and a warning when input/output are different audio devices (independent clocks → latency drift) |
| **v0.77.1** | Default to **48 kHz** — a 44.1 kHz engine default forced an OS-level 44.1↔48 resample on modern systems (PipeWire/CoreAudio graphs and most USB interfaces run at 48 k, as do NAM captures), adding live-monitoring latency + CPU that drifted upward under load (heard as NAM latency growing over time / per added instance). The engine still re-queries the device's actual rate on open, so 44.1 k-only devices adapt. Plus FTZ/DAZ denormal flushing on the audio thread (standard hygiene) |
| **v0.77.0** | Neural Amp Modeler **A2** support — pinned `NeuralAmpModelerCore` to v0.5.3 (locks in the A2 architecture; A1 captures still load and run unchanged, A2-shaped WaveNet models route to the optimised `A2FastModel` via `NAM_ENABLE_A2_FAST`). Added a **"Lite"** toggle to the Neural Amp device that drives `nam::SlimmableModel::SetSlimmableSize` (0 = lite … 1 = full) on slimmable A2 models for a substantial CPU saving; greyed out / inert on non-slimmable (all A1) captures |
| **v0.76.4** | Visual clips honour per-column launch — launching a visual clip now updates the session grid's play-state and clears the previously-active clip in that column (the play-icon path had its own stale copy that no-op'd visual stop). Added a **Stop All Clips** button (session corner) that stops every audio/MIDI/visual clip and wipes each track's launch memory so the next Play starts nothing. Plus a ModelLibrary test-flake fix (per-case temp dir) |
| **v0.76.3** | Glitch / malfunction visual pack — datamosh / dropout / bitcrush post-FX, a `26_glitch_testcard` generative scene, and `glitch_stutter` + `note_glitch_ghosts` scene scripts (kick / beat / note-reactive) |
| **v0.76.2** | Post-FX effects preserve layer alpha (pass-through) — HSV / invert / vignette / etc. chained onto a clip no longer blank the lower tracks; rotate / scale leave out-of-bounds regions transparent |
| **v0.76.1** | Piano-roll default note-velocity control — toolbar Vel control with follow-last + persistence; new-note default raised from ~50% to 100 |
| **v0.76.0** | 3D scene overhaul — multi-model scenes, per-instance attributes + camera, MIDI-reactive scenes, PBR-lite materials & lighting, GPU instancing, animation UI, a model-library browser with live thumbnails, and bundled example scenes |
| **v0.75.1** | Fix: session clips now start in phase with the transport on Play — pressing Play launched the default clips on their NextBar quantize grid (deferred behind a stale boundary check), so the audio came in late and trailed the playhead by a constant offset; now they start immediately at the downbeat. Also: README slimmed and split into `docs/` (this CHANGELOG added) |
| **v0.75.0** | Self-contained releases — the Demucs model + ONNX Runtime are now bundled in the release package, so stem separation works offline out of the box (the repo stays lean; the model is fetched at CI build time, never committed) |
| **v0.74.1** | Stem-separation polish — live progress bar, cancellable download, windowed-sinc resampling, undo on the created tracks |
| **v0.74.0** | Demucs v4 four-stem separation (drums / bass / other / vocals) via ONNX Runtime |
| **v0.73.0** | Windows support for Basic Pitch audio-to-MIDI |
| **v0.72.0** | Polyphonic audio-to-MIDI via Spotify Basic Pitch (ONNX Runtime) |
| **v0.71.0** | Remove dead MIDI menu items; move virtual-keyboard velocity to a transport-bar selector |
| **v0.70.0** | Device preset-name display, scrollable dialogs, marquee over-wide labels; retire `V1MenuBridge` (all menus now native `fw2::MenuEntry`) |
| **v0.69.0** | MIDI LFO named target picker + visual-layer (device / shader) modulation |
| **v0.68.1** | Stop an audio-thread use-after-free when editing / swapping a playing MIDI clip (clone-swap via graveyard) |
| **v0.68.0** | Factory melodic loop library derived from the drum kit |
| **v0.67.1** | Apply the saved BPM on project load |
| **v0.67.0** | MIDI loop browser, save / drag-to-slot, factory drum library |
| **v0.66.1** | Heap-back the live-MIDI buffer to stop an AudioEngine stack overflow |
| **v0.66.0** | Procedural preset generator + scrollable menus |
| **v0.65.0** | Live MIDI record visualization, clip indicators, scene triangles |

For the full per-feature detail, see [docs/features.md](docs/features.md).

## Implementation phases

The early development was organised into numbered phases. Each was
implemented, broken, and fixed in roughly that order.

| Phase | Status | Description |
|---|---|---|
| 1. Project Scaffolding | ✅ Done | CMake build system, SDL3+OpenGL window, directory structure |
| 2. Audio Engine | ✅ Done | PortAudio callback, transport, lock-free ring buffers |
| 3. Clip Playback | ✅ Done | libsndfile loading, quantized clip launching, looping |
| 4. Session View UI | ✅ Done | Clip grid, transport bar, waveform thumbnails, theme |
| 5. Mixer & Routing | ✅ Done | 64-track mixer, 8 send/return buses, master, metering |
| 6. MIDI Engine | ✅ Done | MIDI 2.0-res internals, RtMidi I/O, MPE zones, MIDI clips |
| 7. Metronome | ✅ Done | Synthesized click track, beat-synced, configurable |
| 8. Audio Effects | ✅ Done | Built-in effects + visualizers, effect chains, drag-to-reorder, 3-point insert, sidechain + modulation routing on the `AudioEffect` base |
| 9. Integrated Instruments | ✅ Done | 15 instruments with full UI (SubSynth, FM, Sampler, Karplus-Strong, Wavetable, Granular, Vocoder, String Machine, Drawbar Organ, Electric Piano, Multisampler, InstrumentRack, DrumRack, DrumSlop, DrumSynth) |
| 10. MIDI Effects | ✅ Done | 8 MIDI effects (Arp, Chord, Scale, NoteLength, Velocity, Random, Pitch, LFO) |
| 11. Interactive UI | ✅ Done | Widget system, menu bar, mixer controls, detail panel, virtual keyboard, context menus |
| 12. UI Framework | ✅ Done | Widget tree, FlexBox layout, primitive widgets, dialog system, panel migration |
| 13. Piano Roll | ✅ Done | MIDI note editor with draw/select/erase tools, zoom/scroll, clip integration |
| 14. Composite Widgets | ✅ Done | DeviceWidget, DeviceHeader, FwGrid, VisualizerWidget, SnapScrollContainer, neon knobs |
| 15. Animations & DPI | ✅ Done | Hover animations, panel collapse/expand animations, DPI auto-detection & scaling |
| 16. Arrangement View | ✅ Done | Timeline, clip placement, automation lanes, loop range, waveform display |
| 17. Recording & I/O | ✅ Done | Audio/MIDI recording, MIDI Learn, audio export (WAV/FLAC/OGG), project save/load |
| 18. Session Management | ✅ Done | Scene insert/duplicate/delete, track deletion, follow actions, undo/redo, time stretching |
| 19. VST3 Hosting | ✅ Done | VST3 SDK, plugin scanning, process-isolated editors (Windows HWND + Linux X11 embed with IRunLoop), parameter sync, state persistence |
| 20. Controller Scripting | ✅ Done | Lua 5.4, controller auto-detection, `yawn.*` API, Ableton Push 1 (encoders, display, pads, LEDs) |
| 21. More Controllers | ✅ Done | Ableton Move (32-pad scale grid, ripple LEDs, touch encoders, toast as screen-substitute), Korg nanoKONTROL2 (banked faders/knobs, LED-synced channel buttons, transport row), Yamaha Reface DX (touch-strip → instrument param, CC 7/11 → master/track vol) |
| 22. Visual / VJ Engine | ✅ Done | Per-track GPU layers, Shadertoy-compatible shader hot-reload, video import + live input, glTF 2.0 3D models with skeletal animation, Lua scene scripts, master post-FX chain, A–H knobs + LFOs + automation, arrangement timeline integration |
| 23. Ableton Link | ✅ Done | LAN beat/tempo sync (peers from Live, Logic, Bitwig, iOS apps, etc.) with phase alignment. Local UI tempo edits gated through `localTempoChanged` so the audio thread doesn't clobber typed BPM with the previous-frame's stale session tempo |
| 24. UI Framework Migration | ✅ Done | Three-phase delete-heavy refactor: v1 Widget/FlexBox/EventSystem/UIContext + a 766-line bridge wrapper layer (`PanelWrappers.h`) all retired. Single `fw2::Widget` framework, single `dispatchMouseDown`, single global capture slot. Net ~−2960 lines. Capture-stomp guard added. C4717-and-friends promoted to compile errors |
| 25. Effects Batch II + DrumSynth + Bundled IRs/Models | ✅ Done | Five new effects (Ping-Pong Delay, Spline EQ, Bitcrusher, Noise Gate, Envelope Follower), a Convolution Reverb with full FFT block-convolver + 38 bundled Voxengo IRs, a Neural Amp Modeler integration with 4 bundled `.nam` captures, a fully-synthesised DrumSynth + Piano-Roll DrumRoll mode, and `AudioEffect`-base sidechain + modulation + extra-state plumbing (incl. project-load IR rehydration). RCU-lite atomic engine swap on Conv Reverb + NAM. NAM linked with `$<LINK_LIBRARY:WHOLE_ARCHIVE,nam>` after MSVC silently DCE'd `nam::get_dsp` |
| 26. Period-piece instruments + standalone Phaser/Wah/Rotary | ✅ Done | String Machine (Solina-style ensemble strings), Drawbar Organ (Hammond B-3, auto-pairs with Rotary), and three standalone effects pulled out of inline-synth scope: Phaser, Wah, Rotary |
| 27. Cross-panel drag-drop + audio-clip ops + clip UAF fix | ✅ Done | Drag audio clips from session/arrangement cells onto Sampler / Granular / DrumSlop / Vocoder / DrumRack pads / Multisampler. Crop + Reverse buttons (atomic buffer swap). Project-owned clip graveyard with 5 s TTL covers every slot mutation so the reproducible "delete clip → audio thread reads freed memory" UAF can't recur |
| 28. DrumRack feature parity | ✅ Done | Per-pad audio fx chain (lazy-allocated), per-pad AR envelope, per-pad region trim with reverse playback, choke groups (1–4), kit-preset save/load (one file = one kit) |
| 29. InstrumentRack feature parity | ✅ Done | Default chain on construction, per-chain instrument widget + "Change Instrument →" swap, per-chain effect chain |
| 30. Electric Piano + final instrument round-out | ✅ Done | 2-op FM Electric Piano (Rhodes / Wurli / Suitcase), velocity-driven mod index, per-strike hammer-noise transient, exponential decay |
| 31. Session-record orchestration + stuck-state cure (v0.62) | ✅ Done | Transport Record orchestrates per-track session-record; change-detected clip-state emission cures the "track stays red/green forever" stuck state; `/SUBSYSTEM:WINDOWS` so no stray console window |
| 32. Automation arming + clear scopes + SplineEQ persistence (v0.63) | ✅ Done | Global automation-record arm, clear-automation scopes (all undoable), arrangement loop-marker drag fixes, SplineEQ persistence (per-node-keyed extra-state) |
| 33. Auto-arm UX completion (v0.64) | ✅ Done | Scene-label click runs the per-track record orchestration; per-track MIDI Overdub toggle; per-clip Auto-Rec disable; visual A–H knob touch records breakpoints |

Development past v0.64 is tracked in **Recent releases** above.

## Lessons learned

1. **"It compiles" ≠ "It works"** — But it's a great start when your engineer has no ears
2. **Filter resonance is the QA department** — Crank it up, sweep fast, watch things explode
3. **The AI will always say "Fixed!"** — Statistically, it's right 60% of the time, every time
4. **Lock-free programming is easy** — If you let someone who can't experience race conditions write it
5. **Test counts go both ways** — Because when your codebase is written by autocomplete on steroids, trust but verify. Sometimes you delete tests too. Counting goes up and down
6. **The best bug reports are just vibes** — "After a while the arpeggiator produces notes without me pressing any key" → *chef's kiss*. "When clicking on a second midi track with the synth there is empty space in the detail" → also *chef's kiss*
7. **Track deletion requires stopping the world** — Ableton does it too, so it's a feature not a limitation
8. **MIDI Learn is just "wiggle something, click something"** — The AI understood this perfectly on the 4th attempt
9. **SysEx is where bytes go to hide** — The Push 1 display didn't work for hours because of one missing column offset byte. The PM dug up his own 10-year-old code to prove the AI wrong
10. **Controllers have multiple MIDI ports** — Push 1 sends pads on the "User" port, not the main one. The AI opened the wrong port and wondered why pads were silent
11. **Two UI frameworks side-by-side is one too many** — Two capture slots, two visibility flags, two event types, and a 766-line bridge layer to make them talk. Deleted in three commits. Should have been one. The migration plan went `Phase 1: rename namespace → Phase 2: collapse dispatch → Phase 3: delete the corpse`. We kept Phase 3 a surprise from ourselves
12. **The compiler is trying to tell you something** — `warning C4717: 'fileNameFromPath' is recursive on all control paths, function will cause runtime stack overflow` was in the build output for an unknown number of versions before it actually crashed. It's a compile error now
13. **Member widgets are not children** — `m_scroll` as a value member doesn't get its `invalidate()` to bubble up to the panel. We learned this when a knob's neighbour stopped rendering on the second click. Fix is a one-line `invalidate()` after mutation; the lesson is "lifecycle ownership ≠ tree membership"
14. **Ableton Link's audio-thread API is a foot-gun** — The user types BPM, you set the transport, next audio buffer reads back the OLD session tempo and overwrites your new one. Add a `localTempoChanged` flag. Write the regression test. Walk away
15. **fw2 has a single capture slot** — Two widgets calling `captureMouse()` is one widget overwriting the other's capture. The AI hit this trap so many times that the framework now `assert`s + logs when an ancestor stomps a descendant's capture. The PM appreciates the framework that yells at you when you're about to break it
16. **Audio-thread + UI-thread sharing a `unique_ptr` is a use-after-free waiting for its third opportunity** — Neural Amp's third `.nam` load wrote 0x0 because the audio thread was mid-`process()` on the DSP the UI thread was destroying. Fix: `std::atomic<DSP*>` + retired-list + deferred destruction on the NEXT load (RCU-lite). We then immediately wrote the same trick into Convolution Reverb because the bug was 1 user-event away from happening there too
17. **MSVC's function-level dead-code elimination is a silent linker** — `nam::get_dsp` was in `nam.lib`, the obj file referenced it, the link succeeded, the device showed "idle" with the model path stored. `dumpbin /symbols` on the EXE: not a single `nam::` symbol survived. Fix: `$<LINK_LIBRARY:WHOLE_ARCHIVE,nam>` (CMake 3.24+). Cost is binary size; benefit is the device working
18. **Per-file `CXX_STANDARD 20` collides with a C++17 PCH on a clean build** — and doesn't on incremental ones (the stale `.obj` from before the PCH existed satisfies the link). The first portable `WHOLE_ARCHIVE` attempt passed the AI's local builds and failed CI. PIMPL the C++20 dependency behind a C++17-clean header and the conflict goes away
19. **The 100 MB file is always somewhere** — A git submodule, LFS, a split-in-repo, a release asset — they all move the same ~170 MB Demucs model to a different home with different trade-offs. We bundle it into the release package: the repo stays lean, the download stays self-contained, and the loader prefers the bundled copy over the on-demand cache

*This is what software development looks like in 2026. One human with opinions and one AI with infinite patience. The future is sloppy, it ships, the warnings are errors, the dials turn, and honestly? It kinda slaps.*
