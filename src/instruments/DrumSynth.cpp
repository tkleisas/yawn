#include "DrumSynth.h"
#include "util/Logger.h"
#include <cmath>
#include <cstring>

namespace yawn {
namespace instruments {

// ───────────────────────────────────────────────────────────────────
// Per-drum auto-pan. Classic pop/rock kit positioning seen from
// the drummer's perspective: kick centred, snare near-centred, toms
// spread L→R, hi-hats slightly right (drummer-right), tambourine
// right, clap left for contrast. Predictable enough that the mixer
// doesn't surprise users; can still be overridden via track-level pan.
// ───────────────────────────────────────────────────────────────────
static constexpr float kDrumPan[8] = {
    0.0f,   // Kick
    -0.05f, // Snare
    -0.40f, // Clap (left to balance hi-hat right)
    -0.25f, // Tom1
    +0.30f, // ClosedHH
    +0.30f, // OpenHH
    +0.25f, // Tom2
    +0.45f, // Tambourine
};

// Hi-hat metallic frequencies (TR-808-ish ratios). Multiplied by
// the per-voice tune ratio so the Tune knob shifts the whole
// metallic spectrum coherently rather than detuning individual
// partials. Scaled inside renderHiHat to per-drum tune range.
static constexpr float kHiHatRatios[6] = {
    1.0f, 1.342f, 1.658f, 1.974f, 2.456f, 2.835f
};

// Tambourine "jingle" frequencies — pairs of high tuned partials
// give the metallic shake character. Same ratio-times-base scheme.
static constexpr float kTambRatios[4] = {
    1.0f, 1.193f, 1.486f, 1.738f
};

void DrumSynth::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (int i = 0; i < kParamCount; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
    // Pre-size scratch buffers against the host contract. Audio
    // thread does no allocation thereafter — it only writes to the
    // fixed-capacity vectors.
    m_scratchL.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    m_scratchR.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    m_oversizedBlockWarned = false;
    reset();
    LOG_INFO("DrumSynth", "init sr=%.0f maxBlockSize=%d",
             sampleRate, maxBlockSize);
}

void DrumSynth::reset() {
    for (auto& v : m_voices) v = DrumVoice{};
    m_rngState = 0xC0FFEEu;
    std::memset(m_pinkRows, 0, sizeof(m_pinkRows));
    m_pinkSum = 0.0f;
    m_pinkCounter = 0;
}

float DrumSynth::pinkNoise() {
    // Voss-McCartney pink noise approximation. Cheap, ~3 dB/oct
    // tilt — close enough to pink for the kick-thump tail role
    // we use it for. Each row updates at 1/2^row sample period.
    int idx = ++m_pinkCounter;
    int row = 0;
    while ((idx & 1) == 0 && row < 5) { idx >>= 1; ++row; }
    if (row < 5) {
        m_pinkSum -= m_pinkRows[row];
        m_pinkRows[row] = randFloat() - 0.5f;
        m_pinkSum += m_pinkRows[row];
    }
    return m_pinkSum * 0.5f;
}

void DrumSynth::noteOn(int slot, float velocity) {
    if (slot < 0 || slot >= kNumDrums) return;
    auto& v = m_voices[slot];
    v.active = true;
    v.justTriggered = true;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.ampLevel = 0.0f;
    v.stage = 0;            // Attack
    v.ageSamples = 0;
    v.phase0 = 0.0;
    for (auto& p : v.phases) p = 0.0;
    v.bpZ1 = v.bpZ2 = 0.0f;

    LOG_DEBUG("DrumSynth", "noteOn slot=%d note=%d vel=%.2f",
              slot, kDrumNotes[slot], v.velocity);

    // Auto-choke: triggering ClosedHH cuts OpenHH — matches how a
    // hi-hat foot pedal mutes a previously-open ride. The reverse
    // (open hat triggers don't choke a closed-hat ring) is the
    // natural physical behaviour, and a 60-ms-decay closed hat is
    // already inaudible by the time you'd notice a missing choke,
    // so we don't bother. Other choke groups are uncommon enough to
    // skip — kits with multiple cymbals can wire it through MIDI
    // effects.
    if (slot == ClosedHH) m_voices[OpenHH].active = false;
}

void DrumSynth::noteOff(int /*slot*/) {
    // Drums are one-shot. Note-off doesn't gate them — the envelope
    // releases on its own decay. Holding a key has no effect beyond
    // the initial trigger; that matches every drum machine in
    // existence and sidesteps weird edge cases with sustained kicks.
}

// ───────────────────────────────────────────────────────────────────
// Render helpers. Each operates on one voice and adds its output
// (with auto-pan) to the L/R buffers for `n` samples. The voice's
// envelope and phase state advances inside the loop; if the
// envelope crosses below the deactivation threshold the voice goes
// inactive mid-buffer (no further work).
//
// Envelope shape: linear attack toward 1.0 (target reached in
// `attackMs` real-time), then exponential decay toward 0 with
// time-constant `decayMs`. We don't bother with a sustain stage —
// drums always release once they fire.
// ───────────────────────────────────────────────────────────────────

void DrumSynth::renderKick(DrumVoice& v, float* outL, float* outR, int n) {
    const float baseHz   = tuneToHz(m_params[pKickTune], 30.0f, 120.0f);
    const float atkMs    = m_params[pKickAttack];
    const float decMs    = m_params[pKickDecay];
    const float sineAmt  = m_params[pKickSine];
    const float whiteAmt = m_params[pKickWhite];
    const float pinkAmt  = m_params[pKickPink];
    const float drv      = m_params[pKickDrive];

    const float sr       = static_cast<float>(m_sampleRate);
    const float atkInc   = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
    const float decCoef  = std::exp(-1.0f / std::max(0.001f, decMs * 0.001f * sr));
    // Pitch sweep: drops from baseHz × ~3 down to baseHz over ~30 ms.
    // Gives the kick its "thump" — the click-to-tone transition is
    // basically the whole kick character. Kept independent of the
    // amp decay so even very long kicks have a tight pitch attack.
    const float pitchTau = std::max(0.001f, 0.030f * sr);
    const float pitchCoef = std::exp(-1.0f / pitchTau);

    const float pL = panL(kDrumPan[Kick]);
    const float pR = panR(kDrumPan[Kick]);

    for (int i = 0; i < n; ++i) {
        // Envelope
        if (v.stage == 0) {
            v.ampLevel += atkInc;
            if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.stage = 1; }
        } else {
            v.ampLevel *= decCoef;
            if (v.ampLevel < 0.0001f) { v.active = false; return; }
        }
        // Pitch sweep — pitchEnv is reused via phase0's own role; we
        // store the sweep multiplier in v.phases[0] so we don't need
        // an extra DrumVoice field. White-noise click decays much
        // faster than the body — the first ~5 ms gives the kick its
        // initial transient (phases[1]).
        if (v.justTriggered) {
            v.phases[0] = 1.0f;
            v.phases[1] = 1.0f;
            v.justTriggered = false;
        } else {
            v.phases[0] *= pitchCoef;
            v.phases[1] *= 0.9985f;
        }
        const float pitchMul = 1.0f + 2.0f * static_cast<float>(v.phases[0]);
        const double freq = baseHz * pitchMul;
        v.phase0 += freq / m_sampleRate;
        if (v.phase0 >= 1.0) v.phase0 -= 1.0;

        const float sine = std::sin(static_cast<float>(v.phase0) * 6.2831853f);
        const float white = (randFloat() - 0.5f) * 2.0f
                           * static_cast<float>(v.phases[1]);
        const float pink  = pinkNoise() * 2.0f;

        float out = sineAmt  * sine
                  + whiteAmt * white
                  + pinkAmt  * pink * v.ampLevel;   // pink rides amp env
        out *= v.ampLevel * v.velocity;
        out = drive(out, drv);

        outL[i] += out * pL;
        outR[i] += out * pR;
        ++v.ageSamples;
    }
}

void DrumSynth::renderSnare(DrumVoice& v, float* outL, float* outR, int n) {
    const float baseHz = tuneToHz(m_params[pSnareTune], 120.0f, 350.0f);
    const float atkMs  = m_params[pSnareAttack];
    const float decMs  = m_params[pSnareDecay];
    const float drv    = m_params[pSnareDrive];

    const float sr       = static_cast<float>(m_sampleRate);
    const float atkInc   = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
    const float decCoef  = std::exp(-1.0f / std::max(0.001f, decMs * 0.001f * sr));

    // Bandpass biquad for the noise tail, centred around baseHz × 4
    // (the snare wires resonate ~3–8× the body fundamental). Direct-
    // Form-II Transposed — z1/z2 stored on the voice so the filter
    // memory survives across blocks.
    const float bpHz = std::min(baseHz * 4.0f, sr * 0.45f);
    const float w0   = 2.0f * 3.14159265f * bpHz / sr;
    const float sinW = std::sin(w0), cosW = std::cos(w0);
    const float alpha = sinW / (2.0f * 1.5f);   // Q ≈ 1.5
    const float a0 = 1.0f + alpha;
    const float b0 = (sinW * 0.5f) / a0;
    // BPF: b1 = 0, b2 = -b0
    const float a1 = (-2.0f * cosW) / a0;
    const float a2 = (1.0f - alpha) / a0;

    const float pL = panL(kDrumPan[Snare]);
    const float pR = panR(kDrumPan[Snare]);

    for (int i = 0; i < n; ++i) {
        if (v.stage == 0) {
            v.ampLevel += atkInc;
            if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.stage = 1; }
        } else {
            v.ampLevel *= decCoef;
            if (v.ampLevel < 0.0001f) { v.active = false; return; }
        }
        // Body: triangle at baseHz with fast pitch decay (~25 ms)
        if (v.justTriggered) {
            v.phases[0] = 1.0f;
            v.justTriggered = false;
        } else {
            v.phases[0] *= 0.9982f;
        }
        const double freq = baseHz * (1.0f + static_cast<float>(v.phases[0]) * 0.5f);
        v.phase0 += freq / m_sampleRate;
        if (v.phase0 >= 1.0) v.phase0 -= 1.0;
        const float t = static_cast<float>(v.phase0);
        const float body = (t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t);

        // Noise through bandpass — DF-II Transposed:
        //   y[n] = b0*x[n] + z1[n-1]
        //   z1   = b1*x[n] - a1*y + z2[n-1]   (b1=0 here)
        //   z2   = b2*x[n] - a2*y             (b2 = -b0)
        const float x = (randFloat() - 0.5f) * 2.0f;
        const float y = b0 * x + v.bpZ1;
        v.bpZ1 = -a1 * y + v.bpZ2;
        v.bpZ2 = -b0 * x - a2 * y;

        float out = 0.35f * body * static_cast<float>(v.phases[0]) + 0.65f * y;
        out *= v.ampLevel * v.velocity;
        out = drive(out, drv);

        outL[i] += out * pL;
        outR[i] += out * pR;
        ++v.ageSamples;
    }
}

