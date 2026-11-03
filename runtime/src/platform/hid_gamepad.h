#pragma once
// Which HID element of a generic gamepad drives which host pad input. Kept pure (no IOKit, no
// AppKit) so it can be unit-tested (runtime/tools/hid_gamepad_test.cpp); the macOS AppKit host's
// IOKit reader (gfx/input.mm) is its only user.
//
// IOKit hands back raw HID elements, and which element is which input is a per-device fact: the
// same kind of table SDL keeps in its controller database. Only the recognised Steam software
// device is admitted; a generic joystick's descriptors do not establish an Xbox mapping.
#include <cstdint>
#include <string_view>

namespace hid_gamepad {

// What IOKit reports about a matched HID device, as far as admission needs it.
struct DeviceIdentity {
    bool virtual_device = false;       // "HIDVirtualDevice": a software device (Steam Input's pad)
    uint16_t vendor = 0, product = 0;  // kIOHIDVendorIDKey / kIOHIDProductIDKey
    std::string_view product_name;     // kIOHIDProductKey
    bool gc_synthetic = false;         // "GCSyntheticDevice": made by GameController.framework
    bool gc_supported = false;         // +[GCController supportsHIDDevice:] (macOS 11+)
};

// Steam Input's virtual pad as it identifies itself: a virtual device with the Xbox 360 wired IDs
// (045E:028E) named "GamePad-<n>" (n = 1, 2, ... per virtual pad, renumbered on reconnect);
// "Microsoft GamePad-<n>" is accepted too. The manufacturer string ("Microsoft") is not required.
bool steam_virtual_pad(bool virtual_device, uint16_t vendor, uint16_t product, std::string_view product_name);

// Whether the IOKit reader opens a device, and if not, why. Admission must precede opening or
// reading a device. Steam's virtual pad is admitted before the GameController exclusions:
// GameController claims its Xbox 360 IDs (supportsHIDDevice) but never surfaces it as a
// GCController. Everything else GameController supports or synthesised stays with
// GameController, and unknown physical and virtual devices (joysticks, flight sticks, 3D mice)
// cannot safely use this fixed Xbox mapping, even if their axes/buttons look similar.
enum class Admission : uint8_t { kSteamVirtual, kGameControllerSynthetic, kGameControllerSupported, kUnsupported };
Admission admit(const DeviceIdentity& d);
// The reason logged when a device is ignored (empty for kSteamVirtual).
const char* ignore_reason(Admission a);

// What one HID element feeds. Stick axes are signed (centred at 0), the triggers are 0..1, and a
// hat switch is the D-pad.
enum class Kind : uint8_t {
    kNone, kButton, kLeftX, kLeftY, kRightX, kRightY, kLeftTrigger, kRightTrigger, kHat,
};
// Buttons above 15 (including vendor-specific paddles) are intentionally ignored.
Kind classify(uint32_t usage_page, uint32_t usage);

// Replace only an identified, unique Steam physical/virtual pair after the virtual pad reports
// input. Ambiguous pairs retain their physical sources; unrelated controllers always remain live.
bool replace_steam_source(bool steam_controller, unsigned steam_count, unsigned virtual_count, bool virtual_input);

// How a pad numbers its buttons. The two differ on the stick clicks and Start/Back, and on how the
// D-pad is carried (a hat switch on the standard layout, buttons 12..15 on the Steam virtual pad).
enum class Layout : uint8_t { kStandard, kSteamVirtual };
// Pick the layout from what the pad exposes. has_dpad_buttons: a button usage in 12..15 is present;
// has_hat: a hat switch (usage 0x39) is present. The Steam virtual pad has the first and not the
// second; a standard Xbox 360-style pad has the second.
Layout layout_for(bool has_hat, bool has_dpad_buttons);

// The four D-pad directions one hat switch value means. System hat values run clockwise from up
// (0 = up, 2 = right, 4 = down, 6 = left); a value outside the eight directions is neutral.
// min/max are the element's logical range.
struct Hat { bool up = false, down = false, left = false, right = false; };
Hat hat_directions(long value, long min, long max);

// The host pad position a HID button feeds (usage page 0x09, usage 1..15), or input_map::kPadNone.
int button_pad(Layout layout, uint32_t usage_page, uint32_t usage);

// Normalise a signed stick axis (centred at 0) to -1..1, and a trigger to 0..1, from the element's
// logical range.
float signed_axis(long value, long min, long max);
float trigger_axis(long value, long min, long max);

// This pad's face buttons are Xbox-labelled, so the Automatic face preset may follow them. Only the
// layout we positively recognise is claimed: Steam Input's virtual pad maps an Xbox pad.
bool xbox_labelled(Layout layout);

}  // namespace hid_gamepad
