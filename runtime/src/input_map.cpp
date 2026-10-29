// Controls mapping model, JSON persistence and evaluation (see input_map.h).
#include "input_map.h"

#include "platform/keycodes.h"
#include "platform/host.h"
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace input_map {

// ---- names

namespace {
struct ActionInfo { const char* id; const char* label; uint32_t bit; };
const ActionInfo kActions[kActionCount] = {
    {"A", "A", input::kA}, {"B", "B", input::kB}, {"X", "X", input::kX}, {"Y", "Y", input::kY},
    {"L", "L", input::kL}, {"R", "R", input::kR}, {"ZL", "ZL", input::kZL}, {"ZR", "ZR", input::kZR},
    {"Plus", "+ (Start)", input::kPlus}, {"Minus", "− (Select)", input::kMinus}, {"Home", "Home", input::kHome},
    {"DpadUp", "D-pad ↑", input::kUp}, {"DpadDown", "D-pad ↓", input::kDown},
    {"DpadLeft", "D-pad ←", input::kLeft}, {"DpadRight", "D-pad →", input::kRight},
    {"LeftStickClick", "Left stick click", input::kStickL}, {"RightStickClick", "Right stick click", input::kStickR},
    {"LeftStickUp", "Left stick ↑ (move)", 0}, {"LeftStickDown", "Left stick ↓ (move)", 0},
    {"LeftStickLeft", "Left stick ← (move)", 0}, {"LeftStickRight", "Left stick → (move)", 0},
    {"RightStickUp", "Right stick ↑ (camera)", 0}, {"RightStickDown", "Right stick ↓ (camera)", 0},
    {"RightStickLeft", "Right stick ← (camera)", 0}, {"RightStickRight", "Right stick → (camera)", 0},
    {"Screenshot", "Screenshot (app)", 0},
    {"Jump", "Jump anywhere (mod)", 0}, {"MoonJump", "Moon jump (cheat)", 0},
};

struct PadInfo { const char* id; const char* label; };
const PadInfo kPads[kPadCount] = {
    {"", ""},
    {"A", "A / Cross (bottom)"}, {"B", "B / Circle (right)"}, {"X", "X / Square (left)"}, {"Y", "Y / Triangle (top)"},
    {"LeftShoulder", "LB / L1"}, {"RightShoulder", "RB / R1"}, {"LeftTrigger", "LT / L2"}, {"RightTrigger", "RT / R2"},
    {"Menu", "Menu / Options"}, {"Options", "View / Share"}, {"Home", "Home / Guide"},
    {"LeftStickButton", "Left stick click"}, {"RightStickButton", "Right stick click"},
    {"DpadUp", "D-pad ↑"}, {"DpadDown", "D-pad ↓"}, {"DpadLeft", "D-pad ←"}, {"DpadRight", "D-pad →"},
    {"LeftStickUp", "Left stick ↑"}, {"LeftStickDown", "Left stick ↓"},
    {"LeftStickLeft", "Left stick ←"}, {"LeftStickRight", "Left stick →"},
    {"RightStickUp", "Right stick ↑"}, {"RightStickDown", "Right stick ↓"},
    {"RightStickLeft", "Right stick ←"}, {"RightStickRight", "Right stick →"},
    {"Capture", "Capture / Share"}, {"RightPaddle1", "Right paddle 1"}, {"LeftPaddle1", "Left paddle 1"},
    {"RightPaddle2", "Right paddle 2"}, {"LeftPaddle2", "Left paddle 2"}, {"Touchpad", "Touchpad click"},
    {"Misc2", "Extra button 2"}, {"Misc3", "Extra button 3"}, {"Misc4", "Extra button 4"},
    {"Misc5", "Extra button 5"}, {"Misc6", "Extra button 6"},
};

struct KeyInfo { int code; const char* id; const char* label; };
const KeyInfo kKeys[] = {
    {kVK_ANSI_A, "A", "A"}, {kVK_ANSI_B, "B", "B"}, {kVK_ANSI_C, "C", "C"}, {kVK_ANSI_D, "D", "D"},
    {kVK_ANSI_E, "E", "E"}, {kVK_ANSI_F, "F", "F"}, {kVK_ANSI_G, "G", "G"}, {kVK_ANSI_H, "H", "H"},
    {kVK_ANSI_I, "I", "I"}, {kVK_ANSI_J, "J", "J"}, {kVK_ANSI_K, "K", "K"}, {kVK_ANSI_L, "L", "L"},
    {kVK_ANSI_M, "M", "M"}, {kVK_ANSI_N, "N", "N"}, {kVK_ANSI_O, "O", "O"}, {kVK_ANSI_P, "P", "P"},
    {kVK_ANSI_Q, "Q", "Q"}, {kVK_ANSI_R, "R", "R"}, {kVK_ANSI_S, "S", "S"}, {kVK_ANSI_T, "T", "T"},
    {kVK_ANSI_U, "U", "U"}, {kVK_ANSI_V, "V", "V"}, {kVK_ANSI_W, "W", "W"}, {kVK_ANSI_X, "X", "X"},
    {kVK_ANSI_Y, "Y", "Y"}, {kVK_ANSI_Z, "Z", "Z"},
    {kVK_ANSI_0, "0", "0"}, {kVK_ANSI_1, "1", "1"}, {kVK_ANSI_2, "2", "2"}, {kVK_ANSI_3, "3", "3"},
    {kVK_ANSI_4, "4", "4"}, {kVK_ANSI_5, "5", "5"}, {kVK_ANSI_6, "6", "6"}, {kVK_ANSI_7, "7", "7"},
    {kVK_ANSI_8, "8", "8"}, {kVK_ANSI_9, "9", "9"},
    {kVK_ANSI_Minus, "Minus", "-"}, {kVK_ANSI_Equal, "Equal", "="}, {kVK_ANSI_LeftBracket, "LeftBracket", "["},
    {kVK_ANSI_RightBracket, "RightBracket", "]"}, {kVK_ANSI_Backslash, "Backslash", "\\"},
    {kVK_ANSI_Semicolon, "Semicolon", ";"}, {kVK_ANSI_Quote, "Quote", "'"}, {kVK_ANSI_Comma, "Comma", ","},
    {kVK_ANSI_Period, "Period", "."}, {kVK_ANSI_Slash, "Slash", "/"}, {kVK_ANSI_Grave, "Grave", "`"},
    {kVK_ISO_Section, "Section", "§"},
    {kVK_Space, "Space", "Space"}, {kVK_Return, "Return", "Return"}, {kVK_Tab, "Tab", "Tab"},
    {kVK_Delete, "Backspace", "Delete ⌫"}, {kVK_ForwardDelete, "ForwardDelete", "Forward Delete ⌦"},
    {kVK_Escape, "Escape", "Esc"},
    {kVK_Shift, "LeftShift", "Left Shift"}, {kVK_RightShift, "RightShift", "Right Shift"},
    {kVK_Control, "LeftControl", "Left Control"}, {kVK_RightControl, "RightControl", "Right Control"},
    {kVK_Option, "LeftOption", "Left Option"}, {kVK_RightOption, "RightOption", "Right Option"},
    {kVK_CapsLock, "CapsLock", "Caps Lock"},
    {kVK_UpArrow, "Up", "↑"}, {kVK_DownArrow, "Down", "↓"},
    {kVK_LeftArrow, "Left", "←"}, {kVK_RightArrow, "Right", "→"},
    {kVK_Home, "HomeKey", "Home"}, {kVK_End, "End", "End"}, {kVK_PageUp, "PageUp", "Page Up"},
    {kVK_PageDown, "PageDown", "Page Down"},
    {kVK_F1, "F1", "F1"}, {kVK_F2, "F2", "F2"}, {kVK_F3, "F3", "F3"}, {kVK_F4, "F4", "F4"},
    {kVK_F5, "F5", "F5"}, {kVK_F6, "F6", "F6"}, {kVK_F7, "F7", "F7"}, {kVK_F8, "F8", "F8"},
    {kVK_F9, "F9", "F9"}, {kVK_F10, "F10", "F10"}, {kVK_F11, "F11", "F11"}, {kVK_F12, "F12", "F12"},
    {kVK_F13, "F13", "F13"}, {kVK_F14, "F14", "F14"}, {kVK_F15, "F15", "F15"}, {kVK_F16, "F16", "F16"},
    {kVK_ANSI_Keypad0, "Keypad0", "Keypad 0"}, {kVK_ANSI_Keypad1, "Keypad1", "Keypad 1"},
    {kVK_ANSI_Keypad2, "Keypad2", "Keypad 2"}, {kVK_ANSI_Keypad3, "Keypad3", "Keypad 3"},
    {kVK_ANSI_Keypad4, "Keypad4", "Keypad 4"}, {kVK_ANSI_Keypad5, "Keypad5", "Keypad 5"},
    {kVK_ANSI_Keypad6, "Keypad6", "Keypad 6"}, {kVK_ANSI_Keypad7, "Keypad7", "Keypad 7"},
    {kVK_ANSI_Keypad8, "Keypad8", "Keypad 8"}, {kVK_ANSI_Keypad9, "Keypad9", "Keypad 9"},
    {kVK_ANSI_KeypadDecimal, "KeypadDecimal", "Keypad ."}, {kVK_ANSI_KeypadMultiply, "KeypadMultiply", "Keypad *"},
    {kVK_ANSI_KeypadPlus, "KeypadPlus", "Keypad +"}, {kVK_ANSI_KeypadMinus, "KeypadMinus", "Keypad -"},
    {kVK_ANSI_KeypadDivide, "KeypadDivide", "Keypad /"}, {kVK_ANSI_KeypadEnter, "KeypadEnter", "Keypad Enter"},
    {kVK_ANSI_KeypadEquals, "KeypadEquals", "Keypad ="}, {kVK_ANSI_KeypadClear, "KeypadClear", "Keypad Clear"},
};
const KeyInfo* find_key(int code) {
    for (auto& k : kKeys)
        if (k.code == code) return &k;
    return nullptr;
}
}  // namespace

const char* action_id(int a) { return a >= 0 && a < kActionCount ? kActions[a].id : ""; }
const char* action_label(int a) { return a >= 0 && a < kActionCount ? kActions[a].label : ""; }
uint32_t action_bit(int a) { return a >= 0 && a < kActionCount ? kActions[a].bit : 0; }
uint32_t action_virtual(int a) { return a == kJump ? input::kVirtJump : a == kMoonJump ? input::kVirtMoonJump : 0; }
int action_from_id(const std::string& id) {
    for (int a = 0; a < kActionCount; a++)
        if (id == kActions[a].id) return a;
    return -1;
}

const char* pad_id(int p) { return p >= 0 && p < kPadCount ? kPads[p].id : ""; }
const char* pad_label(int p) {
    static const char* const nintendo[] = {"B (bottom)", "A (right)", "Y (left)", "X (top)"};
    if (p >= kPadA && p <= kPadY && pad_family() == PadFamily::kNintendo) return nintendo[p - kPadA];
    return p > 0 && p < kPadCount ? kPads[p].label : "";
}
int pad_from_id(const std::string& id) {
    if (id.empty()) return kPadNone;
    for (int p = 1; p < kPadCount; p++)
        if (id == kPads[p].id) return p;
    return -1;
}

static const char* mouse_ids[] = {"Mouse1", "Mouse2", "Mouse3", "Mouse4", "Mouse5", "WheelUp", "WheelDown"};
static const char* mouse_labels[] = {"Left mouse button", "Right mouse button", "Middle mouse button", "Mouse 4", "Mouse 5", "Wheel up", "Wheel down"};
std::string key_id(int code) {
    if (code >= kMouse1 && code < kInputCount) return mouse_ids[code-kMouse1];
    if (code < 0 || code > 255) return "";
    if (auto* k = find_key(code)) return k->id;
    return "Key" + std::to_string(code);
}
std::string key_label(int code) {
    if (code >= kMouse1 && code < kInputCount) return mouse_labels[code-kMouse1];
    if (code < 0 || code > 255) return "";
    if (auto* k = find_key(code)) return k->label;
    return "Key " + std::to_string(code);
}
int key_from_id(const std::string& id) {
    for (int i = kMouse1; i < kInputCount; ++i) if (id == mouse_ids[i-kMouse1]) return i;
    for (auto& k : kKeys)
        if (id == k.id) return k.code;
    if (id.size() > 3 && id.compare(0, 3, "Key") == 0) {
        char* end;
        long v = strtol(id.c_str() + 3, &end, 10);
        if (!*end && v >= 0 && v <= 255) return (int)v;
    }
    return kNoKey;
}

const char* reserved_key(int code) {
    // keep in sync with gfx::menu_hotkey (gfx/menu.mm)
    switch (code) {
    case kVK_ANSI_9: return "Graphics › Record sound activity";
    case kVK_ANSI_P: case kVK_F12: return "Graphics › Capture frame";
    case kVK_Command: case kVK_RightCommand: return "menu shortcuts (⌘)";
    default: return nullptr;
    }
}

// All host action labels and defaults live here. F6 loads slot 1: no existing binding uses it.
// Settings: Esc is the long-term key. F1 stays as the alternate during the transition; dropping it
// later is this one line ({kVK_Escape}, {}). With a captured mouse the first Esc only frees the mouse
// (overlay::key).
static const ShortcutInfo kShortcuts[kShortcutCount] = {
    {"Settings", "Settings", "General", {kVK_Escape}, {kVK_F1}, kPadHome},
    {"Fullscreen", "Full screen", "General",
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
     {kVK_ANSI_F, kModSuper}, {kVK_ANSI_F, kModCtrl | kModSuper}},
#else
     {kVK_F11}, {kVK_Return, kModAlt}},
#endif
    {"Screenshot", "Screenshot", "General", {kVK_F10}, {}},
    {"Save1", "Save state 1", "Save states", {kVK_F1, kModShift}, {}},
    {"Save2", "Save state 2", "Save states", {kVK_F2, kModShift}, {}},
    {"Save3", "Save state 3", "Save states", {kVK_F3, kModShift}, {}},
    {"Save4", "Save state 4", "Save states", {kVK_F4, kModShift}, {}},
    {"Save5", "Save state 5", "Save states", {kVK_F5, kModShift}, {}},
    {"Load1", "Load state 1", "Save states", {kVK_F6}, {}},
    {"Load2", "Load state 2", "Save states", {kVK_F2}, {}},
    {"Load3", "Load state 3", "Save states", {kVK_F3}, {}},
    {"Load4", "Load state 4", "Save states", {kVK_F4}, {}},
    {"Load5", "Load state 5", "Save states", {kVK_F5}, {}},
    {"Resolution", "Internal resolution", "Graphics", {kVK_ANSI_R}, {}},
    {"AO", "Ambient occlusion", "Graphics", {kVK_ANSI_O}, {}},
    {"AOHires", "Full-size occlusion depth", "Graphics", {kVK_ANSI_M}, {}},
    {"Aniso", "Anisotropic filtering", "Graphics", {kVK_ANSI_N}, {}},
    {"Interpolation", "Frame interpolation", "Graphics", {kVK_ANSI_6}, {}},
    {"True60", "True 60 fps", "Graphics", {kVK_ANSI_7}, {}},
    {"FXAA", "FXAA", "Graphics", {kVK_ANSI_8}, {}},
    {"PiP", "GamePad picture-in-picture", "Display and audio", {}, {}},
    {"Performance", "Performance overlay", "Display and audio", {}, {}},
    {"Mute", "Mute", "Display and audio", {}, {}},
    {"VolumeDown", "Volume down", "Display and audio", {}, {}},
    {"VolumeUp", "Volume up", "Display and audio", {}, {}},
};
const ShortcutInfo& shortcut_info(int a) { return kShortcuts[a]; }
std::array<ShortcutBinding, kShortcutCount> shortcut_defaults() {
    std::array<ShortcutBinding, kShortcutCount> out{};
    for (int a = 0; a < kShortcutCount; ++a) {
        out[a].keys = {kShortcuts[a].primary, kShortcuts[a].alternate};
        out[a].button = kShortcuts[a].button;
    }
    return out;
}
std::string shortcut_key_label(ShortcutKey k) {
    if (k.code == kNoKey) return "Unbound";
    return std::string(k.mods & kModCtrl ? "Ctrl+" : "") + (k.mods & kModAlt ? "Alt+" : "") +
        (k.mods & kModShift ? "Shift+" : "") + (k.mods & kModSuper ? "Cmd+" : "") + key_label(k.code);
}
int shortcut_for_key(const Mapping& m, int code, int mods) {
    if (code == kNoKey) return -1;
    for (int a = 0; a < kShortcutCount; ++a)
        for (auto k : m.shortcuts[a].keys) if (k.code == code && k.mods == (mods & 15)) return a;
    return -1;
}
bool settings_reachable(const Mapping& m, bool controller_settings) {
    for (auto k : m.shortcuts[kSettings].keys) if (k.code >= 0 && k.code < kInputCount) return true;
    const auto& b = m.shortcuts[kSettings];
    if (b.button > 0 && b.button < kPadCount && b.hold >= 0 && b.hold < kPadCount && b.hold != b.button) return true;
    // The existing held-Select shortcut remains unless Select is used as a combination modifier.
    if (controller_settings) {
        for (const auto& binding : m.shortcuts) if (binding.hold == kPadOptions && binding.button) return false;
        return true;
    }
    return false;
}
bool shortcut_conflict(const Mapping& m, int a) {
    const auto& b = m.shortcuts[a];
    for (auto k : b.keys) {
        if (k.code == kNoKey) continue;
        if (reserved_key(k.code)) return true;
        // Game bindings do not test modifiers, so a plain or Shift shortcut shares their key. A Ctrl/Alt/Cmd
        // shortcut takes its key press before the game sees it (F11/Alt+Enter beside Plus on Enter).
        if (!(k.mods & (kModCtrl | kModAlt | kModSuper)))
            for (int g = 0; g < kScreenshot; ++g)
                for (int v : m.keys[g]) if (v == k.code) return true;
        for (int other = 0; other < kShortcutCount; ++other) if (other != a)
            for (auto v : m.shortcuts[other].keys) if (v == k) return true;
    }
    if (b.button) for (int other = 0; other < kShortcutCount; ++other) if (other != a) {
        const auto& v = m.shortcuts[other];
        if ((!b.hold && (v.hold==b.button || v.button==b.button)) ||
            (!v.hold && v.button && (b.hold==v.button || b.button==v.button))) return true;
        if ((v.hold == b.hold && v.button == b.button) || (v.hold == b.button && v.button == b.hold)) return true;
    }
    return false;
}
static bool combo_down(const ShortcutBinding& b, const float* raw) {
    return b.hold >= 0 && b.hold < kPadCount && b.button > 0 && b.button < kPadCount &&
        b.hold != b.button && (!b.hold || raw[b.hold] > 0.5f) && raw[b.button] > 0.5f;
}
void consume_shortcut_buttons(const Mapping& m, const float* raw, float* game) {
    for (const auto& b : m.shortcuts) if (combo_down(b, raw)) game[b.hold] = game[b.button] = 0;
}
std::vector<int> ShortcutPoll::poll(const Mapping& m, const float* raw, float* game) {
    std::vector<int> pressed;
    for (int a = 0; a < kShortcutCount; ++a) {
        bool down = combo_down(m.shortcuts[a], raw);
        if (down && !held[a]) pressed.push_back(a);
        held[a] = down;
    }
    consume_shortcut_buttons(m, raw, game);
    return pressed;
}

