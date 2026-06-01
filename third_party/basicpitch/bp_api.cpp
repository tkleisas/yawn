#include "bp_api.h"
#include "basicpitch.hpp"

#include <algorithm>

namespace basic_pitch {

std::vector<Note> transcribe(const float* mono22050, int length,
                             bool useMelodiaTrick) {
    // Need enough samples for the model to emit a meaningful number of
    // output frames — below ~0.25 s the posteriorgrams collapse to zero
    // frames and the note-creation step indexes into empty tensors.
    // Real clips are always far longer; this just guards degenerate input.
    const int kMinSamples = kInputSampleRate / 4;
    if (!mono22050 || length < kMinSamples) return {};

    // audio (22050 Hz mono) → posteriorgrams → note events.
    InferenceResult result = ort_inference(mono22050, length);
    std::vector<NoteEvent> events =
        notes_from_inference(result, useMelodiaTrick, /*include_pitch_bends*/ false);

    // Map note-event frame indices (ANNOTATIONS_FPS grid) → seconds.
    const int nFrames = static_cast<int>(result.notes.dimension(0));
    std::vector<float> times = frame_times(nFrames);
    if (times.empty()) return {};
    const int lastIdx = static_cast<int>(times.size()) - 1;

    std::vector<Note> out;
    out.reserve(events.size());
    for (const auto& e : events) {
        int s  = std::clamp(e.start_idx, 0, lastIdx);
        int en = std::clamp(e.end_idx,   0, lastIdx);
        if (en <= s) continue;
        Note n;
        n.startSec   = times[s];
        n.endSec     = times[en];
        n.pitch      = e.pitch;
        n.amplitude  = std::clamp(e.amplitude, 0.0f, 1.0f);
        out.push_back(n);
    }
    return out;
}

} // namespace basic_pitch
