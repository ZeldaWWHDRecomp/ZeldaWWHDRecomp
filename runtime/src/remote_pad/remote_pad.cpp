// The remote GamePad in the game (remote_pad.h): the server, its Bonjour advertisement and the motion feed.
#include "remote_pad.h"

#include "pairing.h"
#include "server.h"
#include "../input_map.h"
#include "../motion/motion.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

// g_mu guards g_server itself only: callers copy the pointer and send (or stop and join) outside it,
// so the audio thread never waits for a keyframe burst or a shutdown
std::mutex g_mu;
std::shared_ptr<Server> g_server;
int g_turns = -1;  // the phone's screen rotation of the previous motion sample (server thread)
std::atomic<bool> g_video{false}, g_keyframe{false};
RumbleSender g_rumble;          // the host's input thread
std::atomic<bool> g_rumble_reset{false};  // a new phone: start from still
std::atomic<bool> g_connected{false};
std::atomic<bool> g_audio{false};
Pairing g_pairing;                                 // who may connect (pairing.h)
std::atomic<bool> g_pair_request{false}, g_paired_changed{false};
std::atomic<bool> g_enabled{false}, g_env{false}, g_host{false};
uint16_t g_port = kDefaultPort;                    // g_mu
TextPrompt g_text;                                 // g_mu: the prompt now (a restarted server gets it too)
std::mutex g_text_mu;
std::vector<TextInput> g_text_inputs;              // g_text_mu
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

uint64_t now_ms() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void start_locked(uint16_t port);       // g_mu held
std::shared_ptr<Server> take_locked();  // g_mu held: the server to stop (outside g_mu) with stopped()
void stopped(std::shared_ptr<Server> s);
std::shared_ptr<Server> server() {
    std::lock_guard lk(g_mu);
    return g_server;
}

}  // namespace

void start_from_env() {
    const char* e = getenv("WWHD_REMOTE_PAD");
    if (!e || !*e || !strcmp(e, "0")) return;
    const long want = strtol(e, nullptr, 10);
    std::lock_guard lk(g_mu);
    g_port = want > 1 && want < 65536 ? uint16_t(want) : kDefaultPort;
    g_env = true;
    start_locked(g_port);
}

void set_enabled(bool on) {
    g_enabled = on;
    std::shared_ptr<Server> old;
    {
        std::lock_guard lk(g_mu);
        if (on || g_env) start_locked(g_port);
        else old = take_locked();
    }
    stopped(std::move(old));
}
bool enabled() { return g_enabled || g_env; }
bool env_forced() { return g_env; }
void set_host_supported() { g_host = true; }
bool host_supported() { return g_host; }

Status status() {
    Status st;
    const auto srv = server();
    if (!srv) return st;
    st.listening = true;
    st.port = srv->port();
    const Server::Snapshot s = srv->snapshot();
    st.connected = s.connected;
    st.device = s.device;
    st.controller = s.controller;
    st.video = s.video;
    st.audio = s.audio;
    return st;
}

std::optional<PairingCode> pairing_code() {
    if (auto p = g_pairing.pending(now_ms())) return PairingCode{p->name, p->code};
    return std::nullopt;
}
bool take_pairing_request() { return g_pair_request.exchange(false); }
void refuse_pairing() { g_pairing.refuse(now_ms()); }
size_t paired_phones() { return g_pairing.size(); }
void forget_paired_phones() {
    g_pairing.forget_all();
    g_paired_changed = true;
}
void load_paired(const std::string& saved) { g_pairing.load(saved); }
bool take_paired_changed(std::string& saved) {
    if (!g_paired_changed.exchange(false)) return false;
    saved = g_pairing.save();
    return true;
}

