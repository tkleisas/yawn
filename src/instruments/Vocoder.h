#pragma once
// Vocoder.h — Vocoder instrument for YAWN
// Splits carrier (MIDI-triggered oscillator) and modulator (loaded sample or
// internal formant) into frequency bands, applies modulator envelope to carrier.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace yawn::instruments {

class Vocoder : public Instrument {
public:
    static constexpr int kMaxVoices  = 8;
    static constexpr int kMaxBands   = 32;
    static constexpr int kParamCount = 14;

    enum Param {
        kBands = 0,       // 4–32 bands
        kCarrierType,     // 0=Saw, 1=Square, 2=Pulse, 3=Noise
        kModSource,       // 0=Sample, 1=FormantA, 2=FormantE, 3=FormantI, 4=FormantO, 5=FormantU
        kBandwidth,       // 0.1–2.0 (Q multiplier)
        kAttack,          // 1–200 ms (envelope follower)
        kRelease,         // 1–500 ms (envelope follower)
        kFormantShift,    // -12 to +12 semitones
        kHighFreqTilt,    // -6 to +6 dB (boost/cut high bands)
        kUnvoicedSens,    // 0–1 (noise injection for sibilants)
        kDryCarrier,      // 0–1 (blend in dry carrier)
        kAmpAttack,       // 1–5000 ms
        kAmpRelease,      // 1–5000 ms
        kFilterCutoff,    // 200–20000 Hz (output LP filter)
        kVolume,          // 0–1
    };

