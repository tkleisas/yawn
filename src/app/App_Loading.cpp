// App_Loading.cpp — loading audio/MIDI files and buffers into clip
// slots, samplers, drum racks, granular, vocoder; clip drag-drop.
// Split out of App.cpp.
#include "app/App.h"
#include "Version.h"
#include "visual/LiveInputEnum.h"
#include "visual/VisualModBus.h"
#include "transcribe/AudioToMidi.h"
#include "ui/framework/v2/Fw2Painters.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Theme.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "instruments/Multisampler.h"
#include "instruments/InstrumentRack.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Limiter.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Phaser.h"
#include "effects/Wah.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include "effects/Tuner.h"
#include "effects/BeatRepeat.h"
#include "effects/BufferRepeat.h"
#include "effects/Resampler.h"
#include "effects/ClockDrift.h"
#include "effects/CDError.h"
#include "effects/AutoPanner.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "midi/LFO.h"
#include "util/ProjectSerializer.h"
#include "util/Logger.h"
#include "util/FileIO.h"
#include "util/MidiFileIO.h"
#include "audio/OfflineRenderer.h"
#include "presets/PresetGenerator.h"
#include "presets/MidiLoopManager.h"
#include "presets/DrumPatterns.h"
#include "presets/MelodicPatterns.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cinttypes>
#include <cstring>
#include <thread>

namespace yawn {


// Helper: load audio file, resample to engine rate, convert to interleaved
static bool loadInterleaved(audio::AudioEngine& engine, const std::string& path,
                            std::vector<float>& outInterleaved, int& outFrames, int& outChannels) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(engine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, engine.sampleRate());
        if (!buffer) return false;
    }

    outFrames = buffer->numFrames();
    outChannels = buffer->numChannels();
    outInterleaved.resize(static_cast<size_t>(outFrames) * outChannels);
    for (int ch = 0; ch < outChannels; ++ch) {
        const float* src = buffer->channelData(ch);
        for (int i = 0; i < outFrames; ++i)
            outInterleaved[static_cast<size_t>(i) * outChannels + ch] = src[i];
    }
    return true;
}

// Helper: extract filename without extension from a path
static std::string fileNameFromPath(const std::string& path) {
    // Find last separator (handle both / and \ for cross-platform paths).
    auto sep = path.find_last_of("/\\");
    std::string name = (sep == std::string::npos) ? path : path.substr(sep + 1);
    // Strip extension.
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name.erase(dot);
    return name;
}

App::~App() {
    shutdown();
}


bool App::loadMidiLoopIntoSlot(const std::string& path, int track, int scene) {
    if (track < 0 || track >= m_project.numTracks()) return false;
    if (scene < 0 || scene >= m_project.numScenes()) return false;
    if (m_project.track(track).type != Track::Type::Midi) {
        m_toastManager.show("MIDI loops need a MIDI track", 2.5f,
                            ui::ToastManager::Severity::Warn);
        return false;
    }
    auto clip = util::loadMidiFile(path);
    if (!clip) {
        m_toastManager.show("Couldn't load MIDI loop", 2.5f,
                            ui::ToastManager::Severity::Error);
        return false;
    }

    const std::string loopName = clip->name();
    // Snapshots kept as shared_ptr so the undo/redo std::function
    // closures stay copyable.
    std::shared_ptr<midi::MidiClip> newSnap = clip->clone();
    auto* prev = m_project.getMidiClip(track, scene);
    std::shared_ptr<midi::MidiClip> prevSnap =
        prev ? std::shared_ptr<midi::MidiClip>(prev->clone()) : nullptr;

    // setMidiClipLive re-points the audio engine if this slot is playing,
    // so dropping a new loop onto a live slot swaps immediately instead
    // of waiting for a stop/relaunch.
    setMidiClipLive(track, scene, std::move(clip));
    markDirty();
    m_undoManager.push({"Load MIDI Loop",
        [this, track, scene, prevSnap] {
            if (prevSnap) setMidiClipLive(track, scene, prevSnap->clone());
            else if (auto* s = m_project.getSlot(track, scene))
                m_project.graveyardSlotClips(*s);
            markDirty();
        },
        [this, track, scene, newSnap] {
            setMidiClipLive(track, scene, newSnap->clone());
            markDirty();
        }, ""});
    m_toastManager.show("Loaded " + loopName, 2.0f,
                        ui::ToastManager::Severity::Info);
    return true;
}

