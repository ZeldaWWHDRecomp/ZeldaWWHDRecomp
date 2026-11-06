// SPDX-License-Identifier: MPL-2.0
#include "udp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace online::proto {

namespace {
#ifdef _WIN32
struct WsaInit {
    WsaInit() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
};
void ensure_wsa() { static WsaInit w; }
using sock_t = SOCKET;
constexpr uintptr_t kNone = ~uintptr_t(0);
#else
void ensure_wsa() {}
#endif
}  // namespace

bool Address::operator==(const Address& o) const {
    return len == o.len && tcp_id == o.tcp_id && std::memcmp(storage, o.storage, len) == 0;
}

std::string Address::text() const {
    char host[64] = "?";
    const sockaddr* sa = reinterpret_cast<const sockaddr*>(storage);
    int port = 0;
    if (sa->sa_family == AF_INET) {
        auto* in = reinterpret_cast<const sockaddr_in*>(storage);
        inet_ntop(AF_INET, &in->sin_addr, host, sizeof host);
        port = ntohs(in->sin_port);
        return std::string(host) + ":" + std::to_string(port);
    }
    if (sa->sa_family == AF_INET6) {
        auto* in = reinterpret_cast<const sockaddr_in6*>(storage);
        inet_ntop(AF_INET6, &in->sin6_addr, host, sizeof host);
        port = ntohs(in->sin6_port);
        return "[" + std::string(host) + "]:" + std::to_string(port);
    }
    return "?";
}

std::string Address::ip_bytes() const {
    const sockaddr* sa = reinterpret_cast<const sockaddr*>(storage);
    if (sa->sa_family == AF_INET) {
        auto* in = reinterpret_cast<const sockaddr_in*>(storage);
        return std::string(reinterpret_cast<const char*>(&in->sin_addr), 4);
    }
    if (sa->sa_family == AF_INET6) {
        auto* in = reinterpret_cast<const sockaddr_in6*>(storage);
        // per /64: one host gets one prefix
        return std::string(reinterpret_cast<const char*>(&in->sin6_addr), 8);
    }
    return {};
}

bool Address::is_loopback() const {
    const sockaddr* sa = reinterpret_cast<const sockaddr*>(storage);
    if (sa->sa_family == AF_INET) {
        auto* in = reinterpret_cast<const sockaddr_in*>(storage);
        return (ntohl(in->sin_addr.s_addr) >> 24) == 127;
    }
    if (sa->sa_family == AF_INET6) {
        auto* in = reinterpret_cast<const sockaddr_in6*>(storage);
        static const uint8_t lo[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        return std::memcmp(&in->sin6_addr, lo, 16) == 0;
    }
    return false;
}

uint64_t Address::hash() const {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) h = (h ^ storage[i]) * 1099511628211ull;
    return h ^ (uint64_t(tcp_id) << 32);
}

