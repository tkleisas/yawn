# Y.A.W.N — Yet Another Audio Workstation New

A cross-platform digital audio workstation inspired by Ableton Live, featuring a Session View with clip launching, arrangement timeline, mixer, and VST3 plugin support.

## Tech Stack

- **C++17** with **SDL3 + OpenGL 3.3** for multi-window UI
- **PortAudio** for low-latency audio I/O
- **CMake** build system
- Cross-platform: **Windows** and **Linux**

## Building

### Prerequisites

- CMake 3.20+
- C++17 compiler (MSVC 2019+, GCC 8+, Clang 8+)
- Git (for FetchContent dependency downloads)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Run

```bash
./build/bin/YAWN        # Linux
build\bin\Release\YAWN.exe  # Windows
```

## License

TBD
