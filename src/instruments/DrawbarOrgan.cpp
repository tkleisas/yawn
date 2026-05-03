#include "instruments/DrawbarOrgan.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

constexpr float DrawbarOrgan::kHarmonicRatio[DrawbarOrgan::kDrawbars];

void DrawbarOrgan::init(double sampleRate, int maxBlockSize) {
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) {
        v.ampEnv.setSampleRate(sampleRate);
        // Organ amp env: very fast attack/release, full sustain so
        // notes hold their level for as long as the key is down —
        // exactly how a real Hammond behaves (no decay phase).
        v.ampEnv.setADSR(0.005f, 0.001f, 1.0f, 0.04f);
    }
    m_percEnv.setSampleRate(sampleRate);
    updatePercEnv();
    applyDefaults();
    reset();
}

void DrawbarOrgan::reset() {
    for (auto& v : m_voices) {
        v.active = false;
        for (auto& p : v.phase) p = 0.0;
        v.ampEnv.reset();
    }
    m_voiceCounter = 0;
    m_pitchBend    = 0.0f;
    m_percActive   = false;
    m_percPhase    = 0.0;
    m_percEnv.reset();
    m_clickLevel   = 0.0f;
    std::memset(m_vibBuf, 0, sizeof(m_vibBuf));
    m_vibWrite     = 0;
    m_vibPhase     = 0.0;
}

void DrawbarOrgan::process(float* buffer, int numFrames, int numChannels,
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
            m_percActive = false;
        }
        else if (msg.type == midi::MidiMessage::Type::PitchBend)
            m_pitchBend = midi::Convert::pb32toFloat(msg.value);
    }

    // ── Pitch-bend multiplier (±2 semitones).
    const float bendMul = std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);

    // ── Pre-compute per-voice / per-drawbar phase increments. The
    // per-sample render loop is hot, so do all the freq math up
    // front. Each drawbar's phase advances by ratio · baseHz / SR
    // every sample.
    struct VoiceInc { double inc[kDrawbars]; bool active; };
    VoiceInc vinc[kMaxVoices];
    for (int vi = 0; vi < kMaxVoices; ++vi) {
        vinc[vi].active = m_voices[vi].active;
        if (!vinc[vi].active) continue;
        const double baseHz = noteToFreq(m_voices[vi].note) * bendMul;
        for (int d = 0; d < kDrawbars; ++d)
            vinc[vi].inc[d] = baseHz * kHarmonicRatio[d] / m_sampleRate;
    }

    const double percInc = m_percFreq / m_sampleRate;

    // ── Vibrato/chorus geometry. A 6.7 Hz sine LFO moves the read
    // tap between [base − maxDepth, base + maxDepth] samples behind
    // the write head. Base is well above maxDepth so the tap never
    // wraps past the write position.
    const float maxDepthMs = 3.5f;
    const float baseMs     = 5.0f;
    const float msPerSample = 1000.0f / static_cast<float>(m_sampleRate);
    const float baseSamples = baseMs / msPerSample;
    const float depthSamples = (maxDepthMs * m_vibDepth) / msPerSample;

    const float percVolMul = m_percNormal ? 1.0f : 0.7f;

    // ── Per-sample render loop ──────────────────────────────────────
    for (int i = 0; i < numFrames; ++i) {
        float voiceMix = 0.0f;

        // 1) Voices — sum 9 weighted sines per active voice.
        for (int vi = 0; vi < kMaxVoices; ++vi) {
            auto& voice = m_voices[vi];
            if (!voice.active) continue;
            float voiceSum = 0.0f;
            for (int d = 0; d < kDrawbars; ++d) {
                if (m_drawbar[d] < 1.0e-4f) {
                    // Still advance the phase so the sine stays
                    // continuous if the drawbar is pulled back in.
                    voice.phase[d] += vinc[vi].inc[d];
                    if (voice.phase[d] >= 1.0) voice.phase[d] -= 1.0;
                    continue;
                }
                voiceSum += static_cast<float>(
                    std::sin(2.0 * M_PI * voice.phase[d])) * m_drawbar[d];
                voice.phase[d] += vinc[vi].inc[d];
                if (voice.phase[d] >= 1.0) voice.phase[d] -= 1.0;
            }
            const float env = voice.ampEnv.process();
            // Drawbars sum into a tall signal at 8-stops-out — scale
            // by 1/kDrawbars so a fully-engaged registration doesn't
            // clip. Velocity is light influence so users hear their
            // touch but the organ stays "on" sounding.
            const float voiceOut = (voiceSum / static_cast<float>(kDrawbars)) *
                                   env * (0.5f + 0.5f * voice.velocity);
            voiceMix += voiceOut;
            if (voice.ampEnv.isIdle()) voice.active = false;
        }

        // 2) Percussion — single shared sine + AR envelope.
        if (m_percActive) {
            voiceMix += static_cast<float>(
                std::sin(2.0 * M_PI * m_percPhase)) *
                m_percEnv.process() * percVolMul;
            m_percPhase += percInc;
            if (m_percPhase >= 1.0) m_percPhase -= 1.0;
            if (m_percEnv.isIdle()) m_percActive = false;
        }

        // 3) Key click — exponential noise decay.
        if (m_clickLevel > 1.0e-5f) {
            m_noiseRng ^= m_noiseRng << 13;
            m_noiseRng ^= m_noiseRng >> 17;
            m_noiseRng ^= m_noiseRng << 5;
            const float noise = static_cast<float>(m_noiseRng & 0xFFFF) /
                                32768.0f - 1.0f;
            voiceMix += noise * m_clickLevel * m_keyClickLevel;
            m_clickLevel *= m_clickDecay;
        }

        // 4) Vibrato/chorus — write into delay line, read at LFO-
        // modulated tap. When chorusOn is true, mix dry + modulated;
        // otherwise the modulated signal replaces dry (pure
        // vibrato). When vibDepth is 0, the modulated tap is at a
        // constant 5ms latency — inaudible offset that we just live
        // with for code simplicity.
        m_vibBuf[m_vibWrite] = voiceMix;

        const double lfo = std::sin(2.0 * M_PI * m_vibPhase);
        const float  delaySamples = baseSamples +
                                    static_cast<float>(lfo) * depthSamples;
        float readPos = static_cast<float>(m_vibWrite) - delaySamples;
        while (readPos < 0.0f)             readPos += kVibBufFrames;
        while (readPos >= kVibBufFrames)   readPos -= kVibBufFrames;
        const int   i0 = static_cast<int>(readPos);
        const int   i1 = (i0 + 1) % kVibBufFrames;
        const float frac = readPos - i0;
        const float modulated = m_vibBuf[i0] * (1.0f - frac) +
                                m_vibBuf[i1] * frac;

        float postVib;
        if (m_chorusOn) {
            postVib = (voiceMix + modulated) * 0.5f;
        } else if (m_vibDepth > 1.0e-4f) {
            postVib = modulated;
        } else {
            postVib = voiceMix;
        }

        m_vibWrite = (m_vibWrite + 1) % kVibBufFrames;
        m_vibPhase += static_cast<double>(kVibRateHz) / m_sampleRate;
        if (m_vibPhase >= 1.0) m_vibPhase -= 1.0;

        // 5) Output (mono → stereo duplicate; rotary effect adds
        // the spatial movement when wired up).
        const float out = postVib * m_volume;
        buffer[i * numChannels + 0] += out;
        if (numChannels > 1)
            buffer[i * numChannels + 1] += out;
    }
}

