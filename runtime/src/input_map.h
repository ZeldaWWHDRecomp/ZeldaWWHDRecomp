// Controls mapping: which keyboard/mouse and host-controller inputs drive each Wii U GamePad input.
//
// Plain C++ (no AppKit) so it can be unit-tested (runtime/tools/input_map_test.cpp). input.mm
// evaluates the current mapping on every read; the Controls window (gfx/controls_ui.mm) edits it.
// Persisted as JSON in ~/Library/Application Support/WWHD/controls.json ($WWHD_CONTROLS overrides).
//
// Format 2 (P6 of the controls plan; idea from the GreenNaugahyde Android fork, reimplemented):
//   - per-controller bindings: "controllers": [{"guid", "name", "controller": {...}}]. A controller
//     uses the profile with its SDL GUID and name, else one with its GUID, else one with its name (a
//     host without GUIDs, or the same model on another bus), else the shared "controller" bindings;
//   - the virtual actions Jump and MoonJump (Jump anywhere, the moon jump): bound like Wii U buttons,
//     never sent to the game (input::PadState::virt);
//   - "options": "l_turns_gamepad_page" (written only when on) and the controllers that asked not to
//     be offered the controller setup again ("controller_setup_declined").
// A format 1 file (no "version": 2) reads as before; its Jump and MoonJump take the inputs of the
// Wii U's L (where both mods used to sit), and the old Jump anywhere button choice is applied by
// adopt_legacy_jump_button().
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
    kJump, kMoonJump,                // virtual inputs (input::kVirt*): Jump anywhere, the moon jump
    kActionCount
};
constexpr int kFirstVirtual = kJump;
inline bool is_virtual(int a) { return a == kJump || a == kMoonJump; }
// a Wii U button the game does not use while Link plays (L only turns the GamePad page, d-pad down
// is free): a virtual action or a moved item button may share its input (see play_buttons)
inline bool free_in_play(int a) { return a == kL || a == kDDown; }
inline bool is_item_action(int a) { return a == kX || a == kY || a == kR; }
uint32_t action_virtual(int a);   // input::kVirt* bit, 0 for the others
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
    kPadCapture, kPadRightPaddle1, kPadLeftPaddle1, kPadRightPaddle2, kPadLeftPaddle2, kPadTouchpad,
    kPadMisc2, kPadMisc3, kPadMisc4, kPadMisc5, kPadMisc6,
    kPadCount
};
const char* pad_id(int p);     // JSON value, e.g. "LeftTrigger"
const char* pad_label(int p);  // for the UI
int pad_from_id(const std::string& id);  // -1 if unknown

// Keyboard: macOS virtual key codes (kVK_*), 0..255; mouse inputs use 256..262.
// Mouse IDs share the two keyboard binding slots, above the legacy 0..255 key range.
constexpr int kMouse1 = 256, kMouse2 = 257, kMouse3 = 258, kMouse4 = 259, kMouse5 = 260;
constexpr int kWheelUp = 261, kWheelDown = 262, kInputCount = 263;
constexpr int kNoKey = -1;
constexpr int kKeysPerAction = 2;
std::string key_id(int code);     // JSON value, e.g. "K", "Space", "LeftShift"; "Key93" for unnamed codes
std::string key_label(int code);  // for the UI, e.g. "Left Shift", "↑"
int key_from_id(const std::string& id);  // kNoKey if unknown

// Keys the app already uses as single-key shortcuts (menu_hotkey in gfx/menu.mm) or that the
// Controls window reserves (Escape cancels a capture). Returns what the key does, or nullptr.
const char* reserved_key(int code);

constexpr float kMinStickRange = 1.0f, kMaxStickRange = 1.5f;

// Host actions share controls.json, but do not drive GamePad inputs.
enum Shortcut { kSettings, kFullscreen, kPhoto, kSave1, kSave2, kSave3, kSave4, kSave5,
    kLoad1, kLoad2, kLoad3, kLoad4, kLoad5, kResolution, kAO, kAOHires, kAniso,
    kInterpolation, kTrue60, kFXAA, kPiP, kPerformance, kMute, kVolumeDown, kVolumeUp, kShortcutCount };
