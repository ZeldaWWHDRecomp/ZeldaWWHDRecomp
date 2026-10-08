// Quitting while a game is in progress asks first (issue #65): closing the TV window or Quit (Cmd+Q)
// shows "Quit Wind Waker HD?" with Quit, Cancel and Save State and Quit. This header holds the
// decisions (when to ask, which test answer); the macOS dialog is gfx/quit_prompt.mm. Kept free of
// AppKit and of the game so runtime/tools/quit_prompt_test.cpp can check it.
#pragma once
#include <cstring>
#include <string>

namespace quitprompt {

// the save state slot "Save State and Quit" writes (Save States menu, Shift+F1)
constexpr int kSaveSlot = 1;

// a save file is being played: not before the game's first stage (boot), the title screen ("sea_T")
// or the file select ("Name"). Only then can quitting lose progress, so only then the prompt asks.
inline bool gameplay_stage(const std::string& stage) { return !stage.empty() && stage != "sea_T" && stage != "Name"; }

using Env = const char* (*)(const char*);
inline bool env_on(const char* v) { return v && *v && strcmp(v, "0") != 0; }

// test and headless runs never ask: hidden windows, scripted runs (WWHD_NO_HOST_INPUT), a quit at a
// frame (WWHD_EXIT_AT_FRAME), the controls window self-test; WWHD_QUIT_PROMPT=0 turns it off
inline bool suppressed(Env env) {
    const char* opt = env("WWHD_QUIT_PROMPT");
    return env_on(env("WWHD_HIDDEN_WINDOWS")) || env_on(env("WWHD_EXIT_AT_FRAME")) || env("WWHD_NO_HOST_INPUT") ||
           env("WWHD_CONTROLS_SELFTEST") || (opt && !env_on(opt));
}

// test aid: WWHD_TEST_QUIT_ANSWER=quit|cancel|save shows the prompt also in a scripted run and answers
// it by itself after a moment
enum class Answer { None, Quit, Cancel, SaveAndQuit };
inline Answer test_answer(Env env) {
    const char* v = env("WWHD_TEST_QUIT_ANSWER");
    if (!v) return Answer::None;
    if (!strcmp(v, "quit")) return Answer::Quit;
    if (!strcmp(v, "cancel")) return Answer::Cancel;
    if (!strcmp(v, "save")) return Answer::SaveAndQuit;
    return Answer::None;
}

// what a quit request (TV window closed, Cmd+Q, Dock Quit, logout) does
enum class Action { Quit, Ask, Ignore };
struct Request {
    bool confirmed = false;    // answered Quit (or the save state is written): the next request quits
    bool busy = false;         // the prompt is open or the save state is being written
    bool game_window = false;  // the TV window exists and the game has drawn a frame
    bool gameplay = false;     // gameplay_stage()
    bool suppressed = false;   // suppressed()
    bool test_answer = false;  // test_answer() != None
};
inline Action decide(const Request& r) {
    if (r.confirmed) return Action::Quit;
    if (r.busy) return Action::Ignore;  // one prompt at a time; quitting from it does not ask again
    if (!r.game_window || !r.gameplay) return Action::Quit;
    if (r.suppressed && !r.test_answer) return Action::Quit;
    return Action::Ask;
}

}  // namespace quitprompt