void DrumSynth::renderClap(DrumVoice& v, float* outL, float* outR, int n) {
    const float baseHz = tuneToHz(m_params[pClapTune], 800.0f, 2400.0f);
    const float atkMs  = m_params[pClapAttack];
    const float decMs  = m_params[pClapDecay];
    const float drv    = m_params[pClapDrive];

    const float sr      = static_cast<float>(m_sampleRate);
    const float atkInc  = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
    const float decCoef = std::exp(-1.0f / std::max(0.001f, decMs * 0.001f * sr));

    // Three quick bursts ~10 ms apart, then a longer noise tail.
    // burstSamples lists trigger-relative sample positions where
    // the burst-envelope re-arms back to ~1.0; between bursts the
    // burst envelope decays at ~3 ms time-constant. The result is
    // a textured "kha-kha-kha-shhh" classic clap shape.
    const float burstTau = std::max(0.001f, 0.003f * sr);
    const float burstCoef = std::exp(-1.0f / burstTau);
    const int burstSamples[3] = {
        static_cast<int>(0.010f * sr),
        static_cast<int>(0.020f * sr),
        static_cast<int>(0.030f * sr),
    };

    const float pL = panL(kDrumPan[Clap]);
    const float pR = panR(kDrumPan[Clap]);

    for (int i = 0; i < n; ++i) {
        if (v.stage == 0) {
            v.ampLevel += atkInc;
            if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.stage = 1; }
        } else {
            v.ampLevel *= decCoef;
            if (v.ampLevel < 0.0001f) { v.active = false; return; }
        }
        // Burst envelope — phases[0] is the burst level. The clap's
        // signature "kha-kha-kha-shhh" texture comes from re-arming
        // this envelope to 1.0 at three preset sample offsets after
        // trigger, then letting it decay between bursts. After the
        // last burst, the noise-tail floor of 0.55 keeps the body
        // audible until the amp envelope fades out.
        if (v.justTriggered) {
            v.phases[0] = 1.0f;     // first burst is at age=0
            v.justTriggered = false;
        }
        for (int b = 0; b < 3; ++b)
            if (v.ageSamples == burstSamples[b]) v.phases[0] = 1.0f;
        v.phases[0] *= burstCoef;
        const float burstAmp = std::max(static_cast<float>(v.phases[0]), 0.55f);

        // Noise centred via simple two-pole BP approximation — same
        // simplified DF-I as the snare, accuracy not critical.
        const float n_ = (randFloat() - 0.5f) * 2.0f;
        v.bpZ1 = 0.92f * v.bpZ1 + 0.08f * n_;     // soft lowpass
        const float shaped = (n_ - v.bpZ1) * 0.7f;  // highpass component

        // Centre frequency tilt — multiply by sine of base wraparound
        // to shift the noise spectrum up; modest effect.
        v.phase0 += baseHz / m_sampleRate;
        if (v.phase0 >= 1.0) v.phase0 -= 1.0;
        const float tilt = std::sin(static_cast<float>(v.phase0) * 6.2831853f) * 0.3f + 0.7f;

        float out = shaped * tilt * burstAmp * v.ampLevel * v.velocity;
        out = drive(out, drv);
        outL[i] += out * pL;
        outR[i] += out * pR;
        ++v.ageSamples;
    }
}