enum KeyModifier { kModShift = 1, kModCtrl = 2, kModAlt = 4, kModSuper = 8 };
struct ShortcutKey {
    int code = kNoKey, mods = 0;
    bool operator==(const ShortcutKey&) const = default;
};
struct ShortcutBinding {
    std::array<ShortcutKey, 2> keys{};
    int hold = kPadNone, button = kPadNone;
    bool operator==(const ShortcutBinding&) const = default;
};
struct ShortcutInfo { const char* id; const char* name; const char* group; ShortcutKey primary, alternate; int button = kPadNone; };
const ShortcutInfo& shortcut_info(int action);
std::array<ShortcutBinding, kShortcutCount> shortcut_defaults();
// controls.json "shortcuts_version": 2 shows Home under Settings, 3 moves an untouched F1 Settings key to Esc + F1.
constexpr int kShortcutsVersion = 3;
std::string shortcut_key_label(ShortcutKey key);

struct Mapping {
    std::array<ShortcutBinding, kShortcutCount> shortcuts = shortcut_defaults();
    std::array<std::array<int, kKeysPerAction>, kActionCount> keys;  // kNoKey = empty slot
    std::array<int, kActionCount> pad;                                // kPadNone = unbound
    // Real sticks do not reliably return exact zero. In particular, a tiny residual zoom input
    // keeps the Picto Box's zoom-blur preset active. Format 2 saves preserve an explicit zero;
    // unmarked saves migrate the former zero default to this value.
    float deadzone = 0.15f;         // radial dead zone for controller sticks, 0..0.9
    // Stick range (issue #40), per stick, kMinStickRange..kMaxStickRange. Applied after the dead
    // zone: the stick vector's length is multiplied by the range and capped at 1, direction kept,
    // so a worn stick that only reaches 0.9 delivers full deflection at about 1.11.
    float left_range = 1.0f;
    float right_range = 1.0f;
    bool file_select_quit = true; // show the controller/mouse Quit button on file select
    bool skip_controller_selection = true; // title: choose the configured Wii U controller
    // Issue #267: controllers keep driving the game while no game window has the focus (a second
    // screen, streaming, remote play). Controllers only: keyboard and mouse are still released on
    // focus loss. Off by default; the host applies it (SDL's background-events hint, GameController's
    // shouldMonitorBackgroundEvents).
    bool background_controllers = false;
    // Camera only (issue #167): applied where the game's camera reads the right stick
    // (camera_invert.h, mods/camera.cpp), never to the stick itself, so the baton is not inverted
    bool invert_camera_y = false;
    bool face_auto = false;         // Automatic face preset: follow the dominant pad's labels
    // In-game button prompts (issues #110, #141, #143; runtime/src/button_glyphs.cpp)
    enum class Prompts : uint8_t { kAuto, kOriginal, kXbox, kPlayStation };
    enum class ShoulderNames : uint8_t { kAuto, kNintendo, kNumbered, kXbox };
    Prompts prompts = Prompts::kAuto;            // face-button look while the face buttons are by label
    ShoulderNames shoulder_names = ShoulderNames::kAuto;
    bool prompt_colours = true;                  // coloured face buttons (Xbox / PlayStation colours)
    // Controls > Controller > "L turns the GamePad page" (off): in play the game reads L only to turn
    // its GamePad screen between Items and Map; off, L reaches the game only outside play, so L1 can
    // jump without turning the page (play_buttons)
    bool l_turns_page = false;
    // per-controller bindings (format 2): see profile_index()
    struct Controller {
        std::string guid;   // SDL GUID (32 hex digits); empty on hosts without one (AppKit)
        std::string name;   // as the host names the controller
        std::array<int, kActionCount> pad{};
        bool operator==(const Controller&) const = default;
    };
    std::vector<Controller> controllers;
    std::vector<std::string> setup_declined;  // controller_key()s that asked "Don't ask again"
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
// the controller input that is the Wii U's A, B, X or Y (action kA..kY) in the current mapping, for
// the overlay's own menus and on-screen keyboard (confirm/back as in the game); unbound: by position
int face_input(int action);

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

// GameController's Nintendo face elements use printed letters, unlike our positional Pad
// inputs. A single sideways Joy-Con has a different permutation from a Pro / paired pad.
// Name parsing and routing are pure so macOS hardware is not needed for regression tests.
enum class AppleFaceLayout { kStandard, kNintendo, kJoyConSingle };
AppleFaceLayout apple_face_layout(const char* product_category, const char* vendor_name);
int apple_face_input(AppleFaceLayout layout, int pad);  // non-face inputs pass through
void apple_face_labels(AppleFaceLayout layout, FaceLabel labels[4]);  // raw A/B/X/Y -> positions
// A possible pre-#181 manual workaround; report it, never silently overwrite an intentional
// by-label choice (or a shared mapping used with another controller).
bool apple_face_workaround(const Mapping& m, AppleFaceLayout layout);

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

// The dominant pad's family, from the labels note_face_labels last reported: PlayStation when it
// names Cross/Circle/Square/Triangle, Xbox when its bottom button is A, Nintendo when it is B.
enum class PadFamily { kUnknown, kXbox, kPlayStation, kNintendo };
PadFamily pad_family_from_labels(const FaceLabel labels[4]);
PadFamily pad_family();

// What the in-game button prompts look like (button_glyphs.cpp draws it). Resolved from the
// mapping and the pad family; pure, so input_map_test and button_glyphs_test can cover it.
//   face: appearance and host-button identities follow the resolved face bindings. Automatic
//     follows Xbox/PlayStation; position/custom on Nintendo or unknown retain original icons.
//     By label retains its existing Xbox fallback. Original never changes face prompts.
//   shoulders: Automatic follows the face style (Xbox: LB/RB/LT/RT, PlayStation: L1/R1/L2/R2,
//     original: the game's L/R/ZL/ZR); a fixed choice applies with any face layout.
enum class FaceStyle : uint8_t { kOriginal, kXbox, kPlayStation };
enum class ShoulderStyle : uint8_t { kOriginal, kNumbered, kXbox };
struct PromptStyle {
    FaceStyle face = FaceStyle::kOriginal;
    bool colours = false;         // face colours (only with a face style)
    ShoulderStyle shoulders = ShoulderStyle::kOriginal;
    bool positions = false;       // HUD face cluster in the Xbox/PlayStation positions
    std::array<int, 4> buttons = {0, 1, 2, 3};  // Wii U face -> host A/B/X/Y
    bool active() const { return face != FaceStyle::kOriginal || shoulders != ShoulderStyle::kOriginal; }
    bool operator==(const PromptStyle&) const = default;
};
PromptStyle prompt_style(const Mapping& m, PadFamily family);
const char* prompts_label(Mapping::Prompts p);              // for the UI
const char* shoulder_names_label(Mapping::ShoulderNames n);  // for the UI

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

// Evaluate a mapping. keys[code] = held (count entries, 256 for legacy keyboard callers).
// values[p] = 0..1 for each Pad input (max over controllers).
input::PadState keyboard_state(const Mapping& m, const bool* keys, int count = 256);
input::PadState controller_state(const Mapping& m, const float values[kPadCount]);
input::PadState controller_state(const Mapping& m, const float values[kPadCount], const std::array<int, kActionCount>& pad);

// ---- per-controller bindings (format 2) ----
std::string controller_key(const std::string& guid, const std::string& name);  // "guid name"
// the profile a controller uses: the same GUID and name, else the same GUID, else the same name; -1: none
int profile_index(const Mapping& m, const std::string& guid, const std::string& name);
// the controller bindings a controller plays with: its profile's, else the shared ones
const std::array<int, kActionCount>& pads_for(const Mapping& m, const std::string& guid, const std::string& name);
// give a controller its own bindings (a copy of the shared ones), or find the ones it has; the index
int ensure_profile(Mapping& m, const std::string& guid, const std::string& name);
void remove_profile(Mapping& m, int index);
// the Controls tab edits one set of controller bindings: `view` is `m` with profile `index`'s bindings
// in pad (-1: the shared ones); store_view puts an edited view back (the profile gets pad, everything
// else is shared)
Mapping view_for(const Mapping& m, int index);
void store_view(Mapping& m, const Mapping& view, int index);
// controller setup offer (controller_setup.h): a new controller (no profile, not declined, not virtual)
bool setup_offer_due(const Mapping& m, const std::string& guid, const std::string& name, bool is_virtual);

// several controllers at once, each through its own bindings: buttons and virtual inputs add up,
// each stick follows the controller that pushes it furthest
input::PadState combine(const input::PadState& a, const input::PadState& b);

// ---- virtual actions on game buttons (Jump anywhere, the moon jump) ----
// Binding Jump or the moon jump to a controller input that a game button uses asks what happens to
// that button's own function:
//   kMove: to a free input, L1 first, else d-pad down (both unused in play);
//   kMoveToL1: the button goes to L1 (with its item, for an item button); L1 then no longer presses
//     the Wii U's L (no page turns with L1, as on the fork);
//   kMoveToDDown: to d-pad down, which stays d-pad down outside play (play_buttons);
//   kTakeOff: no controller input presses it any more.
enum class Displace { kMove, kMoveToL1, kMoveToDDown, kTakeOff };
// the game actions a virtual action would take `pad` from (free-in-play and virtual ones share it)
std::vector<int> displaced_by(const Mapping& m, int pad);
// where kMove would put action `a` (kPadNone: nowhere free)
int move_target(const Mapping& m, int a);
// bind virtual action `v` to `pad`, doing `d` with the displaced game actions; false: nothing changed
bool bind_virtual(Mapping& m, int v, int pad, Displace d);
// The buttons the game gets in play or outside it: L leaves in play unless l_turns_page; an input
// shared by a game button and L or d-pad down presses, in play, the game button only and, outside
// play, L or d-pad down only.
uint32_t play_buttons(const Mapping& m, uint32_t buttons, bool in_play);
// Format 1 migration: Jump on the inputs of the Wii U button `vpad_bit` (the old Jump anywhere button
// choice, one of L, R, ZL, ZR, L3, R3). The game button keeps them too, as it did.
void jump_from_legacy(Mapping& m, uint32_t vpad_bit);

// JSON. from_json starts from defaults: inputs missing from the file keep their default binding.
std::string to_json(const Mapping& m);
bool from_json(const std::string& text, Mapping& out, std::string* error = nullptr);
int json_version(const std::string& text);  // the file's "version" (1 when missing or unreadable)
bool load_file(const std::string& path, Mapping& out, std::string* error = nullptr);
bool save_file(const std::string& path, const Mapping& m);
std::string default_path();

// Settings has a remappable Home default and a held-Select fallback unless Select is a modifier.
bool settings_reachable(const Mapping& m, bool controller_settings = true);
bool shortcut_conflict(const Mapping& m, int action);
int shortcut_for_key(const Mapping& m, int code, int mods);
// A single button or complete combination consumes its inputs only while held. One activation per press.
struct ShortcutPoll {
    std::array<bool, kShortcutCount> held{};
    std::vector<int> poll(const Mapping& m, const float* raw, float* game);
};
void consume_shortcut_buttons(const Mapping& m, const float* raw, float* game);

// The live mapping (thread safe). load_startup() reads default_path() if it exists.
void load_startup();
Mapping current();
void set_current(const Mapping& m, bool save = true);  // applies immediately; saves to default_path()
uint32_t generation();  // bumps on every set_current
bool skip_controller_selection();
bool background_controllers();  // current().background_controllers, any thread (hosts poll it)
bool file_select_quit();
bool invert_camera_y();  // current().invert_camera_y without copying the mapping (camera hooks)
bool l_turns_page();     // current().l_turns_page, any thread
// The old Jump anywhere button (mods: traversal_settings): applied once to a format 1 (or missing)
// controls.json; a format 2 file has its own Jump bindings and ignores it.
void adopt_legacy_jump_button(uint32_t vpad_bit);
// The controller in use (hosts: the one that pressed something last), for the overlay's confirm and
// back buttons (face_input) and the Controls tab's first choice. Empty: none.
void set_active_controller(const std::string& guid, const std::string& name);
void active_controller(std::string& guid, std::string& name);

}  // namespace input_map
