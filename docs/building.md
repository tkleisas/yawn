# Building & Releases

> **Fun fact:** This project has been rebuilt approximately 2,114 times.
> The AI broke the build 478 of those times. The PM broke it 0 times because the PM doesn't touch C++.
> The remaining 1,636 rebuilds were "just to be sure" — including 38 rebuilds during the v1→fw2 migration where the AI confidently insisted "this should be a clean delete" right before introducing 23 link errors.

## Just want to run it?

Grab a [release](https://github.com/tkleisas/yawn/releases) — the Windows
`.zip` and Linux `.tar.gz` are **self-contained**: the binary, the ONNX
Runtime, the Demucs stem-separation model, and all bundled assets are in
the archive. Unpack and run; no first-launch download, no extra installs
(other than `ffmpeg` on `PATH` if you want video import). Everything below
is for building from source.

## Prerequisites

- **CMake 3.24+** — needed for the `$<LINK_LIBRARY:WHOLE_ARCHIVE,nam>` generator expression that keeps Neural Amp's static lib alive through linker dead-code elimination. Pre-3.24 builds work if you turn `YAWN_HAS_NAM=OFF`
- **C++17 compiler** — MSVC 2019+ (Windows), GCC 8+ or Clang 8+ (Linux). The Neural Amp Modeler dependency itself needs C++20 but is kept behind a PIMPL split so YAWN's main targets stay on C++17
- **Python 3 + jinja2** — required by glad2 (OpenGL loader generator)
- **Git** — for FetchContent dependency downloads

```bash
# Install jinja2 if not already present
pip install jinja2
```

All other dependencies are fetched automatically via CMake FetchContent — no
manual installs needed. Lua 5.4 and SQLite3 are vendored as source
amalgamations.

### Linux system dev packages

SDL3, the VST3 editor host (X11 embedding), and the audio backends need:

```bash
sudo apt install \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev libxinerama-dev \
  libwayland-dev libdecor-0-dev \
  libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdrm-dev libgbm-dev \
  libdbus-1-dev libibus-1.0-dev libudev-dev \
  libasound2-dev libpulse-dev libjack-dev libsndio-dev
```

For **video clip import/playback** (optional — gated by `YAWN_HAS_VIDEO`):

```bash
sudo apt install \
  ffmpeg libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```

The `ffmpeg` binary is used at runtime for the transcode step; `libav*`
headers and libraries are linked for real-time video decoding. Without
them, the build still succeeds but the video menu items are hidden.

For **live video input** (optional — gated by `YAWN_HAS_AVDEVICE`,
adds webcam / `v4l2://` / `avfoundation://` / `dshow://` device URLs
on top of the network URLs that work with base FFmpeg):

```bash
sudo apt install libavdevice-dev
```

Network-only URLs (`rtsp://`, `http://`) work without this — the
guard just switches on the OS device demuxers.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run

```bash
# Windows
build\bin\Release\YAWN.exe

# Linux
./build/bin/YAWN
```

## Run Tests

```bash
# Windows
build\bin\Release\yawn_tests.exe

# Linux
./build/bin/yawn_tests

# Or via CTest
cd build && ctest --output-on-failure -C Release
```

## Optional feature flags

Most features are on by default. A few heavyweight or platform-specific ones
are gated behind CMake options:

| Option | Default | What it does |
|---|---|---|
| `YAWN_HAS_NAM` | ON | Neural Amp Modeler (`.nam`) inference. OFF → the Neural Amp device falls back to a gain-stage passthrough. Needs CMake 3.24+ for the WHOLE_ARCHIVE link |
| `YAWN_HAS_VIDEO` | auto | Video clip import/playback (needs `libav*`) |
| `YAWN_HAS_AVDEVICE` | auto | Live video device input (webcam / OS device URLs) |
| `YAWN_HAS_LINK` | ON | Ableton Link LAN sync. OFF compiles a no-op stub |
| `YAWN_VST3` | ON | VST3 plugin hosting |
| `YAWN_HAS_MODEL3D` | ON | glTF 2.0 3D model clips (tinygltf) |
| `YAWN_HAS_BASIC_PITCH` | OFF* | Audio→MIDI via Spotify Basic Pitch (ONNX Runtime). Linux x64 + Windows x64 |
| `YAWN_HAS_STEM_SEPARATION` | OFF* | Stem separation via Demucs v4 (ONNX Runtime). Linux x64 + Windows x64 |

\* **Off by default for source builds, but ON in official release builds.**
The two ONNX features share a prebuilt ONNX Runtime + Eigen, fetched via
FetchContent when either flag is on. macOS is not wired (no test machine)
and auto-disables.

```bash
# Build with the ONNX audio-analysis features, like the official releases:
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DYAWN_HAS_BASIC_PITCH=ON -DYAWN_HAS_STEM_SEPARATION=ON
cmake --build build --config Release
```

## The stem-separation model

Demucs v4 needs a ~170 MB ONNX model (`htdemucs_4s.onnx`). It is **not** in
the git repo (GitHub's 100 MB-per-file limit, and committing it would bloat
history forever). It lives as an asset on the GitHub release tag
[`models-v1`](https://github.com/tkleisas/yawn/releases/tag/models-v1).

YAWN resolves the model from two places, **bundled wins**:

1. **`models/htdemucs_4s.onnx` beside the binary** — shipped inside the
   official release archives, so stem separation works offline out of the
   box. (Resolved relative to the working directory, the same way `assets/`
   is.)
2. **`~/.yawn/models/htdemucs_4s.onnx`** — a download cache. When no bundled
   copy is present (typical for a source build), the first stem-separation
   run downloads the model here via `curl` (cancellable, size-verified).

Both are integrity-checked against the known size (170,681,491 bytes); a
truncated or wrong-size file is rejected.

Basic Pitch's model is much smaller (226 KB) and is **embedded** in the
binary as a byte array, so it never needs a download or a bundled file.

## How releases are built

Releases are cut by pushing a `v*` tag; GitHub Actions
(`.github/workflows/release.yml`) builds the Release config on Windows +
Linux with the ONNX features on, runs the test suite, and packages each
platform into a self-contained archive:

- The binary (`YAWN` / `YAWN.exe`) + bundled `assets/` and `scripts/`
- The ONNX Runtime shared lib beside the binary (Linux relies on `$ORIGIN`
  in the binary's rpath; Windows copies `onnxruntime.dll`)
- The Demucs model, fetched into `models/` during the build (size-verified)
- FFmpeg binaries (Windows) for video import

The workflow also has a `workflow_dispatch` trigger that builds and uploads
the same packaged artifacts for inspection **without** publishing a release
— handy for verifying packaging changes before tagging.
