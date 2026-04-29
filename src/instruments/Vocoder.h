#pragma once
// Vocoder.h — Vocoder instrument for YAWN
// Splits carrier (MIDI-triggered oscillator) and modulator (loaded sample or
// internal formant) into frequency bands, applies modulator envelope to carrier.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "effects/FreqMap.h"
#include <atomic>
#include <vector>
#include <algorithm>

namespace yawn::instruments {

class Vocoder : public Instrument {
public:
    static constexpr int kMaxVoices  = 8;
    static constexpr int kMaxBands   = 32;
    static constexpr int kParamCount = 15;

    // Cutoff stored normalized 0..1, log-mapped to 20..20000 Hz.
    static float cutoffNormToHz(float x) { return effects::logNormToHz(x, 20.0f, 20000.0f); }
    static float cutoffHzToNorm(float hz) { return effects::logHzToNorm(hz, 20.0f, 20000.0f); }
    static void  formatCutoffHz(float v, char* buf, int n) {
        effects::formatHz(cutoffNormToHz(v), buf, n);
    }

    enum Param {
        kBands = 0,       // 4–32 bands
        kCarrierType,     // 0=Saw, 1=Square, 2=Pulse, 3=Noise
        kModSource,       // 0=Sample, 1=FormantA, 2=FormantE, 3=FormantI, 4=FormantO, 5=FormantU, 6=Sidechain
        kBandwidth,       // 0.1–2.0 (Q multiplier)
        kAttack,          // 1–200 ms (envelope follower)
        kRelease,         // 1–500 ms (envelope follower)
        kFormantShift,    // -12 to +12 semitones
        kHighFreqTilt,    // -6 to +6 dB (boost/cut high bands)
        kUnvoicedSens,    // 0–1 (noise injection for sibilants)
        kDryCarrier,      // 0–1 (blend in dry carrier)
        kAmpAttack,       // 1–5000 ms
        kAmpRelease,      // 1–5000 ms
        kFilterCutoff,    // 0..1 normalized → 20..20000 Hz (log) output LP
        kVolume,          // 0–1
        kFreezeFormants,  // 0/1 toggle — sample-and-hold per-band envelopes
    };

    const char* name() const override { return "Vocoder"; }
    const char* id() const override { return "vocoder"; }
    bool supportsSidechain() const override { return true; }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

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

    void clearModulatorSample() {
        m_modSample.clear();
        m_modSampleFrames = 0;
    }

    bool hasModulatorSample() const { return m_modSampleFrames > 0; }
    int modulatorFrames() const { return m_modSampleFrames; }
    const float* modulatorData() const { return m_modSample.empty() ? nullptr : m_modSample.data(); }
    float modulatorPlayhead() const {
        if (m_modSampleFrames <= 0) return 0.0f;
        return static_cast<float>(m_modPlayPos / m_modSampleFrames);
    }
    bool isPlaying() const {
        for (auto& v : m_voices) if (v.active) return true;
        return false;
    }

