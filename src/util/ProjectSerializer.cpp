// ProjectSerializer.cpp — method implementations split from ProjectSerializer.h.

#include "ProjectSerializer.h"
#include "util/Base64.h"
#include "util/Logger.h"
#include "visual/VisualEngineAPI.h"

#ifdef YAWN_HAS_VST3
#include "vst3/VST3Instrument.h"
#include "vst3/VST3Effect.h"
#endif

namespace yawn {

// ---------------------------------------------------------------------------
// Effect chain serialization
// ---------------------------------------------------------------------------

json serializeEffectChain(const effects::EffectChain& chain) {
    json arr = json::array();
    for (int i = 0; i < chain.count(); ++i) {
        auto* fx = chain.effectAt(i);
        if (!fx) continue;
        json j;
        j["id"] = fx->id();
        j["bypassed"] = fx->bypassed();
        j["mix"] = fx->mix();
        j["params"] = serializeParams(*fx);

#ifdef YAWN_HAS_VST3
        if (isVST3Id(fx->id())) {
            auto* vfx = static_cast<const vst3::VST3Effect*>(fx);
            j["vst3modulePath"] = vfx->modulePath();
            j["vst3classID"] = vfx->classIDString();
            if (vfx->instance()) {
                std::vector<uint8_t> procState, ctrlState;
                if (vfx->instance()->getProcessorState(procState))
                    j["vst3state"] = base64Encode(procState);
                if (vfx->instance()->getControllerState(ctrlState))
                    j["vst3controllerState"] = base64Encode(ctrlState);
            }
        }
#endif

        // Generic extra-state hook (mirrors Instrument's pattern).
        // Effects that hold non-parametric state (e.g. NeuralAmp's
        // .nam model file path, future ConvolutionReverb's loaded
        // IR) override saveExtraState; everything else returns null
        // and we omit the key. assetDir is unused for the simple
        // string-storing case but available for effects that want
        // to write supporting binaries alongside the project.
        nlohmann::json extra = fx->saveExtraState(std::filesystem::path{});
        if (!extra.is_null() && !extra.empty())
            j["extraState"] = std::move(extra);

        arr.push_back(j);
    }
    return arr;
}

void deserializeEffectChain(effects::EffectChain& chain, const json& arr,
                            double sampleRate, int maxBlockSize) {
    chain.clear();
    for (const auto& j : arr) {
        std::string id = j.value("id", "");
        std::unique_ptr<effects::AudioEffect> fx;

#ifdef YAWN_HAS_VST3
        if (isVST3Id(id)) {
            std::string modulePath = j.value("vst3modulePath", "");
            std::string classID = j.value("vst3classID", vst3ClassIDFromId(id));
            fx = createVST3Effect(modulePath, classID);
        } else
#endif
        {
            fx = createAudioEffect(id);
        }

        if (!fx) {
            LOG_WARN("Project", "Unknown effect ID '%s' in chain - skipping", id.c_str());
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
        } else
#endif
        if (j.contains("params"))
            deserializeParams(*fx, j["params"]);

        // Restore generic extra state. See saveExtraState above for
        // the rationale — pure parametric effects ignore this.
        if (j.contains("extraState"))
            fx->loadExtraState(j["extraState"], std::filesystem::path{});

        chain.append(std::move(fx));
    }
}

// ---------------------------------------------------------------------------
// MIDI effect chain serialization
// ---------------------------------------------------------------------------

json serializeMidiEffectChain(const midi::MidiEffectChain& chain) {
    json arr = json::array();
    for (int i = 0; i < chain.count(); ++i) {
        auto* fx = chain.effect(i);
        if (!fx) continue;
        json j;
        j["id"] = fx->id();
        j["instanceId"] = fx->instanceId();
        j["bypassed"] = fx->bypassed();
        j["params"] = serializeParams(*fx);
        if (fx->isLinkedToSource())
            j["linkTargetId"] = fx->linkSourceId();
        arr.push_back(j);
    }
    return arr;
}

void deserializeMidiEffectChain(midi::MidiEffectChain& chain, const json& arr,
                                double sampleRate) {
    chain.clear();
    for (const auto& j : arr) {
        std::string id = j.value("id", "");
        auto fx = createMidiEffect(id);
        if (!fx) {
            LOG_WARN("Project", "Unknown MIDI effect ID '%s' in chain - skipping", id.c_str());
            continue;
        }
        fx->init(sampleRate);
        fx->setBypassed(j.value("bypassed", false));
        if (j.contains("instanceId"))
            fx->setInstanceId(j["instanceId"].get<uint32_t>());
        if (j.contains("params"))
            deserializeParams(*fx, j["params"]);
        // Restore LFO link target ID
        if (j.contains("linkTargetId") && id == "lfo") {
            auto* lfo = static_cast<midi::LFO*>(fx.get());
            lfo->setLinkTargetId(j["linkTargetId"].get<uint32_t>());
        }
        chain.addEffect(std::move(fx));
    }
}

// ---------------------------------------------------------------------------
// Instrument serialization (with custom handling for Sampler/DrumRack/DrumSlop)
// ---------------------------------------------------------------------------

json serializeInstrument(const instruments::Instrument& inst,
                         const fs::path& samplesDir, int& sampleCounter) {
    json j;
    j["id"] = inst.id();
    j["bypassed"] = inst.bypassed();
    j["params"] = serializeParams(inst);

#ifdef YAWN_HAS_VST3
    if (isVST3Id(inst.id())) {
        auto& vinst = static_cast<const vst3::VST3Instrument&>(inst);
        j["vst3modulePath"] = vinst.modulePath();
        j["vst3classID"] = vinst.classIDString();
        if (vinst.instance()) {
            std::vector<uint8_t> procState, ctrlState;
            if (vinst.instance()->getProcessorState(procState))
                j["vst3state"] = base64Encode(procState);
            if (vinst.instance()->getControllerState(ctrlState))
                j["vst3controllerState"] = base64Encode(ctrlState);
        }
        return j;  // No further custom serialization for VST3
    }
#endif

    // Sampler: save sample to file
    if (std::string(inst.id()) == "sampler") {
        auto& sampler = static_cast<const instruments::Sampler&>(inst);
        if (sampler.hasSample()) {
            std::string filename = "sample_" + std::to_string(sampleCounter++) + ".wav";
            fs::path samplePath = samplesDir / filename;
            FileIO::saveAudioFile(samplePath.string(),
                                  sampler.sampleData(), sampler.sampleFrames(),
                                  sampler.sampleChannels(), static_cast<int>(kDefaultSampleRate));
            j["sampleFile"] = "samples/" + filename;
        }
    }

    // Multisampler: save per-zone samples + zone metadata. Each zone's
    // audio lives as its own .wav under <project>.yawn/samples/; the
    // JSON tracks the playback metadata (root, key/vel ranges, tune,
    // vol, pan, loop). Samples are stored interleaved (matching the
    // Multisampler's internal Zone layout); save uses the engine's
    // current sample rate as the file's stored rate.
    if (std::string(inst.id()) == "multisampler") {
        auto& ms = static_cast<const instruments::Multisampler&>(inst);
        json zones = json::array();
        for (int i = 0; i < ms.zoneCount(); ++i) {
            const auto* z = ms.zone(i);
            if (!z || z->sampleFrames <= 0) continue;
            std::string filename = "ms_zone_" + std::to_string(i) + "_" +
                                   std::to_string(sampleCounter++) + ".wav";
            fs::path samplePath = samplesDir / filename;
            FileIO::saveAudioFile(samplePath.string(),
                                  z->sampleData.data(), z->sampleFrames,
                                  z->sampleChannels, static_cast<int>(kDefaultSampleRate));
            json zj;
            zj["sampleFile"] = "samples/" + filename;
            zj["sampleRate"] = (z->sampleRate > 0)
                ? z->sampleRate
                : static_cast<int>(kDefaultSampleRate);
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
        j["zones"] = zones;
    }

    // DrumRack: save per-pad samples
    if (std::string(inst.id()) == "drumrack") {
        auto& rack = static_cast<const instruments::DrumRack&>(inst);
        json pads = json::array();
        for (int note = 0; note < 128; ++note) {
            const auto& p = rack.pad(note);
            if (p.sampleFrames <= 0) continue;
            std::string filename = "pad_" + std::to_string(note) + "_" +
                                   std::to_string(sampleCounter++) + ".wav";
            fs::path samplePath = samplesDir / filename;
            FileIO::saveAudioFile(samplePath.string(),
                                  p.sampleData.data(), p.sampleFrames,
                                  p.sampleChannels, static_cast<int>(kDefaultSampleRate));
            json padJ;
            padJ["note"] = note;
            padJ["sampleFile"] = "samples/" + filename;
            padJ["volume"] = p.volume;
            padJ["pan"] = p.pan;
            padJ["pitchAdjust"] = p.pitchAdjust;
            pads.push_back(padJ);
        }
        j["pads"] = pads;
    }

    // DrumSlop: save loop sample + per-pad settings
    if (std::string(inst.id()) == "drumslop") {
        auto& ds = static_cast<const instruments::DrumSlop&>(inst);
        if (ds.hasLoop()) {
            std::string filename = "loop_" + std::to_string(sampleCounter++) + ".wav";
            fs::path samplePath = samplesDir / filename;
            FileIO::saveAudioFile(samplePath.string(),
                                  ds.loopData(), ds.loopFrames(),
                                  ds.loopChannels(), static_cast<int>(kDefaultSampleRate));
            j["loopFile"] = "samples/" + filename;
        }
        json padsArr = json::array();
        for (int i = 0; i < instruments::DrumSlop::kNumPads; ++i) {
            const auto& p = ds.pad(i);
            json padJ;
            padJ["volume"] = p.volume;
            padJ["pan"] = p.pan;
            padJ["pitch"] = p.pitch;
            padJ["reverse"] = p.reverse;
            padJ["filterCutoff"] = p.filterCutoff;
            padJ["filterReso"] = p.filterReso;
            padJ["startOffset"] = p.startOffset;
            padJ["attack"] = p.attack;
            padJ["decay"] = p.decay;
            padJ["sustain"] = p.sustain;
            padJ["release"] = p.release;
            padsArr.push_back(padJ);
        }
        j["dslopPads"] = padsArr;
        j["selectedPad"] = ds.selectedPad();
    }

    // InstrumentRack: save chains with nested instruments
    if (std::string(inst.id()) == "instrack") {
        auto& rack = static_cast<const instruments::InstrumentRack&>(inst);
        json chains = json::array();
        for (int i = 0; i < rack.chainCount(); ++i) {
            const auto& ch = rack.chain(i);
            json cj;
            cj["keyLow"] = ch.keyLow;
            cj["keyHigh"] = ch.keyHigh;
            cj["velLow"] = ch.velLow;
            cj["velHigh"] = ch.velHigh;
            cj["volume"] = ch.volume;
            cj["pan"] = ch.pan;
            cj["enabled"] = ch.enabled;
            if (ch.instrument)
                cj["instrument"] = serializeInstrument(*ch.instrument,
                                                       samplesDir, sampleCounter);
            chains.push_back(cj);
        }
        j["chains"] = chains;
    }

    return j;
}

std::unique_ptr<instruments::Instrument> deserializeInstrument(
    const json& j, const fs::path& projectDir, double sampleRate, int maxBlockSize)
{
    std::string id = j.value("id", "");
    std::unique_ptr<instruments::Instrument> inst;

#ifdef YAWN_HAS_VST3
    if (isVST3Id(id)) {
        std::string modulePath = j.value("vst3modulePath", "");
        std::string classID = j.value("vst3classID", vst3ClassIDFromId(id));
        inst = createVST3Instrument(modulePath, classID);
        if (!inst) {
            LOG_WARN("Project", "Failed to load VST3 instrument '%s' (%s) - skipping",
                     modulePath.c_str(), classID.c_str());
            return nullptr;
        }
        inst->init(sampleRate, maxBlockSize);
        inst->setBypassed(j.value("bypassed", false));
        auto* vinst = static_cast<vst3::VST3Instrument*>(inst.get());
        if (vinst->instance()) {
            if (j.contains("vst3state")) {
                auto data = base64Decode(j["vst3state"].get<std::string>());
                vinst->instance()->setProcessorState(data);
            }
            if (j.contains("vst3controllerState")) {
                auto data = base64Decode(j["vst3controllerState"].get<std::string>());
                vinst->instance()->setControllerState(data);
            }
        }
        return inst;
    }
#endif

    inst = createInstrument(id);
    if (!inst) {
        LOG_WARN("Project", "Unknown instrument ID '%s' - skipping", id.c_str());
        return nullptr;
    }

    inst->init(sampleRate, maxBlockSize);
    inst->setBypassed(j.value("bypassed", false));
    if (j.contains("params"))
        deserializeParams(*inst, j["params"]);

    // Sampler: load sample from file
    if (id == "sampler" && j.contains("sampleFile")) {
        auto& sampler = static_cast<instruments::Sampler&>(*inst);
        fs::path samplePath = projectDir / j["sampleFile"].get<std::string>();
        auto buf = FileIO::loadAudioFile(samplePath.string());
        if (buf) {
            sampler.loadSample(buf->data(), buf->numFrames(), buf->numChannels());
        } else {
            LOG_WARN("Project", "Sample file not found: %s", samplePath.string().c_str());
        }
    }

    // Multisampler: load per-zone samples + zone metadata. Mirrors the
    // save side. AudioBuffer is non-interleaved; the Multisampler's
    // Zone struct holds interleaved sampleData, so we de-stripe the
    // channels back into [f0c0, f0c1, ..., f1c0, f1c1, ...] order.
    if (id == "multisampler" && j.contains("zones")) {
        auto& ms = static_cast<instruments::Multisampler&>(*inst);
        ms.clearZones();
        for (const auto& zj : j["zones"]) {
            std::string relPath = zj.value("sampleFile", "");
            if (relPath.empty()) continue;
            fs::path samplePath = projectDir / relPath;
            auto buf = FileIO::loadAudioFile(samplePath.string());
            if (!buf) {
                LOG_WARN("Project", "Multisampler zone sample missing: %s",
                         samplePath.string().c_str());
                continue;
            }
            const int nf = buf->numFrames();
            const int nc = buf->numChannels();
            instruments::Multisampler::Zone z;
            z.sampleData.resize(static_cast<size_t>(nf) * nc);
            for (int ch = 0; ch < nc; ++ch) {
                const float* src = buf->channelData(ch);
                for (int f = 0; f < nf; ++f)
                    z.sampleData[f * nc + ch] = src[f];
            }
            z.sampleFrames   = nf;
            z.sampleChannels = nc;
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
            ms.addZone(z);
        }
    }

    // DrumRack: load per-pad samples
    if (id == "drumrack" && j.contains("pads")) {
        auto& rack = static_cast<instruments::DrumRack&>(*inst);
        for (const auto& padJ : j["pads"]) {
            int note = padJ.value("note", 0);
            std::string relPath = padJ.value("sampleFile", "");
            if (relPath.empty()) continue;
            fs::path samplePath = projectDir / relPath;
            auto buf = FileIO::loadAudioFile(samplePath.string());
            if (buf) {
                rack.loadPad(note, buf->data(), buf->numFrames(), buf->numChannels());
            } else {
                LOG_WARN("Project", "DrumRack pad %d sample missing: %s", note, samplePath.string().c_str());
            }
            if (padJ.contains("volume"))
                rack.setPadVolume(note, padJ["volume"].get<float>());
            if (padJ.contains("pan"))
                rack.setPadPan(note, padJ["pan"].get<float>());
            if (padJ.contains("pitchAdjust"))
                rack.setPadPitch(note, padJ["pitchAdjust"].get<float>());
        }
    }

    // DrumSlop: load loop sample + per-pad settings
    if (id == "drumslop") {
        auto& ds = static_cast<instruments::DrumSlop&>(*inst);
        if (j.contains("loopFile")) {
            fs::path samplePath = projectDir / j["loopFile"].get<std::string>();
            auto buf = FileIO::loadAudioFile(samplePath.string());
            if (buf) {
                // Convert non-interleaved AudioBuffer to interleaved for DrumSlop
                int nf = buf->numFrames(), nc = buf->numChannels();
                std::vector<float> interleaved(nf * nc);
                for (int ch = 0; ch < nc; ++ch) {
                    const float* src = buf->channelData(ch);
                    for (int f = 0; f < nf; ++f)
                        interleaved[f * nc + ch] = src[f];
                }
                ds.loadLoop(interleaved.data(), nf, nc);
            } else {
                LOG_WARN("Project", "DrumSlop loop sample missing: %s", samplePath.string().c_str());
            }
        }
        if (j.contains("dslopPads")) {
            int idx = 0;
            for (const auto& padJ : j["dslopPads"]) {
                if (idx >= instruments::DrumSlop::kNumPads) break;
                if (padJ.contains("volume"))      ds.setPadVolume(idx, padJ["volume"].get<float>());
                if (padJ.contains("pan"))          ds.setPadPan(idx, padJ["pan"].get<float>());
                if (padJ.contains("pitch"))        ds.setPadPitch(idx, padJ["pitch"].get<float>());
                if (padJ.contains("reverse"))      ds.setPadReverse(idx, padJ["reverse"].get<bool>());
                if (padJ.contains("filterCutoff")) ds.setPadFilterCutoff(idx, padJ["filterCutoff"].get<float>());
                if (padJ.contains("filterReso"))   ds.setPadFilterReso(idx, padJ["filterReso"].get<float>());
                if (padJ.contains("startOffset"))  ds.setPadStartOffset(idx, padJ["startOffset"].get<float>());
                if (padJ.contains("attack"))       ds.setPadAttack(idx, padJ["attack"].get<float>());
                if (padJ.contains("decay"))        ds.setPadDecay(idx, padJ["decay"].get<float>());
                if (padJ.contains("sustain"))      ds.setPadSustain(idx, padJ["sustain"].get<float>());
                if (padJ.contains("release"))      ds.setPadRelease(idx, padJ["release"].get<float>());
                ++idx;
            }
        }
        if (j.contains("selectedPad"))
            ds.setSelectedPad(j["selectedPad"].get<int>());
    }

    // InstrumentRack: load chains with nested instruments
    if (id == "instrack" && j.contains("chains")) {
        auto& rack = static_cast<instruments::InstrumentRack&>(*inst);
        for (const auto& cj : j["chains"]) {
            std::unique_ptr<instruments::Instrument> chainInst;
            if (cj.contains("instrument"))
                chainInst = deserializeInstrument(cj["instrument"], projectDir,
                                                  sampleRate, maxBlockSize);
            uint8_t kl = cj.value("keyLow",  (uint8_t)0);
            uint8_t kh = cj.value("keyHigh", (uint8_t)127);
            uint8_t vl = cj.value("velLow",  (uint8_t)1);
            uint8_t vh = cj.value("velHigh", (uint8_t)127);
            if (chainInst) {
                rack.addChain(std::move(chainInst), kl, kh, vl, vh);
            } else {
                LOG_WARN("Project", "InstrumentRack chain instrument failed to load - skipping chain");
                continue;
            }
            // Set chain-level params after adding
            int ci = rack.chainCount() - 1;
            if (ci >= 0) {
                rack.chain(ci).volume = cj.value("volume", 1.0f);
                rack.chain(ci).pan = cj.value("pan", 0.0f);
                rack.chain(ci).enabled = cj.value("enabled", true);
            }
        }
    }

    return inst;
}

// ---------------------------------------------------------------------------
// MIDI clip serialization
// ---------------------------------------------------------------------------

json serializeMidiClip(const midi::MidiClip& clip) {
    json j;
    j["name"] = clip.name();
    j["type"] = "midi";
    j["lengthBeats"] = clip.lengthBeats();
    j["loop"] = clip.loop();

    json notes = json::array();
    for (int i = 0; i < clip.noteCount(); ++i) {
        const auto& n = clip.note(i);
        json nj;
        nj["start"] = n.startBeat;
        nj["dur"] = n.duration;
        nj["pitch"] = n.pitch;
        nj["vel"] = n.velocity;
        nj["ch"] = n.channel;
        if (n.releaseVelocity != 0) nj["relVel"] = n.releaseVelocity;
        if (n.pressure != 0.0f) nj["pressure"] = n.pressure;
        if (n.slide != 0.5f) nj["slide"] = n.slide;
        if (n.pitchBendOffset != 0.0f) nj["pbOff"] = n.pitchBendOffset;
        notes.push_back(nj);
    }
    j["notes"] = notes;

    json ccEvents = json::array();
    for (int i = 0; i < clip.ccCount(); ++i) {
        const auto& cc = clip.ccEvent(i);
        json ccj;
        ccj["beat"] = cc.beat;
        ccj["cc"] = cc.ccNumber;
        ccj["val"] = cc.value;
        ccj["ch"] = cc.channel;
        ccEvents.push_back(ccj);
    }
    j["ccEvents"] = ccEvents;

    return j;
}

std::unique_ptr<midi::MidiClip> deserializeMidiClip(const json& j) {
    auto clip = std::make_unique<midi::MidiClip>();
    clip->setName(j.value("name", ""));
    clip->setLengthBeats(j.value("lengthBeats", 4.0));
    clip->setLoop(j.value("loop", true));

    if (j.contains("notes")) {
        for (const auto& nj : j["notes"]) {
            midi::MidiNote n;
            n.startBeat = nj.value("start", 0.0);
            n.duration = nj.value("dur", 0.25);
            n.pitch = nj.value("pitch", 60);
            n.velocity = nj.value("vel", 32512);
            n.channel = nj.value("ch", 0);
            n.releaseVelocity = nj.value("relVel", 0);
            n.pressure = nj.value("pressure", 0.0f);
            n.slide = nj.value("slide", 0.5f);
            n.pitchBendOffset = nj.value("pbOff", 0.0f);
            clip->addNote(n);
        }
    }

    if (j.contains("ccEvents")) {
        for (const auto& ccj : j["ccEvents"]) {
            midi::MidiCCEvent cc;
            cc.beat = ccj.value("beat", 0.0);
            cc.ccNumber = ccj.value("cc", 0);
            cc.value = ccj.value("val", 0u);
            cc.channel = ccj.value("ch", 0);
            clip->addCC(cc);
        }
    }

    return clip;
}

// ---------------------------------------------------------------------------
// Audio clip serialization
// ---------------------------------------------------------------------------

json serializeAudioClip(const audio::Clip& clip, const fs::path& samplesDir,
                        int& sampleCounter) {
    json j;
    j["name"] = clip.name;
    j["type"] = "audio";
    j["loopStart"] = clip.loopStart;
    j["loopEnd"] = clip.loopEnd;
    j["looping"] = clip.looping;
    j["gain"] = clip.gain;
    j["transposeSemitones"] = clip.transposeSemitones;
    j["detuneCents"] = clip.detuneCents;

    if (clip.buffer&& clip.buffer->numFrames() > 0) {
        std::string filename = "clip_" + std::to_string(sampleCounter++) + ".wav";
        fs::path samplePath = samplesDir / filename;
        FileIO::saveAudioBuffer(samplePath.string(), *clip.buffer, static_cast<int>(kDefaultSampleRate));
        j["sampleFile"] = "samples/" + filename;
    }

    return j;
}

std::unique_ptr<audio::Clip> deserializeAudioClip(const json& j,
                                                   const fs::path& projectDir) {
    auto clip = std::make_unique<audio::Clip>();
    clip->name = j.value("name", "");
    clip->loopStart = j.value("loopStart", (int64_t)0);
    clip->loopEnd = j.value("loopEnd", (int64_t)-1);
    clip->looping = j.value("looping", true);
    clip->gain = j.value("gain", 1.0f);
    clip->transposeSemitones = j.value("transposeSemitones", 0);
    clip->detuneCents = j.value("detuneCents", 0);

    if (j.contains("sampleFile")){
        fs::path samplePath = projectDir / j["sampleFile"].get<std::string>();
        auto buf = FileIO::loadAudioFile(samplePath.string());
        if (buf) {
            clip->buffer = std::move(buf);
        } else {
            LOG_WARN("Project", "Clip sample missing: %s", samplePath.string().c_str());
        }
    }

    return clip;
}

// ---------------------------------------------------------------------------
// Mixer state serialization
// ---------------------------------------------------------------------------

json serializeMixer(const audio::Mixer& mixer, int numTracks,
                    double sampleRate, int maxBlockSize) {
    json j;

    // Track channels
    json tracks = json::array();
    for (int t = 0; t < numTracks; ++t) {
        const auto& ch = mixer.trackChannel(t);
        json tj;
        tj["volume"] = ch.volume;
        tj["pan"] = ch.pan;
        tj["muted"] = ch.muted;
        tj["soloed"] = ch.soloed;

        json sends = json::array();
        for (int s = 0; s < kMaxSendsPerTrack; ++s) {
            const auto& send = ch.sends[s];
            if (!send.enabled && send.level <= 0.0f) continue;
            json sj;
            sj["bus"] = s;
            sj["level"] = send.level;
            sj["mode"] = (send.mode == audio::SendMode::PreFader) ? "PreFader" : "PostFader";
            sj["enabled"] = send.enabled;
            sends.push_back(sj);
        }
        tj["sends"] = sends;
        tj["audioEffects"] = serializeEffectChain(
            const_cast<audio::Mixer&>(mixer).trackEffects(t));
        tracks.push_back(tj);
    }
    j["tracks"] = tracks;

    // Return buses
    json returns = json::array();
    for (int r = 0; r < kMaxReturnBuses; ++r) {
        const auto& rb = mixer.returnBus(r);
        json rj;
        rj["volume"] = rb.volume;
        rj["pan"] = rb.pan;
        rj["muted"] = rb.muted;
        rj["soloed"] = rb.soloed;
        rj["audioEffects"] = serializeEffectChain(
            const_cast<audio::Mixer&>(mixer).returnEffects(r));
        returns.push_back(rj);
    }
    j["returns"] = returns;

    // Master
    json master;
    master["volume"] = mixer.master().volume;
    master["audioEffects"] = serializeEffectChain(
        const_cast<audio::Mixer&>(mixer).masterEffects());
    j["master"] = master;

    return j;
}

void deserializeMixer(audio::Mixer& mixer, const json& j,
                      double sampleRate, int maxBlockSize) {
    if (j.contains("tracks")) {
        int t = 0;
        for (const auto& tj : j["tracks"]) {
            if (t >= kMaxTracks) break;
            mixer.setTrackVolume(t, tj.value("volume", 1.0f));
            mixer.setTrackPan(t, tj.value("pan", 0.0f));
            mixer.setTrackMute(t, tj.value("muted", false));
            mixer.setTrackSolo(t, tj.value("soloed", false));

            if (tj.contains("sends")) {
                for (const auto& sj : tj["sends"]) {
                    int bus = sj.value("bus", 0);
                    mixer.setSendLevel(t, bus, sj.value("level", 0.0f));
                    std::string mode = sj.value("mode", "PostFader");
                    mixer.setSendMode(t, bus,
                        mode == "PreFader" ? audio::SendMode::PreFader
                                           : audio::SendMode::PostFader);
                    mixer.setSendEnabled(t, bus, sj.value("enabled", false));
                }
            }

            if (tj.contains("audioEffects"))
                deserializeEffectChain(mixer.trackEffects(t), tj["audioEffects"],
                                       sampleRate, maxBlockSize);
            ++t;
        }
    }

    if (j.contains("returns")) {
        int r = 0;
        for (const auto& rj : j["returns"]) {
            if (r >= kMaxReturnBuses) break;
            mixer.setReturnVolume(r, rj.value("volume", 1.0f));
            mixer.setReturnPan(r, rj.value("pan", 0.0f));
            mixer.setReturnMute(r, rj.value("muted", false));

            if (rj.contains("audioEffects"))
                deserializeEffectChain(mixer.returnEffects(r), rj["audioEffects"],
                                       sampleRate, maxBlockSize);
            ++r;
        }
    }

    if (j.contains("master")) {
        const auto& mj = j["master"];
        mixer.setMasterVolume(mj.value("volume", 1.0f));
        if (mj.contains("audioEffects"))
            deserializeEffectChain(mixer.masterEffects(), mj["audioEffects"],
                                   sampleRate, maxBlockSize);
    }
}

// ---------------------------------------------------------------------------
// Arrangement clip serialization
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Visual clip field serialization — extracted so both the session-grid
// clip path and the arrangement-clip path stay in lock-step. Emits just
// the data fields (no "type" tag, no wrapper); the caller composes the
// envelope JSON around it.
// ---------------------------------------------------------------------------

void serializeVisualClipFields(const visual::VisualClip& vc, json& j) {
    j["name"]        = vc.name;
    j["colorIndex"]  = vc.colorIndex;
    j["lengthBeats"] = vc.lengthBeats;
    j["audioSource"] = vc.audioSource;

    // Source pass — the per-clip generator. Empty path means the clip
    // has no source shader (relies on video / model / live alone).
    // Effects no longer live here; the track's visualEffectChain owns
    // them so they apply across whichever clip plays on the track.
    if (!vc.source.shaderPath.empty() || !vc.source.paramValues.empty()) {
        json sj;
        sj["shaderPath"] = vc.source.shaderPath;
        if (!vc.source.paramValues.empty()) {
            json params = json::object();
            for (auto& kv : vc.source.paramValues) params[kv.first] = kv.second;
            sj["params"] = params;
        }
        j["source"] = std::move(sj);
    }

    // (Per-clip "lfos" no longer written — LFO state migrated to the
    // track-level MacroDevice in Phase 4.1. Legacy "lfos" entries on
    // load are silently dropped; there's no reasonable per-track
    // mapping when one track has many clips with different LFO sets.)
    if (!vc.text.empty())             j["text"]            = vc.text;
    if (!vc.videoPath.empty())        j["videoPath"]       = vc.videoPath;
    if (!vc.thumbnailPath.empty())    j["thumbnailPath"]   = vc.thumbnailPath;
    if (!vc.videoSourcePath.empty())  j["videoSourcePath"] = vc.videoSourcePath;
    if (vc.videoLoopBars != 0)        j["videoLoopBars"]   = vc.videoLoopBars;
    if (std::abs(vc.videoRate - 1.0f) > 0.001f) j["videoRate"] = vc.videoRate;
    if (vc.videoIn  != 0.0f)          j["videoIn"]         = vc.videoIn;
    if (vc.videoOut != 1.0f)          j["videoOut"]        = vc.videoOut;
    if (vc.liveInput)                 j["liveInput"]       = true;
    if (!vc.liveUrl.empty())          j["liveUrl"]         = vc.liveUrl;
    if (!vc.modelPath.empty())        j["modelPath"]       = vc.modelPath;
    if (!vc.modelSourcePath.empty())  j["modelSourcePath"] = vc.modelSourcePath;
    if (!vc.scenePath.empty())        j["scenePath"]       = vc.scenePath;
}

std::unique_ptr<visual::VisualClip> deserializeVisualClipFields(const json& val) {
    auto vc = std::make_unique<visual::VisualClip>();
    vc->name        = val.value("name", "");
    vc->colorIndex  = val.value("colorIndex", 0);
    vc->lengthBeats = val.value("lengthBeats", 4.0);
    vc->audioSource = val.value("audioSource", -1);

    // Source pass. New format is a single "source" object holding
    // shaderPath + sparse params. We still accept the prior shapes
    // (legacy "shaderChain" array, "shaderPath"+"params", and
    // "additionalPasses") and just take the first pass as the source —
    // any per-clip effect chain entries from older saves are dropped
    // since effects now live on the track and we can't reliably
    // associate clip-level entries with a specific track here.
    if (val.contains("source") && val["source"].is_object()) {
        const auto& sj = val["source"];
        vc->source.shaderPath = sj.value("shaderPath", "");
        if (sj.contains("params") && sj["params"].is_object())
            for (auto it = sj["params"].begin(); it != sj["params"].end(); ++it)
                vc->source.paramValues.emplace_back(it.key(), it.value().get<float>());
    } else if (val.contains("shaderChain") && val["shaderChain"].is_array()
               && !val["shaderChain"].empty()) {
        const auto& pj = val["shaderChain"].front();
        vc->source.shaderPath = pj.value("shaderPath", "");
        if (pj.contains("params") && pj["params"].is_object())
            for (auto it = pj["params"].begin(); it != pj["params"].end(); ++it)
                vc->source.paramValues.emplace_back(it.key(), it.value().get<float>());
    } else {
        vc->source.shaderPath = val.value("shaderPath", std::string{});
        if (val.contains("params") && val["params"].is_object())
            for (auto it = val["params"].begin(); it != val["params"].end(); ++it)
                vc->source.paramValues.emplace_back(it.key(), it.value().get<float>());
    }

    vc->text            = val.value("text",            std::string());
    vc->videoPath       = val.value("videoPath",       std::string());
    vc->thumbnailPath   = val.value("thumbnailPath",   std::string());
    vc->videoSourcePath = val.value("videoSourcePath", std::string());
    vc->videoLoopBars   = val.value("videoLoopBars",   0);
    vc->videoRate       = val.value("videoRate",       1.0f);
    vc->videoIn         = val.value("videoIn",         0.0f);
    vc->videoOut        = val.value("videoOut",        1.0f);
    vc->liveInput       = val.value("liveInput",       false);
    vc->liveUrl         = val.value("liveUrl",         std::string());
    vc->modelPath       = val.value("modelPath",       std::string());
    vc->modelSourcePath = val.value("modelSourcePath", std::string());
    vc->scenePath       = val.value("scenePath",       std::string());
    // Legacy "lfos" key on the clip — silently ignored. LFOs now
    // live on the track's MacroDevice (see deserialize for tracks).
    return vc;
}

json serializeArrangementClip(const ArrangementClip& clip,
                               const fs::path& samplesDir, int& sampleCounter) {
    json j;
    const char* typeStr = "Audio";
    switch (clip.type) {
        case ArrangementClip::Type::Audio:  typeStr = "Audio";  break;
        case ArrangementClip::Type::Midi:   typeStr = "Midi";   break;
        case ArrangementClip::Type::Visual: typeStr = "Visual"; break;
    }
    j["type"] = typeStr;
    j["startBeat"] = clip.startBeat;
    j["lengthBeats"] = clip.lengthBeats;
    j["offsetBeats"] = clip.offsetBeats;
    j["name"] = clip.name;
    j["colorIndex"] = clip.colorIndex;

    if (clip.type == ArrangementClip::Type::Audio && clip.audioBuffer) {
        // Save the audio buffer reference
        std::string filename = "arr_sample_" + std::to_string(sampleCounter++) + ".wav";
        fs::path samplePath = samplesDir / filename;
        FileIO::saveAudioBuffer(samplePath.string(), *clip.audioBuffer, static_cast<int>(kDefaultSampleRate));
        j["sampleFile"] = filename;
    } else if (clip.type == ArrangementClip::Type::Midi && clip.midiClip) {
        j["midiClip"] = serializeMidiClip(*clip.midiClip);
    } else if (clip.type == ArrangementClip::Type::Visual && clip.visualClip) {
        // Inline the visual-clip data fields under a "visual" key so
        // the outer "type"/"startBeat"/etc. envelope stays clean.
        json vj;
        serializeVisualClipFields(*clip.visualClip, vj);
        j["visual"] = std::move(vj);
    }

    return j;
}

ArrangementClip deserializeArrangementClip(const json& j,
                                            const fs::path& projectDir) {
    ArrangementClip clip;
    std::string typeStr = j.value("type", "Audio");
    if      (typeStr == "Midi")   clip.type = ArrangementClip::Type::Midi;
    else if (typeStr == "Visual") clip.type = ArrangementClip::Type::Visual;
    else                           clip.type = ArrangementClip::Type::Audio;
    clip.startBeat = j.value("startBeat", 0.0);
    clip.lengthBeats = j.value("lengthBeats", 4.0);
    clip.offsetBeats = j.value("offsetBeats", 0.0);
    clip.name = j.value("name", "");
    clip.colorIndex = j.value("colorIndex", -1);

    if (clip.type == ArrangementClip::Type::Audio && j.contains("sampleFile")) {
        fs::path samplePath = projectDir / "samples" / j["sampleFile"].get<std::string>();
        auto buf = FileIO::loadAudioFile(samplePath.string());
        if (buf) {
            clip.audioBuffer = std::move(buf);
        } else {
            LOG_WARN("Project", "Arrangement audio sample missing: %s", samplePath.string().c_str());
        }
    } else if (clip.type == ArrangementClip::Type::Midi && j.contains("midiClip")) {
        clip.midiClip = deserializeMidiClip(j["midiClip"]);
    } else if (clip.type == ArrangementClip::Type::Visual && j.contains("visual")) {
        clip.visualClip = deserializeVisualClipFields(j["visual"]);
    }

    return clip;
}

// ---------------------------------------------------------------------------
// ProjectSerializer — top-level save/load
// ---------------------------------------------------------------------------

bool ProjectSerializer::saveToFolder(const fs::path& folderPath,
                                     const Project& project,
                                     const audio::AudioEngine& engine,
                                     const midi::MidiLearnManager* learnMgr,
                                     const visual::VisualEngine* visualEngine) {
    // Create directory structure
    fs::create_directories(folderPath / "samples");

    double sr = engine.sampleRate();
    int blockSize = 256; // reasonable default for serialization

    int sampleCounter = 0;
    fs::path samplesDir = folderPath / "samples";

    json root;
    root["formatVersion"] = kFormatVersion;
#ifdef YAWN_VERSION_STRING
    root["appVersion"] = YAWN_VERSION_STRING;
#endif

    // Project metadata
    json proj;
    proj["bpm"] = engine.transport().bpm();
    proj["sampleRate"] = sr;
    proj["timeSignatureNumerator"] = engine.transport().numerator();
    proj["timeSignatureDenominator"] = engine.transport().denominator();
    proj["viewMode"] = static_cast<int>(project.viewMode());
    proj["arrangementLength"] = project.arrangementLength();
    root["project"] = proj;

    // Tracks
    json tracks = json::array();
    for (int t = 0; t < project.numTracks(); ++t) {
        const auto& tr = project.track(t);
        json tj;
        tj["name"] = tr.name;
        tj["type"] = (tr.type == Track::Type::Audio)  ? "Audio"
                    : (tr.type == Track::Type::Midi)   ? "Midi"
                                                         : "Visual";
        tj["colorIndex"] = tr.colorIndex;
        tj["volume"] = tr.volume;
        tj["muted"] = tr.muted;
        tj["soloed"] = tr.soloed;
        tj["midiInputPort"] = tr.midiInputPort;
        tj["midiInputChannel"] = tr.midiInputChannel;
        tj["armed"] = tr.armed;
        tj["defaultScene"] = tr.defaultScene;
        tj["sidechainSource"] = tr.sidechainSource;
        tj["resampleSource"] = tr.resampleSource;

        // Visual blend mode — only meaningful for Visual tracks but always
        // written for round-trip safety.
        {
            const char* bm = "Normal";
            switch (tr.visualBlendMode) {
                case Track::VisualBlendMode::Add:      bm = "Add";      break;
                case Track::VisualBlendMode::Multiply: bm = "Multiply"; break;
                case Track::VisualBlendMode::Screen:   bm = "Screen";   break;
                case Track::VisualBlendMode::Normal:   bm = "Normal";   break;
            }
            tj["visualBlendMode"] = bm;
        }

        // Visual effect chain — per-track, ordered. Empty array
        // omitted to keep saves slim for non-Visual tracks.
        if (!tr.visualEffectChain.empty()) {
            json chain = json::array();
            for (const auto& p : tr.visualEffectChain) {
                json pj;
                pj["shaderPath"] = p.shaderPath;
                if (!p.paramValues.empty()) {
                    json params = json::object();
                    for (auto& kv : p.paramValues) params[kv.first] = kv.second;
                    pj["params"] = params;
                }
                if (p.bypassed) pj["bypassed"] = true;
                chain.push_back(std::move(pj));
            }
            tj["visualEffectChain"] = std::move(chain);
        }

        // Macro device — always present on every track, but the JSON
        // representation is sparse: omit defaulted values, labels,
        // LFOs, and skip the whole block entirely if everything is
        // at default. Saves stay clean for fresh tracks.
        {
            json mj;
            // Values — only written when not at the 0.5 default.
            json vals = json::object();
            for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
                if (std::abs(tr.macros.values[i] - 0.5f) > 1e-6f)
                    vals[std::to_string(i)] = tr.macros.values[i];
            }
            if (!vals.empty()) mj["values"] = vals;
            // Labels — only non-empty entries.
            json labs = json::object();
            for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
                if (!tr.macros.labels[i].empty())
                    labs[std::to_string(i)] = tr.macros.labels[i];
            }
            if (!labs.empty()) mj["labels"] = labs;
            // LFOs — only enabled slots.
            json lfos = json::object();
            for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
                const auto& s = tr.macros.lfos[i];
                if (!s.enabled) continue;
                json lj;
                lj["shape"] = s.shape;
                lj["rate"]  = s.rate;
                lj["depth"] = s.depth;
                lj["sync"]  = s.sync;
                lfos[std::to_string(i)] = lj;
            }
            if (!lfos.empty()) mj["lfos"] = lfos;
            // Mappings — written as an ordered array.
            if (!tr.macros.mappings.empty()) {
                json maps = json::array();
                for (const auto& m : tr.macros.mappings) {
                    json mp;
                    mp["macro"] = m.macroIdx;
                    mp["kind"]  = static_cast<int>(m.target.kind);
                    if (m.target.index != 0)         mp["index"]    = m.target.index;
                    if (!m.target.paramName.empty()) mp["param"]    = m.target.paramName;
                    if (m.rangeMin != 0.0f)          mp["rangeMin"] = m.rangeMin;
                    if (m.rangeMax != 1.0f)          mp["rangeMax"] = m.rangeMax;
                    maps.push_back(std::move(mp));
                }
                mj["mappings"] = std::move(maps);
            }
            if (!mj.empty()) tj["macros"] = std::move(mj);
        }

