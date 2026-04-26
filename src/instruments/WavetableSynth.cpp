#include "WavetableSynth.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

void WavetableSynth::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    generateWavetables();
    for (auto& v : m_voices) {
        v.ampEnv.setSampleRate(sampleRate);
        v.filtEnv.setSampleRate(sampleRate);
    }
    applyDefaults();
}

void WavetableSynth::reset() {
    for (auto& v : m_voices) {
        v.active = false;
        v.phase = v.phase2 = v.subPhase = 0.0;
        v.filterLow = v.filterBand = 0.0f;
        v.ampEnv.reset();
        v.filtEnv.reset();
    }
    m_lfoPhase = 0.0;
    m_voiceCounter = 0;
}

void WavetableSynth::generateWavetables() {
    m_tableData = std::make_unique<TableData>();
    auto& T = m_tableData->tables;
    // Table 0: Basic — Sine → Triangle → Saw → Square → Pulse
    m_tableFrameCounts[0] = 5;
    for (int s = 0; s < kFrameSize; ++s) {
        float t = static_cast<float>(s) / kFrameSize;
        T[0][0][s] = std::sin(2.0f * static_cast<float>(M_PI) * t);
        T[0][1][s] = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        T[0][2][s] = 2.0f * t - 1.0f;
        T[0][3][s] = (t < 0.5f) ? 1.0f : -1.0f;
        T[0][4][s] = (t < 0.1f) ? 1.0f : -1.0f;
    }
    // Table 1: PWM — pulse width sweep
    m_tableFrameCounts[1] = 16;
    for (int f = 0; f < 16; ++f) {
        float pw = 0.05f + 0.9f * static_cast<float>(f) / 15.0f;
        for (int s = 0; s < kFrameSize; ++s) {
            float t = static_cast<float>(s) / kFrameSize;
            T[1][f][s] = (t < pw) ? 1.0f : -1.0f;
        }
    }
    // Table 2: Formant — vocal formant sweep
    m_tableFrameCounts[2] = 16;
    for (int f = 0; f < 16; ++f) {
        float formantFreq = 1.0f + static_cast<float>(f) * 2.0f;
        for (int s = 0; s < kFrameSize; ++s) {
            float t = static_cast<float>(s) / kFrameSize;
            float carrier = std::sin(2.0f * static_cast<float>(M_PI) * t);
            float formant = std::sin(2.0f * static_cast<float>(M_PI) * t * formantFreq);
            float window = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
            T[2][f][s] = carrier * 0.5f + formant * window * 0.5f;
        }
    }
    // Table 3: Harmonic — additive harmonics buildup
    m_tableFrameCounts[3] = 16;
    for (int f = 0; f < 16; ++f) {
        int numHarmonics = f + 1;
        for (int s = 0; s < kFrameSize; ++s) {
            float t = static_cast<float>(s) / kFrameSize;
            float val = 0.0f;
            for (int h = 1; h <= numHarmonics; ++h)
                val += std::sin(2.0f * static_cast<float>(M_PI) * t * h) / h;
            T[3][f][s] = val * (2.0f / static_cast<float>(M_PI));
        }
    }
    // Table 4: Digital — bitcrushed textures
    m_tableFrameCounts[4] = 8;
    for (int f = 0; f < 8; ++f) {
        int bits = 8 - f;
        float levels = std::pow(2.0f, static_cast<float>(bits));
        for (int s = 0; s < kFrameSize; ++s) {
            float t = static_cast<float>(s) / kFrameSize;
            float raw = std::sin(2.0f * static_cast<float>(M_PI) * t);
            T[4][f][s] = std::round(raw * levels) / levels;
        }
    }
}

float WavetableSynth::readWavetable(int table, int numFrames, double phase, float position) const {
    float framePos = position * (numFrames - 1);
    int frame0 = static_cast<int>(framePos);
    int frame1 = std::min(frame0 + 1, numFrames - 1);
    float frameFrac = framePos - frame0;

    float samplePos = static_cast<float>(phase * kFrameSize);
    int idx0 = static_cast<int>(samplePos) % kFrameSize;
    int idx1 = (idx0 + 1) % kFrameSize;
    float sampleFrac = samplePos - std::floor(samplePos);

    auto& T = m_tableData->tables;
    float val0 = T[table][frame0][idx0] * (1.0f - sampleFrac)
               + T[table][frame0][idx1] * sampleFrac;
    float val1 = T[table][frame1][idx0] * (1.0f - sampleFrac)
               + T[table][frame1][idx1] * sampleFrac;

    return val0 * (1.0f - frameFrac) + val1 * frameFrac;
}

void WavetableSynth::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active = true;
    v.note = note;
    v.channel = ch;
    v.velocity = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;
    v.phase = v.phase2 = v.subPhase = 0.0;
    v.filterLow = v.filterBand = 0.0f;

    v.ampEnv.setADSR(m_params[kAmpAttack], m_params[kAmpDecay],
                     m_params[kAmpSustain], m_params[kAmpRelease]);
    v.filtEnv.setADSR(m_params[kFiltAttack], m_params[kFiltDecay],
                      m_params[kFiltSustain], m_params[kFiltRelease]);
    v.ampEnv.gate(true);
    v.filtEnv.gate(true);
}

