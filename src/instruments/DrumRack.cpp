#include "instruments/DrumRack.h"

#include "util/FileIO.h"
#include "util/Logger.h"
#include "util/EffectChainJson.h"

#include <filesystem>
#include <system_error>

namespace yawn {
namespace instruments {

namespace fs = std::filesystem;
using nlohmann::json;

void DrumRack::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& p : m_pads) {
        p.env.setSampleRate(sampleRate);
        // Re-init any chain we've already lazy-allocated with the
        // (possibly new) engine sample rate. Newly-allocated chains
        // get init'd inside padFxChain().
        if (p.fx) p.fx->init(sampleRate, maxBlockSize);
    }
    // Stereo scratch sized for the largest block we'll ever see.
    m_scratch.assign(static_cast<size_t>(maxBlockSize) * 2, 0.0f);
}

void DrumRack::reset() {
    for (auto& p : m_pads) {
        p.playing = false;
        p.env.reset();
    }
}

void DrumRack::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    // Handle MIDI triggers
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn()) {
            const int  noteIdx = msg.note & 0x7F;
            auto&      p       = m_pads[noteIdx];
            if (p.sampleFrames > 0) {
                // Choke group — silence every OTHER pad currently
                // playing in the same mute group before retriggering
                // this one. Group 0 means "no choking" so the check
                // is skipped for unassigned pads (the common case).
                if (p.chokeGroup != 0) {
                    for (int n = 0; n < kNumPads; ++n) {
                        if (n == noteIdx) continue;
                        if (m_pads[n].chokeGroup == p.chokeGroup)
                            m_pads[n].playing = false;
                    }
                }
                // Resolve start / end region from normalised knob
                // values. If end < start, play backwards — gives
                // the user a free reverse mode by just dragging
                // the End knob past Start.
                int sFr = static_cast<int>(p.startNorm * p.sampleFrames);
                int eFr = static_cast<int>(p.endNorm   * p.sampleFrames);
                sFr = std::clamp(sFr, 0, p.sampleFrames - 1);
                eFr = std::clamp(eFr, 0, p.sampleFrames);
                if (sFr == eFr) continue;   // zero-length region — don't trigger
                const double baseSpeed = std::pow(2.0, p.pitchAdjust / 12.0);
                p.playSpeed = (eFr > sFr) ? baseSpeed : -baseSpeed;
                p.playPos   = static_cast<double>(sFr);
                p.endFrameI = eFr;
                p.playing   = true;
                p.velocity  = velocityToGain(msg.velocity);
                // Re-arm the per-pad AR envelope. Sustain held at 0
                // so decay rolls straight to silence — drum-machine
                // one-shot semantics. setADSR's release time is
                // unused here (we never call gate(false)) but the
                // value still has to be valid; pass a tiny number.
                p.env.setADSR(p.attackS, p.decayS, 0.0f, 0.001f);
                p.env.gate(true);
            }
        }
        // NoteOff: optionally stop pad (choke). For drums, we let them ring.
    }

    // Render all playing pads — and pads with an fx chain that may
    // still have a tail to bleed out (reverb, delay) even though the
    // sample finished. Lazy-allocated chains keep the cost zero for
    // pads the user hasn't touched.
    const int scratchSamples = numFrames * numChannels;
    for (int n = 0; n < kNumPads; ++n) {
        auto& p = m_pads[n];
        const bool needsFx = (p.fx != nullptr);
        if (!p.playing && !needsFx) continue;

        // Zero the scratch — when a pad's render-loop bails early
        // (env idle / region end / out-of-bounds), the un-written
        // tail of the scratch must be silence so the fx chain sees
        // a clean drop-off instead of stale data from another pad.
        std::fill(m_scratch.begin(),
                  m_scratch.begin() + scratchSamples, 0.0f);

        if (p.playing) {
            // Pre-fx pan + per-pad volume + velocity. m_volume (rack
            // master) is applied AFTER the fx chain, on accumulation
            // — that way the master fader doesn't change how a
            // saturator / compressor responds.
            float angle = (p.pan + 1.0f) * 0.25f * (float)M_PI;
            float gL = p.volume * p.velocity * std::cos(angle);
            float gR = p.volume * p.velocity * std::sin(angle);

            const bool forward = (p.playSpeed > 0.0);
            for (int i = 0; i < numFrames; ++i) {
                int pos = static_cast<int>(p.playPos);
                // Region bounds (direction-aware) + buffer guard.
                const bool reachedEnd = forward
                    ? (pos >= p.endFrameI)
                    : (pos <= p.endFrameI);
                if (reachedEnd || pos < 0 || pos >= p.sampleFrames) {
                    p.playing = false; break;
                }
                const float envVal = p.env.process();
                if (p.env.isIdle()) { p.playing = false; break; }

                float sL = p.sampleData[pos * p.sampleChannels];
                float sR = (p.sampleChannels > 1)
                    ? p.sampleData[pos * p.sampleChannels + 1] : sL;

                m_scratch[i * numChannels + 0] = sL * gL * envVal;
                if (numChannels > 1)
                    m_scratch[i * numChannels + 1] = sR * gR * envVal;

                p.playPos += p.playSpeed;
            }
        }

        // Per-pad fx chain. Always run when present so any tail
        // (reverb / delay) keeps decaying after the dry signal has
        // ended — including when the pad was choked mid-tail.
        if (needsFx)
            p.fx->process(m_scratch.data(), numFrames, numChannels);

        // Accumulate scratch into the rack output, scaled by the
        // rack-master volume.
        for (int i = 0; i < scratchSamples; ++i)
            buffer[i] += m_scratch[i] * m_volume;
    }
}

