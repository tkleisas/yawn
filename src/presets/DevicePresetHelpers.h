#pragma once

// DevicePresetHelpers — High-level save/load functions that bridge between
// PresetManager (file I/O) and the device base classes (Instrument, AudioEffect,
// MidiEffect). Handles VST3 binary state extraction/application automatically.

#include "presets/PresetManager.h"
#include "instruments/Instrument.h"
#include "effects/AudioEffect.h"
#include "midi/MidiEffect.h"

#ifdef YAWN_HAS_VST3
#include "vst3/VST3Instrument.h"
#include "vst3/VST3Effect.h"
#endif

namespace yawn {

// ── Parameter serialization (matching ProjectSerializer pattern) ────────────

inline json serializeDeviceParams(const instruments::Instrument& inst) {
    json params = json::object();
    for (int i = 0; i < inst.parameterCount(); ++i) {
        auto& info = inst.parameterInfo(i);
        if (info.isPerVoice) continue;
        params[info.name] = inst.getParameter(i);
    }
    return params;
}

inline json serializeDeviceParams(const effects::AudioEffect& fx) {
    json params = json::object();
    for (int i = 0; i < fx.parameterCount(); ++i) {
        auto& info = fx.parameterInfo(i);
        if (info.isPerVoice) continue;
        params[info.name] = fx.getParameter(i);
    }
    return params;
}

inline json serializeDeviceParams(const midi::MidiEffect& fx) {
    json params = json::object();
    for (int i = 0; i < fx.parameterCount(); ++i) {
        auto& info = fx.parameterInfo(i);
        if (info.isPerVoice) continue;
        params[info.name] = fx.getParameter(i);
    }
    return params;
}

inline void deserializeDeviceParams(instruments::Instrument& inst, const json& params) {
    for (int i = 0; i < inst.parameterCount(); ++i) {
        auto& info = inst.parameterInfo(i);
        if (info.isPerVoice) continue;
        if (params.contains(info.name))
            inst.setParameter(i, params[info.name].template get<float>());
    }
}

inline void deserializeDeviceParams(effects::AudioEffect& fx, const json& params) {
    for (int i = 0; i < fx.parameterCount(); ++i) {
        auto& info = fx.parameterInfo(i);
        if (info.isPerVoice) continue;
        if (params.contains(info.name))
            fx.setParameter(i, params[info.name].template get<float>());
    }
}

inline void deserializeDeviceParams(midi::MidiEffect& fx, const json& params) {
    for (int i = 0; i < fx.parameterCount(); ++i) {
        auto& info = fx.parameterInfo(i);
        if (info.isPerVoice) continue;
        if (params.contains(info.name))
            fx.setParameter(i, params[info.name].template get<float>());
    }
}

// ── High-level save: device → preset file ───────────────────────────────────

inline std::filesystem::path saveDevicePreset(const std::string& presetName,
                                               instruments::Instrument& inst) {
    json params = serializeDeviceParams(inst);
    std::vector<uint8_t> vst3State, vst3CtrlState;

#ifdef YAWN_HAS_VST3
    if (auto* vi = dynamic_cast<vst3::VST3Instrument*>(&inst)) {
        if (vi->instance()) {
            vi->instance()->getProcessorState(vst3State);
            vi->instance()->getControllerState(vst3CtrlState);
        }
    }
#endif

    // Capture instrument-specific extra state (Multisampler zones,
    // DrumRack pads, …). The Instrument writes any binary assets
    // (per-zone WAVs etc.) into the per-preset asset directory and
    // returns a JSON manifest pointing at them.
    const std::filesystem::path assetDir =
        PresetManager::presetAssetDir(inst.id(), presetName);
    json extra = inst.saveExtraState(assetDir);

    return PresetManager::savePreset(presetName, inst.id(), inst.name(),
                                     params, extra, vst3State, vst3CtrlState);
}

inline std::filesystem::path saveDevicePreset(const std::string& presetName,
                                               effects::AudioEffect& fx) {
    json params = serializeDeviceParams(fx);
    std::vector<uint8_t> vst3State, vst3CtrlState;

#ifdef YAWN_HAS_VST3
    if (auto* ve = dynamic_cast<vst3::VST3Effect*>(&fx)) {
        if (ve->instance()) {
            ve->instance()->getProcessorState(vst3State);
            ve->instance()->getControllerState(vst3CtrlState);
        }
    }
#endif

    return PresetManager::savePreset(presetName, fx.id(), fx.name(),
                                     params, vst3State, vst3CtrlState);
}

inline std::filesystem::path saveDevicePreset(const std::string& presetName,
                                               midi::MidiEffect& fx) {
    json params = serializeDeviceParams(fx);
    // MIDI effects are never VST3 currently
    return PresetManager::savePreset(presetName, fx.id(), fx.name(), params);
}

// ── High-level load: preset file → device ───────────────────────────────────

inline bool loadDevicePreset(const std::filesystem::path& filePath,
                              instruments::Instrument& inst) {
    PresetData data;
    if (!PresetManager::loadPreset(filePath, data)) return false;

#ifdef YAWN_HAS_VST3
    if (auto* vi = dynamic_cast<vst3::VST3Instrument*>(&inst)) {
        if (vi->instance() && !data.vst3State.empty()) {
            vi->instance()->setProcessorState(data.vst3State);
            if (!data.vst3ControllerState.empty())
                vi->instance()->setControllerState(data.vst3ControllerState);
            return true; // VST3 state includes all parameters
        }
    }
#endif

    deserializeDeviceParams(inst, data.params);
    // Restore instrument-specific extra state (zones / pads / sample
    // buffers). assetDir mirrors the save side: same parent folder as
    // the .json, named after the preset stem. Native instruments with
    // no extra state ignore this — the base Instrument default is a
    // no-op.
    if (!data.extraState.is_null() && !data.extraState.empty()) {
        const std::filesystem::path assetDir =
            filePath.parent_path() / filePath.stem();
        inst.loadExtraState(data.extraState, assetDir);
    }
    return true;
}

inline bool loadDevicePreset(const std::filesystem::path& filePath,
                              effects::AudioEffect& fx) {
    PresetData data;
    if (!PresetManager::loadPreset(filePath, data)) return false;

#ifdef YAWN_HAS_VST3
    if (auto* ve = dynamic_cast<vst3::VST3Effect*>(&fx)) {
        if (ve->instance() && !data.vst3State.empty()) {
            ve->instance()->setProcessorState(data.vst3State);
            if (!data.vst3ControllerState.empty())
                ve->instance()->setControllerState(data.vst3ControllerState);
            return true;
        }
    }
#endif

    deserializeDeviceParams(fx, data.params);
    return true;
}

inline bool loadDevicePreset(const std::filesystem::path& filePath,
                              midi::MidiEffect& fx) {
    PresetData data;
    if (!PresetManager::loadPreset(filePath, data)) return false;
    deserializeDeviceParams(fx, data.params);
    return true;
}

} // namespace yawn
