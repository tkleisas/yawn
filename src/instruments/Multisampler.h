#pragma once
// Multisampler.h — Multi-zone sample playback instrument for YAWN
// Maps multiple samples across key and velocity ranges with per-zone
// tuning, volume, pan, and loop settings. Supports velocity crossfade.

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "instruments/SampleData.h"
#include "effects/FreqMap.h"
#include <array>
#include <atomic>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>
#include <algorithm>
#include <memory>

namespace yawn::instruments {

class Multisampler : public Instrument {
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kMaxZones  = 128;
    static constexpr int kParamCount = 14;

    // Cutoff stored normalized 0..1, log-mapped to 20..20000 Hz.
    static float cutoffNormToHz(float x) { return effects::logNormToHz(x, 20.0f, 20000.0f); }
    static float cutoffHzToNorm(float hz) { return effects::logHzToNorm(hz, 20.0f, 20000.0f); }
    static void  formatCutoffHz(float v, char* buf, int n) {
        effects::formatHz(cutoffNormToHz(v), buf, n);
    }

    enum Param {
        kAmpAttack = 0,    // 0.001–5 s
        kAmpDecay,         // 0.001–5 s
        kAmpSustain,       // 0–1
        kAmpRelease,       // 0.001–10 s
        kFilterCutoff,     // 0..1 normalized → 20..20000 Hz (log)
        kFilterReso,       // 0–1
        kFiltEnvAmount,    // -10000 to +10000 Hz
        kFiltAttack,       // 0.001–5 s
        kFiltDecay,        // 0.001–5 s
        kFiltSustain,      // 0–1
        kFiltRelease,      // 0.001–10 s
        kGlide,            // 0–1 s (portamento)
        kVelCrossfade,     // 0–1 (velocity layer crossfade amount)
        kVolume,           // 0–1
    };

    // ── Zone definition ──
    struct Zone {
        std::vector<float> sampleData;   // interleaved audio
        int sampleFrames   = 0;
        int sampleChannels = 1;
        // Sample rate the audio was originally recorded at. 0 = legacy
        // / unknown — falls back to "match engine rate" so old saves
        // sound the same as before this field existed. When non-zero,
        // playback speed compensates for the engine/sample-rate ratio
        // (e.g. a 48000-Hz capture played by a 44100-Hz engine plays
        // 48000/44100 ≈ 1.088× faster, otherwise it'd be ~147 cents
        // flat).
        int sampleRate     = 0;
        int rootNote       = 60;
        int lowKey         = 0;
        int highKey        = 127;
        int lowVel         = 0;     // 0–127
        int highVel        = 127;   // 0–127
        float tune         = 0.0f;  // semitones (-24 to +24)
        float volume       = 1.0f;  // 0–2
        float pan          = 0.0f;  // -1 to +1
        bool loop          = false;
        int loopStart      = 0;     // sample frames
        int loopEnd        = 0;     // sample frames (0 = end of sample)
    };

    const char* name() const override { return "Multisampler"; }
    const char* id() const override { return "multisampler"; }

    // Instruments are destroyed only after the audio grace period
    // (retire list), so no process() call can still hold the
    // published snapshot — safe to delete it here.
    ~Multisampler() override {
        delete m_zoneSnap.load(std::memory_order_relaxed);
    }

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    // ── Zone management (UI thread only) ──
    // Every structural change publishes an immutable ZoneSnapshot the
    // audio thread scans; removed zones are retired, and playing
    // voices pin their zone with a shared_ptr, so no edit can free a
    // zone out from under a voice (see Instrument::retireObject).
    int addZone(const Zone& zone) {
        if (m_zoneCount >= kMaxZones) return -1;
        auto z = std::make_shared<Zone>(zone);
        // Defense against programmatic callers passing loop points
        // outside the sample (the RT wrap math reads sampleData at
        // playPos — see readZoneSample).
        z->loopStart = std::clamp(z->loopStart, 0, z->sampleFrames);
        z->loopEnd   = std::clamp(z->loopEnd, z->loopStart, z->sampleFrames);
        m_zones[m_zoneCount] = std::move(z);
        ++m_zoneCount;
        publishZones();
        return m_zoneCount - 1;
    }

    int addZone(const float* data, int numFrames, int numChannels,
                int rootNote, int lowKey, int highKey,
                int lowVel = 0, int highVel = 127) {
        if (m_zoneCount >= kMaxZones) return -1;
        auto z = std::make_shared<Zone>();
        z->sampleData.assign(data, data + numFrames * numChannels);
        z->sampleFrames = numFrames;
        z->sampleChannels = numChannels;
        z->rootNote = rootNote;
        z->lowKey = lowKey;
        z->highKey = highKey;
        z->lowVel = lowVel;
        z->highVel = highVel;
        z->loopEnd = numFrames;
        m_zones[m_zoneCount] = std::move(z);
        ++m_zoneCount;
        publishZones();
        return m_zoneCount - 1;
    }

    void clearZones() {
        for (int i = 0; i < m_zoneCount; ++i)
            retireObject(std::move(m_zones[i]));
        m_zoneCount = 0;
        publishZones();
    }

