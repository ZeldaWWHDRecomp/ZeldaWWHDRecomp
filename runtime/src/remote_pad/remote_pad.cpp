// The remote GamePad in the game (remote_pad.h): the server, its Bonjour advertisement and the motion feed.
#include "remote_pad.h"

#include "server.h"
#include "../input_map.h"
#include "../motion/motion.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <arpa/inet.h>
#include <dns_sd.h>
#endif

void log_msg(const char* fmt, ...);

namespace remote_pad {
namespace {

std::mutex g_mu;
std::unique_ptr<Server> g_server;
int g_turns = -1;  // the phone's screen rotation of the previous motion sample (server thread)
#ifdef __APPLE__
DNSServiceRef g_bonjour = nullptr;
#endif

std::string host_name() {
#ifndef _WIN32
    char buf[256] = {};
    if (gethostname(buf, sizeof buf - 1) == 0 && buf[0]) {
        std::string s = buf;
        if (const size_t dot = s.find('.'); dot != std::string::npos) s.resize(dot);  // "Mac-mini.local" -> "Mac-mini"
        return s;
    }
#endif
    return "Wind Waker HD";
}

void on_input(const Input& in) {
    if (!in.motion) return;
    // a bias/gravity estimate in the old axes cannot be carried into the new ones (as on Android)
    if (in.quarter_turns != g_turns) { motion::remote_reset(); g_turns = in.quarter_turns; }
    motion::remote_sample(in.motion_ns, in.gyro, in.accel, in.quarter_turns);
}

}  // namespace

void start_from_env() {
    const char* e = getenv("WWHD_REMOTE_PAD");
    if (!e || !*e || !strcmp(e, "0")) return;
    const long want = strtol(e, nullptr, 10);
    const uint16_t port = want > 1 && want < 65536 ? uint16_t(want) : kDefaultPort;
    std::lock_guard lk(g_mu);
    if (g_server) return;
    const std::string name = host_name();
    g_server = std::make_unique<Server>(Server::Callbacks{
        [](const Server::Snapshot&) { g_turns = -1; },
        on_input,
        [](const std::string&) { motion::remote_reset(); g_turns = -1; },
    }, name);
    if (!g_server->start(port)) { g_server.reset(); return; }
    log_msg("[remote pad] waiting for the iPhone GamePad on UDP port %u (WWHD_REMOTE_PAD)", (unsigned)g_server->port());
#ifdef __APPLE__
    // Registration needs no reply processing: mDNSResponder keeps the record while the ref is open.
    const DNSServiceErrorType err = DNSServiceRegister(&g_bonjour, 0, 0, name.c_str(), kBonjourType, nullptr, nullptr,
                                                       htons(g_server->port()), 0, nullptr, nullptr, nullptr);
    if (err != kDNSServiceErr_NoError) {
        g_bonjour = nullptr;
        log_msg("[remote pad] Bonjour advertisement failed (%d): enter this computer's address on the phone", (int)err);
    }
#endif
}

void stop() {
    std::lock_guard lk(g_mu);
#ifdef __APPLE__
    if (g_bonjour) { DNSServiceRefDeallocate(g_bonjour); g_bonjour = nullptr; }
#endif
    if (g_server) g_server->stop();
    g_server.reset();
}

Pad pad() {
    std::lock_guard lk(g_mu);
    Pad p;
    if (!g_server) return p;
    const Server::Snapshot s = g_server->snapshot();
    p.connected = s.connected;
    p.device = s.device;
    p.controller = s.controller;
    p.input = s.input;
    return p;
}

void pad_values(const Input& in, float* v) {
    using namespace input_map;
    for (int i = 0; i < kPadCount; i++) v[i] = 0;
    if (!in.controller) return;
    namespace b = remote_pad;  // (input_map has its own kA, kHome, ...)
    static constexpr std::pair<uint32_t, int> kButtons[] = {
        {b::kA, kPadA}, {b::kB, kPadB}, {b::kX, kPadX}, {b::kY, kPadY}, {b::kLB, kPadLB}, {b::kRB, kPadRB},
        {b::kL3, kPadL3}, {b::kR3, kPadR3}, {b::kMenu, kPadMenu}, {b::kOptions, kPadOptions}, {b::kHome, kPadHome},
        {b::kDUp, kPadDUp}, {b::kDDown, kPadDDown}, {b::kDLeft, kPadDLeft}, {b::kDRight, kPadDRight},
        {b::kCapture, kPadCapture},
    };
    for (auto [bit, p] : kButtons) if (in.buttons & bit) v[p] = 1;
    auto stick = [&](float x, float y, int up, int down, int left, int right) {
        v[right] = std::max(x, 0.f); v[left] = std::max(-x, 0.f);
        v[up] = std::max(y, 0.f);    v[down] = std::max(-y, 0.f);
    };
    stick(in.lx, in.ly, kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight);
    stick(in.rx, in.ry, kPadRSUp, kPadRSDown, kPadRSLeft, kPadRSRight);
    v[kPadLT] = in.lt;
    v[kPadRT] = in.rt;
}

}  // namespace remote_pad
