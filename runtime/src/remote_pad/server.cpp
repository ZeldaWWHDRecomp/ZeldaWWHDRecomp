// Remote GamePad server (server.h). POSIX sockets: macOS, Linux and Android; on Windows start() fails
// (the iPhone client is the only one so far, and it pairs with the Mac).
#include "server.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

void log_msg(const char* fmt, ...);

namespace remote_pad {
namespace {
using Clock = std::chrono::steady_clock;

uint32_t new_session() {
    static std::mt19937 rng{std::random_device{}()};
    uint32_t s;
    do s = rng(); while (s == 0);
    return s;
}

#ifndef _WIN32
std::string address_text(const sockaddr_storage& a) {
    char buf[INET6_ADDRSTRLEN] = "?";
    if (a.ss_family == AF_INET6) {
        const auto& v6 = reinterpret_cast<const sockaddr_in6&>(a);
        inet_ntop(AF_INET6, &v6.sin6_addr, buf, sizeof buf);
        std::string s = buf;
        if (s.rfind("::ffff:", 0) == 0) s = s.substr(7);  // an IPv4 phone on the dual-stack socket
        return s;
    }
    if (a.ss_family == AF_INET) inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in&>(a).sin_addr, buf, sizeof buf);
    return buf;
}
#endif
}  // namespace

Server::Server(Callbacks cb, std::string host_name) : cb_(std::move(cb)), host_(std::move(host_name)) {}
Server::~Server() { stop(); }

#ifdef _WIN32
bool Server::start(uint16_t) { log_msg("[remote pad] not available on this host"); return false; }
bool Server::send_video(const uint8_t*, size_t, bool, uint64_t, uint16_t, uint16_t) { return false; }
void Server::stop() {}
void Server::run() {}
void Server::end_session(const char*) {}
#else
bool Server::start(uint16_t port) {
    if (running_) return true;
    sock_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock_ < 0) { log_msg("[remote pad] socket: %s", strerror(errno)); return false; }
    int no = 0, yes = 1;
    setsockopt(sock_, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof no);  // IPv4 phones too
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    int sndbuf = 4 << 20;  // a keyframe leaves as one burst of pieces (macOS's UDP default is 9 KB)
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    timeval tv{0, 100 * 1000};  // wake up to send keepalives and notice silence
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        log_msg("[remote pad] port %u: %s", (unsigned)port, strerror(errno));
        close(sock_); sock_ = -1;
        return false;
    }
    socklen_t len = sizeof addr;
    getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin6_port);
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lk(mu_);
        if (state_.connected) {
            const auto bye = encode_bye(state_.session);
            sendto(sock_, bye.data(), bye.size(), 0, reinterpret_cast<const sockaddr*>(peer_), peer_len_);
            end_session("the game stopped listening");
        }
    }
    std::lock_guard send(send_mu_);
    close(sock_);
    sock_ = -1;
}

void Server::end_session(const char* why) {
    if (!state_.connected) return;
    log_msg("[remote pad] %s disconnected: %s", state_.device.c_str(), why);
    state_ = {};
    peer_len_ = 0;
    if (cb_.disconnected) cb_.disconnected(why);
}

Server::Snapshot Server::snapshot() const {
    std::lock_guard lk(mu_);
    return state_;
}

bool Server::send_video(const uint8_t* data, size_t size, bool key, uint64_t time_ns, uint16_t width, uint16_t height) {
    sockaddr_storage to{};
    socklen_t to_len = 0;
    uint32_t session = 0, frame = 0;
    {
        std::lock_guard lk(mu_);
        if (!running_ || !state_.connected || !state_.video) return false;
        memcpy(&to, peer_, peer_len_);
        to_len = peer_len_;
        session = state_.session;
        frame = ++video_frame_;
    }
    const auto pieces = encode_video(session, frame, key, time_ns, width, height, data, size);
    std::lock_guard send(send_mu_);
    if (sock_ < 0) return false;
    for (const auto& p : pieces)
        if (sendto(sock_, p.data(), p.size(), 0, reinterpret_cast<const sockaddr*>(&to), to_len) < 0 && errno != ENOBUFS)
            return false;
    return true;
}

void Server::run() {
    uint8_t buf[1500];
    while (running_) {
        sockaddr_storage from{};
        socklen_t from_len = sizeof from;
        const ssize_t n = recvfrom(sock_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        const auto now = Clock::now();
        std::lock_guard lk(mu_);
        const bool same_peer = state_.connected && from_len == peer_len_ && memcmp(&from, peer_, from_len) == 0;
        auto reply = [&](const std::vector<uint8_t>& p) {
            sendto(sock_, p.data(), p.size(), 0, reinterpret_cast<const sockaddr*>(peer_), peer_len_);
        };
        if (n > 0) {
            const auto h = parse_header(buf, (size_t)n);
            if (h && h->type == kHello) {
                const auto hello = parse_hello(buf, (size_t)n);
                const bool free = !state_.connected || same_peer || now - seen_ > kTimeout;
                if (hello && free) {
                    // a repeated Hello (its Welcome was lost) keeps the session; a new one starts afresh
                    if (!same_peer || hello->nonce != hello_nonce_) {
                        if (state_.connected) end_session(same_peer ? "it reconnected" : "another phone took the GamePad");
                        state_.connected = true;
                        state_.session = new_session();
                        state_.device = hello->device.empty() ? "iPhone" : hello->device;
                        state_.controller = hello->controller;
                        state_.video = hello->video;
                        state_.address = address_text(from);
                        memcpy(peer_, &from, from_len);
                        peer_len_ = from_len;
                        hello_nonce_ = hello->nonce;
                        log_msg("[remote pad] %s (%s) connected%s%s%s", state_.device.c_str(), state_.address.c_str(),
                                state_.controller.empty() ? "" : " with ", state_.controller.c_str(),
                                state_.video ? ", showing the GamePad screen" : "");
                        if (cb_.connected) cb_.connected(state_);
                    }
                    seen_ = keepalive_ = now;
                    reply(encode_welcome(state_.session, {hello->nonce, host_}));
                }
            } else if (h && same_peer && h->session == state_.session) {
                if (h->type == kInput) {
                    if (auto in = parse_input(buf, (size_t)n); in && (state_.inputs == 0 || newer(in->seq, state_.input.seq))) {
                        state_.input = *in;
                        state_.inputs++;
                        seen_ = now;
                        if (cb_.input) cb_.input(*in);
                    }
                } else if (h->type == kKeyframe) {
                    seen_ = now;
                    if (cb_.keyframe) cb_.keyframe();
                } else if (h->type == kBye) {
                    end_session("it said goodbye");
                }
            }
        }
        if (state_.connected && now - seen_ > kTimeout) end_session("nothing received for 1.5 s");
        if (state_.connected && now - keepalive_ >= std::chrono::seconds(1)) {
            keepalive_ = now;
            reply(encode_welcome(state_.session, {hello_nonce_, host_}));
        }
    }
}
#endif

}  // namespace remote_pad