// ---- model

Mapping Mapping::defaults() {
    Mapping m;
    for (auto& k : m.keys) k.fill(kNoKey);
    m.pad.fill(kPadNone);
    auto key = [&](int a, int k0, int k1 = kNoKey) { m.keys[a] = {k0, k1}; };
    key(kA, kVK_ANSI_K, kVK_Space); key(kB, kVK_ANSI_J); key(kX, kVK_ANSI_L); key(kY, kVK_ANSI_I);
    key(kL, kVK_ANSI_Q); key(kR, kVK_ANSI_E); key(kZL, kVK_Shift); key(kZR, kVK_ANSI_C);
    key(kPlus, kVK_Return); key(kMinus, kVK_Tab); key(kHome, kVK_ANSI_H);
    key(kDUp, kVK_ANSI_1); key(kDDown, kVK_ANSI_2); key(kDLeft, kVK_ANSI_3); key(kDRight, kVK_ANSI_4);
    key(kStickLClick, kVK_ANSI_X); key(kStickRClick, kVK_ANSI_V);
    key(kLUp, kVK_ANSI_W); key(kLDown, kVK_ANSI_S); key(kLLeft, kVK_ANSI_A); key(kLRight, kVK_ANSI_D);
    key(kRUp, kVK_UpArrow); key(kRDown, kVK_DownArrow); key(kRLeft, kVK_LeftArrow); key(kRRight, kVK_RightArrow);
    key(kScreenshot, kVK_F10);  // free on every host (F11: SDL full screen, F12/P: debug frame capture)
    // Jump anywhere and the moon jump where they always were, on the L inputs: in play the game gets no
    // L from them (play_buttons), so L1 / Q jump without turning the GamePad page
    key(kJump, kVK_ANSI_Q); key(kMoonJump, kVK_ANSI_Q);
    // controllers map by position: the bottom face button (Xbox A) is the Wii U's B (face_layout)
    const int pads[kActionCount] = {
        kPadNone, kPadNone, kPadNone, kPadNone,  // A B X Y: face-layout preset, filled below
        kPadLB, kPadRB, kPadLT, kPadRT, kPadMenu, kPadOptions, kPadHome,
        kPadDUp, kPadDDown, kPadDLeft, kPadDRight, kPadL3, kPadR3,
        kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight, kPadRSUp, kPadRSDown, kPadRSLeft, kPadRSRight,
        kPadNone,  // Screenshot: no controller input by default (every button already plays)
        kPadLB, kPadLB,  // Jump, MoonJump: L1 (shared with the Wii U's L, see above)
    };
    for (int a = 0; a < kActionCount; a++) m.pad[a] = pads[a];
    apply_face_layout(m, FaceLayout::kPosition);
    return m;
}

