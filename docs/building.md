# Building & Releases

> **Fun fact:** This project has been rebuilt approximately 2,114 times.
> The AI broke the build 478 of those times. The PM broke it 0 times because the PM doesn't touch C++.
> The remaining 1,636 rebuilds were "just to be sure" — including 38 rebuilds during the v1→fw2 migration where the AI confidently insisted "this should be a clean delete" right before introducing 23 link errors.

## Just want to run it?

Grab a [release](https://github.com/tkleisas/yawn/releases). Windows `.zip` and
Linux builds are **self-contained**: the binary, the ONNX Runtime, the Demucs
stem-separation model, and all bundled assets are in the archive. Unpack and
run; no first-launch download.

**Linux has two artifacts** (see [Linux deployment options](#linux-deployment-options)
for the full comparison):

- **`YAWN-*-linux.tar.gz`** — plain tarball. Video decode/live capture load
  the host's FFmpeg 6 or 7 at runtime via `dlopen` (no hard library
  dependency — the app starts fine without FFmpeg, video features just
  report unavailable). Video *import* additionally needs the `ffmpeg`
  binary on `PATH`.
- **`YAWN-*-linux-x86_64.AppImage`** — single file, bundles the FFmpeg
  libraries, so video works even with no FFmpeg installed. Needs FUSE
  (`libfuse2`) and a glibc ≥ 2.39 distro.

Everything below is for building from source.

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
headers are used to compile the real-time video decoder. On **POSIX the
libraries are not linked** — `src/visual/FfmpegShim.cpp` `dlopen()`s them
at runtime by FFmpeg release family (7.x first, then 6.x), so the binary
starts on any distro regardless of which FFmpeg soname major (if any) is
installed. Video features degrade gracefully (startup toast + clear error
messages) when no compatible runtime is found. On **Windows** the bundled
FFmpeg DLLs are linked at build time as before.

> **ABI note (do not skip when touching video code):** the shim only
> probes FFmpeg 6 and 7, and only because the exact struct fields the
> code touches were verified layout-compatible across those two majors
> (n6.1.2 vs n7.1.1 headers, field by field). The three forbidden reads
> — `AVFormatContext::duration`, `AVCodecContext::pix_fmt`,
> `AVCodecContext::time_base` — are avoided by design: duration comes
> from `AVStream::duration` and scaler geometry from decoded `AVFrame`
> fields. FFmpeg 5 is excluded (AVStream layout changed in 6.0); FFmpeg 8
> is not probed (AVStream `side_data` removal shifts offsets). Before
> adding a new FFmpeg major to the family table in `FfmpegShim.cpp`,
> re-run that field-layout check.

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

## Linux deployment options

The release workflow publishes two Linux artifacts. Both are built on
`ubuntu-24.04` (pinned — the FFmpeg 6 sonames and the glibc 2.39 floor
come from that runner) and contain the same app, assets, ONNX Runtime,
and Demucs model.

| | tarball (`.tar.gz`) | AppImage |
|---|---|---|
| Runs on | any distro with glibc ≥ 2.39 | any distro with glibc ≥ 2.39 + FUSE (`libfuse2`) |
| Video decode / live capture | host FFmpeg 6 **or** 7 (runtime `dlopen`; absent → feature off, app still runs) | bundled FFmpeg 6 — always on |
| Video **import** (transcode) | needs host `ffmpeg` binary | needs host `ffmpeg` binary (bundling the CLI is a possible follow-up) |
| Footprint | smaller (~220 MB) | larger (+~50 MB of libav* closure) |
| Integrates with host audio | yes | yes — ALSA/JACK/PipeWire/Pulse libs are deliberately *not* bundled |

How the AppImage is assembled (all in `.github/workflows/release.yml`,
"Package AppImage (Linux)" step): the AppDir gets the binary + assets +
model under `usr/bin` (AppRun `cd`s there because assets resolve relative
to CWD), `libonnxruntime` and the five libav* libraries plus their `ldd`
closure under `usr/lib` (with a denylist for core runtime / GL drivers /
audio-server libs, which must be the host's), the existing
`packaging/linux/com.yawn.daw.desktop` + icon, and a small `AppRun` that
sets `LD_LIBRARY_PATH`/`PATH`. `appimagetool` (upstream "continuous"
build — there are no versioned tags to hash-pin) squashes it.

Known limits: FUSE is required to *run* an AppImage (`--appimage-extract`
works without it); glibc 2.39 means Ubuntu 22.04 / Debian 12 and older
can't run either artifact from the official pipeline — build from source
there instead.

## How releases are built

Releases are cut by pushing a `v*` tag; GitHub Actions
(`.github/workflows/release.yml`) builds the Release config on Windows +
Linux with the ONNX features on, runs the test suite, and packages each
platform into a self-contained archive (Linux additionally produces an
AppImage — see above):

- The binary (`YAWN` / `YAWN.exe`) + bundled `assets/` and `scripts/`
- The ONNX Runtime shared lib beside the binary (Linux relies on `$ORIGIN`
  in the binary's rpath; Windows copies `onnxruntime.dll`)
- The Demucs model, fetched into `models/` during the build (size-verified)
- FFmpeg binaries (Windows) for video import

The workflow also has a `workflow_dispatch` trigger that builds and uploads
the same packaged artifacts for inspection **without** publishing a release
— handy for verifying packaging changes before tagging.
