#pragma once

// InstrumentRenderer — offline, fresh-instance device renderer for the
// live-coding prerender system (docs/live-coding.md §4.1). Builds a new
// instrument + optional FX chain from the Factory tables (single source of
// truth), feeds it a generated/deferred MIDI timeline block-by-block, and
// produces a non-interleaved AudioBuffer.
//
// Deliberately independent of the live AudioEngine: no PortAudio stream,
// no shared state, no retire list. Runs on the PrerenderManager worker
// pool. All inputs are value-copied at enqueue time; the only mutable
// exchange with the caller is the progress/cancel atomics.

#include "audio/AudioBuffer.h"
#include "midi/MidiTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace audio {

struct PrerenderNote {
    double beat = 0.0;         // transport beat (quarter notes)
    double durBeats = 0.25;
    int pitch = 60;
    float vel = 0.8f;          // 0..1 → 16-bit velocity
    int channel = 0;
};

// Generative source (offline only): fill `out` with the messages for the
// block covering [beat, beat + beatsInBlock). Return false to stop the
// render early (frames already rendered are discarded on cancel only —
// returning false just ends the timeline). `framesPerBlock` is fixed by
// the spec's block size.
using PrerenderNoteSource =
    std::function<void(double beat, int framesPerBlock, midi::MidiBuffer& out)>;

struct PrerenderSpec {
    std::string device;        // instrument factory id ("granular")
    std::vector<std::pair<int, float>> params;   // (index, value) — names
                               // resolved at ENQUEUE time, UI thread
    struct Fx {
        std::string id;        // effect factory id ("reverb")
        std::vector<std::pair<int, float>> params;
    };
    std::vector<Fx> fx;

    std::vector<PrerenderNote> notes;
    PrerenderNoteSource noteSource;    // optional generative source

    uint64_t seed = 0;         // determinism contract (C++ sources)
    double tempoBpm = 120.0;
    double lengthBeats = 4.0;
    double tailBeats = 1.0;    // release tail after the last note
    int sampleRate = 48000;
    int channels = 2;          // 1 or 2

    // Validity check (factory ids resolvable etc.) done at enqueue.
};

// Renders the spec on the calling (worker) thread. Returns nullptr on
// failure (unknown device, cancelled, noteSource aborted).
std::shared_ptr<AudioBuffer> renderDevice(
    const PrerenderSpec& spec,
    std::atomic<float>& progress,
    const std::atomic<bool>& cancel);

} // namespace audio
} // namespace yawn