// ---- face-button presets (issue #78): pad inputs for the actions kA, kB, kX, kY

namespace {
const int kFaceByPosition[4] = {kPadB, kPadA, kPadY, kPadX};  // by position (default)
const int kFaceByLabel[4] = {kPadA, kPadB, kPadX, kPadY};     // by label (Xbox)
}

FaceLayout face_layout(const Mapping& m) {
    bool pos = true, lab = true;
    for (int i = 0; i < 4; i++) {
        pos &= m.pad[kA + i] == kFaceByPosition[i];
        lab &= m.pad[kA + i] == kFaceByLabel[i];
    }
    return pos ? FaceLayout::kPosition : lab ? FaceLayout::kLabels : FaceLayout::kCustom;
}

void apply_face_layout(Mapping& m, FaceLayout layout) {
    if (layout == FaceLayout::kCustom) return;
    m.face_auto = false;  // a preset is a choice, not a pad's to make
    const int* src = layout == FaceLayout::kLabels ? kFaceByLabel : kFaceByPosition;
    for (int i = 0; i < 4; i++) m.pad[kA + i] = src[i];
}

int face_input(int action) {
    std::string guid, name;
    active_controller(guid, name);
    const Mapping m = current();
    const int p = guid.empty() && name.empty() ? m.pad[action] : pads_for(m, guid, name)[action];
    return p != kPadNone ? p : kFaceByPosition[action - kA];
}

const char* face_layout_label(FaceLayout l) {
    switch (l) {
    case FaceLayout::kPosition: return "by position (Nintendo)";
    case FaceLayout::kLabels: return "by label (Xbox)";
    default: return "custom";
    }
}

// ---- the Automatic preset: follow the labels printed on the dominant pad (issue #78)

namespace {
// Cross/Circle/Square/Triangle are the PlayStation names for the A/B/X/Y letters
int label_letter(FaceLabel l) {
    switch (l) {
    case FaceLabel::kA: case FaceLabel::kCross: return 0;
    case FaceLabel::kB: case FaceLabel::kCircle: return 1;
    case FaceLabel::kX: case FaceLabel::kSquare: return 2;
    case FaceLabel::kY: case FaceLabel::kTriangle: return 3;
    default: return -1;
    }
}

FaceLabel word_letter(const std::string& w) {
    if (w == "a" || w == "xmark" || w == "cross") return FaceLabel::kA;
    if (w == "b" || w == "circle") return FaceLabel::kB;
    if (w == "x" || w == "square") return FaceLabel::kX;
    if (w == "y" || w == "triangle") return FaceLabel::kY;
    return FaceLabel::kUnknown;
}
}  // namespace

FaceLabel face_label_from_text(const char* text) {
    if (!text || !*text) return FaceLabel::kUnknown;
    std::string t;
    for (const char* p = text; *p; p++) t.push_back((char)tolower((unsigned char)*p));
    // SF Symbols: the name first, the decoration after a dot ("a.circle", "x.square.fill"). The
    // name alone decides: "square.circle" is Square, not Circle.
    if (const FaceLabel l = word_letter(t.substr(0, t.find('.'))); l != FaceLabel::kUnknown) return l;
    // a free-form name ("Button A", "Cross Button", "xbox.button.a"): the one word that is a letter
    FaceLabel found = FaceLabel::kUnknown;
    std::string w;
    auto consider = [&](char c) {
        if (c >= 'a' && c <= 'z') {
            w.push_back(c);
            return;
        }
        if (w != "button" && w != "fill" && w != "grid" && w != "the" && found == FaceLabel::kUnknown)
            found = word_letter(w);
        w.clear();
    };
    for (char c : t) consider(c);
    consider(' ');  // the last word
    return found;
}

AppleFaceLayout apple_face_layout(const char* product_category, const char* vendor_name) {
    auto words = [](const char* text) {
        std::string out = " ";
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text ? text : ""); *p; ++p) {
            if (std::isalnum(*p)) out += char(std::tolower(*p));
            else if (out.back() != ' ') out += ' ';
        }
        return out + " ";
    };
    const std::string category = words(product_category), vendor = words(vendor_name);
    // A recognized standard profile wins over a vendor name (e.g. a Nintendo-style pad
    // connected in Xbox mode). Its GameController elements already represent positions.
    for (const char* family : {" xbox ", " dualsense ", " dualshock ", " playstation "})
        if (category.find(family) != std::string::npos) return AppleFaceLayout::kStandard;
    const std::string who = category + vendor;
    auto has = [&](const char* word) { return who.find(word) != std::string::npos; };
    // Do not infer the four-button diamond for other Nintendo retro controllers.
    if (has(" n64 ") || has(" nes ") || has(" genesis ") || has(" gamecube ")) return AppleFaceLayout::kStandard;
    const bool joycon = has(" joy con ") || has(" joycon ");
    if (joycon && !has(" l r ") && !has(" pair ") && (has(" l ") || has(" r ")))
        return AppleFaceLayout::kJoyConSingle;
    if (joycon || has(" nintendo ") || has(" switch pro ") || has(" snes ") || has(" super famicom "))
        return AppleFaceLayout::kNintendo;
    return AppleFaceLayout::kStandard;
}

int apple_face_input(AppleFaceLayout layout, int pad) {
    if (pad < kPadA || pad > kPadY) return pad;
    // SDL's MFi driver uses these same destinations for GCInputButtonA/B/X/Y.
    static constexpr int nintendo[] = {kPadB, kPadA, kPadY, kPadX};
    static constexpr int single[] = {kPadA, kPadX, kPadB, kPadY};
    return layout == AppleFaceLayout::kNintendo ? nintendo[pad - kPadA]
         : layout == AppleFaceLayout::kJoyConSingle ? single[pad - kPadA] : pad;
}

