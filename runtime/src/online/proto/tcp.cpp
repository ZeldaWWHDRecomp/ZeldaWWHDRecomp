// SPDX-License-Identifier: MPL-2.0
#include "tcp.h"

#include <cerrno>
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
using socklen_t = int;
#define WOULD_BLOCK (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS)
static void sock_close(intptr_t s) { closesocket(SOCKET(s)); }
static void set_nonblock(intptr_t s) {
    u_long nb = 1;
    ioctlsocket(SOCKET(s), FIONBIO, &nb);
}
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define WOULD_BLOCK (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS)
static void sock_close(intptr_t s) { ::close(int(s)); }
static void set_nonblock(intptr_t s) { fcntl(int(s), F_SETFL, fcntl(int(s), F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace online::proto {

namespace {
void tune(intptr_t s) {
    int one = 1;
    setsockopt(decltype(socket(0, 0, 0))(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(int(s), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
}  // namespace

TcpStream::~TcpStream() { close(); }

void TcpStream::close() {
    if (fd_ >= 0) sock_close(fd_);
    fd_ = -1;
    connected_ = false;
    out_.clear();
    in_.clear();
    out_at_ = in_at_ = 0;
}

bool TcpStream::connect(const Address& to, std::string& why) {
    close();
    closed_ = false;
    peer_ = to;
    int fam = reinterpret_cast<const sockaddr*>(to.storage)->sa_family;
    auto s = socket(fam, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        why = "socket() failed";
        return false;
    }
    fd_ = intptr_t(s);
    set_nonblock(fd_);
    tune(fd_);
    int rc = ::connect(s, reinterpret_cast<const sockaddr*>(to.storage), socklen_t(to.len));
    if (rc == 0) {
        connected_ = true;
    } else if (!WOULD_BLOCK) {
        why = "cannot connect";
        close();
        closed_ = true;
        return false;
    }
    return true;
}

void TcpStream::adopt(intptr_t fd, const Address& peer) {
    close();
    closed_ = false;
    fd_ = fd;
    peer_ = peer;
    set_nonblock(fd_);
    tune(fd_);
    connected_ = true;
}

bool TcpStream::send_frame(const uint8_t* d, size_t n) {
    if (closed_ || fd_ < 0 || n > kMaxTcpFrame) return false;
    if (queued() + n + 2 > kTcpMaxQueued) {
        close();
        closed_ = true;
        return false;
    }
    if (out_at_ > 64 * 1024 && out_at_ * 2 > out_.size()) {
        out_.erase(out_.begin(), out_.begin() + long(out_at_));
        out_at_ = 0;
    }
    out_.push_back(uint8_t(n >> 8));
    out_.push_back(uint8_t(n));
    out_.insert(out_.end(), d, d + n);
    return true;
}

void TcpStream::pump() {
    if (fd_ < 0) return;
    auto s = decltype(socket(0, 0, 0))(fd_);
    if (!connected_) {
#ifdef _WIN32
        WSAPOLLFD p{};
        p.fd = s;
        p.events = POLLWRNORM;
        if (WSAPoll(&p, 1, 0) <= 0) return;
#else
        pollfd p{};
        p.fd = s;
        p.events = POLLOUT;
        if (::poll(&p, 1, 0) <= 0) return;
#endif
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        if (err != 0) {
            close();
            closed_ = true;
            return;
        }
        connected_ = true;
    }
    // write
    while (out_at_ < out_.size()) {
        auto k = ::send(s, reinterpret_cast<const char*>(out_.data() + out_at_), int(out_.size() - out_at_), kSendFlags);
        if (k > 0) {
            out_at_ += size_t(k);
            continue;
        }
        if (k < 0 && WOULD_BLOCK) break;
        close();
        closed_ = true;
        return;
    }
    if (out_at_ == out_.size()) {
        out_.clear();
        out_at_ = 0;
    }
    // read
    uint8_t buf[16384];
    for (;;) {
        auto k = ::recv(s, reinterpret_cast<char*>(buf), int(sizeof buf), 0);
        if (k > 0) {
            if (in_.size() - in_at_ + size_t(k) > kTcpMaxQueued) {
                close();
                closed_ = true;
                return;
            }
            in_.insert(in_.end(), buf, buf + k);
            continue;
        }
        if (k < 0 && WOULD_BLOCK) break;
        close();  // 0: the peer closed it; < 0: an error
        closed_ = true;
        return;
    }
}

int TcpStream::recv_frame(uint8_t* buf, size_t cap) {
    if (in_.size() - in_at_ < 2) return -1;
    size_t n = (size_t(in_[in_at_]) << 8) | in_[in_at_ + 1];
    if (n > kMaxTcpFrame || n > cap) {
        close();
        closed_ = true;
        return -1;
    }
    if (in_.size() - in_at_ < 2 + n) return -1;
    std::memcpy(buf, in_.data() + in_at_ + 2, n);
    in_at_ += 2 + n;
    if (in_at_ == in_.size()) {
        in_.clear();
        in_at_ = 0;
    } else if (in_at_ > 64 * 1024) {
        in_.erase(in_.begin(), in_.begin() + long(in_at_));
        in_at_ = 0;
    }
    return int(n);
}

TcpListener::~TcpListener() { close(); }

void TcpListener::close() {
    if (fd_ >= 0) sock_close(fd_);
    fd_ = -1;
}

bool TcpListener::open(const std::string& host, uint16_t port, std::string& why) {
    close();
    Address a;
    if (!resolve(host.empty() ? "127.0.0.1" : host, port, a, why)) return false;
    int fam = reinterpret_cast<const sockaddr*>(a.storage)->sa_family;
    auto s = socket(fam, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        why = "socket() failed";
        return false;
    }
    fd_ = intptr_t(s);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    if (bind(s, reinterpret_cast<const sockaddr*>(a.storage), socklen_t(a.len)) != 0 || listen(s, 128) != 0) {
        why = "cannot listen on TCP " + a.text();
        close();
        return false;
    }
    set_nonblock(fd_);
    return true;
}

bool TcpListener::accept(intptr_t& fd, Address& peer) {
    if (fd_ < 0) return false;
    sockaddr_storage ss{};
    socklen_t n = sizeof ss;
    auto c = ::accept(decltype(socket(0, 0, 0))(fd_), reinterpret_cast<sockaddr*>(&ss), &n);
#ifdef _WIN32
    if (c == INVALID_SOCKET) return false;
#else
    if (c < 0) return false;
#endif
    fd = intptr_t(c);
    peer = Address();
    peer.len = uint8_t(n > sizeof peer.storage ? sizeof peer.storage : n);
    std::memcpy(peer.storage, &ss, peer.len);
    return true;
}

}  // namespace online::proto
