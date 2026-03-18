#include "audio/AudioEngine.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(const AudioEngineConfig& config) {
    m_config = config;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
        return false;
    }
    m_paInitialized = true;

    m_transport.setSampleRate(config.sampleRate);
    m_transport.reset();

    m_clipEngine.setTransport(&m_transport);
    m_clipEngine.setSampleRate(config.sampleRate);

    m_metronome.init(config.sampleRate, config.framesPerBuffer);

    for (int t = 0; t < kMaxMidiTracks; ++t)
        m_midiEffectChains[t].init(config.sampleRate);

    // Allocate per-track scratch buffers on heap
    const int bufferStride = kMaxFramesPerBuffer * config.outputChannels;
    m_trackBufferHeap.resize(kMaxTracks * bufferStride, 0.0f);
    for (int t = 0; t < kMaxTracks; ++t) {
        m_trackBufferPtrs[t] = m_trackBufferHeap.data() + t * bufferStride;
    }

    // Open default output stream
    PaStreamParameters outputParams;
    outputParams.device = Pa_GetDefaultOutputDevice();
    if (outputParams.device == paNoDevice) {
        std::fprintf(stderr, "No default audio output device found\n");
        return false;
    }

    outputParams.channelCount = config.outputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency =
        Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(
        &m_stream,
        nullptr,         // no input
        &outputParams,
        config.sampleRate,
        config.framesPerBuffer,
        paClipOff,
        &AudioEngine::paCallback,
        this
    );

    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio open stream failed: %s\n", Pa_GetErrorText(err));
        return false;
    }

    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(outputParams.device);
    std::printf("Audio device: %s\n", devInfo->name);
    std::printf("Sample rate: %.0f Hz, Buffer: %d frames, Channels: %d\n",
        config.sampleRate, config.framesPerBuffer, config.outputChannels);

    return true;
}

bool AudioEngine::start() {
    if (!m_stream) return false;

    PaError err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio start stream failed: %s\n", Pa_GetErrorText(err));
        return false;
    }

    m_running.store(true, std::memory_order_release);
    std::printf("Audio engine started\n");
    return true;
}

void AudioEngine::stop() {
    if (m_stream && m_running.load(std::memory_order_acquire)) {
        Pa_StopStream(m_stream);
        m_running.store(false, std::memory_order_release);
        std::printf("Audio engine stopped\n");
    }
}