void apple_face_labels(AppleFaceLayout layout, FaceLabel labels[4]) {
    const std::array<FaceLabel, 4> raw = {labels[0], labels[1], labels[2], labels[3]};
    for (int i = 0; i < 4; ++i) labels[apple_face_input(layout, kPadA + i) - kPadA] = raw[i];
}

bool apple_face_workaround(const Mapping& m, AppleFaceLayout layout) {
    return layout == AppleFaceLayout::kNintendo && !m.face_auto && face_layout(m) == FaceLayout::kLabels;
}

bool face_bindings_from_labels(const FaceLabel labels[4], int out[4]) {
    // 0..3 are the face pads in the labels' order: kPadA (south), kPadB (east), kPadX, kPadY
    int bind[4] = {-1, -1, -1, -1};  // bind[Wii U letter] = the host face button driving it
    bool taken[4] = {false, false, false, false};
    int found = 0;
    for (int h = 0; h < 4; h++) {  // a printed letter drives the Wii U button of that letter
        const int letter = label_letter(labels[h]);
        if (letter < 0) continue;
        bind[letter] = h;
        taken[h] = true;
        found++;
    }
    if (!found) return false;
    for (int i = 0; i < 4; i++) {  // no printed letter: the by-position button, or the first free one
        if (bind[i] >= 0) continue;
        int h = kFaceByPosition[i] - kPadA;
        if (taken[h]) {
            for (h = 0; h < 4 && taken[h]; h++) {}
            if (h == 4) return false;  // unreachable: four letters, four buttons
        }
        bind[i] = h;
        taken[h] = true;
    }
    for (int i = 0; i < 4; i++) out[i] = kPadA + bind[i];
    return true;
}

PadFamily pad_family_from_labels(const FaceLabel labels[4]) {
    for (int i = 0; i < 4; i++)
        if (labels[i] == FaceLabel::kCross || labels[i] == FaceLabel::kCircle || labels[i] == FaceLabel::kSquare ||
            labels[i] == FaceLabel::kTriangle)
            return PadFamily::kPlayStation;
    if (labels[0] == FaceLabel::kA) return PadFamily::kXbox;      // south
    if (labels[0] == FaceLabel::kB) return PadFamily::kNintendo;
    return PadFamily::kUnknown;
}

std::array<int, 4> hud_spots(const Mapping& m) {
    // The game draws A east, B south, X north and Y west; host buttons are named by Xbox
    // position (A south, B east, X west, Y north), so host button h sits on face h ^ 1's spot.
    std::array<int, 4> spots = {-1, -1, -1, -1};
    unsigned taken = 0;
    for (int i = 0; i < 4; ++i) {
        const int button = m.pad[kA + i] - kPadA;
        if (button < 0 || button >= 4 || (taken & (1u << (button ^ 1)))) continue;
        spots[i] = button ^ 1;
        taken |= 1u << spots[i];
    }
    for (int i = 0; i < 4; ++i)  // not on a face button (or one taken already): its own spot if free
        if (spots[i] < 0 && !(taken & (1u << i))) { spots[i] = i; taken |= 1u << i; }
    for (int i = 0; i < 4; ++i) {
        if (spots[i] >= 0) continue;
        int free = 0;
        while (taken & (1u << free)) ++free;
        spots[i] = free;
        taken |= 1u << free;
    }
    return spots;
}

PromptStyle prompt_style(const Mapping& m, PadFamily family) {
    PromptStyle s;
    const bool labels = face_layout(m) == FaceLayout::kLabels;
    bool unambiguous = true;
    unsigned used = 0;
    for (int i = 0; i < 4; ++i) {
        const int button = m.pad[kA + i] - kPadA;
        // Non-face inputs and duplicate bindings cannot describe a four-button pad picture.
        // Fall back as a complete set rather than mixing original and restyled icons.
        if (button < 0 || button >= 4 || (used & (1u << button))) { unambiguous = false; break; }
        used |= 1u << button;
        s.buttons[i] = button;
    }
    if (unambiguous) {
        switch (m.prompts) {
        case Mapping::Prompts::kAuto:
            s.face = family == PadFamily::kPlayStation ? FaceStyle::kPlayStation
                   : labels || family == PadFamily::kXbox ? FaceStyle::kXbox : FaceStyle::kOriginal;
            break;
        case Mapping::Prompts::kXbox: s.face = FaceStyle::kXbox; break;
        case Mapping::Prompts::kPlayStation: s.face = FaceStyle::kPlayStation; break;
        case Mapping::Prompts::kOriginal: break;
        }
    }
    if (s.face == FaceStyle::kOriginal) s.buttons = {0, 1, 2, 3};
    s.positions = s.face != FaceStyle::kOriginal && labels;
    if (s.face != FaceStyle::kOriginal) s.spots = hud_spots(m);
    s.colours = s.face != FaceStyle::kOriginal && m.prompt_colours;
    s.text_colours = s.face != FaceStyle::kOriginal && m.prompt_text_colours;
    switch (m.shoulder_names) {
    case Mapping::ShoulderNames::kAuto:
        s.shoulders = s.face == FaceStyle::kXbox ? ShoulderStyle::kXbox
                    : s.face == FaceStyle::kPlayStation ? ShoulderStyle::kNumbered : ShoulderStyle::kOriginal;
        break;
    case Mapping::ShoulderNames::kNintendo: break;
    case Mapping::ShoulderNames::kNumbered: s.shoulders = ShoulderStyle::kNumbered; break;
    case Mapping::ShoulderNames::kXbox: s.shoulders = ShoulderStyle::kXbox; break;
    }
    return s;
}

const char* prompts_label(Mapping::Prompts p) {
    switch (p) {
    case Mapping::Prompts::kAuto: return "automatic";
    case Mapping::Prompts::kOriginal: return "original";
    case Mapping::Prompts::kXbox: return "Xbox";
    case Mapping::Prompts::kPlayStation: return "PlayStation";
    }
    return "?";
}

const char* shoulder_names_label(Mapping::ShoulderNames n) {
    switch (n) {
    case Mapping::ShoulderNames::kAuto: return "automatic";
    case Mapping::ShoulderNames::kNintendo: return "L / R / ZL / ZR";
    case Mapping::ShoulderNames::kNumbered: return "L1 / R1 / L2 / R2";
    case Mapping::ShoulderNames::kXbox: return "LB / RB / LT / RT";
    }
    return "?";
}

void set_pad_binding(Mapping& m, int action, int pad) {
    if (action < 0 || action >= kActionCount) return;
    m.pad[action] = pad;
    if (action <= kY) m.face_auto = false;  // a hand-edited face binding is nobody's preset
}

std::vector<int> key_users(const Mapping& m, int code, int except) {
    std::vector<int> v;
    if (code == kNoKey) return v;
    for (int a = 0; a < kActionCount; a++)
        if (a != except)
            for (int k : m.keys[a])
                if (k == code) { v.push_back(a); break; }
    return v;
}
std::vector<int> pad_users(const Mapping& m, int pad, int except) {
    std::vector<int> v;
    if (pad == kPadNone) return v;
    for (int a = 0; a < kActionCount; a++)
        if (a != except && m.pad[a] == pad) v.push_back(a);
    return v;
}

// Two actions may share an input without it counting as a conflict: virtual actions share with each
// other (the moon jump wins) and with the game buttons unused in play (L, d-pad down); a game button
// moved to d-pad down shares it (play_buttons gives it to one of them by the game's state).
static bool shares_ok(int a, int b) {
    if (is_virtual(a) && is_virtual(b)) return true;
    if (is_virtual(a) || is_virtual(b)) return free_in_play(is_virtual(a) ? b : a);
    if (a == kScreenshot || b == kScreenshot) return false;
    return (a == kDDown) != (b == kDDown) && !free_in_play(a == kDDown ? b : a);
}

bool has_conflict(const Mapping& m, int a) {
    if (a < 0 || a >= kActionCount) return false;
    for (int k : m.keys[a])
        if (k != kNoKey)
            for (int b : key_users(m, k, a))
                if (!shares_ok(a, b)) return true;
    if (a < kScreenshot) for (int k : m.keys[a]) if (k != kNoKey)
        for (const auto& b : m.shortcuts) for (auto key : b.keys)
            if (key.code == k && !(key.mods & (kModCtrl | kModAlt | kModSuper))) return true;
    if (m.pad[a] != kPadNone)
        for (int b : pad_users(m, m.pad[a], a))
            if (!shares_ok(a, b)) return true;
    return false;
}

static bool users_conflict(const std::vector<int>& users) {
    for (size_t i = 0; i < users.size(); i++)
        for (size_t j = i + 1; j < users.size(); j++)
            if (!shares_ok(users[i], users[j])) return true;
    return false;
}

int conflict_count(const Mapping& m) {
    int n = 0;
    for (int code = 0; code < kInputCount; code++)
        if (users_conflict(key_users(m, code))) n++;
    for (int p = 1; p < kPadCount; p++)
        if (users_conflict(pad_users(m, p))) n++;
    return n;
}

