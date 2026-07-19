#pragma once

// TcpLineServer — minimal line-based TCP command server bound to
// 127.0.0.1 only. Backs the env-gated UI command channel (YAWN_CMD);
// kept UI-free so it lives in yawn_core and is unit-testable.
//
// All sockets are non-blocking; the owner calls pump() once per frame
// from the UI thread. pump() accepts new clients, reads whatever
// bytes are available, splits on '\n' and invokes the line handler
// for each complete line. The handler's return value is sent back
// followed by '\n' — return an empty string to send nothing now and
// ack later via queueResponse() (used by the deferred screenshot
// path). pump() also flushes any queued outgoing text.
//
// Socket handles are stored as intptr_t so this header stays free of
// winsock2 (which must precede windows.h in any TU that includes
// both) — the platform dance is confined to the .cpp.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace util {

class TcpLineServer {
public:
    // Handler for one complete received line. clientId identifies the
    // connection (stable for its lifetime); the returned string is
    // sent back with a trailing '\n' (empty string = no reply yet).
    using LineHandler = std::function<std::string(int clientId,
                                                  const std::string& line)>;

    // Binds 127.0.0.1:port and listens. Port 0 picks an ephemeral
    // port (read it back via port()). On any failure ok() is false.
    explicit TcpLineServer(int port);
    ~TcpLineServer();

    TcpLineServer(const TcpLineServer&) = delete;
    TcpLineServer& operator=(const TcpLineServer&) = delete;

    bool ok() const { return m_listenFd >= 0; }
    int  port() const { return m_port; }

    void setLineHandler(LineHandler handler) { m_handler = std::move(handler); }

    // Accept / read / dispatch / flush. Call once per frame.
    void pump();

    // Queue a line for a client whose reply was deferred (the line
    // handler returned ""). No-op if the client is gone.
    void queueResponse(int clientId, const std::string& line);

private:
    struct Client {
        int         id = 0;
        intptr_t    fd = -1;
        std::string inBuf;
        std::string outBuf;
        bool        dead = false;
    };

    void acceptClients();
    void readClients();
    void flushClients();
    void dropDeadClients();

    intptr_t    m_listenFd = -1;
    int         m_port = 0;
    LineHandler m_handler;
    std::vector<Client> m_clients;
    int         m_nextClientId = 1;
    bool        m_wsaStarted = false;   // _WIN32 only

    // A peer that never terminates a line would grow inBuf without
    // bound — treat an over-long partial line as a protocol error.
    static constexpr size_t kMaxLineBytes = 64 * 1024;
};

} // namespace util
} // namespace yawn
