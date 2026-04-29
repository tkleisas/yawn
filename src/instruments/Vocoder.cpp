#include "Vocoder.h"
#include <cmath>
#include <climits>

namespace yawn::instruments {

void Vocoder::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) v = Voice{};
    resetParams();
    initBands();
}

void Vocoder::reset() {
    for (auto& v : m_voices) v.active = false;
    m_modPlayPos = 0.0;
    m_rngState = 12345u;
    for (auto& b : m_bandState) b = BandState{};
    m_outFilterIcL[0] = m_outFilterIcL[1] = 0.0f;
    m_outFilterIcR[0] = m_outFilterIcR[1] = 0.0f;
    // Formant synthesizer state — phase rewinds, filter memory zeros,
    // and "last vowel" tag invalidates so coefficients re-design on
    // the next formant-mode sample.
    m_glottalPhase = 0.0;
    for (auto& bq : m_formantBQ) bq = BiquadState{};
    m_formantLastVowel = -1;
    m_formantShiftRatio = 1.0f;
}

void Vocoder::designBandpass(BiquadState& bq, float freq, float q) const {
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

float Vocoder::biquadProcess(float in, BiquadState& bq) const {
    float out = bq.b0 * in + bq.z1;
    bq.z1 = bq.b1 * in - bq.a1 * out + bq.z2;
    bq.z2 = bq.b2 * in - bq.a2 * out;
    return out;
}

void Vocoder::initBands() {
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
        // Both modulator-side bandpass states get the same coefficients
        // — they're independent only in their internal z1/z2 memory so
        // L and R signals filter without crosstalk.
        designBandpass(m_bandState[b].modBQ_L, centerFreq, q);
        designBandpass(m_bandState[b].modBQ_R, centerFreq, q);
        designBandpass(m_bandState[b].carrBQ,  shiftedFreq, q);
        m_bandState[b].envLevel_L = 0.0f;
        m_bandState[b].envLevel_R = 0.0f;
    }
}

