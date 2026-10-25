#pragma once

// RtpMidiReceiver — sessionless RTP-MIDI (RFC 6295) UDP listener.
//
// Accepts RTP-MIDI packets without AppleMIDI session negotiation and
// forwards parsed MIDI 1.0 messages to a callback. The callback is
// invoked from the receiver thread; YAWN's MidiEngine callback pushes
// into a lock-free ring buffer for the audio thread.

#include "midi/MidiTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace yawn {
namespace midi {

class RtpMidiReceiver {
public:
    using Callback = std::function<void(const MidiMessage&)>;

    RtpMidiReceiver() = default;
    ~RtpMidiReceiver();

    RtpMidiReceiver(const RtpMidiReceiver&) = delete;
    RtpMidiReceiver& operator=(const RtpMidiReceiver&) = delete;

    bool start(int port, Callback callback);
    void stop();

    bool running() const { return mRunning.load(std::memory_order_acquire); }
    int port() const { return mPort; }
    const std::string& lastError() const { return mLastError; }

    // Parse one RTP-MIDI packet. Returns the number of MIDI messages
    // appended to out. Public for unit tests.
    static int parsePacket(const uint8_t* data, int length,
                           std::vector<MidiMessage>& out);

private:
    void run();

#ifdef _WIN32
    using SocketHandle = uintptr_t;
#else
    using SocketHandle = int;
#endif
    static constexpr SocketHandle kInvalidSocket =
#ifdef _WIN32
        ~SocketHandle(0);
#else
        -1;
#endif
    SocketHandle mSocket = kInvalidSocket;
    int mPort = 5004;
    std::thread mThread;
    std::atomic<bool> mRunning{false};
    Callback mCallback;
    std::string mLastError;
};

} // namespace midi
} // namespace yawn
