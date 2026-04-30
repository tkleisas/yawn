# Third-Party Content Bundled with YAWN

This file documents creative content (audio samples, impulse
responses, neural amp models, presets, fonts) bundled with YAWN
under permissive licenses. Source code dependencies (libraries
fetched via CMake) are documented separately in their own
LICENSE files inside the build directory.

---

## Convolution Reverb Impulse Responses

### Voxengo Free IR Pack — `assets/reverbs/voxengo/`

Created with [Voxengo Impulse Modeler](https://www.voxengo.com/imodeler/).

- **Producer:** Aleksey Vaneev / Voxengo
- **Source:** https://www.voxengo.com/impulses/
- **License:** Custom — royalty-free for any use (including
  commercial), redistributable under the conditions in the
  bundled `assets/reverbs/voxengo/license.txt`. The full pack
  ships unaltered as required by the license.
- **Files:** 38 reverb IRs (rooms, halls, churches, drum spaces,
  cabinet impulses, special spaces — 16-bit / 44.1 kHz WAV).
- **Acknowledgment:** Aleksey Vaneev retains exclusive ownership
  of these impulse files.

These show up in YAWN's Convolution Reverb device under the
**Preset** dropdown. Users can also load any of their own
`.wav` IR files via the **Load IR…** button.

---

## Neural Amp Modeler (.nam) Models

### Bundled NAM models — `assets/nam/`

YAWN does not currently ship bundled NAM models (each is 1–15 MB
and most community profiles require attribution to the specific
capturer). Users can browse and download free models from:

- [tonehunt.org](https://tonehunt.org/)
- [tone3000.com](https://tone3000.com/)

Drop the `.nam` file via the **Load .nam…** button on the
Neural Amp device.

If/when bundled models land, each will be attributed here with
its capturer name + source URL + license confirmation.

---

## Source-Code Dependencies

Code dependencies are vendored by CMake's FetchContent and live
under `build/_deps/`. Each carries its own LICENSE file:

- **SDL3** — zlib license — https://github.com/libsdl-org/SDL
- **PortAudio** — MIT license — https://github.com/PortAudio/portaudio
- **RtMidi** — modified MIT — https://github.com/thestk/rtmidi
- **libsndfile** — LGPL 2.1 — https://github.com/libsndfile/libsndfile
- **FLAC / Ogg / Vorbis** — BSD-style — https://xiph.org/
- **nlohmann/json** — MIT — https://github.com/nlohmann/json
- **stb (single-file)** — MIT / Public Domain — https://github.com/nothings/stb
- **glad (OpenGL loader)** — MIT — https://github.com/Dav1dde/glad
- **Lua 5.4** — MIT — https://www.lua.org/ (vendored in `third_party/lua54/`)
- **SQLite3** — Public Domain — https://www.sqlite.org/ (vendored in `third_party/sqlite3/`)
- **minimp3** — CC0 — https://github.com/lieff/minimp3
- **Eigen** — MPL 2.0 — https://gitlab.com/libeigen/eigen
- **NeuralAmpModelerCore** — MIT — https://github.com/sdatkinson/NeuralAmpModelerCore
- **tinygltf** — MIT — https://github.com/syoyo/tinygltf
- **Ableton Link** — GPL 2 — https://github.com/Ableton/link
- **VST3 SDK** — GPL 3 / Steinberg dual — https://github.com/steinbergmedia/vst3sdk
