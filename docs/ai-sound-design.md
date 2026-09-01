# AI Sound Design — Integration Possibilities & Prototype Plan

*An AI-written DAW contemplating hosting an AI that judges sounds. The snake eats its tail; the tail is a low-pass filter.*

This document surveys how modern ML models (audio embeddings, generative audio, LLMs) could integrate into YAWN, ranks every possibility by reachability given the existing codebase, and gives a concrete plan for the most reachable tier.

**Status:** design doc — nothing here is implemented. Feature names like `YAWN_HAS_CLAP` are proposals.

---

## 1. The model landscape (what exists out there)

| Family | Examples | What it does | Relevance to YAWN |
|---|---|---|---|
| **Audio–text embedding** | [CLAP](https://arxiv.org/abs/2206.04769) (LAION), MS CLAP | Maps audio and text into one vector space; zero-shot classification/retrieval by cosine similarity | The scoring oracle for any "does this sound match that description/sample?" loop. **The key building block.** |
| **Generative text→audio** | AudioLDM/2, AudioGen, MusicGen, Stable Audio, MMAudio | Synthesizes waveforms from text | Produces *samples*, not editable patches. Useful as an import source, not for instrument programming. |
| **Audio classifiers** | AST, BEATs, PANNs, YAMNet | Fixed-label supervised classification | Subsumed by CLAP zero-shot for our purposes (no training, free-form labels). |
| **Audio LLMs** | Qwen2-Audio, SALMONN | General audio understanding / QA via an LLM | Overkill for scoring; interesting only as a blue-sky assistant. Heavy (GPU-hungry). |
| **Sound matching (audio→params)** | [AST-based sound matching](https://arxiv.org/pdf/2407.16643), perceptual sound matching | Estimate synth parameters to imitate a reference sound | Exactly our Option C below. Research-grade, but our "synthesizer" is YAWN itself. |
| **Text→params (LLM)** | [LLM DX7 parameter estimation](https://openreview.net/forum?id=pj3d2NnHDb) | Fine-tuned LLM outputs a full synth patch from a text description | The headline idea. Works on DX7 partly because of its structured 155-param space and a large synthetic training set. Doesn't transfer to arbitrary synths without that dataset. |
| **Magenta (Google)** | MusicVAE, DDSP, MT3 | ML-for-music research project (now mostly inactive) | **Not this direction** — generative/transcription models, no synth-programming capability. |

### The honest core problem

LLMs are poor at precise numeric regression. "Set cutoff to 2,340 Hz with Q 0.8" is not a thing they do reliably from knowledge alone. Every successful system therefore wraps the LLM in a **closed loop with a scorer**:

```
propose params → render → score against target (text and/or audio) → refine
```

The LLM contributes semantic knowledge ("a supersaw needs detuned oscillators"); the scorer (CLAP similarity or human ear) contributes precision. This document is mostly about how cheaply YAWN can build that loop.

---

## 2. What YAWN already has (and why this is cheaper than it looks)

The integration points all exist, verified in the codebase:

- **ONNX Runtime** — vendored via FetchContent (ORT 1.20.1, SHA-pinned), already used offline/CPU-only with worker threads, cancellation atomics, and progress callbacks (`src/transcribe/`). Two model-delivery precedents: small models embedded as byte arrays (Basic Pitch), large models file-based with on-demand `curl` download (Demucs, ~170 MB).
- **Feature gating** — every heavy ML dep is an off-by-default CMake option (`YAWN_HAS_BASIC_PITCH`, `YAWN_HAS_STEM_SEPARATION`) with runtime `available()` + stub fallback. A new `YAWN_HAS_CLAP` fits this shape exactly.
- **Self-describing parameters** — every instrument/effect exposes `parameterCount()` / `parameterInfo(i)` / `get/setParameter(i, v)` (`src/core/ParameterInfo.h`, `src/instruments/Instrument.h`, `src/effects/AudioEffect.h`), including names, ranges, categorical value labels, and formatting. Full JSON serialization of any device's params already exists (`src/presets/DevicePresetHelpers.h`).
- **Prior art for parameter search** — `PresetGenerator` already does randomized param generation through the render pipeline (`src/app/App.cpp` `startPresetGeneration()`), with the exact worker-thread + atomic-progress + per-frame-poll pattern a scoring loop would reuse.
- **Offline rendering** — `src/audio/OfflineRenderer.h` renders the full arrangement via the AudioEngine (used by WAV/video export). For per-device scoring we need a new, simpler primitive: call `Instrument::process(...)` directly with a test-note MidiBuffer — no transport, no engine.
- **Background-task pattern** — stem separation (`App::startStemSeparation`) is the canonical template: detached worker, `atomic<bool> active/done/cancel`, overlay progress bar, Esc-cancel, main-thread result application with an undo entry.
- **Library database** — `src/library/LibraryDatabase.h` (vendored sqlite3, versioned schema) already indexes audio files, presets, and MIDI loops; a tag/embedding column slots in.
- **Lua parameter introspection** — controller scripts can already enumerate/set every device parameter (`get_device_param_*` / `set_device_param`, `src/controllers/LuaEngine.cpp`) — enough to drive a "set params → render → score" loop from a script once rendering is exposed.

### Constraints

- C++17 globally; C++20 only per-target/per-TU (NAM-style PIMPL split). New deps must respect this.
- macOS is deliberately unwired for the ONNX features.
- No HTTP client code exists in-repo (only spawned `curl` for the Demucs model download, and the localhost-only `YAWN_CMD` TCP server). Any LLM call-out starts from zero.
- Audio-thread rules don't apply here (all inference is offline on worker threads), but the existing linear resampler is quality-limited — CLAP scoring wants ~16 kHz mono input, so this matters.

---

## 3. The possibility space, ranked by reachability

Effort is relative to the existing codebase (a solo dev + AI workflow). ⭐ marks the recommended starting tier.

### ⭐ A. CLAP-powered library auto-tagging (smallest, self-contained)

**What:** During library scan, classify each audio file / preset-render against a fixed vocabulary (~100–200 labels: "kick drum", "analog bass", "pad", "acoustic guitar", ...). Store tags in the existing sqlite `LibraryDatabase`. Browser gains tag filtering.

**Why it's the most reachable:**
- **No LLM, no network, no free-form text at runtime.** Precompute the text embeddings for the fixed vocabulary *once* (Python script, upstream CLAP), ship the ~100×512 float blob in assets. Runtime only needs the **audio encoder** in ONNX — no tokenizer needed at all.
- Reuses: ONNX infra, LibraryScanner worker pattern, sqlite schema (one migration), BrowserPanel.
- Model size: HTSAT-tiny audio encoder ≈ tens of MB — Basic-Pitch-style embedded or Demucs-style downloaded.
- Delivers immediate value independent of everything below (a better browser), and builds the exact CLAP runtime plumbing (preprocessing, embedding, similarity) that B and C need.

**Effort: small (days). Risk: low.** Worst case the tags are mediocre — still better than nothing, and it de-risks the ONNX CLAP port.

### ⭐ B. Sound matching: audio → patch ("make this instrument sound like that sample")

**What:** Drop a sample on an instrument track (context menu → "Match Sound"). A worker renders candidate parameter sets via direct `Instrument::process()` calls, scores them with CLAP(audio_candidate, audio_target), and iterates: seeded random search (PresetGenerator-style) → coordinate descent / CMA-ES on the continuous params. Best patch applied as an undoable preset.

**Why it's reachable:**
- No LLM, no network, no text encoder. Both embeddings are audio-side.
- The optimizer does the numeric precision the LLM can't.
- New code is mostly: a `SingleDeviceRenderer` (~100 lines around `Instrument::process` + a test-note MidiBuffer), the scoring loop, and the worker plumbing copied from `startStemSeparation`.

**Caveats:** CLAP similarity is perceptual, not parametric — expect "sounds similar" not "sounds identical". Categorical params (waveform selectors etc.) need discrete search or LLM seeding (that's tier C). Render the target and candidate through the same preprocessing (mono, ~16 kHz, same duration window).

**Effort: medium (about a week). Risk: medium** — quality is empirical; ship behind the same kind of availability gate and let users judge.

### ⭐ C. Text → patch via LLM-in-the-loop

**What:** Type "dark evolving pad with slow filter sweep" in a device panel. An LLM proposes parameter values (its synth knowledge transfers well at the semantic level), the app renders and CLAP-scores against the *text* embedding, and iterates. Optionally finishes with the tier-B optimizer.

**Why it's one notch harder than B:** free-form text at runtime needs the CLAP **text encoder** (RoBERTa) + a BPE tokenizer in C++, or an external embedding source. Options, in order of preference:

1. **Local small embedding model** — ship the text encoder ONNX + a minimal BPE tokenizer port. Fully offline; most work.
2. **LLM call-out computes both sides** — if we already require a network LLM for proposal generation, let it (or a small local endpoint) return the text embedding too. Zero extra C++ deps, but ties text→patch to being online.
3. **Fixed-description vocabulary** — like tier A, user picks from/extends curated descriptions. Ugly, do not start here.

The proposal channel itself has two shapes:
- **Structured**: LLM returns JSON in the existing `serializeDeviceParams` format (parameter names are self-describing), set atomically via undo-manager. Clean, no scripting.
- **Agentic**: LLM emits Lua driving the `yawn.*` param API (already complete). More flexible (can touch mixer/FX), but requires Lua-capable prompt scaffolding per device.

**Effort: medium-large (2–4 weeks including the embedding decision). Risk: medium-high** — quality depends on the LLM's synth knowledge and CLAP's text fidelity; manage expectations ("starting point", not "finished patch").

### D. Generative text→audio import (AudioLDM-class)

**What:** "Generate kick drum…" → TTA model renders a sample → imported onto a clip / into the Sampler. Essentially a fancy sample generator.

**Assessment:** Technically a straightforward ORT integration (tier-A/B plumbing), but the models are large (latent diffusion, GB-scale, GPU-friendly), quality for music production is hit-and-miss, and commercial licensing of several models is murky. Also philosophically the *least* interesting option for a DAW with 15 real instruments: it produces dead audio, not patches.

**Effort: medium. Risk: high (deps, licensing, GPU expectations). Recommendation: skip unless a concrete need appears.**

### E. Fine-tuned small LLM for text→params (the DX7 paper route)

**What:** Generate a synthetic dataset (render random patches across YAWN instruments → caption with an audio-captioning model → (description, params) pairs), LoRA-fine-tune a small open LLM, run it locally.

**Assessment:** The "real" version of tier C, but it's a research project: dataset generation pipeline, training infra, evaluation. Quality would likely still be matched by C's closed loop for far less work. Revisit only if C proves popular and its latency/online dependency becomes the complaint.

**Effort: large (weeks+). Risk: high. Recommendation: not now.**

### F. Audio LLM assistant (blue sky)

Voice/text QA over the project ("what's that clipping on track 3?", "make the chorus bigger") via a multimodal audio LLM. Fascinating, GPU-hungry, undefined scope. Keep on the horizon; tiers A–C teach us everything needed to evaluate it later.

---

## 4. Recommended path

**A → B → C, in that order, sharing one runtime.**

- A ships the CLAP audio encoder ONNX port, preprocessing, and similarity scoring. It is independently useful and proves the port.
- B adds `SingleDeviceRenderer` + the optimizer and proves the closed loop with zero external dependencies.
- C adds free-form text (text encoder / embedding strategy) and the LLM call-out. By then the risky parts are already built and tested.

Each tier is gated (`YAWN_HAS_CLAP`-style option + runtime availability), so none of it burdens users who don't opt in — same philosophy as Demucs/Basic Pitch today.

---

## 5. Prototype plan — tier A + the B scaffolding

### 5.1 CLAP runtime (`src/clap/` — new, gated on `YAWN_HAS_CLAP`)

1. **Model procurement:** pick a pinned ONNX export of a CLAP audio encoder (LAION HTSAT-tiny or MS CLAP; verify the export's license + quality on a few known sounds). Vendor small, SHA-pin large, following the two existing precedents. Ship the precomputed vocabulary text-embedding blob (`assets/clap/tags.bin`, generated once by `scripts/clap_gen_tags.py` — Python stays a dev-time tool, never a runtime dep).
2. **CMake:** `option(YAWN_HAS_CLAP OFF)` in `cmake/Dependencies.cmake`; reuse the ORT fetch (guard so all three ONNX features share one ORT download); `yawn_core` define + stub `clap::available()` when off — copy the `src/transcribe/` stub pattern.
3. **API sketch:**
   ```cpp
   namespace clap {
     bool available();
     // embed one mono float buffer (any rate; internal resample to model rate)
     bool embedAudio(const float* mono, int frames, int rate, std::vector<float>& out /* 512 */);
     // cosine similarity against precomputed tag table
     std::vector<TagScore> classify(const std::vector<float>& embedding, int topN);
   }
   ```
4. **Preprocessing:** mono downmix + resample. The repo's linear resampler is marginal — port the existing windowed-sinc `resample()` from `src/transcribe/StemSeparation.cpp:84` (currently file-local) into a shared util while doing this.

### 5.2 Auto-tagging (`src/library/`)

5. Schema v3 migration on `LibraryDatabase`: `tags TEXT` (JSON array of label+score) on audio files and presets. For presets: classify a short **rendered** preview (reuse the tier-B renderer once it exists; until then, skip preset tagging and do audio files only).
6. `LibraryScanner`: after `probeAudioFile`, enqueue embedding on the same worker; batch ORT sessions per worker thread. Progress/cancel already exist.
7. BrowserPanel: tag facet filter. Keep it simple — substring + clickable tags, no query language.

### 5.3 Sound-matching scaffold (tier B, same branch or next)

8. `SingleDeviceRenderer`: header-only-ish helper that takes an `Instrument*`, builds a test MidiBuffer (root note + a few seconds, maybe a short chromatic riff), renders via `Instrument::process` at 44.1k stereo, downmixes/resamples to the CLAP input format. ~150 lines. (Note: `src/audio/OfflineRenderer.h` renders the *whole arrangement through the AudioEngine* — too heavy for candidate scoring; the new helper sits *below* it.)
9. `MatchEngine` worker: seed N random param sets (respecting `defaultValue`, categorical via labels) → score → keep best → coordinate-descent passes over continuous params. Cancel/progress/apply copied verbatim from `startStemSeparation()` (`src/app/App.cpp:1260`), including the undo entry on apply.
10. UI: clip/track context menu item + a small results dialog ("best match: score 0.31 — Apply / Keep searching / Cancel"). Put it where "Separate Stems (Demucs)" lives (`src/app/App_TrackClipMenus.cpp:1576`) — same muscle memory.

### 5.4 LLM call-out design notes (tier C, when reached)

- New outbound HTTP util modeled on the spawned-`curl` precedent (no new linked deps), or the localhost `YAWN_CMD` TCP server pattern inverted (a local helper process owns the LLM). Prefer a user-configurable endpoint (OpenAI-compatible API + "custom command") so both cloud and local models (llama.cpp server, ollama) work.
- Prompt contract: system prompt carries the device's `ParameterInfo` table (names, ranges, labels — it's small, ~1–2 KB); assistant must reply with the `serializeDeviceParams` JSON shape. Validate every value against min/max before applying; wrap in undo.
- Scoring loop identical to tier B, target embedding from the text side. Iteration budget ~3–5 rounds, then hand off to the coordinate-descent finisher.

### 5.5 Testing

- Unit: embedding determinism, similarity sanity (identical buffers → ~1.0; kick vs pad → low), resample equivalence, optimizer monotonic improvement on a synthetic target (render patch P, match from random init, assert score rises).
- Integration: extend `scripts/ui_smoke.py` — the `YAWN_CMD` channel already supports queries; assert a matched patch lands with an undo entry.
- A tiny "golden ears" corpus in `assets/examples/clap_check/`: a handful of labeled renders whose CLAP top-1 tag is known, run in CI as a smoke check that the model export behaves.

---

## 6. Risks & open questions

- **CLAP fidelity:** similarity ≠ identity. Manage UX copy ("closest match"), tune with the golden corpus, and always offer the optimizer finisher.
- **ORT version drift** across Basic Pitch / Demucs / CLAP: keep one ORT pin for all three.
- **Tokenizer (tier C):** biggest hidden cost of free-form text. The fixed-vocabulary trick that makes tier A easy does not apply to user sentences — decide early between local text encoder vs. embedding-as-a-service.
- **LLM variance:** same prompt, different patch every time. That's a feature (exploration) until it isn't (reproducibility) — save prompts alongside generated presets.
- **Legal:** verify the chosen CLAP export's license permits shipping weights in a release. Same diligence as the Demucs/Basic Pitch vendoring already does.
