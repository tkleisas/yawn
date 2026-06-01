#include "transcribe/AudioToMidi.h"

#include "audio/AudioBuffer.h"
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"   // midi::Convert::vel7to16

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef YAWN_HAS_BASIC_PITCH
#include "bp_api.h"            // basic_pitch::transcribe (third_party/basicpitch)
#endif

namespace yawn {
namespace transcribe {

bool available() {
#ifdef YAWN_HAS_BASIC_PITCH
    return true;
#else
    return false;
#endif
}

#ifdef YAWN_HAS_BASIC_PITCH
namespace {

// Downmix to mono and resample to the model's input rate (22050 Hz) with
// linear interpolation. Adequate for transcription — instrument
// fundamentals of interest sit well below the post-resample Nyquist
// (~11 kHz), and Basic Pitch is robust to mild aliasing. A windowed-sinc
// resampler would be a future quality bump.
std::vector<float> toMono22050(const audio::AudioBuffer& buf, double srcSr) {
    const int nCh = buf.numChannels();
    const int nFr = buf.numFrames();
    if (nCh <= 0 || nFr <= 0 || srcSr <= 0.0) return {};

    const double dstSr = static_cast<double>(basic_pitch::kInputSampleRate);
    const double ratio = srcSr / dstSr;            // src frames per dst frame
    const int outLen = static_cast<int>(std::floor(static_cast<double>(nFr) / ratio));
    if (outLen <= 0) return {};

    std::vector<float> out(outLen, 0.0f);
    const float invCh = 1.0f / static_cast<float>(nCh);
    for (int i = 0; i < outLen; ++i) {
        const double srcPos = i * ratio;
        const int i0 = static_cast<int>(srcPos);
        const int i1 = std::min(i0 + 1, nFr - 1);
        const float frac = static_cast<float>(srcPos - i0);
        float mono = 0.0f;
        for (int c = 0; c < nCh; ++c) {
            const float s0 = buf.sample(c, i0);
            const float s1 = buf.sample(c, i1);
            mono += s0 + (s1 - s0) * frac;
        }
        out[i] = mono * invCh;
    }
    return out;
}

} // namespace
#endif // YAWN_HAS_BASIC_PITCH

std::unique_ptr<midi::MidiClip> audioToMidi(const audio::AudioBuffer& buffer,
                                            double sourceSampleRate,
                                            double bpm) {
#ifdef YAWN_HAS_BASIC_PITCH
    if (bpm <= 0.0) bpm = 120.0;

    std::vector<float> mono = toMono22050(buffer, sourceSampleRate);
    if (mono.empty()) return nullptr;

    std::vector<basic_pitch::Note> notes =
        basic_pitch::transcribe(mono.data(), static_cast<int>(mono.size()));
    if (notes.empty()) return nullptr;

    const double beatsPerSec = bpm / 60.0;
    double maxEndSec = 0.0;
    for (const auto& n : notes) maxEndSec = std::max(maxEndSec, static_cast<double>(n.endSec));
    double lengthBeats = std::ceil(maxEndSec * beatsPerSec);
    if (lengthBeats < 1.0) lengthBeats = 1.0;

    auto clip = std::make_unique<midi::MidiClip>(lengthBeats);
    for (const auto& n : notes) {
        midi::MidiNote note;
        note.startBeat = static_cast<double>(n.startSec) * beatsPerSec;
        note.duration  = std::max(0.01,
            static_cast<double>(n.endSec - n.startSec) * beatsPerSec);
        note.pitch     = static_cast<uint8_t>(std::clamp(n.pitch, 0, 127));
        const int vel7 = std::clamp(
            static_cast<int>(std::lround(n.amplitude * 127.0f)), 1, 127);
        note.velocity  = midi::Convert::vel7to16(static_cast<uint8_t>(vel7));
        clip->addNote(note);
    }
    return clip;
#else
    (void)buffer; (void)sourceSampleRate; (void)bpm;
    return nullptr;
#endif
}

} // namespace transcribe
} // namespace yawn
