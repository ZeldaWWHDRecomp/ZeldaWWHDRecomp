// Built-in recomp mod catalogue. Package/native-plugin loading is a separate layer.
#pragma once
#include <cstddef>
#include <span>
#include <string_view>

namespace mods::manager {
struct Entry {
    const char* id;
    const char* name;
    const char* category;
    const char* description;
    const char* startup_env;
    bool (*enabled)();
    void (*apply)(bool);
    bool restart_required = false;
    bool default_enabled = false;
};
std::span<const Entry> entries();
// A number setting of a built-in mod: saved as `key` (settings file), `profile_key` (mod profiles).
struct NumberOption {
    const char* key;
    const char* profile_key;
    const char* startup_env;
    float (*value)();
    void (*apply)(float);
};
// the traversal mods' factors (1x..3x): ladder, sidle, ivy, ledge hang, normal sail, Swift Sail
std::span<const NumberOption> traversal_options();
// main thread: apply, save and remember one of them (profile_key, e.g. "sail-speed.swift")
void set_traversal_option(std::string_view profile_key, float value);
const Entry* find(std::string_view id);
// Main thread, once before the game starts. Explicit env values win over saved state.
// Headless tests neither read nor write player preferences.
void load_saved();
bool set_enabled(std::string_view id, bool enabled);
void disable_all();
}  // namespace mods::manager