void App::performClipDragDrop(int srcT, int srcS, int dstT, int dstS, bool isCopy) {
    auto* srcSlot = m_project.getSlot(srcT, srcS);
    auto* dstSlot = m_project.getSlot(dstT, dstS);
    if (!srcSlot || !dstSlot || srcSlot->empty()) return;

    // Capture state for undo
    std::unique_ptr<audio::Clip> oldDstAudio;
    std::unique_ptr<midi::MidiClip> oldDstMidi;
    FollowAction oldDstFA = dstSlot->followAction;
    auto oldDstAuto = dstSlot->clipAutomation;
    auto oldDstQuant = dstSlot->launchQuantize;
    if (dstSlot->audioClip) oldDstAudio = dstSlot->audioClip->clone();
    if (dstSlot->midiClip) oldDstMidi = dstSlot->midiClip->clone();

    std::unique_ptr<audio::Clip> oldSrcAudio;
    std::unique_ptr<midi::MidiClip> oldSrcMidi;
    FollowAction oldSrcFA = srcSlot->followAction;
    auto oldSrcAuto = srcSlot->clipAutomation;
    auto oldSrcQuant = srcSlot->launchQuantize;
    if (srcSlot->audioClip) oldSrcAudio = srcSlot->audioClip->clone();
    if (srcSlot->midiClip) oldSrcMidi = srcSlot->midiClip->clone();

    // Stop playback on source track if moving (not copying)
    if (!isCopy) {
        if (srcSlot->audioClip)
            m_audioEngine.sendCommand(audio::StopClipMsg{srcT});
        else if (srcSlot->midiClip)
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{srcT});
    }

    // Stop playback on destination track if occupied
    if (!dstSlot->empty()) {
        if (dstSlot->audioClip)
            m_audioEngine.sendCommand(audio::StopClipMsg{dstT});
        else if (dstSlot->midiClip)
            m_audioEngine.sendCommand(audio::StopMidiClipMsg{dstT});
    }

    // Perform the operation
    if (isCopy)
        m_project.copySlot(srcT, srcS, dstT, dstS);
    else
        m_project.moveSlot(srcT, srcS, dstT, dstS);

    markDirty();

    std::string action = isCopy ? "Copy Clip" : "Move Clip";
    m_undoManager.push({action,
        [this, srcT, srcS, dstT, dstS,
         srcAudio = std::shared_ptr<audio::Clip>(std::move(oldSrcAudio)),
         srcMidi = std::shared_ptr<midi::MidiClip>(std::move(oldSrcMidi)),
         srcFA = oldSrcFA, srcAuto = oldSrcAuto, srcQuant = oldSrcQuant,
         dstAudio = std::shared_ptr<audio::Clip>(std::move(oldDstAudio)),
         dstMidi = std::shared_ptr<midi::MidiClip>(std::move(oldDstMidi)),
         dstFA = oldDstFA, dstAuto = oldDstAuto, dstQuant = oldDstQuant]() {
            // Undo: restore both slots to original state
            auto* src = m_project.getSlot(srcT, srcS);
            auto* dst = m_project.getSlot(dstT, dstS);
            if (!src || !dst) return;
            src->audioClip = srcAudio ? srcAudio->clone() : nullptr;
            src->midiClip  = srcMidi  ? srcMidi->clone()  : nullptr;
            src->followAction = srcFA;
            src->clipAutomation = srcAuto;
            src->launchQuantize = srcQuant;
            dst->audioClip = dstAudio ? dstAudio->clone() : nullptr;
            dst->midiClip  = dstMidi  ? dstMidi->clone()  : nullptr;
            dst->followAction = dstFA;
            dst->clipAutomation = dstAuto;
            dst->launchQuantize = dstQuant;
            markDirty();
        },
        [this, srcT, srcS, dstT, dstS, isCopy]() {
            // Redo
            if (isCopy) m_project.copySlot(srcT, srcS, dstT, dstS);
            else        m_project.moveSlot(srcT, srcS, dstT, dstS);
            markDirty();
        },
        ""
    });
}


