#include "instruments/StringMachine.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

void StringMachine::init(double sampleRate, int maxBlockSize) {
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) {
        for (auto& o : v.saws) {
            o.setSampleRate(sampleRate);
            o.setWaveform(Oscillator::Saw);
        }
        v.ampEnv.setSampleRate(sampleRate);
    }
    applyDefaults();
    reset();
}

void StringMachine::reset() {
    for (auto& v : m_voices) {
        v.active = false;
        for (auto& o : v.saws) o.reset();
        v.ampEnv.reset();
    }
    m_voiceCounter = 0;
    m_pitchBend    = 0.0f;
    m_filterLow    = 0.0f;
    m_filterBand   = 0.0f;
    std::memset(m_chorusBuf, 0, sizeof(m_chorusBuf));
    m_chorusWrite  = 0;
    m_chorusPhase  = 0.0;
}

void StringMachine::process(float* buffer, int numFrames, int numChannels,
                             const midi::MidiBuffer& midi) {
    // ── MIDI ────────────────────────────────────────────────────────
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

    // ── Per-voice oscillator setup. Detune stagger spans ±halfRange
    // cents around the centre, so saw[0] = -halfRange, saw[1] = 0,
    // saw[2] = +halfRange. 15-cent max range was chosen by ear: less
    // is too narrow to feel like an ensemble, more starts to sound
    // out-of-tune rather than wide.
    const float maxCents  = 15.0f;
    const float halfRange = m_detune * maxCents * 0.5f;
    static const float kOctaveMul[kOctaves] = {0.5f, 1.0f, 2.0f}; // 16', 8', 4'
    const float octaveLevel[kOctaves] = {m_oct16Level, m_oct8Level, m_oct4Level};

    // Pitch-bend ±2 semitones.
    const float bendMul = std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);

    // Pre-update each voice's oscillator frequencies for this block.
    for (int vi = 0; vi < kMaxVoices; ++vi) {
        auto& voice = m_voices[vi];
        if (!voice.active) continue;
        const float baseHz = noteToFreq(voice.note) * bendMul;
        for (int oct = 0; oct < kOctaves; ++oct) {
            const float octHz = baseHz * kOctaveMul[oct];
            for (int s = 0; s < kSawsPerOctave; ++s) {
                // Cents stagger: -halfRange, 0, +halfRange.
                const float cents = (static_cast<float>(s) - 1.0f) * halfRange;
                const float hz    = octHz * std::pow(2.0f, cents / 1200.0f);
                voice.saws[oct * kSawsPerOctave + s].setFrequency(hz);
            }
        }
    }

    // ── Filter coefficients (cheap one-pole pair forming a 2nd-order
    // SVF-ish LP). m_filterLow is the lowpass output; we update it
    // per-sample below from the post-voice-mix sum.
    const float cutoffHz = brightnessNormToHz(m_brightness);
    const float w        = static_cast<float>(M_PI * cutoffHz / m_sampleRate);
    const float fcoef    = 2.0f * std::sin(std::min(w, 1.5f));
    const float qcoef    = 0.7f;   // fixed mild Q

    // ── Chorus state. Three taps spaced 120° in LFO phase, depths
    // up to ~12ms, base offset ~14ms so taps stay inside their
    // modulated range without wrapping past zero.
    const float lfoBaseMs   = 14.0f;
    const float lfoDepthMs  = 12.0f * m_ensembleDepth;
    const float msPerSample = 1000.0f / static_cast<float>(m_sampleRate);

    // ── Per-sample render loop. Voice amp envelopes advance here.
    for (int i = 0; i < numFrames; ++i) {
        // 1) Sum all voices' saw stacks weighted by per-octave levels.
        float voiceMix = 0.0f;
        for (int vi = 0; vi < kMaxVoices; ++vi) {
            auto& voice = m_voices[vi];
            if (!voice.active) continue;
            float voiceSample = 0.0f;
            for (int oct = 0; oct < kOctaves; ++oct) {
                if (octaveLevel[oct] < 1.0e-4f) {
                    // Skip — but advance the oscillators so phase
                    // stays current if the user re-enables the
                    // section mid-note.
                    for (int s = 0; s < kSawsPerOctave; ++s)
                        voice.saws[oct * kSawsPerOctave + s].process();
                    continue;
                }
                float octSum = 0.0f;
                for (int s = 0; s < kSawsPerOctave; ++s)
                    octSum += voice.saws[oct * kSawsPerOctave + s].process();
                voiceSample += octSum * (octaveLevel[oct] / kSawsPerOctave);
            }
            const float env = voice.ampEnv.process();
            voiceMix += voiceSample * env * voice.velocity;
            if (voice.ampEnv.isIdle()) voice.active = false;
        }

        // 2) Shared 2-pole LP filter (paraphonic) — single SVF step
        // with a fixed Q. The "Brightness" knob sets the cutoff.
        const float high = voiceMix - m_filterLow - qcoef * m_filterBand;
        m_filterBand += fcoef * high;
        m_filterLow  += fcoef * m_filterBand;
        m_filterBand  = std::clamp(m_filterBand, -10.0f, 10.0f);
        m_filterLow   = std::clamp(m_filterLow,  -10.0f, 10.0f);
        const float dry = m_filterLow;

        // 3) Write into chorus delay line.
        m_chorusBuf[m_chorusWrite] = dry;

        // 4) Three taps at 120°-offset LFO phases, modulated delay.
        float tap[kChorusTaps];
        if (m_ensembleOn) {
            for (int t = 0; t < kChorusTaps; ++t) {
                const double ph = m_chorusPhase + t * (1.0 / kChorusTaps);
                const float  s  = static_cast<float>(std::sin(2.0 * M_PI * ph));
                const float  delayMs = lfoBaseMs + lfoDepthMs * s;
                const float  delaySamples = delayMs / msPerSample;

                // Linear-interpolated read from the ring buffer.
                float readPos = static_cast<float>(m_chorusWrite) - delaySamples;
                while (readPos < 0.0f)              readPos += kChorusBufFrames;
                while (readPos >= kChorusBufFrames) readPos -= kChorusBufFrames;
                const int   i0 = static_cast<int>(readPos);
                const int   i1 = (i0 + 1) % kChorusBufFrames;
                const float frac = readPos - i0;
                tap[t] = m_chorusBuf[i0] * (1.0f - frac) + m_chorusBuf[i1] * frac;
            }
        } else {
            tap[0] = tap[1] = tap[2] = dry;
        }

        m_chorusWrite = (m_chorusWrite + 1) % kChorusBufFrames;

        // 5) Stereo mix: tap0 → L, tap2 → R, tap1 dead-centre. When
        // ensemble is off we just output dry on both channels (the
        // taps were collapsed to dry above).
        const float left  = (tap[0] * 0.65f + tap[1] * 0.5f) * 0.7f;
        const float right = (tap[2] * 0.65f + tap[1] * 0.5f) * 0.7f;

        const float outL = left  * m_volume;
        const float outR = right * m_volume;

        buffer[i * numChannels + 0] += outL;
        if (numChannels > 1)
            buffer[i * numChannels + 1] += outR;

        // 6) Advance shared chorus LFO once per sample.
        m_chorusPhase += static_cast<double>(m_ensembleRate) / m_sampleRate;
        if (m_chorusPhase >= 1.0) m_chorusPhase -= 1.0;
    }
}

void StringMachine::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active     = true;
    v.note       = note;
    v.channel    = ch;
    v.velocity   = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;
    for (auto& o : v.saws) o.reset();
    // Sustain held at 1.0 so the envelope stays at full level after
    // attack — releases when noteOff fires. Decay is irrelevant
    // (large value) since we don't tween down to a lower sustain.
    v.ampEnv.setADSR(m_attack, 0.001f, 1.0f, m_release);
    v.ampEnv.gate(true);
}

void StringMachine::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch)
            v.ampEnv.gate(false);
}

int StringMachine::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

void StringMachine::updateEnvelopes() {
    for (auto& v : m_voices)
        v.ampEnv.setADSR(m_attack, 0.001f, 1.0f, m_release);
}

void StringMachine::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

} // namespace instruments
} // namespace yawn
