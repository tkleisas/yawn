#include <gtest/gtest.h>

#include "midi/RtpMidiReceiver.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using yawn::midi::MidiMessage;
using yawn::midi::RtpMidiReceiver;

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> makePacket(const std::vector<std::vector<uint8_t>>& messages,
                                bool useRunningStatus = false) {
    std::vector<uint8_t> midiList;
    bool first = true;
    for (const auto& message : messages) {
        midiList.push_back(0); // delta time
        if (useRunningStatus && !first && !message.empty()) {
            midiList.insert(midiList.end(), message.begin() + 1, message.end());
        } else {
            midiList.insert(midiList.end(), message.begin(), message.end());
        }
        first = false;
    }
    const int midiLength = static_cast<int>(midiList.size());
    const bool longHeader = midiLength > 15;
    std::vector<uint8_t> packet;
    packet.push_back(0x80);
    packet.push_back(0x61);
    put16(packet, 42);
    put32(packet, 123456);
    put32(packet, 0xABCDEF01u);
    if (longHeader) {
        packet.push_back(static_cast<uint8_t>(0x80 | 0x20 | ((midiLength >> 8) & 0x0F)));
        packet.push_back(static_cast<uint8_t>(midiLength));
    } else {
        packet.push_back(static_cast<uint8_t>(0x20 | midiLength));
    }
    packet.insert(packet.end(), midiList.begin(), midiList.end());
    return packet;
}

} // namespace

TEST(RtpMidiReceiver, ParsesSingleNoteOn) {
    const auto packet = makePacket({{0x90, 60, 100}});
    std::vector<MidiMessage> messages;
    ASSERT_EQ(1, RtpMidiReceiver::parsePacket(packet.data(),
                                              static_cast<int>(packet.size()),
                                              messages));
    ASSERT_EQ(1u, messages.size());
    EXPECT_EQ(MidiMessage::Type::NoteOn, messages[0].type);
    EXPECT_EQ(0, messages[0].channel);
    EXPECT_EQ(60, messages[0].note);
    EXPECT_EQ(100, messages[0].velocity >> 9);
}

TEST(RtpMidiReceiver, ParsesLongHeaderAndMultipleMessages) {
    const auto packet = makePacket({
        {0x90, 60, 100}, {0xB0, 74, 42}, {0xE0, 0x00, 0x40},
        {0xA0, 60, 77}, {0xD0, 55}, {0x90, 64, 90},
    });
    std::vector<MidiMessage> messages;
    ASSERT_EQ(6, RtpMidiReceiver::parsePacket(packet.data(),
                                              static_cast<int>(packet.size()),
                                              messages));
    ASSERT_EQ(6u, messages.size());
    EXPECT_EQ(MidiMessage::Type::NoteOn, messages[0].type);
    EXPECT_EQ(MidiMessage::Type::ControlChange, messages[1].type);
    EXPECT_EQ(MidiMessage::Type::PitchBend, messages[2].type);
    EXPECT_EQ(MidiMessage::Type::PolyPressure, messages[3].type);
    EXPECT_EQ(MidiMessage::Type::ChannelPressure, messages[4].type);
    EXPECT_EQ(64, messages[5].note);
}

TEST(RtpMidiReceiver, ParsesRunningStatus) {
    const auto packet = makePacket({{0x90, 60, 100}, {0x90, 62, 90},
                                    {0x90, 64, 80}},
                                   true);
    std::vector<MidiMessage> messages;
    ASSERT_EQ(3, RtpMidiReceiver::parsePacket(packet.data(),
                                              static_cast<int>(packet.size()),
                                              messages));
    ASSERT_EQ(3u, messages.size());
    EXPECT_EQ(60, messages[0].note);
    EXPECT_EQ(62, messages[1].note);
    EXPECT_EQ(64, messages[2].note);
}

TEST(RtpMidiReceiver, RejectsNonRtpMidiPackets) {
    std::vector<MidiMessage> messages;
    const auto packet = makePacket({{0x90, 60, 100}});
    EXPECT_EQ(0, RtpMidiReceiver::parsePacket(nullptr, 0, messages));
    EXPECT_EQ(0, RtpMidiReceiver::parsePacket(packet.data(), 10, messages));

    auto wrongPt = packet;
    wrongPt[1] = 0x60; // payload type 96
    EXPECT_EQ(0, RtpMidiReceiver::parsePacket(wrongPt.data(),
                                              static_cast<int>(wrongPt.size()),
                                              messages));

    auto wrongVersion = packet;
    wrongVersion[0] = 0x40;
    EXPECT_EQ(0, RtpMidiReceiver::parsePacket(wrongVersion.data(),
                                              static_cast<int>(wrongVersion.size()),
                                              messages));
}

TEST(RtpMidiReceiver, EmptyMidiListIsIgnored) {
    std::vector<uint8_t> packet;
    packet.push_back(0x80);
    packet.push_back(0x61);
    put16(packet, 1);
    put32(packet, 0);
    put32(packet, 0);
    packet.push_back(0x20); // short header, Z=1, LEN=0
    std::vector<MidiMessage> messages;
    EXPECT_EQ(0, RtpMidiReceiver::parsePacket(packet.data(),
                                              static_cast<int>(packet.size()),
                                              messages));
}

TEST(RtpMidiReceiver, ReceivesUdpPacketEndToEnd) {
#ifdef _WIN32
    WSADATA wsa{};
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa));
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(INVALID_SOCKET, sender);
#else
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sender, 0);
#endif

    // Let the OS pick a free port by binding a temporary socket.
    sockaddr_in probe{};
    probe.sin_family = AF_INET;
    probe.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    probe.sin_port = 0;
    ASSERT_EQ(0, bind(sender, reinterpret_cast<sockaddr*>(&probe), sizeof(probe)));
#ifdef _WIN32
    int probeLength = sizeof(probe);
#else
    socklen_t probeLength = sizeof(probe);
#endif
    ASSERT_EQ(0, getsockname(sender, reinterpret_cast<sockaddr*>(&probe),
                             &probeLength));
    const int port = ntohs(probe.sin_port);
#ifdef _WIN32
    closesocket(sender);
    sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(INVALID_SOCKET, sender);
#else
    close(sender);
    sender = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sender, 0);
#endif

    RtpMidiReceiver receiver;
    std::atomic<int> received{0};
    std::mutex mutex;
    std::vector<MidiMessage> messages;
    ASSERT_TRUE(receiver.start(port, [&](const MidiMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex);
        messages.push_back(msg);
        received.fetch_add(1, std::memory_order_release);
    }));

    const auto packet = makePacket({{0x90, 60, 100}, {0xB0, 74, 42}});
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    target.sin_port = htons(static_cast<uint16_t>(port));
    ASSERT_EQ(static_cast<int>(packet.size()),
              static_cast<int>(sendto(
                  sender, reinterpret_cast<const char*>(packet.data()),
                  static_cast<int>(packet.size()), 0,
                  reinterpret_cast<sockaddr*>(&target), sizeof(target))));

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (received.load(std::memory_order_acquire) < 2 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    receiver.stop();
#ifdef _WIN32
    closesocket(sender);
    WSACleanup();
#else
    close(sender);
#endif

    ASSERT_EQ(2, received.load());
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(2u, messages.size());
    EXPECT_EQ(MidiMessage::Type::NoteOn, messages[0].type);
    EXPECT_EQ(MidiMessage::Type::ControlChange, messages[1].type);
}
