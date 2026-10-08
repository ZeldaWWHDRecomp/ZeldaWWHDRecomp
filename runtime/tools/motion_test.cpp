// Gyro input (runtime/src/motion/): how far the player turned under each axis mode (player space, yaw,
// roll) for the SDL and Cemuhook sources and the usual holds, bias estimation, the virtual GamePad and how
// WWHD's first-person camera reads it (frame-to-frame matrix change), sensitivity and invert, the mouse as a
// gyro, device switching and stuck sensor timestamps, the Cemuhook (DSU) packets and a client/server
// exchange over loopback UDP, and the settings.
#include "motion/dsu.h"
#include "motion/fusion.h"
#include "motion/motion.h"

#include <atomic>
#include <cassert>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _WIN32
// (windows.h defines near and far as empty macros, min and max as macros)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
namespace mods { double game_time() { return 0; } }

// explicit names (no using-directive): nothing from the system headers can make them ambiguous
using motion::Aim;
using motion::Fusion;
using motion::Quat;
using motion::Settings;
using motion::Tuning;
using motion::Vec3;
using motion::VirtualPad;
using motion::VpadMotion;
using motion::controller_gone;
using motion::controller_sample;
using motion::drives_gamepad;
using motion::from_dsu;
using motion::from_kv;
using motion::from_sdl;
using motion::kCemuhook;
using motion::kController;
using motion::kMouse;
using motion::kOff;
using motion::kPlayerSpace;
using motion::kRollAxis;
using motion::kYawAxis;
using motion::mouse_drives_gyro;
using motion::mouse_motion;
using motion::poll_recalibrate;
using motion::recalibrate;
using motion::right_stick;
using motion::set_aiming;
using motion::set_settings;
using motion::status;
using motion::to_ini;
using motion::upgrade_from_first_release;
using motion::vpad;
using motion::wants_controller_sensors;
static constexpr float kPi = 3.14159265358979f;
static constexpr float kDeg = kPi / 180;
static bool close_to(float a, float b, float tol = 0.03f) { return std::fabs(a - b) < tol; }
static bool close_to(Vec3 a, Vec3 b, float tol = 0.03f) { return close_to(a.x, b.x, tol) && close_to(a.y, b.y, tol) && close_to(a.z, b.z, tol); }
static void print(const char* what, Vec3 v) { fprintf(stderr, "  %s = (%.3f, %.3f, %.3f)\n", what, v.x, v.y, v.z); }

// a physical motion of a controller, as SDL (or a Cemuhook server) reports it: rotation rate (rad/s, SDL
// frame) and gravity following the pose. The pose starts flat on a table (SDL y up).
struct Controller {
    Quat pose;  // SDL body -> world (world = SDL frame of the flat controller: x right, y up, z towards player)
    Fusion f;
    int axis = kPlayerSpace;
    bool dsu = false;
    void step(Vec3 rate_sdl, float dt) {
        pose = pose * Quat::axis_angle(rate_sdl, rate_sdl.length() * dt);
        pose.normalize();
        Vec3 up_body = pose.unrotate({0, 1, 0});  // specific force at rest: up, 1 g
        float g[3] = {rate_sdl.x, rate_sdl.y, rate_sdl.z};
        float a[3] = {up_body.x * 9.80665f, up_body.y * 9.80665f, up_body.z * 9.80665f};
        Vec3 gh, ah;
        if (dsu) {
            // DS4 conventions as DSU servers send them: deg/s, gyro (x, -y, -z), acceleration -a / g
            float gd[3] = {g[0] * 180 / kPi, -g[1] * 180 / kPi, -g[2] * 180 / kPi};
            float ad[3] = {-up_body.x, -up_body.y, -up_body.z};
            from_dsu(gd, ad, gh, ah);
        } else {
            from_sdl(g, a, gh, ah);
        }
        f.update(dt, gh, ah, axis);
    }
    // turn about an SDL body axis by `deg` degrees in `secs` seconds at 250 Hz
    void turn(Vec3 axis_sdl, float deg, float secs = 0.5f) {
        int n = (int)(secs * 250);
        float rate = deg * kDeg / secs;
        for (int i = 0; i < n; i++) step(axis_sdl * rate, 1.0f / 250);
    }
    // turn about a world axis (the world frame of `pose`)
    void turn_world(Vec3 axis_world, float deg, float secs = 0.5f) { turn(pose.unrotate(axis_world), deg, secs); }
    void rest(float secs) { for (int i = 0; i < (int)(secs * 250); i++) step({}, 1.0f / 250); }
};
// physical motions: right turn = clockwise seen from above (about world -y), roll right = right side down
// (about the controller's -z, which points at the player), tilt up = top up (about its +x)
static const Vec3 kWorldDown{0, -1, 0};
static const Vec3 kRollRight{0, 0, -1}, kTiltUp{1, 0, 0};

