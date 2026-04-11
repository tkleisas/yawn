#pragma once

// ControllerMidiPort — raw MIDI I/O for controller scripts.
// Uses its own RtMidi instances, completely separate from MidiEngine.
// Supports variable-length messages (including SysEx) via a byte-oriented
// lock-free ring buffer (SPSC: RtMidi callback writes, UI thread reads).

#include <RtMidi.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "util/Logger.h"

namespace yawn {
namespace controllers {

// ---------------------------------------------------------------------------
// RawMidiRingBuffer — byte-oriented SPSC ring buffer for variable-length MIDI
// Each entry stored as: [uint16_t length][length bytes of data]
// ---------------------------------------------------------------------------
class RawMidiRingBuffer {
public:
    static constexpr int kCapacity = 16384;  // 16KB

    bool push(const uint8_t* data, int length) {
        if (length <= 0 || length > 1024) return false;  // sanity limit
        int needed = 2 + length;  // 2 bytes for length prefix

        int h = m_head.load(std::memory_order_relaxed);
        int t = m_tail.load(std::memory_order_acquire);

        int used = (h - t + kCapacity) % kCapacity;
        int free = kCapacity - 1 - used;
        if (needed > free) return false;  // full

        // Write length (little-endian)
        m_buf[h % kCapacity] = static_cast<uint8_t>(length & 0xFF);
        m_buf[(h + 1) % kCapacity] = static_cast<uint8_t>((length >> 8) & 0xFF);

        // Write data
        for (int i = 0; i < length; ++i)
            m_buf[(h + 2 + i) % kCapacity] = data[i];

        m_head.store((h + needed) % kCapacity, std::memory_order_release);
        return true;
    }

    bool pop(std::vector<uint8_t>& out) {
        int h = m_head.load(std::memory_order_acquire);
        int t = m_tail.load(std::memory_order_relaxed);

        if (h == t) return false;  // empty

        // Read length
        uint16_t length = static_cast<uint16_t>(m_buf[t % kCapacity]) |
                          (static_cast<uint16_t>(m_buf[(t + 1) % kCapacity]) << 8);

        if (length == 0 || length > 1024) {
            // Corrupted — reset
            m_tail.store(h, std::memory_order_release);
            return false;
        }

        out.resize(length);
        for (int i = 0; i < length; ++i)
            out[i] = m_buf[(t + 2 + i) % kCapacity];

        m_tail.store((t + 2 + length) % kCapacity, std::memory_order_release);
        return true;
    }

private:
    std::array<uint8_t, kCapacity> m_buf{};
    std::atomic<int> m_head{0};
    std::atomic<int> m_tail{0};
};

// ---------------------------------------------------------------------------
// ControllerMidiPort — opens its own I/O ports for a controller
// ---------------------------------------------------------------------------
class ControllerMidiPort {
public:
    ~ControllerMidiPort() { close(); }

    bool openInput(int portIndex) {
        try {
            auto in = std::make_unique<RtMidiIn>();
            std::string name = in->getPortName(portIndex);
            in->openPort(portIndex);
            in->setCallback(midiCallback, this);
            in->ignoreTypes(false, false, false);  // receive SysEx
            m_inNames.push_back(name);
            m_ins.push_back(std::move(in));
            LOG_INFO("Controller", "Opened input: %s", name.c_str());
            return true;
        } catch (const RtMidiError& e) {
            LOG_ERROR("Controller", "Failed to open input %d: %s", portIndex, e.getMessage().c_str());
            return false;
        }
    }

    bool openOutput(int portIndex) {
        try {
            auto out = std::make_unique<RtMidiOut>();
            std::string name = out->getPortName(portIndex);
            out->openPort(portIndex);
            m_outNames.push_back(name);
            m_outs.push_back(std::move(out));
            LOG_INFO("Controller", "Opened output: %s", name.c_str());
            return true;
        } catch (const RtMidiError& e) {
            LOG_ERROR("Controller", "Failed to open output %d: %s", portIndex, e.getMessage().c_str());
            return false;
        }
    }