// ── Kit preset save / load ────────────────────────────────────────
//
// Hooked into the standard saveDevicePreset / loadDevicePreset
// path via the Instrument base-class virtuals. Layout:
//
//   <preset>/                 (the assetDir handed in by the caller)
//     pad_036.wav             (one .wav per loaded pad)
//     pad_038.wav
//     ...
//     <preset>.json           (written by the caller; it embeds the
//                              JSON we return from saveExtraState)
//
// Per-pad fields mirror what ProjectSerializer writes for the
// in-project save path — Choke / Attack / Decay / Start / End all
// use the same omit-when-default policy so kit-preset JSON stays
// terse for vanilla pads.

json DrumRack::saveExtraState(const fs::path& assetDir) const {
    std::error_code ec;
    fs::create_directories(assetDir, ec);
    // Wipe any stale .wavs from a previous save with the same
    // preset name — fewer pads in the new save would otherwise
    // leave orphans alongside the JSON.
    if (fs::exists(assetDir, ec)) {
        for (auto& entry : fs::directory_iterator(assetDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav")
                fs::remove(entry.path(), ec);
        }
    }

    json pads = json::array();
    for (int n = 0; n < kNumPads; ++n) {
        const auto& p = m_pads[n];
        if (p.sampleFrames <= 0) continue;
        char fnameBuf[32];
        std::snprintf(fnameBuf, sizeof(fnameBuf), "pad_%03d.wav", n);
        const fs::path samplePath = assetDir / fnameBuf;
        if (!util::saveAudioFile(samplePath.string(),
                                  p.sampleData.data(),
                                  p.sampleFrames, p.sampleChannels,
                                  static_cast<int>(m_sampleRate))) {
            LOG_WARN("Preset", "DrumRack: failed to write '%s' for pad %d",
                     samplePath.string().c_str(), n);
            continue;
        }
        json padJ;
        padJ["note"]        = n;
        padJ["sampleFile"]  = fnameBuf;
        padJ["volume"]      = p.volume;
        padJ["pan"]         = p.pan;
        padJ["pitchAdjust"] = p.pitchAdjust;
        if (p.chokeGroup != 0)
            padJ["chokeGroup"] = static_cast<int>(p.chokeGroup);
        if (std::abs(p.attackS - 0.001f) > 1e-5f)
            padJ["attack"] = p.attackS;
        if (std::abs(p.decayS - 10.0f) > 1e-3f)
            padJ["decay"]  = p.decayS;
        if (p.startNorm != 0.0f) padJ["start"] = p.startNorm;
        if (p.endNorm   != 1.0f) padJ["end"]   = p.endNorm;
        // Per-pad fx — only emitted when a chain has been allocated
        // AND has at least one effect. Reuses the same JSON shape
        // the project serialiser uses, so a kit preset's chains
        // are interchangeable with the in-project chain JSON.
        if (p.fx && p.fx->count() > 0)
            padJ["fx"] = serializeEffectChainStandalone(*p.fx);
        pads.push_back(padJ);
    }

    json out;
    out["pads"]   = std::move(pads);
    out["volume"] = m_volume;
    return out;
}

void DrumRack::loadExtraState(const json& state, const fs::path& assetDir) {
    // Wipe before loading — partial overlap (new pad set covers
    // some of the old indices) would otherwise leave orphan
    // samples on the unaddressed pads.
    for (int n = 0; n < kNumPads; ++n) {
        clearPad(n);
        m_pads[n].volume      = 1.0f;
        m_pads[n].pan         = 0.0f;
        m_pads[n].pitchAdjust = 0.0f;
        m_pads[n].chokeGroup  = 0;
        m_pads[n].attackS     = 0.001f;
        m_pads[n].decayS      = 10.0f;
        m_pads[n].startNorm   = 0.0f;
        m_pads[n].endNorm     = 1.0f;
        m_pads[n].playing     = false;
        // Drop any chain from a previous kit so we don't end up
        // with reverbs from kit A on pads of kit B.
        m_pads[n].fx.reset();
    }

    if (state.contains("volume"))
        m_volume = state["volume"].get<float>();

    if (!state.contains("pads")) return;
    for (const auto& padJ : state["pads"]) {
        const int note = padJ.value("note", -1);
        if (note < 0 || note >= kNumPads) continue;
        const std::string fname = padJ.value("sampleFile", "");
        if (fname.empty()) continue;
        const fs::path samplePath = assetDir / fname;
        auto buf = util::loadAudioFile(samplePath.string());
        if (!buf) {
            LOG_WARN("Preset", "DrumRack pad %d sample missing: %s",
                     note, samplePath.string().c_str());
            continue;
        }
        // AudioBuffer is per-channel contiguous; loadPad takes
        // interleaved. De-stripe before handing it over.
        const int nf = buf->numFrames();
        const int nc = buf->numChannels();
        std::vector<float> il(static_cast<size_t>(nf) * nc);
        for (int ch = 0; ch < nc; ++ch) {
            const float* src = buf->channelData(ch);
            for (int f = 0; f < nf; ++f)
                il[static_cast<size_t>(f) * nc + ch] = src[f];
        }
        loadPad(note, il.data(), nf, nc);
        if (padJ.contains("volume"))      setPadVolume(note, padJ["volume"].get<float>());
        if (padJ.contains("pan"))         setPadPan(note, padJ["pan"].get<float>());
        if (padJ.contains("pitchAdjust")) setPadPitch(note, padJ["pitchAdjust"].get<float>());
        if (padJ.contains("chokeGroup"))  setPadChokeGroup(note, padJ["chokeGroup"].get<int>());
        if (padJ.contains("attack"))      setPadAttack(note, padJ["attack"].get<float>());
        if (padJ.contains("decay"))       setPadDecay(note, padJ["decay"].get<float>());
        if (padJ.contains("start"))       setPadStart(note, padJ["start"].get<float>());
        if (padJ.contains("end"))         setPadEnd(note, padJ["end"].get<float>());
        if (padJ.contains("fx")) {
            auto* chain = padFxChain(note);   // lazy-alloc here
            if (chain)
                deserializeEffectChainStandalone(*chain, padJ["fx"],
                    m_sampleRate, m_maxBlockSize);
        }
    }
}

} // namespace instruments
} // namespace yawn