float Vocoder::generateCarrier(Voice& v, int type) {
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

// Vowel formant table (Hz). Standard cardinal vowel data, averaged
// from male-voice references. Three formants per vowel — F1, F2, F3
// — covers enough spectral shape for the analysis bands to extract
// recognisably distinct vowels.
//
//   Vowel | F1   F2    F3
//   ──────┼──────────────────
//     A   | 730  1090  2440
//     E   | 530  1840  2480
//     I   | 270  2290  3010
//     O   | 570   840  2410
//     U   | 300   870  2240
//
// Order matches Vocoder::Param::kModSource values 1..5 (Formant A/E/I/O/U).
static constexpr float s_vowelFormants[5][3] = {
    { 730.0f, 1090.0f, 2440.0f },   // A
    { 530.0f, 1840.0f, 2480.0f },   // E
    { 270.0f, 2290.0f, 3010.0f },   // I
    { 570.0f,  840.0f, 2410.0f },   // O
    { 300.0f,  870.0f, 2240.0f },   // U
};

// Per-formant amplitude weights — F1 is the loudest energy peak in
// vowel spectra, F2 is roughly half as loud, F3 is dimmer still.
// These match the rough source-filter perceptual weighting and keep
// the formant output from saturating the analysis bands.
static constexpr float s_formantAmp[3] = { 1.0f, 0.5f, 0.25f };

float Vocoder::getModulatorSample(int source) {
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

    // ── Internal formant synthesizer (sources 1..5 = A/E/I/O/U) ──
    //
    // Glottal pulse train (saw approximation) at ~110 Hz, fed in
    // parallel through three highly-resonant bandpass filters tuned
    // to the chosen vowel's F1, F2, F3. The analysis bands then
    // extract that spectral envelope and impose it on the MIDI-keyed
    // carrier — i.e. you "speak" the vowel with the carrier oscillator.
    //
    // Pitch is intentionally fixed: a pitch-tracking modulator would
    // produce harmonics that exactly coincide with the carrier
    // harmonics and the band envelopes would just trace the carrier's
    // own spectrum (boring tonally and no formant character at all).
    // 110 Hz gives a dense enough harmonic series that the formant
    // resonators have something to colour.
    //
    // Vowel-change detection: re-design the resonator coefficients
    // only when the selected vowel actually changes (or when Formant
    // Shift moves them), not per-sample — that's still ~64 mults per
    // change, but it amortises across the whole block in practice
    // since vowel changes are mode-switches.
    int vowel = std::clamp(source, 1, 5) - 1;   // 0..4 = A/E/I/O/U

    const float shiftRatio = std::pow(2.0f, m_params[kFormantShift] / 12.0f);
    if (vowel != m_formantLastVowel ||
        std::abs(shiftRatio - m_formantShiftRatio) > 1e-4f) {
        const float nyquist = static_cast<float>(m_sampleRate) * 0.49f;
        // Q≈12 — sharp enough to give each formant a distinct peak,
        // not so sharp that the resonator rings into self-oscillation.
        constexpr float kFormantQ = 12.0f;
        for (int f = 0; f < 3; ++f) {
            float fc = std::clamp(s_vowelFormants[vowel][f] * shiftRatio,
                                  20.0f, nyquist);
            designBandpass(m_formantBQ[f], fc, kFormantQ);
        }
        m_formantLastVowel = vowel;
        m_formantShiftRatio = shiftRatio;
    }

    // Glottal pulse train as band-limited sawtooth at 110 Hz. The saw
    // is rich in harmonics so all three formant resonators have
    // material to ring on. A small amount of noise mixed in gives
    // the result a breathier, more natural texture — closer to a
    // real glottis than a perfectly periodic source.
    constexpr double kGlottalHz = 110.0;
    m_glottalPhase += kGlottalHz / m_sampleRate;
    if (m_glottalPhase >= 1.0) m_glottalPhase -= 1.0;
    const float saw = static_cast<float>(2.0 * m_glottalPhase - 1.0);
    const float breath = (randFloat() - 0.5f) * 0.10f;
    const float excite = saw * 0.9f + breath;

    // Three parallel formant resonators. Sum with vowel-typical
    // amplitude weights so F1 dominates and F2/F3 add colour rather
    // than overwhelming.
    float out = 0.0f;
    for (int f = 0; f < 3; ++f) {
        out += biquadProcess(excite, m_formantBQ[f]) * s_formantAmp[f];
    }
    // Normalisation: parallel resonators with high Q can run hot.
    // 0.4 lands the peak around -12 dBFS on sustained vowels, which
    // is plenty of headroom for the analysis bands and matches the
    // typical level a good sample source would feed in at.
    return out * 0.4f;
}

void Vocoder::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
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

void Vocoder::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices) {
        if (v.active && v.note == note && v.channel == ch) {
            v.ampEnv.gate(false);
            break;
        }
    }
}

