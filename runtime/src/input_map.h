// Controls mapping: which keyboard keys and host-controller inputs drive each Wii U GamePad input.
//
// Plain C++ (no AppKit) so it can be unit-tested (runtime/tools/input_map_test.cpp). input.mm
// evaluates the current mapping on every read; the Controls window (gfx/controls_ui.mm) edits it.
// Persisted as JSON in ~/Library/Application Support/WWHD/controls.json ($WWHD_CONTROLS overrides).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "input.h"

namespace input_map {

// Wii U GamePad / Pro Controller inputs, in the order the Controls window lists them, then the app
// actions that are bound the same way (no VPAD bit; the hosts act on them, see screenshot.h)
enum Action : int {
    kA, kB, kX, kY, kL, kR, kZL, kZR, kPlus, kMinus, kHome,
    kDUp, kDDown, kDLeft, kDRight, kStickLClick, kStickRClick,
    kLUp, kLDown, kLLeft, kLRight,   // left stick (move)
    kRUp, kRDown, kRLeft, kRRight,   // right stick (camera)
    kScreenshot,                     // app action, not a GamePad input: save a screenshot (screenshot.h)
    kActionCount
};
const char* action_id(int a);     // JSON key, e.g. "ZL", "LeftStickUp"
const char* action_label(int a);  // for the UI, e.g. "Left stick ↑ (move)"
uint32_t action_bit(int a);       // VPAD button bit, 0 for stick directions
int action_from_id(const std::string& id);  // -1 if unknown

// Host controller inputs (GameController extended gamepad, named by Xbox position)
enum Pad : int {
    kPadNone,
    kPadA, kPadB, kPadX, kPadY, kPadLB, kPadRB, kPadLT, kPadRT,
    kPadMenu, kPadOptions, kPadHome, kPadL3, kPadR3,
    kPadDUp, kPadDDown, kPadDLeft, kPadDRight,
    kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight,
    kPadRSUp, kPadRSDown, kPadRSLeft, kPadRSRight,
    kPadCount
};
const char* pad_id(int p);     // JSON value, e.g. "LeftTrigger"
const char* pad_label(int p);  // for the UI
int pad_from_id(const std::string& id);  // -1 if unknown

// Keyboard: macOS virtual key codes (kVK_*), 0..255
constexpr int kNoKey = -1;
constexpr int kKeysPerAction = 2;
std::string key_id(int code);     // JSON value, e.g. "K", "Space", "LeftShift"; "Key93" for unnamed codes
std::string key_label(int code);  // for the UI, e.g. "Left Shift", "↑"
int key_from_id(const std::string& id);  // kNoKey if unknown

// Keys the app already uses as single-key shortcuts (menu_hotkey in gfx/menu.mm) or that the
// Controls window reserves (Escape cancels a capture). Returns what the key does, or nullptr.
const char* reserved_key(int code);

struct Mapping {
    std::array<std::array<int, kKeysPerAction>, kActionCount> keys;  // kNoKey = empty slot
    std::array<int, kActionCount> pad;                                // kPadNone = unbound
    float deadzone = 0.0f;          // radial dead zone for controller sticks, 0..0.9
    bool invert_camera_y = false;   // right stick Y, keyboard and controller
    bool face_auto = false;         // Automatic face preset: follow the dominant pad's labels
    static Mapping defaults();
    bool operator==(const Mapping&) const = default;
};

// Which host face buttons drive the Wii U's A/B/X/Y (issue #78). Not stored: it is the shape of
// the four face bindings, so a hand-edited mapping simply reads back as kCustom.
//   kPosition (default): by position — the bottom host button (Xbox A) is the Wii U's B, the right
//     one (Xbox B) is the Wii U's A, and X/Y are swapped the same way (Nintendo layout).
//   kLabels: by label — the host button named A is the Wii U's A (Xbox convention: A accepts,
//     B goes back; the X and Y items follow the printed labels too).
//   kCustom: the four face bindings match neither preset.
enum class FaceLayout { kPosition, kLabels, kCustom };
FaceLayout face_layout(const Mapping& m);
void apply_face_layout(Mapping& m, FaceLayout layout);  // rewrites pad[kA..kY]; kCustom is a no-op
const char* face_layout_label(FaceLayout l);            // for the UI

// The Automatic preset (the follow-up to #97): the four face bindings follow whatever is printed
// on the dominant controller's buttons, so an Xbox pad plays by label and a Nintendo pad by
// position (where the two agree). face_auto is the one preset state that is stored — the two
// manual presets stay the shape of the four bindings — and the shape it resolved to is what
// face_layout() reports, so the drawings and the manual presets keep working unchanged.
// The labels of one pad's four face buttons, in the order the Pad enum names them (by Xbox
// position): [0] south (A), [1] east (B), [2] west (X), [3] north (Y). Cross/Circle/Square/
// Triangle are the PlayStation names for those same four letters.
enum class FaceLabel { kUnknown, kA, kB, kX, kY, kCross, kCircle, kSquare, kTriangle };

// The letter one button has printed on it, from the name a host gives it: an SF Symbol name
// ("a.circle", "xmark.circle", "square.circle.fill", "x.square.fill") or a localized name
// ("Button A", "Cross Button"). The first dot-separated component is the symbol's own name and
// what follows is decoration — so "square.circle" is Square (X) and not Circle (B) — and words
// like "button" are ignored in a free-form name. kUnknown when the name says nothing.
FaceLabel face_label_from_text(const char* text);

// The face bindings a pad with these labels asks for: each printed letter drives the Wii U button
// of that letter (Cross is A, Circle is B, and so on). A letter no button carries falls back to the
// by-position preset. Returns false and leaves `out` alone when no label is known at all (there is
// nothing to follow).
bool face_bindings_from_labels(const FaceLabel labels[4], int out[4]);

// The platform hosts call this whenever the dominant controller's labels may have changed
// (connect, disconnect, startup). With face_auto on it rewrites pad[kA..kY] and saves; otherwise
// it only remembers the labels for the next set_face_auto. An all-unknown report changes nothing.
void note_face_labels(const FaceLabel labels[4]);

// Turn the Automatic preset on or off on `m`. On rewrites pad[kA..kY] from the labels
// note_face_labels last reported (a mapping is ready before the next pad connects); with none
// reported the bindings stay as they are. apply_face_layout turns it off: a preset is a choice.
void set_face_auto(Mapping& m, bool on);

// Rewrite one controller binding. A hand-edited face binding (kA..kY) drops the Automatic preset,
// so a custom binding is not overwritten when the next pad connects.
void set_pad_binding(Mapping& m, int action, int pad);

// actions (other than `except`) that use this key / controller input
std::vector<int> key_users(const Mapping& m, int code, int except = -1);
std::vector<int> pad_users(const Mapping& m, int pad, int except = -1);
// true if a key or the controller input bound to `a` is also bound to another action
bool has_conflict(const Mapping& m, int a);
// keys + controller inputs bound to more than one action (each counted once)
int conflict_count(const Mapping& m);
// short labels for compact UI chips, e.g. "L ⇧", "Num 5" / "LB", "LS ↑"
std::string key_short_label(int code);
const char* pad_short_label(int p);

// Evaluate a mapping. keys[code] = held; values[p] = 0..1 for each Pad input (max over controllers)
input::PadState keyboard_state(const Mapping& m, const bool keys[256]);
input::PadState controller_state(const Mapping& m, const float values[kPadCount]);

// JSON. from_json starts from defaults: inputs missing from the file keep their default binding.
std::string to_json(const Mapping& m);
bool from_json(const std::string& text, Mapping& out, std::string* error = nullptr);
bool load_file(const std::string& path, Mapping& out, std::string* error = nullptr);
bool save_file(const std::string& path, const Mapping& m);
std::string default_path();

// The live mapping (thread safe). load_startup() reads default_path() if it exists.
void load_startup();
Mapping current();
void set_current(const Mapping& m, bool save = true);  // applies immediately; saves to default_path()
uint32_t generation();  // bumps on every set_current

}  // namespace input_map
