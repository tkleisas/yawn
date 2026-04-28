#include "audio/AudioEngine.h"
#include "util/Logger.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

std::vector<AudioDevice> AudioEngine::enumerateDevices() {
    std::vector<AudioDevice> devices;
    PaError err = Pa_Initialize();
    if (err != paNoError) return devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;
        AudioDevice d;
        d.id = i;
        d.name = info->name ? info->name : "";
        d.maxInputChannels = info->maxInputChannels;
        d.maxOutputChannels = info->maxOutputChannels;
        d.defaultSampleRate = info->defaultSampleRate;
        devices.push_back(d);
    }
    return devices;
}

int AudioEngine::defaultOutputDevice() {
    Pa_Initialize();
    int dev = Pa_GetDefaultOutputDevice();
    return dev;
}

int AudioEngine::defaultInputDevice() {
    Pa_Initialize();
    int dev = Pa_GetDefaultInputDevice();
    return dev;
}

bool AudioEngine::init(const AudioEngineConfig& config) {
    m_config = config;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        LOG_ERROR("Audio", "PortAudio init failed: %s", Pa_GetErrorText(err));
        return false;
    }
    m_paInitialized = true;

    m_transport.setSampleRate(config.sampleRate);
    m_transport.reset();

    m_clipEngine.setTransport(&m_transport);
    m_clipEngine.setSampleRate(config.sampleRate);

    m_midiClipEngine.setTransport(&m_transport);
    m_midiClipEngine.setSampleRate(config.sampleRate);

    // Set up follow action callbacks — push events to UI thread
    auto followCb = [this](int trackIndex, int sceneIndex, FollowActionType action) {
        m_eventQueue.push(FollowActionTriggeredEvent{trackIndex, sceneIndex, action});
    };
    m_clipEngine.setFollowActionCallback(followCb);
    m_midiClipEngine.setFollowActionCallback(followCb);

    m_arrPlayback.setTransport(&m_transport);
    m_arrPlayback.setSampleRate(config.sampleRate);

    m_metronome.init(config.sampleRate, config.framesPerBuffer);

    // Preallocate per-track MIDI buffers on heap
    m_trackMidiBuffers.resize(kMaxTracks);
    m_liveInputMidi.resize(kMaxTracks);

    for (int t = 0; t < kMaxMidiTracks; ++t)
        m_midiEffectChains[t].init(config.sampleRate);

    // Allocate per-track scratch buffers on heap
    const int bufferStride = kMaxFramesPerBuffer * config.outputChannels;
    m_trackBufferHeap.resize(kMaxTracks * bufferStride, 0.0f);
    for (int t = 0; t < kMaxTracks; ++t) {
        m_trackBufferPtrs[t] = m_trackBufferHeap.data() + t * bufferStride;
        m_trackSidechainSource[t] = -1;
        m_trackResampleSource[t] = -1;
    }

    // Pre-reserve recording buffers to minimise audio-thread allocations
    for (int t = 0; t < kMaxTracks; ++t) {
        m_trackRecordStates[t].pendingNotes.reserve(128);
        m_trackRecordStates[t].recordedNotes.reserve(4096);
        m_trackRecordStates[t].recordedCCs.reserve(1024);
    }

    // Allocate input buffer for deinterleaving
    m_inputBufferHeap.resize(config.inputChannels * kMaxFramesPerBuffer, 0.0f);

    // Open audio stream (full-duplex if input device available, output-only otherwise)
    PaStreamParameters outputParams;
    outputParams.device = (config.outputDevice >= 0)
        ? static_cast<PaDeviceIndex>(config.outputDevice)
        : Pa_GetDefaultOutputDevice();
    if (outputParams.device == paNoDevice) {
        LOG_ERROR("Audio", "No default audio output device found");
        return false;
    }

    outputParams.channelCount = config.outputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency =
        Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    // Try to open input device for full-duplex recording.
    // Value-init the struct: MSVC's flow analysis can't tell that
    // `inputParams.device` is only read after the conditional path
    // that fully initializes the struct (gated on m_hasInputDevice =
    // inputParamsPtr != nullptr), so leaving it uninitialized trips
    // C4701. Zero-fill keeps the contract self-evident.
    PaStreamParameters inputParams{};
    PaStreamParameters* inputParamsPtr = nullptr;
    PaDeviceIndex inputDev = (config.inputDevice >= 0)
        ? static_cast<PaDeviceIndex>(config.inputDevice)
        : Pa_GetDefaultInputDevice();

    if (inputDev != paNoDevice) {
        const PaDeviceInfo* inInfo = Pa_GetDeviceInfo(inputDev);
        if (inInfo && inInfo->maxInputChannels >= config.inputChannels) {
            inputParams.device = inputDev;
            inputParams.channelCount = config.inputChannels;
            inputParams.sampleFormat = paFloat32;
            inputParams.suggestedLatency = inInfo->defaultLowInputLatency;
            inputParams.hostApiSpecificStreamInfo = nullptr;
            inputParamsPtr = &inputParams;
        }
    }

    err = Pa_OpenStream(
        &m_stream,
        inputParamsPtr,  // nullptr if no input device
        &outputParams,
        config.sampleRate,
        config.framesPerBuffer,
        paClipOff,
        &AudioEngine::paCallback,
        this
    );

    if (err != paNoError && inputParamsPtr) {
        // Full-duplex failed, fall back to output-only
        LOG_WARN("Audio", "Full-duplex failed (%s), falling back to output-only",
                     Pa_GetErrorText(err));
        inputParamsPtr = nullptr;
        err = Pa_OpenStream(
            &m_stream,
            nullptr,
            &outputParams,
            config.sampleRate,
            config.framesPerBuffer,
            paClipOff,
            &AudioEngine::paCallback,
            this
        );
    }

    if (err != paNoError) {
        LOG_ERROR("Audio", "PortAudio open stream failed: %s", Pa_GetErrorText(err));
        return false;
    }

    m_hasInputDevice = (inputParamsPtr != nullptr);

    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(outputParams.device);
    LOG_INFO("Audio", "Output device: %s", devInfo->name);
    if (m_hasInputDevice) {
        const PaDeviceInfo* inDevInfo = Pa_GetDeviceInfo(inputParams.device);
        LOG_INFO("Audio", "Input device: %s (%d ch)", inDevInfo->name, config.inputChannels);
    } else {
        LOG_INFO("Audio", "No audio input device (recording disabled)");
    }
    LOG_INFO("Audio", "Sample rate: %.0f Hz, Buffer: %d frames, Channels: %d",
        config.sampleRate, config.framesPerBuffer, config.outputChannels);

    return true;
}

bool AudioEngine::start() {
    if (!m_stream) return false;

    PaError err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        LOG_ERROR("Audio", "PortAudio start stream failed: %s", Pa_GetErrorText(err));
        return false;
    }

    m_running.store(true, std::memory_order_release);
    LOG_INFO("Audio", "Audio engine started");
    return true;
}