void Vocoder::resetParams() {
    for (int i = 0; i < kParamCount; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
    m_voiceCounter = 0;
    m_modPlayPos = 0.0;
    m_rngState = 12345u;
    m_outFilterIcL[0] = m_outFilterIcL[1] = 0.0f;
    m_outFilterIcR[0] = m_outFilterIcR[1] = 0.0f;
    for (auto& b : m_bandState) b = BandState{};
    m_glottalPhase = 0.0;
    for (auto& bq : m_formantBQ) bq = BiquadState{};
    m_formantLastVowel = -1;
    m_formantShiftRatio = 1.0f;
}

void Vocoder::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    if (m_bypassed) return;

    const int   numBands     = static_cast<int>(m_params[kBands]);
    // Band-count compensation. The synthesis sum is N parallel
    // bandpass slices of the carrier, each weighted by its band
    // envelope. With partially correlated band outputs (typical for
    // adjacent-Q bandpass filters with overlapping skirts) the sum
    // grows roughly as sqrt(N) — without this scale, dialling the
    // Bands knob from 4 → 32 makes the perceived output ~3x louder
    // and the resulting vocoder is hot enough to clip the master
    // bus on a mid-volume modulator. 1/sqrt(N) keeps the apparent
    // loudness flat as you sweep the band count, so the knob's
    // perceptual character is "spectral resolution" rather than
    // "loudness × resolution". Empirically this also lands the
    // typical "16 bands, 0.8 volume" preset around -6 dBFS on a
    // fully-modulated saw carrier, which is sane headroom-wise.
    const float bandGain     = 1.0f /
        std::sqrt(static_cast<float>(std::max(1, numBands)));
    const int   carrierType  = static_cast<int>(m_params[kCarrierType]);
    const int   modSource    = static_cast<int>(m_params[kModSource]);
    const float envAtk       = m_params[kAttack] * 0.001f;
    const float envRel       = m_params[kRelease] * 0.001f;
    const float hfTiltDb     = m_params[kHighFreqTilt];
    const float unvoicedSens = m_params[kUnvoicedSens];
    const float dryCarrier   = m_params[kDryCarrier];
    const float filterCut    = cutoffNormToHz(m_params[kFilterCutoff]);
    const float volume       = m_params[kVolume];
    // Freeze formants: when on, the per-band envelope followers stop
    // tracking the modulator entirely — bs.envLevel_L/R keep their
    // last-snapshot values. The carrier keeps being shaped by that
    // frozen spectral envelope, so the held vowel/timbre persists
    // even after the modulator goes silent. Modulator analysis
    // (biquad filtering) still runs because we use bandL/bandR for
    // the unvoiced-noise injection level — but the envelope SAMPLE
    // step that's normally absR-driven is skipped.
    const bool  freeze       = m_params[kFreezeFormants] >= 0.5f;

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

    // Per-block running peak for the UI meter — CAS-merged into the
    // atomic at the end of the block so we don't pay an atomic op
    // every sample. Tracks max(|L|, |R|) so a hard-panned modulator
    // still drives the meter on whichever side has signal.
    float blockModPeak = 0.0f;

    // Per-band running peak across this block, CAS-merged into
    // m_bandPeak[] at the end. Drives the live spectrum-style band
    // display in VocoderDisplayPanel. Stack-local so the per-sample
    // band loop does no atomic ops at all — one CAS per band per
    // block, not per sample.
    float blockBandPeak[kMaxBands] = {};

    const bool stereoOut = (numChannels > 1);

    for (int s = 0; s < numFrames; ++s) {
        // ── Modulator (stereo) ──
        // Split L/R when we have a real stereo source (sidechain on a
        // stereo buffer); collapse to mono for sample / formant
        // sources where the analysis is single-channel. Mono inputs
        // produce identical L/R envelopes so the output is the same
        // mono signal duplicated across channels (matches pre-stereo
        // behaviour for mono modulator sources).
        float modL, modR;
        if (modSource == 6 && m_sidechainBuffer) {
            modL = m_sidechainBuffer[s * numChannels];
            modR = stereoOut ? m_sidechainBuffer[s * numChannels + 1] : modL;
        } else {
            const float m = getModulatorSample(modSource);
            modL = m;
            modR = m;
        }
        const float modAbs0 = std::max(std::abs(modL), std::abs(modR));
        if (modAbs0 > blockModPeak) blockModPeak = modAbs0;

        // ── Carrier (mono — same MIDI-driven oscillator across L/R) ──
        float carrierSig = 0.0f;
        for (auto& v : m_voices) {
            if (!v.active) continue;
            float e = v.ampEnv.process();
            if (v.ampEnv.isIdle()) { v.active = false; continue; }
            carrierSig += generateCarrier(v, carrierType) * e * v.velocity;
        }

        // ── Per-band analysis (split L/R) + synthesis ──
        // Each band runs two parallel envelope followers (one per
        // modulator channel) but a single carrier bandpass. The
        // carrier band's contribution is scaled by the per-side
        // envelope, so the L/R balance of the modulator drives the
        // L/R balance of the vocoded output.
        float vocodedL = 0.0f, vocodedR = 0.0f;
        float unvoicedAccum = 0.0f;     // mono — sums |modL|+|modR| above 2/3 band

        for (int b = 0; b < numBands; ++b) {
            auto& bs = m_bandState[b];

            const float bandL = biquadProcess(modL, bs.modBQ_L);
            const float bandR = biquadProcess(modR, bs.modBQ_R);
            const float absL  = std::abs(bandL);
            const float absR  = std::abs(bandR);

            // Envelope followers (per side). Skipped when Freeze is
            // engaged so the previous block's envelope levels persist
            // — that's the entire effect: hold whatever spectral
            // envelope the modulator was painting at the moment of
            // freeze.
            if (!freeze) {
                if (absL > bs.envLevel_L)
                    bs.envLevel_L = atkCoeff * bs.envLevel_L + (1.0f - atkCoeff) * absL;
                else
                    bs.envLevel_L = relCoeff * bs.envLevel_L + (1.0f - relCoeff) * absL;

                if (absR > bs.envLevel_R)
                    bs.envLevel_R = atkCoeff * bs.envLevel_R + (1.0f - atkCoeff) * absR;
                else
                    bs.envLevel_R = relCoeff * bs.envLevel_R + (1.0f - relCoeff) * absR;
            }

            const float carrBand = biquadProcess(carrierSig, bs.carrBQ);
            const float gT = tiltGains[b] * bandGain;   // see bandGain comment
            vocodedL += carrBand * bs.envLevel_L * gT;
            vocodedR += carrBand * bs.envLevel_R * gT;

            if (b >= numBands * 2 / 3) unvoicedAccum += (absL + absR) * 0.5f;

            // Track this band's peak envelope for the UI spectrum
            // display. max(L,R) — we want the visualisation to track
            // whichever side has signal so a panned modulator still
            // shows up on the bars rather than averaging to half.
            const float bandLvl = std::max(bs.envLevel_L, bs.envLevel_R);
            if (bandLvl > blockBandPeak[b]) blockBandPeak[b] = bandLvl;
        }

        // Unvoiced/sibilant noise injection — independent random
        // values per channel keep the noise tail stereo-decorrelated
        // (otherwise you'd get a phantom-centre sibilance even on a
        // hard-panned vocal).
        if (unvoicedSens > 0.0f) {
            const float n0 = (randFloat() - 0.5f) * 2.0f;
            const float n1 = (randFloat() - 0.5f) * 2.0f;
            const float gain = unvoicedAccum * unvoicedSens * 0.5f;
            vocodedL += n0 * gain;
            vocodedR += n1 * gain;
        }

        if (dryCarrier > 0.0f) {
            vocodedL += carrierSig * dryCarrier;
            vocodedR += carrierSig * dryCarrier;
        }

        // Output low-pass filter — independent state per side so L/R
        // don't bleed through the integrator memory.
        if (filterCut < 19999.0f) {
            {
                float v3 = vocodedL - m_outFilterIcL[1];
                float v1 = fa1 * m_outFilterIcL[0] + fa2 * v3;
                float v2 = m_outFilterIcL[1] + fa2 * m_outFilterIcL[0] + fa3 * v3;
                m_outFilterIcL[0] = 2.0f * v1 - m_outFilterIcL[0];
                m_outFilterIcL[1] = 2.0f * v2 - m_outFilterIcL[1];
                vocodedL = v2;
            }
            {
                float v3 = vocodedR - m_outFilterIcR[1];
                float v1 = fa1 * m_outFilterIcR[0] + fa2 * v3;
                float v2 = m_outFilterIcR[1] + fa2 * m_outFilterIcR[0] + fa3 * v3;
                m_outFilterIcR[0] = 2.0f * v1 - m_outFilterIcR[0];
                m_outFilterIcR[1] = 2.0f * v2 - m_outFilterIcR[1];
                vocodedR = v2;
            }
        }

        const float outL = vocodedL * volume;
        const float outR = vocodedR * volume;
        buffer[s * numChannels] += outL;
        if (stereoOut)
            buffer[s * numChannels + 1] += outR;
    }

    // CAS-merge the block's modulator peak into the atomic so the UI
    // meter reads "max since last poll". Cheap — one atomic op per
    // block, not per sample.
    float prev = m_modLevelPeak.load(std::memory_order_relaxed);
    while (blockModPeak > prev) {
        if (m_modLevelPeak.compare_exchange_weak(
                prev, blockModPeak,
                std::memory_order_release,
                std::memory_order_relaxed))
            break;
    }

    // Same pattern for the per-band peaks driving the spectrum
    // display. One CAS per active band per block — even at 32 bands
    // and a tiny block size that's well under the noise compared to
    // the per-sample biquad work.
    for (int b = 0; b < numBands; ++b) {
        float bp = m_bandPeak[b].load(std::memory_order_relaxed);
        while (blockBandPeak[b] > bp) {
            if (m_bandPeak[b].compare_exchange_weak(
                    bp, blockBandPeak[b],
                    std::memory_order_release,
                    std::memory_order_relaxed))
                break;
        }
    }
}

} // namespace yawn::instruments
