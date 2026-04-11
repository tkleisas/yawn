#pragma once

#include "core/Constants.h"
#include "audio/Transport.h"
#include "audio/ClipEngine.h"
#include "audio/MidiClipEngine.h"
#include "audio/ArrangementPlayback.h"
#include "audio/Mixer.h"
#include "audio/Metronome.h"
#include "instruments/Instrument.h"
#include "midi/MidiEffectChain.h"
#include "midi/MidiTypes.h"
#include "midi/MidiClip.h"
#include "midi/MidiEngine.h"
#include "automation/AutomationEngine.h"
#include "util/MessageQueue.h"
#include <portaudio.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace audio {

struct AudioDevice {
    int id = -1;
    std::string name;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
    double defaultSampleRate = 44100.0;
};

struct AudioEngineConfig {
    double sampleRate = 44100.0;
    int framesPerBuffer = 256;
    int outputChannels = 2;
    int inputChannels = 2;
    int inputDevice = -1;
    int outputDevice = -1;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init(const AudioEngineConfig& config = {});
    bool start();
    void stop();
    void shutdown();

    static std::vector<AudioDevice> enumerateDevices();
    static int defaultOutputDevice();
    static int defaultInputDevice();

    void sendCommand(const AudioCommand& cmd);
    bool pollEvent(AudioEvent& event);

    // CPU load as reported by PortAudio (0.0 – 1.0)
    double cpuLoad() const { return m_stream ? Pa_GetStreamCpuLoad(m_stream) : 0.0; }

    Transport& transport() { return m_transport; }
    const Transport& transport() const { return m_transport; }
    ClipEngine& clipEngine() { return m_clipEngine; }
    MidiClipEngine& midiClipEngine() { return m_midiClipEngine; }
    ArrangementPlayback& arrangementPlayback() { return m_arrPlayback; }
    Mixer& mixer() { return m_mixer; }
    const Mixer& mixer() const { return m_mixer; }
    Metronome& metronome() { return m_metronome; }
    midi::MidiEffectChain& midiEffectChain(int track) { return m_midiEffectChains[track]; }
    automation::AutomationEngine& automationEngine() { return m_automationEngine; }
    std::vector<automation::AutomationLane>& trackAutoLanes(int track) { return m_trackAutoLanes[track]; }
    const std::vector<automation::AutomationLane>& trackAutoLanes(int track) const { return m_trackAutoLanes[track]; }

    // Instrument management (call before start or with appropriate sync)
    void setInstrument(int track, std::unique_ptr<instruments::Instrument> inst) {
        if (track < 0 || track >= kMaxTracks) return;
        m_instruments[track] = std::move(inst);
        if (m_instruments[track])
            m_instruments[track]->init(m_config.sampleRate, m_config.framesPerBuffer);
    }
    instruments::Instrument* instrument(int track) {
        return (track >= 0 && track < kMaxTracks) ? m_instruments[track].get() : nullptr;
    }