bool App::loadClipToSlot(const std::string& path, int trackIndex, int sceneIndex) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        LOG_INFO("Audio", "Resampling from %d Hz to %.0f Hz...",
            info.sampleRate, m_audioEngine.sampleRate());
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    std::string name = fileNameFromPath(path);

    auto clip = std::make_unique<audio::Clip>();
    clip->name = name;
    clip->buffer = buffer;
    clip->looping = true;
    clip->gain = 0.8f;

    m_project.setClip(trackIndex, sceneIndex, std::move(clip));

    LOG_INFO("File", "Loaded '%s' -> Track %d, Scene %d",
        name.c_str(), trackIndex + 1, sceneIndex + 1);

    markDirty();
    return true;
}

bool App::loadClipToArrangement(const std::string& path, int trackIndex, double beatPos) {
    util::AudioFileInfo info;
    auto buffer = util::loadAudioFile(path, &info);
    if (!buffer) return false;

    if (info.sampleRate != static_cast<int>(m_audioEngine.sampleRate())) {
        buffer = util::resampleBuffer(*buffer, info.sampleRate, m_audioEngine.sampleRate());
        if (!buffer) return false;
    }

    std::string name = fileNameFromPath(path);

    // Calculate length in beats from audio duration
    double sampleRate = m_audioEngine.sampleRate();
    double bpm = m_audioEngine.transport().bpm();
    double durationSec = static_cast<double>(buffer->numFrames()) / sampleRate;
    double durationBeats = durationSec * bpm / 60.0;

    ArrangementClip clip;
    clip.type = ArrangementClip::Type::Audio;
    clip.startBeat = beatPos;
    clip.lengthBeats = durationBeats;
    clip.audioBuffer = buffer;
    clip.name = name;
    clip.colorIndex = m_project.track(trackIndex).colorIndex;

    m_project.track(trackIndex).arrangementClips.push_back(std::move(clip));
    m_project.track(trackIndex).sortArrangementClips();
    m_project.updateArrangementLength();
    syncArrangementClipsToEngine(trackIndex);

    // Activate arrangement playback for this track
    if (!m_project.track(trackIndex).arrangementActive) {
        m_project.track(trackIndex).arrangementActive = true;
        m_audioEngine.sendCommand(audio::SetTrackArrActiveMsg{trackIndex, true});
    }

    LOG_INFO("File", "Loaded '%s' -> Arrangement Track %d at beat %.1f",
        name.c_str(), trackIndex + 1, beatPos);

    markDirty();
    return true;
}

