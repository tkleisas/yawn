#include "instruments/Multisampler.h"
#include "util/FileIO.h"
#include "util/Logger.h"

#include <filesystem>
#include <system_error>

namespace yawn::instruments {

namespace fs = std::filesystem;
using nlohmann::json;

void Multisampler::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) v = Voice{};
    resetParams();
}

void Multisampler::reset() {
    for (auto& v : m_voices) v.active = false;
}

void Multisampler::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    if (m_bypassed || m_zoneCount == 0) return;

    const float volume     = m_params[kVolume];
    // filterCut is stored normalized 0..1 → convert to Hz for filter math.
    const float filterCut  = cutoffNormToHz(m_params[kFilterCutoff]);
    const float filterReso = m_params[kFilterReso];
    const float filtEnvAmt = m_params[kFiltEnvAmount];
    const float velXfade   = m_params[kVelCrossfade];

    // Process MIDI
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn())
            noteOn(msg.note, msg.velocity, msg.channel, velXfade);
        else if (msg.isNoteOff())
            noteOff(msg.note, msg.channel);
        else if (msg.isCC() && msg.ccNumber == 123)
            for (auto& v : m_voices) if (v.active) v.ampEnv.gate(false);
    }

    for (int s = 0; s < numFrames; ++s) {
        float mixL = 0.0f, mixR = 0.0f;

        for (auto& v : m_voices) {
            if (!v.active) continue;

            float ampE = v.ampEnv.process();
            float filtE = v.filtEnv.process();
            if (v.ampEnv.isIdle()) { v.active = false; continue; }

            // Read sample from zone
            float samp = readZoneSample(v);
            if (!v.active) continue; // readZoneSample deactivates if past end

            samp *= v.velocity * v.zoneVolume * ampE;

            // SVF filter
            float cutHz = filterCut + filtEnvAmt * filtE;
            cutHz = std::clamp(cutHz, 20.0f, static_cast<float>(m_sampleRate) * 0.49f);
            float cutNorm = cutHz / static_cast<float>(m_sampleRate);
            float g = std::tan(3.14159265f * std::clamp(cutNorm, 0.001f, 0.499f));
            float k = 2.0f - 2.0f * std::clamp(filterReso, 0.0f, 0.95f);
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;

            float v3 = samp - v.filterIc[1];
            float v1 = a1 * v.filterIc[0] + a2 * v3;
            float v2 = v.filterIc[1] + a2 * v.filterIc[0] + a3 * v3;
            v.filterIc[0] = 2.0f * v1 - v.filterIc[0];
            v.filterIc[1] = 2.0f * v2 - v.filterIc[1];
            float filtered = (filterCut < 19999.0f) ? v2 : samp;

            // Pan
            float panL = std::cos((v.zonePan * 0.5f + 0.5f) * 1.5707963f);
            float panR = std::sin((v.zonePan * 0.5f + 0.5f) * 1.5707963f);
            mixL += filtered * panL;
            mixR += filtered * panR;
        }

        buffer[s * numChannels]     += mixL * volume;
        if (numChannels > 1)
            buffer[s * numChannels + 1] += mixR * volume;
    }
}

float Multisampler::readZoneSample(Voice& v) const {
    if (!v.zone || v.zone->sampleFrames == 0) return 0.0f;

    const Zone& z = *v.zone;
    int idx0 = static_cast<int>(v.playPos);
    if (idx0 < 0 || idx0 >= z.sampleFrames) {
        if (z.loop) {
            int loopStart = z.loopStart;
            int loopEnd = (z.loopEnd > loopStart) ? z.loopEnd : z.sampleFrames;
            int loopLen = loopEnd - loopStart;
            if (loopLen > 0) {
                while (v.playPos >= loopEnd)
                    v.playPos -= loopLen;
                while (v.playPos < loopStart)
                    v.playPos += loopLen;
                idx0 = static_cast<int>(v.playPos);
            }
        } else {
            v.active = false;
            return 0.0f;
        }
    }

    int idx1 = idx0 + 1;
    if (idx1 >= z.sampleFrames) {
        if (z.loop) {
            int loopStart = z.loopStart;
            idx1 = loopStart;
        } else {
            idx1 = z.sampleFrames - 1;
        }
    }

    float frac = static_cast<float>(v.playPos - std::floor(v.playPos));

    // Mix to mono
    float s0 = 0.0f, s1 = 0.0f;
    for (int ch = 0; ch < z.sampleChannels; ++ch) {
        s0 += z.sampleData[static_cast<size_t>(idx0) * z.sampleChannels + ch];
        s1 += z.sampleData[static_cast<size_t>(idx1) * z.sampleChannels + ch];
    }
    s0 /= z.sampleChannels;
    s1 /= z.sampleChannels;

    v.playPos += v.playSpeed;
    return s0 + frac * (s1 - s0);
}

