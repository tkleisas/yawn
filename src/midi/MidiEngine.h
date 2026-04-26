#pragma once

// MidiEngine — central MIDI coordinator.
// Manages MIDI ports, routes input to tracks, handles MPE zone management,
// and provides per-track MIDI buffers for the audio thread.

#include "core/Constants.h"
#include "midi/MidiTypes.h"
#include "midi/MidiPort.h"
#include "midi/MidiMonitorBuffer.h"
#include "midi/MidiMapping.h"
#include "util/MessageQueue.h"
#include "visual/VisualKnobBus.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace midi {

static constexpr int kMaxMidiPorts = 16;

// Track MIDI input routing
struct TrackMidiInput {
    enum class Source : uint8_t {
        None,       // No MIDI input
        AllPorts,   // Receive from all open input ports
        Port,       // Receive from a specific port
    };

    Source   source    = Source::AllPorts;
    int      portIndex = -1;     // Which port (when Source::Port)
    int      channel   = -1;     // -1 = all channels, 0-15 = specific channel
    bool     armed     = false;  // Track must be armed to receive live input
};

class MidiEngine {
public:
    MidiEngine() = default;
    ~MidiEngine();

    MidiEngine(const MidiEngine&) = delete;
    MidiEngine& operator=(const MidiEngine&) = delete;

    // ---- Lifecycle ----

    void init();
    void shutdown();

    // ---- Port management ----

    void refreshPorts();

    int availableInputCount() const { return static_cast<int>(m_availableInputs.size()); }
    int availableOutputCount() const { return static_cast<int>(m_availableOutputs.size()); }
    const std::string& availableInputName(int i) const { return m_availableInputs[i]; }
    const std::string& availableOutputName(int i) const { return m_availableOutputs[i]; }

    bool openInputPort(int portIndex);
    bool openOutputPort(int portIndex);

    int openInputPortCount() const { return static_cast<int>(m_inputPorts.size()); }
    int openOutputPortCount() const { return static_cast<int>(m_outputPorts.size()); }

    // ---- Track routing ----

    void setTrackInput(int trackIndex, const TrackMidiInput& input) {
        if (trackIndex >= 0 && trackIndex < kMaxMidiTracks)
            m_trackInputs[trackIndex] = input;
    }

    const TrackMidiInput& trackInput(int trackIndex) const {
        static const TrackMidiInput kDefault{};
        if (trackIndex < 0 || trackIndex >= kMaxMidiTracks) return kDefault;
        return m_trackInputs[trackIndex];
    }

    void setTrackArmed(int trackIndex, bool armed) {
        if (trackIndex >= 0 && trackIndex < kMaxMidiTracks)
            m_trackInputs[trackIndex].armed = armed;
    }

    // ---- MPE Configuration ----

    MpeConfig& mpeConfig() { return m_mpeConfig; }
    const MpeConfig& mpeConfig() const { return m_mpeConfig; }

    // ---- Monitor ----

    void setMonitorBuffer(MidiMonitorBuffer* buf) { m_monitor = buf; }
    MidiMonitorBuffer* monitorBuffer() const { return m_monitor; }

    // ---- Audio-thread processing ----

    // Call once per audio buffer: collects all MIDI input and routes to per-track buffers
    void process(int numFrames);

    // Get the routed MIDI buffer for a track (valid after process())
    const MidiBuffer& trackBuffer(int trackIndex) const {
        if (trackIndex < 0 || trackIndex >= kMaxMidiTracks) {
            static const MidiBuffer kEmpty{};
            return kEmpty;
        }
        return m_trackBuffers[trackIndex];
    }

    // ---- Output ----

    void sendToAllOutputs(const MidiBuffer& buffer) {
        for (auto& port : m_outputPorts) {
            if (port && port->isOpen())
                port->sendBuffer(buffer);
        }
    }

    void sendToOutput(int portIndex, const MidiBuffer& buffer) {
        if (portIndex >= 0 && portIndex < static_cast<int>(m_outputPorts.size())) {
            if (m_outputPorts[portIndex] && m_outputPorts[portIndex]->isOpen())
                m_outputPorts[portIndex]->sendBuffer(buffer);
        }
    }

private:
    bool matchesRouting(const MidiMessage& msg, const TrackMidiInput& input) const;
    void resolveAndSend(const automation::AutomationTarget& target, float value);

    // Available port names (refreshed by refreshPorts())
    std::vector<std::string> m_availableInputs;
    std::vector<std::string> m_availableOutputs;

    // Open ports
    std::vector<std::unique_ptr<MidiPort>> m_inputPorts;
    std::vector<std::unique_ptr<MidiPort>> m_outputPorts;

    // Per-track input routing
    std::array<TrackMidiInput, kMaxMidiTracks> m_trackInputs;

    // Per-track MIDI buffers (filled by process(), read by audio engine)
    std::array<MidiBuffer, kMaxMidiTracks> m_trackBuffers;

    // Scratch buffer for merging all port input
    MidiBuffer m_mergeBuffer;

    // MPE configuration
    MpeConfig m_mpeConfig;

    // Monitor (optional, owned externally)
    MidiMonitorBuffer* m_monitor = nullptr;

    // MIDI Learn (optional, owned externally)
    MidiLearnManager* m_learnManager = nullptr;
    std::function<void(const audio::AudioCommand&)> m_sendCommand;

public:
    void setLearnManager(MidiLearnManager* mgr) { m_learnManager = mgr; }
    MidiLearnManager* learnManager() const { return m_learnManager; }

    // Set callback for sending commands to AudioEngine (for MIDI-mapped params)
    void setCommandSender(std::function<void(const audio::AudioCommand&)> fn) {
        m_sendCommand = std::move(fn);
    }
};

} // namespace midi
} // namespace yawn
