// Remote GamePad server (server.h). POSIX sockets; on Windows start() fails.
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

Server::Server(Callbacks cb, std::string host_name, Pairing* pairing)
    : cb_(std::move(cb)), host_(std::move(host_name)), pairing_(pairing) {}

static uint64_t now_ms() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}
Server::~Server() { stop(); }

Server::Snapshot Server::snapshot() const {
    std::lock_guard lk(mu_);
    return state_;
}

#ifdef _WIN32
bool Server::start(uint16_t) { log_msg("[remote pad] not available on this host"); return false; }
bool Server::send_video(const uint8_t*, size_t, bool, uint64_t, uint16_t, uint16_t) { return false; }
bool Server::send_rumble(uint8_t) { return false; }
bool Server::send_audio(const int16_t*, uint16_t) { return false; }
void Server::set_text(const TextPrompt&) {}
void Server::set_boot_image(std::vector<uint8_t>) {}
void Server::send_text_locked() {}
void Server::stop() {}
void Server::run() {}
void Server::end_session(const char*) {}
#else
bool Server::start(uint16_t port) {
    if (running_) return true;
    // dual-stack IPv6 (IPv4 phones arrive mapped); plain IPv4 where IPv6 is off
    const bool v6 = (sock_ = socket(AF_INET6, SOCK_DGRAM, 0)) >= 0;
    if (!v6) sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) { log_msg("[remote pad] socket: %s", strerror(errno)); return false; }
    int no = 0, yes = 1;
    if (v6) setsockopt(sock_, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof no);
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    int sndbuf = 4 << 20;  // a keyframe leaves as one burst of pieces (macOS's UDP default is 9 KB)
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    timeval tv{0, 100 * 1000};  // wake up to send keepalives and notice silence
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    sockaddr_storage addr{};
    socklen_t len;
    if (v6) {
        auto& a = reinterpret_cast<sockaddr_in6&>(addr);
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_any;
        a.sin6_port = htons(port);
        len = sizeof a;
    } else {
        auto& a = reinterpret_cast<sockaddr_in&>(addr);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(port);
        len = sizeof a;
    }
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        log_msg("[remote pad] port %u: %s", (unsigned)port, strerror(errno));
        close(sock_); sock_ = -1;
        return false;
    }
    getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(v6 ? reinterpret_cast<sockaddr_in6&>(addr).sin6_port : reinterpret_cast<sockaddr_in&>(addr).sin_port);
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

bool Server::send_rumble(uint8_t level) {
    sockaddr_storage to{};
    socklen_t to_len = 0;
    std::vector<uint8_t> p;
    {
        std::lock_guard lk(mu_);
        if (!running_ || !state_.connected) return false;
        memcpy(&to, peer_, peer_len_);
        to_len = peer_len_;
        p = encode_rumble(state_.session, ++rumble_seq_, level);
    }
    std::lock_guard send(send_mu_);
    return sock_ >= 0 && sendto(sock_, p.data(), p.size(), 0, reinterpret_cast<const sockaddr*>(&to), to_len) >= 0;
}

bool Server::send_audio(const int16_t* lr, uint16_t frames) {
    sockaddr_storage to{};
    socklen_t to_len = 0;
    uint32_t session = 0, seq = 0;
    {
        std::lock_guard lk(mu_);
        if (!running_ || !state_.connected || !state_.audio) return false;
        memcpy(&to, peer_, peer_len_);
        to_len = peer_len_;
        session = state_.session;
        seq = ++audio_seq_;
    }
    const auto p = encode_audio(session, seq, lr, frames);
    std::lock_guard send(send_mu_);
    return sock_ >= 0 && sendto(sock_, p.data(), p.size(), 0, reinterpret_cast<const sockaddr*>(&to), to_len) >= 0;
}

void Server::send_text_locked() {
    text_sent_ = Clock::now();
    if (!state_.connected || (!text_.open && text_closes_ <= 0)) return;
    if (!text_.open) text_closes_--;
    const auto p = encode_text(state_.session, text_);
    std::lock_guard send(send_mu_);
    if (sock_ >= 0) sendto(sock_, p.data(), p.size(), 0, reinterpret_cast<const sockaddr*>(peer_), peer_len_);
}

void Server::set_boot_image(std::vector<uint8_t> jpeg) {
    std::lock_guard lk(mu_);
    boot_image_id_ = jpeg.empty() ? 0 : boot_image_id(jpeg.data(), jpeg.size());
    boot_image_ = std::move(jpeg);
    boot_image_sent_ = {};
}