bool resolve(const std::string& host, uint16_t port, Address& out, std::string& why) {
    ensure_wsa();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    std::string h = host.empty() ? "127.0.0.1" : host;
    if (h.size() > 2 && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
    char ps[8];
    std::snprintf(ps, sizeof ps, "%u", unsigned(port));
    int rc = getaddrinfo(h.c_str(), ps, &hints, &res);
    if (rc != 0 || !res) {
        why = "cannot resolve " + host;
        return false;
    }
    // prefer IPv4 (the server binds IPv4 by default)
    addrinfo* pick = res;
    for (addrinfo* a = res; a; a = a->ai_next)
        if (a->ai_family == AF_INET) {
            pick = a;
            break;
        }
    out = Address();
    out.len = uint8_t(pick->ai_addrlen > sizeof out.storage ? sizeof out.storage : pick->ai_addrlen);
    std::memcpy(out.storage, pick->ai_addr, out.len);
    freeaddrinfo(res);
    return true;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::is_open() const {
#ifdef _WIN32
    return s_ != kNone;
#else
    return s_ >= 0;
#endif
}

void UdpSocket::close() {
    if (!is_open()) return;
#ifdef _WIN32
    closesocket(sock_t(s_));
    s_ = kNone;
#else
    ::close(s_);
    s_ = -1;
#endif
}

#ifdef _WIN32
#define native() sock_t(s_)
#else
#define native() s_
#endif

bool UdpSocket::open(const std::string& host, uint16_t port, std::string& why, bool reuse_port) {
    ensure_wsa();
    close();
    Address a;
    if (!resolve(host.empty() ? "127.0.0.1" : host, port, a, why)) return false;
    int fam = reinterpret_cast<const sockaddr*>(a.storage)->sa_family;
#ifdef _WIN32
    sock_t s = socket(fam, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        why = "socket() failed";
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    s_ = uintptr_t(s);
#else
    int s = socket(fam, SOCK_DGRAM, 0);
    if (s < 0) {
        why = "socket() failed";
        return false;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    s_ = s;
#endif
#ifndef _WIN32
    if (reuse_port) {
        // several sockets on one port: the server sends from one per worker thread (one socket's send path is
        // a single lock in the kernel) and reads them all
        int one = 1;
        setsockopt(native(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(native(), SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    }
#else
    (void)reuse_port;
#endif
    if (bind(native(), reinterpret_cast<const sockaddr*>(a.storage), a.len) != 0) {
        why = "cannot bind " + a.text();
        close();
        return false;
    }
    return true;
}

uint16_t UdpSocket::local_port() const {
    sockaddr_storage ss{};
    socklen_t n = sizeof ss;
    if (getsockname(native(), reinterpret_cast<sockaddr*>(&ss), &n) != 0) return 0;
    if (ss.ss_family == AF_INET) return ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
    if (ss.ss_family == AF_INET6) return ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
    return 0;
}

bool UdpSocket::send_to(const Address& to, const uint8_t* d, size_t n) {
    if (!is_open()) return false;
#ifdef _WIN32
    int rc = sendto(native(), reinterpret_cast<const char*>(d), int(n), 0, reinterpret_cast<const sockaddr*>(to.storage),
                    to.len);
#else
    ssize_t rc = sendto(native(), d, n, 0, reinterpret_cast<const sockaddr*>(to.storage), to.len);
#endif
    if (rc < 0) {
        ++send_errors;
        return false;
    }
    return true;
}

size_t UdpSocket::send_many(const Address& to, const std::vector<std::vector<uint8_t>>& dgs) {
#if defined(__linux__)
    size_t sent = 0;
    while (sent < dgs.size()) {
        mmsghdr msgs[64];
        iovec iov[64];
        size_t n = std::min<size_t>(64, dgs.size() - sent);
        for (size_t i = 0; i < n; ++i) {
            iov[i].iov_base = const_cast<uint8_t*>(dgs[sent + i].data());
            iov[i].iov_len = dgs[sent + i].size();
            std::memset(&msgs[i], 0, sizeof msgs[i]);
            msgs[i].msg_hdr.msg_name = const_cast<uint8_t*>(to.storage);
            msgs[i].msg_hdr.msg_namelen = to.len;
            msgs[i].msg_hdr.msg_iov = &iov[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }
        int rc = sendmmsg(native(), msgs, unsigned(n), 0);
        if (rc <= 0) {
            ++send_errors;
            break;
        }
        sent += size_t(rc);
    }
    return sent;
#else
    size_t sent = 0;
    for (auto& d : dgs) sent += send_to(to, d.data(), d.size()) ? 1 : 0;
    return sent;
#endif
}

int UdpSocket::recv_from(Address& from, uint8_t* buf, size_t cap) {
    if (!is_open()) return -1;
    sockaddr_storage ss{};
    socklen_t n = sizeof ss;
#ifdef _WIN32
    int rc = recvfrom(native(), reinterpret_cast<char*>(buf), int(cap), 0, reinterpret_cast<sockaddr*>(&ss), &n);
#else
    ssize_t rc = recvfrom(native(), buf, cap, 0, reinterpret_cast<sockaddr*>(&ss), &n);
#endif
    if (rc < 0) return -1;
    from = Address();
    from.len = uint8_t(n > sizeof from.storage ? sizeof from.storage : n);
    std::memcpy(from.storage, &ss, from.len);
    return int(rc);
}

bool UdpSocket::wait(int timeout_ms) {
    if (!is_open()) return false;
#ifdef _WIN32
    WSAPOLLFD p{};
    p.fd = native();
    p.events = POLLRDNORM;
    return WSAPoll(&p, 1, timeout_ms) > 0;
#else
    pollfd p{};
    p.fd = native();
    p.events = POLLIN;
    return ::poll(&p, 1, timeout_ms) > 0;
#endif
}

void UdpSocket::set_buffers(int bytes) {
    if (!is_open()) return;
    setsockopt(native(), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof bytes);
    setsockopt(native(), SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bytes), sizeof bytes);
}

#undef native

uint64_t monotonic_us() {
    static const auto t0 = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
}

uint32_t monotonic_ms() {
    uint32_t v = uint32_t(monotonic_us() / 1000);
    return v ? v : 1;
}

}  // namespace online::proto