std::string key_short_label(int code) {
    if (code >= kMouse1 && code <= kMouse5) return "Mouse " + std::to_string(code-kMouse1+1);
    switch (code) {
    case kVK_Shift: return "L ⇧";
    case kVK_RightShift: return "R ⇧";
    case kVK_Control: return "L ⌃";
    case kVK_RightControl: return "R ⌃";
    case kVK_Option: return "L ⌥";
    case kVK_RightOption: return "R ⌥";
    case kVK_CapsLock: return "⇪";
    case kVK_Delete: return "⌫";
    case kVK_ForwardDelete: return "⌦";
    case kVK_PageUp: return "PgUp";
    case kVK_PageDown: return "PgDn";
    case kVK_ANSI_KeypadEnter: return "Num ⏎";
    default: break;
    }
    std::string l = key_label(code);
    if (l.compare(0, 7, "Keypad ") == 0) return "Num " + l.substr(7);
    return l;
}

const char* pad_short_label(int p) {
    static const char* const nintendo[] = {"B", "A", "Y", "X"};
    if (p >= kPadA && p <= kPadY && pad_family() == PadFamily::kNintendo) return nintendo[p - kPadA];
    static const char* const kShort[kPadCount] = {
        "", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Menu", "View", "Home", "L3", "R3",
        "D-pad ↑", "D-pad ↓", "D-pad ←", "D-pad →", "LS ↑", "LS ↓", "LS ←", "LS →", "RS ↑", "RS ↓", "RS ←", "RS →",
        "Capture", "R paddle 1", "L paddle 1", "R paddle 2", "L paddle 2", "Touchpad",
        "Extra 2", "Extra 3", "Extra 4", "Extra 5", "Extra 6",
    };
    return p > 0 && p < kPadCount ? kShort[p] : "";
}

input::PadState keyboard_state(const Mapping& m, const bool* keys, int count) {
    float v[kActionCount];
    for (int a = 0; a < kActionCount; a++) {
        v[a] = 0;
        for (int k : m.keys[a])
            if (k >= 0 && k < count && keys[k]) v[a] = 1;
    }
    input::PadState s;
    for (int a = 0; a < kActionCount; a++)
        if (v[a]) {
            s.buttons |= action_bit(a);
            s.virt |= action_virtual(a);
        }
    s.lx = v[kLRight] - v[kLLeft];
    s.ly = v[kLUp] - v[kLDown];
    s.rx = v[kRRight] - v[kRLeft];
    s.ry = v[kRUp] - v[kRDown];
    if (s.lx && s.ly) { s.lx *= 0.7071f; s.ly *= 0.7071f; }  // keep diagonal walking at full-stick length
    // invert_camera_y is not applied here: the camera applies it (camera_invert.h, issue #167)
    return s;
}

static void apply_deadzone(float& x, float& y, float dz) {
    float len = std::sqrt(x * x + y * y);
    if (dz <= 0) return;
    if (len <= dz) { x = y = 0; return; }
    float scale = std::min(1.0f, (len - dz) / (1.0f - dz)) / len;  // rescale so the edge still reaches 1
    x *= scale;
    y *= scale;
}

// issue #40: scale the vector's length by the range and cap it at full deflection, keeping
// the direction, so a stick that physically stops short still reaches 1.0.
static void apply_range(float& x, float& y, float range) {
    range = std::clamp(range, kMinStickRange, kMaxStickRange);
    if (range == 1.0f) return;
    float len = std::sqrt(x * x + y * y);
    if (len <= 0) return;
    float scale = std::min(len * range, 1.0f) / len;
    x *= scale;
    y *= scale;
}

input::PadState controller_state(const Mapping& m, const float values[kPadCount]) {
    return controller_state(m, values, m.pad);
}

input::PadState controller_state(const Mapping& m, const float values[kPadCount], const std::array<int, kActionCount>& pad) {
    float v[kActionCount];
    for (int a = 0; a < kActionCount; a++) {
        int p = pad[a];
        v[a] = p > kPadNone && p < kPadCount ? values[p] : 0;
    }
    input::PadState s;
    for (int a = 0; a < kActionCount; a++)
        if (v[a] > 0.5f) {
            s.buttons |= action_bit(a);
            s.virt |= action_virtual(a);
        }
    s.lx = v[kLRight] - v[kLLeft];
    s.ly = v[kLUp] - v[kLDown];
    s.rx = v[kRRight] - v[kRLeft];
    s.ry = v[kRUp] - v[kRDown];
    float dz = std::clamp(m.deadzone, 0.0f, 0.9f);
    apply_deadzone(s.lx, s.ly, dz);
    apply_deadzone(s.rx, s.ry, dz);
    apply_range(s.lx, s.ly, m.left_range);
    apply_range(s.rx, s.ry, m.right_range);
    return s;
}

// ---- per-controller bindings (format 2)

std::string controller_key(const std::string& guid, const std::string& name) { return guid + " " + name; }

int profile_index(const Mapping& m, const std::string& guid, const std::string& name) {
    const int n = (int)m.controllers.size();
    for (int i = 0; i < n; i++)
        if (m.controllers[i].guid == guid && m.controllers[i].name == name) return i;
    if (!guid.empty())
        for (int i = 0; i < n; i++)
            if (m.controllers[i].guid == guid) return i;
    if (!name.empty())
        for (int i = 0; i < n; i++)
            if (m.controllers[i].name == name) return i;
    return -1;
}

const std::array<int, kActionCount>& pads_for(const Mapping& m, const std::string& guid, const std::string& name) {
    const int i = profile_index(m, guid, name);
    return i >= 0 ? m.controllers[i].pad : m.pad;
}

int ensure_profile(Mapping& m, const std::string& guid, const std::string& name) {
    for (int i = 0; i < (int)m.controllers.size(); i++)  // its own (exactly this controller) only
        if (m.controllers[i].guid == guid && m.controllers[i].name == name) return i;
    Mapping::Controller c;
    c.guid = guid;
    c.name = name;
    c.pad = pads_for(m, guid, name);  // what it plays with now: a profile of the same model, or the shared
    m.controllers.push_back(c);
    std::erase(m.setup_declined, controller_key(guid, name));
    return (int)m.controllers.size() - 1;
}

void remove_profile(Mapping& m, int index) {
    if (index >= 0 && index < (int)m.controllers.size()) m.controllers.erase(m.controllers.begin() + index);
}

Mapping view_for(const Mapping& m, int index) {
    Mapping v = m;
    if (index >= 0 && index < (int)m.controllers.size()) {
        v.pad = m.controllers[index].pad;
        v.face_auto = false;  // a controller's own bindings are its own: Automatic is for the shared ones
    }
    return v;
}

Mapping active_view(const Mapping& m) {
    std::string guid, name;
    active_controller(guid, name);
    return guid.empty() && name.empty() ? m : view_for(m, profile_index(m, guid, name));
}

PromptStyle active_prompt_style() { return prompt_style(active_view(current()), pad_family()); }

void store_view(Mapping& m, const Mapping& view, int index) {
    if (index < 0 || index >= (int)m.controllers.size()) {
        auto controllers = m.controllers;
        auto declined = m.setup_declined;
        m = view;
        m.controllers = controllers;
        m.setup_declined = declined;
        return;
    }
    const auto shared_pad = m.pad;
    const bool shared_auto = m.face_auto;
    auto controllers = m.controllers;
    auto declined = m.setup_declined;
    m = view;
    controllers[index].pad = view.pad;
    m.pad = shared_pad;
    m.face_auto = shared_auto;
    m.controllers = controllers;
    m.setup_declined = declined;
}

bool setup_offer_due(const Mapping& m, const std::string& guid, const std::string& name, bool is_virtual) {
    if (is_virtual || (guid.empty() && name.empty())) return false;
    if (profile_index(m, guid, name) >= 0) return false;
    const std::string key = controller_key(guid, name);
    return std::find(m.setup_declined.begin(), m.setup_declined.end(), key) == m.setup_declined.end();
}

input::PadState combine(const input::PadState& a, const input::PadState& b) {
    input::PadState s = a;
    s.buttons |= b.buttons;
    s.virt |= b.virt;
    if (b.lx * b.lx + b.ly * b.ly > s.lx * s.lx + s.ly * s.ly) { s.lx = b.lx; s.ly = b.ly; }
    if (b.rx * b.rx + b.ry * b.ry > s.rx * s.rx + s.ry * s.ry) { s.rx = b.rx; s.ry = b.ry; }
    return s;
}

// ---- virtual actions on game buttons

std::vector<int> displaced_by(const Mapping& m, int pad) {
    std::vector<int> v;
    if (pad == kPadNone) return v;
    for (int a = 0; a < kScreenshot; a++)
        if (m.pad[a] == pad && !free_in_play(a)) v.push_back(a);
    return v;
}

// a controller input no game button uses in play: unbound, or only L / d-pad down / virtual actions
static bool free_input(const Mapping& m, int pad, int except) {
    for (int a = 0; a < kScreenshot; a++)
        if (a != except && m.pad[a] == pad && !free_in_play(a)) return false;
    return true;
}

int move_target(const Mapping& m, int a) {
    if (free_input(m, kPadLB, a)) return kPadLB;
    if (free_input(m, kPadDDown, a)) return kPadDDown;
    return kPadNone;
}