void DrumSynth::renderTom(DrumVoice& v, float* outL, float* outR, int n,
                           int slot) {
    const int pBase = (slot == Tom1) ? pTom1Tune : pTom2Tune;
    // Tom1 sits low, Tom2 high — the tune ranges differ accordingly.
    const float lo = (slot == Tom1) ? 60.0f  : 110.0f;
    const float hi = (slot == Tom1) ? 220.0f : 380.0f;
    const float baseHz = tuneToHz(m_params[pBase + 0], lo, hi);
    const float atkMs  = m_params[pBase + 1];
    const float decMs  = m_params[pBase + 2];
    const float drv    = m_params[pBase + 3];

    const float sr      = static_cast<float>(m_sampleRate);
    const float atkInc  = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
    const float decCoef = std::exp(-1.0f / std::max(0.001f, decMs * 0.001f * sr));
    const float pitchCoef = std::exp(-1.0f / std::max(0.001f, 0.060f * sr));

    const float pL = panL(kDrumPan[slot]);
    const float pR = panR(kDrumPan[slot]);

    for (int i = 0; i < n; ++i) {
        if (v.stage == 0) {
            v.ampLevel += atkInc;
            if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.stage = 1; }
        } else {
            v.ampLevel *= decCoef;
            if (v.ampLevel < 0.0001f) { v.active = false; return; }
        }
        if (v.justTriggered) {
            v.phases[0] = 1.0f;
            v.justTriggered = false;
        } else {
            v.phases[0] *= pitchCoef;
        }
        const double freq = baseHz * (1.0f + static_cast<float>(v.phases[0]) * 0.4f);
        v.phase0 += freq / m_sampleRate;
        if (v.phase0 >= 1.0) v.phase0 -= 1.0;

        const float sine = std::sin(static_cast<float>(v.phase0) * 6.2831853f);
        // Subtle noise body so it doesn't sound like a pure sine.
        const float thump = (randFloat() - 0.5f) * 0.10f;
        float out = (sine + thump) * v.ampLevel * v.velocity;
        out = drive(out, drv);
        outL[i] += out * pL;
        outR[i] += out * pR;
        ++v.ageSamples;
    }
}

