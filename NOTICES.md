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

### Community NAM Captures — `assets/nam/`

A small starter set of four NAM captures, sourced from the
community-curated [pelennor2170/NAM_models](https://github.com/pelennor2170/NAM_models)
collection. The whole collection is published under **GNU GPL v3**
(full license text in `assets/nam/COPYING`); upstream README
preserved at `assets/nam/upstream-README.md`.

- **Source:** https://github.com/pelennor2170/NAM_models
- **License:** GNU GPL v3 (`assets/nam/COPYING`). Per the
  upstream README: *"These files are licenced under the GNU
  GPL v3, please see the file COPYING for more details."*
- **Provenance:** Captures contributed by members of the
  Neural Amp Modeler Facebook community
  (https://www.facebook.com/groups/5669559883092788) and
  aggregated by the repository maintainer.
- **Format:** NAM 0.5.0 WaveNet `.nam` files, loadable by
  [Steve Atkinson's Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler)
  and any compatible host (including YAWN).
- **Files copied unmodified** from upstream so original
  filenames preserve full per-capturer attribution.

#### Bundled captures (one per category)

| Category    | Capturer  | Amp / Source                            | File |
|-------------|-----------|-----------------------------------------|------|
| Clean       | Tim R     | Fender Twin Reverb (Norm channel, Bright switch) | `Tim R/Tim R Fender TwinVerb Norm Bright.nam` |
| Crunch      | Sascha S  | Friedman Dirty Shirley Mini (crunch, gain 6)     | `Sascha S/Sascha S DirtyShirleyMini_crunch_G6.nam` |
| High-gain   | Helga B   | Peavey 6505+ Red channel (no boost)              | `Helga B/Helga B 6505+ Red Ch - NoBoost rock.nam` |
| Bass        | Jason Z   | Tech21 dUg DP3X bass preamp pedal (all dimed)    | `Jason Z/Jason Z Tech21 dUg DP3X bass preamp pedal all dimed no shift.nam` |

**Acknowledgments:** captures by Tim R, Sascha S, Helga B, and
Jason Z (handles as published in the upstream collection).
Filename prefixes are preserved so each capture remains
attributable to its author. Steve Atkinson is the original
author of NAM itself.

These show up in YAWN's Neural Amp device's **Load .nam…**
file dialog, which opens directly at the bundled folder so
first-time users can pick a starter amp without any setup.
Users can also load any other `.nam` file from anywhere on
their machine — community libraries are at
[tone3000.com](https://www.tone3000.com/) and
[tonehunt.org](https://tonehunt.org/) (per-model licensing
varies; verify before redistributing).

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