void Multisampler::noteOn(uint8_t note, uint16_t vel16, uint8_t ch, float velXfade) {
    int vel7 = std::min(127, static_cast<int>(vel16 >> 9)); // 16-bit → 7-bit

    // Collect matching zones
    struct Match { int zoneIdx; float gain; };
    Match matches[kMaxZones];
    int matchCount = 0;

    for (int z = 0; z < m_zoneCount; ++z) {
        const Zone& zone = *m_zones[z];
        if (note < zone.lowKey || note > zone.highKey) continue;

        // Velocity matching with optional crossfade
        if (velXfade > 0.0f) {
            // Soft crossfade: zones near velocity boundary share the note
            int velCenter = (zone.lowVel + zone.highVel) / 2;
            int velRange = std::max(1, (zone.highVel - zone.lowVel) / 2);
            float dist = std::abs(vel7 - velCenter) / static_cast<float>(velRange);
            if (dist > 1.0f + velXfade) continue;
            float gain = std::clamp(1.0f - (dist - 1.0f) / std::max(0.01f, velXfade), 0.0f, 1.0f);
            if (gain > 0.0f && matchCount < kMaxZones)
                matches[matchCount++] = {z, gain};
        } else {
            // Hard velocity matching
            if (vel7 < zone.lowVel || vel7 > zone.highVel) continue;
            if (matchCount < kMaxZones)
                matches[matchCount++] = {z, 1.0f};
        }
    }

    // Allocate voices for matching zones
    for (int m = 0; m < matchCount; ++m) {
        Voice* slot = nullptr;
        for (auto& v : m_voices)
            if (!v.active) { slot = &v; break; }
        if (!slot) {
            int64_t oldest = INT64_MAX;
            for (auto& v : m_voices)
                if (v.startOrder < oldest) { oldest = v.startOrder; slot = &v; }
        }
        if (!slot) break;

        const Zone& zone = *m_zones[matches[m].zoneIdx];
        float pitchShift = static_cast<float>(static_cast<int>(note) - zone.rootNote) + zone.tune;

        slot->active = true;
        slot->note = note;
        slot->channel = ch;
        slot->velocity = velocityToGain(vel16) * matches[m].gain;
        slot->startOrder = m_voiceCounter++;
        slot->playPos = 0.0;
        // Sample-rate compensation: playPos advances by playSpeed once
        // per output frame, so when the WAV was recorded at a rate
        // different from the engine's, we need to walk the buffer
        // faster (recorded > engine) or slower (recorded < engine) to
        // preserve pitch. zone.sampleRate == 0 = legacy zone, treat as
        // matching the engine rate (no compensation, same behaviour
        // as before this field existed).
        const double srRatio = (zone.sampleRate > 0 && m_sampleRate > 0)
            ? (static_cast<double>(zone.sampleRate) / m_sampleRate)
            : 1.0;
        slot->playSpeed = std::pow(2.0, pitchShift / 12.0) * srRatio;
        slot->zoneVolume = zone.volume;
        slot->zonePan = zone.pan;
        slot->zone = &zone;
        slot->filterIc[0] = slot->filterIc[1] = 0.0f;

        slot->ampEnv.setSampleRate(m_sampleRate);
        slot->ampEnv.setADSR(m_params[kAmpAttack], m_params[kAmpDecay],
                              m_params[kAmpSustain], m_params[kAmpRelease]);
        slot->ampEnv.gate(true);

        slot->filtEnv.setSampleRate(m_sampleRate);
        slot->filtEnv.setADSR(m_params[kFiltAttack], m_params[kFiltDecay],
                               m_params[kFiltSustain], m_params[kFiltRelease]);
        slot->filtEnv.gate(true);
    }
}

void Multisampler::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices) {
        if (v.active && v.note == note && v.channel == ch) {
            v.ampEnv.gate(false);
            v.filtEnv.gate(false);
        }
    }
}