    // Remove a track slot, shifting all higher tracks down by one.
    // Must be called with transport stopped and no clips playing.
    void removeTrackSlot(int index, int numActive) {
        int last = numActive - 1;
        if (index < 0 || index > last || last >= kMaxTracks) return;

        // Sub-engines
        m_clipEngine.removeTrackSlot(index, last);
        m_midiClipEngine.removeTrackSlot(index, last);
        m_arrPlayback.removeTrackSlot(index, last);
        m_mixer.removeTrackSlot(index, last);
        m_automationEngine.removeTrackSlot(index, last);

        // AudioEngine's own per-track arrays
        for (int i = index; i < last; ++i) {
            m_instruments[i]      = std::move(m_instruments[i + 1]);
            m_midiEffectChains[i] = std::move(m_midiEffectChains[i + 1]);
            m_trackAutoLanes[i]   = std::move(m_trackAutoLanes[i + 1]);
            m_trackArmed[i]       = m_trackArmed[i + 1];
            m_trackMonitorMode[i] = m_trackMonitorMode[i + 1];
            m_trackType[i]        = m_trackType[i + 1];
            m_trackAudioInputCh[i]= m_trackAudioInputCh[i + 1];
            m_trackMono[i]        = m_trackMono[i + 1];
            m_trackMidiOutPort[i] = m_trackMidiOutPort[i + 1];
            m_trackMidiOutCh[i]   = m_trackMidiOutCh[i + 1];
            m_trackSidechainSource[i] = m_trackSidechainSource[i + 1];
            m_trackResampleSource[i]  = m_trackResampleSource[i + 1];
            m_trackRecordStates[i]= std::move(m_trackRecordStates[i + 1]);
            m_audioRecordStates[i]= std::move(m_audioRecordStates[i + 1]);
            // RecordedMidiData/RecordedAudioData contain atomics — transfer manually
            m_recordedMidi[i].notes       = std::move(m_recordedMidi[i + 1].notes);
            m_recordedMidi[i].ccs         = std::move(m_recordedMidi[i + 1].ccs);
            m_recordedMidi[i].trackIndex  = m_recordedMidi[i + 1].trackIndex;
            m_recordedMidi[i].sceneIndex  = m_recordedMidi[i + 1].sceneIndex;
            m_recordedMidi[i].overdub     = m_recordedMidi[i + 1].overdub;
            m_recordedMidi[i].lengthBeats = m_recordedMidi[i + 1].lengthBeats;
            m_recordedMidi[i].ready.store(m_recordedMidi[i + 1].ready.load());
            m_recordedAudio[i].buffer     = std::move(m_recordedAudio[i + 1].buffer);
            m_recordedAudio[i].channels   = m_recordedAudio[i + 1].channels;
            m_recordedAudio[i].frameCount = m_recordedAudio[i + 1].frameCount;
            m_recordedAudio[i].trackIndex = m_recordedAudio[i + 1].trackIndex;
            m_recordedAudio[i].sceneIndex = m_recordedAudio[i + 1].sceneIndex;
            m_recordedAudio[i].overdub    = m_recordedAudio[i + 1].overdub;
            m_recordedAudio[i].ready.store(m_recordedAudio[i + 1].ready.load());
        }
        // Clear the freed last slot
        m_instruments[last].reset();
        m_midiEffectChains[last].clear();
        m_trackAutoLanes[last].clear();
        m_trackArmed[last] = false;
        m_trackMonitorMode[last] = 0;
        m_trackType[last] = 0;
        m_trackAudioInputCh[last] = 0;
        m_trackMono[last] = false;
        m_trackMidiOutPort[last] = 0;
        m_trackMidiOutCh[last] = 0;
        m_trackSidechainSource[last] = -1;
        m_trackResampleSource[last] = -1;

        // Fix sidechain/resample references to removed or shifted tracks
        for (int i = 0; i <= last; ++i) {
            if (m_trackSidechainSource[i] == index) m_trackSidechainSource[i] = -1;
            else if (m_trackSidechainSource[i] > index) m_trackSidechainSource[i]--;
            if (m_trackResampleSource[i] == index) m_trackResampleSource[i] = -1;
            else if (m_trackResampleSource[i] > index) m_trackResampleSource[i]--;
        }
        m_trackRecordStates[last].reset();
        m_audioRecordStates[last].reset();
        m_recordedMidi[last].notes.clear();
        m_recordedMidi[last].ccs.clear();
        m_recordedMidi[last].trackIndex = -1;
        m_recordedMidi[last].sceneIndex = -1;
        m_recordedMidi[last].autoStopped = false;
        m_recordedMidi[last].ready.store(false);
        m_recordedAudio[last].buffer.clear();
        m_recordedAudio[last].frameCount = 0;
        m_recordedAudio[last].trackIndex = -1;
        m_recordedAudio[last].sceneIndex = -1;
        m_recordedAudio[last].autoStopped = false;
        m_recordedAudio[last].ready.store(false);

        // Vectors (heap): erase and push default to keep size = kMaxTracks
        if (index < static_cast<int>(m_trackMidiBuffers.size())) {
            m_trackMidiBuffers.erase(m_trackMidiBuffers.begin() + index);
            m_trackMidiBuffers.emplace_back();
        }
        if (index < static_cast<int>(m_liveInputMidi.size())) {
            m_liveInputMidi.erase(m_liveInputMidi.begin() + index);
            m_liveInputMidi.emplace_back();
        }
        // m_trackBufferPtrs are scratch buffers — no shift needed
    }