bool bind_virtual(Mapping& m, int v, int pad, Displace d) {
    if (!is_virtual(v) || pad <= kPadNone || pad >= kPadCount) return false;
    const std::vector<int> users = displaced_by(m, pad);
    // first check that every displaced button has somewhere to go (nothing changes otherwise)
    std::vector<int> to;
    for (int a : users) {
        int t = kPadNone;
        if (d == Displace::kMove) {
            t = move_target(m, a);
            if (t == kPadNone) return false;
        } else if (d == Displace::kMoveToL1) {
            if (!free_input(m, kPadLB, a) || pad == kPadLB) return false;
            t = kPadLB;
        } else if (d == Displace::kMoveToDDown) {
            if (!free_input(m, kPadDDown, a) || pad == kPadDDown) return false;
            t = kPadDDown;
        }
        to.push_back(t);
    }
    if (users.size() > 1 && d != Displace::kTakeOff) return false;  // one input, one free place
    for (size_t i = 0; i < users.size(); i++) {
        set_pad_binding(m, users[i], to[i]);
        // on L1 the moved button is the one that counts, in menus too (no page turns with L1)
        if (to[i] == kPadLB && m.pad[kL] == kPadLB) m.pad[kL] = kPadNone;
        // the other virtual action there (Jump and the moon jump sit together on L1 by default) comes
        // along to the new input: on one input the moon jump wins while it is on
        for (int o = kFirstVirtual; o < kActionCount; o++)
            if (o != v && to[i] != kPadNone && m.pad[o] == to[i]) m.pad[o] = pad;
    }
    m.pad[v] = pad;
    return true;
}

uint32_t play_buttons(const Mapping& m, uint32_t b, bool in_play) {
    if (in_play && !m.l_turns_page) b &= ~input::kL;
    auto shares = [&](int f, int g) {
        if (m.pad[f] != kPadNone && m.pad[f] == m.pad[g]) return true;
        for (const auto& c : m.controllers)  // a controller's own bindings count as well
            if (c.pad[f] != kPadNone && c.pad[f] == c.pad[g]) return true;
        for (int k : m.keys[f])
            if (k != kNoKey && (m.keys[g][0] == k || m.keys[g][1] == k)) return true;
        return false;
    };
    for (int f : {(int)kL, (int)kDDown}) {
        const uint32_t fb = action_bit(f);
        if (!(b & fb)) continue;
        for (int g = 0; g < kScreenshot; g++) {
            const uint32_t gb = action_bit(g);
            if (!gb || g == f || !(b & gb) || !shares(f, g)) continue;
            if (in_play) b &= ~fb;
            else b &= ~gb;
        }
    }
    return b;
}

void jump_from_legacy(Mapping& m, uint32_t vpad_bit) {
    for (int a = 0; a < kScreenshot; a++)
        if (action_bit(a) == vpad_bit) {
            m.keys[kJump] = m.keys[a];
            m.pad[kJump] = m.pad[a];
            return;
        }
}

// ---- JSON (just enough for this file: objects, arrays, strings, numbers, booleans, null)

namespace {
struct JsonMember;
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    // not std::pair<std::string, Json>: libstdc++ rejects a pair of an incomplete type
    std::vector<JsonMember> obj;
    const Json* get(const char* k) const;
};
struct JsonMember {
    std::string key;
    Json value;
};
const Json* Json::get(const char* k) const {
    for (auto& [key, v] : obj)
        if (key == k) return &v;
    return nullptr;
}

