#include "midi/RtpMidiReceiver.h"
#include "util/Logger.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace yawn {
namespace midi {

namespace {

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 static_cast<uint16_t>(data[1]));
}

bool skipDeltaTime(const uint8_t* data, int end, int& pos) {
    for (int i = 0; i < 4; ++i) {
        if (pos >= end) return false;
        const uint8_t value = data[pos++];
        if ((value & 0x80) == 0) return true;
    }
    return false;
}

int midiDataLength(uint8_t status) {
    if (status < 0xF0) {
        switch (status & 0xF0) {
            case 0xC0:
            case 0xD0:
                return 1;
            default:
                return 2;
        }
    }
    switch (status) {
        case 0xF0:
            return -1; // SysEx (variable length; skipped by the parser)
        case 0xF1:
        case 0xF3:
            return 1;
        case 0xF2:
            return 2;
        default:
            return 0;
    }
}

} // namespace

RtpMidiReceiver::~RtpMidiReceiver() {
    stop();
}

int RtpMidiReceiver::parsePacket(const uint8_t* data, int length,
                                 std::vector<MidiMessage>& out) {
    if (!data || length < 13) return 0;
    if ((data[0] >> 6) != 2) return 0;
    if ((data[1] & 0x7F) != 97) return 0;

    const bool hasPadding = (data[0] & 0x20) != 0;
    const bool hasExtension = (data[0] & 0x10) != 0;
    const int csrcCount = data[0] & 0x0F;
    int headerLength = 12 + csrcCount * 4;
    if (headerLength > length) return 0;

    if (hasExtension) {
        if (headerLength + 4 > length) return 0;
        const int extensionWords = readU16(data + headerLength + 2);
        headerLength += 4 + extensionWords * 4;
        if (headerLength > length) return 0;
    }
    if (headerLength >= length) return 0;

    int payloadLength = length - headerLength;
    if (hasPadding) {
        const int padding = data[length - 1];
        payloadLength -= padding;
        if (payloadLength <= 0) return 0;
    }

    const uint8_t* payload = data + headerLength;
    if (payloadLength < 1) return 0;

    const uint8_t header = payload[0];
    const bool longHeader = (header & 0x80) != 0;
    const bool hasJournal = (header & 0x40) != 0;
    const bool hasFirstDelta = (header & 0x20) != 0;
    (void)hasJournal; // The journal section follows the MIDI list; ignored.

    int pos = 1;
    int midiLength = header & 0x0F;
    if (longHeader) {
        if (payloadLength < 2) return 0;
        midiLength = (midiLength << 8) | payload[1];
        pos = 2;
    }
    if (midiLength <= 0 || pos + midiLength > payloadLength) return 0;

    const int end = pos + midiLength;
    uint8_t runningStatus = 0;
    bool needDelta = hasFirstDelta;
    int parsed = 0;

    while (pos < end) {
        if (needDelta) {
            if (!skipDeltaTime(payload, end, pos)) break;
            if (pos >= end) break;
        }

        uint8_t status = 0;
        if ((payload[pos] & 0x80) != 0) {
            status = payload[pos++];
        } else {
            if (runningStatus == 0) break;
            status = runningStatus;
        }

        const int dataLength = midiDataLength(status);
        if (dataLength < 0) {
            // SysEx: skip through the terminating 0xF7.
            while (pos < end && payload[pos] != 0xF7) ++pos;
            if (pos < end) ++pos;
            runningStatus = 0;
            needDelta = true;
            continue;
        }
        if (pos + dataLength > end) break;

        uint8_t bytes[3] = {status, 0, 0};
        for (int i = 0; i < dataLength; ++i) {
            bytes[1 + i] = payload[pos++];
        }
        const MidiMessage message = Parse::fromBytes(bytes, 1 + dataLength);
        if (message.type != MidiMessage::Type::None) {
            out.push_back(message);
            ++parsed;
        }

        if (status < 0xF0) {
            runningStatus = status;
        } else if (status < 0xF8) {
            runningStatus = 0;
        }
        needDelta = true;
    }
    return parsed;
}

bool RtpMidiReceiver::start(int port, Callback callback) {
    stop();
    mLastError.clear();
    if (port <= 0 || port > 65535) {
        mLastError = "invalid port";
        return false;
    }

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        mLastError = "WSAStartup failed";
        return false;
    }
    mSocket = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (mSocket == kInvalidSocket) {
        mLastError = "could not create UDP socket";
        WSACleanup();
        return false;
    }
#else
    mSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mSocket < 0) {
        mLastError = std::string("could not create UDP socket: ") +
                     std::strerror(errno);
        return false;
    }
#endif

    int reuse = 1;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(mSocket), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(mSocket, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0) {
#ifdef _WIN32
        mLastError = "could not bind UDP port " + std::to_string(port);
        closesocket(static_cast<SOCKET>(mSocket));
        mSocket = kInvalidSocket;
        WSACleanup();
#else
        mLastError = "could not bind UDP port " + std::to_string(port) +
                     ": " + std::strerror(errno);
        close(mSocket);
        mSocket = -1;
#endif
        return false;
    }

#ifdef _WIN32
    DWORD timeoutMs = 200;
    setsockopt(static_cast<SOCKET>(mSocket), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    mPort = port;
    mCallback = std::move(callback);
    mRunning.store(true, std::memory_order_release);
    mThread = std::thread(&RtpMidiReceiver::run, this);
    LOG_INFO("MIDI", "RTP-MIDI listening on UDP port %d", port);
    return true;
}

void RtpMidiReceiver::stop() {
    mRunning.store(false, std::memory_order_release);
#ifdef _WIN32
    const bool hadSocket = mSocket != kInvalidSocket;
    if (hadSocket) {
        closesocket(static_cast<SOCKET>(mSocket));
        mSocket = kInvalidSocket;
    }
#else
    const bool hadSocket = mSocket >= 0;
    if (hadSocket) {
        close(mSocket);
        mSocket = -1;
    }
#endif
    if (mThread.joinable()) {
        mThread.join();
    }
#ifdef _WIN32
    if (hadSocket) {
        WSACleanup();
    }
#endif
    mCallback = nullptr;
}

void RtpMidiReceiver::run() {
    std::vector<uint8_t> buffer(2048);
    std::vector<MidiMessage> messages;
    while (mRunning.load(std::memory_order_acquire)) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLength = sizeof(from);
#else
        socklen_t fromLength = sizeof(from);
#endif
#ifdef _WIN32
        const auto rawSocket = static_cast<SOCKET>(mSocket);
#else
        const int rawSocket = mSocket;
#endif
        const int received = static_cast<int>(
            recvfrom(rawSocket, reinterpret_cast<char*>(buffer.data()),
                     static_cast<int>(buffer.size()), 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLength));
        if (received <= 0) {
            continue;
        }

        messages.clear();
        const int count = parsePacket(buffer.data(), received, messages);
        if (count > 0 && mCallback) {
            for (const auto& message : messages) {
                mCallback(message);
            }
        }
    }
}

} // namespace midi
} // namespace yawn