namespace {
void start_locked(uint16_t port) {
    if (g_server) return;
    const std::string name = host_name();
    g_server = std::make_shared<Server>(Server::Callbacks{
        [](const Server::Snapshot& s) { g_turns = -1; g_keyframe = true; g_video = s.video; g_audio = s.audio; g_rumble_reset = true; g_connected = true; },
        on_input,
        [](const std::string&) { motion::remote_reset(); g_turns = -1; g_video = false; g_audio = false; g_connected = false; },
        [] { g_keyframe = true; },
        [](const Server::Snapshot& s) {
            if (s.video && !g_video) g_keyframe = true;  // the picture back on: from a keyframe
            g_video = s.video;
            g_audio = s.audio;
        },
        [](const std::string&, uint32_t) { g_pair_request = true; },  // Settings opens with the code
        [](const std::string&) { g_paired_changed = true; },         // saved by Settings
        [](const TextInput& t) {
            std::lock_guard lk(g_text_mu);
            if (g_text_inputs.size() < 64) g_text_inputs.push_back(t);
        },
    }, name, &g_pairing);
    if (!g_server->start(port)) { g_server.reset(); return; }
    g_server->set_text(g_text);
    log_msg("[remote pad] waiting for the iPhone GamePad on UDP port %u (%s)", (unsigned)g_server->port(),
            g_env ? "WWHD_REMOTE_PAD" : "Settings > Controls > iPhone GamePad");
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

std::shared_ptr<Server> take_locked() {
#ifdef __APPLE__
    if (g_bonjour) { DNSServiceRefDeallocate(g_bonjour); g_bonjour = nullptr; }
#endif
    return std::move(g_server);
}
void stopped(std::shared_ptr<Server> s) {
    if (!s) return;
    s->stop();
    g_connected = g_video = g_audio = false;
    log_msg("[remote pad] stopped listening for the iPhone GamePad");
}
}  // namespace

void stop() {
    std::shared_ptr<Server> old;
    {
        std::lock_guard lk(g_mu);
        old = take_locked();
    }
    stopped(std::move(old));
}

Pad pad() {
    Pad p;
    const auto srv = server();
    if (!srv) return p;
    const Server::Snapshot s = srv->snapshot();
    p.connected = s.connected;
    p.device = s.device;
    p.controller = s.controller;
    p.input = s.input;
    return p;
}

bool video_wanted() { return g_video.load(std::memory_order_relaxed); }
bool take_keyframe_request() { return g_keyframe.exchange(false); }
void send_video(const uint8_t* data, size_t size, bool key, uint64_t time_ns, uint16_t width, uint16_t height) {
    if (const auto srv = server()) srv->send_video(data, size, key, time_ns, width, height);
}

bool audio_wanted() { return g_audio.load(std::memory_order_relaxed); }
void send_audio(const int32_t* left, const int32_t* right, int frames) {
    if (!g_audio.load(std::memory_order_relaxed)) return;
    int16_t lr[2 * kMaxAudioFrames];
    frames = std::clamp(frames, 0, int(kMaxAudioFrames));
    for (int i = 0; i < frames; i++) {
        lr[2 * i] = int16_t(std::clamp<int32_t>(left[i], -32768, 32767));
        lr[2 * i + 1] = int16_t(std::clamp<int32_t>(right[i], -32768, 32767));
    }
    if (const auto srv = server()) srv->send_audio(lr, uint16_t(frames));
}

void text_prompt(const TextPrompt& t) {
    std::shared_ptr<Server> srv;
    {
        std::lock_guard lk(g_mu);
        if (t == g_text) return;
        if (!t.open || t.serial != g_text.serial) {
            std::lock_guard tk(g_text_mu);
            g_text_inputs.clear();  // anything for an earlier prompt
        }
        g_text = t;
        srv = g_server;
    }
    if (srv) srv->set_text(t);
}

std::vector<TextInput> take_text_inputs() {
    std::lock_guard lk(g_text_mu);
    return std::exchange(g_text_inputs, {});
}

int RumbleSender::update(float level, uint64_t now) {
    const int want = std::isfinite(level) ? int(std::lround(std::clamp(level, 0.0f, 1.0f) * 255)) : 0;
    const uint64_t since = now - at_;
    int send = -1;
    if (!any_ || (want != sent_ && since >= kMinGapMs)) send = want;           // a change
    else if (want != 0 && want == sent_ && since >= kRefreshMs) send = want;   // still running
    else if (want == 0 && sent_ == 0 && stops_left_ > 0 && since >= kStopGapMs) {  // the stop again
        stops_left_--;
        send = 0;
    }
    if (send < 0) return -1;
    if (send == 0 && any_ && sent_ != 0) stops_left_ = kStopRepeats - 1;  // (nothing ran yet: one still is enough)
    sent_ = send;
    at_ = now;
    any_ = true;
    return send;
}

void rumble(float level) {
    if (!g_connected.load(std::memory_order_relaxed)) return;  // (cheap: called every input update)
    if (g_rumble_reset.exchange(false)) g_rumble.reset();
    const int send = g_rumble.update(level, now_ms());
    if (send < 0) return;
    const auto srv = server();
    if (!srv || !srv->send_rumble(uint8_t(send))) g_rumble.reset();  // nobody connected: from still next time
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