    const char* name() const override { return "Vocoder"; }
    const char* id() const override { return "vocoder"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& v : m_voices) v = Voice{};
        resetParams();
        initBands();
    }

    void reset() override {
        for (auto& v : m_voices) v.active = false;
        m_modPlayPos = 0.0;
        m_rngState = 12345u;
        for (auto& b : m_bandState) b = BandState{};
        m_outFilterIc[0] = m_outFilterIc[1] = 0.0f;
    }

    void loadModulatorSample(const float* data, int numFrames, int numChannels) {
        // Store as mono
        m_modSample.resize(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += data[i * numChannels + ch];
            m_modSample[i] = sum / numChannels;
        }
        m_modSampleFrames = numFrames;
    }

    bool hasModulatorSample() const { return m_modSampleFrames > 0; }

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo info[kParamCount] = {
            {"Bands",         4.0f,   32.0f,  16.0f, "",    false},
            {"Carrier Type",  0.0f,    3.0f,   0.0f, "",    false},
            {"Mod Source",    0.0f,    5.0f,   1.0f, "",    false},
            {"Bandwidth",     0.1f,    2.0f,   1.0f, "",    false},
            {"Env Attack",    1.0f,  200.0f,   5.0f, "ms",  false},
            {"Env Release",   1.0f,  500.0f,  20.0f, "ms",  false},
            {"Formant Shift",-12.0f,  12.0f,   0.0f, "st",  false},
            {"HF Tilt",      -6.0f,    6.0f,   0.0f, "dB",  false},
            {"Unvoiced",      0.0f,    1.0f,   0.3f, "",    false},
            {"Dry Carrier",   0.0f,    1.0f,   0.0f, "",    false},
            {"Amp Attack",    1.0f, 5000.0f,   5.0f, "ms",  false},
            {"Amp Release",   1.0f, 5000.0f, 200.0f, "ms",  false},
            {"Output Filter",200.0f,20000.0f,20000.0f,"Hz", false},
            {"Volume",        0.0f,    1.0f,   0.8f, "",    false},
        };
        return info[std::clamp(index, 0, kParamCount - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        auto& pi = parameterInfo(index);
        m_params[index] = std::clamp(value, pi.minValue, pi.maxValue);
        if (index == kBands || index == kBandwidth || index == kFormantShift)
            initBands();
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        if (m_bypassed) return;

        const int   numBands     = static_cast<int>(m_params[kBands]);
        const int   carrierType  = static_cast<int>(m_params[kCarrierType]);
        const int   modSource    = static_cast<int>(m_params[kModSource]);
        const float envAtk       = m_params[kAttack] * 0.001f;
        const float envRel       = m_params[kRelease] * 0.001f;
        const float hfTiltDb     = m_params[kHighFreqTilt];
        const float unvoicedSens = m_params[kUnvoicedSens];
        const float dryCarrier   = m_params[kDryCarrier];
        const float filterCut    = m_params[kFilterCutoff];
        const float volume       = m_params[kVolume];

        // Envelope follower coefficients
        const float atkCoeff = std::exp(-1.0f / std::max(0.001f, envAtk * (float)m_sampleRate));
        const float relCoeff = std::exp(-1.0f / std::max(0.001f, envRel * (float)m_sampleRate));

        // HF tilt: linear gain per band (band 0 = 0dB, band N-1 = tiltDb)
        float tiltGains[kMaxBands];
        for (int b = 0; b < numBands; ++b) {
            float t = (numBands > 1) ? static_cast<float>(b) / (numBands - 1) : 0.0f;
            tiltGains[b] = std::pow(10.0f, (hfTiltDb * t) / 20.0f);
        }

        // Output LP filter
        const float cutNorm = std::clamp(filterCut / static_cast<float>(m_sampleRate), 0.001f, 0.499f);
        const float fg = std::tan(3.14159265f * cutNorm);
        const float fa1 = 1.0f / (1.0f + fg * (fg + 1.414f));
        const float fa2 = fg * fa1;
        const float fa3 = fg * fa2;

        // Process MIDI
        for (int i = 0; i < midi.count(); ++i) {
            const auto& msg = midi[i];
            if (msg.isNoteOn())
                noteOn(msg.note, msg.velocity, msg.channel);
            else if (msg.isNoteOff())
                noteOff(msg.note, msg.channel);
            else if (msg.isCC() && msg.ccNumber == 123)
                for (auto& v : m_voices) if (v.active) v.ampEnv.gate(false);
        }

        int activeVoices = 0;
        for (auto& v : m_voices) if (v.active) ++activeVoices;
        if (activeVoices == 0) return;

        for (int s = 0; s < numFrames; ++s) {
            // Generate modulator sample
            float modSig = getModulatorSample(modSource);

            // Generate carrier: sum of all voices
            float carrierSig = 0.0f;
            float envSum = 0.0f;
            for (auto& v : m_voices) {
                if (!v.active) continue;
                float e = v.ampEnv.process();
                envSum += e;
                if (v.ampEnv.isIdle()) { v.active = false; continue; }
                carrierSig += generateCarrier(v, carrierType) * e * v.velocity;
            }

            // Vocoder processing: analyze modulator bands, apply to carrier bands
            float vocodedOut = 0.0f;
            float unvoicedOut = 0.0f;

            for (int b = 0; b < numBands; ++b) {
                auto& bs = m_bandState[b];

                // Bandpass filter modulator
                float modBand = biquadProcess(modSig, bs.modBQ);

                // Envelope follower
                float modAbs = std::abs(modBand);
                if (modAbs > bs.envLevel)
                    bs.envLevel = atkCoeff * bs.envLevel + (1.0f - atkCoeff) * modAbs;
                else
                    bs.envLevel = relCoeff * bs.envLevel + (1.0f - relCoeff) * modAbs;

                // Bandpass filter carrier
                float carrBand = biquadProcess(carrierSig, bs.carrBQ);

                // Apply modulator envelope to carrier band
                vocodedOut += carrBand * bs.envLevel * tiltGains[b];

                // Detect unvoiced (high frequency energy)
                if (b >= numBands * 2 / 3)
                    unvoicedOut += modAbs;
            }

            // Mix in noise for unvoiced regions (sibilants)
            if (unvoicedSens > 0.0f) {
                float noise = (randFloat() - 0.5f) * 2.0f;
                vocodedOut += noise * unvoicedOut * unvoicedSens * 0.5f;
            }

            // Blend dry carrier
            if (dryCarrier > 0.0f)
                vocodedOut += carrierSig * dryCarrier;

            // Output LP filter
            if (filterCut < 19999.0f) {
                float v3 = vocodedOut - m_outFilterIc[1];
                float v1 = fa1 * m_outFilterIc[0] + fa2 * v3;
                float v2 = m_outFilterIc[1] + fa2 * m_outFilterIc[0] + fa3 * v3;
                m_outFilterIc[0] = 2.0f * v1 - m_outFilterIc[0];
                m_outFilterIc[1] = 2.0f * v2 - m_outFilterIc[1];
                vocodedOut = v2;
            }

            float out = vocodedOut * volume;
            buffer[s * numChannels] += out;
            if (numChannels > 1)
                buffer[s * numChannels + 1] += out;
        }
    }

private:
    // ── Biquad filter state ──
    struct BiquadState {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
    };

    struct BandState {
        BiquadState modBQ;   // modulator bandpass
        BiquadState carrBQ;  // carrier bandpass
        float envLevel = 0.0f;
    };

    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        double phase = 0.0;
        Envelope ampEnv;
    };

    // ── Biquad bandpass design ──
    void designBandpass(BiquadState& bq, float freq, float q) const {
        float w0 = 2.0f * 3.14159265f * freq / static_cast<float>(m_sampleRate);
        float sinW0 = std::sin(w0);
        float cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * q);

        float a0 = 1.0f + alpha;
        bq.b0 = (sinW0 * 0.5f) / a0;
        bq.b1 = 0.0f;
        bq.b2 = -(sinW0 * 0.5f) / a0;
        bq.a1 = (-2.0f * cosW0) / a0;
        bq.a2 = (1.0f - alpha) / a0;
        bq.z1 = bq.z2 = 0.0f;
    }

    float biquadProcess(float in, BiquadState& bq) const {
        float out = bq.b0 * in + bq.z1;
        bq.z1 = bq.b1 * in - bq.a1 * out + bq.z2;
        bq.z2 = bq.b2 * in - bq.a2 * out;
        return out;
    }

    // ── Band initialization ──
    void initBands() {
        int numBands = static_cast<int>(m_params[kBands]);
        float bwMult = m_params[kBandwidth];
        float formantShift = m_params[kFormantShift];
        float shiftRatio = std::pow(2.0f, formantShift / 12.0f);

        // Logarithmically spaced bands from 80Hz to 12000Hz
        float lowFreq = 80.0f;
        float highFreq = 12000.0f;
        float logLow = std::log(lowFreq);
        float logHigh = std::log(highFreq);

        for (int b = 0; b < numBands && b < kMaxBands; ++b) {
            float t = static_cast<float>(b) / std::max(1, numBands - 1);
            float centerFreq = std::exp(logLow + t * (logHigh - logLow));
            float shiftedFreq = centerFreq * shiftRatio;

            // Clamp to Nyquist
            float nyquist = static_cast<float>(m_sampleRate) * 0.49f;
            shiftedFreq = std::clamp(shiftedFreq, 20.0f, nyquist);
            centerFreq = std::clamp(centerFreq, 20.0f, nyquist);

            float q = (2.0f + numBands * 0.3f) * bwMult;
            designBandpass(m_bandState[b].modBQ, centerFreq, q);
            designBandpass(m_bandState[b].carrBQ, shiftedFreq, q);
            m_bandState[b].envLevel = 0.0f;
        }
    }

    // ── Carrier oscillator ──
    float generateCarrier(Voice& v, int type) {
        float freq = noteToFreq(v.note);
        double inc = freq / m_sampleRate;
        float out = 0.0f;

        switch (type) {
        case 0: // Saw
            out = static_cast<float>(2.0 * v.phase - 1.0);
            break;
        case 1: // Square
            out = (v.phase < 0.5) ? 1.0f : -1.0f;
            break;
        case 2: // Pulse (25% duty)
            out = (v.phase < 0.25) ? 1.0f : -1.0f;
            break;
        case 3: // Noise
            out = (randFloat() - 0.5f) * 2.0f;
            break;
        }

        v.phase += inc;
        if (v.phase >= 1.0) v.phase -= 1.0;
        return out;
    }

    // ── Modulator source ──
    float getModulatorSample(int source) {
        if (source == 0 && m_modSampleFrames > 0) {
            // Sample playback (looped)
            int idx = static_cast<int>(m_modPlayPos);
            float frac = static_cast<float>(m_modPlayPos - idx);
            int next = (idx + 1) % m_modSampleFrames;
            idx %= m_modSampleFrames;
            float s = m_modSample[idx] + frac * (m_modSample[next] - m_modSample[idx]);
            m_modPlayPos += 1.0;
            if (m_modPlayPos >= m_modSampleFrames) m_modPlayPos -= m_modSampleFrames;
            return s;
        }

        // Internal formant generator: periodic pulse train through formant filters
        // Approximate vowel formants with noise bursts at formant frequencies
        float noise = (randFloat() - 0.5f) * 2.0f;

        // Simple formant approximation: mix filtered noise
        // (In a full implementation, use a glottal pulse + formant filters)
        switch (source) {
        case 1: return noise * 0.7f;  // A-like: bright
        case 2: return noise * 0.5f;  // E-like: mid
        case 3: return noise * 0.4f;  // I-like: thin
        case 4: return noise * 0.6f;  // O-like: round
        case 5: return noise * 0.3f;  // U-like: dark
        default: return noise * 0.5f;
        }
    }

    // ── Note management ──
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
        Voice* slot = nullptr;
        for (auto& v : m_voices)
            if (!v.active) { slot = &v; break; }
        if (!slot) {
            int64_t oldest = INT64_MAX;
            for (auto& v : m_voices)
                if (v.startOrder < oldest) { oldest = v.startOrder; slot = &v; }
        }
        if (!slot) return;

        slot->active = true;
        slot->note = note;
        slot->channel = ch;
        slot->velocity = velocityToGain(vel16);
        slot->startOrder = m_voiceCounter++;
        slot->phase = 0.0;

        float atkSec = m_params[kAmpAttack] * 0.001f;
        float relSec = m_params[kAmpRelease] * 0.001f;
        slot->ampEnv.setSampleRate(m_sampleRate);
        slot->ampEnv.setADSR(atkSec, 0.01f, 1.0f, relSec);
        slot->ampEnv.gate(true);
    }

    void noteOff(uint8_t note, uint8_t ch) {
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.channel == ch) {
                v.ampEnv.gate(false);
                break;
            }
        }
    }

    float randFloat() {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        return static_cast<float>(m_rngState & 0x7FFFFF) / 8388607.0f;
    }

    void resetParams() {
        for (int i = 0; i < kParamCount; ++i)
            m_params[i] = parameterInfo(i).defaultValue;
        m_voiceCounter = 0;
        m_modPlayPos = 0.0;
        m_rngState = 12345u;
        m_outFilterIc[0] = m_outFilterIc[1] = 0.0f;
        for (auto& b : m_bandState) b = BandState{};
    }

    // ── State ──
    float m_params[kParamCount] = {};
    Voice m_voices[kMaxVoices] = {};
    BandState m_bandState[kMaxBands] = {};

    // Modulator sample
    std::vector<float> m_modSample;
    int m_modSampleFrames = 0;
    double m_modPlayPos = 0.0;

    uint32_t m_rngState = 12345u;
    int64_t m_voiceCounter = 0;
    float m_outFilterIc[2] = {};
};

} // namespace yawn::instruments