void Server::set_text(const TextPrompt& t) {
    std::lock_guard lk(mu_);
    if (t == text_) return;
    if (text_.open && !t.open) text_closes_ = kTextCloses;
    text_ = t;
    if (running_) send_text_locked();
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
        auto reply_to_sender = [&](const std::vector<uint8_t>& p) {
            sendto(sock_, p.data(), p.size(), 0, reinterpret_cast<const sockaddr*>(&from), from_len);
        };
        if (n > 0) {
            const auto h = parse_header(buf, (size_t)n);
            if (h && h->type == kHello) {
                const auto hello = parse_hello(buf, (size_t)n);
                const bool free = !state_.connected || same_peer || now - seen_ > kTimeout;
                if (hello && free && pairing_ && !pairing_->trusted(hello->id, hello->token) && pairing_->awaiting_saved()) {
                    // the saved paired phones are not read yet (pairing.h await_saved): no answer for now
                } else if (hello && free && pairing_ && !pairing_->trusted(hello->id, hello->token)) {
                    // not paired: the computer shows a code for it, the phone asks the player for it
                    const auto req = pairing_->request(hello->id, hello->device.empty() ? "iPhone" : hello->device, now_ms());
                    if (req.fresh) {
                        log_msg("[remote pad] %s (%s) asks to pair: code shown on this computer", hello->device.c_str(),
                                address_text(from).c_str());
                        if (cb_.pairing) cb_.pairing(hello->device, req.code);
                    }
                    reply_to_sender(req.busy ? encode_pairing({hello->nonce, kNoCode, 0})
                                             : encode_pairing({hello->nonce, kEnterCode, uint8_t(req.attempts)}));
                } else if (hello && free) {
                    bool fresh = false;
                    // a repeated Hello (its Welcome was lost) keeps the session; a new one starts afresh
                    if (!same_peer || hello->nonce != hello_nonce_) {
                        if (state_.connected) end_session(same_peer ? "it reconnected" : "another phone took the GamePad");
                        state_.connected = true;
                        state_.session = new_session();
                        state_.device = hello->device.empty() ? "iPhone" : hello->device;
                        state_.controller = hello->controller;
                        state_.video = hello->video;
                        state_.audio = hello->audio;
                        state_.address = address_text(from);
                        memcpy(peer_, &from, from_len);
                        peer_len_ = from_len;
                        hello_nonce_ = hello->nonce;
                        log_msg("[remote pad] %s (%s) connected%s%s%s", state_.device.c_str(), state_.address.c_str(),
                                state_.controller.empty() ? "" : " with ", state_.controller.c_str(),
                                state_.video ? ", showing the GamePad screen" : "");
                        if (state_.audio) log_msg("[remote pad] the GamePad speaker plays on the phone");
                        if (cb_.connected) cb_.connected(state_);
                        fresh = true;
                    } else if (hello->video != state_.video || hello->audio != state_.audio) {
                        // the same session; the phone switched the picture or the sound on or off
                        state_.video = hello->video;
                        state_.audio = hello->audio;
                        log_msg("[remote pad] %s: GamePad screen %s, GamePad speaker %s", state_.device.c_str(),
                                state_.video ? "on" : "off", state_.audio ? "on the phone" : "here");
                        if (cb_.flags) cb_.flags(state_);
                    }
                    seen_ = keepalive_ = now;
                    reply(encode_welcome(state_.session, {hello->nonce, host_}));
                    if (fresh && text_.open) send_text_locked();  // a prompt already showing
                }
            } else if (h && h->type == kPair && pairing_) {
                if (auto req = parse_pair(buf, (size_t)n)) {
                    Key token{};
                    int left = 0;
                    switch (pairing_->answer(req->id, req->code, now_ms(), &token, &left)) {
                    case Pairing::kPaired: {
                        const auto names = pairing_->names();
                        log_msg("[remote pad] %s paired", names.empty() ? "a phone" : names.back().c_str());
                        reply_to_sender(encode_paired({req->nonce, token}));
                        if (cb_.paired) cb_.paired(names.empty() ? std::string() : names.back());
                        break;
                    }
                    case Pairing::kWrong:
                        log_msg("[remote pad] wrong pairing code (%d tries left)", left);
                        reply_to_sender(encode_pairing({req->nonce, uint8_t(left > 0 ? kWrongCode : kNoCode), uint8_t(left)}));
                        break;
                    case Pairing::kNone:
                        reply_to_sender(encode_pairing({req->nonce, kNoCode, 0}));
                        break;
                    }
                }
            } else if (h && same_peer && h->session == state_.session) {
                if (h->type == kInput) {
                    if (auto in = parse_input(buf, (size_t)n); in && (state_.inputs == 0 || newer(in->seq, state_.input.seq))) {
                        state_.input = *in;
                        state_.inputs++;
                        seen_ = now;
                        if (cb_.input) cb_.input(*in);
                    }
                } else if (h->type == kPing) {
                    if (const auto t = parse_ping(buf, (size_t)n)) {
                        seen_ = now;
                        reply(encode_pong(state_.session, {*t, clock_ns()}));
                    }
                } else if (h->type == kTextInput) {
                    if (const auto t = parse_text_input(buf, (size_t)n)) {
                        seen_ = now;
                        if (cb_.text_input) cb_.text_input(*t);
                    }
                } else if (h->type == kBootImageWant) {
                    if (const auto want = parse_boot_image_want(buf, (size_t)n)) {
                        seen_ = now;
                        if (boot_image_id_ && *want != boot_image_id_ &&
                            now - boot_image_sent_ >= std::chrono::milliseconds(kBootImageGapMs)) {
                            boot_image_sent_ = now;
                            for (const auto& p : encode_boot_image(state_.session, boot_image_id_, boot_image_.data(), boot_image_.size()))
                                reply(p);
                        }
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
        if ((text_.open && now - text_sent_ >= kTextRepeat) || (!text_.open && text_closes_ > 0 && now - text_sent_ >= kTextCloseGap))
            send_text_locked();
        if (state_.connected && now - keepalive_ >= std::chrono::seconds(1)) {
            keepalive_ = now;
            reply(encode_welcome(state_.session, {hello_nonce_, host_}));
        }
    }
}
#endif

}  // namespace remote_pad
