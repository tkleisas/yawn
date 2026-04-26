#include "instruments/SubtractiveSynth.h"

namespace yawn {
namespace instruments {

void SubtractiveSynth::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) {
        v.osc1.setSampleRate(sampleRate);
        v.osc2.setSampleRate(sampleRate);
        v.subOsc.setSampleRate(sampleRate);
        v.subOsc.setWaveform(Oscillator::Sine);
        v.ampEnv.setSampleRate(sampleRate);
        v.filtEnv.setSampleRate(sampleRate);
    }
    applyDefaults();
}

void SubtractiveSynth::reset() {
    for (auto& v : m_voices) {
        v.active = false;
        v.osc1.reset(); v.osc2.reset(); v.subOsc.reset();
        v.ampEnv.reset(); v.filtEnv.reset();
        v.filterLow = v.filterBand = 0.0f;
    }
    m_lfoPhase = 0.0;
    m_voiceCounter = 0;
}

void SubtractiveSynth::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    // Handle MIDI
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn())
            noteOn(msg.note, msg.velocity, msg.channel);
        else if (msg.isNoteOff())
            noteOff(msg.note, msg.channel);
        else if (msg.isCC() && msg.ccNumber == 123) {
            // All Notes Off: release all voices
            for (auto& v : m_voices)
                if (v.active) { v.ampEnv.gate(false); v.filtEnv.gate(false); }
        }
        else if (msg.type == midi::MidiMessage::Type::PitchBend)
            m_pitchBend = midi::Convert::pb32toFloat(msg.value);
    }

    // Render voices
    for (int v = 0; v < kMaxVoices; ++v) {
        auto& voice = m_voices[v];
        if (!voice.active) continue;

        float baseFreq = noteToFreq(voice.note) *
            std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);

        voice.osc1.setWaveform((Oscillator::Waveform)(int)m_osc1Wave);
        voice.osc1.setFrequency(baseFreq);
        voice.osc2.setWaveform((Oscillator::Waveform)(int)m_osc2Wave);
        voice.osc2.setFrequency(baseFreq *
            std::pow(2.0f, m_osc2Octave + m_osc2Detune / 12.0f));
        voice.subOsc.setFrequency(baseFreq * 0.5);

        for (int i = 0; i < numFrames; ++i) {
            // Oscillator mix
            float mix = voice.osc1.process() * m_osc1Level
                      + voice.osc2.process() * m_osc2Level
                      + voice.subOsc.process() * m_subLevel
                      + noiseGen() * m_noiseLevel;

            // Filter with envelope + LFO modulation. Cutoff is 0..1
            // normalized; log-map to Hz here, then let env/lfo apply
            // octave-style modulation on top.
            float filtEnvVal = voice.filtEnv.process();
            float lfo = (float)std::sin(2.0 * M_PI * m_lfoPhase);
            float cutoffHz = cutoffNormToHz(m_filterCutoff);
            float modCutoff = cutoffHz *
                std::pow(2.0f, m_filterEnvAmount * filtEnvVal * 4.0f +
                               lfo * m_lfoDepth);
            modCutoff = std::clamp(modCutoff, 20.0f, 20000.0f);

            // SVF coefficient: use sin() for stability at high cutoffs
            float w = (float)(M_PI * modCutoff / m_sampleRate);
            float f = 2.0f * std::sin(std::min(w, 1.5f));
            float q = 1.0f - m_filterResonance * 0.98f;

            // Run SVF with 2x oversampling for stability
            for (int os = 0; os < 2; ++os) {
                float high = mix - voice.filterLow - q * voice.filterBand;
                voice.filterBand += 0.5f * f * high;
                voice.filterLow  += 0.5f * f * voice.filterBand;
            }
            // Clamp state to prevent blowup from rapid parameter changes
            voice.filterBand = std::clamp(voice.filterBand, -10.0f, 10.0f);
            voice.filterLow  = std::clamp(voice.filterLow,  -10.0f, 10.0f);

            float filtered;
            switch ((int)m_filterType) {
                case 1:  filtered = mix - voice.filterLow - q * voice.filterBand; break;
                case 2:  filtered = voice.filterBand; break;
                default: filtered = voice.filterLow; break;
            }

            float ampEnvVal = voice.ampEnv.process();
            float sample = filtered * ampEnvVal * voice.velocity * m_volume;

            buffer[i * numChannels + 0] += sample;
            if (numChannels > 1)
                buffer[i * numChannels + 1] += sample;
        }

        // Advance LFO (shared across voices, advanced once per voice render)
        m_lfoPhase += m_lfoRate * numFrames / m_sampleRate;
        while (m_lfoPhase >= 1.0) m_lfoPhase -= 1.0;

        if (voice.ampEnv.isIdle())
            voice.active = false;
    }
}

void SubtractiveSynth::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active = true;
    v.note = note;
    v.channel = ch;
    v.velocity = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;
    v.osc1.reset(); v.osc2.reset(); v.subOsc.reset();
    v.filterLow = v.filterBand = 0.0f;
    v.ampEnv.setADSR(m_ampA, m_ampD, m_ampS, m_ampR);
    v.filtEnv.setADSR(m_filtA, m_filtD, m_filtS, m_filtR);
    v.ampEnv.gate(true);
    v.filtEnv.gate(true);
}

void SubtractiveSynth::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch) {
            v.ampEnv.gate(false);
            v.filtEnv.gate(false);
        }
}

int SubtractiveSynth::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    // Steal oldest
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

void SubtractiveSynth::updateEnvelopes() {
    for (auto& v : m_voices) {
        v.ampEnv.setADSR(m_ampA, m_ampD, m_ampS, m_ampR);
        v.filtEnv.setADSR(m_filtA, m_filtD, m_filtS, m_filtR);
    }
}

void SubtractiveSynth::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

float SubtractiveSynth::noiseGen() {
    m_noiseRng ^= m_noiseRng << 13;
    m_noiseRng ^= m_noiseRng >> 17;
    m_noiseRng ^= m_noiseRng << 5;
    return (float)(m_noiseRng & 0xFFFF) / 32768.0f - 1.0f;
}

} // namespace instruments
} // namespace yawn
