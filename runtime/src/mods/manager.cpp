#include "manager.h"
#include "packages.h"
#include "mods.h"
#include "climb.h"
#include "../overlay/hostui.h"
#include <cstdlib>
#include <cmath>
#include <string>

namespace mods::manager {
namespace {
const Entry catalogue[] = {
    {"direct-camera", "Direct right-stick camera", "Camera",
     "Turn the camera immediately with the right stick, without the original easing.",
     "WWHD_MOD_DIRECT_CAMERA", direct_camera, set_direct_camera},
    {"mouse-camera", "Mouse camera", "Camera",
     "Click the game picture to capture the mouse. Move it to look around; Esc releases it.",
     "WWHD_MOD_MOUSE_CAMERA", mouse_camera, set_mouse_camera},
    {"first-person", "First-person shortcut", "Camera",
     "Enter first person with R3 or the mouse wheel.",
     "WWHD_MOD_FIRST_PERSON", first_person_wheel, set_first_person_wheel},
    {"wall-climb", "Climb any wall", "Gameplay",
     "Grab and climb walls using a stamina wheel. A or B lets go.",
     "WWHD_CLIMB", climb_enabled, set_climb_enabled},
    {"quick-doors", "Quick doors", "Gameplay",
     "Run door-opening and closing events at four times their normal speed.",
     "WWHD_MOD_QUICK_DOORS", quick_doors, set_quick_doors},
    {"fast-scenes", "Fast scene changes", "Gameplay",
     "Speed up fades and scene transitions while keeping ordinary gameplay at normal speed.",
     "WWHD_MOD_FAST_SCENES", fast_scenes, set_fast_scenes},
};
std::string key(const Entry& entry) { return std::string("mod.")+entry.id+".enabled"; }
bool player_preferences() { return !std::getenv("WWHD_NO_HOST_INPUT"); }
}

std::span<const Entry> entries() { return catalogue; }
const Entry* find(std::string_view id) {
    for (const auto& entry : catalogue) if (id == entry.id) return &entry;
    return nullptr;
}
void load_saved() {
    if (!player_preferences()) return;
    for (const auto& entry : catalogue) {
        // An explicitly supplied zero also overrides a saved enabled preference.
        if (std::getenv(entry.startup_env)) continue;
        std::string value;
        if (hostui::get(key(entry).c_str(), value) && (value == "0" || value == "1"))
            if (entry.enabled() != (value == "1")) entry.apply(value == "1");
    }
    auto number = [](const char* key, const char* env, float lo, float hi, void (*apply)(float)) {
        if (std::getenv(env)) return;
        std::string value;
        if (!hostui::get(key, value) || value.empty()) return;
        char* end = nullptr;
        float parsed = std::strtof(value.c_str(), &end);
        if (end != value.c_str() && *end == '\0' && std::isfinite(parsed) && parsed >= lo && parsed <= hi)
            apply(parsed);
    };
    number("mod.direct-camera.speed", "WWHD_MOD_CAMERA_SPEED", .5f, 2.f, set_camera_speed);
    number("mod.mouse-camera.sensitivity", "WWHD_MOD_MOUSE_SENS", .08f, .3f, set_mouse_sensitivity);
}
bool set_enabled(std::string_view id, bool on) {
    const auto* entry = find(id);
    if (!entry) return false;
    if (entry->enabled() != on) entry->apply(on);
    if (player_preferences()) hostui::set(key(*entry).c_str(), on ? "1" : "0");
    packages::remember_builtin(std::string(id), on);
    return true;
}
void disable_all() {
    for (const auto& entry : catalogue) set_enabled(entry.id, false);
}
}  // namespace mods::manager
