// Unit tests for the controls mapping (runtime/src/input_map.cpp).
//   make -C build/cmake input_map_test && ./build/cmake/input_map_test
#include "platform/keycodes.h"
#include <filesystem>
#include <chrono>
#include <barrier>
#include <thread>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "input_map.h"
#include "mouse_bindings.h"
#include "camera_invert.h"
#include "controller_selection.h"
#include "file_select_quit.h"
#include "platform/touch_face.h"
#include "overlay/settings_shortcut.h"
#include "overlay/binding_capture.h"
#include "platform/android_controller.h"
#include "platform/touch_buttons.h"
#include "overlay/mobile_ui.h"

using namespace input_map;

static int g_fail = 0, g_pass = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (x) g_pass++;                                                      \
        else { g_fail++; fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #x); } \
    } while (0)

static input::PadState press(const Mapping& m, std::initializer_list<int> codes) {
    bool keys[256] = {};
    for (int c : codes) keys[c] = true;
    return keyboard_state(m, keys);
}

static void test_mouse() {
    auto m = Mapping::defaults();
    const auto old = to_json(m);
    for (const auto& slots : m.keys) for (int code : slots) CHECK(code < kMouse1);
    const char* ids[] = {"Mouse1", "Mouse2", "Mouse3", "Mouse4", "Mouse5", "WheelUp", "WheelDown"};
    for (int i=0;i<7;++i) {
        CHECK(key_id(kMouse1+i) == ids[i]); CHECK(key_from_id(ids[i]) == kMouse1+i);
        CHECK(!key_label(kMouse1+i).empty()); CHECK(!key_short_label(kMouse1+i).empty());
        m.keys[kA][0] = kMouse1+i;
        Mapping out; CHECK(from_json(to_json(m),out)); CHECK(out == m);
        bool raw[kInputCount]{}; raw[kMouse1+i] = true;
        CHECK(keyboard_state(m,raw,kInputCount).buttons == input::kA);
        CHECK(keyboard_state(m,raw).buttons == 0); // legacy keyboard callers never read mouse slots
    }
    Mapping legacy; CHECK(from_json(old,legacy)); CHECK(legacy == Mapping::defaults());
    m.keys[kA] = {kMouse1,kNoKey}; m.keys[kB] = {kMouse1,kNoKey};
    CHECK(has_conflict(m,kA)); CHECK(conflict_count(m) == 1);
    m.keys[kB] = {kMouse2,kNoKey}; m.keys[kX] = {kWheelUp,kNoKey}; m.keys[kY] = {kWheelDown,kNoKey};
    input::MouseBindings mouse;
    mouse.button(1,0,true,false); CHECK(mouse.read(m,true).buttons == 0); // capture-only click
    mouse.button(1,0,false,true); mouse.button(1,0,true,true);
    CHECK(mouse.read(m,true).buttons == input::kA);
    mouse.consume(); CHECK(mouse.read(m,true).buttons == 0); // overlay transition without a read
    mouse.button(2,1,true,true); CHECK(mouse.read(m,true).buttons == input::kB); // other device works
    mouse.button(1,0,false,true); mouse.button(1,0,true,true);
    CHECK(mouse.read(m,true).buttons == (input::kA|input::kB));
    CHECK(mouse.read(m,false).buttons == 0); CHECK(mouse.read(m,true).buttons == 0); // text prompt
    mouse.button(1,0,false,true); mouse.button(2,1,false,true);
    mouse.wheel(2,true); CHECK(mouse.read(m,true).buttons == input::kX);
    CHECK(mouse.read(m,true).buttons == 0); CHECK(mouse.read(m,true).buttons == input::kX);
    CHECK(mouse.read(m,true).buttons == 0); CHECK(mouse.read(m,true).buttons == 0);
    mouse.wheel(-1,false); CHECK(mouse.read(m,true).buttons == 0);
    mouse.wheel(-0.5f,true); CHECK(mouse.read(m,true).buttons == 0);
    mouse.wheel(-0.5f,true); CHECK(mouse.read(m,true).buttons == input::kY);
    mouse.consume(); CHECK(mouse.read(m,true).buttons == 0);
    mouse.wheel(1,true); mouse.wheel(-1,true); mouse.wheel(1,true);
    CHECK(mouse.read(m,true).buttons == input::kX); CHECK(mouse.read(m,true).buttons == 0);
    CHECK(mouse.read(m,true).buttons == input::kY); CHECK(mouse.read(m,true).buttons == 0);
    CHECK(mouse.read(m,true).buttons == input::kX); CHECK(mouse.read(m,true).buttons == 0);
    mouse.wheel(2,true); CHECK(mouse.read(m,true,nullptr,nullptr,false).buttons == 0);
    CHECK(mouse.read(m,true).buttons == 0); // wheel option drops queued bindings
    m.keys[kLUp]={kMouse1,kNoKey}; m.keys[kA].fill(kNoKey);
    bool keyboard[256]{}; keyboard[kVK_ANSI_D]=true;
    mouse.button(1,0,true,true);
    auto combined=mouse.read(m,true,keyboard);
    CHECK(std::fabs(combined.lx-0.70710678f)<0.0001f && std::fabs(combined.ly-0.70710678f)<0.0001f);
    mouse.remove_device(1); CHECK(mouse.read(m,true,keyboard).lx == 1 && mouse.read(m,true,keyboard).ly == 0);
    CHECK(mouse.read(m,false,keyboard).lx == 1); // consuming the mouse preserves keyboard input
}

static void test_defaults() {
    Mapping m = Mapping::defaults();
    // the hard-coded keyboard layout this replaces
    struct { int code; uint32_t bit; } old[] = {
        {kVK_ANSI_K, input::kA}, {kVK_Space, input::kA}, {kVK_ANSI_J, input::kB}, {kVK_ANSI_L, input::kX},
        {kVK_ANSI_I, input::kY}, {kVK_ANSI_Q, input::kL}, {kVK_ANSI_E, input::kR}, {kVK_Shift, input::kZL},
        {kVK_ANSI_C, input::kZR}, {kVK_Return, input::kPlus}, {kVK_Tab, input::kMinus}, {kVK_ANSI_H, input::kHome},
        {kVK_ANSI_1, input::kUp}, {kVK_ANSI_2, input::kDown}, {kVK_ANSI_3, input::kLeft}, {kVK_ANSI_4, input::kRight},
        {kVK_ANSI_X, input::kStickL}, {kVK_ANSI_V, input::kStickR},
    };
    for (auto& o : old) CHECK(press(m, {o.code}).buttons == o.bit);
    auto s = press(m, {kVK_ANSI_W, kVK_ANSI_D});
    CHECK(std::fabs(s.lx - 0.7071f) < 1e-4 && std::fabs(s.ly - 0.7071f) < 1e-4);
    s = press(m, {kVK_ANSI_A});
    CHECK(s.lx == -1 && s.ly == 0);
    s = press(m, {kVK_ANSI_S});
    CHECK(s.ly == -1);
    s = press(m, {kVK_UpArrow, kVK_RightArrow});
    CHECK(s.rx == 1 && s.ry == 1);  // right stick: no diagonal scaling (as before)
    CHECK(press(m, {kVK_ANSI_Z}).buttons == 0);
    // the Screenshot app action: F10, no controller input, never a GamePad button or stick
    CHECK(m.keys[kScreenshot][0] == kVK_F10 && m.pad[kScreenshot] == kPadNone);
    CHECK(press(m, {kVK_F10}).buttons == 0 && press(m, {kVK_F10}).lx == 0);
    CHECK(action_bit(kScreenshot) == 0 && action_from_id("Screenshot") == kScreenshot);
    // no default key is reserved; no key is used twice, except Q / L1 by L, Jump and the moon jump
    // (virtual inputs share the Wii U's L: in play L leaves, play_buttons)
    for (int a = 0; a < kActionCount; a++)
        for (int k : m.keys[a]) {
            CHECK(k == kNoKey || !reserved_key(k));
            CHECK(k == kNoKey || key_users(m, k, a).empty() || (k == kVK_ANSI_Q && (a == kL || is_virtual(a))));
            CHECK(!has_conflict(m, a));
        }
    CHECK(conflict_count(m) == 0);
    CHECK(key_users(m, kVK_ANSI_Q).size() == 3 && pad_users(m, kPadLB).size() == 3);

    // controllers by position: Xbox A (bottom) = Wii U B, Xbox B (right) = Wii U A
    float v[kPadCount] = {};
    v[kPadA] = 1;
    CHECK(controller_state(m, v).buttons == input::kB);
    v[kPadA] = 0; v[kPadB] = 1;
    CHECK(controller_state(m, v).buttons == input::kA);
    v[kPadB] = 0; v[kPadLT] = 0.4f;
    CHECK(controller_state(m, v).buttons == 0);  // half-pulled trigger below threshold
    v[kPadLT] = 0.8f;
    CHECK(controller_state(m, v).buttons == input::kZL);
    v[kPadLT] = 0; v[kPadLSLeft] = 0.6f; v[kPadRSUp] = 0.3f;
    m.deadzone = 0;  // check axis bindings independently of the default dead zone
    s = controller_state(m, v);
    CHECK(std::fabs(s.lx + 0.6f) < 1e-6 && std::fabs(s.ry - 0.3f) < 1e-6);
}

