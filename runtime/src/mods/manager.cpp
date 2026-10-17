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
    {"target-letterbox", "Letterbox while targeting", "Camera",
     "Show letterbox bars during normal targeting; cutscenes and special cameras keep their original bars.",
     "WWHD_TARGET_LETTERBOX", letterbox_while_targeting, set_letterbox_while_targeting, false, true},
    {"direct-camera", "Direct right-stick camera", "Camera",
     "Turn the camera immediately with the right stick, without the original easing.",
     "WWHD_MOD_DIRECT_CAMERA", direct_camera, set_direct_camera},
    {"mouse-camera", "Mouse camera", "Camera",
     "Click the game picture to capture the mouse. Move it to look around; Esc releases it.",
     "WWHD_MOD_MOUSE_CAMERA", mouse_camera, set_mouse_camera},
    {"first-person", "First-person shortcut", "Camera",
     "Enter first person with R3 or the mouse wheel.",
     "WWHD_MOD_FIRST_PERSON", first_person_wheel, set_first_person_wheel},
    {"lock-on", "Extended enemy lock-on", "Gameplay",
     "Extend enemy targeting range and press R3 while locked on to cycle enemy targets. "
     "Disabled keeps the original targeting behavior.",
     "WWHD_MOD_LOCK_ON", extended_lock_on, set_extended_lock_on},
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
    {"ladder-speed", "Faster ladders", "Gameplay",
     "Climb ladders faster. The climbing animation plays faster, so hands and feet stay in step.",
     "WWHD_MOD_LADDER_SPEED", ladder_speed, set_ladder_speed},
    {"sidle-speed", "Faster sidling", "Gameplay",
     "Sidle along walls faster. The animation speeds up with the movement.",
     "WWHD_MOD_SIDLE_SPEED", sidle_speed, set_sidle_speed},
    {"ivy-speed", "Faster ivy climbing", "Gameplay",
     "Climb ivy and vines faster, up, down and sideways. The climbing animation plays faster.",
     "WWHD_MOD_IVY_SPEED", ivy_speed, set_ivy_speed},
    {"hang-speed", "Faster ledge hanging", "Gameplay",
     "Move along a ledge faster while hanging from it. The animation plays faster.",
     "WWHD_MOD_HANG_SPEED", hang_speed, set_hang_speed},
    {"sail-speed", "Sailing speed", "Gameplay",
     "Sail faster, with separate multipliers for the normal sail and the Swift Sail.",
     "WWHD_MOD_SAIL_SPEED", sail_speed, set_sail_speed},
    {"auto-wind", "Automatic wind", "Gameplay",
     "Makes the normal sail behave like the Swift Sail: the wind always blows the way the boat is heading.",
     "WWHD_MOD_AUTO_WIND", auto_wind, set_auto_wind},
    {"jump-anywhere", "Jump anywhere", "Gameplay",
     "Jump from standing or running with a button (default L), optionally once more in the air. "
     "Can also turn off the automatic jumps off ledges.",
     "WWHD_MOD_JUMP", jump_anywhere, set_jump_anywhere},
    {"fast-forward", "Fast forward cutscenes and dialogue", "Gameplay",
     "Hold the selected button during events to play faster. Choices still wait for input.",
     "WWHD_MOD_FAST_FORWARD", fast_forward, set_fast_forward},
    {"story-system", "Story system", "Story",
     "Letters, quests, the story notebook and mod charts. It comes with the Story Notebook package, which the "
     "mod manager downloads from the mod catalogue and keeps installed while this is on.",
     "WWHD_STORY_SYSTEM", story_system, set_story_system, true},
    {"fast-scenes", "Fast scene changes", "Gameplay",
     "Speed up fades and scene transitions while keeping ordinary gameplay at normal speed.",
     "WWHD_MOD_FAST_SCENES", fast_scenes, set_fast_scenes},
};
std::string key(const Entry& entry) { return std::string("mod.")+entry.id+".enabled"; }
bool player_preferences() { return !std::getenv("WWHD_NO_HOST_INPUT"); }
const NumberOption traversal_catalogue[] = {
    {"mod.ladder-speed.factor", "ladder-speed.factor", "WWHD_MOD_LADDER_FACTOR", ladder_speed_factor, set_ladder_speed_factor},
    {"mod.sidle-speed.factor", "sidle-speed.factor", "WWHD_MOD_SIDLE_FACTOR", sidle_speed_factor, set_sidle_speed_factor},
    {"mod.ivy-speed.factor", "ivy-speed.factor", "WWHD_MOD_IVY_FACTOR", ivy_speed_factor, set_ivy_speed_factor},
    {"mod.hang-speed.factor", "hang-speed.factor", "WWHD_MOD_HANG_FACTOR", hang_speed_factor, set_hang_speed_factor},
    {"mod.sail-speed.normal", "sail-speed.normal", "WWHD_MOD_SAIL_FACTOR", sail_speed_normal_factor, set_sail_speed_normal_factor},
    {"mod.sail-speed.swift", "sail-speed.swift", "WWHD_MOD_SWIFT_SAIL_FACTOR", sail_speed_swift_factor, set_sail_speed_swift_factor},
};
}  // namespace

