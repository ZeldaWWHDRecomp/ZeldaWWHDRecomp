#pragma once
// PC-mode GameCube controller adapters (DragonRise / EVORETRO / Mayflash-style, USB 0079:1843,
// 0079:1844, 0079:1846) read from their raw HID input reports (issue #556). Pure (no IOKit, no
// AppKit) so the report layout, the axis directions and the calibration are unit-tested
// (runtime/tools/gc_adapter_hid_test.cpp); the macOS AppKit host's IOKit reader (gfx/input.mm) is
// its user.
//
// macOS hands these adapters to GameController.framework as a bare "HID" controller with generic
// element names, which put GameCube X on "Button A", read the C-stick backwards and the sticks over
// the wrong range. The layout here is SDL's (release-3.4.18, src/joystick/hidapi/
// SDL_hidapi_gamecube.c, HIDAPI_DriverGameCube_HandleJoystickPacket / _UpdateDevice):
//
//   Older firmware: 10-byte report, byte 0 = the port (1..4, the HID report ID), bytes 1..8 the pad;
//     its C-stick runs the other way round from the control stick on both axes.
//   Firmware 0x7 and newer: 9-byte report without a port byte, bytes 0..7 the pad (one port); its
//     C-stick runs the same way as the control stick.
//
//   pad byte 0: 0x01 X, 0x02 A, 0x04 B, 0x08 Y, 0x10 L (digital), 0x20 R (digital), 0x80 Z
//   pad byte 1: 0x02 Start, 0x10 D-pad up, 0x20 right, 0x40 down, 0x80 left
//   pad byte 2: control stick X (higher = right)   byte 3: control stick Y (higher = up)
//   pad byte 4: C-stick Y                           byte 5: C-stick X
//   pad byte 6: L analog                            byte 7: R analog
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gc_adapter_hid {

constexpr int kPorts = 4;

// The adapter's USB identity (SDL usb_ids.h: USB_VENDOR_DRAGONRISE, USB_PRODUCT_EVORETRO_GAMECUBE_ADAPTER1..3).
bool pc_mode_adapter(uint16_t vendor, uint16_t product);

// The byte encoding of the axes, from the HID descriptor's logical range of the stick elements
// (usage page 1, X/Y/Z/Rx/Ry/Rz). The usual descriptor is 0..255; a signed one (-128..127) is
// read with its sign. Anything else falls back to 0..255.
struct AxisRange { int min = 0, max = 255; };
AxisRange axis_range(long logical_min, long logical_max);

// One pad's report bytes, decoded. Axes are normalised to "higher = right / up" on both sticks
// (the older firmware's reversed C-stick is turned round here) and remain in the logical range.
struct Raw {
    bool a = false, b = false, x = false, y = false, z = false, l = false, r = false, start = false;
    bool up = false, down = false, left = false, right = false;
    int lx = 0, ly = 0, cx = 0, cy = 0, la = 0, ra = 0;
    bool blank = false;  // all eight pad bytes zero: nothing plugged into the port
};
// A report as IOKit delivers it (the report ID byte included when the descriptor numbers its
// reports). Returns the port (0..3), or -1 for a report that is not a pad report.
int parse_report(const uint8_t* data, size_t len, const AxisRange& range, Raw& out);

// One calibrated stick or trigger. The centre is the value at the first resting report; the full
// deflection starts at SDL's nominal reach (88 of 128 counts each way) and widens to the furthest
// value seen, so every stick reaches 1.0 and none goes past it.
struct StickAxis {
    float centre = 128, neg = 88, pos = 88;
    void reset(const AxisRange& r, int value);
    float read(int value);  // -1..1 after the inner dead zone
};
struct TriggerAxis {
    float rest = 0, full = 216;
    void reset(const AxisRange& r, int value);
    float read(int value);  // 0..1 after the inner dead zone
};
constexpr float kStickDeadZone = 0.04f;    // inner noise only; the mapping's own dead zone follows
constexpr float kTriggerDeadZone = 0.06f;

// One adapter port: calibrated on its first resting report, listed once it is plugged in.
struct Port {
    bool calibrated = false;  // a resting report was seen
    bool used = false;        // a button or a deflection beyond half was seen (ports 2..4 list from then)
    Raw raw;
    StickAxis lx, ly, cx, cy;
    TriggerAxis la, ra;
    float vlx = 0, vly = 0, vcx = 0, vcy = 0, vla = 0, vra = 0;  // the latest calibrated values
    // Feed a decoded report; true when the port's listed state changed (it appeared or went away).
    bool feed(const Raw& r, const AxisRange& range, int index);
    bool listed(int index) const { return calibrated && (index == 0 || used); }
};

struct Adapter {
    AxisRange range;
    Port ports[kPorts];
    int last_port = -1;
    // Feed one report; returns the port it was for (-1: not a pad report). changed: a port appeared or went away.
    int feed(const uint8_t* data, size_t len, bool* changed = nullptr);
};

// The port's host pad values (input_map::Pad positions, 0..1, kPadCount floats), in the GameCube
// layout of the gamecube-apple / legacy PC table: GameCube A, B, X, Y on the A, B, X, Y inputs, Z on
// the right shoulder, L / R analog on the triggers, their digital clicks on Misc3 / Misc4 (the
// GameCube layout presses L / R at 25% travel or the click), Start on Menu.
void port_values(const Port& p, float* values);

// The GCController macOS makes of an adapter this reader has open (issue #556: a bare "HID" pad
// without the extended gamepad profile, named after the adapter's product string), which the host
// leaves out so the adapter is not read twice. GameController exposes no USB IDs, so the name
// decides: the adapter's product string, else (with no extended profile) a GameCube name.
bool shadows(std::string_view gc_vendor_name, std::string_view gc_category, bool extended_profile,
             std::string_view adapter_product);

// WWHD_HID_DUMP: one raw-value line per change at most every min_interval_ms, max_lines in all, so
// the log stays readable when a reporter sends it.
struct DumpLimiter {
    uint64_t min_interval_ms = 50;
    unsigned max_lines = 4000;
    uint64_t last_ms = 0;
    unsigned lines = 0;
    bool started = false;
    bool allow(uint64_t now_ms);
    bool exhausted() const { return lines >= max_lines; }
};
std::string hex(const uint8_t* data, size_t len);
std::string describe(const Raw& r);  // "A . . . Z . . . | up . . . | main 128,128 C 128,128 L/R 30,30"

}  // namespace gc_adapter_hid