void WavetableSynth::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch) {
            v.ampEnv.gate(false);
            v.filtEnv.gate(false);
        }
}

int WavetableSynth::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

void WavetableSynth::updateEnvelopes() {
    for (auto& v : m_voices) {
        v.ampEnv.setADSR(m_params[kAmpAttack], m_params[kAmpDecay],
                         m_params[kAmpSustain], m_params[kAmpRelease]);
        v.filtEnv.setADSR(m_params[kFiltAttack], m_params[kFiltDecay],
                          m_params[kFiltSustain], m_params[kFiltRelease]);
    }
}

void WavetableSynth::applyDefaults() {
    for (int i = 0; i < kParamCount; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
}

void WavetableSynth::process(float* buffer, int numFrames, int numChannels,
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
                if (v.active) { v.ampEnv.gate(false); v.filtEnv.gate(false); }
        }
        else if (msg.type == midi::MidiMessage::Type::PitchBend)
            m_pitchBend = midi::Convert::pb32toFloat(msg.value);
    }

    int tableIdx = std::clamp(static_cast<int>(m_params[kTable]), 0, kNumTables - 1);
    float position = m_params[kPosition];
    float cutoff = cutoffNormToHz(m_params[kFilterCutoff]);
    float reso = m_params[kFilterResonance];
    float filtEnvAmt = m_params[kFilterEnvAmount];
    float subLevel = m_params[kSubLevel];
    float detuneST = m_params[kUnisonDetune];
    float volume = m_params[kVolume];
    float lfoAmount = m_params[kLFOAmount];

    double lfoInc = m_params[kLFORate] / m_sampleRate;
    int numFramesInTable = m_tableFrameCounts[tableIdx];

    for (int vi = 0; vi < kMaxVoices; ++vi) {
        auto& v = m_voices[vi];
        if (!v.active) continue;

        float baseFreq = noteToFreq(v.note) *
            std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);
        double phaseInc = baseFreq / m_sampleRate;

        // Unison: second oscillator detuned
        float detuneRatio = std::pow(2.0f, detuneST / 12.0f);
        double phaseInc2 = phaseInc * detuneRatio;
        double subInc = phaseInc * 0.5;

        for (int i = 0; i < numFrames; ++i) {
            // LFO for position modulation
            float lfo = static_cast<float>(std::sin(m_lfoPhase * 2.0 * M_PI));
            float modPos = position + lfo * lfoAmount * 0.5f;
            modPos = std::clamp(modPos, 0.0f, 1.0f);

            // Read wavetable with frame morphing
            float osc1 = readWavetable(tableIdx, numFramesInTable, v.phase, modPos);
            float osc2 = (detuneST > 0.001f)
                ? readWavetable(tableIdx, numFramesInTable, v.phase2, modPos)
                : 0.0f;

            // Sub oscillator (sine, one octave down)
            float sub = static_cast<float>(std::sin(v.subPhase * 2.0 * M_PI))
                      * subLevel;

            float mix = osc1 * 0.5f + osc2 * 0.25f + sub;

            // SVF filter with envelope modulation
            float filtEnvVal = v.filtEnv.process();
            float modCutoff = cutoff *
                std::pow(2.0f, filtEnvAmt * filtEnvVal * 4.0f);
            modCutoff = std::clamp(modCutoff, 20.0f, 20000.0f);

            float w = static_cast<float>(M_PI * modCutoff / m_sampleRate);
            float f = 2.0f * std::sin(std::min(w, 1.5f));
            float q = 1.0f - reso * 0.98f;

            for (int os = 0; os < 2; ++os) {
                float high = mix - v.filterLow - q * v.filterBand;
                v.filterBand += 0.5f * f * high;
                v.filterLow  += 0.5f * f * v.filterBand;
            }
            v.filterBand = std::clamp(v.filterBand, -10.0f, 10.0f);
            v.filterLow  = std::clamp(v.filterLow,  -10.0f, 10.0f);

            float filtered = v.filterLow;

            // Amp envelope
            float ampEnvVal = v.ampEnv.process();
            float sample = filtered * ampEnvVal * v.velocity * volume;

            buffer[i * numChannels + 0] += sample;
            if (numChannels > 1)
                buffer[i * numChannels + 1] += sample;

            // Advance phases
            v.phase += phaseInc;
            while (v.phase >= 1.0) v.phase -= 1.0;
            v.phase2 += phaseInc2;
            while (v.phase2 >= 1.0) v.phase2 -= 1.0;
            v.subPhase += subInc;
            while (v.subPhase >= 1.0) v.subPhase -= 1.0;

            m_lfoPhase += lfoInc;
            while (m_lfoPhase >= 1.0) m_lfoPhase -= 1.0;
        }

        if (v.ampEnv.isIdle())
            v.active = false;
    }
}

} // namespace instruments
} // namespace yawn
