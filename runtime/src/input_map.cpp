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
int action_from_id(const std::string& id) {
    for (int a = 0; a < kActionCount; a++)
        if (id == kActions[a].id) return a;
    return -1;
}

const char* pad_id(int p) { return p >= 0 && p < kPadCount ? kPads[p].id : ""; }
const char* pad_label(int p) { return p > 0 && p < kPadCount ? kPads[p].label : ""; }
int pad_from_id(const std::string& id) {
    if (id.empty()) return kPadNone;
    for (int p = 1; p < kPadCount; p++)
        if (id == kPads[p].id) return p;
    return -1;
}

std::string key_id(int code) {
    if (code < 0 || code > 255) return "";
    if (auto* k = find_key(code)) return k->id;
    return "Key" + std::to_string(code);
}
std::string key_label(int code) {
    if (code < 0 || code > 255) return "";
    if (auto* k = find_key(code)) return k->label;
    return "Key " + std::to_string(code);
}
int key_from_id(const std::string& id) {
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
    case kVK_ANSI_O: return "Graphics › Ambient occlusion";
    case kVK_ANSI_M: return "Graphics › Full-size occlusion depth";
    case kVK_ANSI_N: return "Graphics › Anisotropic filtering";
    case kVK_ANSI_R: return "Graphics › Internal resolution";
    case kVK_ANSI_6: return "Graphics › 60 fps: frame interpolation";
    case kVK_ANSI_7: return "Graphics › 60 fps: true 60";
    case kVK_ANSI_8: return "Graphics › FXAA";
    case kVK_ANSI_9: return "Graphics › Record sound activity";
    case kVK_ANSI_P: case kVK_F12: return "Graphics › Capture frame";
    case kVK_F1: return "the settings overlay (F1; Shift+F1 saves slot 1)";
    case kVK_F2: case kVK_F3: case kVK_F4: case kVK_F5: return "Save States (F2-F5 load, Shift+F1-F5 save)";
    case kVK_Escape: return "cancelling a key assignment";
    case kVK_Command: case kVK_RightCommand: return "menu shortcuts (⌘)";
    default: return nullptr;
    }
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
    // controllers map by position: the bottom face button (Xbox A) is the Wii U's B (face_layout)
    const int pads[kActionCount] = {
        kPadNone, kPadNone, kPadNone, kPadNone,  // A B X Y: face-layout preset, filled below
        kPadLB, kPadRB, kPadLT, kPadRT, kPadMenu, kPadOptions, kPadHome,
        kPadDUp, kPadDDown, kPadDLeft, kPadDRight, kPadL3, kPadR3,
        kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight, kPadRSUp, kPadRSDown, kPadRSLeft, kPadRSRight,
        kPadNone,  // Screenshot: no controller input by default (every button already plays)
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
    const int p = current().pad[action];
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

bool has_conflict(const Mapping& m, int a) {
    if (a < 0 || a >= kActionCount) return false;
    for (int k : m.keys[a])
        if (k != kNoKey && !key_users(m, k, a).empty()) return true;
    return m.pad[a] != kPadNone && !pad_users(m, m.pad[a], a).empty();
}

int conflict_count(const Mapping& m) {
    int n = 0;
    for (int code = 0; code < 256; code++)
        if (key_users(m, code).size() > 1) n++;
    for (int p = 1; p < kPadCount; p++)
        if (pad_users(m, p).size() > 1) n++;
    return n;
}

std::string key_short_label(int code) {
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
    static const char* const kShort[kPadCount] = {
        "", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Menu", "View", "Home", "L3", "R3",
        "D-pad ↑", "D-pad ↓", "D-pad ←", "D-pad →", "LS ↑", "LS ↓", "LS ←", "LS →", "RS ↑", "RS ↓", "RS ←", "RS →",
    };
    return p > 0 && p < kPadCount ? kShort[p] : "";
}

input::PadState keyboard_state(const Mapping& m, const bool keys[256]) {
    float v[kActionCount];
    for (int a = 0; a < kActionCount; a++) {
        v[a] = 0;
        for (int k : m.keys[a])
            if (k >= 0 && k < 256 && keys[k]) v[a] = 1;
    }
    input::PadState s;
    for (int a = 0; a < kActionCount; a++)
        if (v[a] && action_bit(a)) s.buttons |= action_bit(a);
    s.lx = v[kLRight] - v[kLLeft];
    s.ly = v[kLUp] - v[kLDown];
    s.rx = v[kRRight] - v[kRLeft];
    s.ry = v[kRUp] - v[kRDown];
    if (s.lx && s.ly) { s.lx *= 0.7071f; s.ly *= 0.7071f; }  // keep diagonal walking at full-stick length
    if (m.invert_camera_y) s.ry = -s.ry;
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

input::PadState controller_state(const Mapping& m, const float values[kPadCount]) {
    float v[kActionCount];
    for (int a = 0; a < kActionCount; a++) {
        int p = m.pad[a];
        v[a] = p > kPadNone && p < kPadCount ? values[p] : 0;
    }
    input::PadState s;
    for (int a = 0; a < kActionCount; a++)
        if (action_bit(a) && v[a] > 0.5f) s.buttons |= action_bit(a);
    s.lx = v[kLRight] - v[kLLeft];
    s.ly = v[kLUp] - v[kLDown];
    s.rx = v[kRRight] - v[kRLeft];
    s.ry = v[kRUp] - v[kRDown];
    float dz = std::clamp(m.deadzone, 0.0f, 0.9f);
    apply_deadzone(s.lx, s.ly, dz);
    apply_deadzone(s.rx, s.ry, dz);
    if (m.invert_camera_y) s.ry = -s.ry;
    return s;
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
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}
}  // namespace

std::string to_json(const Mapping& m) {
    std::string o = "{\n  \"version\": 1,\n  \"keyboard\": {\n";
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
    char opt[192];
    snprintf(opt, sizeof opt,
             "  },\n  \"options\": {\n    \"stick_deadzone\": %.3g,\n    \"invert_camera_y\": %s%s\n  }\n}\n",
             m.deadzone, m.invert_camera_y ? "true" : "false",
             m.face_auto ? ",\n    \"face_layout\": \"auto\"" : "");
    return o + opt;
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
            if (v.type == Json::Null) { m.pad[a] = kPadNone; continue; }
            if (v.type != Json::Str) continue;
            int p = pad_from_id(v.s);
            if (p < 0) { warn += "unknown controller input \"" + v.s + "\"; "; continue; }
            m.pad[a] = p;
        }
    }
    if (const Json* op = root.get("options"); op && op->type == Json::Obj) {
        if (const Json* d = op->get("stick_deadzone"); d && d->type == Json::Num)
            m.deadzone = std::clamp((float)d->n, 0.0f, 0.9f);
        if (const Json* i = op->get("invert_camera_y"); i && i->type == Json::Bool) m.invert_camera_y = i->b;
        // only "automatic" is stored: the two manual presets are the shape of the face bindings
        if (const Json* f = op->get("face_layout"); f && f->type == Json::Str) m.face_auto = f->s == "auto";
    }
    out = m;
    if (error) *error = warn;
    return true;
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