void DrawbarOrgan::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    // Single-trigger percussion: only fires when no other note is
    // currently held — the famous "drains" behaviour. Check BEFORE
    // claiming a voice slot for the new note.
    const bool fresh = (activeVoiceCount() == 0);

    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active     = true;
    v.note       = note;
    v.channel    = ch;
    v.velocity   = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;
    for (auto& p : v.phase) p = 0.0;
    v.ampEnv.gate(true);

    // Trigger key-click noise burst — short exponential decay
    // (~5ms time-constant). Decay coefficient picked so amplitude
    // halves every ~3.5ms at 48k.
    m_clickLevel = 0.5f;
    m_clickDecay = std::exp(-1.0f / (0.005f * static_cast<float>(m_sampleRate)));

    // Trigger percussion when allowed.
    if (m_percOn && fresh) {
        m_percActive = true;
        m_percPhase  = 0.0;
        const float harmRatio = m_percThirdHarm ? 3.0f : 2.0f;
        m_percFreq = noteToFreq(note) * harmRatio;
        m_percEnv.gate(true);
    }
}

void DrawbarOrgan::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch)
            v.ampEnv.gate(false);
}

int DrawbarOrgan::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

int DrawbarOrgan::activeVoiceCount() const {
    int n = 0;
    for (const auto& v : m_voices)
        if (v.active && v.ampEnv.stage() != Envelope::Release) ++n;
    return n;
}

void DrawbarOrgan::updatePercEnv() {
    // AR envelope: instant attack (~1ms), exponential decay to zero.
    // "Short" ≈ 150ms time-constant, "Long" ≈ 400ms — the two
    // settings on the real B-3 percussion panel.
    const float decayS = m_percLong ? 0.4f : 0.15f;
    m_percEnv.setADSR(0.001f, decayS, 0.0f, 0.001f);
}

void DrawbarOrgan::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

} // namespace instruments
} // namespace yawn