void DrumSynth::renderHiHat(DrumVoice& v, float* outL, float* outR, int n,
                             bool open, int slot) {
    const int pBase = open ? pOHHTune : pCHHTune;
    const float baseHz = tuneToHz(m_params[pBase + 0], 6000.0f, 14000.0f);
    const float atkMs  = m_params[pBase + 1];
    const float decMs  = m_params[pBase + 2];
    const float drv    = m_params[pBase + 3];

    const float sr      = static_cast<float>(m_sampleRate);
    const float atkInc  = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
    const float decCoef = std::exp(-1.0f / std::max(0.001f, decMs * 0.001f * sr));

    const float pL = panL(kDrumPan[slot]);
    const float pR = panR(kDrumPan[slot]);

    // Pre-compute per-partial increments. baseHz is the lowest
    // partial; the others are scaled up by kHiHatRatios. Keeps the
    // metallic spectrum coherent under the Tune knob.
    double inc[6];
    for (int p = 0; p < 6; ++p)
        inc[p] = baseHz * kHiHatRatios[p] / m_sampleRate;

    // Hi-hat doesn't use phases[0] as an envelope (oscillator phases
    // start at 0 and don't need first-sample initialisation), but we
    // still clear justTriggered for consistency — tests assert it's
    // false once any render has run.
    v.justTriggered = false;

    for (int i = 0; i < n; ++i) {
        if (v.stage == 0) {
            v.ampLevel += atkInc;
            if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.stage = 1; }
        } else {
            v.ampLevel *= decCoef;
            if (v.ampLevel < 0.0001f) { v.active = false; return; }
        }
        // Sum 6 squares — produces an inharmonic metallic spectrum.
        // Square-wave sums create the classic 808 hi-hat colour.
        float sum = 0.0f;
        for (int p = 0; p < 6; ++p) {
            v.phases[p] += inc[p];
            if (v.phases[p] >= 1.0) v.phases[p] -= 1.0;
            sum += (v.phases[p] < 0.5) ? 1.0f : -1.0f;
        }
        sum *= 0.16f;   // 1/6 with some headroom

        // High-pass via leaky-integrator subtraction (cheap one-pole HP).
        // bpZ1 holds running average; output is signal minus average.
        v.bpZ1 = 0.985f * v.bpZ1 + 0.015f * sum;
        float hp = sum - v.bpZ1;

        // Add a touch of bandpassed noise to break up the periodicity.
        const float n_ = (randFloat() - 0.5f) * 0.3f;
        float out = (hp + n_) * v.ampLevel * v.velocity;
        out = drive(out, drv);
        outL[i] += out * pL;
        outR[i] += out * pR;
        ++v.ageSamples;
    }
}

