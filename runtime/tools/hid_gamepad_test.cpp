// Unit tests for the generic HID gamepad mapping (runtime/src/platform/hid_gamepad.cpp).
//   make -C build/cmake hid_gamepad_test && ./build/cmake/hid_gamepad_test
#include <cstdio>

#include "input_map.h"
#include "platform/hid_gamepad.h"

using hid_gamepad::Kind;
using hid_gamepad::Layout;
using input_map::Pad;

static int g_fail = 0, g_pass = 0;
#define CHECK(x)                                                                           \
    do {                                                                                   \
        if (x) g_pass++;                                                                   \
        else { g_fail++; fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #x); }  \
    } while (0)

static void test_admission() {
    using hid_gamepad::supported_device;
    CHECK(supported_device(true, "Microsoft GamePad-1"));
    // Physical devices remain excluded even with the same product string or Xbox-like axes.
    CHECK(!supported_device(false, "Microsoft GamePad-1"));
    CHECK(!supported_device(false, "Generic USB Joystick"));
    CHECK(!supported_device(false, "Flight Stick"));
    CHECK(!supported_device(false, "DirectInput Gamepad"));
    CHECK(!supported_device(true, "Generic USB Joystick"));
    CHECK(!supported_device(true, ""));
    CHECK(!supported_device(true, "Microsoft GamePad-1 extra"));
}

static void test_classify() {
    CHECK(hid_gamepad::classify(0x09, 1) == Kind::kButton);
    CHECK(hid_gamepad::classify(0x09, 15) == Kind::kButton);
    CHECK(hid_gamepad::classify(0x09, 0) == Kind::kNone);
    CHECK(hid_gamepad::classify(0x09, 16) == Kind::kNone);
    CHECK(hid_gamepad::classify(0x01, 0x30) == Kind::kLeftX);
    CHECK(hid_gamepad::classify(0x01, 0x31) == Kind::kLeftY);
    CHECK(hid_gamepad::classify(0x01, 0x33) == Kind::kRightX);
    CHECK(hid_gamepad::classify(0x01, 0x34) == Kind::kRightY);
    CHECK(hid_gamepad::classify(0x01, 0x32) == Kind::kLeftTrigger);
    CHECK(hid_gamepad::classify(0x01, 0x35) == Kind::kRightTrigger);
    CHECK(hid_gamepad::classify(0x01, 0x39) == Kind::kHat);
    CHECK(hid_gamepad::classify(0x01, 0x38) == Kind::kNone);
    CHECK(hid_gamepad::classify(0xFF00, 0x01) == Kind::kNone);
}

static void test_layout() {
    // a D-pad made of buttons 12..15, with no hat: Steam Input's virtual pad
    CHECK(hid_gamepad::layout_for(/*has_hat=*/false, /*has_dpad_buttons=*/true) == Layout::kSteamVirtual);
    // a hat switch: the standard layout, whether or not buttons 12..15 also exist
    CHECK(hid_gamepad::layout_for(true, false) == Layout::kStandard);
    CHECK(hid_gamepad::layout_for(true, true) == Layout::kStandard);
    CHECK(hid_gamepad::layout_for(false, false) == Layout::kStandard);
}

static void test_hat() {
    const auto at = [](long v) { return hid_gamepad::hat_directions(v, 0, 7); };
    CHECK(at(0).up && !at(0).right && !at(0).down && !at(0).left);
    CHECK(at(1).up && at(1).right);
    CHECK(at(2).right);
    CHECK(at(4).down);
    CHECK(at(6).left);
    CHECK(at(7).up && at(7).left);
    CHECK(!at(8).up && !at(8).down && !at(8).left && !at(8).right);  // null
    CHECK(!at(-1).up && !at(-1).down);                               // out of range
    // a 0..8 hat (eight directions plus null) still works
    CHECK(hid_gamepad::hat_directions(1, 1, 8).up);
    CHECK(!hid_gamepad::hat_directions(1, 0, 3).up);  // fewer than eight values: not a hat
}

static void test_axes() {
    CHECK(hid_gamepad::signed_axis(-32768, -32768, 32767) == -1.f);
    CHECK(hid_gamepad::signed_axis(0, -32768, 32767) == 0.f);
    CHECK(hid_gamepad::signed_axis(32767, -32768, 32767) == 1.f);
    CHECK(hid_gamepad::signed_axis(-255, -255, 255) == -1.f);
    CHECK(hid_gamepad::signed_axis(0, 0, 254) == -1.f);
    CHECK(hid_gamepad::signed_axis(127, 0, 254) == 0.f);
    CHECK(hid_gamepad::signed_axis(254, 0, 254) == 1.f);
    CHECK(hid_gamepad::signed_axis(100, 7, 7) == 0.f);
    CHECK(hid_gamepad::trigger_axis(0, 0, 255) == 0.f);
    CHECK(hid_gamepad::trigger_axis(255, 0, 255) == 1.f);
    CHECK(hid_gamepad::trigger_axis(128, 0, 255) > 0.49f && hid_gamepad::trigger_axis(128, 0, 255) < 0.51f);
    CHECK(hid_gamepad::trigger_axis(500, 0, 255) == 1.f);  // clamped
}

static void test_buttons() {
    const Layout std = Layout::kStandard, steam = Layout::kSteamVirtual;
    // the standard Xbox 360 layout
    CHECK(hid_gamepad::button_pad(std, 0x09, 1) == Pad::kPadA);
    CHECK(hid_gamepad::button_pad(std, 0x09, 7) == Pad::kPadOptions);
    CHECK(hid_gamepad::button_pad(std, 0x09, 8) == Pad::kPadMenu);
    CHECK(hid_gamepad::button_pad(std, 0x09, 9) == Pad::kPadL3);
    CHECK(hid_gamepad::button_pad(std, 0x09, 10) == Pad::kPadR3);
    CHECK(hid_gamepad::button_pad(std, 0x09, 11) == Pad::kPadHome);
    CHECK(hid_gamepad::button_pad(std, 0x09, 12) == Pad::kPadNone);  // D-pad is a hat
    // Steam Input's virtual pad moves the stick clicks to 7/8 and Start/Back to 9/10, and its
    // D-pad is buttons 12..15
    CHECK(hid_gamepad::button_pad(steam, 0x09, 7) == Pad::kPadL3);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 8) == Pad::kPadR3);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 9) == Pad::kPadMenu);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 10) == Pad::kPadOptions);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 12) == Pad::kPadDUp);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 13) == Pad::kPadDDown);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 14) == Pad::kPadDLeft);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 15) == Pad::kPadDRight);
    // buttons only come from usage page 0x09
    CHECK(hid_gamepad::button_pad(steam, 0x01, 1) == Pad::kPadNone);
    CHECK(hid_gamepad::button_pad(steam, 0x09, 0) == Pad::kPadNone);
    // only the positively recognised layout is claimed as Xbox-labelled
    CHECK(hid_gamepad::xbox_labelled(Layout::kSteamVirtual));
    CHECK(!hid_gamepad::xbox_labelled(Layout::kStandard));
}

static void test_source_selection() {
    using hid_gamepad::replace_steam_source;
    CHECK(replace_steam_source(true, 1, 1, true));
    CHECK(!replace_steam_source(false, 1, 1, true)); // another DualSense/Xbox remains live
    CHECK(!replace_steam_source(true, 1, 1, false)); // idle or reconnected virtual device
    CHECK(!replace_steam_source(true, 1, 0, true)); // virtual device disconnected
    CHECK(!replace_steam_source(true, 2, 1, true)); // cannot identify which physical pad
    CHECK(!replace_steam_source(true, 1, 2, true)); // cannot identify which virtual pad
}

int main() {
    test_admission();
    test_source_selection();
    test_classify();
    test_layout();
    test_hat();
    test_axes();
    test_buttons();
    printf("hid_gamepad: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
