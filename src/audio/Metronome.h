#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

// Controls when the metronome sounds
enum class MetronomeMode : int {
    Always    = 0,  // play during both playback and recording
    RecordOnly = 1, // play only while recording
    PlayOnly   = 2, // play only during playback (not recording)
    Off        = 3  // never play (overrides enabled)
};

class Metronome {
public:
    void init(double sampleRate, int maxBlockSize);

    void setEnabled(bool e) { m_enabled = e; }
    bool enabled() const { return m_enabled; }

    void setVolume(float v) { m_volume = std::clamp(v, 0.0f, 1.0f); }
    float volume() const { return m_volume; }

    void setBeatsPerBar(int b) { m_beatsPerBar = std::max(1, b); }
    int beatsPerBar() const { return m_beatsPerBar; }

    void setMode(MetronomeMode m) { m_mode = m; }
    MetronomeMode mode() const { return m_mode; }

    void reset();

    // Call from audio thread: adds click on top of output buffer.
    // Must be called BEFORE transport.advance() so positionInSamples is current.
    void process(float* output, int numFrames, int numChannels,
                 double bpm, int64_t positionInSamples, bool playing,
                 bool recording = false);

    const std::vector<float>& accentClick() const { return m_accentClick; }
    const std::vector<float>& normalClick() const { return m_normalClick; }

private:
    void generateClicks();
    void mixClickRange(float* output, int numChannels, int fromFrame, int toFrame);

    double m_sampleRate = kDefaultSampleRate;
    bool m_enabled = false;
    float m_volume = 0.7f;
    int m_beatsPerBar = 4;
    MetronomeMode m_mode = MetronomeMode::Always;

    std::vector<float> m_accentClick;
    std::vector<float> m_normalClick;

    int m_clickPos = -1;
    bool m_clickAccent = false;
};

} // namespace audio
} // namespace yawn
