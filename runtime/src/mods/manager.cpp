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
    {"move-speed", "Run/swim speed", "Gameplay",
     "Boost Link's running and swimming speed, separate multipliers, held or toggled, with a stamina "
     "bar and a cooldown. Default: hold L3 (rebind it in Controls).",
     "WWHD_MOD_MOVE_SPEED", move_speed, set_move_speed},
    {"fast-forward", "Fast forward cutscenes and dialogue", "Gameplay",
     "Hold the selected button during events to play faster. Choices still wait for input.",
     "WWHD_MOD_FAST_FORWARD", fast_forward, set_fast_forward},
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
    if (!std::getenv("WWHD_MOD_FF_RATE")) {
        std::string v;
        if (hostui::get("mod.fast-forward.rate", v) && (v == "2" || v == "3" || v == "4"))
            set_fast_forward_rate(unsigned(v[0] - '0'));
    }
    if (!std::getenv("WWHD_MOD_FF_BUTTON")) {
        std::string v;
        if (hostui::get("mod.fast-forward.button", v)) {
            char* end = nullptr;
            auto n = std::strtoul(v.c_str(), &end, 10);
            if (end != v.c_str() && !*end && n <= UINT32_MAX) set_fast_forward_button(uint32_t(n));
        }
    }
    if (!std::getenv("WWHD_MOD_FF_MUTE")) {
        std::string v;
        if (hostui::get("mod.fast-forward.mute", v) && (v == "0" || v == "1")) set_fast_forward_mute(v == "1");
    }
    number("mod.move-speed.land", "WWHD_MOD_MOVE_FACTOR", 1.f, 4.f, set_move_speed_land_factor);
    number("mod.move-speed.swim", "WWHD_MOD_MOVE_SWIM", 1.f, 4.f, set_move_speed_swim_factor);
    // A settings file from before the split stored one factor, which applied to both states.
    if (!std::getenv("WWHD_MOD_MOVE_FACTOR") && !std::getenv("WWHD_MOD_MOVE_SWIM")) {
        std::string land_v, swim_v, legacy_v;
        if (hostui::get("mod.move-speed.factor", legacy_v) && !hostui::get("mod.move-speed.land", land_v) &&
            !hostui::get("mod.move-speed.swim", swim_v)) {
            char* end = nullptr;
            const float f = std::strtof(legacy_v.c_str(), &end);
            if (end != legacy_v.c_str() && *end == '\0' && std::isfinite(f) && f >= 1.f && f <= 4.f) {
                set_move_speed_land_factor(f);
                set_move_speed_swim_factor(f);
            }
        }
    }    number("mod.move-speed.stamina", "WWHD_MOD_MOVE_STAMINA", 0.f, 60.f, set_move_speed_stamina_seconds);
    number("mod.move-speed.cooldown", "WWHD_MOD_MOVE_COOLDOWN", 0.f, 60.f, set_move_speed_cooldown_seconds);
    std::string mode;
    if (hostui::get("mod.move-speed.mode", mode) && (mode == "0" || mode == "1"))
        set_move_speed_mode(mode == "1" ? MoveMode::kToggle : MoveMode::kHold);
    std::string anim;
    if (hostui::get("mod.move-speed.anim", anim) && (anim == "native" || anim == "dash"))
        set_move_speed_anim(anim == "dash" ? MoveAnim::kDash : MoveAnim::kNative);
    number("mod.direct-camera.speed", "WWHD_MOD_CAMERA_SPEED", .5f, 2.f, set_camera_speed);
    std::string button;
    if (hostui::get("mod.move-speed.button", button)) {
        char* end = nullptr;
        auto value = std::strtoul(button.c_str(), &end, 10);
        if (end != button.c_str() && *end == '\0' && value <= UINT32_MAX) set_move_speed_button(uint32_t(value));
    }
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
