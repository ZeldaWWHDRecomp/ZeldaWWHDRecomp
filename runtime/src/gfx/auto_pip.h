// Host-only policy. The input describes the game's screen routing, never requests a game change.
#pragma once
#include <cstdint>
#include <array>
#include "guest_addr.h"
namespace gfx {
struct GamePadUiState {
    bool available = false;
    bool menu = false;
    bool off_tv = false;
    bool switching = false;
    bool baton = false; // Wind Waker conducting: the game lists the songs on the GamePad
};
// Descriptors are data pointers in the running regional build, never canonical IDs.
// Map every entry separately: a regional map may split the descriptor range.
enum class GamePadSceneKind { Unknown, Other, Menu, Switch, Baton };
inline GamePadSceneKind gamepad_scene_kind(uint32_t descriptor) {
    struct Entry { uint32_t descriptor; GamePadSceneKind kind; };
    static const std::array<Entry, 54> states = {{
        {GD(0x1049EBD4), GamePadSceneKind::Other},
        {GD(0x1049EC00), GamePadSceneKind::Other},
        {GD(0x1049EC2C), GamePadSceneKind::Other},
        {GD(0x1049EC58), GamePadSceneKind::Other},
        {GD(0x1049EC84), GamePadSceneKind::Menu},
        {GD(0x1049ECB0), GamePadSceneKind::Menu},
        {GD(0x1049ECDC), GamePadSceneKind::Menu},
        {GD(0x1049ED08), GamePadSceneKind::Menu},
        {GD(0x1049ED34), GamePadSceneKind::Menu},
        {GD(0x1049ED60), GamePadSceneKind::Menu},
        {GD(0x1049ED8C), GamePadSceneKind::Menu},
        {GD(0x1049EDB8), GamePadSceneKind::Baton}, // Wind Waker song list opening
        {GD(0x1049EDE4), GamePadSceneKind::Baton}, // Wind Waker song list
        {GD(0x1049EE10), GamePadSceneKind::Menu},
        {GD(0x1049EE3C), GamePadSceneKind::Menu},
        {GD(0x1049EE68), GamePadSceneKind::Other},
        {GD(0x1049EE94), GamePadSceneKind::Other},
        {GD(0x1049EEC0), GamePadSceneKind::Other},
        {GD(0x1049EEEC), GamePadSceneKind::Menu},
        {GD(0x1049EF18), GamePadSceneKind::Menu},
        {GD(0x1049EF44), GamePadSceneKind::Menu},
        {GD(0x1049EF70), GamePadSceneKind::Menu},
        {GD(0x1049EF9C), GamePadSceneKind::Menu},
        {GD(0x1049EFC8), GamePadSceneKind::Menu},
        {GD(0x1049EFF4), GamePadSceneKind::Menu},
        {GD(0x1049F020), GamePadSceneKind::Menu},
        {GD(0x1049F04C), GamePadSceneKind::Menu},
        {GD(0x1049F078), GamePadSceneKind::Menu},
        {GD(0x1049F0A4), GamePadSceneKind::Menu},
        {GD(0x1049F0D0), GamePadSceneKind::Menu},
        {GD(0x1049F0FC), GamePadSceneKind::Menu},
        {GD(0x1049F128), GamePadSceneKind::Other},
        {GD(0x1049F154), GamePadSceneKind::Other},
        {GD(0x1049F180), GamePadSceneKind::Other},
        {GD(0x1049F1AC), GamePadSceneKind::Other},
        {GD(0x1049F1D8), GamePadSceneKind::Menu},
        {GD(0x1049F204), GamePadSceneKind::Menu},
        {GD(0x1049F230), GamePadSceneKind::Other},
        {GD(0x1049F25C), GamePadSceneKind::Other},
        {GD(0x1049F288), GamePadSceneKind::Menu},
        {GD(0x1049F2B4), GamePadSceneKind::Menu},
        {GD(0x1049F2E0), GamePadSceneKind::Menu},
        {GD(0x1049F30C), GamePadSceneKind::Menu},
        {GD(0x1049F338), GamePadSceneKind::Menu},
        {GD(0x1049F364), GamePadSceneKind::Other},
        {GD(0x1049F390), GamePadSceneKind::Other},
        {GD(0x1049F3BC), GamePadSceneKind::Other},
        {GD(0x1049F3E8), GamePadSceneKind::Other},
        {GD(0x1049F414), GamePadSceneKind::Other},
        {GD(0x1049F440), GamePadSceneKind::Other},
        {GD(0x1049F46C), GamePadSceneKind::Other},
        {GD(0x1049F498), GamePadSceneKind::Switch},
        {GD(0x1049F4C4), GamePadSceneKind::Switch},
        {GD(0x1049F4F0), GamePadSceneKind::Switch},
    }};
    for (const auto& entry : states)
        if (entry.descriptor == descriptor) return entry.kind;
    return GamePadSceneKind::Unknown;
}
inline bool gamepad_menu_state(uint32_t descriptor, uint32_t routing, uint32_t controller, bool bag_open = false) {
    // 2 routes the pause/menu overlay to DRC; 4 also occurs during ordinary cutscenes.
    return controller == 1 && (routing == 2 || bag_open || gamepad_scene_kind(descriptor) == GamePadSceneKind::Menu);
}
// Wind Waker: the metronome flag is on while Link conducts, the scene's song-list states
// while the GamePad shows the songs (they open a few frames after the flag and close after it).
// With the Pro Controller (0) the GamePad is not used; Off-TV (2) already shows it full size.
inline bool gamepad_baton_state(uint32_t descriptor, bool metronome, uint32_t controller) {
    return controller == 1 && (metronome || gamepad_scene_kind(descriptor) == GamePadSceneKind::Baton);
}
inline bool gamepad_switch_state(uint32_t descriptor) {
    return gamepad_scene_kind(descriptor) == GamePadSceneKind::Switch;
}
struct AutoPipDecision {
    double until = 0;
    void update(GamePadUiState state, double now) {
        if (!state.available || state.off_tv || state.switching) until = 0;
        else if (state.menu || state.baton) until = now + 0.25;
    }
    bool shown(GamePadUiState state, bool pin, double now) const {
        return !state.off_tv && !state.switching &&
            (pin || (state.available && (state.menu || state.baton || now < until)));
    }
};
} // namespace gfx
