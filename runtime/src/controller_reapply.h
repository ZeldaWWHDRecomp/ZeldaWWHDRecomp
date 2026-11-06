// After a save-state load the game must play with the controller the player uses now.
//
// The host's choice (input::pro_controller, Input menu / Controls) is never taken from a state. The
// game keeps its own choice in hdInputManager::mMode (singleton 101F5088 +0x1D0, wwhd_src
// d/hd_input_ctrl.cpp): 0 Pro Controller (TV menus), 1 GamePad, 2 GamePad Off-TV, 3 both (boot,
// title, file select), 4 none. A full state brings back the mode it was saved with; a progress state
// keeps the running one. When it does not match the host, the game reads a controller that is idle
// (Link does not move), so the mode is switched once the player is in play again, with the steps of
// the game's own in-game controller change (Options, StateID_ExchangeProMode) that concern play:
// hdInputManager::setMode and, for the GamePad, its touch pointer. Nothing is shown and no input is
// made. Only if the game refuses the mode directly does the change run through the pause menu (the
// Options screen's controller change, menu closed). Same-controller loads change nothing.
#pragma once
#include <cstdint>

struct Cpu;

namespace controller_reapply {
// The Options category the game has to switch to (0 Pro Controller, 1 GamePad), or -1: nothing to
// do (the game already uses the host's controller, or it is in a scene that picks for itself).
constexpr int needed_category(int game_mode, bool host_pro) {
    if (game_mode == 0) return host_pro ? -1 : 1;
    if (game_mode == 1 || game_mode == 2) return host_pro ? 0 : -1;
    return -1;
}

// The game reads the GamePad (VPAD) in modes 1 and 2 and the Pro Controller (KPAD) in mode 0.
constexpr bool reads_gamepad(int game_mode) { return game_mode == 1 || game_mode == 2; }

// While the game is being moved from the GamePad to the Pro Controller (switching: a re-apply is under
// way), the GamePad keeps its buttons and sticks until the game's mode has followed. The game only
// follows in play: in a menu (pause menu, file select) it needs a press to get back there, and a
// GamePad it no longer hears would leave it waiting forever (issue #330). category: the Options
// category the switch is running with (0 Pro Controller, -1 not decided yet); while the menu path
// switches the game (its own steps, no player input) the GamePad is idle, as before.
constexpr bool keeps_gamepad(bool switching, int game_mode, bool host_pro, int category) {
    return switching && host_pro && reads_gamepad(game_mode) && category != 0;
}

// Without an in-game UI (title, file select, a stage change) the re-apply gives up after a while:
// there is nothing to switch. Not while the GamePad is kept for the game: the file select reaches
// play only by a press on it, however long the player waits there.
constexpr bool may_give_up(bool keeps_gamepad, int frames_without_ui, int give_up_frames) {
    return !keeps_gamepad && frames_without_ui > give_up_frames;
}

// a state was loaded (any thread): check the game's controller once it is playing again. A change of
// the host's choice (input::set_pro_controller) requests the same by itself. While the game's mode does
// not match, it waits for the play state (cutscenes and menus finish first).
void request(const char* kind);
// at the frame boundary (ss::service)
void service(Cpu* c);
// the game is being switched from the Pro Controller to the GamePad: its Pro Controller stays
// connected until the switch is done (padscore)
bool holds_pro_controller();
// ... and keeps its buttons and sticks while the game still reads it (mode 0): in the pause menu the
// press that leads back to play must reach the game. Idle while the menu path switches it.
bool pro_controller_live();
// the game is being switched from the GamePad to the Pro Controller: its GamePad buttons and sticks
// stay live (VPADRead) until the switch is done (keeps_gamepad)
bool holds_gamepad();
}  // namespace controller_reapply