// the dominant pad's labels as note_face_labels last reported them (its own lock: set_current
// takes g_mu)
static std::mutex g_labels_mu;
static FaceLabel g_labels[4] = {FaceLabel::kUnknown, FaceLabel::kUnknown, FaceLabel::kUnknown,
                                FaceLabel::kUnknown};

void note_face_labels(const FaceLabel labels[4]) {
    Mapping m = current();
    {
        std::lock_guard<std::mutex> lk(g_labels_mu);
        for (int i = 0; i < 4; i++) g_labels[i] = labels[i];
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
    if (stat(path.c_str(), &st) != 0) return;  // no file yet: defaults
    if (load_file(path, m, &err)) {
        set_current(m, false);
        fprintf(stderr, "[input] controls loaded from %s%s%s\n", path.c_str(), err.empty() ? "" : ": ", err.c_str());
    } else {
        fprintf(stderr, "[input] %s: %s; using default controls\n", path.c_str(), err.c_str());
    }
}

Mapping current() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_map;
}

void set_current(const Mapping& m, bool save) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_map = m;
    }
    g_gen++;
    if (save && !save_file(default_path(), m))
        fprintf(stderr, "[input] could not save controls to %s\n", default_path().c_str());
}

uint32_t generation() { return g_gen.load(); }

}  // namespace input_map
