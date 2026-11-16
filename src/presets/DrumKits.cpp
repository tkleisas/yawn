#include "presets/DrumKits.h"

#include "instruments/DrumSynth.h"
#include "instruments/DrumRack.h"
#include "presets/DevicePresetHelpers.h"
#include "presets/PresetManager.h"
#include "midi/MidiTypes.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <set>
#include <utility>
#include <vector>

namespace yawn {

namespace {

using KitParam = std::pair<int, float>;

struct KitDef {
    const char* name;
    std::initializer_list<KitParam> params;
};

const KitDef kKits[] = {
    { "808 Kit", {
        { instruments::DrumSynth::pKickTune, 0.25f },
        { instruments::DrumSynth::pKickDecay, 900.0f },
        { instruments::DrumSynth::pKickSine, 0.95f },
        { instruments::DrumSynth::pKickWhite, 0.10f },
        { instruments::DrumSynth::pKickPink, 0.16f },
        { instruments::DrumSynth::pKickDrive, 0.35f },
        { instruments::DrumSynth::pSnareTune, 0.42f },
        { instruments::DrumSynth::pSnareDecay, 260.0f },
        { instruments::DrumSynth::pSnareDrive, 0.35f },
        { instruments::DrumSynth::pClapDecay, 280.0f },
        { instruments::DrumSynth::pTom1Decay, 900.0f },
        { instruments::DrumSynth::pTom2Decay, 800.0f },
        { instruments::DrumSynth::pCHHDecay, 45.0f },
        { instruments::DrumSynth::pOHHDecay, 600.0f },
        { instruments::DrumSynth::pBody, 0.65f },
    } },
    { "909 Kit", {
        { instruments::DrumSynth::pKickTune, 0.45f },
        { instruments::DrumSynth::pKickDecay, 320.0f },
        { instruments::DrumSynth::pKickSine, 0.80f },
        { instruments::DrumSynth::pKickWhite, 0.45f },
        { instruments::DrumSynth::pKickPink, 0.10f },
        { instruments::DrumSynth::pKickDrive, 0.50f },
        { instruments::DrumSynth::pSnareTune, 0.62f },
        { instruments::DrumSynth::pSnareDecay, 180.0f },
        { instruments::DrumSynth::pSnareDrive, 0.50f },
        { instruments::DrumSynth::pClapDecay, 180.0f },
        { instruments::DrumSynth::pCHHTune, 0.62f },
        { instruments::DrumSynth::pCHHDecay, 50.0f },
        { instruments::DrumSynth::pOHHDecay, 350.0f },
        { instruments::DrumSynth::pBody, 0.45f },
    } },
    { "Acoustic Kit", {
        { instruments::DrumSynth::pKickTune, 0.35f },
        { instruments::DrumSynth::pKickDecay, 550.0f },
        { instruments::DrumSynth::pKickSine, 0.80f },
        { instruments::DrumSynth::pKickWhite, 0.30f },
        { instruments::DrumSynth::pKickPink, 0.25f },
        { instruments::DrumSynth::pKickDrive, 0.15f },
        { instruments::DrumSynth::pSnareTune, 0.55f },
        { instruments::DrumSynth::pSnareDecay, 280.0f },
        { instruments::DrumSynth::pSnareDrive, 0.20f },
        { instruments::DrumSynth::pTom1Tune, 0.32f },
        { instruments::DrumSynth::pTom1Decay, 700.0f },
        { instruments::DrumSynth::pTom2Tune, 0.58f },
        { instruments::DrumSynth::pTom2Decay, 600.0f },
        { instruments::DrumSynth::pCHHDecay, 80.0f },
        { instruments::DrumSynth::pOHHDecay, 500.0f },
        { instruments::DrumSynth::pTambDecay, 220.0f },
        { instruments::DrumSynth::pBody, 0.60f },
    } },
    { "Lo-Fi Kit", {
        { instruments::DrumSynth::pKickTune, 0.20f },
        { instruments::DrumSynth::pKickDecay, 450.0f },
        { instruments::DrumSynth::pKickWhite, 0.20f },
        { instruments::DrumSynth::pKickPink, 0.30f },
        { instruments::DrumSynth::pKickDrive, 0.80f },
        { instruments::DrumSynth::pSnareTune, 0.50f },
        { instruments::DrumSynth::pSnareDecay, 200.0f },
        { instruments::DrumSynth::pSnareDrive, 0.70f },
        { instruments::DrumSynth::pClapDrive, 0.45f },
        { instruments::DrumSynth::pCHHDecay, 30.0f },
        { instruments::DrumSynth::pOHHDecay, 200.0f },
        { instruments::DrumSynth::pBody, 0.40f },
    } },
    { "Industrial Kit", {
        { instruments::DrumSynth::pKickTune, 0.40f },
        { instruments::DrumSynth::pKickDecay, 700.0f },
        { instruments::DrumSynth::pKickWhite, 0.50f },
        { instruments::DrumSynth::pKickPink, 0.10f },
        { instruments::DrumSynth::pKickDrive, 0.90f },
        { instruments::DrumSynth::pSnareTune, 0.58f },
        { instruments::DrumSynth::pSnareDecay, 400.0f },
        { instruments::DrumSynth::pSnareDrive, 0.80f },
        { instruments::DrumSynth::pClapDrive, 0.60f },
        { instruments::DrumSynth::pCHHDecay, 100.0f },
        { instruments::DrumSynth::pOHHDecay, 800.0f },
        { instruments::DrumSynth::pBody, 0.80f },
    } },
};

void applyKit(instruments::DrumSynth& ds, const KitDef& kit) {
    for (const auto& p : kit.params) ds.setParameter(p.first, p.second);
}

bool presetExists(const std::set<std::string>& names, const std::string& name) {
    return names.count(name) != 0;
}

std::set<std::string> existingPresetNames(const std::string& deviceId) {
    std::set<std::string> names;
    for (const auto& p : PresetManager::listPresetsForDevice(deviceId))
        names.insert(p.name);
    return names;
}

// Render one pad into an interleaved stereo buffer, trimmed when the
// tail falls below -72 dBFS. Returns empty when the pad is silent.
std::vector<float> renderOneShot(const KitDef& kit, int note, double sr,
                                 int maxFrames) {
    instruments::DrumSynth ds;
    ds.init(sr, 512);
    applyKit(ds, kit);
    ds.reset();

    midi::MidiBuffer midi;
    midi::MidiMessage on{};
    on.type = midi::MidiMessage::Type::NoteOn;
    on.channel = 0;
    on.note = static_cast<uint8_t>(note);
    on.velocity = midi::Convert::vel7to16(110);

    std::vector<float> out;
    out.reserve(static_cast<size_t>(maxFrames) * 2);
    float block[512 * 2];
    bool first = true;
    for (int frame = 0; frame < maxFrames; frame += 512) {
        std::memset(block, 0, sizeof(block));
        const int n = std::min(512, maxFrames - frame);
        midi::MidiBuffer buf;
        if (first) {
            midi.addMessage(on);
            buf = midi;
            first = false;
        }
        ds.process(block, n, 2, buf);
        out.insert(out.end(), block, block + n * 2);
    }

    int last = static_cast<int>(out.size()) / 2 - 1;
    while (last >= 0 && std::fabs(out[static_cast<size_t>(last) * 2]) < 1e-4f &&
           std::fabs(out[static_cast<size_t>(last) * 2 + 1]) < 1e-4f)
        --last;
    if (last < 0) return {};
    // Keep a 10 ms tail so the sample doesn't end on a hard edge.
    last = std::min(last + static_cast<int>(0.010 * sr),
                    static_cast<int>(out.size()) / 2 - 1);
    out.resize(static_cast<size_t>(last + 1) * 2);
    return out;
}

void seedFactoryRack() {
    const std::string rackName = "YAWN Factory Rack";
    auto existing = existingPresetNames("drumrack");
    if (presetExists(existing, rackName)) return;

    const KitDef* kit = nullptr;
    for (const auto& k : kKits)
        if (std::string(k.name) == "Acoustic Kit") { kit = &k; break; }
    if (!kit) return;

    constexpr double sr = 48000.0;
    instruments::DrumRack rack;
    rack.init(sr, 512);

    const int notes[] = { 36, 38, 39, 41, 42, 46, 50, 54 };
    int loaded = 0;
    for (int note : notes) {
        auto sample = renderOneShot(*kit, note, sr, static_cast<int>(1.5 * sr));
        if (sample.empty()) continue;
        rack.loadPad(note, sample.data(), static_cast<int>(sample.size()) / 2, 2);
        if (note == 42 || note == 46) rack.setPadChokeGroup(note, 1);
        ++loaded;
    }
    if (loaded == 0) {
        LOG_WARN("Preset", "DrumKits: factory rack rendered no pads");
        return;
    }
    auto path = saveDevicePreset(rackName, rack);
    if (path.empty())
        LOG_WARN("Preset", "DrumKits: failed to save factory rack");
    else
        LOG_INFO("Preset", "DrumKits: seeded %s (%d pads)", rackName.c_str(),
                 loaded);
}

} // namespace

int DrumKits::seedFactoryKits() {
    int written = 0;
    auto existing = existingPresetNames("drumsynth");
    for (const auto& kit : kKits) {
        if (presetExists(existing, kit.name)) continue;
        instruments::DrumSynth ds;
        ds.init(48000.0, 512);
        applyKit(ds, kit);
        auto path = saveDevicePreset(kit.name, ds);
        if (path.empty())
            LOG_WARN("Preset", "DrumKits: failed to save '%s'", kit.name);
        else
            ++written;
    }
    seedFactoryRack();
    return written;
}

} // namespace yawn
