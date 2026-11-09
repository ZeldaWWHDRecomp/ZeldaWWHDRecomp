// The iPhone GamePad (runtime/src/remote_pad/): packet round trips and rejects, the controller mapping, and
// the server over loopback UDP (connect, inputs in order, one phone at a time, reconnect, goodbye, silence).
#include "remote_pad/pairing.h"
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
    assert(!parse_hello(hp.data(), hp.size() - 33));  // cut inside the names
    // a Hello without the phone id and token (none sent) still parses, as never paired
    auto short_hello = parse_hello(hp.data(), hp.size() - 32);
    assert(short_hello && short_hello->id == Bytes16{} && short_hello->token == Bytes16{});
    // sequence numbers wrap
    assert(newer(1, 0) && !newer(0, 0) && !newer(0, 1) && newer(0, 0xFFFFFFFF));
}

static void test_video_packets() {
    // a Hello asks for video in its header flags
    auto hp = encode_hello({5, "Phone", "", true});
    assert(parse_header(hp.data(), hp.size())->flags == kWantsVideo && parse_hello(hp.data(), hp.size())->video);
    assert(!parse_hello(encode_hello({5, "Phone", ""}).data(), encode_hello({5, "Phone", ""}).size())->video);
    // a frame cut into pieces of at most kMaxDatagram bytes, put back together in order
    std::vector<uint8_t> frame(kMaxVideoPayload * 2 + 17);
    for (size_t i = 0; i < frame.size(); i++) frame[i] = uint8_t(i * 7);
    auto pieces = encode_video(9, 1234, true, 777, 854, 480, frame.data(), frame.size());
    assert(pieces.size() == 3);
    std::vector<uint8_t> back;
    for (size_t i = 0; i < pieces.size(); i++) {
        assert(pieces[i].size() <= kMaxDatagram);
        auto h = parse_header(pieces[i].data(), pieces[i].size());
        assert(h && h->type == kVideo && h->session == 9);
        auto v = parse_video(pieces[i].data(), pieces[i].size());
        assert(v && v->frame == 1234 && v->index == i && v->count == 3 && v->key && v->time_ns == 777);
        assert(v->width == 854 && v->height == 480);
        back.insert(back.end(), v->data, v->data + v->size);
    }
    assert(back == frame);
    // an empty frame is one piece; a bad index or count is rejected
    pieces = encode_video(9, 1, false, 0, 2, 2, nullptr, 0);
    assert(pieces.size() == 1 && parse_video(pieces[0].data(), pieces[0].size())->size == 0);
    auto bad = pieces[0];
    bad[kHeaderSize + 4] = 1;  // index 1 of 1
    assert(!parse_video(bad.data(), bad.size()));
    auto k = encode_keyframe_request(3);
    assert(parse_header(k.data(), k.size())->type == kKeyframe);
}

static void test_rumble() {
    // the packet
    auto p = encode_rumble(5, 77, 200);
    assert(parse_header(p.data(), p.size())->type == kRumble);
    auto r = parse_rumble(p.data(), p.size());
    assert(r && r->seq == 77 && r->level == 200);
    assert(!parse_rumble(p.data(), p.size() - 1));
    // when to send
    RumbleSender s;
    assert(s.update(0, 1000) == 0);       // the first update says where it stands
    assert(s.update(0, 1100) == -1);      // still: nothing more (no stop repeats: nothing ran)
    assert(s.update(0.5f, 1200) == 128);  // a change: at once
    assert(s.update(0.5f, 1250) == -1);   // unchanged
    assert(s.update(0.5f, 1300) == 128);  // still running: again after kRefreshMs
    assert(s.update(1.0f, 1304) == -1);   // a change within kMinGapMs waits ...
    assert(s.update(1.0f, 1308) == 255);  // ... until it has passed
    assert(s.update(0, 1320) == 0);       // the stop
    assert(s.update(0, 1340) == -1);
    assert(s.update(0, 1360) == 0);       // and once more, kStopGapMs later
    assert(s.update(0, 1500) == -1);      // then nothing
    assert(s.update(0, 9000) == -1);
    assert(s.update(2.0f, 9100) == 255 && s.update(std::nanf(""), 9200) == 0);  // clamped; NaN = still
    s.reset();
    assert(s.update(0.25f, 10) == 64);
}