void AudioEngine::stop() {
    m_running.store(false, std::memory_order_release);
    // Use PA's own active-state check rather than m_running, which
    // suspend() flips independently — calling Pa_StopStream on an
    // already-stopped (or never-started) stream returns an error but
    // nothing catastrophic.
    if (m_stream && Pa_IsStreamActive(m_stream) == 1) {
        Pa_StopStream(m_stream);
        LOG_INFO("Audio", "Audio engine stopped");
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

void AudioEngine::handleDeviceLost() {
    // Called from the UI thread when SDL reports the current audio
    // device disappeared. The PA stream is almost certainly in an
    // error state — stop and close it so a subsequent init() (with a
    // new device config from Preferences) can open fresh, without
    // leaving a dead stream behind. Leaves Pa_Initialize in place.
    m_running.store(false, std::memory_order_release);
    if (m_stream) {
        // Pa_StopStream may fail (stream already broken); ignore
        // errors — we're just trying to close cleanly.
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }
    LOG_WARN("Audio", "Device lost — stream closed, engine suspended");
}

void AudioEngine::sendCommand(const AudioCommand& cmd) {
    m_commandQueue.push(cmd);
}

bool AudioEngine::pollEvent(AudioEvent& event) {
    return m_eventQueue.pop(event);
}

// --- PortAudio callback (audio thread) ---

int AudioEngine::paCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* /*timeInfo*/,
    PaStreamCallbackFlags statusFlags,
    void* userData)
{
    auto* engine = static_cast<AudioEngine*>(userData);
    auto* output = static_cast<float*>(outputBuffer);
    auto* input = static_cast<const float*>(inputBuffer);

    // Fill helper — defensive, safe on null output (which can happen if
    // the device disappeared mid-stream on some PortAudio backends).
    auto fillSilence = [&]() {
        if (!output || !engine) return;
        const size_t n = static_cast<size_t>(framesPerBuffer)
                          * static_cast<size_t>(engine->m_config.outputChannels);
        std::memset(output, 0, n * sizeof(float));
    };

    // Nothing we can do without both the engine pointer and a writable
    // output buffer; ask PA to keep going rather than crash.
    if (!engine || !output) return paContinue;

    // Record xrun / device-failure status flags so higher layers can
    // surface them (e.g. auto-suspend on repeated paOutputUnderflow).
    if (statusFlags != 0)
        engine->m_callbackStatusFlags.fetch_or(static_cast<uint32_t>(statusFlags),
                                                std::memory_order_relaxed);

    // Suspended: output silence and skip all processing. start()/stop()
    // toggle m_running; the flag is read with acquire ordering to pair
    // with the release store in start/stop.
    if (!engine->m_running.load(std::memory_order_acquire)) {
        fillSilence();
        return paContinue;
    }

    try {
        engine->processAudio(input, output, framesPerBuffer);
    } catch (const std::exception& e) {
        LOG_ERROR("Audio", "CRASH in audio callback: %s", e.what());
        fillSilence();
        return paContinue;
    } catch (...) {
        LOG_ERROR("Audio", "CRASH in audio callback: unknown exception");
        fillSilence();
        return paContinue;
    }
    return paContinue;
}

void AudioEngine::processAudio(const float* input, float* output, unsigned long numFrames) {
    // Process any pending commands from the UI thread
    // (also populates m_liveInputMidi for virtual keyboard MIDI)
    for (int t = 0; t < kMaxTracks; ++t) m_liveInputMidi[t].clear();
    // Snapshot the transport BPM BEFORE processing commands so we can
    // tell whether the queue carried a local tempo edit. The flag is
    // forwarded into LinkManager so a UI-driven change isn't clobbered
    // by the Link session-tempo read on the same buffer (race that
    // made the BPM box behave as read-only when peers were connected).
    const double bpmBeforeCmds = m_transport.bpm();
    processCommands();
    const bool localTempoChanged = (m_transport.bpm() != bpmBeforeCmds);

    // Ableton Link: sync tempo and beat position from network peers
    {
        double bpm = m_transport.bpm();
        double beat = m_transport.positionInBeats();
        bool playing = m_transport.isPlaying() && !m_transport.isCountingIn();
        m_linkManager.onAudioCallback(bpm, beat, playing, localTempoChanged);
        if (m_linkManager.enabled() && m_linkManager.numPeers() > 0) {
            m_transport.setBPM(bpm);
            if (!playing) {
                double spb = m_config.sampleRate * 60.0 / bpm;
                m_transport.setPositionInSamples(static_cast<int64_t>(beat * spb));
            }
        }
    }

    int nc = m_config.outputChannels;
    int nf = static_cast<int>(numFrames);
    int inCh = m_config.inputChannels;

    // Clear per-track buffers (pointers already set up in init)
    for (int t = 0; t < kMaxTracks; ++t) {
        std::memset(m_trackBufferPtrs[t], 0, nf * nc * sizeof(float));
    }

    // Route audio input to monitored tracks (add to track buffer for monitoring)
    // MonitorMode: 0=Auto (only when armed), 1=In (always), 2=Off (never)
    if (input && m_hasInputDevice) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] != 0) continue;
            bool shouldMonitor = (m_trackMonitorMode[t] == 1) ||
                                 (m_trackMonitorMode[t] == 0 && m_trackArmed[t]);
            if (!shouldMonitor) continue;
            int inputCh = m_trackAudioInputCh[t];
            if (inputCh <= 0) continue;
            float* dst = m_trackBufferPtrs[t];
            for (int f = 0; f < nf; ++f) {
                if (m_trackMono[t]) {
                    int srcIdx = ((inputCh - 1) / 2) * 2;
                    if (srcIdx >= inCh) srcIdx = 0;
                    float mono = (input[f * inCh + srcIdx] +
                                  (srcIdx + 1 < inCh ? input[f * inCh + srcIdx + 1] : input[f * inCh + srcIdx])) * 0.5f;
                    dst[f * nc + 0] += mono;
                    dst[f * nc + 1] += mono;
                } else if (inputCh % 2 == 1 && inputCh >= 3) {
                    int ch0 = inputCh - 2;
                    int ch1 = inputCh - 1;
                    if (ch0 < inCh) dst[f * nc + 0] += input[f * inCh + ch0];
                    if (ch1 < inCh) dst[f * nc + (nc > 1 ? 1 : 0)] += input[f * inCh + ch1];
                } else {
                    int ch0 = (inputCh - 1) < inCh ? (inputCh - 1) : 0;
                    int ch1 = inputCh < inCh ? inputCh : ch0;
                    dst[f * nc + 0] += input[f * inCh + ch0];
                    if (nc > 1) dst[f * nc + 1] += input[f * inCh + ch1];
                }
            }
        }
    }

    // Capture audio for recording from hardware input
    // (resample recording happens later, after track buffers are fully rendered)
    if (m_transport.isRecording() && !m_transport.isCountingIn() && input && m_hasInputDevice) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] != 0) continue;
            if (m_trackResampleSource[t] >= 0) continue; // handled after rendering
            auto& ars = m_audioRecordStates[t];
            if (!ars.recording || !m_trackArmed[t]) continue;

            int64_t remaining = ars.maxFrames - ars.recordedFrames;
            if (remaining <= 0) {
                RecordBufferFullEvent bfe;
                bfe.trackIndex = t;
                bfe.sceneIndex = ars.targetScene;
                bfe.frameCount = ars.recordedFrames;
                m_eventQueue.push(bfe);
                finalizeAudioRecord(t);
                continue;
            }
            int64_t framesToCopy = std::min(static_cast<int64_t>(nf), remaining);

            // Deinterleave input into non-interleaved record buffer
            for (int ch = 0; ch < ars.channels; ++ch) {
                int srcCh = (ch < inCh) ? ch : (inCh - 1);
                float* dst = ars.buffer.data() + ch * ars.maxFrames + ars.recordedFrames;
                for (int64_t f = 0; f < framesToCopy; ++f) {
                    dst[f] = input[f * inCh + srcCh];
                }
            }
            ars.recordedFrames += framesToCopy;
        }
    }

    // Render each track's clip into its own buffer
    m_arrPlayback.applyPendingClips();
    m_clipEngine.checkAndFirePending();
    m_midiClipEngine.checkAndFirePending();
    checkPendingRecordStops(nf);
    for (int t = 0; t < kMaxTracks; ++t) {
        if (m_arrPlayback.isTrackActive(t)) {
            // Arrangement mode: play from timeline
            m_arrPlayback.processAudioTrack(t, m_trackBufferPtrs[t], nf, nc);
            m_arrPlayback.processMidiTrack(t, m_trackMidiBuffers[t], nf);
        } else {
            // Session mode: play from clip slots
            m_clipEngine.processTrackToBuffer(t, m_trackBufferPtrs[t], nf, nc);
        }
    }
    // Generate MIDI from session MIDI clips (only for non-arrangement tracks)
    m_midiClipEngine.process(m_trackMidiBuffers.data(), nf);

    // For recording tracks, clear clip MIDI so only live input goes through
    // effects and gets recorded (standard DAW behavior: clip playback is
    // suspended on the recording track).
    if (m_transport.isRecording()) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] == 1 && m_trackRecordStates[t].recording
                && m_trackArmed[t])
                m_trackMidiBuffers[t].clear();
        }
    }

    // Merge live MIDI input into track MIDI buffers
    // (hardware MIDI from MidiEngine + virtual keyboard from m_liveInputMidi)
    if (m_midiEngine) {
        m_midiEngine->process(nf);
        for (int t = 0; t < kMaxTracks; ++t) {
            const auto& liveBuf = m_midiEngine->trackBuffer(t);
            for (int i = 0; i < liveBuf.count(); ++i)
                m_trackMidiBuffers[t].addMessage(liveBuf[i]);
        }
    }
    for (int t = 0; t < kMaxTracks; ++t) {
        const auto& vkBuf = m_liveInputMidi[t];
        for (int i = 0; i < vkBuf.count(); ++i)
            m_trackMidiBuffers[t].addMessage(vkBuf[i]);
    }

    // Capture pre-effect MIDI for recording (raw input: held chords, etc.)
    // Recording pre-effect means the clip stores your actual key presses.
    // On playback, MIDI effects (arpeggiator, etc.) re-process them.
    if (m_transport.isRecording() && !m_transport.isCountingIn()) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] != 1 || !m_trackRecordStates[t].recording
                || !m_trackArmed[t])
                continue;
            auto& rs = m_trackRecordStates[t];
            double currentBeat = m_transport.positionInBeats();
            const auto& buf = m_trackMidiBuffers[t];
            for (int i = 0; i < buf.count(); ++i) {
                const auto& msg = buf[i];
                double msgBeat = currentBeat +
                    (static_cast<double>(msg.frameOffset) / m_config.sampleRate) *
                    (m_transport.bpm() / 60.0);
                double relBeat = msgBeat - rs.recordStartBeat;
                if (relBeat < 0) relBeat = 0;

                if (msg.type == midi::MidiMessage::Type::NoteOn && msg.velocity > 0) {
                    TrackRecordState::PendingNote pn;
                    pn.pitch = msg.note;
                    pn.channel = msg.channel;
                    pn.velocity = msg.velocity;
                    pn.startBeat = relBeat;
                    rs.pendingNotes.push_back(pn);
                }
                else if (msg.type == midi::MidiMessage::Type::NoteOff ||
                         (msg.type == midi::MidiMessage::Type::NoteOn && msg.velocity == 0)) {
                    for (auto it = rs.pendingNotes.begin(); it != rs.pendingNotes.end(); ++it) {
                        if (it->pitch == msg.note && it->channel == msg.channel) {
                            midi::MidiNote note;
                            note.startBeat = it->startBeat;
                            note.duration = std::max(0.01, relBeat - it->startBeat);
                            note.pitch = it->pitch;
                            note.channel = it->channel;
                            note.velocity = static_cast<uint16_t>(it->velocity * 512);
                            if (note.velocity == 0) note.velocity = 1;
                            rs.recordedNotes.push_back(note);
                            rs.pendingNotes.erase(it);
                            break;
                        }
                    }
                }
                else if (msg.type == midi::MidiMessage::Type::ControlChange) {
                    midi::MidiCCEvent cc;
                    cc.beat = relBeat;
                    cc.ccNumber = msg.ccNumber;
                    cc.value = static_cast<uint32_t>(msg.value) << 25;
                    cc.channel = msg.channel;
                    rs.recordedCCs.push_back(cc);
                }
            }
        }
    }

    // Route resampling: copy source track's buffer into destination track's buffer.
    // Source track buffers already contain clip audio rendered above.
    // For instrument tracks that also have resampling, this adds the source audio
    // to the buffer before the instrument processes (instrument output adds on top).
    for (int t = 0; t < kMaxTracks; ++t) {
        int src = m_trackResampleSource[t];
        if (src < 0 || src >= kMaxTracks || src == t) continue;
        const float* srcBuf = m_trackBufferPtrs[src];
        float* dstBuf = m_trackBufferPtrs[t];
        for (int f = 0; f < nf * nc; ++f)
            dstBuf[f] += srcBuf[f];
    }

    // Process instruments: MIDI effects → instrument
    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_instruments[t]) {
            m_trackMidiBuffers[t].clear();
            continue;
        }

        // Set sidechain input from designated source track
        int scSrc = m_trackSidechainSource[t];
        if (scSrc >= 0 && scSrc < kMaxTracks && scSrc != t)
            m_instruments[t]->setSidechainInput(m_trackBufferPtrs[scSrc]);
        else
            m_instruments[t]->setSidechainInput(nullptr);

        // Run MIDI effect chain on this track's MIDI buffer
        if (m_midiEffectChains[t].count() > 0) {
            midi::TransportInfo ti;
            ti.bpm = m_transport.bpm();
            ti.positionInBeats = m_transport.positionInBeats();
            ti.positionInSamples = m_transport.positionInSamples();
            ti.sampleRate = m_config.sampleRate;
            ti.samplesPerBeat = ti.sampleRate * 60.0 / ti.bpm;
            ti.playing = m_transport.isPlaying() && !m_transport.isCountingIn();
            ti.beatsPerBar = m_transport.beatsPerBar();
            ti.beatDenominator = m_transport.denominator();
            m_midiEffectChains[t].process(m_trackMidiBuffers[t], nf, ti);
        }

        // Render instrument into track buffer (adds to existing audio)
        m_instruments[t]->process(m_trackBufferPtrs[t], nf, nc,
                                  m_trackMidiBuffers[t]);
        m_trackMidiBuffers[t].clear();
    }

    // Apply automation envelopes to parameters before mixer runs
    {
        automation::AutomationEngine::Context actx;
        actx.positionInBeats = m_transport.positionInBeats();
        actx.isPlaying = m_transport.isPlaying() && !m_transport.isCountingIn();
        actx.mixer = &m_mixer;
        for (int t = 0; t < kMaxTracks; ++t) {
            actx.instruments[t] = m_instruments[t].get();
            actx.trackFx[t] = &m_mixer.trackEffects(t);
            actx.midiEffectChains[t] = &m_midiEffectChains[t];
            actx.trackLanes[t] = &m_trackAutoLanes[t];

            // Audio clip automation
            const auto& audioState = m_clipEngine.trackState(t);
            if (audioState.active && audioState.clip && audioState.clipAutomation) {
                actx.clips[t].playing = true;
                // Convert frame position to beats
                double spb = m_transport.samplesPerBeat();
                actx.clips[t].clipLocalBeat = (spb > 0.0) ?
                    static_cast<double>(audioState.playPosition - audioState.clip->loopStart) / spb : 0.0;
                actx.clips[t].clipLengthBeats = (spb > 0.0) ?
                    static_cast<double>(audioState.clip->lengthInFrames()) / spb : 0.0;
                actx.clips[t].clipLanes = audioState.clipAutomation;
            }

            // MIDI clip automation (overrides audio clip if both active)
            const auto& midiState = m_midiClipEngine.trackState(t);
            if (midiState.active && midiState.clip && midiState.clipAutomation) {
                actx.clips[t].playing = true;
                actx.clips[t].clipLocalBeat = midiState.playPositionBeats;
                actx.clips[t].clipLengthBeats = midiState.clip->lengthBeats();
                actx.clips[t].clipLanes = midiState.clipAutomation;
            }
        }
        m_automationEngine.process(actx);
    }

    // Resolve LFO phase links: linked LFOs recompute output using leader's base phase
    for (int t = 0; t < kMaxTracks; ++t) {
        auto& chain = m_midiEffectChains[t];
        for (int e = 0; e < chain.count(); ++e) {
            auto* fx = chain.effect(e);
            if (!fx || fx->bypassed() || !fx->isLinkedToSource()) continue;

            uint32_t targetId = fx->linkSourceId();
            if (targetId == fx->instanceId()) continue; // no self-link

            // Scan all chains for leader with matching instanceId
            midi::MidiEffect* leader = nullptr;
            for (int lt = 0; lt < kMaxTracks && !leader; ++lt) {
                auto& lchain = m_midiEffectChains[lt];
                for (int le = 0; le < lchain.count(); ++le) {
                    auto* lfx = lchain.effect(le);
                    if (lfx && lfx->instanceId() == targetId) {
                        leader = lfx;
                        break;
                    }
                }
            }
            if (leader && !leader->bypassed())
                fx->overridePhase(leader->currentPhase());
        }
    }

    // Apply LFO modulation offsets (after automation sets base values)
    for (int t = 0; t < kMaxTracks; ++t) {
        auto& chain = m_midiEffectChains[t];
        for (int e = 0; e < chain.count(); ++e) {
            auto* fx = chain.effect(e);
            if (!fx) continue;
            // When an LFO stops modulating (bypassed / depth+bias=0),
            // undo the offset it last applied so the user's base value
            // isn't left frozen at the last modulation position.
            if (fx->bypassed() || !fx->hasModulationOutput()) {
                const float prev = fx->lastAppliedOffset();
                if (prev != 0.0f) {
                    const int ttype  = fx->modulationTargetType();
                    const int tchain = fx->modulationTargetChain();
                    const int tparam = fx->modulationTargetParam();
                    switch (ttype) {
                    case 0:
                        if (m_instruments[t])
                            m_instruments[t]->setParameter(
                                tparam, m_instruments[t]->getParameter(tparam) - prev);
                        break;
                    case 1:
                        if (auto* afx = m_mixer.trackEffects(t).effectAt(tchain))
                            afx->setParameter(tparam, afx->getParameter(tparam) - prev);
                        break;
                    case 2:
                        if (auto* mfx = chain.effect(tchain); mfx && tchain != e)
                            mfx->setParameter(tparam, mfx->getParameter(tparam) - prev);
                        break;
                    case 3: {
                        const auto& ch = m_mixer.trackChannel(t);
                        if (tparam == 0)
                            m_mixer.setTrackVolume(t, std::clamp(ch.volume - prev, 0.0f, 2.0f));
                        else if (tparam == 1)
                            m_mixer.setTrackPan(t, std::clamp(ch.pan - prev, -1.0f, 1.0f));
                        else if (tparam >= 3 && tparam <= 10)
                            m_mixer.setSendLevel(t, tparam - 3,
                                std::clamp(ch.sends[tparam - 3].level - prev, 0.0f, 1.0f));
                        break;
                    }
                    }
                    fx->setLastAppliedOffset(0.0f);
                }
                continue;
            }

            float modVal = fx->modulationValue();
            int tgtType  = fx->modulationTargetType();
            int tgtChain = fx->modulationTargetChain();
            int tgtParam = fx->modulationTargetParam();
            // Undo last frame's offset before applying new one — the
            // engine stashes it per-LFO so the effective base value
            // stays stable across frames (otherwise the previous offset
            // bakes in and modulation accumulates to the clamp limit).
            const float prevOffset = fx->lastAppliedOffset();

            // Apply modulation as offset on current parameter value
            switch (tgtType) {
            case 0: // Instrument
                if (m_instruments[t]) {
                    auto info = m_instruments[t]->parameterInfo(tgtParam);
                    float range = info.maxValue - info.minValue;
                    float offset = modVal * range;
                    float base = m_instruments[t]->getParameter(tgtParam) - prevOffset;
                    float newVal = std::clamp(base + offset, info.minValue, info.maxValue);
                    m_instruments[t]->setParameter(tgtParam, newVal);
                    fx->setLastAppliedOffset(newVal - base);
                }
                break;
            case 1: { // Audio effect
                auto* afx = m_mixer.trackEffects(t).effectAt(tgtChain);
                if (afx) {
                    auto info = afx->parameterInfo(tgtParam);
                    float range = info.maxValue - info.minValue;
                    float offset = modVal * range;
                    float base = afx->getParameter(tgtParam) - prevOffset;
                    float newVal = std::clamp(base + offset, info.minValue, info.maxValue);
                    afx->setParameter(tgtParam, newVal);
                    fx->setLastAppliedOffset(newVal - base);
                }
                break;
            }
            case 2: { // MIDI effect (on same track, different slot)
                auto* mfx = chain.effect(tgtChain);
                if (mfx && tgtChain != e) { // don't self-modulate
                    auto info = mfx->parameterInfo(tgtParam);
                    float range = info.maxValue - info.minValue;
                    float offset = modVal * range;
                    float base = mfx->getParameter(tgtParam) - prevOffset;
                    float newVal = std::clamp(base + offset, info.minValue, info.maxValue);
                    mfx->setParameter(tgtParam, newVal);
                    fx->setLastAppliedOffset(newVal - base);
                }
                break;
            }
            case 3: { // Mixer
                // tgtParam maps to MixerParam enum: 0=Volume, 1=Pan, 2=Mute, 3-10=SendLevel0-7
                const auto& ch = m_mixer.trackChannel(t);
                if (tgtParam == 0) { // Volume
                    const float offset = modVal * 2.0f;
                    const float base = ch.volume - prevOffset;
                    const float newVal = std::clamp(base + offset, 0.0f, 2.0f);
                    m_mixer.setTrackVolume(t, newVal);
                    fx->setLastAppliedOffset(newVal - base);
                } else if (tgtParam == 1) { // Pan
                    const float offset = modVal * 2.0f;
                    const float base = ch.pan - prevOffset;
                    const float newVal = std::clamp(base + offset, -1.0f, 1.0f);
                    m_mixer.setTrackPan(t, newVal);
                    fx->setLastAppliedOffset(newVal - base);
                } else if (tgtParam >= 3 && tgtParam <= 10) { // Send levels
                    int sendIdx = tgtParam - 3;
                    const float offset = modVal;
                    const float base = ch.sends[sendIdx].level - prevOffset;
                    const float newVal = std::clamp(base + offset, 0.0f, 1.0f);
                    m_mixer.setSendLevel(t, sendIdx, newVal);
                    fx->setLastAppliedOffset(newVal - base);
                }
                break;
            }
            }
        }
    }

    // Capture resample recording: record from source track's fully rendered buffer
    if (m_transport.isRecording() && !m_transport.isCountingIn()) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] != 0) continue;
            int src = m_trackResampleSource[t];
            if (src < 0 || src >= kMaxTracks) continue;
            auto& ars = m_audioRecordStates[t];
            if (!ars.recording || !m_trackArmed[t]) continue;

            int64_t remaining = ars.maxFrames - ars.recordedFrames;
            if (remaining <= 0) {
                RecordBufferFullEvent bfe;
                bfe.trackIndex = t;
                bfe.sceneIndex = ars.targetScene;
                bfe.frameCount = ars.recordedFrames;
                m_eventQueue.push(bfe);
                finalizeAudioRecord(t);
                continue;
            }
            int64_t framesToCopy = std::min(static_cast<int64_t>(nf), remaining);

            // Record from source track's interleaved stereo buffer
            const float* srcBuf = m_trackBufferPtrs[src];
            for (int ch = 0; ch < ars.channels && ch < nc; ++ch) {
                float* dst = ars.buffer.data() + ch * ars.maxFrames + ars.recordedFrames;
                for (int64_t f = 0; f < framesToCopy; ++f) {
                    dst[f] = srcBuf[f * nc + ch];
                }
            }
            ars.recordedFrames += framesToCopy;
        }
    }

    // Run mixer: per-track fader/pan/mute/solo, sends→returns, master
    // Mixer clears output before writing
    m_mixer.process(m_trackBufferPtrs, kMaxTracks, output, nf, nc);

    // Metronome: add click directly to master output (bypasses mixer routing)
    // During count-in, use the count-in elapsed position so the metronome
    // clicks on every beat of the count-in bar(s).
    {
        int64_t metroPos = m_transport.isCountingIn()
            ? m_transport.countInElapsedSamples()
            : m_transport.positionInSamples();
        m_metronome.process(output, nf, nc,
                            m_transport.bpm(),
                            metroPos,
                            m_transport.isPlaying(),
                            m_transport.isRecording());
    }

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

    // Reset arrangement playback state when loop wraps
    if (m_transport.didLoopWrap()) {
        m_arrPlayback.resetAllTracks();
    }

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
                // Finalize all active recordings before stopping transport
                for (int t = 0; t < kMaxTracks; ++t) {
                    if (m_trackRecordStates[t].recording)
                        finalizeMidiRecord(t);
                    if (m_audioRecordStates[t].recording)
                        finalizeAudioRecord(t);
                }
                m_transport.stop();
                // Stop all playing clips (audio and MIDI)
                for (int t = 0; t < kMaxTracks; ++t) {
                    m_clipEngine.scheduleStop(t, QuantizeMode::None);
                    if (m_midiClipEngine.isTrackPlaying(t)) {
                        m_midiClipEngine.scheduleStop(t, QuantizeMode::None);
                        for (uint8_t n = 0; n < 128; ++n) {
                            m_trackMidiBuffers[t].addMessage(
                                midi::MidiMessage::noteOff(0, n, 0, 0));
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, TransportSetBPMMsg>) {
                m_transport.setBPM(msg.bpm);
            }
            else if constexpr (std::is_same_v<T, TransportSetTimeSignatureMsg>) {
                m_transport.setTimeSignature(msg.numerator, msg.denominator);
                m_metronome.setBeatsPerBar(msg.numerator);
            }
            else if constexpr (std::is_same_v<T, TransportSetPositionMsg>) {
                m_transport.setPositionInSamples(msg.positionInSamples);
                m_arrPlayback.resetAllTracks();
            }
            else if constexpr (std::is_same_v<T, TestToneMsg>) {
                m_testTone.enabled = msg.enabled;
                m_testTone.frequency = msg.frequency;
                m_testTone.phase = 0.0;
            }
            else if constexpr (std::is_same_v<T, LaunchClipMsg>) {
                m_clipEngine.scheduleClip(msg.trackIndex, msg.sceneIndex, msg.clip, msg.quantize, msg.clipAutomation, msg.followAction);
                if (!m_transport.isPlaying())
                    m_transport.play();
            }
            else if constexpr (std::is_same_v<T, StopClipMsg>) {
                m_clipEngine.scheduleStop(msg.trackIndex, QuantizeMode::NextBar);
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
            else if constexpr (std::is_same_v<T, MetronomeSetModeMsg>) {
                m_metronome.setMode(static_cast<MetronomeMode>(msg.mode));
            }
            else if constexpr (std::is_same_v<T, SendMidiToTrackMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    midi::MidiMessage m{};
                    m.type = static_cast<midi::MidiMessage::Type>(msg.type);
                    m.channel = msg.channel;
                    m.note = msg.note;
                    m.velocity = msg.velocity;
                    m.value = msg.value;
                    m.ccNumber = msg.ccNumber;
                    // Store in live input buffer (merged into track buffer later)
                    m_liveInputMidi[msg.trackIndex].addMessage(m);
                    if (m.isNoteOn())
                        LOG_DEBUG("MIDI", "NoteOn track=%d note=%d vel=%d inst=%s",
                                    msg.trackIndex, msg.note, msg.velocity,
                                    m_instruments[msg.trackIndex] ? m_instruments[msg.trackIndex]->name() : "NONE");
                }
            }
            else if constexpr (std::is_same_v<T, LaunchMidiClipMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    LOG_DEBUG("MIDI", "LaunchMidiClip track=%d scene=%d clip=%p len=%.2f",
                                msg.trackIndex, msg.sceneIndex, (const void*)msg.clip,
                                msg.clip ? msg.clip->lengthBeats() : 0.0);
                    m_midiClipEngine.scheduleClip(msg.trackIndex, msg.sceneIndex, msg.clip, msg.quantize, msg.clipAutomation, msg.followAction);
                    // Auto-start transport if not playing
                    if (!m_transport.isPlaying())
                        m_transport.play();
                }
            }
            else if constexpr (std::is_same_v<T, StopMidiClipMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_midiClipEngine.scheduleStop(msg.trackIndex, QuantizeMode::NextBar);
                    // Send all-notes-off to clean up any held notes
                    for (uint8_t n = 0; n < 128; ++n) {
                        m_trackMidiBuffers[msg.trackIndex].addMessage(
                            midi::MidiMessage::noteOff(0, n, 0, 0));
                    }
                }
            }
            else if constexpr (std::is_same_v<T, TransportRecordMsg>) {
                if (msg.arm) {
                    m_transport.startRecording();
                    if (m_transport.countInBars() > 0 && !m_transport.isPlaying()) {
                        m_transport.beginCountIn();
                        m_transport.play();
                    } else if (!m_transport.isPlaying()) {
                        m_transport.play();
                    }
                    // Auto-start per-track recording for all armed MIDI tracks
                    for (int t = 0; t < kMaxTracks; ++t) {
                        if (m_trackArmed[t] && m_trackType[t] == 1 &&
                            !m_trackRecordStates[t].recording) {
                            auto& rs = m_trackRecordStates[t];
                            rs.reset();
                            rs.recording = true;
                            rs.targetScene = msg.sceneIndex;
                            rs.overdub = false;
                            rs.recordStartBeat = m_transport.positionInBeats();
                        }
                    }
                } else {
                    // Gracefully stop all active recordings respecting per-track quantize
                    for (int t = 0; t < kMaxTracks; ++t) {
                        if (m_trackRecordStates[t].recording) {
                            if (m_trackRecordStates[t].pendingStopQuantize == QuantizeMode::None) {
                                // No pending stop yet — finalize immediately
                                finalizeMidiRecord(t);
                            }
                            // else: already has a pending quantized stop, let it complete naturally
                        }
                        if (m_audioRecordStates[t].recording) {
                            if (m_audioRecordStates[t].pendingStopQuantize == QuantizeMode::None) {
                                finalizeAudioRecord(t);
                            }
                        }
                    }
                    m_transport.stopRecording();
                }
            }
            else if constexpr (std::is_same_v<T, TransportSetCountInMsg>) {
                m_transport.setCountInBars(msg.bars);
            }
            else if constexpr (std::is_same_v<T, SetTrackArmedMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackArmed[msg.trackIndex] = msg.armed;
                    if (m_midiEngine) m_midiEngine->setTrackArmed(msg.trackIndex, msg.armed);
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackMonitorMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackMonitorMode[msg.trackIndex] = msg.mode;
                }
            }
            else if constexpr (std::is_same_v<T, StartMidiRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks
                    && m_trackType[msg.trackIndex] == 1) { // Midi only
                    auto& rs = m_trackRecordStates[msg.trackIndex];
                    rs.reset();
                    rs.recording = true;
                    rs.targetScene = msg.sceneIndex;
                    rs.overdub = msg.overdub;
                    rs.targetLengthBars = msg.recordLengthBars;

                    // Session recording always sets recordStartBeat explicitly,
                    // so mark usedCountIn to skip the legacy trim in finalize.
                    rs.usedCountIn = true;

                    // Auto-arm transport recording and start playback
                    if (!m_transport.isRecording()) {
                        m_transport.startRecording();
                        if (m_transport.countInBars() > 0 && !m_transport.isPlaying()) {
                            m_transport.beginCountIn();
                            rs.pendingStart = true;
                            m_transport.play();
                        } else if (!m_transport.isPlaying()) {
                            rs.recordStartBeat = m_transport.positionInBeats();
                            m_transport.play();
                        } else {
                            rs.recordStartBeat = m_transport.positionInBeats();
                        }
                    } else if (m_transport.isCountingIn()) {
                        rs.pendingStart = true;
                    } else {
                        rs.recordStartBeat = m_transport.positionInBeats();
                    }
                }
            }
            else if constexpr (std::is_same_v<T, StopMidiRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    auto& rs = m_trackRecordStates[msg.trackIndex];
                    if (rs.recording) {
                        if (msg.quantize == QuantizeMode::None) {
                            finalizeMidiRecord(msg.trackIndex);
                        } else {
                            rs.pendingStopQuantize = msg.quantize;
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, StartAudioRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks
                    && m_trackType[msg.trackIndex] == 0) { // Audio only
                    auto& ars = m_audioRecordStates[msg.trackIndex];
                    ars.reset();
                    ars.recording = true;
                    ars.targetScene = msg.sceneIndex;
                    ars.overdub = msg.overdub;
                    ars.targetLengthBars = msg.recordLengthBars;
                    ars.channels = m_config.inputChannels;
                    // Pre-allocate for 5 minutes of recording
                    static constexpr int64_t kMaxRecordSec = 300;
                    ars.maxFrames = static_cast<int64_t>(m_config.sampleRate * kMaxRecordSec);
                    ars.buffer.resize(ars.channels * ars.maxFrames, 0.0f);
                    ars.recordedFrames = 0;

                    // Session recording always sets recordStartBeat explicitly,
                    // so mark usedCountIn to skip the legacy trim in finalize.
                    ars.usedCountIn = true;

                    // Auto-arm transport recording and start playback
                    if (!m_transport.isRecording()) {
                        m_transport.startRecording();
                        if (m_transport.countInBars() > 0 && !m_transport.isPlaying()) {
                            m_transport.beginCountIn();
                            ars.pendingStart = true;
                            m_transport.play();
                        } else if (!m_transport.isPlaying()) {
                            ars.recordStartBeat = m_transport.positionInBeats();
                            m_transport.play();
                        } else {
                            ars.recordStartBeat = m_transport.positionInBeats();
                        }
                    } else if (m_transport.isCountingIn()) {
                        ars.pendingStart = true;
                    } else {
                        ars.recordStartBeat = m_transport.positionInBeats();
                    }
                }
            }
            else if constexpr (std::is_same_v<T, StopAudioRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    auto& ars = m_audioRecordStates[msg.trackIndex];
                    if (ars.recording) {
                        if (msg.quantize == QuantizeMode::None) {
                            finalizeAudioRecord(msg.trackIndex);
                        } else {
                            ars.pendingStopQuantize = msg.quantize;
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackTypeMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    int t = msg.trackIndex;
                    // Auto-stop any active recording when type changes
                    if (m_audioRecordStates[t].recording)
                        finalizeAudioRecord(t);
                    if (m_trackRecordStates[t].recording)
                        finalizeMidiRecord(t);
                    m_trackType[t] = msg.type;
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackAudioInputChMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackAudioInputCh[msg.trackIndex] = msg.channel;
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackMonoMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackMono[msg.trackIndex] = msg.mono;
                }
            }
            else if constexpr (std::is_same_v<T, SetSidechainSourceMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackSidechainSource[msg.trackIndex] = msg.sourceTrack;
                }
            }
            else if constexpr (std::is_same_v<T, SetResampleSourceMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackResampleSource[msg.trackIndex] = msg.sourceTrack;
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackMidiOutputMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackMidiOutPort[msg.trackIndex] = msg.portIndex;
                    m_trackMidiOutCh[msg.trackIndex] = msg.channel;
                }
            }
            else if constexpr (std::is_same_v<T, MoveMidiEffectMsg>) {
                // Move already done directly; kept for compatibility
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks)
                    m_midiEffectChains[msg.trackIndex].moveEffect(msg.fromIndex, msg.toIndex);
            }
            else if constexpr (std::is_same_v<T, MoveAudioEffectMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks)
                    m_mixer.trackEffects(msg.trackIndex).moveEffect(msg.fromIndex, msg.toIndex);
            }
            else if constexpr (std::is_same_v<T, ResetMidiEffectChainMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    // Send all-notes-off to avoid stuck notes after reorder
                    if (m_instruments[msg.trackIndex]) {
                        midi::MidiBuffer killBuf;
                        midi::MidiMessage allOff{};
                        allOff.type = midi::MidiMessage::Type::ControlChange;
                        allOff.ccNumber = 123;
                        allOff.value = 0;
                        allOff.channel = 0;
                        killBuf.addMessage(allOff);
                        float silence[512] = {};
                        m_instruments[msg.trackIndex]->process(silence, 1, 2, killBuf);
                    }
                    // Reset all MIDI effects on this track to clear stale state
                    m_midiEffectChains[msg.trackIndex].reset();
                }
            }
            else if constexpr (std::is_same_v<T, SetAutoModeMsg>) {
                m_automationEngine.setTrackAutoMode(msg.trackIndex,
                    static_cast<automation::AutoMode>(msg.mode));
            }
            else if constexpr (std::is_same_v<T, AutoParamTouchMsg>) {
                automation::AutomationTarget target;
                target.type       = static_cast<automation::TargetType>(msg.targetType);
                target.trackIndex = msg.trackIndex;
                target.chainIndex = msg.chainIndex;
                target.paramIndex = msg.paramIndex;
                m_automationEngine.handleParamTouch(msg.trackIndex, target, msg.value, msg.touching);
            }
            else if constexpr (std::is_same_v<T, SetEffectParamMsg>) {
                if (msg.isAudioEffect) {
                    auto* afx = m_mixer.trackEffects(msg.trackIndex).effectAt(msg.chainIndex);
                    if (afx && msg.paramIndex < afx->parameterCount())
                        afx->setParameter(msg.paramIndex, msg.value);
                } else {
                    auto* mfx = m_midiEffectChains[msg.trackIndex].effect(msg.chainIndex);
                    if (mfx && msg.paramIndex < mfx->parameterCount())
                        mfx->setParameter(msg.paramIndex, msg.value);
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackArrActiveMsg>) {
                m_arrPlayback.setTrackActive(msg.trackIndex, msg.active);
            }
            else if constexpr (std::is_same_v<T, TransportSetLoopEnabledMsg>) {
                m_transport.setLoopEnabled(msg.enabled);
            }
            else if constexpr (std::is_same_v<T, TransportSetLoopRangeMsg>) {
                m_transport.setLoopRange(msg.startBeats, msg.endBeats);
            }
        }, cmd);
    }
}

