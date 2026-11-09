// The iPhone GamePad (runtime/src/remote_pad/): packet round trips and rejects, the controller mapping, and
// the server over loopback UDP (connect, inputs in order, one phone at a time, reconnect, goodbye, silence).
#include "remote_pad/protocol.h"
#include "remote_pad/remote_pad.h"
#include "remote_pad/server.h"
#include "input_map.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cmath>
#include <optional>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
namespace mods { double game_time() { return 0; } }

using namespace remote_pad;

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void test_packets() {
    Hello h{0x12345678, "Test’s iPhone", "Backbone One"};
    auto p = encode_hello(h);
    assert(parse_header(p.data(), p.size())->type == kHello);
    assert(parse_hello(p.data(), p.size()) == h);
    assert(!parse_welcome(p.data(), p.size()) && !parse_input(p.data(), p.size()));
    // a name over kMaxName bytes is cut, never inside a UTF-8 sequence
    std::string longname(62, 'a');
    longname += "é";  // 2 bytes: 64 in all
    auto cut = parse_hello(encode_hello({1, longname, ""}).data(), encode_hello({1, longname, ""}).size());
    assert(cut && cut->device == std::string(62, 'a'));

    Welcome w{0x12345678, "Test-Mac"};
    p = encode_welcome(77, w);
    assert(parse_header(p.data(), p.size())->session == 77);
    assert(parse_welcome(p.data(), p.size()) == w);

    Input in;
    in.seq = 9; in.time_ns = 123456789012ull; in.buttons = kA | kRB | kDLeft;
    in.controller = true; in.lx = -1; in.ly = 0.5f; in.rx = 0.25f; in.ry = -0.75f; in.lt = 1; in.rt = 0.5f;
    in.touch = true; in.tx = 0.25f; in.ty = 1;
    in.motion = true; in.quarter_turns = 3; in.motion_ns = 99;
    in.gyro[0] = 1.5f; in.gyro[2] = -3; in.accel[1] = 9.80665f;
    p = encode_input(5, in);
    assert(p.size() == kInputSize);
    auto out = parse_input(p.data(), p.size());
    assert(out && out->seq == 9 && out->time_ns == in.time_ns && out->buttons == in.buttons);
    assert(near(out->lx, -1) && near(out->ly, 0.5f) && near(out->rx, 0.25f) && near(out->ry, -0.75f));
    assert(near(out->lt, 1) && near(out->rt, 0.5f) && near(out->tx, 0.25f) && near(out->ty, 1));
    assert(out->controller && out->touch && out->motion && out->quarter_turns == 3 && out->motion_ns == 99);
    assert(out->gyro[0] == 1.5f && out->gyro[2] == -3 && out->accel[1] == 9.80665f);
    // without the flags the values do not count
    in.controller = in.touch = in.motion = false;
    p = encode_input(5, in);
    out = parse_input(p.data(), p.size());
    assert(out && !out->buttons && out->lx == 0 && out->tx == 0 && out->gyro[0] == 0);
    // out-of-range and non-finite values are clamped or dropped
    in.controller = in.motion = true; in.lx = 7; in.lt = -2; in.gyro[1] = std::nanf("");
    p = encode_input(5, in);
    out = parse_input(p.data(), p.size());
    assert(near(out->lx, 1) && out->lt == 0);
    // rejects: short, wrong magic, wrong version, a truncated name
    assert(!parse_input(p.data(), p.size() - 1));
    auto bad = p; bad[0] = 'X';
    assert(!parse_header(bad.data(), bad.size()));
    bad = p; bad[4] = 2;
    assert(!parse_header(bad.data(), bad.size()));
    auto hp = encode_hello(h);
    assert(!parse_hello(hp.data(), hp.size() - 1));
    // sequence numbers wrap
    assert(newer(1, 0) && !newer(0, 0) && !newer(0, 1) && newer(0, 0xFFFFFFFF));
}

static void test_pad_values() {
    using namespace input_map;
    Input in;
    float v[kPadCount];
    in.buttons = remote_pad::kA | remote_pad::kHome | remote_pad::kDDown;
    pad_values(in, v);  // no controller: nothing
    for (float x : v) assert(x == 0);
    in.controller = true; in.lx = -0.5f; in.ry = 1; in.rt = 0.25f;
    pad_values(in, v);
    assert(v[kPadA] == 1 && v[kPadHome] == 1 && v[kPadDDown] == 1 && v[kPadB] == 0);
    assert(v[kPadLSLeft] == 0.5f && v[kPadLSRight] == 0 && v[kPadRSUp] == 1 && v[kPadRSDown] == 0);
    assert(v[kPadRT] == 0.25f && v[kPadLT] == 0);
}

