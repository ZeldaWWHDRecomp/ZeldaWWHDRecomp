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
};
std::span<const Entry> entries();
const Entry* find(std::string_view id);
// Main thread, once before the game starts. Explicit env values win over saved state.
// Headless tests neither read nor write player preferences.
void load_saved();
bool set_enabled(std::string_view id, bool enabled);
void disable_all();
}  // namespace mods::manager