void AudioEngine::shutdown() {
    stop();

    if (m_stream) {
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    if (m_paInitialized) {
        Pa_Terminate();
        m_paInitialized = false;
    }
}

void AudioEngine::sendCommand(const AudioCommand& cmd) {
    m_commandQueue.push(cmd);
}

bool AudioEngine::pollEvent(AudioEvent& event) {
    return m_eventQueue.pop(event);
}

// --- PortAudio callback (audio thread) ---

int AudioEngine::paCallback(
    const void* /*inputBuffer*/,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* /*timeInfo*/,
    PaStreamCallbackFlags /*statusFlags*/,
    void* userData)
{
    auto* engine = static_cast<AudioEngine*>(userData);
    auto* output = static_cast<float*>(outputBuffer);

    engine->processAudio(output, framesPerBuffer);
    return paContinue;
}

void AudioEngine::processAudio(float* output, unsigned long numFrames) {
    // Process any pending commands from the UI thread
    processCommands();

    int nc = m_config.outputChannels;
    int nf = static_cast<int>(numFrames);

    // Clear per-track buffers (pointers already set up in init)
    for (int t = 0; t < kMaxTracks; ++t) {
        std::memset(m_trackBufferPtrs[t], 0, nf * nc * sizeof(float));
    }

    // Render each track's clip into its own buffer
    m_clipEngine.checkAndFirePending();
    for (int t = 0; t < kMaxTracks; ++t) {
        m_clipEngine.processTrackToBuffer(t, m_trackBufferPtrs[t], nf, nc);
    }

    // Process instruments: MIDI effects → instrument → add to track buffer
    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_instruments[t]) {
            m_trackMidiBuffers[t].clear();
            continue;
        }
        // Run MIDI effect chain on this track's MIDI buffer
        if (m_midiEffectChains[t].count() > 0) {
            midi::TransportInfo ti;
            ti.bpm = m_transport.bpm();
            ti.positionInBeats = m_transport.positionInBeats();
            ti.positionInSamples = m_transport.positionInSamples();
            ti.sampleRate = m_config.sampleRate;
            ti.playing = m_transport.isPlaying();
            m_midiEffectChains[t].process(m_trackMidiBuffers[t], nf, ti);
        }
        // Render instrument into track buffer (adds to existing audio)
        m_instruments[t]->process(m_trackBufferPtrs[t], nf, nc,
                                  m_trackMidiBuffers[t]);
        m_trackMidiBuffers[t].clear();
    }

    // Run mixer: per-track fader/pan/mute/solo, sends→returns, master
    // Mixer clears output before writing
    m_mixer.process(m_trackBufferPtrs, kMaxTracks, output, nf, nc);

    // Metronome: add click directly to master output (bypasses mixer routing)
    m_metronome.process(output, nf, nc,
                        m_transport.bpm(),
                        m_transport.positionInSamples(),
                        m_transport.isPlaying());

    // Add test tone on top of mixer output (bypasses mixer routing)
    if (m_testTone.enabled) {
        double phaseInc = 2.0 * M_PI * m_testTone.frequency / m_config.sampleRate;
        for (int i = 0; i < nf; ++i) {
            float sample = static_cast<float>(std::sin(m_testTone.phase)) * 0.3f;
            for (int ch = 0; ch < nc; ++ch) {
                output[i * nc + ch] += sample;
            }
            m_testTone.phase += phaseInc;
            if (m_testTone.phase >= 2.0 * M_PI) {
                m_testTone.phase -= 2.0 * M_PI;
            }
        }
    }

    // Advance transport
    m_transport.advance(nf);

    // Periodically send position updates to the UI (~30 Hz)
    m_posUpdateCounter += nf;
    int updateInterval = static_cast<int>(m_config.sampleRate / 30.0);
    if (m_posUpdateCounter >= updateInterval) {
        m_posUpdateCounter -= updateInterval;
        emitPositionUpdate();
        emitClipStates();
        emitMeterUpdates();
    }
}