std::span<const NumberOption> traversal_options() { return traversal_catalogue; }
void set_traversal_option(std::string_view profile_key, float value) {
    for (const auto& option : traversal_catalogue) {
        if (profile_key != option.profile_key) continue;
        option.apply(value);
        const float applied = option.value();
        if (player_preferences()) hostui::set(option.key, std::to_string(applied));
        packages::remember_option(option.profile_key, applied);
        return;
    }
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
    number("mod.lock-on.range", "WWHD_MOD_LOCK_ON_RANGE", 1.f, 3.f, set_lock_on_range);
    if (!std::getenv("WWHD_MOD_LOCK_ON_CYCLING")) {
        std::string v;
        if (hostui::get("mod.lock-on.cycling", v) && (v == "0" || v == "1"))
            set_lock_on_cycling(v == "1");
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
    }
    number("mod.move-speed.stamina", "WWHD_MOD_MOVE_STAMINA", 0.f, 60.f, set_move_speed_stamina_seconds);
    number("mod.move-speed.cooldown", "WWHD_MOD_MOVE_COOLDOWN", 0.f, 60.f, set_move_speed_cooldown_seconds);
    std::string mode;
    if (hostui::get("mod.move-speed.mode", mode) && (mode == "0" || mode == "1"))
        set_move_speed_mode(mode == "1" ? MoveMode::kToggle : MoveMode::kHold);
    if (!std::getenv("WWHD_MOD_MOVE_BAR")) {
        std::string v;
        if (hostui::get("mod.move-speed.bar", v) && (v == "0" || v == "1")) set_move_speed_bar(v == "1");
    }
    std::string anim;
    if (!std::getenv("WWHD_MOD_MOVE_ANIM") && hostui::get("mod.move-speed.anim", anim) && valid_move_anim_id(anim))
        set_move_speed_anim(move_anim_from_id(anim));
    for (const auto& option : traversal_options()) number(option.key, option.startup_env, 1.f, 3.f, option.apply);
    number("mod.direct-camera.speed", "WWHD_MOD_CAMERA_SPEED", .5f, 2.f, set_camera_speed);
    std::string button;
    if (!std::getenv("WWHD_MOD_MOVE_BUTTON") && hostui::get("mod.move-speed.button", button)) {
        char* end = nullptr;
        auto value = std::strtoul(button.c_str(), &end, 10);
        if (end != button.c_str() && *end == '\0' && value <= UINT32_MAX) set_move_speed_button(uint32_t(value));
    }
    number("mod.mouse-camera.sensitivity", "WWHD_MOD_MOUSE_SENS", .08f, .3f, set_mouse_sensitivity);
    auto flag = [](const char* key, const char* env, void (*apply)(bool)) {
        std::string v;
        if (!std::getenv(env) && hostui::get(key, v) && (v == "0" || v == "1")) apply(v == "1");
    };
    flag("mod.jump-anywhere.double", "WWHD_MOD_JUMP_DOUBLE", set_double_jump);
    flag("mod.jump-anywhere.no-auto", "WWHD_MOD_JUMP_NO_AUTO", set_no_auto_jump);
    std::string jump;
    if (!std::getenv("WWHD_MOD_JUMP_BUTTON") && hostui::get("mod.jump-anywhere.button", jump)) {
        char* end = nullptr;
        auto value = std::strtoul(jump.c_str(), &end, 10);
        if (end != jump.c_str() && *end == '\0' && value <= UINT32_MAX) set_jump_button(uint32_t(value));
    }
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