void AudioEngine::checkPendingRecordStops(int bufferSize) {
    if (!m_transport.isPlaying()) return;

    // Resolve pending record starts after count-in finishes
    bool countingIn = m_transport.isCountingIn();
    if (!countingIn) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackRecordStates[t].recording && m_trackRecordStates[t].pendingStart) {
                m_trackRecordStates[t].recordStartBeat = m_transport.positionInBeats();
                m_trackRecordStates[t].pendingStart = false;
                LOG_INFO("Audio", "MIDI record start resolved: track=%d recordStartBeat=%.3f", t, m_trackRecordStates[t].recordStartBeat);
            }
            if (m_audioRecordStates[t].recording && m_audioRecordStates[t].pendingStart) {
                m_audioRecordStates[t].recordStartBeat = m_transport.positionInBeats();
                m_audioRecordStates[t].pendingStart = false;
            }
        }
    }

    int64_t pos = m_transport.positionInSamples();
    int64_t prevPos = pos - bufferSize;
    double spb = m_transport.samplesPerBar();
    double spBeat = m_transport.samplesPerBeat();

    double currentBeat = m_transport.positionInBeats();
    int bpb = m_transport.beatsPerBar();

    for (int t = 0; t < kMaxTracks; ++t) {
        auto& rs = m_trackRecordStates[t];
        // Skip auto-stop checks if still waiting for count-in
        if (rs.pendingStart) continue;
        // Fixed-duration auto-stop (MIDI)
        if (rs.recording && rs.targetLengthBars > 0 && rs.pendingStopQuantize == QuantizeMode::None) {
            double targetBeats = rs.targetLengthBars * bpb;
            double elapsed = currentBeat - rs.recordStartBeat;
            if (elapsed >= targetBeats) {
                finalizeMidiRecord(t);
                continue;
            }
        }
        if (rs.recording && rs.pendingStopQuantize != QuantizeMode::None) {
            double interval = (rs.pendingStopQuantize == QuantizeMode::NextBar) ? spb : spBeat;
            if (interval <= 0.0) continue;
            int64_t iInterval = static_cast<int64_t>(interval);
            if (iInterval <= 0) continue;
            if ((pos / iInterval) != (prevPos / iInterval)) {
                finalizeMidiRecord(t);
            }
        }

        auto& ars = m_audioRecordStates[t];
        // Skip auto-stop checks if still waiting for count-in
        if (ars.pendingStart) continue;
        // Fixed-duration auto-stop (audio)
        if (ars.recording && ars.targetLengthBars > 0 && ars.pendingStopQuantize == QuantizeMode::None) {
            double targetBeats = ars.targetLengthBars * bpb;
            double elapsed = currentBeat - ars.recordStartBeat;
            if (elapsed >= targetBeats) {
                finalizeAudioRecord(t);
                continue;
            }
        }
        if (ars.recording && ars.pendingStopQuantize != QuantizeMode::None) {
            double interval = (ars.pendingStopQuantize == QuantizeMode::NextBar) ? spb : spBeat;
            if (interval <= 0.0) continue;
            int64_t iInterval = static_cast<int64_t>(interval);
            if (iInterval <= 0) continue;
            if ((pos / iInterval) != (prevPos / iInterval)) {
                finalizeAudioRecord(t);
            }
        }
    }
}