void DrumSynth::renderTambourine(DrumVoice& v, float* outL, float* outR, int n) {
    const float baseHz = tuneToHz(m_params[pTambTune], 4000.0f, 10000.0f);
    const float atkMs  = m_params[pTambAttack];
    const float decMs  = m_params[pTambDecay];
    const float drv    = m_params[pTambDrive];

    const float sr      = static_cast<float>(m_sampleRate);
    const float atkInc  = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
    const float decCoef = std::exp(-1.0f / std::max(0.001f, decMs * 0.001f * sr));

    const float pL = panL(kDrumPan[Tambourine]);
    const float pR = panR(kDrumPan[Tambourine]);

    double inc[4];
    for (int p = 0; p < 4; ++p)
        inc[p] = baseHz * kTambRatios[p] / m_sampleRate;

    // Tambourine, like the hi-hat, only uses phases[] as oscillator
    // phases starting at 0 — clear the justTriggered flag for
    // consistency with the rest of the renderers.
    v.justTriggered = false;

    for (int i = 0; i < n; ++i) {
        if (v.stage == 0) {
            v.ampLevel += atkInc;
            if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.stage = 1; }
        } else {
            v.ampLevel *= decCoef;
            if (v.ampLevel < 0.0001f) { v.active = false; return; }
        }
        // 4 sines (cleaner than squares for the jingle character).
        float sum = 0.0f;
        for (int p = 0; p < 4; ++p) {
            v.phases[p] += inc[p];
            if (v.phases[p] >= 1.0) v.phases[p] -= 1.0;
            sum += std::sin(static_cast<float>(v.phases[p]) * 6.2831853f);
        }
        sum *= 0.20f;

        // Add bright noise — the tambourine's beads.
        const float n_ = (randFloat() - 0.5f) * 1.2f;
        float out = (sum * 0.4f + n_ * 0.6f) * v.ampLevel * v.velocity;
        out = drive(out, drv);
        outL[i] += out * pL;
        outR[i] += out * pR;
        ++v.ageSamples;
    }
}

