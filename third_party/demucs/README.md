# demucs (vendored)

Four-stem separation (drums / bass / other / vocals) for YAWN, built on
**Demucs v4 (hybrid transformer)**.

## Provenance & licenses

- **C++ inference:** adapted from [sevagh/demucs.onnx](https://github.com/sevagh/demucs.onnx)
  — MIT (see `LICENSE`). We use its ONNX inference (`model_inference.cpp`),
  STFT/ISTFT + reflect padding (`dsp.cpp`), and the segment/overlap-add
  apply loop (`model_apply.cpp`).
- **Model:** Meta's [Demucs v4](https://github.com/facebookresearch/demucs)
  `htdemucs` (4-source), exported to a single self-contained ONNX
  (`htdemucs_4s.onnx`, ~170 MB) via demucs.onnx's converter. MIT.
  **Not stored here** — downloaded on demand from the YAWN GitHub release
  `models-v1` into `~/.yawn/models/`.

Both licenses are compatible with YAWN's MIT license.

## What was adapted

- ORT include switched to the prebuilt-package layout (`<onnxruntime_cxx_api.h>`).
- `ProgressCallback` now returns `bool` — the segment loop bails out early
  (empty tensor) on `false`, which YAWN wires to the Esc/Cancel button.
- `demucs_api.{h,cpp}` — **new**: a plain-struct facade (`demucs::separate`,
  `demucs::Stems`) that hides Eigen + ONNX Runtime from the rest of YAWN.
  The libnyquist-based CLI driver (`src_cli`) is **not** vendored — YAWN
  feeds/receives audio via its own `AudioBuffer`.

## Build

Compiled into the `demucs` static lib (C++17) only when
`-DYAWN_HAS_STEM_SEPARATION=ON`, sharing the prebuilt ONNX Runtime + Eigen
with Basic Pitch. The YAWN side lives in `src/transcribe/StemSeparation.{h,cpp}`.
Input/output are 44.1 kHz stereo; YAWN resamples to/from the project rate.
