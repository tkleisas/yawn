# Y.A.W.N Visual Engine

YAWN includes a full Shadertoy-compatible visual engine for live VJ work:
per-track GPU layers, blend-mode compositing, a master post-processing
chain, audio-reactive uniforms, bar-synced video clip playback, and
on-screen text rasterisation. This document covers the architecture,
shader-authoring conventions, video workflow, and known limitations.

## Contents

- [Architecture overview](#architecture-overview)
- [Visual tracks & the session grid](#visual-tracks--the-session-grid)
- [Shader authoring](#shader-authoring)
  - [Shadertoy-compatible uniforms](#shadertoy-compatible-uniforms)
  - [YAWN-specific uniforms](#yawn-specific-uniforms)
  - [The 8 generic knobs (A–H)](#the-8-generic-knobs-ah)
  - [Custom parameters with `@range`](#custom-parameters-with-range)
- [Compositing & blend modes](#compositing--blend-modes)
- [Audio reactivity](#audio-reactivity)
- [LFO modulation & MIDI learn](#lfo-modulation--midi-learn)
- [Post-FX chain](#post-fx-chain)
- [Text on `iChannel1`](#text-on-ichannel1)
- [Video clips](#video-clips)
  - [Import pipeline](#import-pipeline)
  - [Playback modes](#playback-modes)
  - [File layout & project portability](#file-layout--project-portability)
- [Live video input](#live-video-input)
- [3D model clips](#3d-model-clips)
  - [Loading and localization](#loading-and-localization)
  - [Transform uniform convention](#transform-uniform-convention)
  - [Lua scene scripts](#lua-scene-scripts)
- [Output window & fullscreen](#output-window--fullscreen)
- [Bundled shader pack](#bundled-shader-pack)
- [Limitations](#limitations)

---

## Architecture overview

```
┌─────────────────────────────────────────────────────────┐
│  VisualEngine  (UI thread, separate GL context)         │
│                                                         │
│   ┌──────────┐   ┌──────────┐   ┌──────────┐            │
│   │ Layer 0  │   │ Layer 1  │   │ Layer N  │   per track│
│   │ (shader) │   │ (shader) │…  │ (shader) │   640×360  │
│   │   FBO    │   │   FBO    │   │   FBO    │   FBO each │
│   └────┬─────┘   └────┬─────┘   └────┬─────┘            │
│        └───────┬──────┴────────┬─────┘                  │
│                ▼                                         │
│     Ping-pong compositor   (blend mode + opacity,       │
│     accumulator FBOs × 2    Normal / Add / Multiply /   │
│                             Screen; transparent alpha)  │
│                ▼                                         │
│     Master post-FX chain   (ordered list, same          │
│     ping-pong FBOs reused   ping-pong pattern)          │
│                ▼                                         │
│                      Blit to output window              │
└─────────────────────────────────────────────────────────┘

           audio thread  ┌─── band-gated biquads (per source)
                         │    (audio-reactive uniforms)
                         └─── 1024-sample master tap → UI
                              (FFT for iChannel0)
```

Everything GL-related runs on the UI thread; the audio thread only writes
to lock-free data structures (per-channel peaks, band-analyzer peaks, a
circular master-sample tap). The output window gets its own SDL3 GL
context sharing resources with the main UI context, so textures and
shaders are visible across both.

## Visual tracks & the session grid

A visual track is a third `Track::Type` alongside Audio and MIDI. Its
clips hold a shader path, an optional video path, an optional scrolling
text string, custom parameter values, LFO states, blend mode, and audio
source. Clips launch with the same gesture as audio/MIDI clips; scene
launch fires every clip on a row together.

Each visual track owns one **layer** in the compositor — a 640×360 FBO
and a shader program. Track volume on the mixer becomes layer opacity
(`clamp(volume, 0, 1)`). Track index determines compositor order:
lower-index tracks are drawn first (bottom of the stack). Track blend
mode (right-click track header → Blend Mode) picks how this layer
combines with the accumulator below.

## Shader authoring

User shaders declare a single function:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // ...
}
```

YAWN supplies the `#version`, `main()` wrapper, uniform declarations,
and reserves line numbers so your `#line 1` aligns with the first
non-comment line of your source. Hot reload watches the file's mtime and
recompiles on save; if the new source fails to compile, the old program
keeps rendering.

### Shadertoy-compatible uniforms

| Uniform | Type | Notes |
|---|---|---|
| `iResolution` | `vec3` | (width, height, 1) — always (640, 360, 1) internal |
| `iTime` | `float` | Wall-clock seconds since shader was loaded. Always advances, so snippets animate even with transport stopped. |
| `iTimeDelta` | `float` | Seconds since previous frame |
| `iFrame` | `int` | Frame counter since shader load |
| `iMouse` | `vec4` | Reserved; currently 0 |
| `iDate` | `vec4` | (year, month-1, day, seconds-since-midnight) |
| `iSampleRate` | `float` | Audio sample rate (44100) |
| `iChannel0` | `sampler2D` | 512×2 R8 — row 0 FFT, row 1 waveform. Swizzled so `.r/.g/.b` all return the stored value. Master output. |
| `iChannel1` | `sampler2D` | Text strip (R8). Empty-string layers render blank. |
| `iChannel2` | `sampler2D` | Video frame (RGBA8, 640×360) if a video is attached, else dummy black. |
| `iChannel3` | `sampler2D` | Dummy black (reserved for future). |
| `iChannelResolution[4]` | `vec3[4]` | Per-channel texture size |
| `iChannelTime[4]` | `float[4]` | Per-channel time (currently all = `iTime`) |

### YAWN-specific uniforms

| Uniform | Type | Notes |
|---|---|---|
| `iBeat` | `float` | Transport position in beats |
| `iTransportTime` | `float` | Transport position in seconds |
| `iTransportPlaying` | `float` | 1.0 if playing, 0.0 if stopped |
| `iAudioLevel` | `float` | 0..1 envelope-smoothed peak of the layer's audio source |
| `iAudioLow` / `iAudioMid` / `iAudioHigh` | `float` | 0..1 per-band envelope (LP 200 Hz, BP 800 Hz, HP 2 kHz) |
| `iKick` | `float` | 0..1 decaying impulse on low-band onsets (~120 ms) |
| `iTextWidth` | `float` | Rendered pixel width of the text strip in `iChannel1` |
| `iTextTexWidth` | `float` | Always 2048 — the text texture's full width |

### The 8 generic knobs (A–H)

Always-available playable controls, mapped one-to-one to hardware
encoder banks (Push/Move/APC-style). Any shader that declares the
corresponding uniform can use them:

```glsl
uniform float knobA, knobB, knobC, knobD, knobE, knobF, knobG, knobH;
```

Each is 0..1, shown as a labelled knob row in the Visual Params panel
when the track is selected. Right-click a knob for LFO modulation
(shape / rate / depth) or MIDI Learn.

### Custom parameters with `@range`

Any `uniform float NAME;` you declare in your shader gets exposed as a
per-clip knob in the Visual Params panel. Default range is 0..1, default
value 0. To customise:

```glsl
uniform float speed;     // @range 0..4 default=1.0
uniform float warp;      // @range 0..2 default=0.4
uniform float phase;     // @range -3.14..3.14 default=0.0
```

The parser accepts the annotation on the same line, tolerates
whitespace, and silently falls back to 0..1/0.0 if the comment is
missing or malformed.

Values persist per clip (not per track), so launching different clips on
the same track with the same shader swaps between saved knob states.

## Compositing & blend modes

The compositor uses **ping-pong accumulator FBOs** and a single
fragment shader with four blend modes selected by uniform:

- **Normal** — `mix(dst, src, α)` (over)
- **Add** — `mix(dst, dst + src, α)`
- **Multiply** — `mix(dst, dst * src, α)`
- **Screen** — `mix(dst, 1 - (1-dst)(1-src), α)`

`α = trackVolume × sourceAlpha`. Shaders that output `vec4(col, 1.0)`
(the vast majority) blend normally; shaders that output partial alpha —
for example, the bundled text shaders write
`vec4(col * coverage, coverage)` — let layers below show through where
they have no content.

Right-click a **visual track header** → Blend Mode → Normal / Add /
Multiply / Screen. Persists per track.

## Audio reactivity

Three tiers of audio information are wired through to shaders:

1. **Per-source envelope**: `iAudioLevel`, `iAudioLow/Mid/High`, `iKick`.
   Source selected per clip via the right-click **Audio Source** menu
   (Master or any audio/midi track). The mixer only runs the 3-band
   biquad analyzer on channels that are actually wired to a visible
   layer — so 64 unused tracks cost nothing.

2. **Master FFT** on `iChannel0`: a 512-bin magnitude row + 512-sample
   waveform row, updated every frame from a 1024-sample circular tap on
   the master bus. Windowed radix-2 Cooley–Tukey on the UI thread.

3. **Kick detector**: baseline-tracking envelope on the low band with
   an 80 ms refractory window. Drives `iKick` as a decaying impulse
   with ~120 ms visible tail.

The audio thread writes plain `float` peaks; the UI thread reads and
envelope-smooths. The torn-read risk is identical to YAWN's existing
metering path and harmless at 60 Hz.

## LFO modulation & MIDI learn

Each of the 8 A–H knobs has an **optional LFO**:

- Right-click a knob → pick Shape (Sine / Triangle / Saw / Square /
  Sample-and-Hold), Rate (1/16 / 1/8 / 1/4 / 1/2 / 1 / 2 / 4 bars),
  Depth (10–100%).
- LFOs are beat-synced when transport plays, wall-clock when stopped.
- The knob arc "breathes" to reflect the modulated value while the text
  readout stays at the user's base value.

**MIDI Learn** on the same knobs:

- Right-click → MIDI Learn… → turn a CC on your controller. Done.
- Routing is audio-thread-safe: MidiEngine writes into a lock-free
  `VisualKnobBus` per-slot atomic; the UI thread drains each frame and
  applies to the live layer + persists to the clip.
- Mapping label shows in the menu; "Remove MIDI Mapping" unbinds.

## Parameter automation

Visual clips participate in the project-wide automation model with two
scopes:

- **Per-clip envelopes** — edited in the Browser panel's **Clip** tab
  under the Follow Actions section. A dropdown picks either one of the
  eight generic knobs (A..H) or any `@range` uniform the clip's shader
  declares. Breakpoints are clip-local beats; they loop with
  `clip.lengthBeats` (set via right-click → **Clip Length**:
  1/2/4/8/16/32 bars).
- **Per-track arrangement lanes** — absolute timeline beats, shown as
  expandable automation rows under each visual track on the arrangement
  view. Create via right-click a visual track header → **Add Knob
  Lane**. `autoMode = Read` is auto-enabled on first lane add so the
  curve plays immediately.

Evaluation order per frame: clip envelope → track lane → LFO. The
arrangement lane always wins when both the clip envelope and the track
lane target the same knob (matches the "final cut" layering used in
typical DAWs). LFOs compose on top of whichever source was chosen.

Main-thread polling, 60 Hz, no audio-thread involvement for visual
targets — visuals don't need the jitter budget and it keeps the path
simple. Shader-param automation uses `TargetType::VisualParam` with the
uniform name stored on the target; it round-trips through the project
file.

## Post-FX chain

A global ordered chain of effects runs on the composited output before
the final blit. Effects are standalone fragment shaders that sample
`iPrev` (the previous stage) and write the next accumulator, using the
same ping-pong FBOs.

Bundled effects (`assets/shaders/post/`):

- **Bloom** — thresholded 5×5 box blur added over the source (`threshold`, `intensity`, `radius`)
- **Pixelate** — grid-snapped sampling (`cellSize`)
- **Kaleidoscope** — N-fold radial mirror (`segments`, `rotate`)
- **Chromatic Split** — radial RGB offset, audio-reactive (`shift`, `reactive`)
- **Vignette** — smoothstep edge darkening (`strength`, `softness`)
- **Invert** — `1 - color` with amount (`amount`)

Manage via **View menu** → Post FX: Add … / Remove Last / Clear All.
Params appear as knobs in the bottom section of the Visual Params panel
(when a visual track is selected) with an × remove button per effect.
Chain + param values persist in the project JSON.

Post-FX effects receive the same Shadertoy-style uniforms as layer
shaders (minus the knob row), and pick up master audio automatically
when the chain is non-empty.

## Text on `iChannel1`

Each visual clip has an optional `text` string. YAWN rasterises it with
`stb_truetype` (using the bundled JetBrainsMono font) at ~45 px height
into a 2048×64 `GL_R8` texture, bound to `iChannel1`. The texture is
swizzled so `texture(iChannel1, uv).r` returns the glyph coverage.

`iTextWidth` is the rendered pixel width — use it for wrap-correct
scrolling:

```glsl
float pxX = fragCoord.x + iTime * speed * iTextWidth * 0.5;
float px  = mod(pxX, iTextWidth);
float u   = px / iTextTexWidth;   // 2048
```

Right-click a visual clip → **Set Text…** to edit. Empty text = blank
texture; if the shader outputs alpha from the texture, the layer is
fully transparent and layers below show through unchanged.

Bundled examples: `21_text_marquee.frag`, `22_text_kick_pulse.frag`,
`23_text_glitch.frag`, `24_text_debug.frag`.

## Video clips

Any visual clip can additionally reference a video file. If the clip has
no custom shader, YAWN automatically loads `video_passthrough.frag`
(full-frame sample of `iChannel2`). Custom shaders can sample
`iChannel2` themselves for any kind of video manipulation (kaleidoscope,
chromakey, colourise, glitch, etc.).

### Import pipeline

Right-click visual clip → **Set Video…** or drag a video file onto a
visual-track slot. YAWN runs `ffmpeg` in a background thread:

1. **Transcode** the source to an all-intraframe H.264 MP4, 640×360 @
   30 fps, yuv420p. Aspect is preserved with black padding (pillarbox
   for 4:3, letterbox for vertical phone video, exact fill for 16:9).
2. **Extract audio** (if present) to stereo PCM16 WAV 44.1 kHz.
3. **Generate thumbnail** (160×90 JPEG of frame 0).

Transcoded files land in `<project>/media/<hash>.mp4` + `<hash>.wav` +
`<hash>_thumb.jpg`, where `<hash>` is an FNV-1a of `sourcePath +
fileSize`. Re-importing the same source is instant (cache hit).

The clip slot shows an **importing… N%** overlay with a progress bar
while ffmpeg runs — `ffprobe` grabs source duration up-front and
ffmpeg's `-progress pipe:1` stream drives the percentage.

If the source has an audio stream, YAWN **auto-creates a sibling audio
track** named `"<stem> audio"` and loads the extracted WAV into the
same scene row, so scene-launch fires image + audio in lockstep.

Every visual clip with a video also stores `videoSourcePath` so
**Re-import Video** (right-click menu) can invalidate the cache and
retranscode without re-prompting the user.

### Playback modes

Right-click a clip with a video:

- **Video Loop** — Free (native 30 fps, wrap at EOF) / 1 / 2 / 4 / 8 /
  16 bars. In bar-sync mode, the full video is stretched to fit exactly
  N bars of transport time — the end of the video always lands on a bar
  boundary regardless of source duration.
- **Video Rate** — 0.25× / 0.5× / 1× / 2× / 4×. Works in both modes; in
  bar-sync mode a rate >1 plays the video through faster, looping
  multiple times within the N-bar span.
- **Video Trim** — Full / First half / Last half / Middle / First
  quarter / Last quarter. Restricts playback to that 0..1 fraction of
  the source.

All settings persist per clip.

### File layout & project portability

```
<project>.yawn/
├── project.json          ← tracks, clips, automation, mappings…
├── samples/              ← audio sample library references
└── media/                ← imported video assets
    ├── 3a1b…f7.mp4
    ├── 3a1b…f7.wav
    └── 3a1b…f7_thumb.jpg
```

Moving or sharing the `.yawn` folder brings the video with it.

## Live video input

The same `iChannel2` slot that hosts imported video can also pull a live
stream. Right-click any visual clip → **Live Input** to open a submenu
of discovered capture devices (Linux: `/dev/video*` with friendly names
from sysfs) plus a **Custom URL…** entry. Any libav-readable URL works:

```
v4l2:///dev/video0        # Linux webcam
rtsp://cam.local/stream   # IP camera
http://example.com/a.ts   # MPEG-TS HTTP stream
```

Live input has its own decode thread with drop-frames-on-overrun
semantics. A small status pip on the clip cell in the session grid
shows connection state: grey (stopped), pulsing yellow (connecting),
green (connected), red (failed). YAWN auto-reconnects with exponential
backoff (cap 30 s) after a live drop; bad URLs fail after three 1-, 2-,
4-second attempts so typos surface quickly.

Requires `libavdevice-dev` at build time for device URLs; network URLs
work with base FFmpeg alone.

## 3D model clips

`iChannel2` is also the landing pad for rendered 3D. A clip with a glTF
2.0 model assigned (`.glb` or `.gltf`) gets a dedicated M3DRenderer
that draws into the layer's 640×360 RGBA8 + depth FBO with a Lambert +
ambient lit material; that colour attachment is then bound as
`iChannel2` and the layer's shader post-processes it exactly like a
video frame. Every existing `iChannel2`-sampling shader works on
models without changes.

Right-click a visual clip → **Set Model…** → pick a `.glb` (or
`.gltf`). If no shader is assigned, YAWN falls back to
`assets/shaders/model_passthrough.frag`, which blits `iChannel2`
straight out and declares the transform uniforms (see below) so the
custom-knob row of the Visual Params panel immediately gives you
position / rotation / spin / scale controls.

### Loading and localization

Models go through the same project-local copy pattern as shaders: the
first time you pick a `.glb` outside the project, YAWN copies it into
`<project>/models/<stem>.glb` and stores the project-relative path,
so projects are portable. `.gltf` files reference external `.bin` and
texture files by relative path — copying just the `.gltf` would break
them — so `.gltf` inputs stay at the absolute path the user picked
(log a warning and use `.glb` for portable projects).

Models are mutually exclusive with file video and live video at the
engine level: assigning a model to a layer tears down any existing
video decoder and `LiveVideoSource`. A bundled Duck
(`assets/examples/3d/Duck.glb`, CC-BY 4.0 via Khronos sample assets)
is shipped for smoke-testing.

The renderer normalises every loaded model to a common on-screen
scale: vertices are recentered to the origin and uniformly scaled so
the largest single-axis half-extent fits ~90 % of the camera's
visible half-height. Camera sits at `(0, 0, 2.5)` looking at the
origin with a 45° vertical FOV. The net effect: whether the glTF was
authored in millimetres, metres, or arbitrary units, the model fills
the frame at `modelScale = 1.0` without hand-tuning.

### Transform uniform convention

The engine looks up **named** `@range` uniforms in the shader each
frame and feeds their values to the M3DRenderer's transform. Any
shader that declares any subset of these names gets the
corresponding control:

| Uniform name                     | Meaning                          | Units      |
|----------------------------------|----------------------------------|------------|
| `modelPosX` / `modelPosY` / `modelPosZ` | Translation added on top of the auto-center | world units |
| `modelRotX` / `modelRotY` / `modelRotZ` | Static Euler rotation (applied Z·Y·X) | degrees |
| `modelSpinX` / `modelSpinY` / `modelSpinZ` | Continuous auto-rotation speed, integrated by the engine each frame | degrees / second |
| `modelScale`                     | Uniform scale multiplier on top of the auto-fit | dimensionless (1 = full auto-fit) |

Because they're plain `@range` uniforms, A–H knobs, LFOs, MIDI learn,
and clip automation all work on them with zero extra wiring. Map a
saw-shaped LFO to `modelRotY` for a clean transport-synced spin, or
point knob G at `modelScale` and let an envelope drive a breathing
pulse on the kick.

`assets/shaders/examples/25_model_audio_glow.frag` is a worked
example: same transform controls plus `iKick`-driven bloom and
low/mid/high band tinting. Drop it on any model clip and the object
pulses with the music out of the box.

### Lua scene scripts

For scenes that need more than one model instance or event-driven
logic (spawn on kick, procedural arrangement, parametric choreography),
a clip can carry an optional **Lua scene script**. Right-click a
visual clip with a model assigned → **Set Scene Script…** → pick a
`.lua` file. YAWN localizes it into `<project>/scripts/<stem>.lua` on
load, same pattern as shaders and models.

The contract is one global function:

```lua
function tick(ctx)
    -- ctx.time         (float, wall-clock seconds)
    -- ctx.beat         (float, transport beat position)
    -- ctx.playing      (bool)
    -- ctx.audio.level  (float, 0..1 smoothed)
    -- ctx.audio.low    (float)
    -- ctx.audio.mid    (float)
    -- ctx.audio.high   (float)
    -- ctx.audio.kick   (float, peak-triggered, decays)
    -- ctx.knobs.A .. ctx.knobs.H  (floats, 0..1)
    --
    -- Returns a list of transforms — the engine draws the clip's
    -- primary model once per entry, all accumulating into the same
    -- depth buffer.
    return {
        { position = {x, y, z},
          rotation = {x, y, z},    -- euler XYZ degrees
          scale    = s },
        -- … more instances …
    }
end
```

Missing table fields default to `position={0,0,0}, rotation={0,0,0},
scale=1`. Returning `nil` or an empty table skips the draw this frame —
handy for gating on `ctx.audio.kick > 0.5`.

The Lua sandbox exposes the standard `math`, `table`, `string`, and
`utf8` libraries. `io`, `os`, `package`, and `debug` are intentionally
absent. Runtime errors are logged, don't crash the app, and don't
disable the script — edit and save to fix, YAWN polls `mtime` and
hot-reloads automatically.

`assets/examples/scripts/`:

- `static_demo.lua` — minimal template: one instance, slow Y-spin.
- `kick_ring.lua`   — ring of eight copies around the origin, whole
  ring rotating, each copy scaled by the kick envelope.

Because the script's per-instance transforms sit in the same shader
pipeline, every existing iChannel2 post-processing shader (including
`25_model_audio_glow.frag`) works on top of a Lua-driven scene.

## Output window & fullscreen

A secondary SDL3 window hosts the compositor output. It's hidden on
startup and toggled via **View → Visual Output Window** (or left open
across sessions).

Fullscreen:

- **F11** from anywhere → toggle fullscreen on the output window
  (shows it first if hidden).
- **View → Visual Output Fullscreen (F11)** menu item does the same.
- **Esc** exits fullscreen (also closes open menus / quits if neither
  applies).

Internal rendering is fixed at 640×360 and blitted with linear filtering
to whatever size the output window happens to be — drag the window to a
4K monitor and the image stays crisp up to typical VJ viewing
distances.

## Bundled shader pack

`assets/shaders/examples/` — 25 original MIT-licensed shaders covering
plasma, palette sweeps, flow noise, rings, spectrum/waveform visualisers,
spirals, chequerboards, voronoi, tunnels, fractal circles, triangular
grids, FBM clouds, kaleidoscopes, pulse grids, auroras, radial EQ bars,
RGB-split, beat strobes, kick flashes, the text shaders (marquee, kick
pulse, glitch, debug), and the audio-reactive 3D example
(`25_model_audio_glow.frag`). All use the `@range` annotation
convention so they play nicely with the knob UI out of the box.

`assets/shaders/default.frag` — the startup demo; uses A–E knobs as
palette speed, hue offset, audio-splash strength, kick-flash strength,
and spectrum bar height.

`assets/shaders/model_passthrough.frag` — minimal shader used as the
fallback when a clip has a model but no custom shader: just blits
`iChannel2` and declares the full `model*` transform-uniform set so
knob mapping works immediately.

`assets/examples/3d/Duck.glb` — bundled Khronos sample model (CC-BY 4.0)
for smoke-testing the static 3D pipeline.

`assets/examples/3d/Fox.glb` — rigged and animated (CC-BY 4.0, Khronos
sample asset). Ships three clips (Survey / Walk / Run); the renderer
auto-plays clip 0 on wall-clock time, so dropping Fox.glb on a visual
clip immediately shows the skeletal-animation path end to end.

## Limitations

- **Audio-reactive post-FX** use the master bus only (by design — they
  apply to the final composite). Layer-level audio reactivity is
  per-source.
- **Video is always scaled to 640×360** on import — no choice of
  resolution or codec. Rationale: single, fast, portable format; all
  media files are the same shape; seeks are frame-exact (every frame is
  a keyframe).
- **Import requires the `ffmpeg` binary in `$PATH`**, in addition to
  the `libav*` headers/libs used by the runtime decoder.
- **No drag-drop of shader files** yet — use the right-click menu.
- **No in-app shader editor** — edit `.frag` files with your favourite
  editor; hot reload picks up changes on save.
- **Scrub bar** (click-drag playhead on a video clip) isn't implemented;
  the Video Trim submenu picks fixed sub-ranges instead.
- **Cross-fade between post-FX chains** — not a feature. Chain edits
  apply immediately.
- **Visual tracks appear in the mixer** (needed for the volume→opacity
  fader) but their I/O region is blank; audio sends don't apply.
- **Skeletal animation** is supported for standard glTF rigs: TRS
  channels (translation / rotation / scale) with Step or Linear
  interpolation, up to 128 joints per skin, 4-bones-per-vertex
  skinning. **Cubic-spline interpolation** falls back to linear, and
  **morph-target weights** are parsed but not yet applied (both
  deferred). Per-clip UI to pick *which* animation to play and/or
  sync it to transport is also deferred — the renderer currently
  auto-plays animation clip 0 on wall-clock time.
- **3D lighting is a minimal Lambert + ambient** — no PBR, no shadows,
  no IBL. Suitable for VJ silhouettes and stylised renders, not
  product-viz quality.
- **Lua scene scripts only clone the clip's primary model** — loading
  additional `.glb` files from Lua (e.g. `load_model("cube.glb")`) is
  a planned follow-up. Per-instance material overrides also deferred.
