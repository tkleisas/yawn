#pragma once

// AudioToMidi — converts an audio buffer into a MidiClip using Spotify's
// Basic Pitch model (polyphonic, via the `basicpitch` static lib). When
// the YAWN_HAS_BASIC_PITCH feature is not compiled in, available() is
// false and audioToMidi() returns nullptr.

#include <memory>

namespace yawn {
namespace audio { class AudioBuffer; }
namespace midi  { class MidiClip; }

namespace transcribe {

// True when Basic Pitch was compiled into this build.
bool available();

// Convert an audio buffer (any sample rate / channel count) into a
// MidiClip at the given project BPM. The audio is downmixed to mono and
// resampled to the model's 22050 Hz internally. Returns nullptr when the
// feature is unavailable, the input is empty, or no notes were detected.
// Runs synchronously (CPU only) — safe to call from a worker thread.
std::unique_ptr<midi::MidiClip> audioToMidi(const audio::AudioBuffer& buffer,
                                            double sourceSampleRate,
                                            double bpm);

} // namespace transcribe
} // namespace yawn