static void test_apple_face_normalization() {
    using AL = AppleFaceLayout;
    using FL = FaceLabel;
    for (const char* name : {"Switch Pro Controller", "Nintendo Switch Pro Controller",
                            "Nintendo Switch Joy-Con (L/R)", "JoyCon Pair", "Nintendo SNES Controller",
                            "SNES Controller", "Super Famicom Controller", "Nintendo Switch Online SNES"}) {
        CHECK(apple_face_layout(name, nullptr) == AL::kNintendo);
        CHECK(apple_face_layout(nullptr, name) == AL::kNintendo);
    }
    CHECK(apple_face_layout("sWiTcH pRo CoNtRoLlEr", "") == AL::kNintendo);
    CHECK(apple_face_layout("Wireless Controller", "Nintendo") == AL::kNintendo);
    for (const char* name : {"Nintendo Switch Joy-Con (L)", "Nintendo Switch Joy-Con (R)", "JoyCon (L)"})
        CHECK(apple_face_layout(name, nullptr) == AL::kJoyConSingle);
    for (const char* name : {"Xbox One", "DualSense", "DualShock 4", "Wireless Controller", "Switcheroo",
                            "Nintendo N64 Controller", "Nintendo NES Controller", "Nintendo Genesis Controller", ""})
        CHECK(apple_face_layout(name, nullptr) == AL::kStandard);
    CHECK(apple_face_layout(nullptr, nullptr) == AL::kStandard);
    CHECK(apple_face_layout("Xbox One", "Nintendo SNES-style pad") == AL::kStandard);
    CHECK(apple_face_layout("Nintendo GameCube Controller", nullptr) == AL::kStandard);
    CHECK(apple_face_layout("Nintendo Switch Joy-Con  (L / R)", nullptr) == AL::kNintendo);
    const FL raw[] = {FL::kA, FL::kB, FL::kX, FL::kY};
    const FL expected[] = {FL::kB, FL::kA, FL::kY, FL::kX};
    for (AL layout : {AL::kStandard, AL::kNintendo, AL::kJoyConSingle}) {
        FL labels[] = {raw[0], raw[1], raw[2], raw[3]};
        apple_face_labels(layout, labels);
        const int destinations[3][4] = {{kPadA, kPadB, kPadX, kPadY},
                                       {kPadB, kPadA, kPadY, kPadX},
                                       {kPadA, kPadX, kPadB, kPadY}};
        for (int i = 0; i < 4; ++i) {
            const int dest = destinations[int(layout)][i];
            CHECK(apple_face_input(layout, kPadA + i) == dest);
            CHECK(labels[dest - kPadA] == raw[i]);
            float values[kPadCount]{};
            values[apple_face_input(layout, kPadA + i)] = 0.73f;
            CHECK(values[dest] == 0.73f);
            Mapping m = Mapping::defaults();
            CHECK(controller_state(m, values).buttons == action_bit(
                dest == kPadB ? kA : dest == kPadA ? kB : dest == kPadY ? kX : kY));
        }
        for (int p = kPadLB; p < kPadCount; ++p) CHECK(apple_face_input(layout, p) == p);
        CHECK(apple_face_input(layout, kPadNone) == kPadNone);
        if (layout == AL::kNintendo) {
            for (int i = 0; i < 4; ++i) CHECK(labels[i] == expected[i]);
            CHECK(pad_family_from_labels(labels) == PadFamily::kNintendo);
            note_face_labels(labels);
            Mapping automatic = Mapping::defaults();
            set_face_auto(automatic, true);
            CHECK(automatic.face_auto && face_layout(automatic) == FaceLayout::kPosition);
            const auto style = prompt_style(automatic, pad_family_from_labels(labels));
            CHECK(style.face == FaceStyle::kOriginal && !style.positions && !style.active());
        }
    }
    // Parse representative framework SF Symbols and localized names before routing them.
    for (bool symbols : {false, true}) {
        const char* names[4] = {symbols ? "a.circle" : "Button A", symbols ? "b.circle.fill" : "Button B",
                                symbols ? "x.circle" : "Button X", symbols ? "y.circle.fill" : "Button Y"};
        FL labels[4];
        for (int i = 0; i < 4; ++i) labels[i] = face_label_from_text(names[i]);
        apple_face_labels(AL::kNintendo, labels);
        for (int i = 0; i < 4; ++i) CHECK(labels[i] == expected[i]);
        Mapping m = Mapping::defaults();
        note_face_labels(labels);
        set_face_auto(m, true);
        CHECK(face_layout(m) == FaceLayout::kPosition);
        CHECK(prompt_style(m, pad_family()).face == FaceStyle::kOriginal);
        for (int i = 0; i < 4; ++i) {
            float values[kPadCount]{};
            values[apple_face_input(AL::kNintendo, kPadA + i)] = 1;
            CHECK(controller_state(m, values).buttons == action_bit(kA + i));
            Mapping by_label = m;
            apply_face_layout(by_label, FaceLayout::kLabels);
            CHECK(controller_state(by_label, values).buttons == action_bit(
                i == 0 ? kB : i == 1 ? kA : i == 2 ? kY : kX));
        }
    }
    FL partial[] = {FL::kA, FL::kUnknown, FL::kX, FL::kUnknown};
    apple_face_labels(AL::kNintendo, partial);
    CHECK(partial[0] == FL::kUnknown && partial[1] == FL::kA &&
          partial[2] == FL::kUnknown && partial[3] == FL::kX);
    FL missing[4]{};
    apple_face_labels(AL::kNintendo, missing);
    for (FL label : missing) CHECK(label == FL::kUnknown);
    Mapping m = Mapping::defaults(), saved;
    CHECK(!apple_face_workaround(m, AL::kNintendo));
    apply_face_layout(m, FaceLayout::kLabels);
    CHECK(from_json(to_json(m), saved) && saved == m);
    CHECK(apple_face_workaround(saved, AL::kNintendo));
    CHECK(!apple_face_workaround(saved, AL::kStandard));
    CHECK(!apple_face_workaround(saved, AL::kJoyConSingle));
    // Detection never mutates a deliberate mapping, including non-face customizations.
    saved.pad[kL] = kPadRT;
    const Mapping before = saved;
    CHECK(apple_face_workaround(saved, AL::kNintendo) && saved == before);
    saved.face_auto = true;
    CHECK(!apple_face_workaround(saved, AL::kNintendo));
    saved.face_auto = false;
    for (int a = kA; a <= kY; ++a) {
        Mapping custom = saved;
        custom.pad[a] = kPadLB;
        CHECK(face_layout(custom) == FaceLayout::kCustom);
        CHECK(!apple_face_workaround(custom, AL::kNintendo));
    }
}

static void test_face_layout() {
    Mapping m = Mapping::defaults();
    // the default is the by-position (Nintendo) preset
    CHECK(face_layout(m) == FaceLayout::kPosition);
    CHECK(m.pad[kA] == kPadB && m.pad[kB] == kPadA && m.pad[kX] == kPadY && m.pad[kY] == kPadX);
    // by label (Xbox): A accepts/acts, B goes back (issue #78)
    apply_face_layout(m, FaceLayout::kLabels);
    CHECK(face_layout(m) == FaceLayout::kLabels);
    CHECK(m.pad[kA] == kPadA && m.pad[kB] == kPadB && m.pad[kX] == kPadX && m.pad[kY] == kPadY);
    float v[kPadCount] = {};
    v[kPadA] = 1;
    CHECK(controller_state(m, v).buttons == input::kA);
    v[kPadA] = 0; v[kPadB] = 1;
    CHECK(controller_state(m, v).buttons == input::kB);
    v[kPadB] = 0; v[kPadX] = 1;
    CHECK(controller_state(m, v).buttons == input::kX);
    // keyboard keys and the non-face controller bindings are not rewritten by the preset
    CHECK(m.keys[kA] == Mapping::defaults().keys[kA]);
    CHECK(m.keys[kB] == Mapping::defaults().keys[kB]);
    CHECK(m.keys[kX] == Mapping::defaults().keys[kX]);
    CHECK(m.keys[kY] == Mapping::defaults().keys[kY]);
    CHECK(m.pad[kL] == Mapping::defaults().pad[kL]);
    CHECK(m.pad[kZL] == Mapping::defaults().pad[kZL]);
    CHECK(m.pad[kPlus] == Mapping::defaults().pad[kPlus]);
    CHECK(m.pad[kStickLClick] == Mapping::defaults().pad[kStickLClick]);
    CHECK(m.pad[kLUp] == Mapping::defaults().pad[kLUp]);
    CHECK(m.pad[kScreenshot] == Mapping::defaults().pad[kScreenshot]);
    // one face binding by hand: neither preset
    m.pad[kX] = kPadLB;
    CHECK(face_layout(m) == FaceLayout::kCustom);
    // a preset restores all four; kCustom is a no-op
    apply_face_layout(m, FaceLayout::kPosition);
    CHECK(face_layout(m) == FaceLayout::kPosition);
    apply_face_layout(m, FaceLayout::kCustom);
    CHECK(face_layout(m) == FaceLayout::kPosition);
    // the labels are for the UI: present and distinct (the wording is free to change)
    CHECK(face_layout_label(FaceLayout::kPosition) && *face_layout_label(FaceLayout::kPosition));
    CHECK(face_layout_label(FaceLayout::kLabels) && *face_layout_label(FaceLayout::kLabels));
    CHECK(std::string(face_layout_label(FaceLayout::kPosition)) != face_layout_label(FaceLayout::kLabels));
}

// #88: the on-screen A B X Y act as the Wii U button drawn on them with either face preset
static void test_touch_face() {
    const uint32_t wiiu[4] = {input::kA, input::kB, input::kX, input::kY};
    for (FaceLayout layout : {FaceLayout::kPosition, FaceLayout::kLabels}) {
        Mapping m = Mapping::defaults();
        apply_face_layout(m, layout);
        for (int i = 0; i < 4; i++) {
            const uint32_t sdl = touch_face::buttons(1u << i, m);
            // the virtual pad's buttons as input_sdl.cpp reads them, then through the mapping
            float v[kPadCount] = {};
            for (int p = 0; p < kPadCount; p++)
                if (touch_face::sdl_button(p) >= 0 && (sdl >> touch_face::sdl_button(p) & 1)) v[p] = 1;
            CHECK(controller_state(m, v).buttons == wiiu[i]);
        }
    }
    Mapping m = Mapping::defaults();
    // the drawn A by position is the pad's east button (SDL 1), by label its south button (SDL 0)
    CHECK(touch_face::buttons(1, m) == 1u << 1);
    apply_face_layout(m, FaceLayout::kLabels);
    CHECK(touch_face::buttons(1, m) == 1u << 0);
    // a Wii U A bound to a trigger (custom) can't be pressed by a button bit; unbound neither
    set_pad_binding(m, kA, kPadLT);
    CHECK(touch_face::buttons(1, m) == 0);
    m.pad[kB] = kPadNone;
    CHECK(touch_face::buttons(2, m) == 0);
    CHECK(touch_face::buttons(0, Mapping::defaults()) == 0);
}

