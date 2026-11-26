// Steam Deck: the built-in controller's gyro under Steam Input (issue #566).
//
// Under Steam (Game Mode, or a game started from Steam with Steam Input on) Steam hides the Deck's
// physical controller (Valve 28DE:1205) from games through SDL's ignore list
// (SDL_GAMECONTROLLER_IGNORE_DEVICES in the environment) and gives them its virtual pad (28DE:11FF),
// which has no motion sensors: the "Controller gyro" source had nothing to read. As the Metroid Prime 2
// port does, the SDL host (platform/input_sdl.cpp) keeps reading every button from Steam's virtual pad
// and opens the physical Deck controller next to it for its gyro and accelerometer only:
//   - before SDL_Init, on a Steam Deck: 28DE:1205 is taken out of the ignore lists for this process (the
//     other ignored devices stay ignored) and SDL's HIDAPI Steam Deck driver is turned on;
//   - while a Steam virtual pad is connected, the physical Deck controller is a sensor-only device: its
//     buttons, sticks and triggers never reach the game, it never rumbles and never becomes the
//     controller the prompts follow; its gyro feeds the Controller gyro source while the controller in
//     use has no motion sensors of its own;
//   - without a Steam virtual pad (Steam not running, Steam Input off) it is the normal controller.
// WWHD_DECK_GYRO=0 turns all of this off (the behaviour before #566).
//
// This file is the host-independent part (no SDL), so the selection rules are tested on any host
// (motion_test); input_sdl.cpp applies them.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace deck_gyro {

constexpr uint16_t kValve = 0x28DE;
constexpr uint16_t kDeckController = 0x1205;     // the Deck's built-in controller (SDL: "Steam Deck")
constexpr uint16_t kSteamVirtualPad = 0x11FF;    // Steam Input's virtual gamepad on Linux

bool is_deck_controller(uint16_t vendor, uint16_t product);
// Steam Input's virtual pad: 28DE:11FF, or a pad SDL reports a Steam Input handle for
bool is_steam_virtual_pad(uint16_t vendor, uint16_t product, uint64_t steam_handle = 0);

// The feature is on: a Steam Deck, and WWHD_DECK_GYRO (`env`, may be null) is not "0" / "off" / "false"
bool enabled(bool steam_deck, const char* env);

// SDL VID/PID lists ("0x28de/0x1205,0x28de/0x11ff"; SDL reads every "0xVVVV" ... "0xPPPP" pair in the
// string, whatever separates them). The list without vendor/product, the other pairs kept in their order
// (written as SDL writes them: 0x%04x/0x%04x, comma separated). `changed` tells whether it was listed.
std::string without_device(const std::string& list, uint16_t vendor, uint16_t product, bool* changed = nullptr);
// The list with vendor/product added (unchanged when it is listed already)
std::string with_device(const std::string& list, uint16_t vendor, uint16_t product);
bool lists_device(const std::string& list, uint16_t vendor, uint16_t product);

// The environment variables / SDL hints to set before SDL_Init (name, value), from the current values.
// `get` returns a variable's value or null. A list given as "@file" (SDL's form for a list in a file) is
// read with `read_file` and written back as the list itself. Empty when the feature is off.
struct Prepared {
    std::vector<std::pair<std::string, std::string>> set;
    bool was_ignored = false;  // 28DE:1205 was on an ignore list (Steam hides the controller)
};
Prepared prepare(bool on, const std::function<const char*(const char*)>& get,
                 const std::function<std::string(const std::string&)>& read_file = {});

// ---- roles (input_sdl.cpp, on every controller added or removed) ----
struct Pad {
    uint32_t id = 0;
    uint16_t vendor = 0, product = 0;
    uint64_t steam_handle = 0;
};
enum class Role { kController, kSensorOnly };
// A connected pad's role, with `present` every connected pad (it may include `pad`): the physical Deck
// controller is sensor-only while the feature is on and a Steam virtual pad is connected.
Role role(bool on, const Pad& pad, const std::vector<Pad>& present);

// Whether a sensor-only device's gyro feeds the Controller gyro source now: only while the controller in
// use has no motion sensors of its own (no controller in use yet: yes), so two gyros never fight.
bool sensor_only_feeds(bool active_known, bool active_has_motion);

}  // namespace deck_gyro