// a phone on the loopback interface
struct Phone {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    explicit Phone(uint16_t port) {
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sockaddr_in me{};
        me.sin_family = AF_INET;
        me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(s, reinterpret_cast<sockaddr*>(&me), sizeof me);
        timeval tv{2, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    ~Phone() { close(s); }
    void send(const std::vector<uint8_t>& p) { sendto(s, p.data(), p.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to); }
    std::optional<std::pair<uint32_t, Welcome>> welcome() {
        uint8_t buf[512];
        const ssize_t n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) return std::nullopt;
        auto h = parse_header(buf, size_t(n));
        auto w = parse_welcome(buf, size_t(n));
        if (!h || !w) return std::nullopt;
        return std::pair{h->session, *w};
    }
    bool silent(int ms) {  // no Welcome within ms
        timeval tv{0, ms * 1000};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        uint8_t buf[512];
        const bool none = recv(s, buf, sizeof buf, 0) <= 0;
        tv = {2, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        return none;
    }
};

template <class F> static bool wait_for(F f) {
    for (int i = 0; i < 300; i++) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void test_server() {
    std::atomic<int> connects{0}, inputs{0}, disconnects{0};
    Server server({[&](const Server::Snapshot&) { connects++; }, [&](const Input&) { inputs++; },
                   [&](const std::string&) { disconnects++; }},
                  "Test-Mac");
    assert(server.start(0) && server.port() != 0);
    Phone phone(server.port());
    phone.send(encode_hello({42, "Test iPhone", "Backbone One"}));
    auto w = phone.welcome();
    assert(w && w->first != 0 && w->second.nonce == 42 && w->second.host == "Test-Mac");
    const uint32_t session = w->first;
    auto snap = server.snapshot();
    assert(snap.connected && snap.device == "Test iPhone" && snap.controller == "Backbone One" && snap.address == "127.0.0.1");
    assert(connects == 1);
    // a repeated Hello (its Welcome was lost) keeps the session
    phone.send(encode_hello({42, "Test iPhone", "Backbone One"}));
    w = phone.welcome();
    assert(w && w->first == session && connects == 1);

    Input in;
    in.controller = true;
    in.seq = 10; in.buttons = kB;
    phone.send(encode_input(session, in));
    assert(wait_for([&] { return inputs == 1; }));
    in.seq = 9; in.buttons = kX;  // older: dropped
    phone.send(encode_input(session, in));
    in.seq = 11; in.buttons = kY;
    phone.send(encode_input(session, in));
    in.seq = 12; in.buttons = kA;  // wrong session: dropped
    phone.send(encode_input(session + 1, in));
    assert(wait_for([&] { return inputs == 2; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(inputs == 2 && server.snapshot().input.buttons == kY && server.snapshot().inputs == 2);

    // a second phone cannot take the GamePad while the first one sends
    {
        Phone other(server.port());
        other.send(encode_hello({7, "Other iPhone", ""}));
        assert(other.silent(300));
        assert(server.snapshot().device == "Test iPhone");
    }
    // the same phone with a new Hello (the app restarted): a new session
    phone.send(encode_hello({43, "Test iPhone", ""}));
    w = phone.welcome();
    assert(w && w->first != session && connects == 2 && disconnects == 1);
    const uint32_t session2 = w->first;
    // the session's first input is taken whatever its number
    in.seq = 1;
    phone.send(encode_input(session2, in));
    assert(wait_for([&] { return inputs == 3; }));
    // keepalive Welcomes about once a second
    assert(phone.welcome());
    // goodbye
    phone.send(encode_bye(session2));
    assert(wait_for([&] { return disconnects == 2 && !server.snapshot().connected; }));
    // silence ends a session
    phone.send(encode_hello({44, "Test iPhone", ""}));
    w = phone.welcome();
    assert(w && server.snapshot().connected);
    assert(wait_for([&] { return disconnects == 3; }));
    assert(!server.snapshot().connected);
    server.stop();
    assert(!server.running());
}

int main() {
    test_packets();
    test_pad_values();
    test_server();
    puts("remote_pad_test: packets, controller mapping, loopback server (sessions, order, one phone, reconnect, bye, silence) passed");
}
