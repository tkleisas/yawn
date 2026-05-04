#pragma once
//
// EffectChainJson.h — serialize / deserialize an effects::EffectChain
// to/from JSON without pulling in Project.h.
//
// Originally lived inside ProjectSerializer.cpp as out-of-line
// definitions. Hoisted here as inline templates/free functions so
// secondary call sites (per-pad fx in DrumRack, future per-zone fx
// in Multisampler) can serialise their own chains for kit-preset
// save/load without depending on the heavy project-serialiser
// header chain.

#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include "effects/EffectChain.h"
#include "effects/AudioEffect.h"
#include "util/Factory.h"
#include "util/Logger.h"
#include "util/Base64.h"

#ifdef YAWN_HAS_VST3
#include "vst3/VST3Effect.h"
#endif

namespace yawn {

using EcJson = nlohmann::json;

// Match the serializeParams template living in ProjectSerializer.h.
// Duplicated here so this header can stand alone; both copies do the
// same thing (named-param JSON). If they ever drift the project
// loader's keys-by-name resolution will keep the two formats
// interoperable.
template<typename T>
inline EcJson ecSerializeParams(const T& obj) {
    EcJson params = EcJson::object();
    for (int i = 0; i < obj.parameterCount(); ++i) {
        auto& info = obj.parameterInfo(i);
        if (info.isPerVoice) continue;
        params[info.name] = obj.getParameter(i);
    }
    return params;
}

template<typename T>
inline void ecDeserializeParams(T& obj, const EcJson& params) {
    for (int i = 0; i < obj.parameterCount(); ++i) {
        auto& info = obj.parameterInfo(i);
        if (info.isPerVoice) continue;
        if (params.contains(info.name))
            obj.setParameter(i, params[info.name].template get<float>());
    }
}

inline EcJson serializeEffectChainStandalone(const effects::EffectChain& chain) {
    EcJson arr = EcJson::array();
    for (int i = 0; i < chain.count(); ++i) {
        auto* fx = chain.effectAt(i);
        if (!fx) continue;
        EcJson j;
        j["id"]       = fx->id();
        j["bypassed"] = fx->bypassed();
        j["mix"]      = fx->mix();
        j["params"]   = ecSerializeParams(*fx);

#ifdef YAWN_HAS_VST3
        if (isVST3Id(fx->id())) {
            auto* vfx = static_cast<const vst3::VST3Effect*>(fx);
            j["vst3modulePath"] = vfx->modulePath();
            j["vst3classID"]    = vfx->classIDString();
            if (vfx->instance()) {
                std::vector<uint8_t> procState, ctrlState;
                if (vfx->instance()->getProcessorState(procState))
                    j["vst3state"] = base64Encode(procState);
                if (vfx->instance()->getControllerState(ctrlState))
                    j["vst3controllerState"] = base64Encode(ctrlState);
            }
        }
#endif

        EcJson extra = fx->saveExtraState(std::filesystem::path{});
        if (!extra.is_null() && !extra.empty())
            j["extraState"] = std::move(extra);

        arr.push_back(j);
    }
    return arr;
}

inline void deserializeEffectChainStandalone(
        effects::EffectChain& chain, const EcJson& arr,
        double sampleRate, int maxBlockSize) {
    chain.clear();
    for (const auto& j : arr) {
        std::string id = j.value("id", "");
        std::unique_ptr<effects::AudioEffect> fx;

#ifdef YAWN_HAS_VST3
        if (isVST3Id(id)) {
            std::string modulePath = j.value("vst3modulePath", "");
            std::string classID    = j.value("vst3classID", vst3ClassIDFromId(id));
            fx = createVST3Effect(modulePath, classID);
        } else
#endif
        {
            fx = createAudioEffect(id);
        }

        if (!fx) {
            LOG_WARN("Project", "Unknown effect ID '%s' in chain - skipping",
                     id.c_str());
            continue;
        }
        fx->init(sampleRate, maxBlockSize);
        fx->setBypassed(j.value("bypassed", false));
        fx->setMix(j.value("mix", 1.0f));

#ifdef YAWN_HAS_VST3
        if (isVST3Id(id)) {
            auto* vfx = static_cast<vst3::VST3Effect*>(fx.get());
            if (vfx->instance()) {
                if (j.contains("vst3state")) {
                    auto data = base64Decode(j["vst3state"].get<std::string>());
                    vfx->instance()->setProcessorState(data);
                }
                if (j.contains("vst3controllerState")) {
                    auto data = base64Decode(j["vst3controllerState"].get<std::string>());
                    vfx->instance()->setControllerState(data);
                }
            }
        }
#endif

        if (j.contains("params"))
            ecDeserializeParams(*fx, j["params"]);
        if (j.contains("extraState"))
            fx->loadExtraState(j["extraState"], std::filesystem::path{});

        chain.append(std::move(fx));
    }
}

} // namespace yawn