    double sampleRate() const { return m_config.sampleRate; }
    const AudioEngineConfig& config() const { return m_config; }
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    // Render a single buffer offline (for export). Must be called with stream stopped.
    void renderBuffer(float* output, unsigned long numFrames) {
        processAudio(nullptr, output, numFrames);
    }

    // MidiEngine integration — called from App after init
    void setMidiEngine(midi::MidiEngine* me) { m_midiEngine = me; }

    // Thread-safe transfer of recorded MIDI data to UI (per-track)
    struct RecordedMidiData {
        std::vector<midi::MidiNote> notes;
        std::vector<midi::MidiCCEvent> ccs;
        int trackIndex = -1;
        int sceneIndex = -1;
        bool overdub = true;
        double lengthBeats = 4.0;
        bool autoStopped = false;   // true when fixed-duration auto-stop triggered
        std::atomic<bool> ready{false};
    };
    RecordedMidiData& recordedMidiData(int track) { return m_recordedMidi[track]; }

    // Thread-safe transfer of recorded audio data to UI (per-track)
    struct RecordedAudioData {
        std::vector<float> buffer;  // non-interleaved: [ch0_frames...][ch1_frames...]
        int channels = 2;
        int64_t frameCount = 0;
        int trackIndex = -1;
        int sceneIndex = -1;
        bool overdub = false;
        bool autoStopped = false;   // true when fixed-duration auto-stop triggered
        std::atomic<bool> ready{false};
    };
    RecordedAudioData& recordedAudioData(int track) { return m_recordedAudio[track]; }

private:
    static int paCallback(
        const void* inputBuffer,
        void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData
    );

    void processAudio(const float* input, float* output, unsigned long numFrames);
    void processCommands();
    void emitPositionUpdate();
    void emitClipStates();
    void emitMeterUpdates();
    void maybeStopTransportRecording();
    void checkPendingRecordStops(int bufferSize);
    void finalizeMidiRecord(int trackIndex);
    void finalizeAudioRecord(int trackIndex);

    struct TestTone {
        bool enabled = false;
        float frequency = 440.0f;
        double phase = 0.0;
    };

    AudioEngineConfig m_config;
    Transport m_transport;
    ClipEngine m_clipEngine;
    MidiClipEngine m_midiClipEngine;
    ArrangementPlayback m_arrPlayback;
    Mixer m_mixer;
    Metronome m_metronome;
    automation::AutomationEngine m_automationEngine;
    // Per-track automation lanes for recording (written by automation engine)
    std::vector<automation::AutomationLane> m_trackAutoLanes[kMaxTracks];
    midi::MidiEffectChain m_midiEffectChains[kMaxMidiTracks];
    std::unique_ptr<instruments::Instrument> m_instruments[kMaxTracks];
    // Heap-allocated to avoid stack overflow (~1MB for 64 MidiBuffers)
    std::vector<midi::MidiBuffer> m_trackMidiBuffers;
    // Captures virtual keyboard / UI MIDI input for recording
    std::vector<midi::MidiBuffer> m_liveInputMidi;

    CommandQueue m_commandQueue;
    EventQueue m_eventQueue;

    PaStream* m_stream = nullptr;
    std::atomic<bool> m_running{false};
    bool m_paInitialized = false;

