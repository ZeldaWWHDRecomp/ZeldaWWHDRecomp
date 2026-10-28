// The common settings as one list (prototype, p9-gamesettings): what the game-style settings page in
// the item menu (game_settings_page.h) shows, and the one place that changes them. Each item reads
// the live value from the module that owns it and changes it through that module's own setter, saving
// it where the Settings window (overlay.cpp) saves it (settings.ini / display.plist through hostui::set,
// controls.json through input_map::set_current, the mod profile through packages::remember_option),
// so the page and the Settings window always show the same value: there is no copy of a value here.
//
// Items come from the port (the built-in ones below, registered by register_builtin()) and later from
// mods (the planned QoL package: a manifest "settings" list, owner = the mod's ID; see the design note
// in the p9 report). Values are floats: toggles 0 / 1, choices their index, numbers themselves.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace game_settings {

enum class Kind : uint8_t { Toggle, Choice, Number };

struct Item {
    std::string id;     // stable key ("camera.invert-y"); also the settings key for mod items
    std::string tab;    // "camera", "controls", "screen", "game", "mods"
    std::string label;  // English for now (the language plan: the report)
    std::string help;   // one line under the list while the item is selected
    Kind kind = Kind::Toggle;
    std::vector<std::string> choices;  // Choice: the value names
    float min = 0, max = 1, step = 1;  // Number
    std::function<float()> get;        // any thread (the owners keep their values in atomics)
    std::function<void(float)> set;    // main thread (apply() posts it): applies and saves
    std::function<std::string(float)> format;  // Number: the value as shown ("1.25x"); empty: "%.2f"
    std::function<bool()> enabled;     // false: shown dimmed (it depends on another item); empty: always
    std::string owner = "port";
    bool next_start = false;  // a change applies at the next start (the page says so after the help)
};

// The tabs that have items, in their order (camera, controls, hud, mods, then others)
std::vector<std::string> tabs();
std::string tab_label(const std::string& tab);
// The items of a tab, in registration order
std::vector<const Item*> items(const std::string& tab);
const Item* find(const std::string& id);
// Registers (or replaces, same id) an item. Main thread or before the game starts.
void add(Item item);
// The built-in items (the camera tab of the prototype); once, at start-up
void register_builtin();

// The item's value as the page shows it ("On", "Off", "1.25x", a choice name)
std::string value_text(const Item& item);
// A change from the page: toggles flip (step 0) or follow the direction, choices cycle, numbers move
// by one step within their range. Returns the new value; the item's set() runs on the main thread
// (hostui::post), so the value the page reads may lag one frame (the page shows `pending` meanwhile).
float apply(const Item& item, int direction);
// the value just applied for this id that the owner may not show yet (main thread lag), else NaN
float pending(const std::string& id);

// The Settings window's writers that the page shares (one source of truth for how they are saved)
void set_camera_speed(float speed);         // main thread
void set_mouse_sensitivity(float degrees);  // main thread
void set_invert_camera_y(bool on);          // main thread
void set_fov_scale(float scale);            // main thread (1 = the original gameplay field of view)
void set_rumble(bool on);                   // main thread
void set_move_speed_bar(bool on);           // main thread

}  // namespace game_settings