static void test_face_auto() {
    using FL = FaceLabel;
    // the four labels of a pad, south (kPadA), east (kPadB), west (kPadX), north (kPadY)
    const FL xbox[4] = {FL::kA, FL::kB, FL::kX, FL::kY};
    const FL ps[4] = {FL::kCross, FL::kCircle, FL::kSquare, FL::kTriangle};
    const FL nintendo[4] = {FL::kB, FL::kA, FL::kY, FL::kX};  // Switch Pro prints B A Y X
    const FL gc[4] = {FL::kA, FL::kX, FL::kB, FL::kY};        // GameCube prints A X B Y
    const FL none[4] = {FL::kUnknown, FL::kUnknown, FL::kUnknown, FL::kUnknown};
    int out[4] = {};

    // an Xbox pad (and a PlayStation one, whose Cross sits under the Xbox A): the by-label preset
    CHECK(face_bindings_from_labels(xbox, out));
    CHECK(out[0] == kPadA && out[1] == kPadB && out[2] == kPadX && out[3] == kPadY);
    CHECK(face_bindings_from_labels(ps, out));
    CHECK(out[0] == kPadA && out[1] == kPadB && out[2] == kPadX && out[3] == kPadY);
    // a Nintendo pad: the by-position preset, where the labels and the positions agree
    CHECK(face_bindings_from_labels(nintendo, out));
    CHECK(out[0] == kPadB && out[1] == kPadA && out[2] == kPadY && out[3] == kPadX);
    // a GameCube pad: a third shape, which reads back as custom
    CHECK(face_bindings_from_labels(gc, out));
    CHECK(out[0] == kPadA && out[1] == kPadX && out[2] == kPadB && out[3] == kPadY);
    // nothing printed: there is nothing to follow
    out[0] = out[1] = out[2] = out[3] = kPadNone;
    CHECK(!face_bindings_from_labels(none, out));
    CHECK(out[0] == kPadNone && out[3] == kPadNone);
    // one label only: the printed letter wins and the rest go to free buttons
    const FL one[4] = {FL::kA, FL::kUnknown, FL::kUnknown, FL::kUnknown};
    CHECK(face_bindings_from_labels(one, out));
    CHECK(out[0] == kPadA && out[1] != kPadA && out[2] != kPadA && out[3] != kPadA);
    CHECK(out[1] != out[2] && out[2] != out[3] && out[1] != out[3]);
    // every mapping stays free of double binds
    const FL* pads[] = {xbox, ps, nintendo, gc, one};
    for (const FL* labels : pads) {
        CHECK(face_bindings_from_labels(labels, out));
        for (int i = 0; i < 4; i++)
            for (int j = i + 1; j < 4; j++) CHECK(out[i] != out[j]);
    }

    // the Automatic preset follows the noted labels
    Mapping m = Mapping::defaults();
    CHECK(!m.face_auto && face_layout(m) == FaceLayout::kPosition);
    note_face_labels(nintendo);
    CHECK(std::string(pad_label(kPadA)) == "B (bottom)");
    CHECK(std::string(pad_label(kPadB)) == "A (right)");
    CHECK(std::string(pad_short_label(kPadX)) == "Y");
    CHECK(std::string(pad_short_label(kPadY)) == "X");
    set_face_auto(m, true);
    CHECK(m.face_auto && face_layout(m) == FaceLayout::kPosition);
    note_face_labels(xbox);
    set_face_auto(m, true);
    CHECK(m.face_auto && face_layout(m) == FaceLayout::kLabels);
    note_face_labels(gc);
    set_face_auto(m, true);
    CHECK(m.face_auto && face_layout(m) == FaceLayout::kCustom);
    // keyboard keys and the non-face controller bindings are left alone
    CHECK(m.keys[kA] == Mapping::defaults().keys[kA]);
    CHECK(m.keys[kY] == Mapping::defaults().keys[kY]);
    CHECK(m.pad[kL] == Mapping::defaults().pad[kL]);
    CHECK(m.pad[kZL] == Mapping::defaults().pad[kZL]);
    CHECK(m.pad[kPlus] == Mapping::defaults().pad[kPlus]);
    CHECK(m.pad[kLUp] == Mapping::defaults().pad[kLUp]);
    CHECK(m.pad[kScreenshot] == Mapping::defaults().pad[kScreenshot]);
    // turning it off leaves the bindings as they resolved
    set_face_auto(m, false);
    CHECK(!m.face_auto && face_layout(m) == FaceLayout::kCustom);
    // a hand-edited face binding drops the preset; a hand-edited one elsewhere does not
    note_face_labels(xbox);
    set_face_auto(m, true);
    CHECK(m.face_auto);
    set_pad_binding(m, kZL, kPadRT);
    CHECK(m.face_auto && m.pad[kZL] == kPadRT);
    set_pad_binding(m, kX, kPadLB);
    CHECK(!m.face_auto && face_layout(m) == FaceLayout::kCustom);
    // a preset is a choice of its own
    note_face_labels(xbox);
    set_face_auto(m, true);
    CHECK(m.face_auto);
    apply_face_layout(m, FaceLayout::kPosition);
    CHECK(!m.face_auto && face_layout(m) == FaceLayout::kPosition);

    // the live mapping follows the dominant pad's labels while Automatic is on
    std::string dir = (std::filesystem::temp_directory_path() /
                       ("input_map_auto." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
                          .string();
    std::error_code ec;
    CHECK(std::filesystem::create_directory(dir, ec));
    std::string path = dir + "/controls.json";
#ifdef _WIN32
    _putenv_s("WWHD_CONTROLS", path.c_str());
#else
    setenv("WWHD_CONTROLS", path.c_str(), 1);
#endif
    Mapping live = Mapping::defaults();
    note_face_labels(xbox);
    set_face_auto(live, true);
    set_current(live);
    CHECK(face_layout(current()) == FaceLayout::kLabels);
    note_face_labels(nintendo);  // the pad changed: the bindings follow
    CHECK(face_layout(current()) == FaceLayout::kPosition);
    uint32_t g = generation();
    note_face_labels(nintendo);  // the same pad again: nothing to do
    CHECK(generation() == g);
    note_face_labels(none);  // unplugged: the choice stands
    CHECK(face_layout(current()) == FaceLayout::kPosition && generation() == g);
    // and it comes back from the file with the preset
    Mapping r;
    CHECK(load_file(path, r) && r.face_auto && face_layout(r) == FaceLayout::kPosition);
    std::filesystem::remove_all(dir, ec);
}

// In-game button prompts (#143): the resolved style and the controls.json round trip
static void test_prompts() {
    using FL = FaceLabel;
    const FL xbox[4] = {FL::kA, FL::kB, FL::kX, FL::kY};
    const FL ps[4] = {FL::kCross, FL::kCircle, FL::kSquare, FL::kTriangle};
    const FL nintendo[4] = {FL::kB, FL::kA, FL::kY, FL::kX};
    const FL none[4] = {FL::kUnknown, FL::kUnknown, FL::kUnknown, FL::kUnknown};
    CHECK(pad_family_from_labels(xbox) == PadFamily::kXbox);
    CHECK(pad_family_from_labels(ps) == PadFamily::kPlayStation);
    CHECK(pad_family_from_labels(nintendo) == PadFamily::kNintendo);
    CHECK(pad_family_from_labels(none) == PadFamily::kUnknown);

    Mapping m = Mapping::defaults();
    CHECK(m.prompts == Mapping::Prompts::kAuto && m.shoulder_names == Mapping::ShoulderNames::kAuto && m.prompt_colours);
    // Full layout x appearance x family matrix. Automatic layout is resolved from labels.
    for (int layout = 0; layout < 4; ++layout)
        for (auto prompts : {Mapping::Prompts::kAuto, Mapping::Prompts::kOriginal,
                             Mapping::Prompts::kXbox, Mapping::Prompts::kPlayStation})
            for (auto family : {PadFamily::kXbox, PadFamily::kPlayStation, PadFamily::kNintendo, PadFamily::kUnknown}) {
                Mapping candidate = Mapping::defaults();
                if (layout == 1) apply_face_layout(candidate, FaceLayout::kLabels);
                if (layout == 2) {
                    note_face_labels(family == PadFamily::kXbox ? xbox : family == PadFamily::kPlayStation ? ps
                                     : family == PadFamily::kNintendo ? nintendo : none);
                    set_face_auto(candidate, true);
                }
                if (layout == 3) {
                    candidate.pad[kA] = kPadX; candidate.pad[kB] = kPadY;
                    candidate.pad[kX] = kPadA; candidate.pad[kY] = kPadB;
                }
                candidate.prompts = prompts;
                const bool labels = face_layout(candidate) == FaceLayout::kLabels;
                const FaceStyle expected = prompts == Mapping::Prompts::kOriginal ? FaceStyle::kOriginal
                    : prompts == Mapping::Prompts::kXbox ? FaceStyle::kXbox
                    : prompts == Mapping::Prompts::kPlayStation ? FaceStyle::kPlayStation
                    : family == PadFamily::kPlayStation ? FaceStyle::kPlayStation
                    : labels || family == PadFamily::kXbox ? FaceStyle::kXbox : FaceStyle::kOriginal;
                const auto actual = prompt_style(candidate, family);
                CHECK(actual.face == expected);
                CHECK(actual.positions == (labels && expected != FaceStyle::kOriginal));
                CHECK(actual.colours == (expected != FaceStyle::kOriginal));
                for (int i = 0; i < 4; ++i)
                    CHECK(actual.buttons[i] == (expected == FaceStyle::kOriginal ? i : candidate.pad[kA+i] - kPadA));
                // Every invalid/ambiguous face set falls back together.
                for (int invalid : {int(kPadNone), int(kPadLB), candidate.pad[kB]}) {
                    auto bad = candidate; bad.pad[kA] = invalid;
                    CHECK(prompt_style(bad, family).face == FaceStyle::kOriginal);
                }
            }
    // by label, automatic: Xbox look (LB/RB/LT/RT) unless the pad is a PlayStation one (L1/R1/L2/R2)
    apply_face_layout(m, FaceLayout::kLabels);
    PromptStyle s = prompt_style(m, PadFamily::kXbox);
    CHECK(s.face == FaceStyle::kXbox && s.colours && s.positions && s.shoulders == ShoulderStyle::kXbox);
    CHECK(prompt_style(m, PadFamily::kUnknown) == s);
    CHECK(prompt_style(m, PadFamily::kNintendo) == s);
    s = prompt_style(m, PadFamily::kPlayStation);
    CHECK(s.face == FaceStyle::kPlayStation && s.colours && s.positions && s.shoulders == ShoulderStyle::kNumbered);
    // white buttons at the Xbox positions (#143 a)
    m.prompt_colours = false;
    s = prompt_style(m, PadFamily::kXbox);
    CHECK(s.face == FaceStyle::kXbox && !s.colours && s.positions);
    m.prompt_colours = true;
    // fixed choices
    m.prompts = Mapping::Prompts::kPlayStation;
    CHECK(prompt_style(m, PadFamily::kXbox).face == FaceStyle::kPlayStation);
    m.prompts = Mapping::Prompts::kXbox;
    CHECK(prompt_style(m, PadFamily::kPlayStation).face == FaceStyle::kXbox);
    m.prompts = Mapping::Prompts::kOriginal;
    s = prompt_style(m, PadFamily::kXbox);
    CHECK(!s.active() && !s.positions && !s.colours);
    m.shoulder_names = Mapping::ShoulderNames::kNumbered;  // today's R1/R2 names, kept on request
    CHECK(prompt_style(m, PadFamily::kXbox).shoulders == ShoulderStyle::kNumbered);
    m.shoulder_names = Mapping::ShoulderNames::kNintendo;
    m.prompts = Mapping::Prompts::kAuto;
    CHECK(prompt_style(m, PadFamily::kXbox).shoulders == ShoulderStyle::kOriginal);
    // a fixed shoulder choice also applies by position (the shoulder bindings do not move)
    apply_face_layout(m, FaceLayout::kPosition);
    m.shoulder_names = Mapping::ShoulderNames::kXbox;
    s = prompt_style(m, PadFamily::kXbox);
    CHECK(s.face == FaceStyle::kXbox && !s.positions && s.shoulders == ShoulderStyle::kXbox && s.active());
    // custom face bindings: the game's face prompts
    apply_face_layout(m, FaceLayout::kLabels);
    m.pad[kX] = kPadLB;
    CHECK(prompt_style(m, PadFamily::kXbox).face == FaceStyle::kOriginal);

    // controls.json: defaults are not written; every choice comes back
    const Mapping d = Mapping::defaults();
    const std::string plain = to_json(d);
    CHECK(plain.find("button_prompts") == std::string::npos && plain.find("shoulder_names") == std::string::npos &&
          plain.find("button_colours") == std::string::npos);
    for (auto p : {Mapping::Prompts::kAuto, Mapping::Prompts::kOriginal, Mapping::Prompts::kXbox, Mapping::Prompts::kPlayStation})
        for (auto n : {Mapping::ShoulderNames::kAuto, Mapping::ShoulderNames::kNintendo, Mapping::ShoulderNames::kNumbered,
                       Mapping::ShoulderNames::kXbox})
            for (bool c : {true, false}) {
                Mapping w = d, r;
                w.prompts = p;
                w.shoulder_names = n;
                w.prompt_colours = c;
                std::string err;
                CHECK(from_json(to_json(w), r, &err) && err.empty() && r == w);
            }
    Mapping r;
    std::string err;
    CHECK(from_json("{\"options\": {\"button_prompts\": \"sega\", \"shoulder_names\": 3}}", r, &err));
    CHECK(r.prompts == Mapping::Prompts::kAuto && r.shoulder_names == Mapping::ShoulderNames::kAuto);
    CHECK(err.find("button_prompts") != std::string::npos);

    // a new pad family is a new generation: the prompts follow the pad that was plugged in
    note_face_labels(xbox);
    const uint32_t g = generation();
    note_face_labels(ps);
    CHECK(pad_family() == PadFamily::kPlayStation && generation() != g);
    const uint32_t g2 = generation();
    note_face_labels(none);  // unplugged: the last family stands
    CHECK(pad_family() == PadFamily::kPlayStation && generation() == g2);
    note_face_labels(xbox);
    CHECK(pad_family() == PadFamily::kXbox);
}

static void test_face_label_text() {
    using FL = FaceLabel;
    // the SF Symbol names GameController reports: the name first, the decoration after a dot.
    // The decoration must not decide: "circle" is B only as a name, never as ".circle".
    CHECK(face_label_from_text("a.circle") == FL::kA);
    CHECK(face_label_from_text("b.circle") == FL::kB);
    CHECK(face_label_from_text("x.circle") == FL::kX);
    CHECK(face_label_from_text("y.circle") == FL::kY);
    CHECK(face_label_from_text("a.circle.fill") == FL::kA);
    CHECK(face_label_from_text("xmark.circle") == FL::kA);       // PlayStation Cross
    CHECK(face_label_from_text("xmark.circle.fill") == FL::kA);
    CHECK(face_label_from_text("circle.circle") == FL::kB);      // PlayStation Circle
    CHECK(face_label_from_text("circle") == FL::kB);
    CHECK(face_label_from_text("square.circle") == FL::kX);      // PlayStation Square, not Circle
    CHECK(face_label_from_text("square.circle.fill") == FL::kX);
    CHECK(face_label_from_text("square.fill") == FL::kX);
    CHECK(face_label_from_text("triangle.circle") == FL::kY);
    CHECK(face_label_from_text("triangle") == FL::kY);
    CHECK(face_label_from_text("x.square.fill") == FL::kX);      // letter in a square, not Square
    // localized names and vendor-style names: the one word that is a letter
    CHECK(face_label_from_text("Button A") == FL::kA);
    CHECK(face_label_from_text("Button B") == FL::kB);
    CHECK(face_label_from_text("Cross Button") == FL::kA);
    CHECK(face_label_from_text("Circle Button") == FL::kB);
    CHECK(face_label_from_text("xbox.button.a") == FL::kA);
    CHECK(face_label_from_text("xbox.button.y") == FL::kY);
    CHECK(face_label_from_text("X Button") == FL::kX);
    // nothing a name can say: unknown
    CHECK(face_label_from_text(nullptr) == FL::kUnknown);
    CHECK(face_label_from_text("") == FL::kUnknown);
    CHECK(face_label_from_text("button") == FL::kUnknown);
    CHECK(face_label_from_text("fill") == FL::kUnknown);
    CHECK(face_label_from_text("gamepad") == FL::kUnknown);
}

static void test_names() {
    for (int a = 0; a < kActionCount; a++) CHECK(action_from_id(action_id(a)) == a);
    for (int p = 1; p < kPadCount; p++) CHECK(pad_from_id(pad_id(p)) == p);
    for (int k = 0; k < 256; k++) CHECK(key_from_id(key_id(k)) == k);
    CHECK(key_from_id("LeftShift") == kVK_Shift);
    CHECK(key_from_id("Nonsense") == kNoKey);
    CHECK(action_from_id("Nonsense") == -1);
}

static void test_reserved() {
    for (int k : std::initializer_list<int>{kVK_ANSI_9, kVK_ANSI_P, kVK_F12})
        CHECK(reserved_key(k) != nullptr);
    CHECK(reserved_key(kVK_ANSI_K) == nullptr);
    CHECK(reserved_key(kVK_ANSI_5) == nullptr);
}

static void test_conflicts() {
    Mapping m = Mapping::defaults();
    auto u = key_users(m, kVK_ANSI_K);
    CHECK(u.size() == 1 && u[0] == kA);
    CHECK(key_users(m, kVK_ANSI_K, kA).empty());
    m.keys[kB][1] = kVK_ANSI_K;  // K on A and B
    u = key_users(m, kVK_ANSI_K, kB);
    CHECK(u.size() == 1 && u[0] == kA);
    CHECK(key_users(m, kVK_ANSI_K).size() == 2);
    CHECK(press(m, {kVK_ANSI_K}).buttons == (input::kA | input::kB));
    auto p = pad_users(m, kPadA);
    CHECK(p.size() == 1 && p[0] == kB);
    CHECK(pad_users(m, kPadNone).empty());
}

static void test_json_roundtrip() {
    Mapping m = Mapping::defaults();
    std::string err;
    Mapping r;
    CHECK(from_json(to_json(m), r, &err) && err.empty());
    CHECK(r == m);
    // swap A and B everywhere, clear Home, change options
    std::swap(m.keys[kA], m.keys[kB]);
    std::swap(m.pad[kA], m.pad[kB]);
    m.keys[kHome] = {kNoKey, kNoKey};
    m.pad[kHome] = kPadNone;
    m.keys[kZR] = {kVK_ANSI_C, 93};  // unnamed code survives as "Key93"
    m.deadzone = 0.15f;
    m.invert_camera_y = true;
    std::string j = to_json(m);
    CHECK(j.find("\"Key93\"") != std::string::npos);
    CHECK(from_json(j, r, &err) && err.empty());
    CHECK(r == m);
    CHECK(press(r, {kVK_ANSI_K}).buttons == input::kB);
    CHECK(press(r, {kVK_ANSI_J}).buttons == input::kA);
    CHECK(press(r, {kVK_ANSI_H}).buttons == 0);
    CHECK(press(r, {kVK_UpArrow}).ry == 1);   // issue #167: the stick stays as it is (the camera inverts)
    CHECK(press(r, {kVK_ANSI_W}).ly == 1);    // move stick not inverted
    // the Automatic preset is the one face state that is stored
    CHECK(to_json(Mapping::defaults()).find("face_layout") == std::string::npos);
    m.face_auto = true;
    j = to_json(m);
    CHECK(j.find("\"face_layout\": \"auto\"") != std::string::npos);
    CHECK(from_json(j, r, &err) && err.empty() && r == m);
    CHECK(from_json(R"({"options": {"face_layout": "manual"}})", r, &err) && !r.face_auto);
    CHECK(from_json(R"({"options": {"face_layout": "auto"}})", r, &err) && r.face_auto);
}

static void test_json_partial_and_bad() {
    Mapping r;
    std::string err;
    // missing inputs keep defaults; a single string is accepted for one key; null unbinds
    CHECK(from_json(R"({"keyboard": {"A": "J", "B": ["K", "Space"]}, "controller": {"Home": null}})", r, &err));
    CHECK(err.empty());
    CHECK(r.keys[kA][0] == kVK_ANSI_J && r.keys[kA][1] == kNoKey);
    CHECK(r.keys[kB][0] == kVK_ANSI_K && r.keys[kB][1] == kVK_Space);
    CHECK(r.keys[kX] == Mapping::defaults().keys[kX]);
    CHECK(r.pad[kHome] == kPadNone && r.pad[kA] == kPadB);
    CHECK(r.deadzone == 0.15f && !r.invert_camera_y);
    // unknown names and app shortcuts are dropped with a warning, the rest still loads
    CHECK(from_json(R"({"keyboard": {"A": ["R", "K"], "Jump": ["J"], "B": ["Bogus"]}, "options": {"stick_deadzone": 5}})", r, &err));
    CHECK(!err.empty());
    CHECK(r.keys[kA][0] == kVK_ANSI_R && r.keys[kA][1] == kVK_ANSI_K);
    CHECK(r.keys[kB][0] == kNoKey);
    CHECK(r.deadzone == 0.9f);  // clamped
    // syntax errors fail and leave the output alone
    Mapping before = r;
    CHECK(!from_json("{\"keyboard\": {\"A\": [\"K\"}", r, &err) && !err.empty());
    CHECK(!from_json("[1, 2]", r, &err));
    CHECK(!from_json("", r, &err));
    CHECK(!from_json("{} x", r, &err));
    CHECK(r == before);
    CHECK(from_json("{}", r, &err) && r == Mapping::defaults());
}

static void test_deadzone() {
    Mapping m = Mapping::defaults();
    float drift[kPadCount] = {};
    // A resting stick with 1% noise must deliver exact neutral, not a slow zoom that leaves
    // the game's full zoom-blur preset selected. Cover both signs and both sticks.
    for (int axis : {kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight,
                     kPadRSUp, kPadRSDown, kPadRSLeft, kPadRSRight}) {
        drift[axis] = 0.01f;
        auto p = controller_state(m, drift);
        CHECK(p.lx == 0 && p.ly == 0 && p.rx == 0 && p.ry == 0);
        drift[axis] = 0;
    }
    drift[kPadLSUp] = drift[kPadLSRight] = 0.1f;
    CHECK(controller_state(m, drift).lx == 0 && controller_state(m, drift).ly == 0);
    drift[kPadLSUp] = drift[kPadLSRight] = 0;
    drift[kPadLSUp] = 1;
    CHECK(controller_state(m, drift).ly == 1);
    drift[kPadLSUp] = 0;
    // Unmarked zero was the old default; marked zero is an explicit raw-input preference.
    std::string err;
    Mapping raw;
    CHECK(from_json(R"({"options":{"stick_deadzone":0}})", raw, &err));
    CHECK(raw.deadzone == 0.15f);
    CHECK(to_json(raw).find("\"format\": 2") != std::string::npos);
    Mapping migrated;
    CHECK(from_json(to_json(raw), migrated, &err) && migrated == raw);
    CHECK(from_json(R"({"options":{"stick_deadzone":0.23}})", raw, &err));
    CHECK(raw.deadzone == 0.23f);
    CHECK(from_json(R"({"options":{"format":2,"stick_deadzone":0}})", raw, &err));
    CHECK(raw.deadzone == 0);
    CHECK(from_json(to_json(raw), migrated, &err) && migrated == raw);
    drift[kPadLSUp] = 0.01f;
    CHECK(controller_state(raw, drift).ly == 0.01f);
    CHECK(controller_state(m, drift).ly == 0);
    m.deadzone = 0.2f;
    float v[kPadCount] = {};
    v[kPadLSRight] = 0.15f;
    CHECK(controller_state(m, v).lx == 0);
    v[kPadLSRight] = 1.0f;
    CHECK(std::fabs(controller_state(m, v).lx - 1.0f) < 1e-6);
    v[kPadLSRight] = 0.6f;
    CHECK(std::fabs(controller_state(m, v).lx - 0.5f) < 1e-6);  // (0.6 - 0.2) / 0.8
    v[kPadLSRight] = 0; v[kPadRSDown] = 0.6f;
    m.invert_camera_y = true;
    CHECK(std::fabs(controller_state(m, v).ry + 0.5f) < 1e-6);  // issue #167: not inverted here
}

// issue #167: Invert camera up/down is camera only (camera_invert.h): the mapping hands the game the
// stick as it is, and only the camera's reads of it are inverted, so the Wind Waker baton is not
static void test_camera_invert() {
    using camera_invert::Use;
    Mapping m = Mapping::defaults();
    float v[kPadCount] = {};
    v[kPadRSUp] = 1.0f;
    for (bool inv : {false, true}) {
        m.invert_camera_y = inv;
        const float ry = controller_state(m, v).ry;
        CHECK(ry == 1.0f);                                                  // controller: raw either way
        CHECK(camera_invert::stick_y(ry, inv, Use::kBaton) == 1.0f);        // baton: conducts up
        CHECK(camera_invert::stick_y(ry, inv, Use::kFigureZoom) == 1.0f);   // figure viewer zoom
        CHECK(camera_invert::stick_y(ry, inv, Use::kCamera) == (inv ? -1.0f : 1.0f));  // camera inverted
        CHECK(camera_invert::mouse_pitch_sign(inv) == (inv ? -1.0f : 1.0f));
    }
    bool keys[256] = {};
    for (int k : m.keys[kRUp])
        if (k >= 0 && k < 256) keys[k] = true;
    m.invert_camera_y = true;
    CHECK(keyboard_state(m, keys).ry == 1.0f);  // keyboard: raw too
    // every reader of the right stick's Y is classified; only the camera's are hooked
    int camera = 0, baton = 0;
    for (const auto& site : camera_invert::kSites) {
        camera += site.use == Use::kCamera;
        baton += site.use == Use::kBaton;
        CHECK(camera_invert::inverts(site.use) == (site.use == Use::kCamera));
    }
    CHECK(camera == 5 && baton == 1);
    // The guest helper flips its delta for descending endpoints. Both input paths
    // must have the same final direction, independent of the live style order.
    const Mapping previous_mapping = current();
    for (bool inv : {false, true, false, true}) {
        m.invert_camera_y = inv;
        set_current(m, false);
        CHECK(invert_camera_y() == inv);  // Controls changes publish immediately
        for (double direction : {-1.0, 1.0}) {
            for (bool descending : {false, true}) {
                const double first = descending ? 10.0 : -10.0;
                const double second = -first;
                const double mouse = direction * camera_invert::mouse_pitch_sign(inv);
                const double stick = -camera_invert::stick_y(float(-direction), inv, Use::kCamera);
                for (double input : {mouse, stick}) {
                    double delta = camera_invert::manual_pitch_delta(input, first, second);
                    if (descending) delta = -delta;  // guest limited_range_addition
                    CHECK(delta == direction * (inv ? -1.0 : 1.0));
                }
            }
        }
    }
    set_current(previous_mapping, false);
    CHECK(!std::signbit(camera_invert::manual_pitch_delta(0.0, 10.0, -10.0)));
    CHECK(std::signbit(camera_invert::manual_pitch_delta(-0.0, 10.0, -10.0)));
    CHECK(camera_invert::manual_pitch_delta(1.0, 5.0, 5.0) == 1.0);
}

// Concurrent mapping writers must leave the camera and Controls in agreement.
// Check only while both workers are parked: a read across an active update may
// legitimately observe different generations, but a settled disagreement is stale.
static void test_live_camera_invert_publication() {
    const Mapping previous = current();
    auto on = Mapping::defaults(), off = on;
    on.invert_camera_y = true;
    constexpr unsigned rounds = 2048;
    std::barrier phase(3);
    auto writer = [&](Mapping m) {
        for (unsigned round = 0; round < rounds; ++round) {
            phase.arrive_and_wait();
            for (unsigned update = 0; update < 8; ++update) set_current(m, false);
            phase.arrive_and_wait();
        }
    };
    std::thread first(writer, on), second(writer, off);
    for (unsigned round = 0; round < rounds; ++round) {
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        CHECK(current().invert_camera_y == invert_camera_y());
    }
    first.join();
    second.join();
    set_current(previous, false);
}

// issue #40: per-stick range, applied radially after the dead zone
static void test_stick_range() {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
    Mapping m = Mapping::defaults();
    CHECK(m.left_range == 1.0f && m.right_range == 1.0f);
    m.deadzone = 0;
    float v[kPadCount] = {};
    // default 1.0 is the identity
    v[kPadRSDown] = 0.9f;
    v[kPadLSRight] = 0.37f;
    auto p = controller_state(m, v);
    CHECK(p.ry == -0.9f && p.lx == 0.37f);
    // a stick that stops at 0.9 reaches full deflection at 1.11, only on the stick that has it
    m.right_range = 1.11f;  // 0.9 * 1.11 = 0.999: full deflection to the game's resolution
    p = controller_state(m, v);
    CHECK(std::fabs(p.ry + 1.0f) < 1.5e-3f && p.ry >= -1.0f);
    CHECK(p.lx == 0.37f);
    m.right_range = 1.12f;  // past 1/0.9 the result is exactly 1
    CHECK(controller_state(m, v).ry == -1.0f);
    // the magnitude is capped at 1
    m.right_range = 1.5f;
    v[kPadRSDown] = 1.0f;
    CHECK(controller_state(m, v).ry == -1.0f);
    v[kPadRSDown] = 0.5f;
    CHECK(near(controller_state(m, v).ry, -0.75f));
    // diagonal input keeps its angle and is capped radially, not per axis
    v[kPadRSDown] = 0;
    v[kPadLSUp] = 0.6f;
    v[kPadLSRight] = 0.3f;
    m.left_range = 1.5f;
    p = controller_state(m, v);
    CHECK(near(p.ly / p.lx, 2.0f));
    CHECK(near(std::sqrt(p.lx * p.lx + p.ly * p.ly), 1.0f));
    v[kPadLSUp] = 0.4f;  // length 0.5 -> 0.75, angle kept
    p = controller_state(m, v);
    CHECK(near(p.lx, 0.3f * 1.5f) && near(p.ly, 0.4f * 1.5f));
    // dead zone first, then range: 0.6 with a 0.2 dead zone is 0.5, times 1.5 is 0.75
    Mapping d = Mapping::defaults();
    d.deadzone = 0.2f;
    d.left_range = 1.5f;
    float w[kPadCount] = {};
    w[kPadLSRight] = 0.6f;
    CHECK(near(controller_state(d, w).lx, 0.75f));
    w[kPadLSRight] = 0.15f;  // inside the dead zone stays exactly neutral
    CHECK(controller_state(d, w).lx == 0);
    // out-of-range values in the struct are clamped when applied
    d.deadzone = 0;
    d.left_range = 3.0f;
    w[kPadLSRight] = 0.5f;
    CHECK(near(controller_state(d, w).lx, 0.75f));
    d.left_range = 0.5f;
    CHECK(controller_state(d, w).lx == 0.5f);
    // save / load round trip, new keys next to the format marker
    std::string err;
    Mapping s = Mapping::defaults(), r;
    s.left_range = 1.25f;
    s.right_range = 1.11f;
    std::string j = to_json(s);
    CHECK(j.find("\"left_stick_range\": 1.25") != std::string::npos);
    CHECK(j.find("\"right_stick_range\": 1.11") != std::string::npos);
    CHECK(j.find("\"format\": 2") != std::string::npos);
    CHECK(from_json(j, r, &err) && err.empty() && r == s);
    // older files without the keys load at 1.0 (and the rest unchanged)
    CHECK(from_json(R"({"options":{"format":2,"stick_deadzone":0.2,"invert_camera_y":true}})", r, &err));
    CHECK(r.left_range == 1.0f && r.right_range == 1.0f && r.deadzone == 0.2f && r.invert_camera_y);
    CHECK(from_json(R"({"options":{"stick_deadzone":0}})", r, &err));
    CHECK(r.left_range == 1.0f && r.right_range == 1.0f && r.deadzone == 0.15f);
    // out-of-range and wrongly typed values
    CHECK(from_json(R"({"options":{"left_stick_range":7,"right_stick_range":0.2}})", r, &err));
    CHECK(r.left_range == 1.5f && r.right_range == 1.0f);
    CHECK(from_json(R"({"options":{"left_stick_range":-1,"right_stick_range":"big"}})", r, &err));
    CHECK(r.left_range == 1.0f && r.right_range == 1.0f);
}

static void test_files() {
    std::string dir=(std::filesystem::temp_directory_path()/("input_map_test."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    std::error_code ec;
    CHECK(std::filesystem::create_directory(dir,ec));
    std::string path = std::string(dir) + "/sub/dir/controls.json";  // directories are created
    Mapping m = Mapping::defaults();
    std::swap(m.keys[kA], m.keys[kB]);
    CHECK(save_file(path, m));
    Mapping r;
    std::string err;
    CHECK(load_file(path, r, &err) && r == m);
    CHECK(!load_file(std::string(dir) + "/missing.json", r, &err));
    // the live mapping: WWHD_CONTROLS points it at our file
#ifdef _WIN32
    _putenv_s("WWHD_CONTROLS",path.c_str());
#else
    setenv("WWHD_CONTROLS", path.c_str(), 1);
#endif
    CHECK(default_path() == path);
    load_startup();
    CHECK(current() == m);
    uint32_t g = generation();
    set_current(Mapping::defaults());
    CHECK(generation() != g);
    CHECK(load_file(path, r, &err) && r == Mapping::defaults());  // saved
    // the overlay's menus and on-screen keyboard confirm/cancel with the Wii U's A/B of the live
    // mapping: by position (the default) the right button is A and the bottom one is B
    CHECK(face_input(kA) == kPadB && face_input(kB) == kPadA && face_input(kX) == kPadY && face_input(kY) == kPadX);
    Mapping labels = Mapping::defaults();
    apply_face_layout(labels, FaceLayout::kLabels);
    set_current(labels, false);
    CHECK(face_input(kA) == kPadA && face_input(kB) == kPadB && face_input(kX) == kPadX && face_input(kY) == kPadY);
    labels.pad[kA] = kPadNone;  // unbound: the by-position button
    set_current(labels, false);
    CHECK(face_input(kA) == kPadB && face_input(kB) == kPadB);
    set_current(Mapping::defaults(), false);
    std::filesystem::remove_all(dir,ec);
}

static void test_conflict_helpers() {
    Mapping m = Mapping::defaults();
    CHECK(conflict_count(m) == 0);
    for (int a = 0; a < kActionCount; a++) CHECK(!has_conflict(m, a));
    m.keys[kZR][1] = kVK_ANSI_K;  // K is also A's key
    CHECK(has_conflict(m, kZR) && has_conflict(m, kA) && !has_conflict(m, kB));
    CHECK(conflict_count(m) == 1);
    m.pad[kX] = m.pad[kY];        // two actions on one controller button
    CHECK(has_conflict(m, kX) && has_conflict(m, kY));
    CHECK(conflict_count(m) == 2);
    m.pad[kX] = kPadNone;
    CHECK(!has_conflict(m, kX) && !has_conflict(m, kY) && conflict_count(m) == 1);
    CHECK(key_short_label(kVK_Shift) == "L ⇧");
    CHECK(key_short_label(kVK_ANSI_Keypad5) == "Num 5");
    CHECK(key_short_label(kVK_ANSI_K) == "K");
    CHECK(std::string(pad_short_label(kPadLT)) == "LT");
    CHECK(std::string(pad_short_label(kPadNone)).empty());
    for (int p = 1; p < kPadCount; p++) CHECK(*pad_short_label(p));
}

static void test_binding_capture_faces() {
    for (int face : {kPadA, kPadB, kPadX, kPadY}) {
        overlay::BindingCapture capture;
        float values[kPadCount] = {};
        values[kPadB] = 1;
        CHECK(capture.poll(values) == -1);  // the confirm that opened capture
        values[kPadB] = 0;
        CHECK(capture.poll(values) == -1);
        values[face] = 1;
        CHECK(capture.poll(values) == face);
        Mapping m = Mapping::defaults();
        for (int action = kA; action <= kY; ++action) {
            set_pad_binding(m, action, face);
            CHECK(controller_state(m, values).buttons & action_bit(action));
        }
        capture.reset();
        CHECK(capture.poll(values) == -1);
    }
}

static void test_android_odin_mapping() {
    const std::string old = "03000000,name,a:b0,b:b1,back:b4,start:b6,leftx:a0,lefty:a1,face:abxy,";
    CHECK(android_controller::mapping(0x045e, 0x111, old) == old);
    CHECK(android_controller::mapping(0x2020, 0x112, old) == old);
    const auto fixed = android_controller::mapping(0x2020, 0x111, old);
    CHECK(fixed.find("a:b1,b:b0,x:b3,y:b2,face:bayx,") != std::string::npos);
    CHECK(fixed.find("back:b4,start:b6,leftx:a0,lefty:a1,") != std::string::npos);
    CHECK(fixed.find("a:b0,") == std::string::npos);
    CHECK(android_controller::mapping(0x2020, 0x111, fixed) == fixed);
    Mapping repaired;
    std::string warning;
    CHECK(from_json(R"({"controller":{"A":null,"B":null,"X":null,"Y":null,"Home":null}})", repaired, &warning));
    for (int a = kA; a <= kY; ++a) {
        CHECK(repaired.pad[a] == Mapping::defaults().pad[a]);
        CHECK(warning.find(std::string("null face binding ") + action_id(a) + " repaired") != std::string::npos);
    }
    CHECK(repaired.pad[kHome] == kPadNone);
}

static void test_settings_shortcut() {
    using S = overlay::SettingsShortcut;
    S s;
    CHECK(s.poll(10, true, false, false) == S::None);
    CHECK(s.poll(10.499, true, false, false) == S::None);
    CHECK(s.poll(10.5, true, false, false) == S::Open);
    CHECK(s.poll(13, true, false, true) == S::None);  // held: never toggle again
    CHECK(s.poll(14, false, false, true) == S::None);
    CHECK(s.poll(15, true, false, true) == S::Close);
    CHECK(s.poll(16, false, false, false) == S::None);
    CHECK(s.poll(17, true, false, false) == S::None);
    CHECK(s.poll(17.2, false, false, false) == S::None);  // short Minus stays in the game
    CHECK(s.poll(18, false, true, false) == S::Open);
    CHECK(s.poll(19, false, true, true) == S::None);
    CHECK(s.poll(20, false, false, true) == S::None);
    CHECK(s.poll(21, false, true, true) == S::Close);
    // Input polls continue while the renderer is stalled for three seconds.
    S stalled;
    double opened = -1;
    for (int i = 0; i <= 3000; ++i)
        if (stalled.poll(i / 1000.0, true, false, false) == S::Open) opened = i / 1000.0;
    CHECK(opened == 0.5);
}

static void test_shortcuts() {
    auto m = Mapping::defaults();
    CHECK(shortcut_for_key(m, kVK_F6, 0) == kLoad1);
    CHECK(shortcut_for_key(m, kVK_F1, 0) == kSettings);
    CHECK(shortcut_for_key(m, kVK_Escape, 0) == kSettings);
    CHECK(shortcut_for_key(m, kVK_F1, kModShift) == kSave1);
    CHECK(shortcut_for_key(m, kVK_F10, 0) == kPhoto);
    for (int a = 0; a < kShortcutCount; ++a) {
        CHECK(*shortcut_info(a).id && *shortcut_info(a).name && *shortcut_info(a).group);
        CHECK(!shortcut_conflict(m, a));
        for (int b = a + 1; b < kShortcutCount; ++b) CHECK(std::string(shortcut_info(a).id) != shortcut_info(b).id);
    }
    for (int a = kPiP; a < kShortcutCount; ++a) {
        CHECK(m.shortcuts[a].keys[0].code == kNoKey);
        CHECK(m.shortcuts[a].keys[1].code == kNoKey);
        CHECK(!m.shortcuts[a].hold && !m.shortcuts[a].button);
    }
    CHECK(m.shortcuts[kSettings].button==kPadHome && !m.shortcuts[kSettings].hold);
    CHECK(!reserved_key(kVK_Escape));
    // Settings: Esc first, F1 as the alternate, Home on a controller.
    CHECK(m.shortcuts[kSettings].keys[0] == ShortcutKey{kVK_Escape} && m.shortcuts[kSettings].keys[1] == ShortcutKey{kVK_F1});
    CHECK(shortcut_for_key(m, kVK_Escape, 0) == kSettings && settings_reachable(m, false));
    {
        // Files written before Esc opened Settings: only the untouched default moves to Esc + F1.
        auto legacy = [](std::array<ShortcutKey, 2> keys, int escGameInput = -1) {
            Mapping old = Mapping::defaults();
            old.shortcuts[kSettings].keys = keys;
            if (escGameInput >= 0) old.keys[escGameInput][1] = kVK_Escape;
            std::string text = to_json(old);
            const std::string now = "\"shortcuts_version\": " + std::to_string(kShortcutsVersion);
            const size_t at = text.find(now);
            CHECK(at != std::string::npos);
            return std::pair{text.replace(at, now.size(), "\"shortcuts_version\": 2"), old};
        };
        const auto defaults = shortcut_defaults()[kSettings].keys;
        Mapping loaded;
        for (auto keys : {std::array<ShortcutKey, 2>{{{kVK_F1}, {}}}, std::array<ShortcutKey, 2>{{{kVK_F1}, {kVK_ANSI_Comma, kModSuper}}}}) {
            auto [text, old] = legacy(keys);
            CHECK(from_json(text, loaded) && loaded.shortcuts[kSettings].keys == defaults);
            CHECK(loaded.shortcuts[kSettings].button == kPadHome);
            loaded.shortcuts[kSettings].keys = old.shortcuts[kSettings].keys; CHECK(loaded == old); // nothing else changes
        }
        // Saved custom bindings are kept, as is F1 when a game input already uses Esc.
        for (auto keys : {std::array<ShortcutKey, 2>{{{kVK_F9}, {kVK_F1}}}, std::array<ShortcutKey, 2>{{{kVK_F1}, {kVK_F8, kModAlt}}},
                          std::array<ShortcutKey, 2>{{{}, {kVK_F1}}}, std::array<ShortcutKey, 2>{{{kVK_F1, kModCtrl}, {}}}}) {
            auto [text, old] = legacy(keys);
            CHECK(from_json(text, loaded) && loaded == old);
        }
        auto [text, old] = legacy({{{kVK_F1}, {}}}, kB);
        CHECK(from_json(text, loaded) && loaded == old && loaded.shortcuts[kSettings].keys[0] == ShortcutKey{kVK_F1});
        // A current file is never migrated: a player who removed Esc keeps F1 alone.
        Mapping f1Only = Mapping::defaults(); f1Only.shortcuts[kSettings].keys = {{{kVK_F1}, {}}};
        CHECK(to_json(f1Only).find("\"shortcuts_version\": 3") != std::string::npos);
        CHECK(from_json(to_json(f1Only), loaded) && loaded == f1Only);
        // A file without shortcuts gets the current defaults.
        CHECK(from_json(R"({"keyboard":{"A":["K"]}})", loaded) && loaded.shortcuts[kSettings].keys == defaults);
    }
    for (int p=kPadCapture;p<kPadCount;++p) {
        CHECK(*pad_id(p) && *pad_label(p) && *pad_short_label(p));
        m.pad[kZR]=p; Mapping round; CHECK(from_json(to_json(m),round)); CHECK(round==m);
        float raw[kPadCount]{};raw[p]=1;CHECK(controller_state(m,raw).buttons&input::kZR);
    }
    m=Mapping::defaults();
    CHECK(from_json(R"({"shortcuts":{"Settings":{"keys":[{"key":"F9","modifiers":0}],"hold":"","button":""}}})",m));
    CHECK(m.shortcuts[kSettings].keys[0].code==kVK_F9 && m.shortcuts[kSettings].button==kPadHome);
    m.shortcuts[kSettings].button=0;
    Mapping round;CHECK(from_json(to_json(m),round));CHECK(!round.shortcuts[kSettings].button);
    m=Mapping::defaults();
    m.shortcuts[kPhoto].keys[1] = {kVK_ANSI_K, kModCtrl};
    CHECK(!shortcut_conflict(m, kPhoto)); // a Ctrl/Alt/Cmd shortcut takes its key press before the game
    m.shortcuts[kPhoto].keys[1] = {kVK_ANSI_K, kModShift};
    CHECK(shortcut_conflict(m, kPhoto) && has_conflict(m, kA)); // plain or Shift keys share the game's K
    m.shortcuts[kPhoto].keys[1] = {kVK_ANSI_K, 0};
    CHECK(shortcut_conflict(m, kPhoto));
    m.shortcuts[kPhoto].keys[1] = m.shortcuts[kSettings].keys[0];
    CHECK(shortcut_conflict(m, kPhoto) && shortcut_conflict(m, kSettings));
    m.shortcuts[kSettings] = {};
    CHECK(settings_reachable(m)); // immutable Home / Select fallback
    CHECK(!settings_reachable(m, false));
    m.shortcuts[kSettings].keys[1] = {kVK_F9, kModAlt};
    CHECK(settings_reachable(m, false));
    m.shortcuts[kPhoto].keys = {{{kVK_F9, kModShift | kModCtrl}, {kVK_F8, kModAlt}}};
    m.shortcuts[kPhoto].hold = kPadOptions; m.shortcuts[kPhoto].button = kPadDUp;
    CHECK(shortcut_for_key(m, kVK_F9, kModShift | kModCtrl) == kPhoto);
    CHECK(shortcut_for_key(m, kVK_F10, 0) == -1);
    Mapping out; CHECK(from_json(to_json(m), out)); CHECK(out == m);
    CHECK(from_json(R"({"keyboard":{"Screenshot":["F9","F8"]},"controller":{"Screenshot":"RightStickButton"}})", out));
    CHECK(out.shortcuts[kPhoto].keys[0].code == kVK_F9 && out.shortcuts[kPhoto].keys[1].code == kVK_F8);
    CHECK(out.pad[kScreenshot] == kPadR3);
    CHECK(from_json(R"({"keyboard":{"Screenshot":["Mouse4"]}})", out));
    CHECK(out.shortcuts[kPhoto].keys[0].code == kMouse4);
    CHECK(from_json(to_json(out), m)); CHECK(out == m);
    m = Mapping::defaults();
    m.shortcuts[kPhoto].hold = kPadOptions; m.shortcuts[kPhoto].button = kPadDUp;
    float raw[kPadCount]{}, game[kPadCount]{}; ShortcutPoll poll;
    raw[kPadOptions] = 1; std::copy(std::begin(raw), std::end(raw), game);
    CHECK(poll.poll(m, raw, game).empty()); CHECK(game[kPadOptions] == 1);
    raw[kPadDUp] = 1; raw[kPadA] = 1; std::copy(std::begin(raw), std::end(raw), game);
    CHECK(poll.poll(m, raw, game) == std::vector<int>{kPhoto});
    CHECK(game[kPadOptions] == 0 && game[kPadDUp] == 0 && game[kPadA] == 1);
    CHECK(poll.poll(m, raw, game).empty());
    raw[kPadDUp] = 0; std::copy(std::begin(raw), std::end(raw), game);
    CHECK(poll.poll(m, raw, game).empty()); CHECK(game[kPadOptions] == 1);
    raw[kPadDUp] = 1; CHECK(poll.poll(m, raw, game) == std::vector<int>{kPhoto});
    m.shortcuts[kMute].hold = kPadDUp; m.shortcuts[kMute].button = kPadOptions;
    CHECK(shortcut_conflict(m, kPhoto) && shortcut_conflict(m, kMute));
}

// Settings sizes by screen (mobile_ui.h): phones a compact layout, tablets and handhelds as before, desktop as before
static void test_screen_layouts() {
    struct Screen { const char* name; float w, h, dpi; bool phone; };
    const Screen screens[] = {
        {"OnePlus 8 Pro 6.78\" at 1080p", 2400, 1080, 388, true},
        {"OnePlus 8 Pro portrait", 1080, 2400, 388, true},
        {"OnePlus 8 Pro at 1440p", 3168, 1440, 513, true},
        {"Galaxy S25 6.2\"", 2340, 1080, 416, true},
        {"Galaxy S25 portrait", 1080, 2340, 416, true},
        {"Pixel 2 XL 18:9 6\"", 2880, 1440, 538, true},
        {"Retroid Pocket 5 5.5\" 16:9", 1920, 1080, 401, false},
        {"AYN Thor / Odin 2 6\" 16:9", 1920, 1080, 367, false},
        {"Odin 2 Portal 7\" 16:9", 1920, 1080, 315, false},
        {"Retroid Pocket Mini 3.9\" 4:3-ish", 1240, 1080, 420, false},
        {"tablet 11\" 16:10", 2560, 1600, 274, false},
        {"foldable inner 7.6\"", 2208, 1768, 373, false},
        {"a long tablet 10\" 21:9 (too large for a phone)", 2560, 1080, 278, false},
    };
    for (const Screen& s : screens) {
        const mobile_ui::Layout a = mobile_ui::layout(s.dpi, s.dpi / 160, mobile_ui::kAutomatic, s.w, s.h);
        CHECK(a.phone == s.phone);
        const float classic = mobile_ui::scale(s.dpi, s.dpi / 160, 0);  // today's size
        if (s.phone) {
            // smaller text than before (2.2 mm instead of 2.8), controls still 48 dp = 7.6 mm to tap
            CHECK(std::fabs(a.control_mm(s.dpi) - 7.62f) < 0.01f);
            CHECK(std::fabs(a.text_mm(s.dpi) - 2.20f) < 0.02f);
            CHECK(a.scale < classic * 0.81f);
            // the player's choice decides: Small < Automatic < Medium < Large, every control at least 7 mm
            const float mm[] = {0, 7, 8, 9};
            float last = 0;
            for (int p : {1, 0, 2, 3}) {
                const mobile_ui::Layout l = mobile_ui::layout(s.dpi, s.dpi / 160, p, s.w, s.h);
                CHECK(l.phone && l.scale > last && l.control_mm(s.dpi) >= 7.0f - 0.01f);
                if (p) CHECK(std::fabs(l.control_mm(s.dpi) - mm[p]) < 0.01f);
                last = l.scale;
            }
        } else {
            // tablets and handhelds: today's size and padding, 8 mm controls, for every UI size
            for (int p = 0; p < 4; ++p) {
                const mobile_ui::Layout l = mobile_ui::layout(s.dpi, s.dpi / 160, p, s.w, s.h);
                CHECK(!l.phone && l.pad_y == 16 && l.scale == mobile_ui::scale(s.dpi, s.dpi / 160, p));
            }
            CHECK(std::fabs(a.control_mm(s.dpi) - 8.0f) < 0.01f);
        }
        std::printf("  %-48s %s: scale %.2f (was %.2f), control %.1f mm, text %.1f mm, %4.0fx%-4.0f layout px\n", s.name,
                    a.phone ? "phone   " : "handheld", a.scale, classic, a.control_mm(s.dpi), a.text_mm(s.dpi), s.w / a.scale, s.h / a.scale);
    }
    // an unknown screen shape keeps the tablet layout
    CHECK(!mobile_ui::layout(400, 2.5f, 0).phone);
    // desktop: unchanged (backing scale, larger on tall windows)
    CHECK(mobile_ui::desktop_scale(1, 752) == 1.0f && mobile_ui::desktop_scale(2, 1504) == 2.0f);
    CHECK(std::fabs(mobile_ui::desktop_scale(1, 2160) - 2160 / 820.0f) < 1e-5f);
}


// ---- P6: virtual inputs, per-controller bindings, the move / take-off rule, L and d-pad down in play

static void test_virtual_actions() {
    Mapping m = Mapping::defaults();
    CHECK(is_virtual(kJump) && is_virtual(kMoonJump) && !is_virtual(kL) && !is_virtual(kScreenshot));
    CHECK(action_bit(kJump) == 0 && action_bit(kMoonJump) == 0);
    CHECK(action_virtual(kJump) == input::kVirtJump && action_virtual(kMoonJump) == input::kVirtMoonJump);
    CHECK(action_from_id("Jump") == kJump && action_from_id("MoonJump") == kMoonJump);
    // Q and L1 press the Wii U's L and both virtual inputs; the virtual ones never set a game button
    auto k = press(m, {kVK_ANSI_Q});
    CHECK(k.buttons == input::kL && k.virt == (input::kVirtJump | input::kVirtMoonJump));
    float v[kPadCount] = {};
    v[kPadLB] = 1;
    auto c = controller_state(m, v);
    CHECK(c.buttons == input::kL && c.virt == (input::kVirtJump | input::kVirtMoonJump));
    // a virtual action alone on a face button: the game gets nothing from that button
    CHECK(bind_virtual(m, kJump, kPadR3, Displace::kTakeOff));
    v[kPadLB] = 0; v[kPadR3] = 1;
    c = controller_state(m, v);
    CHECK(c.buttons == 0 && c.virt == input::kVirtJump);
    // combine: buttons and virtual bits add up, sticks follow the furthest
    input::PadState a, b;
    a.buttons = input::kA; a.lx = .2f; b.virt = input::kVirtJump; b.lx = -.9f; b.ry = .5f;
    auto s = combine(a, b);
    CHECK(s.buttons == input::kA && s.virt == input::kVirtJump && s.lx == -.9f && s.ry == .5f);
}

static void test_play_buttons() {
    Mapping m = Mapping::defaults();
    // L leaves the game in play (option off), stays in menus; with the option it always reaches it
    CHECK(play_buttons(m, input::kL | input::kA, true) == input::kA);
    CHECK(play_buttons(m, input::kL | input::kA, false) == (input::kL | input::kA));
    m.l_turns_page = true;
    CHECK(play_buttons(m, input::kL, true) == input::kL);
    m.l_turns_page = false;
    // an item button moved to d-pad down: the item in play, d-pad down in menus
    CHECK(bind_virtual(m, kJump, m.pad[kY], Displace::kMoveToDDown));
    CHECK(m.pad[kY] == kPadDDown && m.pad[kDDown] == kPadDDown && !has_conflict(m, kY) && !has_conflict(m, kDDown));
    float v[kPadCount] = {};
    v[kPadDDown] = 1;
    const uint32_t both = controller_state(m, v).buttons;
    CHECK(both == (input::kY | input::kDown));
    CHECK(play_buttons(m, both, true) == input::kY);
    CHECK(play_buttons(m, both, false) == input::kDown);
    // d-pad down pressed on its own (another input of the same bindings is not shared): kept
    CHECK(play_buttons(m, input::kDown, true) == input::kDown);
    // unrelated buttons held together are not filtered
    Mapping d = Mapping::defaults();
    CHECK(play_buttons(d, input::kDown | input::kY, true) == (input::kDown | input::kY));
}

static void test_displace_rules() {
    // Move (L1 first): the face button that was B moves to L1, which then no longer presses L
    Mapping m = Mapping::defaults();
    const int bottom = m.pad[kB];  // by position: the bottom face button is the Wii U's B
    CHECK(displaced_by(m, bottom) == std::vector<int>{kB});
    CHECK(displaced_by(m, kPadLB).empty());  // L (unused in play) does not count
    CHECK(move_target(m, kB) == kPadLB);
    CHECK(bind_virtual(m, kJump, bottom, Displace::kMove));
    CHECK(m.pad[kJump] == bottom && m.pad[kB] == kPadLB && m.pad[kL] == kPadNone);
    CHECK(!has_conflict(m, kB) && !has_conflict(m, kJump));
    // the moon jump, which sat on L1 with Jump, comes along (on one input the moon jump wins)
    CHECK(m.pad[kMoonJump] == bottom && !has_conflict(m, kMoonJump));
    // L1 is taken now: the next Move goes to d-pad down
    CHECK(move_target(m, kX) == kPadDDown);
    const int left = m.pad[kY];
    CHECK(bind_virtual(m, kMoonJump, left, Displace::kMove));
    CHECK(m.pad[kY] == kPadDDown && m.pad[kMoonJump] == left);
    // nowhere left to move to: Move and Move to L1 / d-pad down refuse and change nothing
    Mapping before = m;
    CHECK(move_target(m, kA) == kPadNone);
    CHECK(!bind_virtual(m, kJump, m.pad[kA], Displace::kMove));
    CHECK(!bind_virtual(m, kJump, m.pad[kA], Displace::kMoveToL1));
    CHECK(!bind_virtual(m, kJump, m.pad[kA], Displace::kMoveToDDown));
    CHECK(m == before);
    // Take off: the item button can't be pressed from the controller any more
    Mapping t = Mapping::defaults();
    const int top = t.pad[kX];
    CHECK(bind_virtual(t, kJump, top, Displace::kTakeOff));
    CHECK(t.pad[kX] == kPadNone && t.pad[kJump] == top && t.pad[kL] == kPadLB);
    float v[kPadCount] = {};
    v[top] = 1;
    CHECK(controller_state(t, v).buttons == 0 && controller_state(t, v).virt == input::kVirtJump);
    // Move to L1 for an item button: its item goes there (by its Wii U button), L leaves L1
    Mapping l = Mapping::defaults();
    CHECK(bind_virtual(l, kJump, l.pad[kR], Displace::kMoveToL1));
    CHECK(l.pad[kR] == kPadLB && l.pad[kL] == kPadNone && l.pad[kJump] == kPadRB);
    // a free input (L1 / unbound): bound at once, nothing displaced, whatever is chosen
    Mapping f = Mapping::defaults();
    CHECK(bind_virtual(f, kJump, kPadLB, Displace::kTakeOff) && f.pad[kL] == kPadLB);
    CHECK(bind_virtual(f, kJump, kPadMisc2, Displace::kMove) && f.pad[kJump] == kPadMisc2);
    // only virtual actions and real inputs
    CHECK(!bind_virtual(f, kA, kPadMisc3, Displace::kMove));
    CHECK(!bind_virtual(f, kJump, kPadNone, Displace::kMove));
    // a face button moved by hand is no preset any more
    Mapping a = Mapping::defaults();
    a.face_auto = true;
    CHECK(bind_virtual(a, kJump, a.pad[kA], Displace::kMove) && !a.face_auto);
}

static void test_profiles() {
    Mapping m = Mapping::defaults();
    const std::string xg = "030000005e0400008e02000014010000", xn = "Xbox 360 Controller";
    const std::string pg = "050000004c050000e60c000000810000", pn = "DualSense Wireless Controller";
    // no profile: the shared bindings
    CHECK(profile_index(m, xg, xn) == -1 && &pads_for(m, xg, xn) == &m.pad);
    CHECK(setup_offer_due(m, xg, xn, false));
    CHECK(!setup_offer_due(m, xg, xn, true));  // the on-screen controls / a test pad: never offered
    CHECK(!setup_offer_due(m, "", "", false));
    // its own bindings: a copy of the shared ones, then by letters for the Xbox pad only
    const int xi = ensure_profile(m, xg, xn);
    CHECK(xi == 0 && ensure_profile(m, xg, xn) == 0 && m.controllers.size() == 1);
    CHECK(m.controllers[0].pad == m.pad && !setup_offer_due(m, xg, xn, false));
    Mapping view = view_for(m, xi);
    apply_face_layout(view, FaceLayout::kLabels);
    store_view(m, view, xi);
    CHECK(face_layout(m) == FaceLayout::kPosition);  // the shared ones are untouched
    float v[kPadCount] = {};
    v[kPadA] = 1;  // the bottom button
    CHECK(controller_state(m, v, pads_for(m, xg, xn)).buttons == input::kA);  // the Xbox pad: by letters
    CHECK(controller_state(m, v, pads_for(m, pg, pn)).buttons == input::kB);  // others: by position
    // the fallbacks: the same GUID with another name, then the same name (another bus, or AppKit)
    CHECK(profile_index(m, xg, "Xbox Wireless") == xi);
    CHECK(profile_index(m, "", xn) == xi);
    CHECK(profile_index(m, "0300000000000000000000000000ffff", xn) == xi);
    CHECK(profile_index(m, "0300000000000000000000000000ffff", "Other") == -1);
    // an exact match beats a GUID-only one
    const int pi = ensure_profile(m, pg, pn);
    const int pi2 = ensure_profile(m, pg, pn + " (USB)");
    CHECK(pi == 1 && pi2 == 2);
    m.controllers[pi2].pad[kZR] = kPadNone;
    CHECK(profile_index(m, pg, pn + " (USB)") == pi2 && profile_index(m, pg, pn) == pi);
    // a shared-view edit keeps the profiles; the profile view keeps the keyboard edits shared
    Mapping sv = view_for(m, -1);
    sv.keys[kA] = {kVK_ANSI_Z, kNoKey};
    sv.pad[kZL] = kPadNone;
    store_view(m, sv, -1);
    CHECK(m.controllers.size() == 3 && m.pad[kZL] == kPadNone && m.controllers[xi].pad[kZL] == kPadLT);
    Mapping pv = view_for(m, pi);
    pv.keys[kB] = {kVK_ANSI_O, kNoKey};
    pv.pad[kR] = kPadNone;
    store_view(m, pv, pi);
    CHECK(m.keys[kB][0] == kVK_ANSI_O && m.pad[kR] == kPadRB && m.controllers[pi].pad[kR] == kPadNone);
    // "Don't ask again", and a profile clears it
    m.setup_declined.push_back(controller_key("aa", "Pad"));
    CHECK(!setup_offer_due(m, "aa", "Pad", false) && setup_offer_due(m, "bb", "Pad", false));
    // JSON round trip (format 2), names with quotes, backslashes and control characters
    m.controllers[pi].name = "Odd \"pad\" \\ \x01";
    m.l_turns_page = true;
    std::string err;
    const std::string j = to_json(m);
    CHECK(j.find("\"version\": 2") != std::string::npos && json_version(j) == 2);
    CHECK(j.find("\"l_turns_gamepad_page\": true") != std::string::npos);
    Mapping r;
    CHECK(from_json(j, r, &err) && err.empty());
    if (!err.empty()) fprintf(stderr, "%s\n%s\n", err.c_str(), j.c_str());
    CHECK(r == m);
    remove_profile(m, xi);
    CHECK(m.controllers.size() == 2 && profile_index(m, xg, xn) == -1);
    // the option is written only when on
    CHECK(to_json(Mapping::defaults()).find("l_turns_gamepad_page") == std::string::npos);
    // a profile missing inputs takes the shared ones; bad entries are skipped with a warning
    CHECK(from_json(R"({"version":2,"controller":{"ZR":null},"controllers":[{"guid":"g","name":"n","controller":{"A":"Y","Bogus":"A"}},{"name":"no guid"}]})", r, &err));
    CHECK(r.controllers.size() == 1 && r.controllers[0].pad[kA] == kPadY && r.controllers[0].pad[kZR] == kPadNone);
    CHECK(r.controllers[0].pad[kB] == r.pad[kB] && !err.empty());
}

static void test_v1_migration() {
    // a format 1 file: no profiles; Jump and the moon jump on whatever pressed the Wii U's L
    const char* v1 = R"({"version":1,"keyboard":{"L":["U"]},"controller":{"L":"LeftTrigger","ZL":"LeftShoulder"},"options":{"format":2}})";
    Mapping r;
    std::string err;
    CHECK(json_version(v1) == 1 && json_version("{}") == 1 && json_version("nonsense") == 1);
    CHECK(from_json(v1, r, &err) && err.empty());
    CHECK(r.controllers.empty() && r.setup_declined.empty() && !r.l_turns_page);
    CHECK(r.pad[kJump] == kPadLT && r.pad[kMoonJump] == kPadLT);
    CHECK(r.keys[kJump][0] == kVK_ANSI_U && r.keys[kMoonJump][0] == kVK_ANSI_U);
    // the default format 1 file reads as today's defaults
    std::string old = to_json(Mapping::defaults());
    const size_t at = old.find("\"version\": 2");
    old.replace(at, 12, "\"version\": 1");
    CHECK(from_json(old, r, &err) && r == Mapping::defaults());
    // a format 2 file keeps its own Jump bindings, also where L moved
    Mapping m = Mapping::defaults();
    m.pad[kL] = kPadMisc2;
    CHECK(from_json(to_json(m), r) && r.pad[kJump] == kPadLB);
    // the old Jump anywhere button choice: Jump on that button's inputs, which keep their own use
    Mapping j = Mapping::defaults();
    jump_from_legacy(j, input::kStickR);
    CHECK(j.pad[kJump] == kPadR3 && j.keys[kJump][0] == kVK_ANSI_V && j.pad[kStickRClick] == kPadR3);
    CHECK(j.pad[kMoonJump] == kPadLB);
    jump_from_legacy(j, 0x12345);  // not a button: nothing
    CHECK(j.pad[kJump] == kPadR3);
}

int main() {
    // Old controls files default to showing Quit; an explicit choice round-trips.
    {
        auto mapping = input_map::Mapping::defaults();
        CHECK(mapping.file_select_quit);
        mapping.file_select_quit = false;
        input_map::Mapping loaded;
        CHECK(input_map::from_json(input_map::to_json(mapping), loaded));
        CHECK(!loaded.file_select_quit);
    }
    // Quit consumes the mapped Wii U X action. Its displayed host identity is
    // resolved from the same face preset as the game's existing prompt glyphs.
    for (auto layout : {FaceLayout::kPosition, FaceLayout::kLabels}) {
        auto mapping = Mapping::defaults();
        apply_face_layout(mapping, layout);
        float physical[kPadCount]{};
        const int button = mapping.pad[kX];
        physical[button] = 1;
        const auto delivered = controller_state(mapping, physical);
        CHECK(delivered.buttons == input::kX);
        for (bool shown : {false, true}) {
            file_select_quit::Routing routing;
            auto filtered = delivered;
            CHECK(routing.filter(filtered, shown) == shown);
            CHECK(filtered.buttons == (shown ? 0u : input::kX));
        }
        for (auto prompts : {Mapping::Prompts::kXbox, Mapping::Prompts::kPlayStation}) {
            mapping.prompts = prompts;
            const auto look = prompt_style(mapping, PadFamily::kUnknown);
            CHECK(look.buttons[2] == button - kPadA);
        }
    }
    test_shortcuts();
    for (int mode : {touch_buttons::Automatic, touch_buttons::AlwaysShow, touch_buttons::Hide})
        for (bool controller : {false, true}) {
            CHECK(touch_buttons::visible(mode, controller, false) ==
                  (mode == touch_buttons::AlwaysShow || (mode == touch_buttons::Automatic && !controller)));
            CHECK(touch_buttons::visible(mode, controller, true));
        }
    for (float dpi : {160.f, 320.f, 367.f, 420.f, 640.f}) {
        const float automatic = mobile_ui::scale(dpi, 1, 0);
        CHECK(std::fabs(49 * automatic * 25.4f / dpi - 8.0f) < 0.001f);
        CHECK(mobile_ui::scale(dpi, 1, 1) < automatic);
        CHECK(mobile_ui::scale(dpi, 1, 2) == automatic);
        CHECK(mobile_ui::scale(dpi, 1, 3) > automatic);
    }
    CHECK(std::isfinite(mobile_ui::scale(NAN, NAN, 0)));
    test_screen_layouts();
    test_settings_shortcut();
    test_android_odin_mapping();
    test_binding_capture_faces();
    test_conflict_helpers();
    test_mouse();
    test_defaults();
    test_apple_face_normalization();
    test_face_layout();
    test_face_auto();
    test_prompts();
    test_touch_face();
    test_face_label_text();
    test_names();
    test_reserved();
    test_conflicts();
    test_json_roundtrip();
    test_json_partial_and_bad();
    test_deadzone();
    test_stick_range();
    test_camera_invert();
    test_live_camera_invert_publication();
    using controller_selection::decision;
    CHECK(decision(true, true, 0, true, true) == 0);
    CHECK(decision(true, true, 1, true, true) == 1);
    CHECK(decision(false, true, 0, true, true) == -1);
    CHECK(decision(true, false, 1, true, true) == -1);
    CHECK(decision(true, true, -1, true, true) == -1);
    CHECK(decision(true, true, 2, true, true) == -1);
    CHECK(decision(true, true, 0, false, true) == -1);
    CHECK(decision(true, true, 1, true, false) == -1);
    Mapping skip = Mapping::defaults(), parsed;
    CHECK(skip.skip_controller_selection);
    CHECK(from_json("{\"options\":{}}", parsed));
    CHECK(parsed.skip_controller_selection);
    skip.skip_controller_selection = false;
    CHECK(from_json(to_json(skip), parsed));
    CHECK(parsed == skip);
    set_current(skip, false);
    CHECK(!skip_controller_selection());
    set_current(Mapping::defaults(), false);
    CHECK(skip_controller_selection());
    // issue #267: controller input in the background, off by default, saved only when on
    Mapping background = Mapping::defaults();
    CHECK(!background.background_controllers && !background_controllers());
    CHECK(to_json(background).find("background_controllers") == std::string::npos);
    CHECK(from_json("{\"options\":{}}", parsed) && !parsed.background_controllers);
    background.background_controllers = true;
    CHECK(to_json(background).find("\"background_controllers\": true") != std::string::npos);
    CHECK(from_json(to_json(background), parsed));
    CHECK(parsed == background);
    set_current(background, false);
    CHECK(background_controllers());
    set_current(Mapping::defaults(), false);
    CHECK(!background_controllers());
    test_files();
    test_virtual_actions();
    test_play_buttons();
    test_displace_rules();
    test_profiles();
    test_v1_migration();
    printf("input_map_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