bool App::loadSampleToSampler(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* sampler = dynamic_cast<instruments::Sampler*>(inst);
    if (!sampler) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    sampler->loadSample(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded sample '%s' into Sampler on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Sample (Sampler)",
            [this, ti]{ // undo: clear sample
                auto* s = dynamic_cast<instruments::Sampler*>(m_audioEngine.instrument(ti));
                if (s) s->clearSample();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadSampleToSampler(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadLoopToDrumSlop(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* ds = dynamic_cast<instruments::DrumSlop*>(inst);
    if (!ds) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    ds->loadLoop(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded loop '%s' into DrumSlop on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Loop (DrumSlop)",
            [this, ti]{ // undo: clear loop
                auto* d = dynamic_cast<instruments::DrumSlop*>(m_audioEngine.instrument(ti));
                if (d) d->clearLoop();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadLoopToDrumSlop(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadSampleToDrumRack(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* rack = dynamic_cast<instruments::DrumRack*>(inst);
    if (!rack) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    rack->loadPad(rack->selectedPad(), interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    int padIdx = rack->selectedPad();
    LOG_INFO("File", "Loaded sample '%s' into Drum Rack pad %d on Track %d",
        name.c_str(), padIdx, trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Sample (DrumRack)",
            [this, ti, padIdx]{ // undo: clear pad
                auto* r = dynamic_cast<instruments::DrumRack*>(m_audioEngine.instrument(ti));
                if (r) r->clearPad(padIdx);
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadSampleToDrumRack(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadSampleToGranular(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* granular = dynamic_cast<instruments::GranularSynth*>(inst);
    if (!granular) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    granular->loadSample(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded sample '%s' into Granular Synth on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Sample (Granular)",
            [this, ti]{ // undo: clear sample
                auto* g = dynamic_cast<instruments::GranularSynth*>(m_audioEngine.instrument(ti));
                if (g) g->clearSample();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadSampleToGranular(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadModulatorToVocoder(const std::string& path, int trackIndex) {
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* vocoder = dynamic_cast<instruments::Vocoder*>(inst);
    if (!vocoder) return false;

    std::vector<float> interleaved;
    int frames, channels;
    if (!loadInterleaved(m_audioEngine, path, interleaved, frames, channels))
        return false;

    vocoder->loadModulatorSample(interleaved.data(), frames, channels);

    std::string name = fileNameFromPath(path);
    LOG_INFO("File", "Loaded modulator '%s' into Vocoder on Track %d",
        name.c_str(), trackIndex + 1);

    {
        std::string undoPath = path;
        int ti = trackIndex;
        m_undoManager.push({"Load Modulator (Vocoder)",
            [this, ti]{ // undo: clear modulator
                auto* v = dynamic_cast<instruments::Vocoder*>(m_audioEngine.instrument(ti));
                if (v) v->clearModulatorSample();
                updateDetailForSelectedTrack();
            },
            [this, undoPath, ti]{ // redo: reload from file
                loadModulatorToVocoder(undoPath, ti);
            },
            ""});
    }

    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

// ── Buffer-based load helpers ──────────────────────────────────────
// Used by in-app drag-and-drop. Path-based loaders above own the
// "load file → resample → install" chain; these skip straight to
// install since the buffer is already at engine rate.

namespace {
void interleaveBuffer(const audio::AudioBuffer& src,
                      std::vector<float>& outIl,
                      int& outFrames, int& outChannels) {
    outFrames   = src.numFrames();
    outChannels = src.numChannels();
    outIl.resize(static_cast<size_t>(outFrames) * outChannels);
    for (int ch = 0; ch < outChannels; ++ch) {
        const float* csrc = src.channelData(ch);
        for (int i = 0; i < outFrames; ++i)
            outIl[static_cast<size_t>(i) * outChannels + ch] = csrc[i];
    }
}
} // anon

bool App::loadBufferToSampler(std::shared_ptr<audio::AudioBuffer> buf,
                               const std::string& name, int trackIndex) {
    if (!buf) return false;
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* s    = dynamic_cast<instruments::Sampler*>(inst);
    if (!s) return false;
    std::vector<float> il; int frames, chans;
    interleaveBuffer(*buf, il, frames, chans);
    s->loadSample(il.data(), frames, chans);
    LOG_INFO("Drop", "Dropped buffer '%s' into Sampler on Track %d",
             name.c_str(), trackIndex + 1);
    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadBufferToDrumSlop(std::shared_ptr<audio::AudioBuffer> buf,
                                const std::string& name, int trackIndex) {
    if (!buf) return false;
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* d    = dynamic_cast<instruments::DrumSlop*>(inst);
    if (!d) return false;
    std::vector<float> il; int frames, chans;
    interleaveBuffer(*buf, il, frames, chans);
    d->loadLoop(il.data(), frames, chans);
    LOG_INFO("Drop", "Dropped buffer '%s' into DrumSlop on Track %d",
             name.c_str(), trackIndex + 1);
    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadBufferToGranular(std::shared_ptr<audio::AudioBuffer> buf,
                                const std::string& name, int trackIndex) {
    if (!buf) return false;
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* g    = dynamic_cast<instruments::GranularSynth*>(inst);
    if (!g) return false;
    std::vector<float> il; int frames, chans;
    interleaveBuffer(*buf, il, frames, chans);
    g->loadSample(il.data(), frames, chans);
    LOG_INFO("Drop", "Dropped buffer '%s' into Granular on Track %d",
             name.c_str(), trackIndex + 1);
    updateDetailForSelectedTrack();
    markDirty();
    return true;
}

bool App::loadBufferToVocoder(std::shared_ptr<audio::AudioBuffer> buf,
                               const std::string& name, int trackIndex) {
    if (!buf) return false;
    auto* inst = m_audioEngine.instrument(trackIndex);
    auto* v    = dynamic_cast<instruments::Vocoder*>(inst);
    if (!v) return false;
    std::vector<float> il; int frames, chans;
    interleaveBuffer(*buf, il, frames, chans);
    v->loadModulatorSample(il.data(), frames, chans);
    LOG_INFO("Drop", "Dropped buffer '%s' into Vocoder on Track %d",
             name.c_str(), trackIndex + 1);
    updateDetailForSelectedTrack();
    markDirty();
    return true;
}


} // namespace yawn
