<p align="center">
  <img src="yawn_ico512x512.png" alt="Y.A.W.N Logo" width="128" />
</p>

<h1 align="center">Y.A.W.N</h1>
<h3 align="center">Yet Another Audio Workstation New</h3>

<p align="center">
  A cross-platform digital audio workstation inspired by Ableton Live.<br/>
  Session View · Clip Launching · Multi-track Audio · VST3 (planned)
</p>

---

## Features

- **Session View** — Ableton-style clip grid with 8 tracks × 8 scenes
- **Real-time Audio Engine** — Lock-free audio thread with PortAudio (ASIO/WASAPI/ALSA)
- **Clip Playback** — Drag-and-drop audio files (WAV, FLAC, OGG, AIFF), looping, gain, fade-in/out
- **Quantized Launching** — Launch clips on beat or bar boundaries
- **Transport** — Play/stop, BPM control, beat-synced position tracking
- **Custom 2D Renderer** — Batched OpenGL 3.3 rendering with font atlas (stb_truetype)
- **Multi-window Ready** — Built on SDL3 for future detachable panels
- **Test-Driven Development** — 85 unit tests via Google Test

### Planned

- 🎛️ Mixer & audio routing
- 🎹 Arrangement View (timeline)
- 🔌 VST3 plugin hosting
- 💾 Project save/load

## Screenshots

*Coming soon — the Session View shows a dark Ableton-style grid with transport bar, track headers, scene labels, and waveform thumbnails.*

## Tech Stack

| Component | Technology |
|---|---|
| Language | C++17 |
| UI / Windowing | SDL3 + OpenGL 3.3 |
| Audio I/O | PortAudio |
| Audio Files | libsndfile |
| Font Rendering | stb_truetype |
| Build System | CMake 3.20+ |
| Testing | Google Test 1.14 |
| Platforms | Windows, Linux |

All dependencies are fetched automatically via CMake FetchContent — no manual installs needed.

## Building

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

| Key | Action |
|---|---|
| `Space` | Play / Stop |
| `T` | Toggle test tone (440 Hz) |
| `Up` / `Down` | BPM +/- 1 |
| `Home` | Reset position to 0 |
| `Q` | Cycle quantize: None → Beat → Bar |
| `Esc` | Quit |
| **Mouse click** | Launch/stop clips in the session grid |
| **Drag & drop** | Load audio files into clip slots |

## Architecture

```
┌─────────────────────────────────────────────────┐
│              UI Layer (SDL3 + OpenGL)            │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ Session  │ │  Renderer│ │  Font / Theme    │ │
│  │  View    │ │   2D     │ │                  │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
├─────────────────────────────────────────────────┤
│              Application Core                    │
│  ┌─────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ Project │ │Transport │ │  Message Queue   │ │
│  │ Model   │ │          │ │  (lock-free)     │ │
│  └─────────┘ └──────────┘ └──────────────────┘ │
├─────────────────────────────────────────────────┤
│              Audio Engine (real-time thread)     │
│  ┌─────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │PortAudio│ │  Clip    │ │  Ring Buffer     │ │
│  │Callback │ │  Engine  │ │  (SPSC)          │ │
│  └─────────┘ └──────────┘ └──────────────────┘ │
└─────────────────────────────────────────────────┘
```

**Thread model:** UI thread (SDL main loop) + Audio thread (PortAudio callback). Communication is entirely via lock-free SPSC ring buffers — no mutexes or allocations on the audio thread.

## Project Structure

```
yawn/
├── CMakeLists.txt              # Main build configuration
├── cmake/
│   └── Dependencies.cmake      # FetchContent (SDL3, glad, PortAudio, libsndfile, stb, gtest)
├── src/
│   ├── main.cpp                # Entry point
│   ├── app/
│   │   ├── App.h/cpp           # Application lifecycle, event loop
│   │   └── Project.h           # Track/Scene/Clip grid model
│   ├── audio/
│   │   ├── AudioBuffer.h       # Non-interleaved multi-channel buffer
│   │   ├── AudioEngine.h/cpp   # PortAudio lifecycle and callback
│   │   ├── Clip.h              # Clip data model and play state
│   │   ├── ClipEngine.h/cpp    # Multi-track quantized playback
│   │   └── Transport.h         # Play/stop, BPM, position (atomics)
│   ├── ui/
│   │   ├── Font.h/cpp          # stb_truetype font atlas
│   │   ├── Renderer.h/cpp      # Batched 2D OpenGL renderer
│   │   ├── SessionView.h/cpp   # Clip grid, transport bar, waveforms
│   │   ├── Theme.h             # Ableton-dark color scheme
│   │   └── Window.h/cpp        # SDL3 + OpenGL window wrapper
│   └── util/
│       ├── FileIO.h/cpp        # Audio file loading (libsndfile)
│       ├── MessageQueue.h      # Typed command/event variants
│       └── RingBuffer.h        # Lock-free SPSC ring buffer
├── tests/
│   ├── CMakeLists.txt          # Test build configuration
│   ├── test_AudioBuffer.cpp
│   ├── test_Clip.cpp
│   ├── test_ClipEngine.cpp
│   ├── test_FileIO.cpp
│   ├── test_MessageQueue.cpp
│   ├── test_Project.cpp
│   ├── test_RingBuffer.cpp
│   └── test_Transport.cpp
└── assets/                     # Runtime assets (copied to build dir)
```

## License

TBD