void Multisampler::resetParams() {
    for (int i = 0; i < kParamCount; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
    m_voiceCounter = 0;
}

// ─── Preset extra state ─────────────────────────────────────────────
// Mirrors ProjectSerializer's per-zone WAV layout so a preset captures
// the same content as saving a project — sample data + zone keymap.
// Without this, Multisampler "presets" only round-trip the 14 global
// params and a load looks like a no-op.

json Multisampler::saveExtraState(const fs::path& assetDir) const {
    std::error_code ec;
    fs::create_directories(assetDir, ec);
    // Wipe any stale .wavs from a previous save with the same preset
    // name — a smaller new zone count would otherwise leave orphans
    // alongside the JSON.
    if (fs::exists(assetDir, ec)) {
        for (auto& entry : fs::directory_iterator(assetDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                fs::remove(entry.path(), ec);
            }
        }
    }

    json zones = json::array();
    for (int i = 0; i < m_zoneCount; ++i) {
        const auto* z = m_zones[i].get();
        if (!z || z->sampleFrames <= 0) continue;
        char fnameBuf[32];
        std::snprintf(fnameBuf, sizeof(fnameBuf), "zone_%02d.wav", i);
        const fs::path samplePath = assetDir / fnameBuf;
        if (!util::saveAudioFile(samplePath.string(),
                                  z->sampleData.data(),
                                  z->sampleFrames,
                                  z->sampleChannels,
                                  static_cast<int>(m_sampleRate))) {
            LOG_WARN("Preset", "Multisampler: failed to write '%s' for zone %d",
                     samplePath.string().c_str(), i);
            continue;
        }
        json zj;
        zj["sampleFile"] = fnameBuf;     // relative to assetDir
        zj["sampleRate"] = (z->sampleRate > 0)
            ? z->sampleRate
            : static_cast<int>(m_sampleRate);
        zj["rootNote"]   = z->rootNote;
        zj["lowKey"]     = z->lowKey;
        zj["highKey"]    = z->highKey;
        zj["lowVel"]     = z->lowVel;
        zj["highVel"]    = z->highVel;
        zj["tune"]       = z->tune;
        zj["volume"]     = z->volume;
        zj["pan"]        = z->pan;
        zj["loop"]       = z->loop;
        zj["loopStart"]  = z->loopStart;
        zj["loopEnd"]    = z->loopEnd;
        zones.push_back(zj);
    }

    json out;
    out["zones"] = std::move(zones);
    return out;
}

void Multisampler::loadExtraState(const json& state,
                                   const fs::path& assetDir) {
    if (!state.contains("zones")) return;
    clearZones();
    for (const auto& zj : state["zones"]) {
        const std::string fname = zj.value("sampleFile", "");
        if (fname.empty()) continue;
        const fs::path samplePath = assetDir / fname;
        auto buf = util::loadAudioFile(samplePath.string());
        if (!buf) {
            LOG_WARN("Preset", "Multisampler zone sample missing: %s",
                     samplePath.string().c_str());
            continue;
        }
        const int nf = buf->numFrames();
        const int nc = buf->numChannels();
        Zone z;
        // AudioBuffer is non-interleaved (channel-stacked); de-stripe
        // back into the Multisampler's interleaved layout. Same code
        // path as ProjectSerializer's load.
        z.sampleData.resize(static_cast<size_t>(nf) * nc);
        for (int ch = 0; ch < nc; ++ch) {
            const float* src = buf->channelData(ch);
            for (int f = 0; f < nf; ++f)
                z.sampleData[f * nc + ch] = src[f];
        }
        z.sampleFrames   = nf;
        z.sampleChannels = nc;
        // Recorded rate from JSON when present, fall back to the WAV's
        // own header (libsndfile reports the file's stored rate via
        // the AudioBuffer; we don't currently expose it, but the JSON
        // is the authoritative location for newly-saved presets).
        // Older presets without "sampleRate" treat 0 as "match engine".
        z.sampleRate = zj.value("sampleRate", 0);
        z.rootNote  = zj.value("rootNote", 60);
        z.lowKey    = zj.value("lowKey",   0);
        z.highKey   = zj.value("highKey",  127);
        z.lowVel    = zj.value("lowVel",   0);
        z.highVel   = zj.value("highVel",  127);
        z.tune      = zj.value("tune",     0.0f);
        z.volume    = zj.value("volume",   1.0f);
        z.pan       = zj.value("pan",      0.0f);
        z.loop      = zj.value("loop",     false);
        z.loopStart = zj.value("loopStart", 0);
        z.loopEnd   = zj.value("loopEnd",   nf);
        addZone(z);
    }
}

} // namespace yawn::instruments
