// ProjectSerializer.cpp — method implementations split from ProjectSerializer.h.

#include "ProjectSerializer.h"

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
        arr.push_back(j);
    }
    return arr;
}

void deserializeEffectChain(effects::EffectChain& chain, const json& arr,
                            double sampleRate, int maxBlockSize) {
    chain.clear();
    for (const auto& j : arr) {
        std::string id = j.value("id", "");
        auto fx = createAudioEffect(id);
        if (!fx) continue;
        fx->init(sampleRate, maxBlockSize);
        fx->setBypassed(j.value("bypassed", false));
        fx->setMix(j.value("mix", 1.0f));
        if (j.contains("params"))
            deserializeParams(*fx, j["params"]);
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
        j["bypassed"] = fx->bypassed();
        j["params"] = serializeParams(*fx);
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
        if (!fx) continue;
        fx->init(sampleRate);
        fx->setBypassed(j.value("bypassed", false));
        if (j.contains("params"))
            deserializeParams(*fx, j["params"]);
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

    // Sampler: save sample to file
    if (std::string(inst.id()) == "sampler") {
        auto& sampler = static_cast<const instruments::Sampler&>(inst);
        if (sampler.hasSample()) {
            std::string filename = "sample_" + std::to_string(sampleCounter++) + ".wav";
            fs::path samplePath = samplesDir / filename;
            FileIO::saveAudioFile(samplePath.string(),
                                  sampler.sampleData(), sampler.sampleFrames(),
                                  sampler.sampleChannels(), 44100);
            j["sampleFile"] = "samples/" + filename;
        }
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
                                  p.sampleChannels, 44100);
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
                                  ds.loopChannels(), 44100);
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
    auto inst = createInstrument(id);
    if (!inst) return nullptr;

    inst->init(sampleRate, maxBlockSize);
    inst->setBypassed(j.value("bypassed", false));
    if (j.contains("params"))
        deserializeParams(*inst, j["params"]);

    // Sampler: load sample from file
    if (id == "sampler" && j.contains("sampleFile")) {
        auto& sampler = static_cast<instruments::Sampler&>(*inst);
        fs::path samplePath = projectDir / j["sampleFile"].get<std::string>();
        auto buf = FileIO::loadAudioFile(samplePath.string());
        if (buf)
            sampler.loadSample(buf->data(), buf->numFrames(), buf->numChannels());
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
            if (buf)
                rack.loadPad(note, buf->data(), buf->numFrames(), buf->numChannels());
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
            if (chainInst)
                rack.addChain(std::move(chainInst), kl, kh, vl, vh);
            else
                continue;
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
        FileIO::saveAudioBuffer(samplePath.string(), *clip.buffer, 44100);
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
        if (buf) clip->buffer = std::move(buf);
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
// ProjectSerializer — top-level save/load
// ---------------------------------------------------------------------------

bool ProjectSerializer::saveToFolder(const fs::path& folderPath,
                                     const Project& project,
                                     const audio::AudioEngine& engine) {
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
    root["project"] = proj;

    // Tracks
    json tracks = json::array();
    for (int t = 0; t < project.numTracks(); ++t) {
        const auto& tr = project.track(t);
        json tj;
        tj["name"] = tr.name;
        tj["type"] = (tr.type == Track::Type::Audio) ? "Audio" : "Midi";
        tj["colorIndex"] = tr.colorIndex;
        tj["volume"] = tr.volume;
        tj["muted"] = tr.muted;
        tj["soloed"] = tr.soloed;
        tj["midiInputPort"] = tr.midiInputPort;
        tj["midiInputChannel"] = tr.midiInputChannel;
        tj["armed"] = tr.armed;

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
            if (slot->audioClip)
                clips[key] = serializeAudioClip(*slot->audioClip, samplesDir, sampleCounter);
            else if (slot->midiClip)
                clips[key] = serializeMidiClip(*slot->midiClip);
        }
    }
    root["clips"] = clips;

    // Mixer
    root["mixer"] = serializeMixer(engine.mixer(), project.numTracks(), sr, blockSize);

    // Write project.json
    std::ofstream out(folderPath / "project.json");
    if (!out.is_open()) return false;
    out << root.dump(2);
    out.close();

    return true;
}

bool ProjectSerializer::loadFromFolder(const fs::path& folderPath,
                                       Project& project,
                                       audio::AudioEngine& engine) {
    fs::path jsonPath = folderPath / "project.json";
    if (!fs::exists(jsonPath)) return false;

    std::ifstream in(jsonPath);
    if (!in.is_open()) return false;

    json root;
    try {
        root = json::parse(in);
    } catch (...) {
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
            tr.type = (typeStr == "Midi") ? Track::Type::Midi : Track::Type::Audio;
            tr.colorIndex = tj.value("colorIndex", t);
            tr.volume = tj.value("volume", 1.0f);
            tr.muted = tj.value("muted", false);
            tr.soloed = tj.value("soloed", false);
            tr.midiInputPort = tj.value("midiInputPort", -1);
            tr.midiInputChannel = tj.value("midiInputChannel", -1);
            tr.armed = tj.value("armed", false);

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
            }
        }
    }

    // Mixer
    if (root.contains("mixer")) {
        deserializeMixer(engine.mixer(), root["mixer"], sr, blockSize);
    }

    return true;
}

} // namespace yawn
