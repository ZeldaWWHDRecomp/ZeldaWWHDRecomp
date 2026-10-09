// Unit tests for the controls mapping (runtime/src/input_map.cpp).
//   make -C build/cmake input_map_test && ./build/cmake/input_map_test
#include "platform/keycodes.h"
#include <filesystem>
#include <chrono>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "input_map.h"

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
    // no default key is reserved; no key is used twice
    for (int a = 0; a < kActionCount; a++)
        for (int k : m.keys[a]) {
            CHECK(k == kNoKey || !reserved_key(k));
            CHECK(k == kNoKey || key_users(m, k, a).empty());
        }

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
    s = controller_state(m, v);
    CHECK(std::fabs(s.lx + 0.6f) < 1e-6 && std::fabs(s.ry - 0.3f) < 1e-6);
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

static void test_names() {
    for (int a = 0; a < kActionCount; a++) CHECK(action_from_id(action_id(a)) == a);
    for (int p = 1; p < kPadCount; p++) CHECK(pad_from_id(pad_id(p)) == p);
    for (int k = 0; k < 256; k++) CHECK(key_from_id(key_id(k)) == k);
    CHECK(key_from_id("LeftShift") == kVK_Shift);
    CHECK(key_from_id("Nonsense") == kNoKey);
    CHECK(action_from_id("Nonsense") == -1);
}

static void test_reserved() {
    for (int k : std::initializer_list<int>{kVK_ANSI_O, kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9, kVK_ANSI_P,
                  kVK_ANSI_R, kVK_F12, kVK_Escape})
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
    CHECK(press(r, {kVK_UpArrow}).ry == -1);  // inverted camera
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
    CHECK(r.deadzone == 0 && !r.invert_camera_y);
    // unknown names and app shortcuts are dropped with a warning, the rest still loads
    CHECK(from_json(R"({"keyboard": {"A": ["R", "K"], "Jump": ["J"], "B": ["Bogus"]}, "options": {"stick_deadzone": 5}})", r, &err));
    CHECK(!err.empty());
    CHECK(r.keys[kA][0] == kVK_ANSI_K && r.keys[kA][1] == kNoKey);
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
    CHECK(std::fabs(controller_state(m, v).ry - 0.5f) < 1e-6);
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

int main() {
    test_conflict_helpers();
    test_defaults();
    test_face_layout();
    test_face_auto();
    test_names();
    test_reserved();
    test_conflicts();
    test_json_roundtrip();
    test_json_partial_and_bad();
    test_deadzone();
    test_files();
    printf("input_map_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