    void close() {
        closeInput();
        closeOutput();
    }

    bool isInputOpen() const { return !m_ins.empty(); }
    bool isOutputOpen() const { return !m_outs.empty(); }
    const std::string& inputName() const { return m_inNames.empty() ? kEmpty : m_inNames[0]; }
    const std::vector<std::string>& inputNames() const { return m_inNames; }
    const std::string& outputName() const { return m_outNames.empty() ? kEmpty : m_outNames[0]; }
    const std::vector<std::string>& outputNames() const { return m_outNames; }

    // UI thread: drain all pending raw messages
    int readRawMessages(std::vector<std::vector<uint8_t>>& out) {
        int count = 0;
        std::vector<uint8_t> msg;
        while (m_ring.pop(msg)) {
            out.push_back(std::move(msg));
            msg.clear();
            ++count;
        }
        return count;
    }

    // Send raw bytes to all output ports (for SysEx or any message)
    void sendRawBytes(const uint8_t* data, int length) {
        if (length <= 0) return;
        for (auto& out : m_outs) {
            try {
                out->sendMessage(data, static_cast<size_t>(length));
            } catch (...) {}
        }
    }

    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
        uint8_t msg[3] = {
            static_cast<uint8_t>(0xB0 | (channel & 0x0F)),
            static_cast<uint8_t>(cc & 0x7F),
            static_cast<uint8_t>(value & 0x7F)
        };
        sendRawBytes(msg, 3);
    }

    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        uint8_t msg[3] = {
            static_cast<uint8_t>(0x90 | (channel & 0x0F)),
            static_cast<uint8_t>(note & 0x7F),
            static_cast<uint8_t>(velocity & 0x7F)
        };
        sendRawBytes(msg, 3);
    }

    void sendNoteOff(uint8_t channel, uint8_t note) {
        uint8_t msg[3] = {
            static_cast<uint8_t>(0x80 | (channel & 0x0F)),
            static_cast<uint8_t>(note & 0x7F),
            0
        };
        sendRawBytes(msg, 3);
    }

    // Static enumeration (delegates to RtMidi)
    static std::vector<std::string> enumerateInputPorts() {
        std::vector<std::string> names;
        try {
            RtMidiIn in;
            int n = static_cast<int>(in.getPortCount());
            names.reserve(n);
            for (int i = 0; i < n; ++i)
                names.push_back(in.getPortName(i));
        } catch (...) {}
        return names;
    }

    static std::vector<std::string> enumerateOutputPorts() {
        std::vector<std::string> names;
        try {
            RtMidiOut out;
            int n = static_cast<int>(out.getPortCount());
            names.reserve(n);
            for (int i = 0; i < n; ++i)
                names.push_back(out.getPortName(i));
        } catch (...) {}
        return names;
    }

private:
    void closeInput() {
        for (auto& in : m_ins) {
            try { in->cancelCallback(); } catch (...) {}
            try { in->closePort(); } catch (...) {}
        }
        m_ins.clear();
        m_inNames.clear();
    }

    void closeOutput() {
        for (auto& out : m_outs) {
            try { out->closePort(); } catch (...) {}
        }
        m_outs.clear();
        m_outNames.clear();
    }

    static void midiCallback(double /*deltaTime*/,
                              std::vector<unsigned char>* message,
                              void* userData) {
        auto* port = static_cast<ControllerMidiPort*>(userData);
        if (!message || message->empty()) return;
        port->m_ring.push(message->data(), static_cast<int>(message->size()));
    }

    std::vector<std::unique_ptr<RtMidiIn>> m_ins;
    std::vector<std::unique_ptr<RtMidiOut>> m_outs;
    std::vector<std::string> m_inNames;
    std::vector<std::string> m_outNames;
    static inline const std::string kEmpty;
    RawMidiRingBuffer m_ring;
};

} // namespace controllers
} // namespace yawn