void AudioEngine::finalizeMidiRecord(int trackIndex) {
    auto& rs = m_trackRecordStates[trackIndex];
    if (!rs.recording) return;

    double endBeat = m_transport.positionInBeats() - rs.recordStartBeat;
    for (auto& pn : rs.pendingNotes) {
        midi::MidiNote note;
        note.startBeat = pn.startBeat;
        note.duration = std::max(0.01, endBeat - pn.startBeat);
        note.pitch = pn.pitch;
        note.channel = pn.channel;
        note.velocity = static_cast<uint16_t>(pn.velocity * 512);
        if (note.velocity == 0) note.velocity = 1;
        rs.recordedNotes.push_back(note);
    }
    rs.pendingNotes.clear();

    // Reset MIDI effect chain to clear arpeggiator state and prevent stuck notes
    m_midiEffectChains[trackIndex].reset();

    // Send all-notes-off to the instrument to silence any lingering arp notes
    if (m_instruments[trackIndex]) {
        midi::MidiBuffer killBuf;
        midi::MidiMessage allOff{};
        allOff.type = midi::MidiMessage::Type::ControlChange;
        allOff.ccNumber = 123; // All Notes Off
        allOff.value = 0;
        allOff.channel = 0;
        killBuf.addMessage(allOff);
        float silence[512] = {};
        m_instruments[trackIndex]->process(silence, 1, 2, killBuf);
    }

    // Trim leading bars equal to count-in duration (only for arrangement recording
    // where count-in was handled externally; session recording with pendingStart
    // already sets recordStartBeat after count-in, so notes are already correct)
    if (!rs.usedCountIn) {
        int countInBars = m_transport.countInBars();
        if (countInBars > 0) {
            int bpb = m_transport.beatsPerBar();
            double trimBeats = static_cast<double>(countInBars) * bpb;
            for (auto& n : rs.recordedNotes)
                n.startBeat -= trimBeats;
            for (auto& cc : rs.recordedCCs)
                cc.beat -= trimBeats;
            rs.recordStartBeat += trimBeats;
            // Remove any events that ended up before beat 0
            rs.recordedNotes.erase(
                std::remove_if(rs.recordedNotes.begin(), rs.recordedNotes.end(),
                    [](const midi::MidiNote& n) { return n.startBeat + n.duration < 0.0; }),
                rs.recordedNotes.end());
            for (auto& n : rs.recordedNotes)
                if (n.startBeat < 0.0) n.startBeat = 0.0;
            rs.recordedCCs.erase(
                std::remove_if(rs.recordedCCs.begin(), rs.recordedCCs.end(),
                    [](const midi::MidiCCEvent& cc) { return cc.beat < 0.0; }),
                rs.recordedCCs.end());
        }
    }

    // Discard empty MIDI recordings (no notes and no CCs)
    if (rs.recordedNotes.empty() && rs.recordedCCs.empty()) {
        // Emit event with noteCount=0 so UI clears recording state
        auto& xfer = m_recordedMidi[trackIndex];
        xfer.trackIndex = trackIndex;
        xfer.sceneIndex = rs.targetScene;
        xfer.notes.clear();
        xfer.ccs.clear();
        xfer.lengthBeats = 0.0;
        xfer.autoStopped = false;
        xfer.ready.store(true, std::memory_order_release);

        MidiRecordCompleteEvent evt;
        evt.trackIndex = trackIndex;
        evt.sceneIndex = rs.targetScene;
        evt.noteCount = 0;
        m_eventQueue.push(evt);

        rs.recording = false;
        rs.pendingStopQuantize = QuantizeMode::None;
        maybeStopTransportRecording();
        return;
    }

    auto& xfer = m_recordedMidi[trackIndex];
    xfer.notes = rs.recordedNotes;
    xfer.ccs = rs.recordedCCs;
    xfer.trackIndex = trackIndex;
    xfer.sceneIndex = rs.targetScene;
    xfer.overdub = rs.overdub;
    xfer.autoStopped = (rs.targetLengthBars > 0);
    double rawLen = m_transport.positionInBeats() - rs.recordStartBeat;
    int bpb = m_transport.beatsPerBar();
    double bars = (rs.targetLengthBars > 0)
        ? static_cast<double>(rs.targetLengthBars)
        : std::ceil(rawLen / bpb);
    xfer.lengthBeats = bars * bpb;
    LOG_INFO("Audio", "finalizeMidiRecord: track=%d targetLengthBars=%d rawLen=%.3f bars=%.1f lengthBeats=%.1f recordStartBeat=%.3f usedCountIn=%d",
        trackIndex, rs.targetLengthBars, rawLen, bars, xfer.lengthBeats, rs.recordStartBeat, rs.usedCountIn ? 1 : 0);
    xfer.ready.store(true, std::memory_order_release);

    MidiRecordCompleteEvent evt;
    evt.trackIndex = trackIndex;
    evt.sceneIndex = rs.targetScene;
    evt.noteCount = static_cast<int>(rs.recordedNotes.size());
    m_eventQueue.push(evt);

    rs.recording = false;
    rs.pendingStopQuantize = QuantizeMode::None;
    maybeStopTransportRecording();
}

