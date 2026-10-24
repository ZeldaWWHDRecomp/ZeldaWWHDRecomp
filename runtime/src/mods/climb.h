// "Climb any wall" mod (Gameplay menu): see climb.cpp.
#pragma once

namespace mods {
bool climb_enabled();
void set_climb_enabled(bool on);

// stamina wheel state for the HUD overlay (mod_hud.mm); read from the render thread
struct ClimbHud {
    float stamina;    // 0..1
    float alpha;      // 0 = hidden
    bool exhausted;   // ran out: no climbing until full again
};
ClimbHud climb_hud();
ClimbHud climb_hud_drawn();  // as the frame being shown draws it (hud_gate.cpp)
}  // namespace mods
