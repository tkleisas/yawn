#pragma once

#include "core/Constants.h"
#include "audio/Transport.h"
#include "audio/ClipEngine.h"
#include "audio/MidiClipEngine.h"
#include "audio/Mixer.h"
#include "audio/Metronome.h"
#include "instruments/Instrument.h"
#include "midi/MidiEffectChain.h"
#include "midi/MidiTypes.h"
#include "midi/MidiClip.h"
#include "midi/MidiEngine.h"
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

    Transport& transport() { return m_transport; }
    const Transport& transport() const { return m_transport; }
    ClipEngine& clipEngine() { return m_clipEngine; }
    MidiClipEngine& midiClipEngine() { return m_midiClipEngine; }
    Mixer& mixer() { return m_mixer; }
    const Mixer& mixer() const { return m_mixer; }
    Metronome& metronome() { return m_metronome; }
    midi::MidiEffectChain& midiEffectChain(int track) { return m_midiEffectChains[track]; }

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

    double sampleRate() const { return m_config.sampleRate; }
    const AudioEngineConfig& config() const { return m_config; }
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    // MidiEngine integration — called from App after init
    void setMidiEngine(midi::MidiEngine* me) { m_midiEngine = me; }

    // Thread-safe transfer of recorded MIDI data to UI
    struct RecordedMidiData {
        std::vector<midi::MidiNote> notes;
        std::vector<midi::MidiCCEvent> ccs;
        int trackIndex = -1;
        int sceneIndex = -1;
        bool overdub = true;
        double lengthBeats = 4.0;
        std::atomic<bool> ready{false};
    };
    RecordedMidiData& recordedMidiData() { return m_recordedMidi; }

    // Thread-safe transfer of recorded audio data to UI
    struct RecordedAudioData {
        std::vector<float> buffer;  // non-interleaved: [ch0_frames...][ch1_frames...]
        int channels = 2;
        int64_t frameCount = 0;
        int trackIndex = -1;
        int sceneIndex = -1;
        std::atomic<bool> ready{false};
    };
    RecordedAudioData& recordedAudioData() { return m_recordedAudio; }

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
    void checkPendingRecordStops();
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
    Mixer m_mixer;
    Metronome m_metronome;
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
    midi::MidiEngine* m_midiEngine = nullptr;

    // MIDI recording state per track
    struct TrackRecordState {
        bool recording = false;
        int targetScene = -1;
        bool overdub = true;
        double recordStartBeat = 0.0;
        QuantizeMode pendingStopQuantize = QuantizeMode::None;

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
            targetScene = -1;
            overdub = true;
            recordStartBeat = 0.0;
            pendingStopQuantize = QuantizeMode::None;
            pendingNotes.clear();
            recordedNotes.clear();
            recordedCCs.clear();
        }
    };

    TrackRecordState m_trackRecordStates[kMaxTracks];
    RecordedMidiData m_recordedMidi;
    RecordedAudioData m_recordedAudio;

    // Audio recording state per track
    struct AudioRecordState {
        bool recording = false;
        int targetScene = -1;
        int64_t recordedFrames = 0;
        int64_t maxFrames = 0;
        std::vector<float> buffer;
        int channels = 2;
        QuantizeMode pendingStopQuantize = QuantizeMode::None;

        void reset() {
            recording = false;
            targetScene = -1;
            recordedFrames = 0;
            pendingStopQuantize = QuantizeMode::None;
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