    // Remove a single zone, sliding remaining zones down to keep
    // contiguous indices. UI thread only; voices already playing the
    // removed zone keep it alive via their pinned shared_ptr.
    void removeZone(int idx) {
        if (idx < 0 || idx >= m_zoneCount) return;
        retireObject(std::move(m_zones[idx]));
        for (int i = idx; i < m_zoneCount - 1; ++i)
            m_zones[i] = std::move(m_zones[i + 1]);
        m_zones[m_zoneCount - 1].reset();
        --m_zoneCount;
        publishZones();
    }

    int zoneCount() const { return m_zoneCount; }
    const Zone* zone(int idx) const {
        return (idx >= 0 && idx < m_zoneCount) ? m_zones[idx].get() : nullptr;
    }
    Zone* zone(int idx) {
        return (idx >= 0 && idx < m_zoneCount) ? m_zones[idx].get() : nullptr;
    }

    // ── Preset extra state ──────────────────────────────────────────
    // Multisampler stores its zones (sample data + key/velocity map +
    // tune/vol/pan/loop) outside the parameter list, so device-preset
    // save/load needs special handling. We mirror the project-side
    // serializer's layout: per-zone WAVs in `assetDir`, plus a JSON
    // array of zone metadata referencing them by relative filename.
    //
    // Preset asset folder layout:
    //   <preset>.json        ← params + extraState (zone metadata)
    //   <preset>/            ← assetDir (created on save)
    //       zone_00.wav
    //       zone_01.wav
    //       ...
    nlohmann::json saveExtraState(
            const std::filesystem::path& assetDir) const override;
    void loadExtraState(const nlohmann::json& state,
                         const std::filesystem::path& assetDir) override;

    int parameterCount() const override { return kParamCount; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo info[kParamCount] = {
            {"Amp Attack",    0.001f,  5.0f,  0.005f, "s",   false},
            {"Amp Decay",     0.001f,  5.0f,  0.1f,   "s",   false},
            {"Amp Sustain",   0.0f,    1.0f,  1.0f,   "",    false, false, WidgetHint::DentedKnob},
            {"Amp Release",   0.001f, 10.0f,  0.3f,   "s",   false},
            {"Filter Cut",   0.0f, 1.0f, 1.0f, "", false, false,
                WidgetHint::Knob, nullptr, 0, &formatCutoffHz},
            {"Filter Reso",   0.0f,    1.0f,  0.0f,   "",    false},
            {"Filt Env Amt",-10000.0f,10000.0f,0.0f,   "Hz",  false, false, WidgetHint::DentedKnob},
            {"Filt Attack",   0.001f,  5.0f,  0.01f,  "s",   false},
            {"Filt Decay",    0.001f,  5.0f,  0.3f,   "s",   false},
            {"Filt Sustain",  0.0f,    1.0f,  0.5f,   "",    false, false, WidgetHint::DentedKnob},
            {"Filt Release",  0.001f, 10.0f,  0.3f,   "s",   false},
            {"Glide",         0.0f,    1.0f,  0.0f,   "s",   false},
            {"Vel Crossfade", 0.0f,    1.0f,  0.0f,   "",    false},
            {"Volume",        0.0f,    1.0f,  0.8f,   "",    false, false, WidgetHint::DentedKnob},
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
    }

private:
    struct Voice {
        bool active = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        float velocity = 0.0f;
        int64_t startOrder = 0;
        double playPos = 0.0;
        double playSpeed = 1.0;
        float zoneVolume = 1.0f;
        float zonePan = 0.0f;
        // Pinned at note-on: the zone object (and its sample data)
        // stays alive for the whole render even if the UI removes it.
        std::shared_ptr<const Zone> zone;
        Envelope ampEnv;
        Envelope filtEnv;
        float filterIc[2] = {};
    };

    // Immutable zone list published to the audio thread. Holds
    // shared_ptrs so zones can't die while a snapshot is parked in
    // the retire list, and voices pin from it at note-on.
    struct ZoneSnapshot {
        std::array<std::shared_ptr<const Zone>, kMaxZones> zones;
        int count = 0;
    };

    float readZoneSample(Voice& v) const;
    void noteOn(uint8_t note, uint16_t vel16, uint8_t ch, float velXfade,
                const ZoneSnapshot* snap);
    void noteOff(uint8_t note, uint8_t ch);
    void resetParams();

    // Rebuild + publish the zone snapshot (UI thread only; allocates).
    void publishZones() {
        auto* snap = new ZoneSnapshot();
        snap->count = m_zoneCount;
        for (int i = 0; i < m_zoneCount; ++i)
            snap->zones[i] = m_zones[i];
        const ZoneSnapshot* old =
            m_zoneSnap.exchange(snap, std::memory_order_acq_rel);
        if (old) {
            if (m_retireList)
                m_retireList->retire(std::unique_ptr<const ZoneSnapshot>(old));
            else
                delete old;
        }
    }

    // ── State ──
    float m_params[kParamCount] = {};
    Voice m_voices[kMaxVoices] = {};

    std::shared_ptr<Zone> m_zones[kMaxZones];
    int m_zoneCount = 0;
    std::atomic<const ZoneSnapshot*> m_zoneSnap{nullptr};

    int64_t m_voiceCounter = 0;
};

} // namespace yawn::instruments
