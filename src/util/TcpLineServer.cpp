#include "util/TcpLineServer.h"

#ifdef _WIN32
// winsock2.h MUST come before windows.h — this TU includes the latter
// nowhere, so the ordering constraint stays contained here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace yawn {
namespace util {

namespace {

#ifdef _WIN32
using SockType = SOCKET;
constexpr intptr_t kInvalid = static_cast<intptr_t>(INVALID_SOCKET);

void closeSocket(intptr_t fd) { closesocket(static_cast<SockType>(fd)); }

void setNonBlocking(intptr_t fd) {
    u_long mode = 1;
    ioctlsocket(static_cast<SockType>(fd), FIONBIO, &mode);
}

bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using SockType = int;
constexpr intptr_t kInvalid = -1;

void closeSocket(intptr_t fd) { ::close(static_cast<SockType>(fd)); }

void setNonBlocking(intptr_t fd) {
    int flags = fcntl(static_cast<SockType>(fd), F_GETFL, 0);
    fcntl(static_cast<SockType>(fd), F_SETFL, flags | O_NONBLOCK);
}

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

} // namespace

TcpLineServer::TcpLineServer(int port) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    m_wsaStarted = true;
#endif

    SockType s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (static_cast<intptr_t>(s) == kInvalid) return;

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(s, 8) != 0) {
        closeSocket(static_cast<intptr_t>(s));
        return;
    }
    setNonBlocking(static_cast<intptr_t>(s));

    // Resolve the actual bound port (matters when port == 0).
    sockaddr_in bound{};
#ifdef _WIN32
    int boundLen = sizeof(bound);
#else
    socklen_t boundLen = sizeof(bound);
#endif
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
        m_port = ntohs(bound.sin_port);

    m_listenFd = static_cast<intptr_t>(s);
}

TcpLineServer::~TcpLineServer() {
    for (auto& c : m_clients)
        if (c.fd >= 0) closeSocket(c.fd);
    if (m_listenFd >= 0) closeSocket(m_listenFd);
#ifdef _WIN32
    if (m_wsaStarted) WSACleanup();
#endif
}

void TcpLineServer::pump() {
    if (!ok()) return;
    acceptClients();
    readClients();
    flushClients();
    dropDeadClients();
}

void TcpLineServer::acceptClients() {
    for (;;) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peerLen = sizeof(peer);
#else
        socklen_t peerLen = sizeof(peer);
#endif
        SockType c = accept(static_cast<SockType>(m_listenFd),
                            reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (static_cast<intptr_t>(c) == kInvalid) return;  // no pending peers
        setNonBlocking(static_cast<intptr_t>(c));
        Client client;
        client.id = m_nextClientId++;
        client.fd = static_cast<intptr_t>(c);
        m_clients.push_back(std::move(client));
    }
}

void TcpLineServer::readClients() {
    char buf[4096];
    for (auto& c : m_clients) {
        if (c.dead) continue;

        // Drain whatever the peer has sent so far.
        for (;;) {
            int n = recv(static_cast<SockType>(c.fd), buf, sizeof(buf), 0);
            if (n > 0) {
                c.inBuf.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) c.dead = true;          // orderly close
            else if (!wouldBlock()) c.dead = true;
            break;
        }
        if (c.dead) continue;

        // Dispatch complete lines; keep the partial tail buffered.
        for (;;) {
            const size_t nl = c.inBuf.find('\n');
            if (nl == std::string::npos) {
                if (c.inBuf.size() > kMaxLineBytes) c.dead = true;
                break;
            }
            std::string line = c.inBuf.substr(0, nl);
            c.inBuf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (m_handler) {
                std::string resp = m_handler(c.id, line);
                if (!resp.empty()) {
                    c.outBuf += resp;
                    c.outBuf += '\n';
                }
            }
        }
    }
}

void TcpLineServer::flushClients() {
    for (auto& c : m_clients) {
        if (c.dead) continue;
        while (!c.outBuf.empty()) {
#ifdef _WIN32
            int n = send(static_cast<SockType>(c.fd), c.outBuf.data(),
                         static_cast<int>(c.outBuf.size()), 0);
#else
            // MSG_NOSIGNAL: a vanished peer must not kill the process
            // with SIGPIPE mid-frame.
            int n = static_cast<int>(send(static_cast<SockType>(c.fd),
                                          c.outBuf.data(), c.outBuf.size(),
                                          MSG_NOSIGNAL));
#endif
            if (n > 0) {
                c.outBuf.erase(0, static_cast<size_t>(n));
                continue;
            }
            if (!wouldBlock()) c.dead = true;
            break;
        }
    }
}

void TcpLineServer::dropDeadClients() {
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (it->dead) {
            closeSocket(it->fd);
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
}

void TcpLineServer::queueResponse(int clientId, const std::string& line) {
    for (auto& c : m_clients) {
        if (c.id == clientId && !c.dead) {
            c.outBuf += line;
            c.outBuf += '\n';
            return;
        }
    }
}

} // namespace util
} // namespace yawn