void AudioEngine::processCommands() {
    AudioCommand cmd;
    while (m_commandQueue.pop(cmd)) {
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;

            if constexpr (std::is_same_v<T, TransportPlayMsg>) {
                m_transport.play();
            }
            else if constexpr (std::is_same_v<T, TransportStopMsg>) {
                m_transport.stop();
            }
            else if constexpr (std::is_same_v<T, TransportSetBPMMsg>) {
                m_transport.setBPM(msg.bpm);
            }
            else if constexpr (std::is_same_v<T, TransportSetPositionMsg>) {
                m_transport.setPositionInSamples(msg.positionInSamples);
            }
            else if constexpr (std::is_same_v<T, TestToneMsg>) {
                m_testTone.enabled = msg.enabled;
                m_testTone.frequency = msg.frequency;
                m_testTone.phase = 0.0;
            }
            else if constexpr (std::is_same_v<T, LaunchClipMsg>) {
                m_clipEngine.scheduleClip(msg.trackIndex, msg.clip);
            }
            else if constexpr (std::is_same_v<T, StopClipMsg>) {
                m_clipEngine.scheduleStop(msg.trackIndex);
            }
            else if constexpr (std::is_same_v<T, SetQuantizeMsg>) {
                m_clipEngine.setQuantizeMode(msg.mode);
            }
            else if constexpr (std::is_same_v<T, SetTrackVolumeMsg>) {
                m_mixer.setTrackVolume(msg.trackIndex, msg.volume);
            }
            else if constexpr (std::is_same_v<T, SetTrackPanMsg>) {
                m_mixer.setTrackPan(msg.trackIndex, msg.pan);
            }
            else if constexpr (std::is_same_v<T, SetTrackMuteMsg>) {
                m_mixer.setTrackMute(msg.trackIndex, msg.muted);
            }
            else if constexpr (std::is_same_v<T, SetTrackSoloMsg>) {
                m_mixer.setTrackSolo(msg.trackIndex, msg.soloed);
            }
            else if constexpr (std::is_same_v<T, SetMasterVolumeMsg>) {
                m_mixer.setMasterVolume(msg.volume);
            }
            else if constexpr (std::is_same_v<T, SetSendLevelMsg>) {
                m_mixer.setSendLevel(msg.trackIndex, msg.sendIndex, msg.level);
            }
            else if constexpr (std::is_same_v<T, SetSendModeMsg>) {
                m_mixer.setSendMode(msg.trackIndex, msg.sendIndex,
                    msg.mode == 0 ? SendMode::PreFader : SendMode::PostFader);
            }
            else if constexpr (std::is_same_v<T, SetSendEnabledMsg>) {
                m_mixer.setSendEnabled(msg.trackIndex, msg.sendIndex, msg.enabled);
            }
            else if constexpr (std::is_same_v<T, SetReturnVolumeMsg>) {
                m_mixer.setReturnVolume(msg.busIndex, msg.volume);
            }
            else if constexpr (std::is_same_v<T, SetReturnPanMsg>) {
                m_mixer.setReturnPan(msg.busIndex, msg.pan);
            }
            else if constexpr (std::is_same_v<T, SetReturnMuteMsg>) {
                m_mixer.setReturnMute(msg.busIndex, msg.muted);
            }
            else if constexpr (std::is_same_v<T, MetronomeToggleMsg>) {
                m_metronome.setEnabled(msg.enabled);
            }
            else if constexpr (std::is_same_v<T, MetronomeSetVolumeMsg>) {
                m_metronome.setVolume(msg.volume);
            }
            else if constexpr (std::is_same_v<T, MetronomeSetBeatsPerBarMsg>) {
                m_metronome.setBeatsPerBar(msg.beatsPerBar);
            }
            else if constexpr (std::is_same_v<T, SendMidiToTrackMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    midi::MidiMessage m{};
                    m.type = static_cast<midi::MidiMessage::Type>(msg.type);
                    m.channel = msg.channel;
                    m.note = msg.note;
                    m.velocity = msg.velocity;
                    m.value = msg.value;
                    m_trackMidiBuffers[msg.trackIndex].addMessage(m);
                }
            }
        }, cmd);
    }
}

void AudioEngine::emitPositionUpdate() {
    TransportPositionUpdate update;
    update.positionInSamples = m_transport.positionInSamples();
    update.positionInBeats = m_transport.positionInBeats();
    update.isPlaying = m_transport.isPlaying();
    m_eventQueue.push(update);
}

void AudioEngine::emitClipStates() {
    for (int t = 0; t < kMaxTracks; ++t) {
        const auto& state = m_clipEngine.trackState(t);
        if (state.clip) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = state.active;
            csu.playPosition = state.playPosition;
            m_eventQueue.push(csu);
        }
    }
}

void AudioEngine::emitMeterUpdates() {
    // Track meters (only emit for tracks with non-trivial peaks)
    for (int t = 0; t < kMaxTracks; ++t) {
        const auto& ch = m_mixer.trackChannel(t);
        if (ch.peakL > 0.001f || ch.peakR > 0.001f) {
            m_eventQueue.push(MeterUpdate{t, ch.peakL, ch.peakR});
        }
    }

    // Return bus meters: encoded as -2..-5
    for (int r = 0; r < kMaxReturnBuses; ++r) {
        const auto& rb = m_mixer.returnBus(r);
        if (rb.peakL > 0.001f || rb.peakR > 0.001f) {
            m_eventQueue.push(MeterUpdate{-(r + 2), rb.peakL, rb.peakR});
        }
    }

    // Master meter: encoded as -1
    const auto& master = m_mixer.master();
    if (master.peakL > 0.001f || master.peakR > 0.001f) {
        m_eventQueue.push(MeterUpdate{-1, master.peakL, master.peakR});
    }
}

} // namespace audio
} // namespace yawn