static void test_audio_packets() {
    auto hp = encode_hello({5, "Phone", "", true, true});
    assert(parse_header(hp.data(), hp.size())->flags == (kWantsVideo | kWantsAudio));
    auto h = parse_hello(hp.data(), hp.size());
    assert(h->video && h->audio);
    // one game audio frame: 144 stereo frames, little-endian int16 left/right
    int16_t lr[2 * 144];
    for (int i = 0; i < 144; i++) { lr[2 * i] = int16_t(i * 100 - 7000); lr[2 * i + 1] = int16_t(-i); }
    auto p = encode_audio(3, 41, lr, 144);
    assert(p.size() == kAudioHeaderSize + 144 * 4 && p.size() <= kMaxDatagram);
    auto a = parse_audio(p.data(), p.size());
    assert(a && a->seq == 41 && a->frames == 144 && !a->silent);
    for (int i = 0; i < 2 * 144; i++) assert(int16_t(a->data[2 * i] | a->data[2 * i + 1] << 8) == lr[i]);
    // silence travels as its length alone
    int16_t zero[2 * 144] = {};
    p = encode_audio(3, 42, zero, 144);
    assert(p.size() == kAudioHeaderSize);
    a = parse_audio(p.data(), p.size());
    assert(a && a->silent && a->frames == 144 && !a->data);
    assert(encode_audio(3, 43, nullptr, 144).size() == kAudioHeaderSize);
    // a short packet is rejected
    p = encode_audio(3, 44, lr, 144);
    assert(!parse_audio(p.data(), p.size() - 2));
}