struct Parser {
    const char* p;
    const char* end;
    std::string err;
    void ws() { while (p < end && isspace((unsigned char)*p)) p++; }
    bool fail(const char* what) {
        if (err.empty()) err = what;
        return false;
    }
    bool lit(const char* w) {
        size_t n = strlen(w);
        if ((size_t)(end - p) < n || memcmp(p, w, n)) return false;
        p += n;
        return true;
    }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | c >> 6); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xE0 | c >> 12); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    bool str(std::string& o) {
        if (p >= end || *p != '"') return fail("expected string");
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) break;
                char c = *p++;
                switch (c) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (end - p < 4) return fail("bad \\u escape");
                    utf8(o, (uint32_t)strtoul(std::string(p, 4).c_str(), nullptr, 16));
                    p += 4;
                    break;
                }
                default: o += c;
                }
            } else o += *p++;
        }
        if (p >= end) return fail("unterminated string");
        p++;
        return true;
    }
    bool value(Json& v, int depth = 0) {
        if (depth > 32) return fail("nested too deep");
        ws();
        if (p >= end) return fail("unexpected end");
        if (*p == '{') {
            v.type = Json::Obj;
            p++;
            ws();
            if (p < end && *p == '}') { p++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (p >= end || *p++ != ':') return fail("expected ':'");
                Json c;
                if (!value(c, depth + 1)) return false;
                v.obj.push_back({std::move(k), std::move(c)});
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (*p == '[') {
            v.type = Json::Arr;
            p++;
            ws();
            if (p < end && *p == ']') { p++; return true; }
            for (;;) {
                Json c;
                if (!value(c, depth + 1)) return false;
                v.arr.push_back(std::move(c));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (*p == '"') { v.type = Json::Str; return str(v.s); }
        if (lit("true")) { v.type = Json::Bool; v.b = true; return true; }
        if (lit("false")) { v.type = Json::Bool; return true; }
        if (lit("null")) return true;
        char* e;
        v.n = strtod(p, &e);
        if (e == p) return fail("unexpected character");
        v.type = Json::Num;
        p = e;
        return true;
    }
};

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if ((unsigned char)c < 0x20) {  // controller names come from devices: no raw control characters
            char u[8];
            snprintf(u, sizeof u, "\\u%04x", (unsigned char)c);
            o += u;
            continue;
        }
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}
}  // namespace

std::string to_json(const Mapping& m) {
    std::string o = "{\n  \"version\": 2,\n  \"keyboard\": {\n";
    for (int a = 0; a < kActionCount; a++) {
        o += "    " + quote(action_id(a)) + ": [";
        bool first = true;
        for (int k : m.keys[a]) {
            if (k == kNoKey) continue;
            o += (first ? "" : ", ") + quote(key_id(k));
            first = false;
        }
        o += a + 1 < kActionCount ? "],\n" : "]\n";
    }
    o += "  },\n  \"controller\": {\n";
    for (int a = 0; a < kActionCount; a++) {
        o += "    " + quote(action_id(a)) + ": " + (m.pad[a] == kPadNone ? std::string("null") : quote(pad_id(m.pad[a])));
        o += a + 1 < kActionCount ? ",\n" : "\n";
    }
    // button prompts: written only when not the default, as face_layout
    static const char* const kPromptIds[] = {"auto", "original", "xbox", "playstation"};
    static const char* const kShoulderIds[] = {"auto", "nintendo", "numbered", "xbox"};
    std::string prompts;
    if (m.prompts != Mapping::Prompts::kAuto)
        prompts += std::string(",\n    \"button_prompts\": \"") + kPromptIds[(int)m.prompts] + "\"";
    if (m.shoulder_names != Mapping::ShoulderNames::kAuto)
        prompts += std::string(",\n    \"shoulder_names\": \"") + kShoulderIds[(int)m.shoulder_names] + "\"";
    if (!m.prompt_colours) prompts += ",\n    \"button_colours\": false";
    if (!m.prompt_text_colours) prompts += ",\n    \"button_text_colours\": false";
    // issue #267: written only when on (files without it stay as they were)
    if (m.background_controllers) prompts += ",\n    \"background_controllers\": true";
    if (m.l_turns_page) prompts += ",\n    \"l_turns_gamepad_page\": true";
    char opt[320];
    snprintf(opt, sizeof opt,
             "  },\n  \"options\": {\n    \"format\": 2,\n    \"stick_deadzone\": %.3g,\n"
             "    \"left_stick_range\": %.3g,\n    \"right_stick_range\": %.3g,\n"
             "    \"skip_controller_selection\": %s,\n    \"invert_camera_y\": %s%s",
             m.deadzone, m.left_range, m.right_range, m.skip_controller_selection ? "true" : "false", m.invert_camera_y ? "true" : "false",
             m.face_auto ? ",\n    \"face_layout\": \"auto\"" : "");
    o += opt + prompts + std::string(",\n    \"file_select_quit\": ") + (m.file_select_quit ? "true" : "false") + "\n  },\n";
    // per-controller bindings and the controllers that declined the setup (format 2)
    o += "  \"controllers\": [";
    for (size_t i = 0; i < m.controllers.size(); i++) {
        const auto& c = m.controllers[i];
        o += std::string(i ? ",\n" : "\n") + "    {\"guid\": " + quote(c.guid) + ", \"name\": " + quote(c.name) + ", \"controller\": {";
        for (int a = 0; a < kActionCount; a++)
            o += std::string(a ? ", " : "") + quote(action_id(a)) + ": " + (c.pad[a] == kPadNone ? std::string("null") : quote(pad_id(c.pad[a])));
        o += "}}";
    }
    o += m.controllers.empty() ? "],\n" : "\n  ],\n";
    o += "  \"controller_setup_declined\": [";
    for (size_t i = 0; i < m.setup_declined.size(); i++) o += (i ? ", " : "") + quote(m.setup_declined[i]);
    o += "],\n  \"shortcuts_version\": " + std::to_string(kShortcutsVersion) + ",\n  \"shortcuts\": {\n";
    for (int a = 0; a < kShortcutCount; ++a) {
        const auto& b = m.shortcuts[a];
        o += "    " + quote(shortcut_info(a).id) + ": {\"keys\": [";
        for (int slot = 0; slot < 2; ++slot) {
            if (slot) o += ", ";
            const auto k = b.keys[slot];
            o += "{\"key\": " + (k.code == kNoKey ? std::string("null") : quote(key_id(k.code))) +
                ", \"modifiers\": " + std::to_string(k.mods) + "}";
        }
        o += "], \"hold\": " + quote(pad_id(b.hold)) + ", \"button\": " + quote(pad_id(b.button)) + "}";
        o += a + 1 < kShortcutCount ? ",\n" : "\n";
    }
    return o + "  }\n}\n";
}

bool from_json(const std::string& text, Mapping& out, std::string* error) {
    auto bad = [&](const std::string& e) { if (error) *error = e; return false; };
    Parser ps{text.data(), text.data() + text.size(), {}};
    Json root;
    if (!ps.value(root)) return bad("JSON: " + ps.err);
    ps.ws();
    if (ps.p != ps.end) return bad("JSON: trailing characters");
    if (root.type != Json::Obj) return bad("JSON: expected an object");
    Mapping m = Mapping::defaults();
    std::string warn;
    if (const Json* kb = root.get("keyboard"); kb && kb->type == Json::Obj) {
        for (auto& [id, v] : kb->obj) {
            int a = action_from_id(id);
            if (a < 0) { warn += "unknown input \"" + id + "\"; "; continue; }
            m.keys[a].fill(kNoKey);
            std::vector<const Json*> list;
            if (v.type == Json::Arr) for (auto& e : v.arr) list.push_back(&e);
            else if (v.type == Json::Str) list.push_back(&v);
            int slot = 0;
            for (const Json* e : list) {
                if (e->type != Json::Str) continue;
                int k = key_from_id(e->s);
                if (k == kNoKey) { warn += "unknown key \"" + e->s + "\"; "; continue; }
                if (reserved_key(k)) { warn += "key \"" + e->s + "\" is an app shortcut; "; continue; }
                if (slot < kKeysPerAction) m.keys[a][slot++] = k;
            }
        }
    }
    if (const Json* pc = root.get("controller"); pc && pc->type == Json::Obj) {
        for (auto& [id, v] : pc->obj) {
            int a = action_from_id(id);
            if (a < 0) { warn += "unknown input \"" + id + "\"; "; continue; }
            if (v.type == Json::Null) {
                if (a >= kA && a <= kY) {
                    warn += "null face binding " + id + " repaired to default; ";
                } else m.pad[a] = kPadNone;
                continue;
            }
            if (v.type != Json::Str) continue;
            int p = pad_from_id(v.s);
            if (p < 0) { warn += "unknown controller input \"" + v.s + "\"; "; continue; }
            m.pad[a] = p;
        }
    }
    const Json* fileVersion = root.get("version");
    if (!fileVersion || fileVersion->type != Json::Num || fileVersion->n < 2) {
        // format 1: Jump anywhere and the moon jump sat on the Wii U's L, whatever pressed it
        m.keys[kJump] = m.keys[kMoonJump] = m.keys[kL];
        m.pad[kJump] = m.pad[kMoonJump] = m.pad[kL];
    }
    if (const Json* list = root.get("controllers"); list && list->type == Json::Arr) {
        for (const Json& e : list->arr) {
            if (e.type != Json::Obj) continue;
            const Json* guid = e.get("guid");
            const Json* name = e.get("name");
            const Json* binds = e.get("controller");
            if (!guid || guid->type != Json::Str || !name || name->type != Json::Str) { warn += "controller without guid/name; "; continue; }
            Mapping::Controller c;
            c.guid = guid->s;
            c.name = name->s;
            c.pad = m.pad;  // inputs missing from the profile: the shared binding
            if (binds && binds->type == Json::Obj)
                for (auto& [id, v] : binds->obj) {
                    const int a = action_from_id(id);
                    if (a < 0) { warn += "unknown input \"" + id + "\"; "; continue; }
                    if (v.type == Json::Null) { c.pad[a] = kPadNone; continue; }
                    if (v.type != Json::Str) continue;
                    const int p = pad_from_id(v.s);
                    if (p < 0) { warn += "unknown controller input \"" + v.s + "\"; "; continue; }
                    c.pad[a] = p;
                }
            bool dup = false;
            for (auto& o : m.controllers) dup |= o.guid == c.guid && o.name == c.name;
            if (!dup) m.controllers.push_back(std::move(c));
        }
    }
    if (const Json* list = root.get("controller_setup_declined"); list && list->type == Json::Arr)
        for (const Json& e : list->arr)
            if (e.type == Json::Str && std::find(m.setup_declined.begin(), m.setup_declined.end(), e.s) == m.setup_declined.end())
                m.setup_declined.push_back(e.s);
    if (const Json* op = root.get("options"); op && op->type == Json::Obj) {
        const Json* format = op->get("format");
        const bool legacy = !format || format->type != Json::Num || format->n < 2;
        if (const Json* d = op->get("stick_deadzone"); d && d->type == Json::Num) {
            // Old saves always wrote zero, the former default. Format 2 distinguishes a
            // deliberate zero from that default; saving writes the marker after migration.
            if (!(legacy && d->n == 0))
                m.deadzone = std::clamp((float)d->n, 0.0f, 0.9f);
        }
        // issue #40: missing keys keep the default 1.0 (no scaling)
        if (const Json* r = op->get("left_stick_range"); r && r->type == Json::Num)
            m.left_range = std::clamp((float)r->n, kMinStickRange, kMaxStickRange);
        if (const Json* r = op->get("right_stick_range"); r && r->type == Json::Num)
            m.right_range = std::clamp((float)r->n, kMinStickRange, kMaxStickRange);
        if (const Json* i = op->get("file_select_quit"); i && i->type == Json::Bool) m.file_select_quit = i->b;
        if (const Json* i = op->get("skip_controller_selection"); i && i->type == Json::Bool) m.skip_controller_selection = i->b;
        if (const Json* i = op->get("invert_camera_y"); i && i->type == Json::Bool) m.invert_camera_y = i->b;
        if (const Json* i = op->get("background_controllers"); i && i->type == Json::Bool) m.background_controllers = i->b;
        if (const Json* i = op->get("l_turns_gamepad_page"); i && i->type == Json::Bool) m.l_turns_page = i->b;
        // only "automatic" is stored: the two manual presets are the shape of the face bindings
        if (const Json* f = op->get("face_layout"); f && f->type == Json::Str) m.face_auto = f->s == "auto";
        // button prompts (#143); an unknown value keeps the default
        if (const Json* p = op->get("button_prompts"); p && p->type == Json::Str) {
            if (p->s == "original") m.prompts = Mapping::Prompts::kOriginal;
            else if (p->s == "xbox") m.prompts = Mapping::Prompts::kXbox;
            else if (p->s == "playstation") m.prompts = Mapping::Prompts::kPlayStation;
            else if (p->s != "auto") warn += "unknown button_prompts \"" + p->s + "\"; ";
        }
        if (const Json* n = op->get("shoulder_names"); n && n->type == Json::Str) {
            if (n->s == "nintendo") m.shoulder_names = Mapping::ShoulderNames::kNintendo;
            else if (n->s == "numbered") m.shoulder_names = Mapping::ShoulderNames::kNumbered;
            else if (n->s == "xbox") m.shoulder_names = Mapping::ShoulderNames::kXbox;
            else if (n->s != "auto") warn += "unknown shoulder_names \"" + n->s + "\"; ";
        }
        if (const Json* c = op->get("button_colours"); c && c->type == Json::Bool) m.prompt_colours = c->b;
        if (const Json* c = op->get("button_text_colours"); c && c->type == Json::Bool) m.prompt_text_colours = c->b;
    }
    // Migration is one-way: an explicit shortcuts.Screenshot wins over the old Photo row.
    for (int i = 0; i < 2; ++i) m.shortcuts[kPhoto].keys[i] = {m.keys[kScreenshot][i], 0};
    // Legacy single-button controller screenshots retain their original path.
    const Json* versionValue = root.get("shortcuts_version");
    const double version = versionValue && versionValue->type == Json::Num ? versionValue->n : 0;
    if (const Json* sc = root.get("shortcuts"); sc && sc->type == Json::Obj) {
        for (int a = 0; a < kShortcutCount; ++a) {
            const Json* v = sc->get(shortcut_info(a).id);
            if (!v || v->type != Json::Obj) continue;
            auto& b = m.shortcuts[a];
            if (const Json* keys = v->get("keys"); keys && keys->type == Json::Arr) {
                b.keys = {};
                for (size_t i = 0; i < keys->arr.size() && i < 2; ++i) {
                    const auto& e = keys->arr[i];
                    const auto* key = e.get("key"); const auto* mods = e.get("modifiers");
                    if (key && key->type == Json::Str) {
                        int code = key_from_id(key->s);
                        if (code >= 0 && code < kInputCount) b.keys[i].code = code;
                    }
                    if (mods && mods->type == Json::Num && mods->n >= 0 && mods->n <= 15 && std::floor(mods->n) == mods->n)
                        b.keys[i].mods = int(mods->n);
                }
            }
            if (const Json* h = v->get("hold"); h && h->type == Json::Str) b.hold = std::max(0, pad_from_id(h->s));
            if (const Json* h = v->get("button"); h && h->type == Json::Str) b.button = std::max(0, pad_from_id(h->s));
            if (!b.button || b.hold == b.button) b.hold = b.button = kPadNone;
            // Old files could not edit the built-in Home shortcut. Make that implicit default visible.
            if (a == kSettings && !b.button && version < 2) b.button = kPadHome;
        }
    }
    // Files from before Esc opened Settings keep custom keys; the untouched old default (F1, plus
    // Cmd+, in the macOS app) becomes the current one, unless a game input already uses Esc. Cmd+, stays
    // available on macOS as the app menu's fixed Settings... key (gfx/menu_shortcuts.h).
    if (version < 3) {
        auto& keys = m.shortcuts[kSettings].keys;
        bool escUsed = false;
        for (int g = 0; g < kScreenshot; ++g) for (int v : m.keys[g]) escUsed |= v == kVK_Escape;
        for (int a = 0; a < kShortcutCount; ++a) for (auto k : m.shortcuts[a].keys) escUsed |= k.code == kVK_Escape;
        const bool legacy = keys[0] == ShortcutKey{kVK_F1} &&
            (keys[1] == ShortcutKey{} || keys[1] == ShortcutKey{kVK_ANSI_Comma, kModSuper});
        if (legacy && !escUsed) keys = shortcut_defaults()[kSettings].keys;
    }
    out = m;
    if (error) *error = warn;
    return true;
}

int json_version(const std::string& text) {
    Parser ps{text.data(), text.data() + text.size(), {}};
    Json root;
    if (!ps.value(root) || root.type != Json::Obj) return 1;
    const Json* v = root.get("version");
    return v && v->type == Json::Num && v->n >= 2 ? (int)v->n : 1;
}

bool load_file(const std::string& path, Mapping& out, std::string* error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = strerror(errno);
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    return from_json(text, out, error);
}

static void mkdirs(const std::string& dir) {
    std::error_code ec; std::filesystem::create_directories(dir,ec);
}

bool save_file(const std::string& path, const Mapping& m) {
    size_t slash = path.rfind('/');
    if (slash != std::string::npos) mkdirs(path.substr(0, slash));
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    std::string text = to_json(m);
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (ok) ok = host::replace_file(tmp,path);  // atomic: never a half-written file
    if (!ok) remove(tmp.c_str());
    return ok;
}

std::string default_path() {
    // WWHD_CONTROLS=<file> uses another controls file (tests)
    if (const char* e = getenv("WWHD_CONTROLS"); e && *e) return e;
    return host::config_dir() + "/controls.json";
}

// ---- live mapping

static std::mutex g_mu;
static Mapping g_map = Mapping::defaults();
static std::atomic<uint32_t> g_gen{1};
static std::atomic<bool> g_skip_controller_selection{true};
static std::atomic<bool> g_background_controllers{false};
static std::atomic<bool> g_file_select_quit{true};
static std::atomic<bool> g_invert_camera_y{false};
static std::atomic<bool> g_l_turns_page{false};
// format 1 (or no file) at startup: the old Jump anywhere button is still to be applied
static std::mutex g_legacy_mu;
static bool g_loaded = false, g_legacy = true;
static uint32_t g_legacy_bit = 0;
static std::mutex g_active_mu;
static std::string g_active_guid, g_active_name;

// the dominant pad's labels as note_face_labels last reported them (its own lock: set_current
// takes g_mu)
static std::mutex g_labels_mu;
static FaceLabel g_labels[4] = {FaceLabel::kUnknown, FaceLabel::kUnknown, FaceLabel::kUnknown,
                                FaceLabel::kUnknown};

static std::atomic<int> g_family{(int)PadFamily::kUnknown};

PadFamily pad_family() { return (PadFamily)g_family.load(std::memory_order_relaxed); }

void note_face_labels(const FaceLabel labels[4]) {
    Mapping m = current();
    {
        std::lock_guard<std::mutex> lk(g_labels_mu);
        for (int i = 0; i < 4; i++) g_labels[i] = labels[i];
    }
    // the prompts follow the family (button_glyphs re-reads the style on a new generation). An
    // unplugged pad (all unknown) keeps the last family, as the Automatic face preset keeps its choice.
    if (const PadFamily f = pad_family_from_labels(labels); f != PadFamily::kUnknown && f != pad_family()) {
        g_family.store((int)f, std::memory_order_relaxed);
        g_gen++;
    }
    if (!m.face_auto) return;
    int out[4];
    if (!face_bindings_from_labels(labels, out)) return;
    bool changed = false;
    for (int i = 0; i < 4; i++) changed |= m.pad[kA + i] != out[i];
    if (!changed) return;
    for (int i = 0; i < 4; i++) m.pad[kA + i] = out[i];
    set_current(m);
}

void set_face_auto(Mapping& m, bool on) {
    m.face_auto = on;
    if (!on) return;
    int out[4];
    FaceLabel labels[4];
    {
        std::lock_guard<std::mutex> lk(g_labels_mu);
        for (int i = 0; i < 4; i++) labels[i] = g_labels[i];
    }
    if (face_bindings_from_labels(labels, out))  // none noted: the bindings wait for a pad
        for (int i = 0; i < 4; i++) m.pad[kA + i] = out[i];
}

void load_startup() {
    std::string path = default_path(), err;
    Mapping m;
    struct stat st;
    bool legacy = true;
    if (stat(path.c_str(), &st) == 0) {  // else no file yet: defaults
        if (load_file(path, m, &err)) {
            set_current(m, false);
            fprintf(stderr, "[input] controls loaded from %s%s%s\n", path.c_str(), err.empty() ? "" : ": ", err.c_str());
            if (FILE* f = fopen(path.c_str(), "rb")) {
                std::string text;
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
                fclose(f);
                legacy = json_version(text) < 2;
            }
        } else {
            fprintf(stderr, "[input] %s: %s; using default controls\n", path.c_str(), err.c_str());
        }
    }
    uint32_t bit = 0;
    {
        std::lock_guard<std::mutex> lk(g_legacy_mu);
        g_loaded = true;
        g_legacy = legacy;
        bit = std::exchange(g_legacy_bit, 0u);
    }
    if (bit) adopt_legacy_jump_button(bit);
}

void adopt_legacy_jump_button(uint32_t vpad_bit) {
    {
        std::lock_guard<std::mutex> lk(g_legacy_mu);
        if (!g_loaded) { g_legacy_bit = vpad_bit; return; }  // load_startup applies it
        if (!g_legacy) return;
        g_legacy = false;  // once
    }
    if (vpad_bit == input::kL) return;  // where format 1 already put it
    Mapping m = current();
    jump_from_legacy(m, vpad_bit);
    if (!(m == current())) {
        fprintf(stderr, "[input] Jump anywhere: the old button choice (%04X) became the Jump binding\n", vpad_bit);
        set_current(m);
    }
}

void set_active_controller(const std::string& guid, const std::string& name) {
    std::lock_guard<std::mutex> lk(g_active_mu);
    if (g_active_guid == guid && g_active_name == name) return;
    g_active_guid = guid;
    g_active_name = name;
    g_gen++;  // the game's prompts follow the bindings of the controller in use (button_glyphs::style)
}
void active_controller(std::string& guid, std::string& name) {
    std::lock_guard<std::mutex> lk(g_active_mu);
    guid = g_active_guid;
    name = g_active_name;
}

Mapping current() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_map;
}

