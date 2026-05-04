#include "instruments/ElectricPiano.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

void ElectricPiano::init(double sampleRate, int maxBlockSize) {
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices)
        v.ampEnv.setSampleRate(sampleRate);
    applyDefaults();
    reset();
}

void ElectricPiano::reset() {
    for (auto& v : m_voices) {
        v.active     = false;
        v.carPhase   = 0.0;
        v.modPhase   = 0.0;
        v.hammerAmp  = 0.0f;
        v.hammerLpZ  = 0.0f;
        v.ampEnv.reset();
    }
    m_voiceCounter = 0;
    m_pitchBend    = 0.0f;
    m_tremPhase    = 0.0;
}

void ElectricPiano::process(float* buffer, int numFrames, int numChannels,
                             const midi::MidiBuffer& midi) {
    // ── MIDI ───────────────────────────────────────────────────────
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

    const auto mi = modeInfo();

    // Pitch-bend ±2 semitones — applied per-block, fine for an EP
    // (no need for per-sample bend interpolation).
    const float bendMul = std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);

    // Hammer transient: per-sample multiplier. ~30 ms half-life for
    // a snappy "thock". e^(-t/tau) per sample → coef = exp(-1 / (sr*tau)).
    const float hammerTau = 0.030f;
    const float hammerCoef = std::exp(
        -1.0f / static_cast<float>(m_sampleRate * hammerTau));
    // Cheap one-pole LP for the noise — heavily smoothed so the
    // transient reads as a felt-mallet "thump" instead of a snare-
    // like white-noise burst that would clash with the carrier
    // pitch and make the patch feel out of tune.
    const float lpCoef = 0.08f;

    // Tremolo per-sample increment (cycles per sample).
    const double tremStep = static_cast<double>(m_tremRate) / m_sampleRate;

    // ── Per-sample render loop ─────────────────────────────────────
    for (int s = 0; s < numFrames; ++s) {
        float voiceMixL = 0.0f;
        float voiceMixR = 0.0f;

        for (int vi = 0; vi < kMaxVoices; ++vi) {
            auto& v = m_voices[vi];
            if (!v.active) continue;

            // Per-voice modulation index — built up from three
            // terms inside the mode-specific range:
            //   * idxBase   — minimum index even at vel 0
            //   * Bell      — constant brightness offset (vel-independent)
            //   * Hardness × vel² — velocity-driven bell shimmer
            //
            // Both Bell and Hardness saturate at idxRange so the
            // mod index can never exceed the mode's safe ceiling
            // (~0.9 for Rhodes/Suitcase, ~2.8 for Wurli). This is
            // why the patch sounds in-tune now — the fundamental
            // stays loud relative to the FM sidebands.
            const float velIdxScale = v.velocity * v.velocity;
            const float modIdx = mi.idxBase + mi.idxRange *
                                  (m_bellMix * 0.5f +
                                   m_hardness * velIdxScale * 0.7f);

            // FM: y = sin(2π·carPhase + modIdx · sin(2π·modPhase))
            const float modSig = static_cast<float>(
                std::sin(2.0 * M_PI * v.modPhase));
            const float carSig = static_cast<float>(
                std::sin(2.0 * M_PI * v.carPhase + modIdx * modSig));

            // Advance phases. Use the cached freqs scaled by pitch-bend.
            const float carHz = v.carFreqHz * bendMul;
            const float modHz = v.modFreqHz * bendMul;
            v.carPhase += carHz / m_sampleRate;
            v.modPhase += modHz / m_sampleRate;
            if (v.carPhase >= 1.0) v.carPhase -= 1.0;
            if (v.modPhase >= 1.0) v.modPhase -= 1.0;

            // Hammer transient — a quick decaying noise burst summed
            // into the voice signal. Decoupled from the carrier so it
            // reads as a percussive "thock" on top of the tonal body.
            float thock = 0.0f;
            if (v.hammerAmp > 1.0e-4f) {
                const float n = nextNoise();
                v.hammerLpZ += lpCoef * (n - v.hammerLpZ);
                thock = v.hammerLpZ * v.hammerAmp;
                v.hammerAmp *= hammerCoef;
            }

            const float env = v.ampEnv.process();
            const float voiceSig = (carSig + thock) * env * v.velocity;

            // No per-voice pan yet — collapse to mono per-voice and
            // let the post-mix tremolo handle stereo movement.
            voiceMixL += voiceSig;
            voiceMixR += voiceSig;

            if (v.ampEnv.isIdle()) v.active = false;
        }

        // ── Tremolo (post-mix). Pan tremolo for Rhodes/Suitcase,
        // amp tremolo for Wurli. Pan tremolo uses an equal-power
        // panning curve so the perceived loudness stays roughly
        // constant as the signal walks left↔right.
        const float lfo = static_cast<float>(std::sin(2.0 * M_PI * m_tremPhase));
        float gL = 1.0f, gR = 1.0f;
        if (m_tremDepth > 1.0e-4f) {
            if (mi.tremType == 0) {
                // Pan tremolo. Map LFO -1..+1 → pan -depth..+depth,
                // then equal-power: gL = cos(angle), gR = sin(angle)
                // with angle = (pan + 1)/2 · π/2.
                const float pan   = lfo * m_tremDepth;
                const float angle = (pan + 1.0f) * 0.25f *
                                     static_cast<float>(M_PI);
                gL = std::cos(angle) * 1.4142f;   // √2 normalisation so
                gR = std::sin(angle) * 1.4142f;   // centre = unity gain
            } else {
                // Amp tremolo. Loudness wobble synchronised on both
                // channels — depth=1 → 0..2 swing (mute on min). The
                // (1 + lfo) shift keeps the signal positive.
                const float amp = 1.0f - m_tremDepth * 0.5f * (1.0f + lfo);
                gL = gR = amp;
            }
        }

        const float outL = voiceMixL * gL * m_volume * 0.5f;
        const float outR = voiceMixR * gR * m_volume * 0.5f;

        buffer[s * numChannels + 0] += outL;
        if (numChannels > 1)
            buffer[s * numChannels + 1] += outR;

        m_tremPhase += tremStep;
        if (m_tremPhase >= 1.0) m_tremPhase -= 1.0;
    }
}