// WWHD's reading (dCamera_c::CalcSubjectAngle via 02618604): R = C^T M with the dir vectors as the
// matrix columns, C = the previous frame's matrix; yaw input = (R[2][0] - R[1][0]) * 30, pitch = R[2][1] * 30
struct Game {
    Vec3 prev[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    bool first = true;
    float yaw = 0, pitch = 0;  // sums of the inputs
    void frame(const VpadMotion& m) {
        if (!first) {
            auto R = [&](int i, int j) { return prev[i].dot(m.dir[j]); };
            yaw += (R(2, 0) - R(1, 0)) * 30;
            pitch += R(2, 1) * 30;
        }
        first = false;
        for (int i = 0; i < 3; i++) prev[i] = m.dir[i];
    }
};
// the game's input for a turn of `deg` degrees (radians x 30; sin for a turn within one frame)
static float game_units(float deg) { return deg * kDeg * 30; }
static float game_units_one_frame(float deg) { return std::sin(deg * kDeg) * 30; }

static void test_aim_conventions() {
    // how far the player turned, under each axis mode, for the usual holds; tilting up is always "up"
    struct Case { const char* what; int axis; float tilt; Vec3 motion; bool world; float yaw, pitch; };
    const Case cases[] = {
        {"player space, flat, turn right", kPlayerSpace, 0, kWorldDown, true, 30, 0},
        {"player space, held at 45 degrees, turn right", kPlayerSpace, 45, kWorldDown, true, 30, 0},
        {"player space, upright, turn right", kPlayerSpace, 90, kWorldDown, true, 30, 0},
        {"player space, flat, roll right", kPlayerSpace, 0, kRollRight, false, 0, 0},
        {"player space, flat, tilt up", kPlayerSpace, 0, kTiltUp, false, 0, 30},
        {"player space, held at 45 degrees, tilt up", kPlayerSpace, 45, kTiltUp, false, 0, 30},
        {"yaw, flat, turn right", kYawAxis, 0, kWorldDown, true, 30, 0},
        {"yaw, flat, roll right", kYawAxis, 0, kRollRight, false, 0, 0},
        {"yaw, flat, tilt up", kYawAxis, 0, kTiltUp, false, 0, 30},
        {"roll, flat, roll right", kRollAxis, 0, kRollRight, false, 30, 0},
        {"roll, flat, turn right", kRollAxis, 0, kWorldDown, true, 0, 0},
        {"roll, flat, tilt up", kRollAxis, 0, kTiltUp, false, 0, 30},
    };
    for (bool dsu : {false, true})
        for (const Case& c : cases) {
            Controller k;
            k.axis = c.axis;
            k.dsu = dsu;
            k.rest(0.1f);
            if (c.tilt) k.turn(kTiltUp, c.tilt);
            k.rest(1.0f);  // the gravity estimate settles
            k.f.take();
            if (c.world) k.turn_world(c.motion, 30);
            else k.turn(c.motion, 30);
            Aim a = k.f.take();
            fprintf(stderr, "  %s%s: yaw %.1f, pitch %.1f\n", c.what, dsu ? " (Cemuhook)" : "", a.yaw / kDeg, a.pitch / kDeg);
            assert(close_to(a.yaw / kDeg, c.yaw, 1.5f) && close_to(a.pitch / kDeg, c.pitch, 1.5f));
        }
    {
        // player space follows a change of hold right away: tilt the controller while turning
        Controller k;
        k.rest(0.5f);
        k.f.take();
        for (int i = 0; i < 10; i++) {
            k.turn(kTiltUp, 9, 0.1f);
            k.turn_world(kWorldDown, 3, 0.1f);
        }
        Aim a = k.f.take();
        fprintf(stderr, "  player space, tilting up 90 while turning right 30: yaw %.1f, pitch %.1f\n", a.yaw / kDeg, a.pitch / kDeg);
        assert(close_to(a.yaw / kDeg, 30, 2.5f) && close_to(a.pitch / kDeg, 90, 3.0f));
    }
}

static float g_up_pitch = 0;  // the game's pitch input for the GamePad tilted up 30 degrees
static void test_game_reading() {
    // the virtual GamePad turns about its own axes: the game reads exactly the turn, in any pose
    VirtualPad p;
    Game g;
    g.frame(p.vpad());
    for (int i = 0; i < 30; i++) { p.turn(1.0f / 30, {1 * kDeg, 0}); g.frame(p.vpad()); }
    fprintf(stderr, "  game yaw input for 30 degrees right: %.3f (pitch %.3f)\n", g.yaw, g.pitch);
    // right is positive: in the game the camera angle U then decreases, a turn to the right (in-game test)
    assert(close_to(g.yaw, game_units(30), 0.05f) && close_to(g.pitch, 0, 0.01f));
    g.yaw = 0;
    for (int i = 0; i < 30; i++) { p.turn(1.0f / 30, {0, 1 * kDeg}); g.frame(p.vpad()); }
    fprintf(stderr, "  game pitch input for 30 degrees up: %.3f\n", g.pitch);
    assert(close_to(std::fabs(g.pitch), game_units(30), 0.05f) && close_to(g.yaw, 0, 0.01f));
    g_up_pitch = g.pitch;
    // after looking up (and any number of turns), a turn right is still a pure turn right
    for (int i = 0; i < 2000; i++) { p.turn(1.0f / 30, {7 * kDeg, (i % 7 - 3) * kDeg}); g.frame(p.vpad()); }
    g.yaw = g.pitch = 0;
    for (int i = 0; i < 30; i++) { p.turn(1.0f / 30, {-1 * kDeg, 0}); g.frame(p.vpad()); }
    fprintf(stderr, "  after 2000 frames of turning, 30 degrees left: yaw %.3f, pitch %.3f\n", g.yaw, g.pitch);
    assert(close_to(g.yaw, -game_units(30), 0.05f) && close_to(g.pitch, 0, 0.01f));
    // VPAD gyro: the mean rate since the previous read, in revolutions per second
    p.turn(0.5f, {90 * kDeg, 0});
    VpadMotion m = p.vpad();
    print("gyro after 90 degrees right in 0.5 s", m.gyro);
    assert(close_to(m.gyro.length(), 0.5f, 0.01f));
    assert(close_to(m.acc_magnitude, 1, 0.01f));
    // tuning: sensitivity and invert per axis
    Tuning t;
    assert(t.sensitivity_x == 0.5f && t.sensitivity_y == 0.5f && t.axis == kPlayerSpace);
    t.sensitivity_x = 2;
    t.invert_y = true;
    Aim a = t.apply({1, 1});
    assert(a.yaw == 2.0f && a.pitch == -0.5f);
}

static void test_bias_and_noise() {
    // a controller at rest with a gyro offset (Switch Pro: up to 5 deg/s): after resting the offset is
    // learnt, the aim does not drift
    Controller c;
    Vec3 bias_sdl{0.09f, -0.06f, 0.02f};
    Aim last_second;
    for (int i = 0; i < 250 * 4; i++) {
        float gy[3] = {bias_sdl.x, bias_sdl.y, bias_sdl.z}, a[3] = {0, 9.80665f, 0};
        Vec3 gh, ah;
        from_sdl(gy, a, gh, ah);
        c.f.update(1.0f / 250, gh, ah, kPlayerSpace);
        if (i == 250 * 3) c.f.take();
    }
    last_second = c.f.take();
    print("bias", c.f.bias());
    assert(c.f.calibrated());
    assert(close_to(c.f.bias(), {bias_sdl.x, -bias_sdl.y, -bias_sdl.z}, 0.005f));
    fprintf(stderr, "  drift over the last second: yaw %.4f pitch %.4f degrees\n", last_second.yaw / kDeg, last_second.pitch / kDeg);
    assert(std::fabs(last_second.yaw) < 0.05f * kDeg && std::fabs(last_second.pitch) < 0.05f * kDeg);
    // slow aiming after calibration does not move the bias
    Vec3 b = c.f.bias();
    c.turn_world(kWorldDown, 20, 2.0f);  // 10 deg/s
    assert(close_to(c.f.bias(), b, 0.002f));
    // recalibrate: the next rest replaces the bias
    c.f.recalibrate();
    assert(!c.f.calibrated());
    c.rest(1.0f);
    assert(c.f.calibrated());
    // a broken sample (NaN) is dropped: nothing poisoned
    float nan = std::nanf("");
    c.f.update(1.0f / 250, {nan, 0, 0}, {0, 1, 0}, kPlayerSpace);
    c.f.take();
    c.turn_world(kWorldDown, 10, 0.2f);
    Aim a = c.f.take();
    assert(std::isfinite(c.f.bias().x) && close_to(a.yaw / kDeg, 10, 1.0f));
}

static void test_dsu_packets() {
    assert(dsu::crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u);
    auto info = dsu::encode_port_info_request(7, {0});
    assert(info.size() == 25 && !memcmp(info.data(), "DSUC", 4) && info[4] == 0xE9 && info[5] == 0x03 && info[6] == 9);
    auto req = dsu::encode_pad_data_request(7, 2);
    assert(req.size() == 28 && req[16] == 0x02 && req[18] == 0x10 && req[20] == 1 && req[21] == 2);
    // a pad data packet as DS4Windows sends it (built here with the same layout)
    dsu::PadData d;
    d.slot = 1; d.state = 2; d.model = 2; d.connected = true; d.packet = 1234; d.timestamp_us = 0x0123456789ull;
    d.accel[0] = 0.01f; d.accel[1] = -0.98f; d.accel[2] = 0.12f;
    d.gyro[0] = 1.5f; d.gyro[1] = -90.0f; d.gyro[2] = 0.25f;
    auto pkt = dsu::encode_pad_data(99, d);
    assert(pkt.size() == dsu::kPadDataSize && !memcmp(pkt.data(), "DSUS", 4));
    assert(pkt[6] == 84 && pkt[7] == 0);  // length after the header
    // the fixed offsets of the protocol: packet counter at 32, timestamp at 68, accel at 76, gyro at 88
    assert(pkt[32] == (1234 & 0xFF) && pkt[33] == (1234 >> 8) && pkt[68] == 0x89 && pkt[72] == 0x01);
    float gy;
    memcpy(&gy, &pkt[92], 4);
    assert(gy == -90.0f);
    dsu::PadData out;
    assert(dsu::parse_pad_data(pkt.data(), pkt.size(), out));
    assert(out.slot == 1 && out.connected && out.packet == 1234 && out.timestamp_us == 0x0123456789ull);
    assert(out.accel[1] == -0.98f && out.gyro[1] == -90.0f && out.gyro[2] == 0.25f);
    // corrupted, truncated, client packets and other messages are refused
    auto bad = pkt;
    bad[80] ^= 1;
    assert(!dsu::parse_pad_data(bad.data(), bad.size(), out));
    assert(!dsu::parse_pad_data(pkt.data(), 60, out));
    assert(!dsu::parse_pad_data(req.data(), req.size(), out));
    assert(dsu::message_type(info.data(), info.size()) == 0);  // DSUC: not a server message
    bad = pkt;
    bad[16] = 0x01;  // port info type, CRC redone: a valid message but not pad data
    bad[8] = bad[9] = bad[10] = bad[11] = 0;
    uint32_t crc = dsu::crc32(bad.data(), bad.size());
    for (int i = 0; i < 4; i++) bad[8 + i] = (uint8_t)(crc >> (8 * i));
    assert(dsu::message_type(bad.data(), bad.size()) == dsu::kPortInfo && !dsu::parse_pad_data(bad.data(), bad.size(), out));
    assert(!dsu::parse_pad_data(nullptr, 0, out));
}

static void test_dsu_loopback() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    using sock = SOCKET;
#else
    using sock = int;
#endif
    // a tiny DSU server on a free loopback port: answers each pad data request with a sample
    sock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    assert(bind(s, (sockaddr*)&a, sizeof a) == 0);
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    const uint16_t port = ntohs(a.sin_port);
#ifdef _WIN32
    DWORD tv = 100;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
    timeval tv{0, 100000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    std::atomic<int> got{0};
    dsu::PadData last;
    std::mutex mu;
    dsu::Client client([&](const dsu::PadData& d) { std::lock_guard lk(mu); last = d; got++; });
    client.start("127.0.0.1", port, 0);
    int requests = 0;
    uint32_t n = 0;
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (got < 3 && std::chrono::steady_clock::now() < until) {
        uint8_t buf[256];
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        int r = (int)recvfrom(s, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (r == 28 && !memcmp(buf, "DSUC", 4) && buf[16] == 0x02) {
            requests++;
            for (int i = 0; i < 3; i++) {  // a burst, as a 250 Hz server sends between requests
                dsu::PadData d;
                d.slot = 0; d.state = 2; d.model = 2; d.connected = true; d.packet = ++n; d.timestamp_us = 1000 + 4000 * n;
                d.gyro[1] = 45;
                d.accel[1] = -1;
                auto p = dsu::encode_pad_data(5, d);
                sendto(s, (const char*)p.data(), (int)p.size(), 0, (sockaddr*)&from, fl);
            }
        }
    }
    assert(requests >= 1 && got >= 3);
    {
        std::lock_guard lk(mu);
        assert(last.gyro[1] == 45 && last.packet >= 3);
    }
    assert(client.receiving());
    auto t0 = std::chrono::steady_clock::now();
    client.stop();
    assert(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));  // never hangs
    assert(!client.running());
    // a server that is not there: the client keeps asking, stop() still returns quickly
    client.start("127.0.0.1", 9, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(!client.receiving());
    t0 = std::chrono::steady_clock::now();
    client.stop();
    assert(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}


static void test_settings_and_sources() {
    Settings s;
    assert(s.source == kOff);  // off by default
    assert(!drives_gamepad());
    s.source = kCemuhook;
    s.tuning.sensitivity_x = 1.5f;
    s.tuning.invert_y = true;
    s.tuning.axis = kRollAxis;
    s.dsu_host = "192.168.1.20";
    s.dsu_port = 26761;
    s.dsu_slot = 2;
    s.recenter_pad = 13;
    s.recenter_key = 15;
    s.mouse_degrees = 0.25f;
    std::string ini = to_ini(s);
    assert(ini.find("gyro.axis=roll\n") != std::string::npos);
    Settings r;
    size_t at = 0;
    while (at < ini.size()) {
        size_t nl = ini.find('\n', at), eq = ini.find('=', at);
        from_kv(r, ini.substr(at, eq - at), ini.substr(eq + 1, nl - eq - 1));
        at = nl + 1;
    }
    assert(r == s);
    from_kv(r, "gyro.sensitivityX", "99");
    from_kv(r, "gyro.sensitivityY", "0.001");
    from_kv(r, "gyro.dsuPort", "junk");
    from_kv(r, "gyro.source", "nonsense");
    from_kv(r, "gyro.axis", "nonsense");
    assert(r.tuning.sensitivity_x == 5.0f && r.tuning.sensitivity_y == 0.05f && r.dsu_port == 26760 && r.source == kCemuhook && r.tuning.axis == kRollAxis);
    // settings of the first release: its default 1.0 becomes the new default, a value the player chose stays
    Settings old;
    from_kv(old, "gyro.sensitivityX", "1");
    from_kv(old, "gyro.sensitivityY", "0.4");
    upgrade_from_first_release(old);
    assert(old.tuning.sensitivity_x == Tuning::kDefaultSensitivity && old.tuning.sensitivity_y == 0.4f);

    // the mouse source: movement counts only while the game aims; 100 points x 0.1 degrees x sensitivity 1
    Settings m;
    m.source = kMouse;
    m.mouse_degrees = 0.1f;
    m.tuning.sensitivity_x = m.tuning.sensitivity_y = 1;
    set_settings(m);
    assert(drives_gamepad());
    Game g;
    g.frame(vpad(false));
    mouse_motion(100, 0);
    g.frame(vpad(false));
    assert(close_to(g.yaw, 0, 1e-4f));  // not aiming: ignored
    assert(!mouse_drives_gyro());
    set_aiming(true);
    assert(mouse_drives_gyro());
    mouse_motion(100, 0);  // 10 degrees right
    g.frame(vpad(false));
    fprintf(stderr, "  mouse 100 points right: game yaw %.3f\n", g.yaw);
    assert(close_to(g.yaw, game_units_one_frame(10), 0.01f));
    {  // a stall (a slow frame, a busy CI machine) keeps the whole angle: the mouse gives angles, not rates
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        mouse_motion(0, -100);  // up
        g.frame(vpad(false));
        fprintf(stderr, "  mouse 100 points up after a 150 ms stall: game pitch %.3f\n", g.pitch);
        assert(close_to(std::fabs(g.pitch), game_units_one_frame(10), 0.01f) && g.pitch * g_up_pitch > 0);
    }
    VpadMotion a = vpad(false), b = vpad(true);  // a repeated read repeats
    assert(close_to(b.dir[0], a.dir[0], 1e-6f));
    // sensitivity and invert
    m.tuning.sensitivity_x = 0.5f;
    m.tuning.invert_x = true;
    set_settings(m);
    g = {};
    g.frame(vpad(false));
    mouse_motion(100, 0);
    g.frame(vpad(false));
    assert(close_to(g.yaw, -game_units_one_frame(5), 0.01f));
    set_aiming(false);
    // the right stick keeps the game from using the gyro: the status says so while aiming
    right_stick(0.5f, 0);
    assert(status().find("right stick") == std::string::npos);  // not aiming
    set_aiming(true);
    right_stick(0.5f, 0);
    fprintf(stderr, "  %s\n", status().c_str());
    assert(status().find("right stick") != std::string::npos);
    right_stick(0.05f, 0);
    assert(status().find("right stick") == std::string::npos);
    set_aiming(false);

    // the controller source: samples from SDL; sensitivity 0.5 (the default)
    Settings c;
    c.source = kController;
    set_settings(c);
    assert(wants_controller_sensors() && drives_gamepad());
    float right90[3] = {0, -kPi / 2, 0}, still[3] = {0, 0, 0}, acc[3] = {0, 9.80665f, 0};  // 90 deg/s right
    uint64_t t = 1000000000ull;
    g = {};
    g.frame(vpad(false));
    // 100 samples, 99 steps of 4 ms: 35.64 degrees, 17.82 with sensitivity 0.5
    for (int i = 0; i < 100; i++) controller_sample(42, t += 4000000, right90, acc);
    g.frame(vpad(false));
    fprintf(stderr, "  controller 35.6 degrees right: game yaw %.3f\n", g.yaw);
    assert(close_to(g.yaw, game_units_one_frame(17.82f), 0.02f));
    // a second controller that rests does not take over; one that turns does once the first one rests
    for (int i = 0; i < 100; i++) {
        controller_sample(42, t += 4000000, right90, acc);
        controller_sample(43, t, still, acc);
    }
    g.frame(vpad(false));
    assert(close_to(g.yaw, game_units_one_frame(17.82f) + game_units_one_frame(18), 0.03f));
    float left90[3] = {0, kPi / 2, 0};
    controller_sample(42, t += 4000000, still, acc);  // controller 42 now rests (for over half a second) ...
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    for (int i = 0; i < 150; i++) {  // ... while 43 turns left
        controller_sample(42, t += 4000000, still, acc);
        controller_sample(43, t, left90, acc);
    }
    g.frame(vpad(false));
    fprintf(stderr, "  after the second controller turned left: game yaw %.3f\n", g.yaw);
    assert(g.yaw < game_units_one_frame(17.82f) + game_units_one_frame(18) - game_units(5));
    // disconnecting: the virtual GamePad stays where it is (the game would see a jump)
    float before = g.yaw;
    controller_gone(43);
    controller_gone(42);
    g.frame(vpad(false));
    assert(close_to(g.yaw, before, 1e-4f) && close_to(g.pitch, 0, 1e-3f));
    // a sensor whose timestamps do not advance still turns (arrival times)
    for (int i = 0; i < 60; i++) {
        controller_sample(44, 5000, right90, acc);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    g.frame(vpad(false));
    fprintf(stderr, "  stuck timestamps, 0.24 s at 90 deg/s: game yaw %.3f more\n", g.yaw - before);
    assert(g.yaw - before > game_units(5));
    // recalibrate binding: rising edge only; the controller calibrates again
    c.recenter_key = 15;
    set_settings(c);
    for (int i = 0; i < 400; i++) controller_sample(44, t += 4000000, still, acc);
    assert(status() == "Receiving motion.");
    bool keys[256] = {};
    keys[15] = true;
    poll_recalibrate(nullptr, keys);
    assert(status().find("calibrating") != std::string::npos);
    for (int i = 0; i < 400; i++) controller_sample(44, t += 4000000, still, acc);
    assert(status() == "Receiving motion.");
    poll_recalibrate(nullptr, keys);  // still held: nothing
    assert(status() == "Receiving motion.");
    // the Cemuhook source starts and stops its client (no server on port 9: it keeps asking)
    Settings d;
    d.source = kCemuhook;
    d.dsu_port = 9;
    set_settings(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    fprintf(stderr, "  %s\n", status().c_str());
    assert(status().rfind("Cemuhook: ", 0) == 0);
    d.dsu_port = 10;
    set_settings(d);  // restarts on the new port
    before = g.yaw;
    g.frame(vpad(false));
    assert(close_to(g.yaw, before, 1e-4f));
    set_settings(Settings{});
    assert(status().rfind("Gyro off", 0) == 0);
    assert(!wants_controller_sensors() && !drives_gamepad());
    for (int i = 0; i < 10; i++) controller_sample(42, t += 4000000, right90, acc);
    g.frame(vpad(false));
    assert(close_to(g.yaw, before, 1e-4f));  // off: ignored
    recalibrate();
}

int main() {
    test_aim_conventions();
    test_game_reading();
    test_bias_and_noise();
    test_dsu_packets();
    test_dsu_loopback();
    test_settings_and_sources();
    puts("motion_test: aim conventions (player space, yaw, roll; SDL and Cemuhook), game reading, bias, sensitivity/invert, "
         "mouse, device switching, timestamps, DSU packets and loopback, settings passed");
}