void AudioEngine::finalizeAudioRecord(int trackIndex) {
    auto& ars = m_audioRecordStates[trackIndex];
    // Minimum ~50ms at any sample rate
    static constexpr int64_t kMinRecordFrames = 2048;
    if (!ars.recording || ars.recordedFrames < kMinRecordFrames) {
        // Emit event with frameCount=0 so UI clears recording state
        auto& xfer = m_recordedAudio[trackIndex];
        xfer.trackIndex = trackIndex;
        xfer.sceneIndex = ars.targetScene;
        xfer.frameCount = 0;
        xfer.buffer.clear();
        xfer.ready.store(true, std::memory_order_release);

        AudioRecordCompleteEvent evt;
        evt.trackIndex = trackIndex;
        evt.sceneIndex = ars.targetScene;
        evt.frameCount = 0;
        m_eventQueue.push(evt);

        ars.buffer.clear();
        ars.buffer.shrink_to_fit();
        ars.recording = false;
        ars.pendingStopQuantize = QuantizeMode::None;
        maybeStopTransportRecording();
        return;
    }

    // Trim leading bars equal to count-in duration (only for arrangement recording;
    // session recording with usedCountIn sets recordStartBeat after count-in)
    int64_t framesPerBar = static_cast<int64_t>(m_transport.samplesPerBar());
    int64_t trimFrames = 0;
    if (!ars.usedCountIn) {
        int countInBars = m_transport.countInBars();
        if (countInBars > 0 && framesPerBar > 0) {
            trimFrames = static_cast<int64_t>(countInBars) * framesPerBar;
            if (trimFrames >= ars.recordedFrames)
                trimFrames = 0;  // safety: don't trim everything
        }
    }
    int64_t effectiveFrames = ars.recordedFrames - trimFrames;

    // Clamp to exact target length when fixed-duration recording
    if (ars.targetLengthBars > 0 && framesPerBar > 0) {
        int64_t targetFrames = static_cast<int64_t>(ars.targetLengthBars) * framesPerBar;
        if (effectiveFrames > targetFrames)
            effectiveFrames = targetFrames;
    }

    auto& xfer = m_recordedAudio[trackIndex];
    xfer.channels = ars.channels;
    xfer.frameCount = effectiveFrames;
    xfer.trackIndex = trackIndex;
    xfer.sceneIndex = ars.targetScene;
    xfer.overdub = ars.overdub;
    xfer.autoStopped = (ars.targetLengthBars > 0);
    xfer.buffer.resize(ars.channels * effectiveFrames);
    for (int ch = 0; ch < ars.channels; ++ch) {
        std::memcpy(
            xfer.buffer.data() + ch * effectiveFrames,
            ars.buffer.data() + ch * ars.maxFrames + trimFrames,
            effectiveFrames * sizeof(float));
    }
    xfer.ready.store(true, std::memory_order_release);

    AudioRecordCompleteEvent evt;
    evt.trackIndex = trackIndex;
    evt.sceneIndex = ars.targetScene;
    evt.frameCount = effectiveFrames;
    evt.overdub = ars.overdub;
    m_eventQueue.push(evt);

    ars.buffer.clear();
    ars.buffer.shrink_to_fit();
    ars.recording = false;
    ars.pendingStopQuantize = QuantizeMode::None;
    maybeStopTransportRecording();
}