    // Modulator-input level meter — returns the peak |modSig| seen by
    // process() since the last call, atomically swapped to zero so the
    // next read sees only "peak since last poll". Used by the Vocoder
    // display panel to render a live level meter regardless of which
    // modulator source is selected (sample, formant, sidechain). Cheap
    // when no audio is flowing through the vocoder.
    float consumeModulatorLevel() {
        return m_modLevelPeak.exchange(0.0f, std::memory_order_acq_rel);
    }

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kCarrierLabels[] = {"Saw", "Sqr", "Pulse", "Noise"};
        static constexpr const char* kModSourceLabels[] = {"Sample", "A", "E", "I", "O", "U", "SC"};
        static const InstrumentParameterInfo info[kParamCount] = {
            {"Bands",         4.0f,   32.0f,  16.0f, "",    false, false, WidgetHint::StepSelector},
            {"Carrier Type",  0.0f,    3.0f,   0.0f, "",    false, false, WidgetHint::StepSelector, kCarrierLabels, 4},
            {"Mod Source",    0.0f,    6.0f,   1.0f, "",    false, false, WidgetHint::StepSelector, kModSourceLabels, 7},
            {"Bandwidth",     0.1f,    2.0f,   1.0f, "",    false, false, WidgetHint::DentedKnob},
            {"Env Attack",    1.0f,  200.0f,   5.0f, "ms",  false},
            {"Env Release",   1.0f,  500.0f,  20.0f, "ms",  false},
            {"Formant Shift",-12.0f,  12.0f,   0.0f, "st",  false, false, WidgetHint::DentedKnob},
            {"HF Tilt",      -6.0f,    6.0f,   0.0f, "dB",  false, false, WidgetHint::DentedKnob},
            {"Unvoiced",      0.0f,    1.0f,   0.3f, "",    false},
            {"Dry Carrier",   0.0f,    1.0f,   0.0f, "",    false},
            {"Amp Attack",    1.0f, 5000.0f,   5.0f, "ms",  false},
            {"Amp Release",   1.0f, 5000.0f, 200.0f, "ms",  false},
            {"Output Filter",0.0f,1.0f,1.0f,"", false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Volume",        0.0f,    1.0f,   0.8f, "",    false, false, WidgetHint::DentedKnob},
            // Freeze: when on, the per-band envelope followers stop
            // updating and the carrier keeps imposing the last
            // spectral envelope it saw — turn the knob to 1, sing a
            // vowel, the carrier holds that vowel forever.
            {"Freeze",        0.0f,    1.0f,   0.0f, "",    false, false, WidgetHint::Toggle},
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

private:
    // ── Biquad filter state ──
    struct BiquadState {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
    };

    struct BandState {
        // Two modulator-side bandpass filter states + envelope
        // followers, one per stereo channel. With a sidechain source
        // panned hard right, the right-channel envelope opens while
        // the left stays low, so the carrier ducks asymmetrically and
        // the vocoder output preserves the modulator's stereo image.
        // For mono modulators (sample / formant) the engine feeds the
        // same signal to both sides and envL == envR (output collapses
        // to mono identically to the pre-stereo behaviour).
        BiquadState modBQ_L;
        BiquadState modBQ_R;
        BiquadState carrBQ;  // carrier bandpass (mono — shared by L/R)
        float envLevel_L = 0.0f;
        float envLevel_R = 0.0f;
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
    void designBandpass(BiquadState& bq, float freq, float q) const;
    float biquadProcess(float in, BiquadState& bq) const;

    // ── Band initialization ──
    void initBands();

    // ── Carrier oscillator ──
    float generateCarrier(Voice& v, int type);

    // ── Modulator source ──
    float getModulatorSample(int source);

    // ── Note management ──
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch);
    void noteOff(uint8_t note, uint8_t ch);

    float randFloat() {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        return static_cast<float>(m_rngState & 0x7FFFFF) / 8388607.0f;
    }

    void resetParams();

    // ── State ──
    float m_params[kParamCount] = {};
    Voice m_voices[kMaxVoices] = {};
    BandState m_bandState[kMaxBands] = {};

    // Modulator sample
    std::vector<float> m_modSample;
    int m_modSampleFrames = 0;
    double m_modPlayPos = 0.0;

    // ── Internal formant synthesizer ──
    // Glottal-pulse (band-limited sawtooth) excitation fed through
    // three parallel resonant bandpass filters tuned per-vowel to F1,
    // F2, F3. Replaces the original "noise * constant" stub so the
    // formant modulator sources actually produce vowel-shaped spectra
    // for the analysis bands to extract.
    //
    // Design notes:
    //   * Glottal pitch is fixed at ~110 Hz (typical male F0); we do
    //     NOT track MIDI note frequency here, because the modulator's
    //     job is to provide a stable formant ENVELOPE that the bands
    //     can lift onto the carrier. A pitch-tracking modulator would
    //     just multiply the carrier's harmonics by themselves.
    //   * Resonator Q is high (~12) so each formant has a distinct
    //     spectral peak. Lower Q smears the vowel; higher Q rings.
    //   * Vowel data lives in s_vowelFormants[5][3] in Vocoder.cpp.
    double m_glottalPhase = 0.0;
    BiquadState m_formantBQ[3] = {};
    int m_formantLastVowel = -1;     // re-design filters when vowel changes
    float m_formantShiftRatio = 1.0f; // re-design filters when Formant Shift changes

    uint32_t m_rngState = 12345u;
    int64_t m_voiceCounter = 0;
    // Output low-pass filter — separate state per stereo side so the
    // L/R signal paths don't bleed into each other through the shared
    // integrator memory.
    float m_outFilterIcL[2] = {};
    float m_outFilterIcR[2] = {};

    // Atomic peak |modSig| seen during process(), drained by the UI
    // via consumeModulatorLevel(). Allows the display panel to render
    // a meter that reflects the actual signal hitting the analysis
    // bands — works the same whether the modulator is a sample, a
    // formant generator, or a sidechain.
    std::atomic<float> m_modLevelPeak{0.0f};
};

} // namespace yawn::instruments
