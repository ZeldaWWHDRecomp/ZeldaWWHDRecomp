// World editor, phase E0: the running game's side of the command layer (docs/mod-editor.md).
//
//   - the session: the active mods' packages (registered while they load), run from the overlay's Editor
//     area, from a test script (WWHD_EDITOR_SCRIPT) and later from a tool interface; one lock;
//   - the game thread (logic_service, once per logic pass): live actor updates, the fly camera, the
//     test-mode story snapshot, mod record resets;
//   - warps and test states go through the save-state service (savestate.h: the frame boundary);
//   - the fly camera holds a compulsory event (Link frozen, as a mod scene does) and drives the game
//     camera with dCamera_c's own set call once per logic pass (1/30 s), so the 60/120 fps
//     interpolation draws it smoothly; with true 60 the camera runs at 60 Hz, and so does the fly
//     camera (every pass, 1/60 s each).
//
// Only in the editor launch mode: the game started with --editor (WWHD_EDITOR=1 or WWHD_EDITOR_MODE=1 in
// scripted runs). A normal launch has no editor at all.
#pragma once
#include <functional>
#include <string>

#include "commands.h"

struct Cpu;
namespace input { struct PadState; }

namespace editor {

bool launch_mode();               // started with --editor (any thread)
void set_launch_mode(bool on);    // main(), from the command line, before the overlay starts
inline bool enabled() { return launch_mode(); }

// While each mod's manifest loads (guest_mods.cpp load_world): packages with actors, models, scenes or quests.
void register_package(const std::string& mod, const std::string& name, const std::string& dir, const Value& manifest);

// The command layer (any thread but the game thread inside a pass; serialised).
Value run(const std::string& command, const Value& params = Value());
Value query(const std::string& command, const Value& params = Value());  // the same, kept out of the command log (UI reads)
std::string run_json(const std::string& request);
void with_session(const std::function<void(Session&)>& f);  // the UI's read-only view, under the same lock

// The last asynchronous outcome (a warp started, a test state written, a live update refused), for the UI.
std::string status();

// ---- game side ----
void logic_service(Cpu* c);           // interp.cpp: once per logic pass (never on interpolation-only passes)
// interp.cpp, every pass of fpcEx_Handler: the fly camera steps on logic passes, and with true 60 also on
// its 60 Hz passes (the game camera runs at 60 Hz there), never on interpolation-only passes
void camera_service(Cpu* c, bool logic_pass);
void filter_pad(input::PadState& s);  // end of input::read: the fly camera takes the controller and keyboard
bool take_mouse(float dx, float dy);  // mouse movement while captured: true while the fly camera uses it
bool take_wheel(float dy);

struct FlyView {
    bool on = false;
    float eye[3] = {}, target[3] = {};
    float speed = 0;
    std::string stage;
    int room = -1;  // Link's room
};
FlyView fly_view();  // any thread

}  // namespace editor
