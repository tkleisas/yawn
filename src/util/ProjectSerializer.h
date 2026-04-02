#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <fstream>
#include <filesystem>
#include <cstdio>

#include "app/Project.h"
#include "audio/AudioEngine.h"
#include "audio/Clip.h"
#include "audio/AudioBuffer.h"
#include "midi/MidiClip.h"
#include "instruments/Instrument.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffect.h"
#include "midi/MidiEffectChain.h"
#include "util/Factory.h"
#include "util/FileIO.h"

namespace yawn {

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace FileIO = util;

// Current file format version. Bump when making breaking changes.
static constexpr int kFormatVersion = 1;

// ---------------------------------------------------------------------------
// Generic parameter serialization (works for any Instrument/Effect/MidiEffect)
// ---------------------------------------------------------------------------

template<typename T>
inline json serializeParams(const T& obj) {
    json params = json::object();
    for (int i = 0; i < obj.parameterCount(); ++i) {
        auto& info = obj.parameterInfo(i);
        if (info.isPerVoice) continue;  // per-voice params serialized separately
        params[info.name] = obj.getParameter(i);
    }
    return params;
}

template<typename T>
inline void deserializeParams(T& obj, const json& params) {
    for (int i = 0; i < obj.parameterCount(); ++i) {
        auto& info = obj.parameterInfo(i);
        if (info.isPerVoice) continue;  // per-voice params deserialized separately
        if (params.contains(info.name)) {
            obj.setParameter(i, params[info.name].get<float>());
        }
    }
}

// ---------------------------------------------------------------------------
// Effect chain serialization
// ---------------------------------------------------------------------------

json serializeEffectChain(const effects::EffectChain& chain);
void deserializeEffectChain(effects::EffectChain& chain, const json& arr,
                            double sampleRate, int maxBlockSize);

// ---------------------------------------------------------------------------
// MIDI effect chain serialization
// ---------------------------------------------------------------------------

json serializeMidiEffectChain(const midi::MidiEffectChain& chain);
void deserializeMidiEffectChain(midi::MidiEffectChain& chain, const json& arr,
                                double sampleRate);

// ---------------------------------------------------------------------------
// Instrument serialization (with custom handling for Sampler/DrumRack/DrumSlop)
// ---------------------------------------------------------------------------

json serializeInstrument(const instruments::Instrument& inst,
                         const fs::path& samplesDir, int& sampleCounter);
std::unique_ptr<instruments::Instrument> deserializeInstrument(
    const json& j, const fs::path& projectDir, double sampleRate, int maxBlockSize);

// ---------------------------------------------------------------------------
// MIDI clip serialization
// ---------------------------------------------------------------------------

json serializeMidiClip(const midi::MidiClip& clip);
std::unique_ptr<midi::MidiClip> deserializeMidiClip(const json& j);

// ---------------------------------------------------------------------------
// Audio clip serialization
// ---------------------------------------------------------------------------

json serializeAudioClip(const audio::Clip& clip, const fs::path& samplesDir,
                        int& sampleCounter);
std::unique_ptr<audio::Clip> deserializeAudioClip(const json& j,
                                                   const fs::path& projectDir);

// ---------------------------------------------------------------------------
// Mixer state serialization
// ---------------------------------------------------------------------------

json serializeMixer(const audio::Mixer& mixer, int numTracks,
                    double sampleRate, int maxBlockSize);
void deserializeMixer(audio::Mixer& mixer, const json& j,
                      double sampleRate, int maxBlockSize);

// ---------------------------------------------------------------------------
// ProjectSerializer — top-level save/load
// ---------------------------------------------------------------------------

class ProjectSerializer {
public:

    // Save the entire project to a .yawn folder.
    // Returns true on success.
    static bool saveToFolder(const fs::path& folderPath,
                             const Project& project,
                             const audio::AudioEngine& engine);

    // Load a project from a .yawn folder.
    // Returns true on success.
    static bool loadFromFolder(const fs::path& folderPath,
                               Project& project,
                               audio::AudioEngine& engine);
};

} // namespace yawn
