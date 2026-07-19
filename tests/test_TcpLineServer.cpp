// TcpLineServer tests — real loopback sockets, no mocks. The server
// is non-blocking and single-threaded, so each test drives pump() in
// a poll loop with a deadline instead of sleeping fixed amounts.

#include <gtest/gtest.h>

#include "util/TcpLineServer.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using yawn::util::TcpLineServer;

namespace {

#ifdef _WIN32
using SockT = SOCKET;
inline void setNonBlocking(SockT s) {
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}
#else
using SockT = int;
inline void setNonBlocking(SockT s) {
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
}
#endif

// Client socket for the loopback server under test. Blocking connect
// (completes immediately against a listening loopback socket), then
// non-blocking for sends/reads so poll loops never hang.
struct TestClient {
    intptr_t fd = -1;

    explicit TestClient(int port) {
#ifdef _WIN32
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        fd = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        EXPECT_GE(fd, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<unsigned short>(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int rc = connect(static_cast<SockT>(fd),
                         reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        EXPECT_EQ(rc, 0);
        setNonBlocking(static_cast<SockT>(fd));
    }
    ~TestClient() {
        if (fd >= 0) {
#ifdef _WIN32
            closesocket(static_cast<SockT>(fd));
#else
            ::close(static_cast<int>(fd));
#endif
        }
    }

    void send(const std::string& s) {
        int n = ::send(static_cast<SockT>(fd), s.data(),
                       static_cast<int>(s.size()), 0);
        EXPECT_EQ(n, static_cast<int>(s.size()));
    }

    // Read whatever is available right now (non-blocking).
    std::string poll() {
        char buf[4096];
        int n = recv(static_cast<SockT>(fd), buf, sizeof(buf), 0);
        if (n <= 0) return {};
        return std::string(buf, static_cast<size_t>(n));
    }
};

// Pump the server and drain the client until `received` contains
// `expected` or the deadline (2 s) passes. Returns the accumulated
// client-side text.
std::string pumpUntil(TcpLineServer& server, TestClient& client,
                      const std::string& expected, std::string received = {}) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (received.find(expected) == std::string::npos &&
           std::chrono::steady_clock::now() < deadline) {
        server.pump();
        received += client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return received;
}

} // namespace

TEST(TcpLineServer, BindsEphemeralPort) {
    TcpLineServer server(0);
    ASSERT_TRUE(server.ok());
    EXPECT_GT(server.port(), 0);
}

TEST(TcpLineServer, EchoRoundTrip) {
    TcpLineServer server(0);
    ASSERT_TRUE(server.ok());
    server.setLineHandler([](int, const std::string& line) {
        return "ECHO:" + line;
    });
    TestClient client(server.port());
    client.send("ping\n");
    EXPECT_NE(pumpUntil(server, client, "ECHO:ping\n").find("ECHO:ping\n"),
              std::string::npos);
}

TEST(TcpLineServer, PartialLineHeldUntilComplete) {
    TcpLineServer server(0);
    server.setLineHandler([](int, const std::string& line) {
        return "GOT:" + line;
    });
    TestClient client(server.port());
    client.send("hel");
    // Give the server several pumps — no complete line yet, so no reply.
    std::string early = pumpUntil(server, client, "GOT:");
    EXPECT_EQ(early.find("GOT:"), std::string::npos);
    client.send("lo\n");
    EXPECT_NE(pumpUntil(server, client, "GOT:hello\n").find("GOT:hello\n"),
              std::string::npos);
}

TEST(TcpLineServer, MultipleLinesOnePacket) {
    TcpLineServer server(0);
    server.setLineHandler([](int, const std::string& line) {
        return "R:" + line;
    });
    TestClient client(server.port());
    client.send("one\ntwo\n");
    const std::string got = pumpUntil(server, client, "R:two\n");
    EXPECT_NE(got.find("R:one\n"), std::string::npos);
    EXPECT_NE(got.find("R:two\n"), std::string::npos);
}

TEST(TcpLineServer, DeferredQueueResponse) {
    TcpLineServer server(0);
    int seenClient = -1;
    server.setLineHandler([&](int clientId, const std::string& line) {
        seenClient = clientId;
        return std::string();   // no immediate reply
    });
    TestClient client(server.port());
    client.send("later\n");
    // Pump until the handler has run (nothing should come back).
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (seenClient < 0 && std::chrono::steady_clock::now() < deadline) {
        server.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GE(seenClient, 0);
    EXPECT_EQ(client.poll(), "");

    server.queueResponse(seenClient, "OK deferred");
    EXPECT_NE(pumpUntil(server, client, "OK deferred\n").find("OK deferred\n"),
              std::string::npos);
}

TEST(TcpLineServer, ClientDisconnectHandled) {
    TcpLineServer server(0);
    server.setLineHandler([](int, const std::string& line) {
        return "ECHO:" + line;
    });
    int clientId = -1;
    {
        TestClient client(server.port());
        client.send("bye\n");
        pumpUntil(server, client, "ECHO:bye\n");
        // Capture the server's id for this connection via a second round.
        server.setLineHandler([&](int id, const std::string& line) {
            clientId = id;
            return "ECHO:" + line;
        });
        client.send("who\n");
        pumpUntil(server, client, "ECHO:who\n");
    }   // client socket closes here
    // Server must survive the disconnect; queueing to the dead id is a no-op.
    server.pump();
    server.pump();
    if (clientId >= 0) server.queueResponse(clientId, "ghost");
    server.pump();
    SUCCEED();
}

TEST(TcpLineServer, CarriageReturnStripped) {
    TcpLineServer server(0);
    server.setLineHandler([](int, const std::string& line) {
        return "LINE:" + line;
    });
    TestClient client(server.port());
    client.send("crlf\r\n");
    EXPECT_NE(pumpUntil(server, client, "LINE:crlf\n").find("LINE:crlf\n"),
              std::string::npos);
}