void DrumSynth::process(float* buffer, int numFrames, int numChannels,
                         const midi::MidiBuffer& midi) {
    if (m_bypassed) return;
    const bool stereo = (numChannels > 1);

    // ── Process MIDI ──
    // We don't sample-accurate-step each event because drum DSP is
    // already coarse-grained (envelopes are ms-scale, not sample-
    // accurate). All events for the block fire at block start; that
    // matches every classic drum machine and keeps the renderer
    // routines simple.
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn() && msg.velocity > 0) {
            const int slot = slotForNote(msg.note);
            if (slot >= 0) noteOn(slot, velocityToGain(msg.velocity));
        } else if (msg.isNoteOff()) {
            // intentional no-op — drums are one-shot
        } else if (msg.isCC() && msg.ccNumber == 123) {
            // All Notes Off: silence everything immediately. Count
            // active voices first so the log line is informative
            // (and skip the log entirely if nothing was playing).
            int silenced = 0;
            for (auto& vv : m_voices) if (vv.active) { vv.active = false; ++silenced; }
            if (silenced > 0)
                LOG_INFO("DrumSynth", "All Notes Off (CC 123) — silenced %d voice%s",
                         silenced, silenced == 1 ? "" : "s");
        }
    }

    // ── Render each active drum into the output buffer ──
    //
    // Member-owned scratch buffers were sized at init() against the
    // host's m_maxBlockSize contract; if the host violates that
    // contract (sends a bigger block than it promised) we log once
    // and bail rather than write past the end of the vector.
    if (numFrames > static_cast<int>(m_scratchL.size())) {
        if (!m_oversizedBlockWarned) {
            LOG_WARN("DrumSynth",
                "process called with numFrames=%d but scratch sized for %zu — "
                "host is exceeding the m_maxBlockSize contract. Block dropped.",
                numFrames, m_scratchL.size());
            m_oversizedBlockWarned = true;
        }
        return;
    }

    // Zero the scratch (we ADD to it, matching the Instrument
    // convention that process() is additive into `buffer`).
    std::memset(m_scratchL.data(), 0, static_cast<size_t>(numFrames) * sizeof(float));
    std::memset(m_scratchR.data(), 0, static_cast<size_t>(numFrames) * sizeof(float));
    float* scratchL = m_scratchL.data();
    float* scratchR = m_scratchR.data();

    for (int slot = 0; slot < kNumDrums; ++slot) {
        auto& v = m_voices[slot];
        if (!v.active) continue;
        switch (slot) {
            case Kick:       renderKick(v, scratchL, scratchR, numFrames); break;
            case Snare:      renderSnare(v, scratchL, scratchR, numFrames); break;
            case Clap:       renderClap(v, scratchL, scratchR, numFrames); break;
            case Tom1:
            case Tom2:       renderTom(v, scratchL, scratchR, numFrames, slot); break;
            case ClosedHH:   renderHiHat(v, scratchL, scratchR, numFrames, false, slot); break;
            case OpenHH:     renderHiHat(v, scratchL, scratchR, numFrames, true,  slot); break;
            case Tambourine: renderTambourine(v, scratchL, scratchR, numFrames); break;
        }
    }

    // ── Mix scratch into the output buffer (additive) ──
    for (int i = 0; i < numFrames; ++i) {
        buffer[i * numChannels] += scratchL[i];
        if (stereo) buffer[i * numChannels + 1] += scratchR[i];
    }
}

} // namespace instruments
} // namespace yawn
