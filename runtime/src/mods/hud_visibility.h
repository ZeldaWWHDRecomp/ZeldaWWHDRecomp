// When the host-drawn gameplay HUDs (the run/swim boost's bar, the climb mod's stamina wheel) may be
// on screen. Pure rules, so hud_visibility_test can check them without the game; hud_gate.cpp reads
// the game's state once per frame and mod_hud.mm / vulkan/present.cpp apply the result.
#pragma once
#include <algorithm>
#include <cmath>

namespace mods {

// What the game shows over gameplay this frame (hud_gate.cpp reads it from the game).
struct GameUi {
    bool event = false;    // a cutscene, a talk, a get-item scene or another event runs (dComIfGp_event_runCheck)
    bool message = false;  // a message or dialogue box is open (dComIfGp_getMesgStatus, also the Telescope's)
    bool menu = false;     // a game menu: pause/start menu, item and collection screens, map, options
};

// The HUDs sit in the middle of the picture, so anything the game puts over gameplay hides them.
inline bool game_ui_hides_hud(const GameUi& ui) { return ui.event || ui.message || ui.menu; }

// One step of the HUDs' shared fade, `seconds` of real time since the last step. Quick (about a
// quarter of a second to vanish) so the HUD never lingers over a menu that is opening, and it
// snaps at the ends so a hidden HUD is exactly 0 and is not drawn at all.
constexpr float kGameUiFadeRate = 16.f;  // per second
inline float game_ui_fade(float alpha, bool hidden, float seconds) {
    const float target = hidden ? 0.f : 1.f;
    if (!(seconds > 0.f)) return alpha;
    alpha += (target - alpha) * std::min(1.f, seconds * kGameUiFadeRate);
    if (std::abs(alpha - target) < 0.01f) alpha = target;
    return std::clamp(alpha, 0.f, 1.f);
}

}  // namespace mods