void set_current(const Mapping& requested, bool save) {
    Mapping m = requested;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (m.shortcuts[kPhoto] == g_map.shortcuts[kPhoto] && m.keys[kScreenshot] != g_map.keys[kScreenshot])
            for (int i = 0; i < 2; ++i) m.shortcuts[kPhoto].keys[i] = {m.keys[kScreenshot][i], 0};
    }

    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_map = m;
        // Publish the camera's cached option in the same writer order as the
        // mapping shown by Controls. Publishing after unlocking lets an older
        // writer overwrite a newer value, leaving the camera inverted while
        // current().invert_camera_y is false until another mapping update.
        g_invert_camera_y = m.invert_camera_y;
    }
    g_skip_controller_selection = m.skip_controller_selection;
    g_background_controllers = m.background_controllers;
    g_file_select_quit = m.file_select_quit;
    g_l_turns_page = m.l_turns_page;
    g_gen++;
    if (save && !save_file(default_path(), m))
        fprintf(stderr, "[input] could not save controls to %s\n", default_path().c_str());
}

uint32_t generation() { return g_gen.load(); }
bool file_select_quit() { return g_file_select_quit.load(std::memory_order_relaxed); }
bool skip_controller_selection() { return g_skip_controller_selection.load(std::memory_order_relaxed); }
bool background_controllers() { return g_background_controllers.load(std::memory_order_relaxed); }
bool invert_camera_y() { return g_invert_camera_y.load(std::memory_order_relaxed); }
bool l_turns_page() { return g_l_turns_page.load(std::memory_order_relaxed); }

}  // namespace input_map