    TestTone m_testTone;
    int m_posUpdateCounter = 0;
    bool m_trackArmed[kMaxTracks] = {};
    uint8_t m_trackMonitorMode[kMaxTracks] = {}; // 0=Auto, 1=In, 2=Off
    uint8_t m_trackType[kMaxTracks] = {};         // 0=Audio, 1=Midi
    int m_trackAudioInputCh[kMaxTracks] = {};     // 0=none, 1=In1, 2=In2, 3=In1+2...
    bool m_trackMono[kMaxTracks] = {};
    int m_trackMidiOutPort[kMaxTracks] = {};       // -1=none
    int m_trackMidiOutCh[kMaxTracks] = {};         // -1=all, 0-15
    int m_trackSidechainSource[kMaxTracks] = {};   // -1=none, 0..kMaxTracks-1 = source track
    int m_trackResampleSource[kMaxTracks] = {};    // -1=none, 0..kMaxTracks-1 = source track
    midi::MidiEngine* m_midiEngine = nullptr;

    // MIDI recording state per track
    struct TrackRecordState {
        bool recording = false;
        bool pendingStart = false;  // waiting for count-in to finish
        bool usedCountIn = false;   // true if count-in was applied
        int targetScene = -1;
        bool overdub = true;
        double recordStartBeat = 0.0;
        QuantizeMode pendingStopQuantize = QuantizeMode::None;
        int targetLengthBars = 0;  // 0 = unlimited

        struct PendingNote {
            uint8_t pitch = 0;
            uint8_t channel = 0;
            uint16_t velocity = 0;
            double startBeat = 0.0;
        };
        std::vector<PendingNote> pendingNotes;
        std::vector<midi::MidiNote> recordedNotes;
        std::vector<midi::MidiCCEvent> recordedCCs;

        void reset() {
            recording = false;
            pendingStart = false;
            usedCountIn = false;
            targetScene = -1;
            overdub = true;
            recordStartBeat = 0.0;
            pendingStopQuantize = QuantizeMode::None;
            targetLengthBars = 0;
            pendingNotes.clear();
            recordedNotes.clear();
            recordedCCs.clear();
        }
    };

    TrackRecordState m_trackRecordStates[kMaxTracks];
    RecordedMidiData m_recordedMidi[kMaxTracks];
    RecordedAudioData m_recordedAudio[kMaxTracks];

    // Audio recording state per track
    struct AudioRecordState {
        bool recording = false;
        bool pendingStart = false;  // waiting for count-in to finish
        bool usedCountIn = false;   // true if count-in was applied
        int targetScene = -1;
        int64_t recordedFrames = 0;
        int64_t maxFrames = 0;
        std::vector<float> buffer;
        int channels = 2;
        bool overdub = false;
        QuantizeMode pendingStopQuantize = QuantizeMode::None;
        int targetLengthBars = 0;  // 0 = unlimited
        double recordStartBeat = 0.0;

        void reset() {
            recording = false;
            pendingStart = false;
            usedCountIn = false;
            targetScene = -1;
            recordedFrames = 0;
            overdub = false;
            pendingStopQuantize = QuantizeMode::None;
            targetLengthBars = 0;
            recordStartBeat = 0.0;
        }
    };
    AudioRecordState m_audioRecordStates[kMaxTracks];

    // Per-track input buffer (interleaved from PortAudio, deinterleaved during processing)
    std::vector<float> m_inputBufferHeap;   // inputChannels * kMaxFramesPerBuffer
    bool m_hasInputDevice = false;

    // Per-track scratch buffers for mixer routing (heap-allocated, preallocated in init)
    std::vector<float> m_trackBufferHeap;     // kMaxTracks * kMaxFramesPerBuffer * 2
    float* m_trackBufferPtrs[kMaxTracks] = {};
};

} // namespace audio
} // namespace yawn