        // Automation mode and lanes
        tj["autoMode"] = static_cast<int>(tr.autoMode);
        if (!tr.automationLanes.empty())
            tj["automationLanes"] = automation::lanesToJson(tr.automationLanes);

        // Arrangement clips
        tj["arrangementActive"] = tr.arrangementActive;
        if (!tr.arrangementClips.empty()) {
            json arrClips = json::array();
            for (auto& ac : tr.arrangementClips)
                arrClips.push_back(serializeArrangementClip(ac, samplesDir, sampleCounter));
            tj["arrangementClips"] = arrClips;
        }

        // Instrument
        auto* inst = const_cast<audio::AudioEngine&>(engine).instrument(t);
        if (inst) {
            tj["instrument"] = serializeInstrument(*inst, samplesDir, sampleCounter);
        }

        // MIDI effect chain
        auto& midiChain = const_cast<audio::AudioEngine&>(engine).midiEffectChain(t);
        if (!midiChain.empty()) {
            tj["midiEffects"] = serializeMidiEffectChain(midiChain);
        }

        tracks.push_back(tj);
    }
    root["tracks"] = tracks;

    // Scenes
    json scenes = json::array();
    for (int s = 0; s < project.numScenes(); ++s) {
        json sj;
        sj["name"] = project.scene(s).name;
        scenes.push_back(sj);
    }
    root["scenes"] = scenes;

    // Clips (sparse: only non-empty slots)
    json clips = json::object();
    for (int t = 0; t < project.numTracks(); ++t) {
        for (int s = 0; s < project.numScenes(); ++s) {
            auto* slot = project.getSlot(t, s);
            if (!slot || slot->empty()) continue;
            std::string key = std::to_string(t) + ":" + std::to_string(s);
            json clipJ;
            if (slot->audioClip)
                clipJ = serializeAudioClip(*slot->audioClip, samplesDir, sampleCounter);
            else if (slot->midiClip)
                clipJ = serializeMidiClip(*slot->midiClip);
            else if (slot->visualClip) {
                clipJ["type"] = "visual";
                serializeVisualClipFields(*slot->visualClip, clipJ);
            }
            if (!slot->clipAutomation.empty())
                clipJ["clipAutomation"] = automation::lanesToJson(slot->clipAutomation);
            // Follow action
            if (slot->followAction.enabled) {
                json fa;
                fa["enabled"] = true;
                fa["barCount"] = slot->followAction.barCount;
                fa["actionA"] = static_cast<int>(slot->followAction.actionA);
                fa["actionB"] = static_cast<int>(slot->followAction.actionB);
                fa["chanceA"] = slot->followAction.chanceA;
                clipJ["followAction"] = fa;
            }
            clips[key] = clipJ;
        }
    }
    root["clips"] = clips;

    // Mixer
    root["mixer"] = serializeMixer(engine.mixer(), project.numTracks(), sr, blockSize);

    // MIDI Mappings
    if (learnMgr && !learnMgr->mappings().empty()) {
        root["midiMappings"] = learnMgr->toJson();
    }

    // Visual post-FX chain — ordered list of { path, params } per effect.
    if (visualEngine) {
        json fx = json::array();
        int n = visual::postFX_count(*visualEngine);
        for (int i = 0; i < n; ++i) {
            json entry;
            entry["path"] = visual::postFX_path(*visualEngine, i);
            auto pv = visual::postFX_getParamValues(*visualEngine, i);
            if (!pv.empty()) {
                json pj = json::object();
                for (auto& kv : pv) pj[kv.first] = kv.second;
                entry["params"] = pj;
            }
            fx.push_back(entry);
        }
        if (!fx.empty()) root["visualPostFX"] = fx;
    }

    // Write project.json
    std::ofstream out(folderPath / "project.json");
    if (!out.is_open()) {
        LOG_ERROR("Project", "Failed to open %s for writing",
                  (folderPath / "project.json").string().c_str());
        return false;
    }
    out << root.dump(2);
    out.close();

    return true;
}

