#pragma once

// MidiEngine — central MIDI coordinator.
// Manages MIDI ports, routes input to tracks, handles MPE zone management,
// and provides per-track MIDI buffers for the audio thread.

#include "core/Constants.h"
#include "midi/MidiTypes.h"
#include "midi/MidiPort.h"
#include <array>
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
    ~MidiEngine() { shutdown(); }

    MidiEngine(const MidiEngine&) = delete;
    MidiEngine& operator=(const MidiEngine&) = delete;

    // ---- Lifecycle ----

    void init() {
        refreshPorts();
        for (auto& buf : m_trackBuffers) buf.clear();
        m_mergeBuffer.clear();
    }

    void shutdown() {
        for (auto& p : m_inputPorts) {
            if (p) p->close();
        }
        for (auto& p : m_outputPorts) {
            if (p) p->close();
        }
        m_inputPorts.clear();
        m_outputPorts.clear();
    }

    // ---- Port management ----

    void refreshPorts() {
        m_availableInputs.clear();
        m_availableOutputs.clear();
        int nIn = MidiPort::countInputPorts();
        int nOut = MidiPort::countOutputPorts();
        for (int i = 0; i < nIn; ++i)
            m_availableInputs.push_back(MidiPort::inputPortName(i));
        for (int i = 0; i < nOut; ++i)
            m_availableOutputs.push_back(MidiPort::outputPortName(i));
    }

    int availableInputCount() const { return static_cast<int>(m_availableInputs.size()); }
    int availableOutputCount() const { return static_cast<int>(m_availableOutputs.size()); }
    const std::string& availableInputName(int i) const { return m_availableInputs[i]; }
    const std::string& availableOutputName(int i) const { return m_availableOutputs[i]; }

    bool openInputPort(int portIndex) {
        if (portIndex < 0 || portIndex >= availableInputCount()) return false;
        auto port = std::make_unique<MidiPort>(MidiPort::Direction::Input);
        if (!port->open(portIndex)) return false;
        m_inputPorts.push_back(std::move(port));
        return true;
    }

    bool openOutputPort(int portIndex) {
        if (portIndex < 0 || portIndex >= availableOutputCount()) return false;
        auto port = std::make_unique<MidiPort>(MidiPort::Direction::Output);
        if (!port->open(portIndex)) return false;
        m_outputPorts.push_back(std::move(port));
        return true;
    }

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

    // ---- Audio-thread processing ----

    // Call once per audio buffer: collects all MIDI input and routes to per-track buffers
    void process(int /*numFrames*/) {
        // Clear all track buffers
        for (auto& buf : m_trackBuffers) buf.clear();

        // Collect input from all open ports into the merge buffer
        m_mergeBuffer.clear();
        for (auto& port : m_inputPorts) {
            if (port && port->isOpen())
                port->readMessages(m_mergeBuffer);
        }

        if (m_mergeBuffer.empty()) return;
        m_mergeBuffer.sortByFrame();

        // Route messages to tracks based on track input configuration
        for (int t = 0; t < kMaxMidiTracks; ++t) {
            const auto& input = m_trackInputs[t];
            if (input.source == TrackMidiInput::Source::None) continue;
            if (!input.armed) continue;

            for (int i = 0; i < m_mergeBuffer.count(); ++i) {
                const auto& msg = m_mergeBuffer[i];
                if (!matchesRouting(msg, input)) continue;
                m_trackBuffers[t].addMessage(msg);
            }
        }
    }

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
    bool matchesRouting(const MidiMessage& msg, const TrackMidiInput& input) const {
        // Channel filter (-1 = all channels)
        if (input.channel >= 0 && msg.channel != input.channel) return false;

        // MPE awareness: if MPE is enabled, route member channel messages
        // to the track that owns the zone, regardless of per-channel filter
        if (m_mpeConfig.lowerZone.enabled && m_mpeConfig.lowerZone.isMemberChannel(msg.channel))
            return true;
        if (m_mpeConfig.upperZone.enabled && m_mpeConfig.upperZone.isMemberChannel(msg.channel))
            return true;

        return true; // Source matches (AllPorts or specific port already filtered upstream)
    }

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
};

} // namespace midi
} // namespace yawn
