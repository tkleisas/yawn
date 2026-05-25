#pragma once

// PresetGenerator — procedural, seeded preset generation for YAWN's
// built-in instruments, audio effects and MIDI effects.
//
// Design (see also PresetNameGenerator):
//   * Works through the uniform parameter interface every device shares
//     (parameterCount / parameterInfo / get|setParameter), so a single
//     generic randomizer covers all three device kinds.
//   * Each preset picks a character ARCHETYPE (pad / pluck / bass / …
//     for instruments; subtle / lush / extreme / … for effects) that
//     biases parameters by their inferred ROLE (amp ADSR, filter cutoff,
//     wet/dry, feedback, …). Roles are inferred from parameter *names*,
//     which are consistent across devices, so no per-device tables are
//     needed.
//   * Everything is driven by a 64-bit seed → fully reproducible. A
//     single preset can be re-rolled by changing its sub-seed.
//   * Generated presets are written through the normal preset pipeline
//     (saveDevicePreset) so they appear in every load menu, and are
//     tracked in a side manifest for re-roll / lock.
//
// This lives in yawn_core (no UI/GL deps) so both the CLI batch entry
// and the in-app generator panel share it.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace yawn {
namespace instruments { class Instrument; }
namespace effects     { class AudioEffect; }
namespace midi        { class MidiEffect; }

namespace presets {

enum class DeviceKind { Instrument, AudioEffect, MidiEffect };

// Salient sound descriptors extracted from a (randomized) device, used
// to colour descriptive preset names. Defined here so both the
// generator and the name generator can see it without a circular
// include.
struct NameContext {
    DeviceKind kind       = DeviceKind::Instrument;
    int        archetype  = 0;     // index into the per-kind archetype table
    float      brightness = 0.5f;  // 0 dark … 1 bright (filter cutoff)
    float      resonance  = 0.5f;  // 0 … 1
    float      length     = 0.5f;  // 0 short … 1 long (amp envelope)
    float      motion     = 0.5f;  // 0 static … 1 moving (LFO depth/rate)
    bool       distorted  = false; // notable drive/distortion
    bool       wide       = false; // strong stereo / width
};

// One device + how many presets to make for it.
struct GenSpec {
    std::string deviceId;
    DeviceKind  kind = DeviceKind::Instrument;
    int         count = 0;
};

struct GenOptions {
    uint64_t masterSeed     = 0;     // 0 → derive a random seed at run time
    float    alienNameRatio = 0.5f;  // 0 = all descriptive, 1 = all alien
    bool     validate       = true;  // offline audibility / stability check
    int      maxRerollAttempts = 16; // reroll budget when validation fails
    bool     overwrite      = true;  // overwrite same-named generated presets
    double   sampleRate     = 44100.0;
};

// Result record for one generated preset (also persisted in the manifest).
struct GeneratedPreset {
    std::string           deviceId;
    std::string           deviceName;
    DeviceKind            kind = DeviceKind::Instrument;
    std::string           name;
    uint64_t              seed = 0;       // sub-seed → reproduces this preset
    int                   archetype = 0;  // archetype index used
    int                   attempts = 1;   // validation attempts taken
    bool                  valid = true;   // passed audibility check
    bool                  locked = false; // user-locked (batch reroll skips)
    std::filesystem::path filePath;       // saved .json (empty if unsaved)
};

// ── Low-level: randomize a live device in place ─────────────────────────────
// Fills `outCtx` with the chosen archetype + sound descriptors (for
// naming) and returns the archetype index. The device must already be
// init()'d if it will be auditioned/validated afterwards.
int randomizeInstrument (instruments::Instrument& inst, uint64_t seed, const GenOptions& opt, NameContext& outCtx);
int randomizeAudioEffect(effects::AudioEffect&     fx,   uint64_t seed, const GenOptions& opt, NameContext& outCtx);
int randomizeMidiEffect (midi::MidiEffect&         fx,   uint64_t seed, const GenOptions& opt, NameContext& outCtx);

// ── Low-level: offline validation (audible / finite / stable) ───────────────
bool validateInstrument (instruments::Instrument& inst, double sampleRate);
bool validateAudioEffect(effects::AudioEffect&     fx,   double sampleRate);
bool validateMidiEffect (midi::MidiEffect&         fx,   double sampleRate);

// ── High level ──────────────────────────────────────────────────────────────
class PresetGenerator {
public:
    explicit PresetGenerator(GenOptions opts = {});

    // Catalog of self-contained devices we can generate for, with the
    // requested default counts (instruments 20-50, audio FX 10-20,
    // MIDI FX 10). Excludes sample/asset-dependent and zero-param
    // devices.
    static std::vector<GenSpec> defaultCatalog();

    // Is a device id one the generator supports (self-contained)?
    static bool isSupported(const std::string& deviceId);
    static DeviceKind kindOf(const std::string& deviceId);

    // Generate (and save) every preset described by `specs`. Existing
    // locked manifest entries are preserved; everything else is
    // (re)generated. Returns the full resulting set.
    std::vector<GeneratedPreset> generateBatch(const std::vector<GenSpec>& specs);

    // Generate a single preset for a device and save it. `subSeed`
    // selects the variation; pass a fresh value to re-roll. `usedNames`
    // avoids collisions within a batch.
    GeneratedPreset generateOneAndSave(const std::string& deviceId,
                                        uint64_t subSeed,
                                        const std::vector<std::string>& usedNames);

    // Manifest persistence (tracks generated presets + seeds + locks).
    std::vector<GeneratedPreset> loadManifest() const;
    void saveManifest(const std::vector<GeneratedPreset>& presets) const;
    static std::filesystem::path manifestPath();

    const GenOptions& options() const { return m_opts; }
    void setOptions(const GenOptions& o) { m_opts = o; }

private:
    // Special path for the Instrument Rack: compose 1-4 randomized
    // self-contained sub-instruments into layered / key-split /
    // velocity-split chains (huge variety). Validated as a whole
    // instrument; saved via the rack's saveExtraState.
    GeneratedPreset generateRackAndSave(uint64_t subSeed,
                                        const std::vector<std::string>& usedNames);

    GenOptions m_opts;
};

} // namespace presets
} // namespace yawn
