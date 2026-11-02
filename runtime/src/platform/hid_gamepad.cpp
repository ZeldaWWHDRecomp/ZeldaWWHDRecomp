#include "platform/hid_gamepad.h"
#include "input_map.h"

#include <algorithm>

namespace hid_gamepad {

using input_map::Pad;

bool supported_device(bool virtual_device, std::string_view product_name) {
    return virtual_device && product_name == "Microsoft GamePad-1";
}

bool replace_steam_source(bool steam_controller, unsigned steam_count, unsigned virtual_count, bool virtual_input) {
    return steam_controller && steam_count == 1 && virtual_count == 1 && virtual_input;
}

Kind classify(uint32_t page, uint32_t usage) {
    if (page == 0x09 && usage >= 1 && usage <= 15) return Kind::kButton;
    if (page != 0x01) return Kind::kNone;
    switch (usage) {
        case 0x30: return Kind::kLeftX;
        case 0x31: return Kind::kLeftY;
        case 0x33: return Kind::kRightX;
        case 0x34: return Kind::kRightY;
        case 0x32: return Kind::kLeftTrigger;   // Z: the left trigger on the pads we read
        case 0x35: return Kind::kRightTrigger;  // Rz: the right trigger
        case 0x39: return Kind::kHat;
        default: return Kind::kNone;
    }
}

Layout layout_for(bool has_hat, bool has_dpad_buttons) {
    return (has_dpad_buttons && !has_hat) ? Layout::kSteamVirtual : Layout::kStandard;
}

Hat hat_directions(long value, long min, long max) {
    Hat h;
    if (max - min + 1 < 8) return h;      // not an eight-way hat
    const long i = value - min;
    if (i < 0 || i > 7) return h;         // neutral / null
    h.up = (i == 7 || i <= 1);
    h.right = (i >= 1 && i <= 3);
    h.down = (i >= 3 && i <= 5);
    h.left = (i >= 5 && i <= 7);
    return h;
}

float signed_axis(long value, long min, long max) {
    if (max <= min) return 0.f;
    // Signed descriptors conventionally use zero; unsigned descriptors use the midpoint.
    const double centre = min < 0 ? 0.0 : (double(min) + double(max)) * 0.5;
    const double span = value < centre ? centre - double(min) : double(max) - centre;
    return span > 0 ? float(std::clamp((double(value) - centre) / span, -1.0, 1.0)) : 0.f;
}

float trigger_axis(long value, long min, long max) {
    if (max <= min) return 0.f;
    return std::clamp((float)(value - min) / (float)(max - min), 0.f, 1.f);
}

// The standard Xbox 360 HID layout: the D-pad is a hat switch, so usages 12..15 are unused.
static int standard_button(uint32_t usage) {
    switch (usage) {
        case 1: return Pad::kPadA;
        case 2: return Pad::kPadB;
        case 3: return Pad::kPadX;
        case 4: return Pad::kPadY;
        case 5: return Pad::kPadLB;
        case 6: return Pad::kPadRB;
        case 7: return Pad::kPadOptions;  // Back
        case 8: return Pad::kPadMenu;     // Start
        case 9: return Pad::kPadL3;
        case 10: return Pad::kPadR3;
        case 11: return Pad::kPadHome;
        default: return Pad::kPadNone;
    }
}

// Steam Input's virtual pad: the stick clicks and Start/Back sit on different usages than the
// standard layout, and the D-pad is four buttons (12..15: up, down, left, right) instead of a hat.
static int steam_button(uint32_t usage) {
    switch (usage) {
        case 1: return Pad::kPadA;
        case 2: return Pad::kPadB;
        case 3: return Pad::kPadX;
        case 4: return Pad::kPadY;
        case 5: return Pad::kPadLB;
        case 6: return Pad::kPadRB;
        case 7: return Pad::kPadL3;
        case 8: return Pad::kPadR3;
        case 9: return Pad::kPadMenu;     // Start
        case 10: return Pad::kPadOptions; // Back
        case 11: return Pad::kPadHome;
        case 12: return Pad::kPadDUp;
        case 13: return Pad::kPadDDown;
        case 14: return Pad::kPadDLeft;
        case 15: return Pad::kPadDRight;
        default: return Pad::kPadNone;
    }
}

int button_pad(Layout layout, uint32_t usage_page, uint32_t usage) {
    if (usage_page != 0x09 || usage < 1 || usage > 15) return Pad::kPadNone;
    return layout == Layout::kSteamVirtual ? steam_button(usage) : standard_button(usage);
}

bool xbox_labelled(Layout layout) { return layout == Layout::kSteamVirtual; }

}  // namespace hid_gamepad
