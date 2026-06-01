# basicpitch (vendored)

Polyphonic audio-to-MIDI for YAWN, built on **Spotify Basic Pitch**.

## Provenance & licenses

- **C++ port:** adapted from [sevagh/basicpitch.cpp](https://github.com/sevagh/basicpitch.cpp)
  — MIT (see `LICENSE`). We use its ONNX inference (`ort_inference.cpp`) and
  note-creation (`midi_notes.cpp`, the `output_to_notes_polyphonic` algorithm).
- **Model:** `model.onnx` from [spotify/basic-pitch](https://github.com/spotify/basic-pitch)
  (`icassp_2022/nmp.onnx`) — Apache-2.0. Embedded here as `basic_pitch_model.h`
  (226 KB, generated with `xxd -i`).

Both licenses are compatible with YAWN's MIT license.

## What was adapted

- `basicpitch.hpp` — constants + `InferenceResult`/`NoteEvent` structs. The original
  `convert_to_midi` (which wrote a MIDI **file** via libremidi) is replaced by
  `notes_from_inference()` + `frame_times()` so YAWN builds a `MidiClip` directly.
- `ort_inference.cpp` — loads the embedded ONNX model from memory (prebuilt ONNX
  Runtime) instead of the original baked `.ort` static-build header. Tensor I/O
  names and windowing are unchanged.
- `midi_notes.cpp` — kept the note-creation half; dropped the libremidi
  serialization (`note_events_to_midi` / `time_to_ticks` / `convert_to_midi`).
- `bp_api.{h,cpp}` — **new**: a plain-struct facade (`basic_pitch::transcribe`,
  `basic_pitch::Note`) that hides Eigen + ONNX Runtime + C++20 from the rest of
  YAWN (which stays C++17), mirroring the NeuralAmp PIMPL split.

## Build

Compiled into the `basicpitch` static lib (C++20) only when
`-DYAWN_HAS_BASIC_PITCH=ON`. Pulls a prebuilt ONNX Runtime (Linux x64) via
FetchContent. See `cmake/Dependencies.cmake` and the `basicpitch` target in
`CMakeLists.txt`. The YAWN side lives in `src/transcribe/AudioToMidi.{h,cpp}`.
