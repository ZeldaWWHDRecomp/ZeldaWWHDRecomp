// World editor: the running game's side of the command layer (docs/mod-editor.md).
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
bool try_with_session(const std::function<void(Session&)>& f);  // the same, false while a command runs (no waiting)
// The UI's commands: run in order on a worker thread (a command may wait for the game); `done` gets the
// result there.
// `source` marks the command log entry ("MCP": a tool client, editor/mcp.h).
void run_async(const std::string& command, const Value& params, std::function<void(const Value&)> done, const std::string& source = {});
bool commands_pending();
// The editor's handle drag (render thread, never waits): while a handle is dragged the actors follow it
// in the game, once per logic step, through the live update the commands use (Session::drag_preview),
// without a command, an undo step or a log entry. `poses`: [{"id", "position"?, "rotation"?}], sent every
// frame (unchanged poses cost nothing). end_drag: the drag is over; `put_back` shows `ids` at the
// package's places again (cancelled, or the released command changed nothing); otherwise the released
// command's own live update follows.
void drag_actors(const std::string& mod, const Value& poses);
void end_drag(const std::string& mod, const std::vector<std::string>& ids, bool put_back);
std::string drag_note();  // what the drag can't move live ("" all of it)
void note(const std::string& command, const Value& params, const Value& result, const std::string& source);  // a log entry

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

// ---- the launch mode's editor view (render thread unless said) ----
bool playing();      // Play is on: the editor's overlays hide (any thread)
bool editor_view();  // the editor's overlays show: launch mode, the fly camera on, not playing (any thread)
void toggle_play();  // Tab / the controller's View button: Play from the camera, or Stop
// A scene preview runs (preview_scene): the editor's overlays hide but for its Skip / Stop bar (any thread).
bool previewing();
std::string preview_label();  // "harbor: step 3" while previewing
// The game camera as camera_draw last drew it (eye, center, up, fovy in degrees, aspect, near/far and
// the projection it built): what the overlays project the world with.
struct CameraView {
    bool valid = false;
    float eye[3] = {}, center[3] = {}, up[3] = {};
    float fovy = 0, aspect = 0, near_z = 0, far_z = 0;
    float proj[16] = {};
};
void camera_drawn(uint32_t camera);  // interp.cpp, game thread: after camera_draw
CameraView camera_view();
void add_look(float dx, float dy);   // the editor view's right-button drag (pixels)
void add_speed(float notches);       // the mouse wheel over the picture: the fly speed

}  // namespace editor
