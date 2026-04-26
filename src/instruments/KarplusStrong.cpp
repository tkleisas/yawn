#include "instruments/KarplusStrong.h"

namespace yawn {
namespace instruments {

void KarplusStrong::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) {
        v.ampEnv.setSampleRate(sampleRate);
    }
    applyDefaults();
}

void KarplusStrong::reset() {
    for (auto& v : m_voices) {
        v.active = false;
        std::memset(v.delayLine, 0, sizeof(v.delayLine));
        v.writePos = 0;
        v.delayLen = 0;
        v.excitePos = 0;
        v.exciteLen = 0;
        v.lpState = 0.0f;
        v.apState = 0.0f;
        v.ampEnv.reset();
        v.bodyL.reset();
        v.bodyR.reset();
    }
    m_voiceCounter = 0;
    m_noiseRng = 54321;
}

void KarplusStrong::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    // Handle MIDI
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn())
            noteOn(msg.note, msg.velocity, msg.channel);
        else if (msg.isNoteOff())
            noteOff(msg.note, msg.channel);
        else if (msg.isCC() && msg.ccNumber == 123) {
            for (auto& v : m_voices)
                if (v.active) v.ampEnv.gate(false);
        }
        else if (msg.type == midi::MidiMessage::Type::PitchBend)
            m_pitchBend = midi::Convert::pb32toFloat(msg.value);
    }

    float damping = m_params[kDamping];
    float decay = 0.9f + m_params[kDecay] * 0.099f;  // 0.9 to 0.999
    float stretch = m_params[kStretch];
    float body = m_params[kBody];
    float volume = m_params[kVolume];

    // Allpass coefficient for string stretch (inharmonicity)
    float apCoeff = stretch * 0.5f;

    // Damping: LP coefficient in feedback loop (higher = more damping)
    float lpCoeff = 0.5f + damping * 0.45f;  // 0.5 to 0.95

    for (int vi = 0; vi < kMaxVoices; ++vi) {
        auto& v = m_voices[vi];
        if (!v.active) continue;

        for (int i = 0; i < numFrames; ++i) {
            // Excitation: inject into delay line during attack phase
            float excite = 0.0f;
            if (v.excitePos < v.exciteLen) {
                excite = generateExcitation(v) * v.velocity;
                v.excitePos++;
            }

            // Read from delay line (with linear interpolation for fractional delay)
            float readPosF = static_cast<float>(v.writePos) - v.delayLenF;
            if (readPosF < 0.0f) readPosF += kMaxDelay;
            int idx0 = static_cast<int>(readPosF) % kMaxDelay;
            int idx1 = (idx0 + 1) % kMaxDelay;
            float frac = readPosF - std::floor(readPosF);
            float delayed = v.delayLine[idx0] * (1.0f - frac) + v.delayLine[idx1] * frac;

            // Low-pass filter in feedback loop (damping)
            v.lpState = v.lpState * lpCoeff + delayed * (1.0f - lpCoeff);

            // Allpass filter for inharmonicity (string stretch)
            float ap_in = v.lpState;
            float ap_out = apCoeff * ap_in + v.apState - apCoeff * v.apState;
            // Blend between pure KS and stretched
            float feedback = ap_in * (1.0f - stretch) + ap_out * stretch;
            v.apState = ap_in;

            // Write feedback + excitation into delay line
            v.delayLine[v.writePos] = (feedback * decay) + excite;
            v.writePos = (v.writePos + 1) % kMaxDelay;

            // Apply amplitude envelope
            float env = v.ampEnv.process();
            float sample = delayed * env * volume;

            // Body resonance (bandpass filter)
            if (body > 0.01f) {
                float bodyOut = v.bodyL.process(sample);
                sample = sample * (1.0f - body) + bodyOut * body;
            }

            buffer[i * numChannels + 0] += sample;
            if (numChannels > 1)
                buffer[i * numChannels + 1] += sample;
        }

        if (v.ampEnv.isIdle())
            v.active = false;
    }
}

void KarplusStrong::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active = true;
    v.note = note;
    v.channel = ch;
    v.velocity = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;

    // Calculate delay line length from pitch
    float freq = noteToFreq(note) *
        std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);
    v.delayLenF = static_cast<float>(m_sampleRate / freq);
    v.delayLen = std::min(static_cast<int>(v.delayLenF), kMaxDelay - 1);

    // Clear delay line
    std::memset(v.delayLine, 0, sizeof(v.delayLine));
    v.writePos = 0;
    v.lpState = 0.0f;
    v.apState = 0.0f;

    // Excitation length
    float attackTime = m_params[kAttack];
    v.exciteLen = std::max(1, static_cast<int>(attackTime * m_sampleRate));
    v.excitePos = 0;

    // Body resonance: tune to note fundamental
    v.bodyL.compute(effects::Biquad::Type::BandPass, m_sampleRate,
                    freq, 0.0, 2.0);

    // Amp envelope: short attack, long sustain, moderate release
    v.ampEnv.setADSR(0.001f, 0.01f, 1.0f, 0.5f);
    v.ampEnv.gate(true);
}

void KarplusStrong::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices) {
        if (v.active && v.note == note && v.channel == ch)
            v.ampEnv.gate(false);
    }
}

int KarplusStrong::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

float KarplusStrong::generateExcitation(Voice& v) {
    int type = std::clamp(static_cast<int>(m_params[kExciter]), 0, 3);
    float brightness = m_params[kBrightness];
    float t = static_cast<float>(v.excitePos) / static_cast<float>(v.exciteLen);

    // Envelope shape: quick attack, fast decay within excitation window
    float excEnv = (t < 0.1f) ? t / 0.1f : (1.0f - t) / 0.9f;
    excEnv = std::max(0.0f, excEnv);

    float sample = 0.0f;
    switch (type) {
    case Noise:
        // White noise burst
        sample = noiseGen();
        break;
    case Bright:
        // Band-limited noise (brighter, more harmonics)
        sample = noiseGen() * 0.5f + noiseGen() * 0.5f;  // sum of two → more gaussian
        sample *= (1.0f + brightness);
        break;
    case Saw:
        // Sawtooth impulse
        sample = 2.0f * (static_cast<float>(v.excitePos % v.delayLen) /
                 std::max(1.0f, static_cast<float>(v.delayLen))) - 1.0f;
        break;
    case Sine:
        // Sine burst at string fundamental
        sample = std::sin(2.0f * static_cast<float>(M_PI) * v.excitePos / v.delayLenF);
        break;
    }

    // Brightness: simple LP on excitation
    sample = sample * brightness + v.lpState * (1.0f - brightness) * 0.3f;

    return sample * excEnv;
}

float KarplusStrong::noiseGen() {
    m_noiseRng ^= m_noiseRng << 13;
    m_noiseRng ^= m_noiseRng >> 17;
    m_noiseRng ^= m_noiseRng << 5;
    return static_cast<float>(static_cast<int32_t>(m_noiseRng)) / 2147483648.0f;
}

void KarplusStrong::applyDefaults() {
    for (int i = 0; i < kParamCount; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
}

} // namespace instruments
} // namespace yawn
