// World editor MCP interface: the HTTP/1.1 listener (mcp_server.h). Plain sockets, loopback only.
#include "mcp_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace editor_mcp {
namespace {
#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;
void close_socket(socket_t s) { closesocket(s); }
int poll_one(socket_t s, int ms) {
    WSAPOLLFD p{s, POLLRDNORM, 0};
    return WSAPoll(&p, 1, ms);
}
bool net_init() {
    static const bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}
constexpr int kSendFlags = 0;
#else
using socket_t = int;
constexpr socket_t kNoSocket = -1;
void close_socket(socket_t s) { close(s); }
int poll_one(socket_t s, int ms) {
    pollfd p{s, POLLIN, 0};
    return poll(&p, 1, ms);
}
bool net_init() { return true; }
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

void no_sigpipe(socket_t s) {
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    (void)s;
#endif
}
bool send_all(socket_t s, const std::string& data) {
    size_t at = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (at < data.size()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        const int n = int(send(s, data.data() + at, int(std::min<size_t>(data.size() - at, 1 << 20)), kSendFlags));
        if (n <= 0) return false;
        at += size_t(n);
    }
    return true;
}
socket_t listen_on(bool v6, int port, std::string& why) {
    socket_t s = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) return why = "no socket", kNoSocket;
    int one = 1;
#if defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&one, sizeof one);  // nobody else on this port
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);  // a restart while the old port is in TIME_WAIT
#endif
    int rc;
    if (v6) {
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&one, sizeof one);
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_loopback;
        a.sin6_port = htons(uint16_t(port));
        rc = bind(s, (const sockaddr*)&a, sizeof a);
    } else {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(uint16_t(port));
        rc = bind(s, (const sockaddr*)&a, sizeof a);
    }
    if (rc != 0 || listen(s, 8) != 0) {
        close_socket(s);
        return why = "the port is in use", kNoSocket;
    }
    return s;
}
int bound_port(socket_t s) {
    sockaddr_in a{};
    socklen_t n = sizeof a;
    if (getsockname(s, (sockaddr*)&a, &n) != 0) return 0;
    return ntohs(a.sin_port);
}
}  // namespace

struct Server::State {
    std::atomic<bool> stop{false};
    std::atomic<int> connections{0};
    std::shared_ptr<Handler> handler;
    std::vector<socket_t> listeners;
    int port = 0;
    std::thread accept_thread;
};

namespace {
void serve(std::shared_ptr<Server::State> st, socket_t c) {
    no_sigpipe(c);
    std::string buf;
    auto idle_since = std::chrono::steady_clock::now();
    auto first_byte = idle_since;
    while (!st->stop.load()) {
        Request rq;
        size_t used = 0;
        std::string why;
        const Parsed p = parse_request(buf, rq, used, why);
        if (p == Parsed::Complete) {
            buf.erase(0, used);
            Response r;
            try {
                r = st->handler->handle(rq);
            } catch (const std::exception& e) {
                r = error_response(500, "internal error");
            }
            const bool keep = rq.keep_alive() && !st->stop.load();
            if (!send_all(c, response_text(r, keep)) || !keep) break;
            idle_since = first_byte = std::chrono::steady_clock::now();
            continue;
        }
        if (p == Parsed::Bad || p == Parsed::TooLarge) {
            const bool headers = why.find("header") != std::string::npos;
            send_all(c, response_text(error_response(p == Parsed::Bad ? 400 : headers ? 431 : 413, why), false));
            break;
        }
        const int ready = poll_one(c, 250);
        const auto now = std::chrono::steady_clock::now();
        if (ready < 0) break;
        if (ready == 0) {
            if (buf.empty() && now - idle_since > std::chrono::seconds(30)) break;  // an idle keep-alive connection
            if (!buf.empty() && now - first_byte > std::chrono::seconds(10)) {     // a request that never completes
                send_all(c, response_text(error_response(408, "request timeout"), false));
                break;
            }
            continue;
        }
        char chunk[16384];
        const int n = int(recv(c, chunk, sizeof chunk, 0));
        if (n <= 0) break;
        if (buf.empty()) first_byte = now;
        buf.append(chunk, size_t(n));
    }
    close_socket(c);
    --st->connections;
}
void accept_loop(std::shared_ptr<Server::State> st) {
    while (!st->stop.load()) {
        for (socket_t l : st->listeners) {
            if (poll_one(l, st->listeners.size() > 1 ? 100 : 200) <= 0) continue;
            socket_t c = accept(l, nullptr, nullptr);
            if (c == kNoSocket) continue;
            if (st->stop.load()) {
                close_socket(c);
                break;
            }
            if (st->connections.load() >= kMaxConnections) {
                no_sigpipe(c);
                send_all(c, response_text(error_response(503, "too many connections"), false));
                close_socket(c);
                continue;
            }
            ++st->connections;
            std::thread(serve, st, c).detach();
        }
    }
    for (socket_t l : st->listeners) close_socket(l);
    st->listeners.clear();
}
}  // namespace

Server::Server() = default;
Server::~Server() { stop(); }

bool Server::start(std::shared_ptr<Handler> handler, int port, std::string& why) {
    stop();
    if (!net_init()) return why = "no network sockets on this system", false;
    auto st = std::make_shared<State>();
    st->handler = std::move(handler);
    socket_t v4 = kNoSocket;
    for (int p = port; p > 0 && p < port + 10 && v4 == kNoSocket; p++) v4 = listen_on(false, p, why);
    if (v4 == kNoSocket) v4 = listen_on(false, 0, why);
    if (v4 == kNoSocket) return why = "cannot listen on 127.0.0.1: " + why, false;
    st->port = bound_port(v4);
    st->listeners.push_back(v4);
    std::string ignored;
    socket_t v6 = listen_on(true, st->port, ignored);  // best effort: ::1 on the same port
    if (v6 != kNoSocket) st->listeners.push_back(v6);
    st->handler->set_port(st->port);
    st->accept_thread = std::thread(accept_loop, st);
    s_ = st;
    return true;
}
void Server::stop() {
    if (!s_) return;
    s_->stop = true;
    if (s_->accept_thread.joinable()) s_->accept_thread.join();
    s_.reset();
}
bool Server::running() const { return s_ != nullptr; }
int Server::port() const { return s_ ? s_->port : 0; }
int Server::connections() const { return s_ ? s_->connections.load() : 0; }

}  // namespace editor_mcp