static void test_pairing() {
    // a predictable generator: each call's bytes count up
    uint8_t next = 1;
    Pairing pr([&](uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = next++; });
    Key phone{}; phone[0] = 9;
    Key none{};
    assert(!pr.trusted(phone, none) && pr.size() == 0);
    auto r = pr.request(phone, "Test iPhone", 1000);
    assert(r.fresh && r.code < 1'000'000 && r.attempts == Pairing::kAttempts);
    auto again = pr.request(phone, "Test iPhone", 2000);  // asked again: the same code, not shown anew
    assert(!again.fresh && again.code == r.code);
    assert(pr.pending(2000)->name == "Test iPhone" && pr.pending(2000)->code == r.code);
    // wrong codes use up the tries; then the code is gone
    int left = -1;
    Key token{};
    assert(pr.answer(phone, (r.code + 1) % 1'000'000, 3000, &token, &left) == Pairing::kWrong && left == 2);
    assert(pr.answer(phone, (r.code + 2) % 1'000'000, 3000, &token, &left) == Pairing::kWrong && left == 1);
    assert(pr.answer(phone, (r.code + 3) % 1'000'000, 3000, &token, &left) == Pairing::kWrong && left == 0);
    assert(pr.answer(phone, r.code, 3000, &token, &left) == Pairing::kNone && !pr.pending(3000));
    // no new code before the cooldown has passed (guessing must not run at network speed)
    assert(pr.request(phone, "Test iPhone", 3000 + Pairing::kCooldownMs - 1).busy);
    // a new code; the right one pairs, with a token only this phone knows
    uint64_t t = 3000 + Pairing::kCooldownMs;
    r = pr.request(phone, "Test iPhone", t);
    assert(r.fresh && !r.busy);
    Key other{}; other[0] = 7;
    assert(pr.request(other, "Other", t).busy);  // one phone pairs at a time
    assert(pr.answer(other, r.code, t, &token, &left) == Pairing::kNone);  // another phone's code
    assert(pr.answer(phone, r.code, t + 1000, &token, &left) == Pairing::kPaired);
    assert(token != Key{} && pr.trusted(phone, token) && !pr.trusted(phone, none) && !pr.trusted(other, token));
    assert(pr.size() == 1 && pr.names()[0] == "Test iPhone" && !pr.pending(t + 1000));
    // pairing leaves no cooldown; a code that expires does
    t += 1000;
    r = pr.request(other, "Other", t);
    assert(r.fresh);
    t += Pairing::kCodeLifetimeMs;
    assert(pr.answer(other, r.code, t, &token, &left) == Pairing::kNone && pr.request(other, "Other", t).busy);
    // "Don't pair" removes the code, with a cooldown too
    t += Pairing::kCooldownMs;
    r = pr.request(other, "Other", t);
    assert(r.fresh);
    pr.refuse(t);
    assert(!pr.pending(t) && pr.answer(other, r.code, t, &token, &left) == Pairing::kNone);
    assert(pr.request(other, "Other", t).busy);
    t += Pairing::kCooldownMs;
    // saved and loaded: the same phones trusted (a name loses its line breaks)
    Key t2{};
    r = pr.request(other, "Line\nbre;ak", t);
    assert(r.fresh && pr.answer(other, r.code, t, &t2, &left) == Pairing::kPaired);
    const std::string saved = pr.save();
    Pairing loaded;
    assert(saved.find('\n') == std::string::npos);
    loaded.load(saved + ";garbage");
    assert(loaded.size() == 2 && loaded.trusted(phone, token) && loaded.trusted(other, t2));
    assert(loaded.names()[1] == "Linebreak");
    loaded.forget_all();
    assert(loaded.size() == 0 && !loaded.trusted(phone, token));
    // hex
    Key k{}; k[0] = 0xAB; k[15] = 0x01;
    Key back{};
    assert(hex(k) == "ab000000000000000000000000000001" && parse_hex(hex(k), back) && back == k);
    assert(!parse_hex("xyz", back));
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

static void test_server_video() {
    std::atomic<int> keyframes{0};
    Server server({nullptr, nullptr, nullptr, [&] { keyframes++; }}, "Test-Mac");
    assert(server.start(0));
    std::vector<uint8_t> frame(5000, 0xAB);
    assert(!server.send_video(frame.data(), frame.size(), true, 1, 854, 480));  // nobody connected
    // a phone without the video flag gets no video
    {
        Phone phone(server.port());
        phone.send(encode_hello({1, "Input only", ""}));
        auto w = phone.welcome();
        assert(w && !server.snapshot().video);
        assert(!server.send_video(frame.data(), frame.size(), true, 1, 854, 480));
        phone.send(encode_bye(w->first));
        assert(wait_for([&] { return !server.snapshot().connected; }));
    }
    Phone phone(server.port());
    phone.send(encode_hello({2, "Video phone", "", true}));
    auto w = phone.welcome();
    assert(w && server.snapshot().video);
    assert(server.send_video(frame.data(), frame.size(), true, 42, 854, 480));
    assert(server.send_video(frame.data(), 10, false, 43, 854, 480));
    // the pieces arrive in order: frame 1 in 4 pieces (5000 bytes), then frame 2 in one
    std::vector<uint8_t> got;
    uint32_t frames_seen = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t buf[2048];
        const ssize_t n = recv(phone.s, buf, sizeof buf, 0);
        assert(n > 0);
        auto h = parse_header(buf, size_t(n));
        if (h->type == kWelcome) { i--; continue; }  // a keepalive in between
        auto v = parse_video(buf, size_t(n));
        assert(v && h->session == w->first);
        if (v->frame == 1) { assert(v->key && v->count == 4 && v->time_ns == 42); got.insert(got.end(), v->data, v->data + v->size); }
        else { assert(v->frame == 2 && !v->key && v->count == 1 && v->size == 10); frames_seen = 2; }
    }
    assert(got == frame && frames_seen == 2);
    // rumble reaches the phone in the session (video or not)
    assert(server.send_rumble(180));
    for (;;) {
        uint8_t buf[2048];
        const ssize_t n = recv(phone.s, buf, sizeof buf, 0);
        assert(n > 0);
        auto h = parse_header(buf, size_t(n));
        if (h->type != kRumble) continue;
        auto r = parse_rumble(buf, size_t(n));
        assert(r && r->level == 180 && r->seq == 1 && h->session == w->first);
        break;
    }
    // no sound asked for: none sent
    int16_t lr[2 * 144] = {1, 2};
    assert(!server.send_audio(lr, 144));
    // the phone switches the sound on: the same Hello with kWantsAudio keeps the session
    std::atomic<int> flags_changes{0};
    phone.send(encode_hello({2, "Video phone", "", true, true}));
    auto w2 = phone.welcome();
    assert(w2 && w2->first == w->first);
    assert(wait_for([&] { return server.snapshot().audio; }));
    assert(server.send_audio(lr, 144));
    for (;;) {
        uint8_t buf[2048];
        const ssize_t n = recv(phone.s, buf, sizeof buf, 0);
        assert(n > 0);
        if (parse_header(buf, size_t(n))->type != kAudio) continue;
        auto a = parse_audio(buf, size_t(n));
        assert(a && a->frames == 144 && !a->silent && a->seq == 1 && a->data[0] == 1 && a->data[2] == 2);
        break;
    }
    (void)flags_changes;
    // a lost piece: the phone asks for a keyframe
    phone.send(encode_keyframe_request(w->first));
    assert(wait_for([&] { return keyframes == 1; }));
    phone.send(encode_keyframe_request(w->first + 1));  // not its session: ignored
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(keyframes == 1);
    server.stop();
    assert(!server.send_video(frame.data(), frame.size(), true, 1, 854, 480));
    assert(!server.send_rumble(1));
    assert(!server.send_audio(lr, 144));
}