void AudioEngine::emitPositionUpdate() {
    TransportPositionUpdate update;
    update.positionInSamples = m_transport.positionInSamples();
    update.positionInBeats = m_transport.positionInBeats();
    update.isPlaying = m_transport.isPlaying();
    m_eventQueue.push(update);

    // Also emit record state
    if (m_transport.isRecording() || m_transport.isCountingIn()) {
        TransportRecordStateUpdate rsu;
        rsu.recording = m_transport.isRecording();
        rsu.countingIn = m_transport.isCountingIn();
        rsu.countInProgress = m_transport.countInProgress();
        rsu.countInBeats = m_transport.isCountingIn()
            ? static_cast<double>(m_transport.countInElapsedSamples()) / m_transport.samplesPerBeat()
            : 0.0;
        m_eventQueue.push(rsu);
    }
}

void AudioEngine::maybeStopTransportRecording() {
    if (!m_transport.isRecording()) return;
    for (int t = 0; t < kMaxTracks; ++t) {
        if (m_trackRecordStates[t].recording || m_audioRecordStates[t].recording)
            return; // At least one track still recording
    }
    m_transport.stopRecording();
}

void AudioEngine::emitClipStates() {
    for (int t = 0; t < kMaxTracks; ++t) {
        bool midiRec = m_trackRecordStates[t].recording;
        bool audioRec = m_audioRecordStates[t].recording;

        // Safety: only one recording type should be active per track
        if (midiRec && audioRec) {
            LOG_ERROR("Audio", "Track %d has BOTH midi and audio recording!", t);
            if (m_trackType[t] == 1)
                { finalizeAudioRecord(t); audioRec = false; }
            else
                { finalizeMidiRecord(t); midiRec = false; }
        }

        int midiRecScene = midiRec ? m_trackRecordStates[t].targetScene : -1;
        int audioRecScene = audioRec ? m_audioRecordStates[t].targetScene : -1;

        // Audio clips
        const auto& state = m_clipEngine.trackState(t);
        if (state.clip || audioRec) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = state.active;
            csu.playPosition = state.playPosition;
            csu.playingScene = state.sceneIndex;
            csu.recording = audioRec;
            csu.recordingScene = audioRecScene;
            m_eventQueue.push(csu);
        }
        // MIDI clips
        const auto& mstate = m_midiClipEngine.trackState(t);
        if (mstate.clip || midiRec) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = mstate.active;
            csu.playPosition = static_cast<int64_t>(mstate.playPositionBeats * 1000000.0);
            csu.playingScene = mstate.sceneIndex;
            csu.isMidi = true;
            csu.clipLengthBeats = mstate.clip ? mstate.clip->lengthBeats() : 0.0;
            csu.recording = midiRec;
            csu.recordingScene = midiRecScene;
            m_eventQueue.push(csu);
        }

        // Emit recording state even when no clip is loaded
        if (!state.clip && !mstate.clip && (midiRec || audioRec)) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = false;
            csu.playPosition = 0;
            csu.isMidi = midiRec;
            csu.recording = true;
            csu.recordingScene = midiRec ? midiRecScene : audioRecScene;
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