void ElectricPiano::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active     = true;
    v.note       = note;
    v.channel    = ch;
    v.velocity   = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;
    // Fresh phases — keeps each strike's transient consistent
    // (otherwise the modulator's running phase would change the
    // initial timbre on retrigger).
    v.carPhase = 0.0;
    v.modPhase = 0.0;

    const auto mi = modeInfo();
    v.carFreqHz = noteToFreq(note);
    v.modFreqHz = v.carFreqHz * mi.ratio;

    // Hammer amp scales with velocity AND mode (Wurli has a more
    // pronounced strike). Squared velocity for a more "playable"
    // curve — soft hits aren't dominated by hammer, hard hits get
    // the percussive thump.
    v.hammerAmp = m_hammer * mi.hammerScale *
                   v.velocity * v.velocity * 1.5f;
    v.hammerLpZ = 0.0f;

    // EP-style envelope: fast attack, immediate exponential decay
    // (sustain held at 0 so the note rolls naturally to silence).
    // Release applies on noteOff. Real EPs have no sustain stage —
    // the tine's energy just dissipates.
    v.ampEnv.setADSR(m_attack, m_decay, 0.0f, m_release);
    v.ampEnv.gate(true);
}

void ElectricPiano::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch)
            v.ampEnv.gate(false);
}

int ElectricPiano::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

void ElectricPiano::updateEnvelopes() {
    for (auto& v : m_voices)
        v.ampEnv.setADSR(m_attack, m_decay, 0.0f, m_release);
}

void ElectricPiano::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

} // namespace instruments
} // namespace yawn