static void test_server_pairing() {
    Pairing pr;
    std::atomic<int> shown{0}, saved{0}, connects{0};
    std::atomic<uint32_t> shown_code{0};
    Server::Callbacks cb;
    cb.connected = [&](const Server::Snapshot&) { connects++; };
    cb.pairing = [&](const std::string&, uint32_t code) { shown_code = code; shown++; };
    cb.paired = [&](const std::string&) { saved++; };
    Server server(cb, "Test-Mac", &pr);
    assert(server.start(0));
    Phone phone(server.port());
    Hello h{11, "New iPhone", ""};
    h.id[3] = 0x55;
    // unknown: a code is shown on the computer, the phone is asked for it, no session
    phone.send(encode_hello(h));
    uint8_t buf[2048];
    ssize_t n = recv(phone.s, buf, sizeof buf, 0);
    auto pairing = parse_pairing(buf, size_t(n));
    assert(pairing && pairing->nonce == 11 && pairing->state == kEnterCode && pairing->attempts == 3);
    assert(shown == 1 && !server.snapshot().connected && connects == 0);
    phone.send(encode_hello(h));  // Hello again: the same code, not shown again
    n = recv(phone.s, buf, sizeof buf, 0);
    assert(parse_pairing(buf, size_t(n)) && shown == 1);
    // a wrong code
    phone.send(encode_pair({11, h.id, (shown_code + 1) % 1'000'000}));
    n = recv(phone.s, buf, sizeof buf, 0);
    pairing = parse_pairing(buf, size_t(n));
    assert(pairing && pairing->state == kWrongCode && pairing->attempts == 2);
    // the right one: a token, and the computer saves the list
    phone.send(encode_pair({11, h.id, shown_code}));
    n = recv(phone.s, buf, sizeof buf, 0);
    auto paired = parse_paired(buf, size_t(n));
    assert(paired && paired->nonce == 11 && saved == 1 && pr.size() == 1);
    // with the token it connects
    h.token = paired->token;
    phone.send(encode_hello(h));
    auto w = phone.welcome();
    assert(w && server.snapshot().connected && connects == 1);
    phone.send(encode_bye(w->first));
    assert(wait_for([&] { return !server.snapshot().connected; }));
    // a copied id with another token is not let in
    Hello impostor = h;
    impostor.token[0] ^= 1;
    impostor.nonce = 12;
    phone.send(encode_hello(impostor));
    n = recv(phone.s, buf, sizeof buf, 0);
    assert(parse_pairing(buf, size_t(n)) && !server.snapshot().connected);
    // a code typed with nothing pending
    pr.refuse(0);
    phone.send(encode_pair({12, h.id, shown_code}));
    n = recv(phone.s, buf, sizeof buf, 0);
    pairing = parse_pairing(buf, size_t(n));
    assert(pairing && pairing->state == kNoCode);
    server.stop();
}

int main() {
    test_packets();
    test_video_packets();
    test_rumble();
    test_audio_packets();
    test_pairing();
    test_pad_values();
    test_server();
    test_server_video();
    test_server_pairing();
    puts("remote_pad_test: packets, controller mapping, loopback server (sessions, order, one phone, reconnect, bye, silence, video, keyframe requests, sound switched on), rumble, audio packets, pairing (codes, tries, expiry, refusal, saved list; over loopback) passed");
}