bool ProjectSerializer::loadFromFolder(const fs::path& folderPath,
                                       Project& project,
                                       audio::AudioEngine& engine,
                                       midi::MidiLearnManager* learnMgr,
                                       visual::VisualEngine* visualEngine) {
    fs::path jsonPath = folderPath / "project.json";
    if (!fs::exists(jsonPath)) {
        LOG_ERROR("Project", "Project file not found: %s", jsonPath.string().c_str());
        return false;
    }

    std::ifstream in(jsonPath);
    if (!in.is_open()) {
        LOG_ERROR("Project", "Failed to open %s for reading", jsonPath.string().c_str());
        return false;
    }

    json root;
    try {
        root = json::parse(in);
    } catch (const std::exception& e) {
        LOG_ERROR("Project", "JSON parse error in %s: %s", jsonPath.string().c_str(), e.what());
        in.close();
        return false;
    } catch (...) {
        LOG_ERROR("Project", "Unknown JSON parse error in %s", jsonPath.string().c_str());
        in.close();
        return false;
    }
    in.close();

    double sr = engine.sampleRate();
    int blockSize = 256;

    // Transport
    if (root.contains("project")) {
        const auto& proj = root["project"];
        engine.transport().setBPM(proj.value("bpm", 120.0));
        engine.transport().setTimeSignature(
            proj.value("timeSignatureNumerator", 4),
            proj.value("timeSignatureDenominator", 4));
        project.setViewMode(static_cast<ViewMode>(proj.value("viewMode", 0)));
        project.setArrangementLength(proj.value("arrangementLength", 64.0));
    }

    // Tracks & Scenes
    int numTracks = 0;
    int numScenes = 0;
    if (root.contains("tracks")) numTracks = static_cast<int>(root["tracks"].size());
    if (root.contains("scenes")) numScenes = static_cast<int>(root["scenes"].size());

    if (numTracks == 0) numTracks = kDefaultNumTracks;
    if (numScenes == 0) numScenes = kDefaultNumScenes;

    project.init(numTracks, numScenes);

    // Restore track metadata
    if (root.contains("tracks")) {
        int t = 0;
        for (const auto& tj : root["tracks"]) {
            if (t >= numTracks) break;
            auto& tr = project.track(t);
            tr.name = tj.value("name", "Track " + std::to_string(t + 1));
            std::string typeStr = tj.value("type", "Audio");
            tr.type = (typeStr == "Midi")   ? Track::Type::Midi
                     : (typeStr == "Visual") ? Track::Type::Visual
                                               : Track::Type::Audio;
            tr.colorIndex = tj.value("colorIndex", t);
            tr.volume = tj.value("volume", 1.0f);
            tr.muted = tj.value("muted", false);
            tr.soloed = tj.value("soloed", false);
            tr.midiInputPort = tj.value("midiInputPort", -1);
            tr.midiInputChannel = tj.value("midiInputChannel", -1);
            tr.armed = tj.value("armed", false);
            tr.defaultScene = tj.value("defaultScene", -1);
            tr.sidechainSource = tj.value("sidechainSource", -1);
            tr.resampleSource = tj.value("resampleSource", -1);

            {
                std::string bm = tj.value("visualBlendMode", "Normal");
                tr.visualBlendMode = (bm == "Add")      ? Track::VisualBlendMode::Add
                                    : (bm == "Multiply") ? Track::VisualBlendMode::Multiply
                                    : (bm == "Screen")   ? Track::VisualBlendMode::Screen
                                                           : Track::VisualBlendMode::Normal;
            }

            // Visual effect chain — per track, ordered.
            if (tj.contains("visualEffectChain") &&
                tj["visualEffectChain"].is_array()) {
                for (const auto& pj : tj["visualEffectChain"]) {
                    visual::ShaderPass p;
                    p.shaderPath = pj.value("shaderPath", "");
                    p.bypassed   = pj.value("bypassed", false);
                    if (pj.contains("params") && pj["params"].is_object())
                        for (auto it = pj["params"].begin();
                             it != pj["params"].end(); ++it)
                            p.paramValues.emplace_back(it.key(),
                                                        it.value().get<float>());
                    tr.visualEffectChain.push_back(std::move(p));
                }
            }

            // Macro device — sparse on disk; defaults already in
            // place from the Track ctor, so we only overwrite the
            // fields the JSON explicitly carries.
            if (tj.contains("macros") && tj["macros"].is_object()) {
                const auto& mj = tj["macros"];
                if (mj.contains("values") && mj["values"].is_object()) {
                    for (auto it = mj["values"].begin();
                         it != mj["values"].end(); ++it) {
                        int i = std::stoi(it.key());
                        if (i >= 0 && i < MacroDevice::kNumMacros)
                            tr.macros.values[i] = it.value().get<float>();
                    }
                }
                if (mj.contains("labels") && mj["labels"].is_object()) {
                    for (auto it = mj["labels"].begin();
                         it != mj["labels"].end(); ++it) {
                        int i = std::stoi(it.key());
                        if (i >= 0 && i < MacroDevice::kNumMacros)
                            tr.macros.labels[i] = it.value().get<std::string>();
                    }
                }
                if (mj.contains("lfos") && mj["lfos"].is_object()) {
                    for (auto it = mj["lfos"].begin();
                         it != mj["lfos"].end(); ++it) {
                        int i = std::stoi(it.key());
                        if (i < 0 || i >= MacroDevice::kNumMacros) continue;
                        const auto& lj = it.value();
                        auto& s = tr.macros.lfos[i];
                        s.enabled = true;
                        s.shape   = lj.value("shape", 0);
                        s.rate    = lj.value("rate",  1.0f);
                        s.depth   = lj.value("depth", 0.3f);
                        s.sync    = lj.value("sync",  true);
                    }
                }
                if (mj.contains("mappings") && mj["mappings"].is_array()) {
                    for (const auto& mp : mj["mappings"]) {
                        MacroMapping m;
                        m.macroIdx        = mp.value("macro", 0);
                        m.target.kind     = static_cast<MacroTarget::Kind>(
                                                mp.value("kind", 0));
                        m.target.index    = mp.value("index", 0);
                        m.target.paramName= mp.value("param", std::string{});
                        m.rangeMin        = mp.value("rangeMin", 0.0f);
                        m.rangeMax        = mp.value("rangeMax", 1.0f);
                        tr.macros.mappings.push_back(std::move(m));
                    }
                }
            }

            // Automation mode and lanes
            tr.autoMode = static_cast<automation::AutoMode>(tj.value("autoMode", 0));
            if (tj.contains("automationLanes"))
                tr.automationLanes = automation::lanesFromJson(tj["automationLanes"]);

            // Arrangement clips
            tr.arrangementActive = tj.value("arrangementActive", false);
            if (tj.contains("arrangementClips")) {
                for (auto& acj : tj["arrangementClips"])
                    tr.arrangementClips.push_back(deserializeArrangementClip(acj, folderPath));
                tr.sortArrangementClips();
            }

            // Instrument
            if (tj.contains("instrument")) {
                const auto& instJ = tj["instrument"];
                auto inst = deserializeInstrument(instJ, folderPath, sr, blockSize);
                if (inst) {
                    engine.setInstrument(t, std::move(inst));
                    // Re-apply params: setInstrument calls init() which resets defaults
                    auto* loaded = engine.instrument(t);
                    if (loaded) {
                        loaded->setBypassed(instJ.value("bypassed", false));
                        if (instJ.contains("params"))
                            deserializeParams(*loaded, instJ["params"]);
                    }
                }
            }

            // MIDI effect chain
            if (tj.contains("midiEffects")) {
                deserializeMidiEffectChain(engine.midiEffectChain(t),
                                          tj["midiEffects"], sr);
            }

            ++t;
        }
    }

    // Scenes
    if (root.contains("scenes")) {
        int s = 0;
        for (const auto& sj : root["scenes"]) {
            if (s >= numScenes) break;
            project.scene(s).name = sj.value("name", std::to_string(s + 1));
            ++s;
        }
    }

    // Clips
    if (root.contains("clips")) {
        for (auto& [key, val] : root["clips"].items()) {
            // Parse "track:scene" key
            auto sep = key.find(':');
            if (sep == std::string::npos) continue;
            int trackIdx = std::stoi(key.substr(0, sep));
            int sceneIdx = std::stoi(key.substr(sep + 1));

            std::string clipType = val.value("type", "audio");
            if (clipType == "audio") {
                auto clip = deserializeAudioClip(val, folderPath);
                if (clip)
                    project.setClip(trackIdx, sceneIdx, std::move(clip));
            } else if (clipType == "midi") {
                auto clip = deserializeMidiClip(val);
                if (clip)
                    project.setMidiClip(trackIdx, sceneIdx, std::move(clip));
            } else if (clipType == "visual") {
                project.setVisualClip(trackIdx, sceneIdx,
                                       deserializeVisualClipFields(val));
            }

            // Clip-level automation
            if (val.contains("clipAutomation")) {
                auto* slot = project.getSlot(trackIdx, sceneIdx);
                if (slot)
                    slot->clipAutomation = automation::lanesFromJson(val["clipAutomation"]);
            }
            // Follow action
            if (val.contains("followAction")) {
                auto* slot = project.getSlot(trackIdx, sceneIdx);
                if (slot) {
                    auto& fa = val["followAction"];
                    slot->followAction.enabled  = fa.value("enabled", false);
                    slot->followAction.barCount = fa.value("barCount", 1);
                    slot->followAction.actionA  = static_cast<FollowActionType>(fa.value("actionA", 0));
                    slot->followAction.actionB  = static_cast<FollowActionType>(fa.value("actionB", 0));
                    slot->followAction.chanceA  = fa.value("chanceA", 100);
                }
            }
        }
    }

    // Mixer
    if (root.contains("mixer")) {
        deserializeMixer(engine.mixer(), root["mixer"], sr, blockSize);
    }

    // MIDI Mappings
    if (learnMgr) {
        learnMgr->clearAll();
        if (root.contains("midiMappings")) {
            learnMgr->fromJson(root["midiMappings"]);
        }
    }

    // Visual post-FX chain — rebuild from the saved ordered list.
    // Back-compat: an older save may have stored plain path strings.
    if (visualEngine) {
        visual::postFX_clear(*visualEngine);
        if (root.contains("visualPostFX") && root["visualPostFX"].is_array()) {
            int idx = 0;
            for (const auto& entry : root["visualPostFX"]) {
                if (entry.is_string()) {
                    visual::postFX_add(*visualEngine, entry.get<std::string>());
                    ++idx;
                } else if (entry.is_object() && entry.contains("path")) {
                    if (visual::postFX_add(*visualEngine,
                                             entry["path"].get<std::string>())) {
                        if (entry.contains("params") && entry["params"].is_object()) {
                            std::vector<std::pair<std::string, float>> pv;
                            for (auto it = entry["params"].begin();
                                 it != entry["params"].end(); ++it) {
                                pv.emplace_back(it.key(), it.value().get<float>());
                            }
                            visual::postFX_applyParamValues(*visualEngine, idx, pv);
                        }
                        ++idx;
                    }
                }
            }
        }
    }

    return true;
}

} // namespace yawn
