// Unit tests for the PC-mode GameCube adapter reader (runtime/src/platform/gc_adapter_hid.cpp, issue #556).
//   ninja -C build/mac gc_adapter_hid_test && ./build/mac/gc_adapter_hid_test
// The report fixtures follow SDL release-3.4.18 src/joystick/hidapi/SDL_hidapi_gamecube.c (no hardware).
#include <cmath>
#include <cstdio>
#include <vector>

#include "input.h"
#include "input_map.h"
#include "platform/gc_adapter_hid.h"

using namespace gc_adapter_hid;
using namespace input_map;

static int g_fail = 0, g_pass = 0;
#define CHECK(x)                                                                           \
    do {                                                                                   \
        if (x) g_pass++;                                                                   \
        else { g_fail++; fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #x); }  \
    } while (0)
static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

// a pad's eight bytes: buttons, buttons, main X, main Y, C Y, C X, L, R
struct PadBytes { uint8_t b0 = 0, b1 = 0, lx = 0x80, ly = 0x80, cy = 0x80, cx = 0x80, la = 0x22, ra = 0x22; };
// firmware 0x7 and newer: 9 bytes, no port byte
static std::vector<uint8_t> new_fw(const PadBytes& p) { return {p.b0, p.b1, p.lx, p.ly, p.cy, p.cx, p.la, p.ra, 0}; }
// older firmware: 10 bytes, the port (1..4) first
static std::vector<uint8_t> old_fw(int port, const PadBytes& p) {
    return {(uint8_t)port, p.b0, p.b1, p.lx, p.ly, p.cy, p.cx, p.la, p.ra, 0};
}
static int feed(Adapter& a, const std::vector<uint8_t>& r, bool* changed = nullptr) { return a.feed(r.data(), r.size(), changed); }
static std::vector<float> values(const Adapter& a, int port) {
    std::vector<float> v(kPadCount);
    port_values(a.ports[port], v.data());
    return v;
}

static void test_identity() {
    CHECK(pc_mode_adapter(0x0079, 0x1843));
    CHECK(pc_mode_adapter(0x0079, 0x1844));
    CHECK(pc_mode_adapter(0x0079, 0x1846));  // the #556 reporter's adapter
    CHECK(!pc_mode_adapter(0x057e, 0x0337));  // Nintendo WUP-028: its own protocol
    CHECK(!pc_mode_adapter(0x0079, 0x0006));  // a generic DragonRise pad
    CHECK(!pc_mode_adapter(0x0079, 0x1800));
}

static void test_report_sizes() {
    Raw r;
    const AxisRange range;
    CHECK(parse_report(new_fw({}).data(), 9, range, r) == 0);
    for (int port = 1; port <= 4; port++) CHECK(parse_report(old_fw(port, {}).data(), 10, range, r) == port - 1);
    CHECK(parse_report(old_fw(0, {}).data(), 10, range, r) == -1);  // no port 0
    CHECK(parse_report(old_fw(5, {}).data(), 10, range, r) == -1);
    const uint8_t wup028[37] = {0x21};  // the Nintendo adapter's 37-byte report is not this protocol
    CHECK(parse_report(wup028, sizeof wup028, range, r) == -1);
    CHECK(parse_report(new_fw({}).data(), 8, range, r) == -1);
    CHECK(parse_report(nullptr, 9, range, r) == -1);
}

static void test_buttons() {
    struct Case { uint8_t b0, b1; int pad; };
    // SDL HIDAPI_DriverGameCube_HandleJoystickPacket's READ_BUTTON table, in our pad positions
    const Case cases[] = {
        {0x01, 0, kPadX}, {0x02, 0, kPadA}, {0x04, 0, kPadB}, {0x08, 0, kPadY},
        {0x10, 0, kPadMisc3}, {0x20, 0, kPadMisc4}, {0x80, 0, kPadRB},
        {0, 0x02, kPadMenu}, {0, 0x10, kPadDUp}, {0, 0x20, kPadDRight}, {0, 0x40, kPadDDown}, {0, 0x80, kPadDLeft},
    };
    for (const auto& c : cases) {
        for (int old = 0; old < 2; old++) {
            Adapter a;
            feed(a, old ? old_fw(1, {}) : new_fw({}));  // the resting report calibrates
            PadBytes p; p.b0 = c.b0; p.b1 = c.b1;
            CHECK(feed(a, old ? old_fw(1, p) : new_fw(p)) == 0);
            const auto v = values(a, 0);
            CHECK(v[c.pad] == 1.f);
            int pressed = 0;
            for (int i = kPadA; i < kPadCount; i++) if (v[i] > 0.5f) pressed++;
            // L / R clicks also hold their trigger at full travel
            CHECK(pressed == (c.pad == kPadMisc3 || c.pad == kPadMisc4 ? 2 : 1));
        }
    }
    // the unused bits press nothing
    Adapter a;
    feed(a, new_fw({}));
    PadBytes p; p.b0 = 0x40; p.b1 = 0x0D;
    feed(a, new_fw(p));
    const auto v = values(a, 0);
    for (int i = kPadA; i < kPadCount; i++) CHECK(v[i] == 0.f);
}

static void test_stick_directions() {
    for (int old = 0; old < 2; old++) {
        auto rep = [&](const PadBytes& p) { return old ? old_fw(1, p) : new_fw(p); };
        Adapter a;
        feed(a, rep({}));
        // control stick: higher X = right, higher Y = up, on both firmwares
        PadBytes p; p.lx = 0x80 + 88; p.ly = 0x80 + 88;
        feed(a, rep(p));
        auto v = values(a, 0);
        CHECK(near(v[kPadLSRight], 1.f) && v[kPadLSLeft] == 0.f);
        CHECK(near(v[kPadLSUp], 1.f) && v[kPadLSDown] == 0.f);
        p = {}; p.lx = 0x80 - 88; p.ly = 0x80 - 88;
        feed(a, rep(p));
        v = values(a, 0);
        CHECK(near(v[kPadLSLeft], 1.f) && near(v[kPadLSDown], 1.f));
        // C-stick right and up: the older firmware sends both axes the other way round
        p = {};
        p.cx = old ? 0x80 - 88 : 0x80 + 88;
        p.cy = old ? 0x80 - 88 : 0x80 + 88;
        feed(a, rep(p));
        v = values(a, 0);
        CHECK(near(v[kPadRSRight], 1.f) && v[kPadRSLeft] == 0.f);
        CHECK(near(v[kPadRSUp], 1.f) && v[kPadRSDown] == 0.f);
        p = {};
        p.cx = old ? 0x80 + 70 : 0x80 - 70;
        feed(a, rep(p));
        v = values(a, 0);
        CHECK(v[kPadRSRight] == 0.f && v[kPadRSLeft] > 0.7f);
    }
}

static void test_calibration() {
    // a stick held at plug-in is not taken as the centre
    Adapter a;
    PadBytes held; held.lx = 0xF0;
    bool changed = false;
    feed(a, new_fw(held), &changed);
    CHECK(!a.ports[0].calibrated && !changed);
    CHECK(values(a, 0)[kPadLSRight] == 0.f);
    // the first resting report sets the centre, here a few counts off 128
    PadBytes rest; rest.lx = 0x7C; rest.ly = 0x83;
    feed(a, new_fw(rest), &changed);
    CHECK(a.ports[0].calibrated && changed && a.ports[0].listed(0));
    auto v = values(a, 0);
    CHECK(v[kPadLSLeft] == 0.f && v[kPadLSRight] == 0.f && v[kPadLSUp] == 0.f && v[kPadLSDown] == 0.f);
    // small noise around the centre stays in the inner dead zone
    PadBytes p = rest; p.lx = 0x7C + 2;
    feed(a, new_fw(p));
    CHECK(values(a, 0)[kPadLSRight] == 0.f);
    // half of the nominal 88-count reach is about half deflection (not ~5%, issue #556)
    p = rest; p.lx = 0x7C + 44;
    feed(a, new_fw(p));
    const float half = values(a, 0)[kPadLSRight];
    CHECK(near(half, (0.5f - kStickDeadZone) / (1.f - kStickDeadZone)));
    // full reach is 1.0 and never more
    p = rest; p.lx = 0x7C + 88;
    feed(a, new_fw(p));
    CHECK(near(values(a, 0)[kPadLSRight], 1.f));
    // a stick that goes further widens the range: its own maximum is 1.0, and halfway is about half
    p = rest; p.lx = 0x7C + 110;
    feed(a, new_fw(p));
    CHECK(near(values(a, 0)[kPadLSRight], 1.f));
    p = rest; p.lx = 0x7C + 55;
    feed(a, new_fw(p));
    CHECK(near(values(a, 0)[kPadLSRight], (0.5f - kStickDeadZone) / (1.f - kStickDeadZone)));
    // each direction has its own reach
    p = rest; p.lx = 0x7C - 88;
    feed(a, new_fw(p));
    CHECK(near(values(a, 0)[kPadLSLeft], 1.f));
}

static void test_triggers() {
    Adapter a;
    feed(a, new_fw({}));  // L / R rest at 0x22
    PadBytes p; p.la = 216;
    feed(a, new_fw(p));
    auto v = values(a, 0);
    CHECK(near(v[kPadLT], 1.f) && v[kPadRT] == 0.f && v[kPadMisc3] == 0.f);
    p = {}; p.ra = 0x22 + (216 - 0x22) / 2;
    feed(a, new_fw(p));
    v = values(a, 0);
    CHECK(v[kPadRT] > 0.4f && v[kPadRT] < 0.5f);
    // the click
    p = {}; p.b0 = 0x20; p.ra = 0xC0;
    feed(a, new_fw(p));
    v = values(a, 0);
    CHECK(v[kPadRT] == 1.f && v[kPadMisc4] == 1.f);
    // a lower rest than the first report lowers the rest
    p = {}; p.la = 0x10;
    feed(a, new_fw(p));
    CHECK(values(a, 0)[kPadLT] == 0.f);
}

static void test_ports() {
    Adapter a;
    bool changed = false;
    // port 1 is listed on its first resting report
    CHECK(feed(a, old_fw(1, {}), &changed) == 0 && changed);
    // ports 2..4 report too while nothing is plugged in: listed only once used
    CHECK(feed(a, old_fw(2, {}), &changed) == 1 && !changed);
    CHECK(a.ports[1].calibrated && !a.ports[1].listed(1));
    PadBytes p; p.b0 = 0x02;
    CHECK(feed(a, old_fw(2, p), &changed) == 1 && changed && a.ports[1].listed(1));
    CHECK(values(a, 1)[kPadA] == 1.f && values(a, 0)[kPadA] == 0.f);
    // an all-zero report is an empty port: never a stick held down-left
    CHECK(feed(a, old_fw(3, PadBytes{0, 0, 0, 0, 0, 0, 0, 0}), &changed) == 2 && !changed);
    CHECK(!a.ports[2].calibrated);
    for (float x : values(a, 2)) CHECK(x == 0.f);
    // unplugging port 2 drops it and its calibration
    CHECK(feed(a, old_fw(2, PadBytes{0, 0, 0, 0, 0, 0, 0, 0}), &changed) == 1 && changed);
    CHECK(!a.ports[1].listed(1));
}

static void test_signed_range() {
    CHECK(axis_range(0, 255).min == 0 && axis_range(0, 255).max == 255);
    CHECK(axis_range(-128, 127).min == -128);
    CHECK(axis_range(0, 0).max == 255);       // nonsense: the usual 0..255
    CHECK(axis_range(0, 65535).max == 255);   // not a byte axis
    Adapter a;
    a.range = axis_range(-128, 127);
    feed(a, new_fw(PadBytes{0, 0, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80}));  // centred at 0, triggers at -128
    CHECK(a.ports[0].calibrated);
    feed(a, new_fw(PadBytes{0, 0, 88, (uint8_t)-88, 0x00, 0x00, 0x80, 0x80}));
    const auto v = values(a, 0);
    CHECK(near(v[kPadLSRight], 1.f) && near(v[kPadLSDown], 1.f));
}

// the whole path: an adapter report through the GameCube layout to the game's buttons (docs/gamecube-controller.md)
static void test_gamecube_layout() {
    Mapping m = Mapping::defaults();
    apply_gamecube(m, true);  // the gamecube-apple / legacy PC face table this reader feeds
    Adapter a;
    feed(a, new_fw({}));
    auto game = [&](const PadBytes& p) {
        feed(a, new_fw(p));
        const auto v = values(a, 0);
        return controller_state(m, v.data());
    };
    PadBytes p;
    p = {}; p.b0 = 0x02; CHECK(game(p).buttons == input::kA);  // A: context action
    p = {}; p.b0 = 0x04; CHECK(game(p).buttons == input::kB);  // B: sword
    p = {}; p.b0 = 0x01; CHECK(game(p).buttons == input::kX);  // X: item 1
    p = {}; p.b0 = 0x08; CHECK(game(p).buttons == input::kY);  // Y: item 2
    p = {}; p.b0 = 0x80; CHECK(game(p).buttons == input::kR);  // Z: item 3
    p = {}; p.b0 = 0x10; CHECK(game(p).buttons == input::kZL); // L click: target
    p = {}; p.b0 = 0x20; CHECK(game(p).buttons == input::kZR); // R click: shield
    p = {}; p.la = 0x22 + (216 - 0x22) * 3 / 10; CHECK(game(p).buttons == input::kZL);  // L at 30%
    p = {}; p.la = 0x22 + (216 - 0x22) / 10; CHECK(game(p).buttons == 0);              // L at 10%
    p = {}; p.lx = 0x80 + 88; CHECK(near(game(p).lx, 1.f));
    p = {}; p.cx = 0x80 + 88; CHECK(game(p).rx > 0.9f);  // C-stick right is camera right
    p = {}; p.ly = 0x80 + 88; CHECK(near(game(p).ly, 1.f));  // +y up
}

static void test_shadows() {
    // the #556 log: "GameCube Controller Adapter" (category "HID", physical input profile only)
    CHECK(shadows("GameCube Controller Adapter", "HID", false, "GameCube Controller Adapter"));
    CHECK(shadows("GameCube Controller Adapter", "HID", false, ""));  // no product string from IOKit
    CHECK(shadows("USB GamePad", "HID", false, "USB GamePad"));      // a renamed adapter, by its product string
    CHECK(shadows("GameCube Controller Adapter", "HID", true, "GameCube Controller Adapter"));  // even with a full profile
    CHECK(!shadows("Nintendo GameCube Controller", "Switch", true, "USB GamePad"));  // another, complete pad
    CHECK(!shadows("Xbox Wireless Controller", "Xbox One", false, "GameCube Controller Adapter"));
    CHECK(!shadows("", "", false, ""));
}

static void test_dump_limiter() {
    DumpLimiter d;
    d.min_interval_ms = 50;
    d.max_lines = 3;
    CHECK(d.allow(1000));
    CHECK(!d.allow(1020));
    CHECK(d.allow(1050));
    CHECK(d.allow(2000));
    CHECK(!d.allow(5000) && d.exhausted());
    const uint8_t r[3] = {0x01, 0xAB, 0x00};
    CHECK(hex(r, 3) == "01 AB 00");
    Raw raw;
    raw.a = raw.z = raw.up = true;
    raw.lx = 128;
    CHECK(describe(raw).rfind("A . . . Z . . . | up . . . | main 128,", 0) == 0);
}

int main() {
    test_identity();
    test_report_sizes();
    test_buttons();
    test_stick_directions();
    test_calibration();
    test_triggers();
    test_ports();
    test_signed_range();
    test_gamecube_layout();
    test_shadows();
    test_dump_limiter();
    printf("gc_adapter_hid_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
