#include "render_objects.h"
#include "tooltip.h"
#include "controls_options.h"
#include "gfx/display_modes.h"
#include <mutex>
// In-game settings overlay: the Dear ImGui user interface (see overlay.h). The renderers draw the
// resulting ImDrawData (gfx/overlay_metal.mm, gfx/vulkan/overlay.cpp); the hosts feed input and apply
// changes on their main thread (hostui.h).
#include "overlay.h"
#include "imgui_internal.h"
#include "pointer.h"
#ifdef WWHD_SDL_HOST
#include "window_target.h"
#endif
#include "gfx/shader_prebuild.h"
#include "../audio_out.h"
#include "guest_hud.h"
#include "setup_test.h"
#include "setup_flow.h"
#include "mods_tab.h"
#include "editor_panel.h"
#include "graphics_switch.h"
#include "restart_dialog.h"
#include "quit_dialog.h"
#include "../file_select_quit.h"
#include "gyro_popup.h"
#include "popup.h"
#include "widgets.h"
#include "gamepad_nav.h"
#include "perf_average.h"
#ifdef __ANDROID__
#include "android_telemetry.h"
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#include <algorithm>
#include <cstdarg>
#include <functional>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <set>
#include <thread>
#include <vector>

#include "imgui.h"
#include "trace.h"
#include "hostui.h"
#include "../mods/code_mods.h"
#include "controls_view.h"
#include "binding_widget.h"
#include "extensions.h"
#include "release_gate.h"
#include "text_entry.h"
#include "../aspect.h"
#include "gfx/stereo.h"
#include "gfx/hdr.h"
#include "../crashrec.h"
#include "../game_languages.h"
#include "../gfx/renderer.h"
#ifdef __ANDROID__
#include "../gfx/vulkan/android_driver.h"
#endif
#ifdef WWHD_HAS_VULKAN
#include "../gfx/vulkan/settings.h"
namespace gfxvk { bool buffer_cache_enabled(); unsigned submission_pool_slots(); }  // buffer_cache.h, submission_pool.h
#endif
#include "../input.h"
#include "../input_map.h"
#include "../mods/climb.h"
#include "../mods/mods.h"
#include "../mods/manager.h"
#include "../mods/packages.h"
#include "../mods/catalogue_client.h"
#include "../platform/https.h"
#include "guest_addr.h"
#include "../motion/motion.h"
#include "../platform/keycodes.h"
#include "../rumble.h"
#ifdef __ANDROID__
#include "../platform/phone_rumble.h"
#include "../platform/heat_guard.h"
#endif
#include "../interp.h"
#include "../interp_pacing.h"
#include "../runtime.h"
#include "../savestate.h"
#include "../save_convenience.h"
#include "../screenshot.h"
#ifndef __ANDROID__
#include "template_export.h"
#endif
#include "../render_prof.h"
#include "../background_shaders.h"
#include <cctype>
#include "../perf_metrics.h"
#include "../build_info.h"
#include "settings_shortcut.h"
#include "layer_escape.h"
#include "toggle_guard.h"
#include "open_reason.h"
#include "host_focus.h"
#include "binding_capture.h"
#include "mobile_ui.h"
#include "../report_header.h"
#include "../dynres.h"

namespace gx2 { uint64_t flips_presented(); bool uncapped(); void set_uncapped(bool on); }

namespace overlay {
namespace {

using clock = std::chrono::steady_clock;
double now_s() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); }

std::atomic<bool> g_open{false};
std::atomic<bool> g_perf{false};
std::atomic<bool> g_stereo_pointer_visible{false};
PerfAverage g_average;
std::atomic<float> g_density{1.0f};
std::atomic<float> g_android_dpi{160.0f};
std::atomic<int> g_ui_size{0};
std::atomic<double> g_last_frame{0};      // frame() ran (alive(): the game's text prompt can show)
const bool g_no_host = getenv("WWHD_NO_HOST_INPUT") != nullptr;  // test runs ignore the user's input
// ... except keys a test posts itself (WWHD_TEST_POST_KEYS, gfx/input.mm; the hidden test window never
// has the user's keyboard)
const bool g_no_host_keys = g_no_host && !getenv("WWHD_TEST_POST_KEYS");
// ... and the mouse events a test posts (WWHD_TEST_POST_MOUSE, gfx/overlay_appkit.mm: only those reach here then)
const bool g_no_host_mouse = g_no_host && !getenv("WWHD_TEST_POST_MOUSE");
bool g_pad_b_used = false;  // B answered a dialog this frame: it does not also close the menu
// a dialog (or a list) is open in Settings: Esc and Android's Back close it, not the menu (popup.h)
std::atomic<bool> g_dialog_open{false};

// input events from the host's main thread, replayed into ImGui on the render thread
struct Event {
    enum Kind { Key, MousePos, MouseButton, Wheel, Focus, Text } kind;
    int code = 0;
    bool down = false;
    float x = 0, y = 0;
    int mods = 0;
    bool touch = false;
    std::string text = {};  // Text: typed characters (UTF-8)
};
std::mutex g_mu;
std::vector<Event> g_events;
// the system clipboard as the host last read it (main thread, before it passes the paste shortcut on);
// Dear ImGui reads it on the render thread when Ctrl/Cmd+V reaches a text field
std::string g_clipboard;
std::atomic<bool> g_wants_text{false};  // a text field of the overlay has the keyboard
std::atomic<bool> g_shortcut_capture{false};
std::atomic<int> g_capture_mods{0};
std::atomic<int> g_capture_key{-2};  // remap: key pressed while capturing (-2 means none)
std::atomic<bool> g_capturing_keys{false};
std::atomic<bool> g_external_capture{false}, g_external_capture_finished{false};
// keys held while the overlay is open (the Controls tab lights them; the game never sees them)
std::atomic<bool> g_held_keys[256];

void push(const Event& e) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_events.size() < 4096) g_events.push_back(e);
}

ImGuiKey imgui_key(int code) {
    switch (code) {
    case kVK_ANSI_A: return ImGuiKey_A; case kVK_ANSI_B: return ImGuiKey_B; case kVK_ANSI_C: return ImGuiKey_C;
    case kVK_ANSI_D: return ImGuiKey_D; case kVK_ANSI_E: return ImGuiKey_E; case kVK_ANSI_F: return ImGuiKey_F;
    case kVK_ANSI_G: return ImGuiKey_G; case kVK_ANSI_H: return ImGuiKey_H; case kVK_ANSI_I: return ImGuiKey_I;
    case kVK_ANSI_J: return ImGuiKey_J; case kVK_ANSI_K: return ImGuiKey_K; case kVK_ANSI_L: return ImGuiKey_L;
    case kVK_ANSI_M: return ImGuiKey_M; case kVK_ANSI_N: return ImGuiKey_N; case kVK_ANSI_O: return ImGuiKey_O;
    case kVK_ANSI_P: return ImGuiKey_P; case kVK_ANSI_Q: return ImGuiKey_Q; case kVK_ANSI_R: return ImGuiKey_R;
    case kVK_ANSI_S: return ImGuiKey_S; case kVK_ANSI_T: return ImGuiKey_T; case kVK_ANSI_U: return ImGuiKey_U;
    case kVK_ANSI_V: return ImGuiKey_V; case kVK_ANSI_W: return ImGuiKey_W; case kVK_ANSI_X: return ImGuiKey_X;
    case kVK_ANSI_Y: return ImGuiKey_Y; case kVK_ANSI_Z: return ImGuiKey_Z;
    case kVK_ANSI_0: return ImGuiKey_0; case kVK_ANSI_1: return ImGuiKey_1; case kVK_ANSI_2: return ImGuiKey_2;
    case kVK_ANSI_3: return ImGuiKey_3; case kVK_ANSI_4: return ImGuiKey_4; case kVK_ANSI_5: return ImGuiKey_5;
    case kVK_ANSI_6: return ImGuiKey_6; case kVK_ANSI_7: return ImGuiKey_7; case kVK_ANSI_8: return ImGuiKey_8;
    case kVK_ANSI_9: return ImGuiKey_9;
    case kVK_Return: case kVK_ANSI_KeypadEnter: return ImGuiKey_Enter;
    case kVK_Tab: return ImGuiKey_Tab;
    case kVK_Space: return ImGuiKey_Space;
    case kVK_Delete: return ImGuiKey_Backspace;
    case kVK_ForwardDelete: return ImGuiKey_Delete;
    case kVK_Escape: return ImGuiKey_Escape;
    case kVK_LeftArrow: return ImGuiKey_LeftArrow; case kVK_RightArrow: return ImGuiKey_RightArrow;
    case kVK_UpArrow: return ImGuiKey_UpArrow; case kVK_DownArrow: return ImGuiKey_DownArrow;
    case kVK_Home: return ImGuiKey_Home; case kVK_End: return ImGuiKey_End;
    case kVK_PageUp: return ImGuiKey_PageUp; case kVK_PageDown: return ImGuiKey_PageDown;
    case kVK_Shift: return ImGuiKey_LeftShift; case kVK_RightShift: return ImGuiKey_RightShift;
    case kVK_Control: return ImGuiKey_LeftCtrl; case kVK_RightControl: return ImGuiKey_RightCtrl;
    case kVK_Option: return ImGuiKey_LeftAlt; case kVK_RightOption: return ImGuiKey_RightAlt;
    case kVK_Command: return ImGuiKey_LeftSuper; case kVK_RightCommand: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

// ---------------------------------------------------------------- UI state (render thread)
// the built-in tabs (extensions.h kBuiltinTabs: ids and names); the tab bar is ext::tabs(), which a
// platform may extend (added tabs, hidden ones)
enum Tab { kSaves, kGraphics, kDisplay, kAudio, kMods, kControls, kShortcuts, kAbout, kTabs };
static_assert(kTabs == ext::kBuiltinCount, "a built-in tab per kBuiltinTabs entry");

struct Ui {
    bool init = false;
    // tab: the open tab, an index in ext::tabs() (remembered while the game runs); select_tab: switch to
    // this one next frame (shoulder buttons, tests)
    int tab = 0, select_tab = -1;
    double last_time = 0;
    // controller
    float values[input_map::kPadCount] = {};
    float prev[input_map::kPadCount] = {};
    PadValues nav_values{}, nav_prev{};
    ReleaseGate capture_navigation_gate;
    bool capture_completed = false;
    GamepadNavigation gamepad_navigation;
    ImVec2 mouse_position = {-FLT_MAX, -FLT_MAX};
    // remap capture: action, column (0, 1 keys; 2 controller)
    int cap_action = -1, cap_col = 0;
    BindingCapture binding_capture;
    double cap_started = 0;
    std::string cap_note;
    // save state slots, refreshed twice a second while the Saves tab is shown
    ss::SlotInfo slots[ss::kSlots + 1];
    crashrec::AutoInfo autos[crashrec::kAutoSlots + 1];
    ss::SlotInfo last_session, auto_saves[ss::convenience::kAutoSaves + 1];
    double slots_time = -1;
    // performance
    double fps_t0 = 0, fps = 0;
    uint64_t fps_n0 = 0;
    float frame_ms[120] = {};
    int frame_i = 0;
    double last_present = 0;
    // language (applies on the next start)
    int language = -1;
    int language_at_start = -1;  // the language this start runs with (game_lang: the one on the disc)
    int language_region = 0;     // game_lang region of a language source (0: the installed game)
    int language_region_at_start = 0;
    bool linearized = false;
    bool just_opened = false;
#ifdef __ANDROID__
    bool list_view = true;
#else
    bool list_view = false;
#endif  // Controls: the table instead of the drawing
    int hover_action = -1;   // Controls: input under the cursor (status line)
    float options_height = 0;  // Controls: the options under the drawing, last frame (they wrap when narrow)
};
Ui U;

bool controller_down(int p) { return U.values[p] > 0.5f; }
bool controller_pressed(int p) { return U.values[p] > 0.5f && U.prev[p] <= 0.5f; }
bool navigation_pressed(int p) { return U.cap_action < 0 && !U.capture_completed && U.gamepad_navigation.pressed(U.nav_values.data(), U.nav_prev.data(), p); }
int shown_builtin() { return ext::builtin_tab(U.tab); }  // the open tab's Tab, -1 for an added one

void post_changed(std::function<void()> fn) {
    hostui::post([fn] {
        fn();
        hostui::graphics_changed();
    });
}

// ---------------------------------------------------------------- style
void setup_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 10;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.GrabRounding = 5;
    s.TabRounding = 6;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(9, 5);
#ifdef __ANDROID__
    mobile_ui::apply_touch_style(s, mobile_ui::Layout{});  // touch-sized controls; each frame sizes them for the screen
#endif
    s.ItemSpacing = ImVec2(10, 7);
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;  // unchecked boxes and radio buttons stay visible on the dark background
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    ImVec4* c = s.Colors;
    // deep sea blue with a teal accent
    c[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.07f, 0.12f, 0.975f);  // sRGB targets blend in linear light: keep it dense
    c[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.13f, 0.20f, 0.35f);
    c[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.11f, 0.18f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.62f, 0.70f, 0.45f);
    c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.20f, 0.30f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.27f, 0.38f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.22f, 0.32f, 0.85f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.34f, 0.46f, 0.90f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.42f, 0.54f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.12f, 0.36f, 0.48f, 0.90f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.50f, 0.62f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.60f, 0.70f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.12f, 0.36f, 0.48f, 0.70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.50f, 0.62f, 0.85f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.60f, 0.70f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.08f, 0.24f, 0.34f, 0.90f);
    c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.52f, 0.64f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.44f, 0.56f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.95f, 0.85f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.80f, 0.80f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.95f, 0.90f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.62f, 0.70f, 0.35f);
    c[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);  // controller cursor: gold, easy to follow
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.08f, 0.24f, 0.34f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
}

// a font with arrows and accents if the system has one; Dear ImGui's own scalable font otherwise
void setup_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    if (const ext::FontLoader& load = ext::font_loader(); load && load(io)) return;  // the platform's (extensions.h)
    static const char* const candidates[] = {
#if defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
#elif defined(_WIN32)
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
#endif
    };
    for (const char* path : candidates) {
        if (FILE* f = fopen(path, "rb")) {
            fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, 17.0f)) {
                LOG("[overlay] font %s", path);
                return;
            }
        }
    }
    ImFontConfig cfg;
    cfg.SizePixels = 17.0f;
    io.Fonts->AddFontDefaultVector(&cfg);
}

void init_context() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nothing written next to the game
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "wwhd";
    io.ConfigNavCaptureKeyboard = true;
#ifdef NDEBUG
    // Dear ImGui's red "conflicting ID" box is for developers: release builds never show it to players (debug
    // builds and the offscreen tests keep it, and mods_tab_ids checks the Mods tab for conflicts)
    io.ConfigDebugHighlightIdConflicts = false;
#endif
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
        static std::string copy;
        std::lock_guard<std::mutex> lk(g_mu);
        copy = g_clipboard;
        return copy.c_str();
    };
    pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        std::string copy = text ? text : "";
        { std::lock_guard<std::mutex> lk(g_mu); g_clipboard = copy; }
        hostui::post([copy] { hostui::set_clipboard(copy); });
    };
    setup_style();
    setup_fonts();
    ImGui::GetStyle().FontSizeBase = 17.0f;
    U.init = true;
}

// ---------------------------------------------------------------- test switch
struct TestSwitch {
    bool open = false, perf = false, quit = false;
    int tab = -1;
    uint64_t at = 1;
    bool done = false;
};
TestSwitch parse_test() {
    TestSwitch t;
    const char* e = getenv("WWHD_TEST_OVERLAY");
    if (!e || !*e) { t.done = true; return t; }
    std::string s = e;
    if (size_t at = s.find('@'); at != std::string::npos) {
        t.at = strtoull(s.c_str() + at + 1, nullptr, 10);
        s.resize(at);
    }
    std::string what = s, tab;
    if (size_t c = s.find(':'); c != std::string::npos) { what = s.substr(0, c); tab = s.substr(c + 1); }
    // quit: open the menu and choose Quit Game (its confirmation stays up unless WWHD_TEST_QUIT_ANSWER answers)
    t.quit = what == "quit";
    t.open = what == "open" || t.quit;
    t.perf = what == "perf";
    if (!tab.empty()) t.tab = ext::tab_index(tab);  // built-in or added (extensions.h)
    return t;
}

// ---------------------------------------------------------------- controller
// debug: WWHD_TEST_PAD=320-400:B+LeftStickRight:0.8+LeftStickUp:0.4 holds host controller inputs
// (controls.json names, value 1 unless given) during TV frames 320..400. They reach only the
// Controls tab's live display and the game's text prompt (not the menu navigation, capture or the game).
void test_pad(float* v) {
    struct Hold { uint64_t from, to; int pad; float value; };
    static const std::vector<Hold> holds = [] {
        std::vector<Hold> out;
        const char* e = getenv("WWHD_TEST_PAD");
        while (e && *e) {
            unsigned long long a, b;
            int n;
            if (sscanf(e, "%llu-%llu:%n", &a, &b, &n) != 2) break;
            e += n;
            for (;;) {
                size_t len = strcspn(e, "+,");
                std::string item(e, len), id = item;
                float value = 1;
                if (size_t c = item.find(':'); c != std::string::npos) { id = item.substr(0, c); value = (float)atof(item.c_str() + c + 1); }
                int p = input_map::pad_from_id(id);
                if (p > 0) out.push_back({a, b, p, value});
                else LOG("[overlay] WWHD_TEST_PAD: unknown controller input %s", id.c_str());
                e += len;
                if (*e != '+') break;
                e++;
            }
            if (*e != ',') break;
            e++;
        }
        return out;
    }();
    const uint64_t f = render::frame_count();
    for (const Hold& h : holds)
        if (f >= h.from && f <= h.to) v[h.pad] = std::max(v[h.pad], h.value);
}

void read_controller() {
    std::copy(std::begin(U.values), std::end(U.values), std::begin(U.prev));
    if (g_no_host) {
        std::fill(std::begin(U.values), std::end(U.values), 0.0f);
#ifndef WWHD_SDL_HOST
        if (getenv("WWHD_TEST_PAD")) { test_pad(U.values); poll_controller_shortcuts(U.values); }
#endif
        return;
    }
    input::host_controller_values(U.values);
    if (ext::text_input()) {
        // a platform keyboard of a text field (text_entry::field) has the controller: the inputs held
        // there stay from the menu until each one is released, as from the game (release_gate.h)
        static ReleaseGate keyboard_gate;
        keyboard_gate.filter(U.values, text_entry::field_active());
    }
    using namespace input_map;
#ifndef WWHD_SDL_HOST
    // The AppKit host still supplies controller state on its render path.
    poll_controller_shortcuts(U.values);
#endif
}

// ---------------------------------------------------------------- widgets
using widgets::help;
using widgets::note;
using widgets::radio;
using widgets::check;
using widgets::selectable;
void heading(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.95f, 0.85f, 1.0f));
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
}
void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.35f, 1.0f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

bool combo(const char* label, int* current, const char* items) {
#ifndef __ANDROID__
    return ImGui::Combo(label, current, items);
#else
    const char* preview = items;
    for (int i = 0; i < *current && *preview; ++i) preview += strlen(preview) + 1;
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        int index = 0;
        for (const char* item = items; *item; item += strlen(item) + 1, ++index) {
            const bool selected = index == *current;
            if (selectable(item, selected)) { *current = index; changed = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
#endif
}

// ---------------------------------------------------------------- tabs
void refresh_slots(bool force) {
    double t = now_s();
    if (!force && U.slots_time >= 0 && t - U.slots_time < 0.5) return;
    U.slots_time = t;
    for (int i = 1; i <= ss::kSlots; i++) U.slots[i] = ss::slot_info(i);
    for (int i = 1; i <= crashrec::kAutoSlots; i++) U.autos[i] = crashrec::auto_info(i);
    U.last_session = ss::slot_info(ss::convenience::kLastSessionSlot);
    for (int i = 1; i <= ss::convenience::kAutoSaves; i++) U.auto_saves[i] = ss::slot_info(ss::convenience::kAutoSaveBase + i);
}

void tab_saves_screenshots();
void tab_saves_crash_recovery();
void tab_saves_convenience();
void tab_saves() {
    refresh_slots(false);
    std::string msg = ss::last_message();
    if (!msg.empty()) note("%s", msg.c_str());
    heading("Save states");
    const bool full = ss::full_states();
    const char* keys = ext::features().keyboard ? " Shift+F1..F5 save in game, F6 loads slot 1, F2..F5 load slots 2..5 (Esc or F1 opens this menu)." : "";
    if (full)
        note("Full save states: the whole running game (large, contain game data: never share them).%s", keys);
    else
        note("A save state keeps your progress and where Link stands (a few KB, no game data). Loading enters that place "
             "with that progress; enemies and cutscenes start fresh.%s", keys);
    if (ImGui::BeginTable("slots", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Saved");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 1; i <= ss::kSlots; i++) {
            const ss::SlotInfo& s = U.slots[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Slot %d", i);
            ImGui::TableNextColumn();
            if (!s.used) ImGui::TextDisabled("empty");
            else {
                std::string d = s.when + (s.area.empty() ? "" : "  -  " + s.area);
                if (!s.controller.empty()) d += "  -  " + s.controller;
                if (!s.portable) d += "  (full)";
                if (!s.compatible) d += "  (incompatible)";
                ImGui::TextUnformatted(d.c_str());
                if (s.older_other && s.portable) {
                    // decision: an older full state stays on disk; say so (it is large and must not be shared)
                    ImGui::TextDisabled("also holds an older full state (slot%d.bin, %.0f MB)", i, s.older_bytes / 1048576.0);
                    if (overlay::tooltip_hovered())
                        ImGui::SetTooltip("Kept on disk, not loaded: the newer portable state is. It contains game data: "
                                          "don't share it. Delete it in the states folder if you no longer need it.");
                } else if (s.older_other) {
                    ImGui::TextDisabled("also holds an older portable state (slot%d.wwstate)", i);
                }
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Save")) {
                ss::request_save(i);
                U.slots_time = -1;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!s.used || !s.compatible);
            if (ImGui::Button("Load")) {
                ss::request_load(i);
                set_open(false, OpenReason::LoadSlot);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    tab_saves_convenience();
    // bug reports: the portable state and the save file, never a full state
    const ext::Features& features = ext::features();
    if (features.clipboard) {
        static double copiedAt = -10;
        if (ImGui::Button("Copy save for bug report")) {
            std::string text = ss::bug_report_text();
            hostui::post([text] { hostui::set_clipboard(text); });
            copiedAt = ImGui::GetTime();
        }
        if (ImGui::GetTime() - copiedAt < 2.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("Copied");
        }
        help("Copies the paths of your newest save state and of your save file (cking.sav): attach both files to the bug "
             "report. Save a state at the place of the problem first.");
    } else {
        // no clipboard on this host: the paths are shown (looked up at most once a second)
        static std::string paths;
        static double pathsAt = -10;
        if (ImGui::GetTime() - pathsAt > 1.0) {
            paths = ss::bug_report_text();
            pathsAt = ImGui::GetTime();
        }
        note("For a bug report, attach your newest save state and your save file (cking.sav). Save a state at the "
             "place of the problem first.");
        note("%s", paths.c_str());
    }
    note("States folder: %s", ss::states_dir().c_str());
    if (features.full_states) {
        bool fs;
        if (check("Full save states (large, contain game data, don't share) - for debugging", full, &fs, !ss::full_states_forced()))
            ss::set_full_states(fs);
        help(ss::full_states_forced() ? "Set by WWHD_FULL_SAVE_STATES or a test variable for this start."
                                      : "Saves the whole running game instead (about 300 MB per slot), exactly as it is. "
                                        "These files contain game code and data: never attach them to a bug report.");
        int savePath=int(ss::save_path());
        if (ImGui::Combo("Full-state save path", &savePath, "Auto\0Streaming (less memory)\0Fast (background compression)\0"))
            ss::set_save_path(ss::SavePath(savePath));
        help("Auto uses less-memory streaming on Android and fast background compression on desktop. "
             "Applies to manual full states and Crash Recovery; loading works with either path.");

    }
    if (features.screenshots) tab_saves_screenshots();
    if (features.full_states) tab_saves_crash_recovery();  // its automatic states are full states
}

void tab_saves_screenshots() {
    heading("Screenshots");
    {
        // The Screenshot shortcut and any migrated single-button controller binding.
        const input_map::Mapping m = input_map::current();
        std::string keys;
        for (auto k : m.shortcuts[input_map::kPhoto].keys)
            if (k.code != input_map::kNoKey) keys += (keys.empty() ? "" : " or ") + input_map::shortcut_key_label(k);
        if (m.pad[input_map::kScreenshot] != input_map::kPadNone)
            keys += (keys.empty() ? "controller " : " or controller ") + std::string(input_map::pad_short_label(m.pad[input_map::kScreenshot]));
        if (keys.empty()) note("Screenshot is not bound: set a key in the Shortcuts tab.");
        else note("%s in game saves the TV picture as a PNG at the internal resolution, without this menu (change the key in Controls).", keys.c_str());
    }
    bool gp;
    if (check("Also save the GamePad screen (while it is shown)", screenshot::gamepad_too(), &gp))
        hostui::post([gp] { screenshot::set_gamepad_too(gp); });
    help("A second file, ..._GamePad.png, while the GamePad picture is on screen (its window or the overlay in the TV picture).");
    if (hostui::can_open_folder()) {
        if (ImGui::Button("Open screenshots folder")) {
            std::string d = screenshot::dir();
            hostui::post([d] { hostui::open_folder(d); });
        }
        ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", screenshot::dir().c_str());
}

// Last session, Auto save and At start (save_convenience.h): portable states in their own slots
void tab_saves_convenience() {
    namespace conv = ss::convenience;
    heading("Last session and auto saves");
    auto row = [](int id, const char* name, const ss::SlotInfo& s) {
        ImGui::PushID(id);
        ImGui::BeginDisabled(!s.used || !s.compatible);
        if (ImGui::Button("Load")) {
            ss::request_load(id);
            set_open(false, OpenReason::LoadSlot);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (s.used && s.compatible) ImGui::Text("%s:  %s%s%s", name, s.when.c_str(), s.area.empty() ? "" : "  -  ", s.area.c_str());
        else ImGui::TextDisabled("%s:  %s", name, s.used ? "incompatible" : "empty");
        ImGui::PopID();
    };
    row(conv::kLastSessionSlot, "Last session", U.last_session);
    help("Taken when you quit the game (on Android also when the app goes to the background), unless a cutscene or "
         "dialogue is running then. A small portable state: your progress and where Link stands.");
    for (int i = 1; i <= conv::kAutoSaves; i++) {
        char name[32];
        snprintf(name, sizeof name, "Auto save %d", i);
        row(conv::kAutoSaveBase + i, name, U.auto_saves[i]);
    }
    const int minutes = ss::auto_save_minutes();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Auto save every");
    for (int m : conv::kAutoSaveMinutes) {
        ImGui::SameLine();
        char label[16];
        if (m) snprintf(label, sizeof label, "%d min", m);
        else snprintf(label, sizeof label, "Off");
        if (radio(label, minutes == m)) ss::set_auto_save_minutes(m);
    }
    help("Saves a small portable state in the background into Auto save 1 to 3, the oldest first. If the game is in a "
         "cutscene or dialogue, it tries again a little later. Separate from Crash Recovery.");
    const conv::StartChoice start = conv::parse_start(ss::at_start());
    int current = start.what == conv::Start::kLastSession ? 1 : start.what == conv::Start::kSlot ? 1 + start.slot : 0;
    if (combo("At start", &current, "Intro as usual\0Continue last session\0Slot 1\0Slot 2\0Slot 3\0Slot 4\0Slot 5\0\0"))
        ss::set_at_start(current == 0 ? "intro" : current == 1 ? "last" : "slot" + std::to_string(current - 1));
    help("Loads that state when the game starts, as soon as you continue a Quest Log on the title screen. An empty "
         "slot starts the game as usual.");
}

void tab_saves_crash_recovery() {
    heading("Crash Recovery");
    bool on;
    if (check("Crash Recovery (automatic state every few minutes)", crashrec::enabled(), &on)) crashrec::set_enabled(on);
    help("Saves the game into automatic states in the background and records the controller input since the latest "
         "one. After a crash, the crash log says how to load it. Saving freezes the game for a moment.");
    for (int i = 1; i <= crashrec::kAutoSlots; i++) {
        const crashrec::AutoInfo& a = U.autos[i];
        ImGui::PushID(100 + i);
        ImGui::BeginDisabled(!a.used);
        if (ImGui::Button("Load")) {
            crashrec::request_load(i);
            set_open(false, OpenReason::LoadSlot);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (a.used) ImGui::Text("Automatic state %d:  %s%s%s", i, a.when.c_str(), a.area.empty() ? "" : "  -  ", a.area.c_str());
        else ImGui::TextDisabled("Automatic state %d:  empty", i);
        ImGui::PopID();
    }
}

std::string performance_device() {
    std::string device = render::device();
#ifdef __APPLE__
    if (!render::vulkan()) device += ", driver: " + reporthdr::os_description();
#endif
#ifdef __ANDROID__
    const auto driver = gfxvk::drivers::active_name();
    if (!driver.empty()) device += " (" + driver + ")";
#endif
    return device.empty() ? "GPU/driver: n/a" : device;
}

std::string performance_report() {
    float sum = 0, worst = 0;
    for (float f : U.frame_ms) { sum += f; worst = std::max(worst, f); }
    char frame[180];
    snprintf(frame, sizeof frame, "Output FPS %.1f (average %.1f), render interval %.2f ms (worst %.2f), average logic steps/s %.1f\n",
             U.fps, g_average.fps, sum / 120.0f, worst, g_average.logic);
    std::string report = std::string(frame) + perf::summary();
    report += render_objects::line() + "\n";  // live shaders/pipelines, memory mappings (render_objects.h)
    report += dynres::status() + "\n";
#ifdef __ANDROID__
    report += android_telemetry() + "\n";
#endif
    report += "CPU time is thread running time per output frame. GPU submission intervals may include stalls; not GPU load.\n";
    report += bgshaders::report_line();  // Shader compile level: draws left out, waits
    report += rprof::report_section();  // the detailed sections, or a line on how to turn them on
    // which build, system, GPU and rendering-path switches (report_header.h)
    reporthdr::Info h;
    h.version = build::version();
    h.commit = build::commit();
    h.variant = build::variant();
    h.os = reporthdr::os_description();
    h.gpu = performance_device();
    h.renderer = render::api_name(render::active());
    h.host = hostui::name();
    h.fps = interp::mode() == 2 ? "true 60 fps" : interp::mode() == 1 ? std::to_string(interp::fps()) + " fps interpolation" : "30 fps";
    if (interp::mode() == 1) h.keepGameSpeed = interp::paced_interpolation();  // at that rate
    h.scale = hostui::res_scale();
#ifdef WWHD_HAS_VULKAN
    if (render::vulkan()) {
        h.bufferCache = gfxvk::buffer_cache_enabled();
        h.submissionPool = int(gfxvk::submission_pool_slots());
        h.overrides = reporthdr::vulkan_overrides([](const char* n) -> const char* { return getenv(n); });
    }
#endif
    if (const int g = motion::settings().source; g != motion::kOff) h.gyro = motion::source_id(g);
    reporthdr::Display d;
    d.hz = interp::display_hz();
    d.overridden = interp::display_hz_overridden();
    d.requestedHz = interp::requested_display_hz();
    interp::display_hz_seen(&d.seenLo, &d.seenHi);
    // the drawn rate: frame interpolation's (capped to the display), true 60's or the original 30
    d.fps = interp::mode() == 1 ? interp::output_fps() : interp::mode() == 2 ? 60 : 30;
    h.display = reporthdr::display_line(d);
    report = reporthdr::format(h) + report;
    return report;
}

// Graphics > Shader cache. Left out with a platform backend (gfx/renderer_ext.h), which keeps its own
// shader caches: the rebuild request and the template export work on Metal's and Vulkan's.
static void shader_cache_section() {
    heading("Shader cache");
    static bool rebuildFailed = false;
    if (ImGui::Button("Rebuild shader cache"))
        rebuildFailed = !shaderprebuild::request_rebuild();
    if (rebuildFailed) note("Could not save the request. Check that the settings folder is writable.");
    else if (shaderprebuild::requested()) note(shaderprebuild::setup_hint());
    help(shaderprebuild::setup_hint());
#ifndef __ANDROID__
    {
        const auto exported = template_export::status();
        ImGui::BeginDisabled(exported.running);
        if (ImGui::Button("Export shader template")) template_export::start();
        ImGui::EndDisabled();
        if (!exported.line.empty()) note("%s", exported.line.c_str());
        help("Saves my-template.bin in the screenshots folder: the shader states and pipelines this computer\n"
             "recorded, as hashes and settings only (no shader code, no game data). Attach it to issue #177\n"
             "so the next release prepares them for everyone.");
    }
#endif
}

void tab_graphics() {
    if (!render::platform()) shader_cache_section();
#ifdef __ANDROID__
    heading("GPU driver (Snapdragon / Adreno)");
    note("Active: %s", gfxvk::drivers::active_name().c_str());
    auto driver_action = [](auto fn) { hostui::post([fn] { try { fn(); } catch (const std::exception& e) { LOG("[vulkan driver] %s", e.what()); } }); };
    if (ImGui::Button("Install driver ZIP...")) driver_action([] { gfxvk::drivers::request_install(); });
    auto selected = gfxvk::drivers::selection();
    if (radio("System driver", selected.empty())) driver_action([] { gfxvk::drivers::select(""); });
    for (const auto& driver : gfxvk::drivers::installed()) {
        ImGui::PushID(driver.id.c_str());
        auto id = driver.id;
        if (radio((driver.name + " " + driver.version).c_str(), selected == id))
            driver_action([id] { gfxvk::drivers::select(id); });
        ImGui::SameLine();
        if (ImGui::Button("Remove")) driver_action([id] { gfxvk::drivers::remove(id); });
        ImGui::PopID();
    }
    note("%s", gfxvk::drivers::message().c_str());
    help("Driver changes take effect on restart. An unfinished first 120-frame probe selects the system driver on the next start.");
#endif
    if (render::can_choose()) {
        heading("Renderer (takes effect after a restart)");
        for (render::Api a : {render::Api::Metal, render::Api::Vulkan}) {
            std::string label = a == render::Api::Vulkan ? "Vulkan (MoltenVK)" : "Metal";
            if (a == render::active()) label += "  - in use";
            if (radio(label.c_str(), render::preferred() == a, render::compiled(a)))
                post_changed([a] { render::set_preferred(a); });
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (!render::fallback_reason().empty()) note("This start: %s", render::fallback_reason().c_str());
        if (render::restart_pending()) {
            note("The game uses %s after a restart. Progress since your last save is lost.", render::api_name(render::preferred()));
            if (ImGui::Button("Restart now")) hostui::post([] { render::restart(); });
        }
    }
    heading("Frame rate");
    int m = interp::mode(), f = interp::fps();
    if (radio("30 fps (original)", m == 0)) post_changed([] { interp::set_mode(0); });
    for (int r : {40, 60, 120, 240}) {
        ImGui::SameLine();
        char label[16];
        snprintf(label, sizeof label, "%d fps", r);
        if (radio(label, m == 1 && f == r)) post_changed([r] { interp::set_fps(r); interp::set_mode(1); });
    }
    help("40, 60, 120 and 240 fps: frame interpolation. The game logic keeps its 30 steps a second, and the\n"
         "frames in between are drawn blended (1 in-between frame per step at 60 fps, 3 at 120 fps,\n"
         "7 at 240 fps). The display shows at most its refresh rate: higher choices draw that many.\n"
         "40 fps draws 4 frames for every 3 steps (1 in-between frame per 3 steps), for 40, 80 and\n"
         "120 Hz screens (a Steam Deck set to 40 Hz or 80 Hz, phones with a 120 Hz panel).");
    if (radio("True 60 (experimental)", m == 2)) post_changed([] { interp::set_mode(2); });
    help("True 60 runs the game logic at 60 steps per second");
    // the display's refresh rate, and what the chosen rate draws on it (interp::output_fps)
    if (const int hz = interp::display_hz(); hz > 0) {
        const int out = interp::output_fps();
        if (m == 1 && f == 40 && !interp::pacing::even_on_display(40, hz))
            note("Your display: %d Hz. 40 fps needs a 40, 80 or 120 Hz screen: here each frame stays on\n"
                 "screen for 1 or 2 refreshes in turn (uneven motion). Choose 30 or 60 fps, or set the\n"
                 "screen to 40, 80 or 120 Hz.", hz);
        else if (m == 1 && out < f)
            note("Your display: %d Hz. %d fps needs a %d Hz display; %d fps are drawn (the display cannot show more).", hz, f, f, out);
        else if (m == 1 && f > hz + 2)
            note("Your display: %d Hz. Frames beyond %d a second are drawn but not shown (presentation without vsync).", hz, hz);
        else
            note("Your display: %d Hz", hz);
    } else if (m == 1 && f > 60) {
        note("Display refresh rate unknown: %d fps are drawn; frames beyond the display's rate are not shown.", f);
    }
    if (m == 1) {
        bool paced;
        if (check("Keep game speed (recommended)", interp::paced_interpolation(), &paced, !getenv("WWHD_INTERP_PACED")))
            post_changed([paced] { interp::set_paced_interpolation(paced); });
        help("When the computer cannot draw all frames, skip in-between frames instead of slowing the\n"
             "whole game down. The performance overlay shows how many are drawn.\n"
             "Off: every logic step waits for all of its frames, so if the frame target is not reached\n"
             "(e.g. 120/240 fps at a high internal resolution) the whole game runs in slow motion.\n"
             "Saved separately for 40/60 fps (off by default; on by default on Android and Windows on ARM)\n"
             "and 120/240 fps (on by default).");
        if (!interp::paced_interpolation())
            note("Off: if this computer cannot reach %d fps, the whole game slows down (the performance\n"
                 "overlay then shows fewer than 30 logic steps/s). Turn it on to keep the game's speed.", interp::fps());
    }
#ifdef __ANDROID__
    // the heat guard exists on Android only (platform/perf_hint.cpp); WWHD_HEAT_GUARD wins and is not saved
    {
        const int hg = heat_guard::choice.load();
        const bool editable = !heat_guard::env_override();
        ImGui::TextUnformatted("Heat guard");
        static const char* const hgNames[] = {"Normal", "Relaxed", "Off"};
        for (int i = heat_guard::kNormal; i <= heat_guard::kOff; i++) {
            ImGui::SameLine();
            char label[24];
            snprintf(label, sizeof label, "%s##heatguard", hgNames[i]);
            if (radio(label, hg == i, editable)) post_changed([i] { heat_guard::choice = i; });
        }
        help("When the device gets hot, 60 fps pauses until it cools down. Relaxed and Off keep 60 fps\n"
             "longer, but the device may slow down or get warm. Normal suits this device; Relaxed waits\n"
             "longer on any device; Off never pauses 60 fps for heat.");
    }
#endif
    // debug only, not saved (gx2::uncapped)
    bool unc;
    if (check("Uncapped (debug: the game runs too fast)", gx2::uncapped(), &unc)) hostui::post([unc] { gx2::set_uncapped(unc); });
    help("Debug only, to see how many frames a second this computer can draw: no frame limit and no\n"
         "vsync. The game counts frames, so it runs faster than normal. The frame rate is in the window\n"
         "title and the performance overlay. Not saved. (With frame interpolation and Keep game speed\n"
         "on, the game logic still keeps 30 steps a second.)");

    heading("Internal resolution");
    static const float scales[] = {1.0f, 1.5f, 2.0f, 3.0f};
    static const char* const names[] = {"1x  (1280x720)", "1.5x  (1920x1080)", "2x  (2560x1440)", "3x  (3840x2160)"};
    float cur = hostui::res_scale();
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        float s = scales[i];
        if (radio(names[i], std::fabs(cur - s) < 0.01f)) post_changed([s] { hostui::set_res_scale(s); });
    }

    heading("Dynamic resolution");
    {
        static const float mins[] = {0.0f, 0.75f, 0.5f};
        static const char* const dnames[] = {"Off##dynres", "Down to 75%##dynres", "Down to 50%##dynres"};
        const float dm = dynres::min_factor();
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine();
            const float m = mins[i];
            if (radio(dnames[i], std::fabs(dm - m) < 0.01f, !dynres::min_from_env()))
                post_changed([m] { dynres::set_min(m); hostui::set("dynResMin", m > 0 ? std::to_string(m) : "0"); });
        }
        help("Lowers the internal resolution while the graphics chip cannot keep up with the frame rate, down to\n"
             "the chosen share of it, and raises it again when there is room. The picture is softer while it is\n"
             "lower. Helps where the graphics chip is the limit (phones, older computers at a high resolution);\n"
             "when the processor is the limit, the resolution stays.");
        if (dynres::on()) note("%s", dynres::status().c_str());
    }

    float fovPercent=aspect::fov_scale()*100.0f;
    if(ImGui::SliderFloat("Gameplay FOV", &fovPercent,75.0f,250.0f,"%.0f%%",ImGuiSliderFlags_AlwaysClamp))
        post_changed([fovPercent] { aspect::set_fov_scale(fovPercent/100.0f); hostui::set("gameplayFovScale",std::to_string(fovPercent/100.0f)); });
    help("100% keeps the original field of view. Higher values show more of the world and make Link\n"
         "appear smaller. Applies to gameplay and combat in 2D and 3D. Cutscenes, dialogue,\n"
         "scene transitions and menus keep their authored FOV. First-person view, item aiming and\n"
         "the Telescope are widened by the same factor, so their zoom is weaker above 100%.\n"
         "Maximum vertical FOV: 110 degrees. Wider settings also widen visibility/culling checks.");

    heading("Aspect ratio");
    int am = aspect::mode();
    for (int i = aspect::kOriginal; i <= aspect::k32x9; i++) {
        if (i) flow::next(flow::check(aspect::mode_name(i)));
        if (radio(aspect::mode_name(i), am == i))
            post_changed([i] { aspect::set_mode(i); hostui::set("aspectMode", std::to_string(i)); });
    }

#ifdef WWHD_HAS_VULKAN
    if (render::g_backend->api == render::Api::Vulkan) {
        heading("Stereoscopic 3D (experimental)");
        const char* modes[] = {"Off", "Half SBS", "Full SBS", "Top and bottom (TAB)"};
        int mode = stereo::requestedMode.load();
        if (ImGui::Combo("3D output", &mode, modes, stereo::Count))
            post_changed([mode] { stereo::set_mode(mode); hostui::set("stereoMode", std::to_string(mode)); });
        float depth = stereo::requestedDepth.load(), convergence = stereo::requestedConvergence.load();
        if (ImGui::SliderFloat("3D depth", &depth, 0.0f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
            post_changed([depth] { stereo::set_depth(depth); hostui::set("stereoDepth", std::to_string(depth)); });
        if (ImGui::SliderFloat("Convergence", &convergence, 10.0f, 10000.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            post_changed([convergence] { stereo::set_convergence(convergence); hostui::set("stereoConvergence", std::to_string(convergence)); });
        bool swap = stereo::requestedSwap.load();
        if (ImGui::Checkbox("Swap eyes", &swap))
            post_changed([swap] { stereo::requestedSwap = swap; hostui::set("stereoSwap", swap ? "1" : "0"); });
        help("Separate geometry rendering for each eye. Half SBS and TAB are anamorphic; Full SBS preserves each eye's pixels. Experimental: scene and effect validation is in progress.");
    }
#endif
#if defined(WWHD_HAS_VULKAN) && defined(WWHD_HDR_OUTPUT)
    if(render::vulkan()) {
        heading("HDR output");
        // On XWayland the saved HDR choice applies from the next start (it falls back to SDR now).
        const bool restart=hdr::waylandAfterRestart.load();
        const bool chosen=hdr::enabled.load()||(restart&&hdr::fellBackToSdr.load());
        if(radio("SDR",!chosen))post_changed([] {hdr::enabled=false;hdr::fellBackToSdr=false;hostui::set("hdrEnabled","0");});
        ImGui::SameLine();
        // scRGB when the surface offers it, otherwise HDR10 PQ (backend.cpp make_swapchain)
        const bool pq=hdr::enabled.load()?hdr::pqOutput.load():!hdr::scrgbSupported.load()&&hdr::pqSupported.load();
        if(radio(restart?"HDR (after restart)":pq?"HDR (HDR10 PQ)":"HDR (scRGB)",chosen,
                 restart||(hdr::displayEnabled.load()&&hdr::formatSupported.load())))
            post_changed([] {hdr::enabled=true;hdr::fellBackToSdr=false;hostui::set("hdrEnabled","1");});
        if(restart)note("The game is running through XWayland, which has no HDR. Choose HDR and restart the game to use Wayland.");
        else {
            if(hdr::fellBackToSdr.load())note("HDR is unavailable on this display. Using SDR output.");
#ifdef _WIN32
            if(!hdr::displayEnabled.load())note("Enable HDR in Windows for the display containing this window.");
#else
            if(!hdr::displayEnabled.load())note("Enable HDR in your compositor for the display containing this window.");
#endif
            else if(!hdr::formatSupported.load())note("This Vulkan surface supports neither scRGB nor HDR10 PQ output.");
        }
        auto slider=[](const char* label,const char* key,std::atomic<float>& setting,float lo,float hi,const char* fmt) {
            float v=setting.load();ImGui::SetNextItemWidth(260);
            if(ImGui::SliderFloat(label,&v,lo,hi,fmt,ImGuiSliderFlags_AlwaysClamp)) {
                auto* dst=&setting;
                post_changed([dst,v,lo,hi,key] {hdr::set(*dst,v,lo,hi);hostui::set(key,std::to_string(v));});
            }
        };
        ImGui::BeginDisabled(!hdr::enabled.load());
        slider("Paper white","hdrPaperWhite",hdr::paperWhite,80,500,"%.0f nits");
        slider("Peak brightness","hdrPeak",hdr::peak,200,4000,"%.0f nits");
        slider("Exposure","hdrExposure",hdr::exposure,-3,3,"%+.2f EV");
        slider("Highlight expansion","hdrHighlights",hdr::highlights,0,1,"%.2f");
        slider("Settings menu brightness","hdrMenuWhite",hdr::menuWhite,80,500,"%.0f nits");
        ImGui::EndDisabled();
        help("HDR output expands the game's SDR picture; it cannot recover already clipped lighting. Paper white also affects the in-game HUD. Settings-menu brightness is independent. Set peak brightness to your display's capability. SDR screenshots remain SDR.");
    }
#endif
    heading("Effects");
    bool v;
    float bloom = render::bloom_strength() * 100.0f;
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Bloom strength", &bloom, 0.0f, 200.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
        post_changed([bloom] { render::set_bloom_strength(bloom / 100.0f); });
    flow::next(flow::button("Off##bloom"));
    if (ImGui::Button("Off##bloom")) post_changed([] { render::set_bloom_strength(0.0f); });
    flow::next(flow::button("Default##bloom"));
    if (ImGui::Button("Default##bloom")) post_changed([] { render::set_bloom_strength(1.0f); });
    help("Glow around bright areas. 100% matches the original game; 0% turns bloom off. Applies immediately.");
    float haze = render::horizon_haze() * 100.0f;
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Horizon haze", &haze, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
        post_changed([haze] { render::set_horizon_haze(haze / 100.0f); });
    flow::next(flow::button("Default##haze"));
    if (ImGui::Button("Default##haze")) post_changed([] { render::set_horizon_haze(1.0f); });
    help("The pale band low in the sky. 100% matches the original game; lower values give a clearer, deeper blue "
         "sky near the horizon. Together with less bloom it gives a more vivid sky. Applies immediately.");
    static const char* const cloudStyles[] = {"Clouds: original", "Toon clouds (Lite)", "Toon clouds (High)"};
    for (int i = 0; i < 3; i++) {
        if (i) flow::next(flow::check(cloudStyles[i]));
        if (radio(cloudStyles[i], render::cloud_style() == i)) post_changed([i] { render::set_cloud_style(i); });
    }
    help("Original keeps the game's painted clouds. Toon clouds draws them as puffy cel-shaded clouds with sunlit "
         "tops, shaded undersides and banks along the horizon, from the same clouds the game places, so weather and "
         "time of day still drive them. Lite is lighter on the GPU (phones, handhelds); High adds self-shadowing.");
    if (check("Cloud shadows", render::cloud_shadows(), &v)) post_changed([v] { render::set_cloud_shadows(v); });
    help("Soft shadows of the drifting sky clouds on the sea, islands and characters. Weaker at dusk, off at night. "
         "Off matches the original game.");
    const bool ao_ok = render::feature_available(render::kFeatureAO);
    static const char* const ao[] = {"AO: original", "AO: centre fix", "AO: centre + noise fix"};
    for (int i = 0; i < 3; i++) {
        if (i) flow::next(flow::check(ao[i]));
        if (radio(ao[i], render::ao_mode() == i, ao_ok)) post_changed([i] { render::set_ao_mode(i); });
    }
    if (check("Full-size occlusion depth", render::ao_hires(), &v, render::feature_available(render::kFeatureAOHires)))
        post_changed([v] { render::set_ao_hires(v); });
    flow::next(flow::check("16x anisotropic filtering"), 30);
    if (check("16x anisotropic filtering", render::aniso(), &v, render::feature_available(render::kFeatureAniso)))
        post_changed([v] { render::set_aniso(v); });
    flow::next(flow::check("Edge smoothing (FXAA)"), 30);
    if (check("Edge smoothing (FXAA)", render::fxaa(), &v, render::feature_available(render::kFeatureFXAA)))
        post_changed([v] { render::set_fxaa(v); });
    // background_shaders.h: what a draw does while its shader compiles (off / fast)
    {
        static const char* const names[] = {"Shader compile: Off (exact)", "Fast (not exact)"};
        static const char* const tips[] = {
            "Default. Frames wait for their shaders, so pictures are complete, but a first visit to a place can "
            "stutter. (Metal waits up to 25 ms per frame, then leaves that object out for a moment.)",
            "Fast: smoother first visits, objects, shadows or effects may be missing for a moment. Anything the "
            "game reads back or keeps (photos, snapshots) still waits."};
        const bool env = bgshaders::env_override() != nullptr;
        const bgshaders::Level shown = env ? bgshaders::level() : bgshaders::requested();
        for (int i = 0; i < bgshaders::kLevels; i++) {
            if (i) flow::next(flow::check(names[i]));
            if (radio(names[i], shown == i, !env)) {
                const auto l = bgshaders::Level(i);
                bgshaders::set_level(l);  // applies to the next draw
                hostui::post([l] { hostui::set(bgshaders::kSettingKey, bgshaders::level_value(l)); });
            }
            help(tips[i]);
        }
        if (env) note("WWHD_BG_SHADERS=%s is set for this start and takes precedence.", bgshaders::env_override());
    }
#if defined(WWHD_HAS_VULKAN) && !defined(__ANDROID__)
    if (render::vulkan()) {
        heading("GPU (Vulkan)");
        const bool env = gfxvk::device_from_env();
        if (radio("Automatic", gfxvk::device_setting().empty(), !env))
            post_changed([] { gfxvk::set_device_setting(""); });
        for (size_t i = 0; i < gfxvk::device_inventory.size(); ++i) {
            const auto& gpu = gfxvk::device_inventory[i];
            ImGui::PushID(int(i));
            const auto label = gpu.name + " (" + gpu.type + ")";
            if (radio(label.c_str(), gfxvk::device_setting() == gpu.identity, !env && gpu.usable())) {
                const auto identity = gpu.identity;
                post_changed([identity] { gfxvk::set_device_setting(identity); });
            }
            if (!gpu.usable()) note("%s", gpu.unavailable.c_str());
            ImGui::PopID();
        }
        note("GPU changes apply after a restart. Currently using %s.", gfxvk::active_device.c_str());
#ifndef _WIN32
        if (ImGui::Button("Restart now##gpu")) hostui::post([] { render::restart(); });
#endif
        if (env) note("WWHD_VK_DEVICE=%s takes precedence for this start.", getenv("WWHD_VK_DEVICE"));
        if (!gfxvk::device_fallback.empty()) note("%s", gfxvk::device_fallback.c_str());
    }
#endif
    heading("Presentation (Vulkan)");
#ifdef WWHD_HAS_VULKAN
    {
        const bool vk = render::vulkan(), env = gfxvk::present_mode_from_env();
        static const char* const names[] = {"Vsync (smooth)", "Low latency", "Off (may tear)"};
        static const char* const tips[] = {"FIFO: every frame waits for the display's refresh; no tearing (default)",
                                           "MAILBOX: the newest finished frame is shown at the next refresh; no tearing, less delay",
                                           "IMMEDIATE: frames are shown at once; lowest delay, may tear"};
        // the mode in use: the setting, or vsync when this driver does not offer it
        const int in_use = gfxvk::present_mode_offered(gfxvk::present_mode()) ? gfxvk::present_mode() : gfxvk::kPresentFifo;
        for (int i = 0; i < gfxvk::kPresentModes; i++) {
            if (i) ImGui::SameLine();
            const bool offered = gfxvk::present_mode_offered(i);
            if (radio(names[i], in_use == i, vk && !env && offered)) post_changed([i] { gfxvk::set_present_mode(i); });
            if (vk && !offered) help("not offered by this driver");
            else help(tips[i]);
        }
        if (!vk) note("Used by the Vulkan renderer only (Metal always presents with vsync).");
        else if (env) note("WWHD_VK_PRESENT_MODE=%s is set for this start and takes precedence.", getenv("WWHD_VK_PRESENT_MODE"));
        else {
            std::string missing;
            for (int i = 1; i < gfxvk::kPresentModes; i++)
                if (!gfxvk::present_mode_offered(i)) missing += std::string(missing.empty() ? "" : ", ") + names[i];
            if (!missing.empty()) note("Not offered by this driver: %s.", missing.c_str());
        }
    }
#else
    ImGui::BeginDisabled();
    radio("Vsync (smooth)", true);
    ImGui::EndDisabled();
    note("This build has no Vulkan renderer.");
#endif
    heading("Overlay");
    if (check("Performance overlay (FPS, frame time)", perf_shown(), &v)) set_perf_shown(v);
    if (ImGui::Button("Reset performance averages")) { g_average.reset(); perf::reset(); interp::reset_display_hz_seen(); }
    // the render-thread profiler (render_prof.h): its report is the detailed part of the performance report
    if (check("Detailed performance profiling", rprof::requested(), &v, !rprof::env_override())) {
        rprof::set_requested(v);  // applied at the next frame
        hostui::post([v] { hostui::set(rprof::kSettingKey, v ? "1" : "0"); });
    }
    if (rprof::env_override()) note("WWHD_PROFILE=%s is set for this start and takes precedence.", rprof::env_override());
    else help("Adds timings, uploads and draw classes to Copy performance report. Takes effect at once; the first "
              "report is ready a few seconds later. Costs a little speed, so leave it off unless you send a "
              "performance report. Off by default.");
    // the render-thread profiler's latest report (render_prof.h), for performance bug reports
    if (ext::features().clipboard) {
        static double copiedAt = -10;
        if (ImGui::Button("Copy performance report")) {
            std::string report = performance_report();
            hostui::post([report] { hostui::set_clipboard(report); });
            copiedAt = ImGui::GetTime();
        }
        if (ImGui::GetTime() - copiedAt < 2.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("Copied");
        }
        help("Where the renderer spends its time over the last few seconds, as text for a bug report");
    }
#ifdef WWHD_HAS_VULKAN
    heading("Advanced");
    // Vulkan commands recorded and submitted on a worker thread (gfx/vulkan/record_worker.h)
    constexpr bool recordWorkerDefault = false;  // gfx/vulkan/record_worker.h record_worker_default()
    static bool recordWorker = [] { std::string value; return hostui::get("vkRecordWorker", value) ? value == "1" : recordWorkerDefault; }();
    if (check("Record graphics on a second thread", recordWorker, &v, !getenv("WWHD_VK_RECORD_WORKER"))) {
        recordWorker = v;
        hostui::post([v] { hostui::set("vkRecordWorker", v ? "1" : "0"); });
    }
    help("Applies after restarting the game. Hands the graphics work to a second CPU thread. This can raise "
         "the frame rate on devices whose CPU holds it back (phones, handhelds). On other computers it can "
         "make frame times less even (more short hitches). Uses a little more CPU and battery. Off by default.");
    static bool forceVertexFallback = [] { std::string value; return hostui::get("vkVertexFallback", value) && value == "1"; }();
    if (check("Force RGB vertex fallback", forceVertexFallback, &v, !getenv("WWHD_VK_VERTEX_FALLBACK"))) {
        forceVertexFallback = v;
        hostui::post([v] { hostui::set("vkVertexFallback", v ? "1" : "0"); });
    }
    help("Applies after restarting the game. Splits RGB vertex inputs for driver troubleshooting.");
    // issue #207 driver troubleshooting (gfx/vulkan/vertex_format_policy.h VertexDiagnostics)
    struct Diagnostic { const char* label; const char* key; const char* env; const char* tip; };
    static const Diagnostic diagnostics[] = {
        {"Convert vertices on the CPU", "vkCpuVertices", "WWHD_VK_CPU_VERTICES",
         "Applies after restarting the game. Byte swap and format conversion of the game's vertices run on the "
         "CPU instead of the vertex shader (slower; for driver troubleshooting)."},
        {"Full precision in shaders", "vkFullPrecision", "WWHD_VK_FULL_PRECISION",
         "Applies after restarting the game. Removes reduced-precision hints from every shader (driver troubleshooting)."},
        {"No vertex reuse", "vkNoVertexReuse", "WWHD_VK_NO_VERTEX_REUSE",
         "Applies after restarting the game. Copies and binds every vertex buffer in full for every draw "
         "(slower; for driver troubleshooting)."},
    };
    static bool diagnosticOn[3] = {};
    static bool diagnosticRead = false;
    if (!diagnosticRead) {
        diagnosticRead = true;
        for (int i = 0; i < 3; ++i) { std::string value; diagnosticOn[i] = hostui::get(diagnostics[i].key, value) && value == "1"; }
    }
    static int cpuMode = [] { std::string value; hostui::get("vkCpuVertices", value);
        return value == "1" ? 1 : value == "0" ? 2 : 0; }();
    ImGui::BeginDisabled(getenv("WWHD_VK_CPU_VERTICES") != nullptr);
    if (ImGui::Combo("Convert vertices on the CPU", &cpuMode, "Automatic\0On (all)\0Off\0")) {
        const std::string value = cpuMode == 1 ? "1" : cpuMode == 2 ? "0" : "auto";
        hostui::post([value] { hostui::set("vkCpuVertices", value); });
    }
    ImGui::EndDisabled();
    help("Applies after restarting. Automatic converts only what PowerVR GPUs decode wrongly (32-bit byte-swapped data). On converts every attribute; Off disables it.");
    if (ImGui::TreeNode("Vertex conversion diagnostics")) {
        static int cpuClass = [] { std::string value; hostui::get("vkCpuVertexClasses", value);
            const char* names[] = {"", "none", "swap16", "swap32", "snorm", "scaled", "3comp", "unaligned", "integer", "unorm", "all"};
            for (int i = 0; i < 11; ++i) if (value == names[i]) return i;
            return 0; }();
        ImGui::BeginDisabled(cpuMode != 0 || getenv("WWHD_VK_CPU_VERTICES") || getenv("WWHD_VK_CPU_VERTEX_CLASSES"));
        if (ImGui::Combo("CPU conversion class", &cpuClass,
            "Driver default\0None\0Swap16\0Swap32\0SNORM\0Scaled integers\0Three components\0Unaligned offset\0Integer\0UNORM\0All\0")) {
            const char* names[] = {"", "none", "swap16", "swap32", "snorm", "scaled", "3comp", "unaligned", "integer", "unorm", "all"};
            const std::string value = names[cpuClass];
            hostui::post([value] { hostui::set("vkCpuVertexClasses", value); });
        }
        ImGui::EndDisabled();
        help("For driver testing. Use Automatic above, select one class, then restart. The log records the active classes.");
        ImGui::TreePop();
    }
    for (int i = 1; i < 3; ++i) {
        if (check(diagnostics[i].label, diagnosticOn[i], &v, !getenv(diagnostics[i].env))) {
            diagnosticOn[i] = v;
            const char* key = diagnostics[i].key;
            hostui::post([key, v] { hostui::set(key, v ? "1" : "0"); });
        }
        help(diagnostics[i].tip);
    }
#endif
    heading("Bug reports");
#ifdef __ANDROID__
    if (ImGui::Button("Share log")) hostui::post([] {
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!env || !activity) return;
        auto cls = env->GetObjectClass(activity);
        auto method = env->GetMethodID(cls, "shareLog", "()V");
        if (method) env->CallVoidMethod(activity, method);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
    });
#endif
    // read once at start-up (main.cpp start_log_file); WWHD_LOG_FILE decides when set
    static bool logFile = [] { std::string v;
#ifdef __ANDROID__
        return !hostui::get("logFile", v) || v != "0";
#else
        return hostui::get("logFile", v) && v == "1";
#endif
    }();
    if (check("Write a log file", logFile, &v, !getenv("WWHD_LOG_FILE"))) {
        logFile = v;
        hostui::post([v] { hostui::set("logFile", v ? "1" : "0"); });
    }
#ifdef __ANDROID__
    help("Logging is on by default. Share the current and previous run after reproducing a bug. "
         "User paths are removed; each log stops at 64 MiB. Changes apply on the next start.");
#else
    help("From the next start: captures/wwhd.log in the game's data folder (the previous run's is kept as "
         "wwhd-previous.log), with your user paths removed. Turn it on before reproducing a bug and attach "
         "the file to the bug report.");
#endif
}

// Both previews use the exact same geometry as the host compositor.
static gfx::Box pip_tv_box() {
    float x = 0, y = 0, w = 1, h = 1;
    gfx::main_picture(&x, &y, &w, &h);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    return {x * ds.x, y * ds.y, w * ds.x, h * ds.y};
}
// Dragging and the D-pad place the picture freely (#221): passing over a preset never catches it.
static void pip_move(gfx::Box tv, float x, float y) {
    gfx::pip_set_position(gfx::pip_place(tv, gfx::g_drc_aspect, gfx::g_pip_size, x, y));
}
// The nine preset targets in the diagram: large enough to see and to hit with a finger.
static float pip_target_radius() { return std::max(7.0f, ImGui::GetFontSize() * 0.5f); }
static void pip_diagram() {
    const gfx::Box screen = pip_tv_box();
    const float w = std::min(320.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const gfx::Box tv{origin.x, origin.y, w, w * screen.h / screen.w};
    ImGui::InvisibleButton("Placement diagram", ImVec2(tv.w, tv.h), ImGuiButtonFlags_EnableNav);
    const bool active = ImGui::IsItemActive(), focused = ImGui::IsItemFocused();
    auto b = gfx::pip_box(tv, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    const float radius = pip_target_radius();
    // A press only becomes a drag once the pointer moves past ImGui's drag threshold. A press that is
    // released without dragging is a click/tap: on a preset target it snaps there, elsewhere outside the
    // picture it moves the picture's centre to that point.
    static ImVec2 offset;
    static bool pointer = false, dragged = false, grabbed = false;
    if (ImGui::IsItemActivated()) {
        pointer = ImGui::GetCurrentContext()->ActiveIdSource == ImGuiInputSource_Mouse;
        dragged = false;
        const ImVec2 m = ImGui::GetIO().MousePos;
        grabbed = m.x >= b.x && m.x <= b.x + b.w && m.y >= b.y && m.y <= b.y + b.h;
        offset = grabbed ? ImVec2(m.x - b.x - b.w / 2, m.y - b.y - b.h / 2) : ImVec2(0, 0);
    }
    if (active && pointer && ImGui::IsMouseDown(0) && ImGui::IsMouseDragging(0))
        dragged = true;
    if (active && pointer && dragged) {
        const auto m = ImGui::GetIO().MousePos;
        pip_move(tv, (m.x - offset.x - tv.x) / tv.w, (m.y - offset.y - tv.y) / tv.h);
    }
    if (ImGui::IsItemDeactivated()) {
        if (pointer && !dragged) {
            const ImVec2 m = ImGui::GetIO().MouseClickedPos[0];
            const int a = gfx::pip_target_at(tv, m.x, m.y, radius * 1.5f);
            if (a >= 0)
                gfx::pip_set_position({a, gfx::g_pip_x, gfx::g_pip_y});
            else if (!grabbed)
                pip_move(tv, (m.x - tv.x) / tv.w, (m.y - tv.y) / tv.h);
        }
        pointer = dragged = false;
        hostui::post([] { hostui::save_pip_position(); });
    }
    if (focused) {
        for (auto key :
             {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown,
              ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow})
            ImGui::SetKeyOwner(key, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
        auto pressed = [](ImGuiKey key) {
            return ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, ImGui::GetItemID());
        };
        float dx = 0, dy = 0;
        if (pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_LeftArrow))
            dx = -0.01f;
        if (pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_RightArrow))
            dx = 0.01f;
        if (pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_UpArrow))
            dy = -0.01f;
        if (pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_DownArrow))
            dy = 0.01f;
        if (dx || dy) {
            pip_move(tv, (b.x - tv.x + b.w / 2) / tv.w + dx, (b.y - tv.y + b.h / 2) / tv.h + dy);
            hostui::post([] { hostui::save_pip_position(); });
        }
    }
    b = gfx::pip_box(tv, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    auto *d = ImGui::GetWindowDrawList();
    d->AddRectFilled(origin, ImVec2(tv.x + tv.w, tv.y + tv.h), IM_COL32(18, 28, 36, 255));
    d->AddRect(origin, ImVec2(tv.x + tv.w, tv.y + tv.h),
               focused ? IM_COL32(255, 210, 80, 255) : IM_COL32(180, 190, 200, 255));
    d->AddRectFilled(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h),
                     IM_COL32(75, 180, 220, int(255 * gfx::g_pip_opacity)));
    d->AddRect(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), IM_COL32(235, 245, 255, 255));
    // Targets on top of the picture, so they stay visible at large sizes; the chosen preset is filled.
    const int anchor = gfx::g_pip_anchor;
    for (int a = 0; a < gfx::kPipCustom; ++a) {
        ImVec2 c;
        gfx::pip_target_centre(tv, a, &c.x, &c.y);
        d->AddCircleFilled(c, radius, a == anchor ? IM_COL32(255, 210, 80, 235) : IM_COL32(18, 28, 36, 170));
        d->AddCircle(c, radius, a == anchor ? IM_COL32(255, 240, 200, 255) : IM_COL32(220, 230, 240, 230), 0,
                     std::max(1.5f, radius * 0.22f));
    }
    note("Drag to place the picture anywhere. Click or tap a circle to snap it to that spot. "
         "Focus the diagram and use the D-pad to move.");
}
static void pip_live_drag() {
    if (!gfx::g_pip_preview)
        return;
    const auto tv = pip_tv_box();
    const auto b = gfx::pip_box(tv, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    ImGui::SetNextWindowPos(ImVec2(b.x, b.y));
    ImGui::SetNextWindowSize(ImVec2(b.w, b.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin("##pip_live_drag", nullptr, flags);
    ImGui::InvisibleButton("##drag", ImVec2(b.w, b.h));
    static ImVec2 offset;
    if (ImGui::IsItemActivated()) {
        const auto m = ImGui::GetIO().MousePos;
        offset = ImVec2(m.x - b.x - b.w / 2, m.y - b.y - b.h / 2);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        const auto m = ImGui::GetIO().MousePos;
        pip_move(tv, (m.x - offset.x - tv.x) / tv.w, (m.y - offset.y - tv.y) / tv.h);
    }
    if (ImGui::IsItemDeactivated())
        hostui::post([] { hostui::save_pip_position(); });
    ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->AddRect(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), IM_COL32(255, 220, 90, 255),
                                            0, 1.5f);
    ImGui::End();
    ImGui::PopStyleVar();
}

// One slider per value; the bounds hold for mouse, touch, controller and ImGui's Ctrl+click typing.
static void pip_percent(const char* label, float value, float minimum, float maximum, void (*save)(float)) {
    const int lo = int(std::lround(minimum * 100)), hi = int(std::lround(maximum * 100));
    int percent = std::clamp(int(std::lround(value * 100)), lo, hi);
    if (ImGui::SliderInt(label, &percent, lo, hi, "%d%%", ImGuiSliderFlags_AlwaysClamp))
        hostui::post([percent, save] { save(percent / 100.0f); });
}

void tab_display() {
    bool v;
    heading("GamePad screen");
#ifdef __ANDROID__
    if (check("Swap TV and GamePad displays", hostui::displays_swapped(), &v))
        hostui::post([v] { hostui::set_displays_swapped(v); });
#endif
    // Offer separate-window mode while Android has a usable display, including
    // when its surface has been released.
    const int mode = hostui::drc_mode();
    if (hostui::drc_modes() >= 4) {
        static const char* const modes[] = {"Separate window", "Picture-in-picture", "Automatic picture-in-picture", "Off", "GamePad only"};
        if (ImGui::BeginCombo("View", modes[mode])) {
            for (int i=0; i<5; ++i) {
                if (hostui::drc_mode_offered(i) && ImGui::Selectable(modes[i],mode==i))
                    hostui::post([i] { hostui::set_drc_mode(i); });
            }
            ImGui::EndCombo();
        }
        if (mode == 0)
            note("Gaming Mode shows one window at a time. A separate GamePad window may make the game picture blurry.");
        if (mode == 2)
            note("Shows GamePad menus and the Wind Waker song list while open and follows Off-TV Play.");
        if (mode == 4)
            note("Only the GamePad picture. In the game, Minus switches to Off-TV "
                 "Play (the game on the GamePad).");
    }
    if (check(mode == 2 ? "Pin GamePad picture-in-picture" : "Show GamePad screen", hostui::drc_shown(), &v,
              hostui::drc_available() && (gfx::g_second_display_available ||
                  !(hostui::drc_modes() >= 4 && (mode == 3 || mode == 4)))))
        hostui::post([v] { hostui::show_drc(v); });
    if (hostui::drc_modes() >= 4 && (mode == 1 || mode == 2 || mode == 3)) {
        bool eligible = true;
#ifdef __ANDROID__
        eligible = !gfx::g_has_drc_window;
#endif
        if (eligible && ImGui::CollapsingHeader("Picture-in-picture placement", ImGuiTreeNodeFlags_DefaultOpen)) {
            gfx::g_pip_preview = true;
            static const char *const names[] = {"Top left",     "Top centre",   "Top right",   "Centre left",
                                                "Centre",       "Centre right", "Bottom left", "Bottom centre",
                                                "Bottom right", "Custom"};
            if (ImGui::BeginCombo("Position", names[hostui::pip_anchor()])) {
                for (int i = 0; i < 10; ++i)
                    if (ImGui::Selectable(names[i], hostui::pip_anchor() == i))
                        hostui::post([i] { hostui::set_pip_anchor(i); });
                ImGui::EndCombo();
            }
            pip_diagram();
            pip_percent("Size (%)", hostui::pip_size(), gfx::kPipSizeMin, gfx::kPipSizeMax, hostui::set_pip_size);
            pip_percent("Opacity (%)", hostui::pip_opacity(), gfx::kPipOpacityMin, gfx::kPipOpacityMax,
                        hostui::set_pip_opacity);
            note("The game picture previews changes while this section is open.");
        }
    }
    heading("Window");
    if (check("Full screen", hostui::fullscreen(), &v))
        hostui::post([v] { hostui::set_fullscreen(v); });
#ifndef __ANDROID__ // always full screen there
    help(!strcmp(hostui::name(), "AppKit") ? "The TV window (Cmd+F); remembered for the next start"
                                           : "The TV window (F11 or Alt+Enter); remembered for the next start");
#endif
#ifdef __ANDROID__
    heading("Interface");
    int uiSize = g_ui_size;
    if (combo("UI size", &uiSize, "Automatic\0Small\0Medium\0Large\0"))
        hostui::post([uiSize] {
            g_ui_size = uiSize;
            hostui::set("uiSize", std::to_string(uiSize));
        });
    help("Automatic sizes Settings for your screen: more compact on a phone, larger on a tablet or handheld, "
         "with every button at least 7 mm tall. Small, Medium and Large set the size yourself.");
    heading("Touch controls");
    int touchMode = gfx::g_touch_buttons;
    if (combo("Touch buttons", &touchMode, "Automatic\0Always show\0Hide\0"))
        hostui::post([touchMode] {
            gfx::g_touch_buttons = touchMode;
            hostui::set("touchButtons", std::to_string(touchMode));
        });
    help("Automatic hides the floating buttons while a controller is connected. "
         "Tap near the top-left corner to reveal them for five seconds.");
    if (check("On-screen controls", gfx::g_touch_controls, &v))
        hostui::post([v] {
            gfx::g_touch_controls = v;
            hostui::set("touchControls", v ? "1" : "0");
        });
#endif
    heading("Picture scaling");
    static const char *const f[] = {"Smooth", "Sharp", "Integer scale (pixel exact)"};
    const bool fok = hostui::scale_filter_available();
    for (int i = 0; i < 3; i++) {
        if (i)
            flow::next(flow::check(f[i]));
        if (radio(f[i], hostui::scale_filter() == i, fok))
            hostui::post([i] { hostui::set_scale_filter(i); });
    }

}

mods::catalogue::Version catalogue_port_version() {
    // a build ahead of its release counts as the next one (mods::catalogue::port_version_from_build)
    return mods::catalogue::port_version_from_build(build::version());
}
using setupflow::setup_run;
using setupflow::setup_run_tick;

void code_mod_dialog() {
    auto status=mods::code::status();
    if(!status.requested)return;
    // Explicit test acceptance drives the same offer/rebuild path in an isolated,
    // input-free run. The normal native-code trust check still runs first.
    static bool test_accepted=false;
    if(g_no_host&&getenv("WWHD_TEST_CODE_MOD_REBUILD")&&!test_accepted&&!status.building&&!status.ready) {
        test_accepted=true;mods::code::begin();
    }
#ifdef __ANDROID__
    const bool closable=true;  // building: Hide
#else
    const bool closable=!status.building;  // building: Cancel rebuild, or wait for it
#endif
    if(popup::begin("Rebuild code-mod support",true,0,closable)) {
        // the X, a tap outside, Esc or B: Later, Hide or Cancel (never cancels a rebuild on desktop)
        const bool back=navigation_pressed(input_map::face_input(input_map::kB));
        const bool dismissed=popup::dismissed(back);
        if(dismissed&&back)g_pad_b_used=true;
        ImGui::PushTextWrapPos(popup::wrap_pos(30));
        if(status.ready) {
            ImGui::TextWrapped("Game code is ready. Restart the game to apply the change.");
            if(!status.error.empty())ImGui::TextWrapped("%s",status.error.c_str());
            if(ImGui::Button("Restart now"))hostui::post([] {std::string error;if(!mods::code::restart(error))fprintf(stderr,"[code mods] %s\n",error.c_str());});
            ImGui::SameLine();
            if(ImGui::Button("Later")||dismissed){mods::code::dismiss();ImGui::CloseCurrentPopup();}
        } else if(status.building) {
            ImGui::TextUnformatted(status.stage.c_str());
            if(status.total)ImGui::ProgressBar(float(status.done)/status.total,ImVec2(360,0));
#ifdef __ANDROID__
            ImGui::TextWrapped("The setup builds it in the background; you can keep playing. Its notification and screen can pause it.");
            if(ImGui::Button("Hide")||dismissed){mods::code::cancel();mods::code::dismiss();ImGui::CloseCurrentPopup();}
#else
            if(ImGui::Button("Cancel rebuild"))mods::code::cancel();
#endif
        } else {
            if(status.target)ImGui::TextWrapped("This mod needs code-mod support. Enable it now?");
            ImGui::TextWrapped("Code mods need the game code to be rebuilt %s mod support, %s. Rebuild now?",status.target?"with":"without",mods::code::rebuild_duration());
            if(!status.error.empty())ImGui::TextWrapped("%s",status.error.c_str());
            if(ImGui::Button("Rebuild now"))mods::code::begin();
            ImGui::SameLine();
            if(ImGui::Button("Cancel")||dismissed){mods::code::dismiss();ImGui::CloseCurrentPopup();}
        }
        ImGui::PopTextWrapPos();
        popup::end();
    }
}

// ---- "Restart required": derived each frame from the mod manager's state (restart_notice.h)
restart::Notice g_restart_notice;
std::vector<restart::Item> g_restart_items;
bool restart_possible(const mods::code::Status& code) {
#ifdef __ANDROID__
    return false;
#else
    // a finished code-mod rebuild relaunches through its own path, which also works on Windows
    return render::can_restart() || code.ready;
#endif
}
void restart_now(bool code_build) {
    hostui::post([code_build] {
        std::string error;
        if (code_build) { if (!mods::code::restart(error)) LOG("[code mods] %s", error.c_str()); }
        else if (!render::restart()) LOG("[mods] cannot relaunch the game; restart it to apply mod changes");
    });
}
// Every frame the settings window is shown (any tab), and once at the first overlay frame so that
// changes made before the menu is opened (a resumed one-click setup) still count as new.
void restart_notice_frame(bool draw) {
    auto code = mods::code::status();
    g_restart_items = restart::pending(mods::packages::list(), code, experimental::staged_restore());
    // the rebuild dialog has told the player already ("Restart now" / "Later")
    if (code.requested) for (const auto& item : g_restart_items) if (item.key.starts_with("code:")) g_restart_notice.acknowledge(item.key);
    // ... and so has the end of a mod install (Restart now / Later on the mod's page)
    const auto& flow = modsui::state().job.flow;
    if (flow.active())
        for (const auto& item : g_restart_items)
            if (item.key == "code:on:" + flow.id) g_restart_notice.acknowledge(item.key);
    // a mod switched on or off says "Restart to apply" in its row, and the Mods tab has the Restart now line:
    // no popup on top of that
    for (const auto& item : g_restart_items)
        if (item.key.starts_with("pkg:")) g_restart_notice.acknowledge(item.key);
    const bool blocked = !draw || code.requested || code.building || (!setup_run.id.empty() && setup_run.error.empty()) || flow.active() ||
                         ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || ImGui::IsAnyItemActive();
    const bool open = g_restart_notice.update(g_restart_items, ImGui::GetTime(), blocked);
    if (!draw) return;
    const bool back = navigation_pressed(input_map::face_input(input_map::kB));
    auto answer = restart::dialog(g_restart_notice, open, restart_possible(code), back);
    if (answer == restart::Answer::Later && back) g_pad_b_used = true;
    if (answer == restart::Answer::RestartNow) restart_now(code.ready);
}

// A built-in mod's options on its page (Mods > the mod).
void builtin_options(const std::string& id) {
    if (id == "direct-camera") {
        float speed = mods::camera_speed();
        if (ImGui::SliderFloat("Camera speed", &speed, .5f, 2.f, "%.2fx"))
            hostui::post([speed] { mods::set_camera_speed(speed); hostui::set("mod.direct-camera.speed", std::to_string(speed)); mods::packages::remember_option("direct-camera.speed", speed); });
    } else if (id == "lock-on") {
        float range = mods::lock_on_range();
        if (ImGui::SliderFloat("Enemy lock-on range", &range, 1.f, 3.f, "%.2fx"))
            hostui::post([range] { mods::set_lock_on_range(range); hostui::set("mod.lock-on.range", std::to_string(range)); mods::packages::remember_option("lock-on.range", range); });
        bool cycling = mods::lock_on_cycling();
        if (ImGui::Checkbox("Cycle enemy targets with R3", &cycling))
            hostui::post([cycling] { mods::set_lock_on_cycling(cycling); hostui::set("mod.lock-on.cycling", cycling ? "1" : "0"); mods::packages::remember_option("lock-on.cycling", cycling); });
        help("Range 1 keeps the original enemy targeting distance. Press R3 while locked on "
             "to cycle enemy targets. Disabling this mod restores original targeting behavior.");
        for (const char* env : {"WWHD_MOD_LOCK_ON_RANGE", "WWHD_MOD_LOCK_ON_CYCLING"})
            if (const char* override = std::getenv(env))
                note("%s=%s overrides the saved option at startup.", env, override);
    } else if (id == "jump-anywhere") {
        const char* names[] = {"L", "R", "ZL", "ZR", "L3", "R3"};
        const uint32_t buttons[] = {input::kL, input::kR, input::kZL, input::kZR, input::kStickL, input::kStickR};
        for (int i = 0; i < 6; ++i) {
            if (i) flow::next(flow::check(names[i]));
            if (radio(names[i], mods::jump_button() == buttons[i])) {
                auto button = buttons[i];
                hostui::post([button] { mods::set_jump_button(button); hostui::set("mod.jump-anywhere.button", std::to_string(button)); mods::packages::remember_option("jump-anywhere.button", button); });
            }
        }
        bool dj = mods::double_jump();
        if (ImGui::Checkbox("Double jump (once more in the air)", &dj))
            hostui::post([dj] { mods::set_double_jump(dj); hostui::set("mod.jump-anywhere.double", dj ? "1" : "0"); mods::packages::remember_option("jump-anywhere.double", dj); });
        bool no_auto = mods::no_auto_jump();
        if (ImGui::Checkbox("No automatic jumps off ledges", &no_auto))
            hostui::post([no_auto] { mods::set_no_auto_jump(no_auto); hostui::set("mod.jump-anywhere.no-auto", no_auto ? "1" : "0"); mods::packages::remember_option("jump-anywhere.no-auto", no_auto); });
        help("The button also keeps its normal use. Link drops off ledges instead of jumping when automatic ledge jumps are off; press the button just after leaving the edge to jump. The moon jump cheat takes L while it is on.");
    } else if (id == "quick-use") {
        ImGui::TextUnformatted("Quick-use animation speed");
        for (float speed : {1.f, 1.25f, 1.5f}) {
            const char* label = speed == 1.f ? "1x (vanilla)" : speed == 1.25f ? "1.25x" : "1.5x";
            if (speed != 1.f) flow::next(flow::check(label));
            if (radio(label, mods::quick_use_speed() == speed))
                hostui::post([speed] { mods::set_quick_use_speed(speed); hostui::set("mod.quick-use.speed", std::to_string(speed)); mods::packages::remember_option("quick-use.speed", speed); });
        }
        help("Only accelerates supported upper-body equip and boomerang wind-up for enemy-locked quick use. "
             "Does not bypass native readiness or ammo, or change projectile speed or flight/return. "
             "Default: vanilla 1x, with quick use off.");
        if (const char* override = std::getenv("WWHD_MOD_QUICK_USE_SPEED"))
            note("WWHD_MOD_QUICK_USE_SPEED=%s overrides the saved speed at startup.", override);
    } else if (id == "fast-forward") {
        for (unsigned rate : {2u, 3u, 4u}) {
            if (rate != 2) flow::next(flow::check(rate == 2 ? "2x" : rate == 3 ? "3x" : "4x"));
            if (radio(rate == 2 ? "2x" : rate == 3 ? "3x" : "4x", mods::fast_forward_rate() == rate))
                hostui::post([rate] { mods::set_fast_forward_rate(rate); hostui::set("mod.fast-forward.rate", std::to_string(rate)); mods::packages::remember_option("fast-forward.rate", rate); });
        }
        const char* names[] = {"L3", "R3", "L", "R", "ZL", "ZR"};
        const uint32_t buttons[] = {input::kStickL, input::kStickR, input::kL, input::kR, input::kZL, input::kZR};
        for (int i = 0; i < 6; ++i) {
            if (i) flow::next(flow::check(names[i]));
            if (radio(names[i], mods::fast_forward_button() == buttons[i])) {
                auto button = buttons[i];
                hostui::post([button] { mods::set_fast_forward_button(button); hostui::set("mod.fast-forward.button", std::to_string(button)); mods::packages::remember_option("fast-forward.button", button); });
            }
        }
        bool mute = mods::fast_forward_mute();
        if (ImGui::Checkbox("Mute audio while fast forwarding", &mute))
            hostui::post([mute] { mods::set_fast_forward_mute(mute); hostui::set("mod.fast-forward.mute", mute ? "1" : "0"); mods::packages::remember_option("fast-forward.mute", mute); });
        help("Hold during a cutscene or conversation. Prompts still wait for input. Rebind the selected game button in Controls. Unmuted audio plays at a higher pitch.");
    } else if (id == "move-speed") {
        if (radio("Hold the button", mods::move_speed_mode() == mods::MoveMode::kHold))
            hostui::post([] { mods::set_move_speed_mode(mods::MoveMode::kHold); hostui::set("mod.move-speed.mode", "0"); mods::packages::remember_option("move-speed.mode", 0); });
        flow::next(flow::check("Press to toggle"));
        if (radio("Press to toggle", mods::move_speed_mode() == mods::MoveMode::kToggle))
            hostui::post([] { mods::set_move_speed_mode(mods::MoveMode::kToggle); hostui::set("mod.move-speed.mode", "1"); mods::packages::remember_option("move-speed.mode", 1); });
        if (radio("Native animation, played faster", mods::move_speed_anim() == mods::MoveAnim::kNative))
            hostui::post([] { mods::set_move_speed_anim(mods::MoveAnim::kNative); hostui::set("mod.move-speed.anim", "native"); mods::packages::remember_option("move-speed.anim", std::string("native")); });
        modsui::same_line_if_fits("Dash instead of walk while boosting");
        if (radio("Dash instead of walk while boosting", mods::move_speed_anim() == mods::MoveAnim::kDash))
            hostui::post([] { mods::set_move_speed_anim(mods::MoveAnim::kDash); hostui::set("mod.move-speed.anim", "dash"); mods::packages::remember_option("move-speed.anim", std::string("dash")); });
        if (radio("Sprint pose (lean and arm drive)", mods::move_speed_anim() == mods::MoveAnim::kSprint))
            hostui::post([] { mods::set_move_speed_anim(mods::MoveAnim::kSprint); hostui::set("mod.move-speed.anim", "sprint"); mods::packages::remember_option("move-speed.anim", std::string("sprint")); });
        float running = mods::move_speed_land_factor();
        if (ImGui::SliderFloat("Running", &running, 1.f, 4.f, "%.2fx"))
            hostui::post([running] { mods::set_move_speed_land_factor(running); hostui::set("mod.move-speed.land", std::to_string(running)); mods::packages::remember_option("move-speed.land", running); });
        float swimming = mods::move_speed_swim_factor();
        if (ImGui::SliderFloat("Swimming", &swimming, 1.f, 4.f, "%.2fx"))
            hostui::post([swimming] { mods::set_move_speed_swim_factor(swimming); hostui::set("mod.move-speed.swim", std::to_string(swimming)); mods::packages::remember_option("move-speed.swim", swimming); });
        float seconds = mods::move_speed_stamina_seconds();
        if (ImGui::SliderFloat("Boost time", &seconds, 0.f, 20.f, seconds <= 0.f ? "no limit" : "%.1f s"))
            hostui::post([seconds] { mods::set_move_speed_stamina_seconds(seconds); hostui::set("mod.move-speed.stamina", std::to_string(seconds)); mods::packages::remember_option("move-speed.stamina", seconds); });
        float cooldown = mods::move_speed_cooldown_seconds();
        if (ImGui::SliderFloat("Cooldown", &cooldown, 0.f, 20.f, cooldown <= 0.f ? "none" : "%.1f s"))
            hostui::post([cooldown] { mods::set_move_speed_cooldown_seconds(cooldown); hostui::set("mod.move-speed.cooldown", std::to_string(cooldown)); mods::packages::remember_option("move-speed.cooldown", cooldown); });
        bool bar = mods::move_speed_bar();
        if (ImGui::Checkbox("Show boost bar", &bar))
            hostui::post([bar] { mods::set_move_speed_bar(bar); hostui::set("mod.move-speed.bar", bar ? "1" : "0"); mods::packages::remember_option("move-speed.bar", bar); });
        const char* names[] = {"L3", "R3", "L", "R", "ZL", "ZR"};
        const uint32_t buttons[] = {input::kStickL, input::kStickR, input::kL, input::kR, input::kZL, input::kZR};
        for (int i = 0; i < 6; ++i) {
            if (i) flow::next(flow::check(names[i]));
            if (radio(names[i], mods::move_speed_button() == buttons[i])) {
                auto button = buttons[i];
                hostui::post([button] { mods::set_move_speed_button(button); hostui::set("mod.move-speed.button", std::to_string(button)); mods::packages::remember_option("move-speed.button", button); });
            }
        }
        modsui::same_line_if_fits("Roll, then hold A");
        if (radio("Roll, then hold A", mods::move_speed_button() == mods::kMoveButtonRollA))
            hostui::post([] { mods::set_move_speed_button(mods::kMoveButtonRollA); hostui::set("mod.move-speed.button", std::to_string(mods::kMoveButtonRollA)); mods::packages::remember_option("move-speed.button", mods::kMoveButtonRollA); });
        if (const char* override = std::getenv("WWHD_MOD_MOVE_BUTTON"))
            note("WWHD_MOD_MOVE_BUTTON=%s overrides the saved button at startup.", override);
        help("A multiplier of 1 leaves that state alone. Boost time is how long a full bar lasts; "
             "0 has no limit, and the bar refills whenever you are not boosting. Cooldown is the "
             "bar's refill time (0 for immediate refill). With no limit, or with Show boost bar off, "
             "no bar is shown; the boost works the same. The bar also hides while a menu, "
             "a cutscene or a conversation is on screen. The dash option replaces the walk clip "
             "while boosting; full-speed running already uses dash. Sprint pose adds a forward "
             "lean and arm drive while running freely, blended with the boost. Rebind the "
             "chosen game button in Controls. Roll, then hold A: roll while running and keep A "
             "held as you come out of the roll to boost until you let go. It is always held "
             "(Press to toggle does not apply) and works on land only.");
    } else if (id == "ladder-speed" || id == "sidle-speed" || id == "ivy-speed" || id == "hang-speed") {
        const char* key = id == "ladder-speed" ? "ladder-speed.factor" : id == "sidle-speed" ? "sidle-speed.factor"
                        : id == "ivy-speed" ? "ivy-speed.factor" : "hang-speed.factor";
        float factor = id == "ladder-speed" ? mods::ladder_speed_factor() : id == "sidle-speed" ? mods::sidle_speed_factor()
                     : id == "ivy-speed" ? mods::ivy_speed_factor() : mods::hang_speed_factor();
        if (ImGui::SliderFloat("Speed", &factor, 1.f, 3.f, "%.2fx"))
            hostui::post([key, factor] { mods::manager::set_traversal_option(key, factor); });
    } else if (id == "sail-speed") {
        float normal = mods::sail_speed_normal_factor();
        if (ImGui::SliderFloat("Normal sail", &normal, 1.f, 3.f, "%.2fx"))
            hostui::post([normal] { mods::manager::set_traversal_option("sail-speed.normal", normal); });
        float swift = mods::sail_speed_swift_factor();
        if (ImGui::SliderFloat("Swift Sail", &swift, 1.f, 3.f, "%.2fx"))
            hostui::post([swift] { mods::manager::set_traversal_option("sail-speed.swift", swift); });
        help("Multiplies the speed the wind gives the boat. The jump (R) keeps its normal height.");
    } else if (id == "mouse-camera") {
        float sensitivity = mods::mouse_sensitivity();
        if (ImGui::SliderFloat("Sensitivity", &sensitivity, .08f, .3f, "%.3f"))
            hostui::post([sensitivity] { mods::set_mouse_sensitivity(sensitivity); hostui::set("mod.mouse-camera.sensitivity", std::to_string(sensitivity)); mods::packages::remember_option("mouse-camera.sensitivity", sensitivity); });
    }
}

void cheats() {
    bool v;
    note("Save in the game to keep them.");
    auto cheat = [first = true](const char* label, int which) mutable {
        if (!first) modsui::same_line_if_fits(label);
        first = false;
        if (ImGui::Button(label)) mods::request_cheat(which);
    };
    cheat("Give all items", mods::kCheatItems);
    cheat("Equip Master Sword + Mirror Shield (until reload)", mods::kCheatSword);
    cheat("20 hearts, double magic, 5000 rupees", mods::kCheatStats);
    cheat("Fill the bags (spoils and bait)", mods::kCheatBags);
    static const std::pair<const char*, int> inf[] = {{"Infinite health", mods::kInfHealth}, {"Infinite magic", mods::kInfMagic},
                                                      {"Infinite arrows and bombs", mods::kInfAmmo},
                                                      {"Infinite rupees", mods::kInfRupees},
                                                      {"Infinite bait and spoils", mods::kInfBags},
                                                      {"Infinite swimming", mods::kInfBreath},
                                                      {"Moon jump (hold L; left stick steers)", mods::kMoonJump},
                                                      {"No fall damage after moon jump", mods::kMoonNoFall}};
    for (const auto& [label, which] : inf) {
        int bit = which;
        if (check(label, mods::infinite(bit), &v)) hostui::post([bit, v] { mods::set_infinite(bit, v); });
    }
    heading("Story cheats (can break story events; use a spare save file)");
    auto story = [first = true](const char* label, int which) mutable {
        if (!first) modsui::same_line_if_fits(label);
        first = false;
        if (ImGui::Button(label)) mods::request_cheat(which);
    };
    story("All songs", mods::kCheatSongs);
    story("All Triforce shards", mods::kCheatTriforce);
    story("Map, compass, boss key", mods::kCheatDungeon);
    story("Small key", mods::kCheatKey);
}

modsui::Host& mods_host() {
    static modsui::Host h = [] {
        modsui::Host h;
        h.choose_game_source = [](bool folder, std::function<void(std::string)> chosen) { hostui::choose_game_source(folder, std::move(chosen)); };
        h.choose_mod_source = [](bool folder, std::function<void(std::string)> chosen) { hostui::choose_mod_source(folder, std::move(chosen)); };
        h.fetch = host::download_https;
        h.can_restart = [] { return restart_possible(mods::code::status()); };
        h.restart = [](bool code_build) { restart_now(code_build); };
        h.builtin_options = [](const char* id) { builtin_options(id); };
        h.cheats = [] { cheats(); };
        h.port_version = catalogue_port_version();
        h.build_name = g_guest_build_name;
#ifdef __ANDROID__
        h.android = true;
#endif
        h.test = g_no_host && getenv("WWHD_MOD_MANAGER_DIR");
        h.file_picker = ext::features().file_picker;
        h.code_mods = ext::features().code_mods;
        h.mod_downloads = ext::features().mod_downloads;
        return h;
    }();
    h.back = false;
    h.b_used = false;
    return h;
}

void tab_mods() {
    auto& h = mods_host();
    h.back = navigation_pressed(input_map::face_input(input_map::kB));
    modsui::draw(h, restart::summary(g_restart_items));
    if (h.b_used) g_pad_b_used = true;
}

void tab_shortcuts() {
    using namespace input_map;
    static int capture_hold = 0;
    auto m = current();
    note("Tap a controller button, or hold one and press another. Right-click to clear; Delete, Backspace or X clears the focused binding. Home is listed under Settings. Select used as a hold button is reserved for combinations.");
    if (ImGui::Button("Reset to defaults")) {
        m.shortcuts = shortcut_defaults();
        m.pad[kScreenshot] = kPadNone;
        set_current(m);
        U.cap_action = -1; g_capturing_keys = false; capture_hold = 0;
    }
    if (U.cap_action >= kActionCount) {
        int a = U.cap_action - kActionCount;
        int code = g_capture_key.exchange(-2);
        bool done = false;
        if (U.cap_col < 2 && code >= 0 && code < kInputCount && code != kVK_Shift && code != kVK_RightShift &&
            code != kVK_Control && code != kVK_RightControl && code != kVK_Option && code != kVK_RightOption &&
            code != kVK_Command && code != kVK_RightCommand) {
            m.shortcuts[a].keys[U.cap_col] = {code, g_capture_mods.load() & 15};
            done = true; set_current(m);
        }
        if (U.cap_col == 2 && !done) {
            // Wait for the button that activated the UI to be released before capturing a hold.
            int& hold = capture_hold;
            if (U.binding_capture.poll(U.values) > 0 && !hold)
                for (int p = 1; p < kPadCount; ++p) if (U.values[p] > 0.5f) { hold = p; break; }
            if (hold && U.values[hold] <= 0.5f) {
                m.shortcuts[a].hold = kPadNone; m.shortcuts[a].button = hold;
                if (a == kPhoto) m.pad[kScreenshot] = kPadNone;
                set_current(m); done = true; hold = 0;
            }
            if (hold) for (int p = 1; p < kPadCount; ++p) if (p != hold && U.values[p] > 0.5f) {
                m.shortcuts[a].hold = hold; m.shortcuts[a].button = p;
                if (a == kPhoto) m.pad[kScreenshot] = kPadNone;
                set_current(m); done = true; hold = 0; break;
            }
            if (done) hold = 0;
        }
        if (done || now_s() - U.cap_started > 15) { U.capture_completed = true; U.cap_action = -1; g_capturing_keys = false; capture_hold = 0; g_pad_b_used = true; }
        else note(U.cap_col == 2 ? "Tap a controller button, or hold one and press another." : "Press a key or mouse button with optional modifiers.");
    }
    if (ImGui::BeginTable("shortcut bindings", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Key"); ImGui::TableSetupColumn("Alternate"); ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        std::string group;
        for (int a = 0; a < kShortcutCount; ++a) {
            if (group != shortcut_info(a).group) {
                group = shortcut_info(a).group;
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(group.c_str());
            }
            ImGui::PushID(a); ImGui::TableNextRow(); ImGui::TableNextColumn();
            const bool conflict = shortcut_conflict(m, a);
            if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.3f, 1));
            ImGui::TextWrapped("%s%s", shortcut_info(a).name, conflict ? " (conflict)" : "");
            if (conflict) ImGui::PopStyleColor();
            for (int col = 0; col < 3; ++col) {
                ImGui::TableNextColumn(); ImGui::PushID(col);
                const auto& b = m.shortcuts[a];
                std::string label = col < 2 ? shortcut_key_label(b.keys[col]) :
                    b.hold ? "Hold " + std::string(pad_short_label(b.hold)) + " + " + pad_short_label(b.button) :
                    b.button ? std::string(pad_short_label(b.button)) :
                    a == kPhoto && m.pad[kScreenshot] ? std::string(pad_short_label(m.pad[kScreenshot])) + " (legacy)" : "Unbound";
                if (U.cap_action == kActionCount + a && U.cap_col == col) label = "Press...";
                if (ImGui::Button((label + "###shortcut_binding").c_str(), ImVec2(-FLT_MIN, 0)) && U.cap_action < 0) {
                    U.cap_action = kActionCount + a; U.cap_col = col; U.cap_started = now_s();
                    U.binding_capture.reset(); capture_hold = 0; g_capture_key = -2; g_shortcut_capture = true; g_capturing_keys = true;
                }
                if (binding_clear_requested(U.cap_action >= 0 || U.capture_completed)) {
                    if (col < 2) m.shortcuts[a].keys[col] = {};
                    else { m.shortcuts[a].hold = m.shortcuts[a].button = 0; if (a == kPhoto) m.pad[kScreenshot] = 0; }
                    if (settings_reachable(m)) set_current(m);
                    U.cap_action = -1; g_capturing_keys = false; capture_hold = 0;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void start_capture(int a, int col) {
    g_shortcut_capture = false;
    U.cap_action = a;
    U.cap_col = col;
    U.binding_capture.reset();
    U.cap_started = now_s();
    U.cap_note.clear();
    g_capture_key = -2;
    g_capturing_keys = true;
}

void clear_binding(int a, int col) {
    input_map::Mapping m = input_map::current();
    if (col == kColPad) input_map::set_pad_binding(m, a, input_map::kPadNone);
    else m.keys[a][col] = input_map::kNoKey;
    input_map::set_current(m);
}

void apply_capture() {
    // Shortcuts owns its encoded action IDs and must receive the pending input itself.
    if (U.cap_action < 0 || U.cap_action >= input_map::kActionCount) return;
    using namespace input_map;
    input_map::Mapping m = input_map::current();
    const int a = U.cap_action, colm = U.cap_col;
    int code = g_capture_key.exchange(-2);
    bool done = false;
    if (code >= 0 && colm != kColPad) {
        if (const char* r = input_map::reserved_key(code)) U.cap_note = std::string("That key is used for ") + r;
        else {
            m.keys[a][colm == kColAny ? 0 : colm] = code;
            input_map::set_current(m);
            done = true;
        }
    }
    if (!done) {
        // controller: wait for all inputs to be released, then take the first one pressed (controller
        // slots); for a key slot, B cancels
        const int pressed = U.binding_capture.poll(U.values);
        if (pressed > 0) {
            if (colm == kColPad || colm == kColAny) {
                set_pad_binding(m, a, pressed);
                input_map::set_current(m);
            }
            done = true;  // (key slots: any controller input cancels)
            g_pad_b_used = true;  // issue #111: the input just assigned (B too) does not also close the menu this frame
        }
    }
    if (!done && now_s() - U.cap_started > 8) done = true;  // nothing pressed: give up
    if (done) {
        U.capture_completed = true;
        U.cap_action = -1;
        g_capturing_keys = false;
    }
}

std::string summary(const input_map::Mapping& m, int a) {
    std::string out;
    for (int k : m.keys[a])
        if (k != input_map::kNoKey) out += (out.empty() ? "" : "  -  ") + input_map::key_label(k);
    if (m.pad[a] != input_map::kPadNone) out += (out.empty() ? "controller " : "  -  controller ") + std::string(input_map::pad_label(m.pad[a]));
    return out.empty() ? "not bound" : out;
}

// the table (List view)
void controls_list(input_map::Mapping& m, float h) {
    if (ImGui::BeginTable("map", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame,
                          ImVec2(0, h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Wii U input", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Key 2");
        ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        for (int a = 0; a < input_map::kScreenshot; a++) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool conflict = input_map::has_conflict(m, a);
            if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.20f, 1.0f));
            ImGui::TextUnformatted(input_map::action_label(a));
            if (conflict) ImGui::PopStyleColor();
            for (int col = 0; col < 3; col++) {
                ImGui::TableNextColumn();
                std::string label;
                if (U.cap_action == a && (U.cap_col == col || (U.cap_col == kColAny && col != kColKey1))) label = "...";
                else if (col < 2) label = m.keys[a][col] == input_map::kNoKey ? "-" : input_map::key_label(m.keys[a][col]);
                else label = m.pad[a] == input_map::kPadNone ? "-" : input_map::pad_label(m.pad[a]);
                label += "##c" + std::to_string(col);
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0)) && U.cap_action < 0) start_capture(a, col);
                if (binding_clear_requested(U.cap_action >= 0 || U.capture_completed))
                    clear_binding(a, col);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------- gyro (motion/motion.h)
void save_gyro(const motion::Settings& g) {
    hostui::post([g] {
        motion::set_settings(g);
        for (const char* k : motion::kKeys) hostui::set(k, motion::value_of(g, k));
    });
}
void load_gyro() {
    std::vector<std::pair<std::string, std::string>> saved;
    std::string v;
    for (const char* k : motion::kKeys)
        if (hostui::get(k, v)) saved.emplace_back(k, v);
    motion::set_settings(motion::from_saved(saved));
}
// the Gyro dialog (Controls tab > Gyro..., gyro_popup.h): what it changes is saved at once
void gyro_window(bool& open) {
    motion::Settings g = motion::settings(), before = g;
    const bool back = navigation_pressed(input_map::face_input(input_map::kB));
    if (gyro::dialog(open, g, back)) g_pad_b_used = true;  // B closed the dialog: the menu stays open
    open = false;
    if (!(g == before)) save_gyro(g);
}

void tab_controls() {
    bool v;
    // a grey note after a row's choices: on the same line when it fits (flow_layout.h)
    auto disabled_after = [](const std::string& text) {
        flow::next(flow::text(text.c_str()));
        ImGui::TextDisabled("%s", text.c_str());
    };
    input_map::Mapping m = input_map::current();
    // top: which controller the keyboard and controllers act as (the drawing follows), view switch
    const bool pro = input::pro_controller();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keyboard and controllers act as");
    flow::next(flow::check("Wii U GamePad"));
    // the choice is saved (issue #26: every start went back to the GamePad and showed its screen)
    if (radio("Wii U GamePad", !pro))
        hostui::post([] { hostui::set_pro_controller(false); hostui::set("proController", "0"); });
    flow::next(flow::check("Wii U Pro Controller"));
    if (radio("Wii U Pro Controller", pro))
        hostui::post([] { hostui::set_pro_controller(true); hostui::set("proController", "1"); });
    // List view at the right edge, or on the next line when the row leaves no room (phones)
    flow::next(flow::check("List view"));
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - flow::check("List view")));
    ImGui::Checkbox("List view", &U.list_view);
    help("A plain table of all inputs instead of the controller drawing");
    if (check("File select Quit button", m.file_select_quit, &v)) {
        m.file_select_quit = v;
        input_map::set_current(m);
    }
    help("Show a controller and mouse Quit button on the file selection screen.");
    if (check("Skip controller selection", m.skip_controller_selection, &v)) {
        m.skip_controller_selection = v;
        input_map::set_current(m);
    }
    help("At the title screen, automatically choose the configured Wii U controller.");
#ifndef __ANDROID__
    // issue #267 (not on Android: the app is in front or paused there). Saved with the controls; the host
    // applies it at once (input_sdl.cpp, gfx/input.mm); beside Skip, or below it in a narrow window
    flow::next(flow::check("Controller input when the game isn't focused"), 24);
    if (check("Controller input when the game isn't focused", m.background_controllers, &v)) {
        m.background_controllers = v;
        input_map::set_current(m);
    }
    help("For a second screen, streaming or remote play. Keyboard and mouse still only work while the "
         "game is focused.");
#endif
    // face-button preset (issue #78): which host face buttons drive A/B/X/Y
    const input_map::FaceLayout fl = input_map::face_layout(m);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Face buttons");
    flow::next(flow::check(input_map::face_layout_label(input_map::FaceLayout::kPosition)));
    if (radio(input_map::face_layout_label(input_map::FaceLayout::kPosition),
              !m.face_auto && fl == input_map::FaceLayout::kPosition)) {
        input_map::apply_face_layout(m, input_map::FaceLayout::kPosition);
        input_map::set_current(m);
    }
    flow::next(flow::check(input_map::face_layout_label(input_map::FaceLayout::kLabels)));
    if (radio(input_map::face_layout_label(input_map::FaceLayout::kLabels),
              !m.face_auto && fl == input_map::FaceLayout::kLabels)) {
        input_map::apply_face_layout(m, input_map::FaceLayout::kLabels);
        input_map::set_current(m);
    }
    flow::next(flow::check("automatic"));
    if (radio("automatic", m.face_auto)) {
        input_map::set_face_auto(m, true);
        input_map::set_current(m);
    }
    help("Which physical button does each Wii U action. By position: right is A, bottom B, "
         "top X and left Y. By label: A does A, B does B, X does X and Y does Y. Automatic "
         "reads the connected pad's labels. Button prompts below chooses how those buttons "
         "look in the game. Only the four face bindings change.");
    if (m.face_auto) {
        disabled_after(std::string("(-> ") + input_map::face_layout_label(fl) + ")");
    } else if (fl == input_map::FaceLayout::kCustom) {
        disabled_after("(custom)");
    }
    // in-game button prompts (#110, #143): what the game's own button icons look like
    {
        using P = input_map::Mapping::Prompts;
        using N = input_map::Mapping::ShoulderNames;
        const input_map::PromptStyle look = input_map::prompt_style(m, input_map::pad_family());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Button prompts");
        ImGui::PushID("prompts");  // "automatic" is also a face-buttons choice
        for (P p : {P::kAuto, P::kOriginal, P::kXbox, P::kPlayStation}) {
            flow::next(flow::check(input_map::prompts_label(p)));
            if (radio(input_map::prompts_label(p), m.prompts == p)) {
                m.prompts = p;
                input_map::set_current(m);
            }
        }
        ImGui::PopID();
        help("How the game's button icons look, with any face-button setting. Xbox and "
             "PlayStation show the button bound to each Wii U action. By position on Xbox, "
             "roll shows B and sword shows A. Automatic follows the connected pad; Nintendo "
             "keeps the game's look. By label keeps its existing automatic Xbox fallback. "
             "Original keeps the game's look. Custom bindings need four distinct face buttons; "
             "otherwise the original icons are used.");
        disabled_after(std::string("(-> ") + (look.face == input_map::FaceStyle::kPlayStation ? "PlayStation"
                                       : look.face == input_map::FaceStyle::kXbox      ? "Xbox"
                                                                                       : "original") + ")");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Shoulder names");
        ImGui::PushID("shoulders");
        for (N n : {N::kAuto, N::kNintendo, N::kNumbered, N::kXbox}) {
            flow::next(flow::check(input_map::shoulder_names_label(n)));
            if (radio(input_map::shoulder_names_label(n), m.shoulder_names == n)) {
                m.shoulder_names = n;
                input_map::set_current(m);
            }
        }
        ImGui::PopID();
        help("How prompts name the shoulder buttons and triggers. Automatic follows the button "
             "prompts: LB / RB / LT / RT with the Xbox look, L1 / R1 / L2 / R2 with the PlayStation "
             "shapes, the game's L / R / ZL / ZR otherwise. A fixed choice applies with any face "
             "buttons setting.");
        disabled_after(std::string("(-> ") + (look.shoulders == input_map::ShoulderStyle::kXbox       ? "LB / RB / LT / RT"
                                       : look.shoulders == input_map::ShoulderStyle::kNumbered ? "L1 / R1 / L2 / R2"
                                                                                               : "L / R / ZL / ZR") + ")");
        bool colours = m.prompt_colours;
        if (ImGui::Checkbox("Coloured face buttons", &colours)) {
            m.prompt_colours = colours;
            input_map::set_current(m);
        }
        help("Green A, red B, blue X and yellow Y in the Xbox look; blue cross, red circle, pink "
             "square and green triangle with the PlayStation shapes. Off: the game's white buttons "
             "(in the Xbox positions while the face buttons are by label).");
    }
    // status line: capture prompt > note > hovered input > duplicates > help
    std::string status;
    ImVec4 sc(0.70f, 0.78f, 0.84f, 1.0f);
    if (U.cap_action >= 0) {
        sc = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
        status = std::string(input_map::action_label(U.cap_action)) + ": " +
                 (U.cap_col == kColPad   ? "press a controller button or stick direction (the next input is recorded)"
                  : U.cap_col == kColAny ? "press a key, mouse button or controller button (the next input is recorded)"
                                         : "press a key or mouse button (the next input is recorded)");
    } else if (!U.cap_note.empty()) {
        status = U.cap_note;
    } else if (U.hover_action >= 0) {
        status = std::string(input_map::action_label(U.hover_action)) + "  -  " + summary(m, U.hover_action) +
                 ".  A / Enter / click: rebind; X / Delete / right-click: clear.";
    } else if (int n = input_map::conflict_count(m)) {
        sc = ImVec4(1.0f, 0.62f, 0.20f, 1.0f);
        status = std::to_string(n) + " input(s) are bound to more than one action (orange).";
    } else {
        status = "Select a button on the controller or a chip to rebind it. Changes are saved to controls.json.";
    }
    // status line and the options below (as tall as they came out last frame: more lines when they wrap)
    const float bottom = ImGui::GetTextLineHeightWithSpacing() + std::max(U.options_height, ImGui::GetFrameHeightWithSpacing() * 2) + 4;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float h = std::max(160.0f, avail.y - bottom);
    U.hover_action = -1;
    if (U.list_view) {
        controls_list(m, h);
    } else {
        ControlsView cv;
        cv.m = m;
        cv.pro = pro;
        cv.cap_action = U.cap_action;
        cv.cap_col = U.cap_col;
        static float live[input_map::kPadCount];
        std::copy(std::begin(U.values), std::end(U.values), live);
        test_pad(live);
        cv.pad = live;
        cv.t = now_s();
        // live display: pressed inputs and stick deflection through the current mapping, from the raw
        // host state (as the Controls window's poll; camera inversion left out)
        static bool keys[256];
        for (int i = 0; i < 256; i++) keys[i] = g_held_keys[i].load(std::memory_order_relaxed);
        cv.keys = keys;
        for (int a = 0; a < input_map::kScreenshot; a++) {
            float x = 0;
            for (int key : m.keys[a])
                if (key != input_map::kNoKey && keys[key]) x = 1;
            if (int p = m.pad[a]; p != input_map::kPadNone) x = std::max(x, live[p]);
            cv.act[a] = x;
        }
        input::PadState ks = input_map::keyboard_state(m, keys), cs = input_map::controller_state(m, live);
        const bool kl = ks.lx || ks.ly, kr = ks.rx || ks.ry;
        cv.stick[0][0] = kl ? ks.lx : cs.lx;
        cv.stick[0][1] = kl ? ks.ly : cs.ly;
        cv.stick[1][0] = kr ? ks.rx : cs.rx;
        cv.stick[1][1] = kr ? ks.ry : cs.ry;
        draw_controls(cv, avail.x, h);
        U.hover_action = cv.hover_action;
        if (cv.start_action >= 0) start_capture(cv.start_action, cv.start_col);
        if (cv.clear_action >= 0) clear_binding(cv.clear_action, cv.clear_col);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, sc);
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopStyleColor();
    // the options below (controls_options.h): they wrap in a narrow window (phones)
    const float optionsTop = ImGui::GetCursorPosY();
    controls_options::Values o;
    o.deadzone = m.deadzone;
    o.invert_camera_y = m.invert_camera_y;
    o.rumble = rumble::enabled();
    o.rumble_available = input::has_rumble();
#ifdef __ANDROID__
    o.phone_vibration = phone_rumble::choice.load();
#endif
    static bool gyro_open = false;
    const motion::Settings gs = motion::settings();
    const std::string gyro_label = std::string("Gyro: ") + (gs.source == motion::kOff ? "off" : motion::source_label(gs.source)) + "...";
    o.gyro_label = gyro_label.c_str();
    o.range[0] = m.left_range;
    o.range[1] = m.right_range;
    o.range_min = input_map::kMinStickRange;
    o.range_max = input_map::kMaxStickRange;
    const unsigned changed = controls_options::draw(o);
    U.options_height = ImGui::GetCursorPosY() - optionsTop;
    if (changed & (controls_options::kDeadzone | controls_options::kInvert | controls_options::kRangeLeft |
                   controls_options::kRangeRight)) {
        m.deadzone = o.deadzone;
        m.invert_camera_y = o.invert_camera_y;
        m.left_range = o.range[0];
        m.right_range = o.range[1];
        input_map::set_current(m);
    }
    if (changed & controls_options::kRumble) {
        const bool on = o.rumble;
        rumble::set_enabled(on);
        hostui::post([on] { hostui::set("rumble", on ? "1" : "0"); });
    }
#ifdef __ANDROID__
    if (changed & controls_options::kPhone) {
        const int phone = o.phone_vibration;
        phone_rumble::choice = static_cast<phone_rumble::Mode>(phone);
        hostui::post([phone] { hostui::set("phoneRumble", std::to_string(phone)); });
    }
#endif
    if (changed & controls_options::kGyro) gyro_open = true;
    gyro_window(gyro_open);
    if (changed & controls_options::kReset) input_map::set_current(input_map::Mapping::defaults());
}

int saved_language() {
    std::string v;
    if (hostui::get("language", v) && !v.empty()) {
        int l = atoi(v.c_str());
        if (l >= 0 && l < game_lang::kLanguages) return l;
    }
    return 1;
}

// the region of the saved language when it comes from a language source (game_lang::kNoRegion: the game's own)
int saved_language_region() {
    std::string v;
    if (hostui::get("language_region", v)) {
        const int r = game_lang::region_from_code(v);
        if (r != game_lang::kUsa) return r;
    }
    return game_lang::kNoRegion;
}

// "English, French and Spanish"
std::string language_list(const std::vector<int>& langs) {
    std::string s;
    for (size_t k = 0; k < langs.size(); k++)
        s += std::string(k == 0 ? "" : k + 1 == langs.size() ? " and " : ", ") + game_lang::name(langs[k]);
    return s;
}

void choose_language(int language, int region) {
    U.language = language;
    U.language_region = region;
    hostui::post([language, region] {
        hostui::set("language", std::to_string(language));
        hostui::set("language_region", game_lang::region_code(region));
    });
}

void tab_about() {
    const char* env = getenv("WWHD_LANGUAGE");
    const bool env_set = env && *env;
    char* end = nullptr;
    const long env_value = env_set ? strtol(env, &end, 10) : 1;
    const int env_language = env_set && !*end && env_value >= 0 && env_value < game_lang::kLanguages ? (int)env_value : 1;
    if (U.language < 0) {
        U.language = saved_language();
        U.language_region = saved_language_region();
        const game_lang::Start now = game_lang::current();
        U.language_at_start = now.language;  // the game reads it early in the boot
        U.language_region_at_start = now.pack ? now.region : game_lang::kNoRegion;
        if (U.language_at_start < 0) {
            const game_lang::Start s = game_lang::choose(env_set ? env_language : U.language,
                                                         env_set ? game_lang::kNoRegion : U.language_region);
            U.language_at_start = s.language;
            U.language_region_at_start = s.pack ? s.region : game_lang::kNoRegion;
        }
    }
    const std::vector<int>& avail = game_lang::available();
    heading("Console language (applies on the next start)");
    if (avail.empty())
        note("The game picks its text language from the console language. Its language packs (content/Common/Pack) "
             "were not found, so every language is offered; the game shows only those its disc carries.");
    else
        note("The game picks its text language from the console language. This game (%s) contains %s; the other "
             "languages need a disc that carries them.", game_lang::region().c_str(), language_list(avail).c_str());
    ImGui::BeginDisabled(env_set);
    for (int i : {1, 2, 5, 3, 4, 8, 9, 10, 7, 0, 6, 11}) {
        if (i != 1 && i != 3 && i != 9 && i != 0) flow::next(flow::check(game_lang::name(i)));  // rows of three
        if (radio(game_lang::name(i), U.language == i && U.language_region == game_lang::kNoRegion,
                  game_lang::is_available(i)))
            choose_language(i, game_lang::kNoRegion);
    }
    // the languages of the language sources (a European or Japanese disc of the player's, set up with
    // the setup's --language-source): docs/language-packs.md
    for (int region : {(int)game_lang::kEurope, (int)game_lang::kJapan}) {
        const std::vector<int> langs = game_lang::source_languages(region);
        if (langs.empty()) continue;
        heading(region == game_lang::kEurope ? "From your European game (language source, experimental)"
                                             : "From your Japanese game (language source, experimental)");
        for (size_t k = 0; k < langs.size(); k++) {
            if (k % 3) ImGui::SameLine();
            const std::string label = std::string(game_lang::name(langs[k])) + " (" + game_lang::region_name(region) + ")";
            if (radio(label.c_str(), U.language == langs[k] && U.language_region == region))
                choose_language(langs[k], region);
        }
    }
    if (!game_lang::source_languages(game_lang::kJapan).empty())
        note("Japanese language sources are untested so far: report what looks wrong.");
    ImGui::EndDisabled();
    const bool from_source = U.language_region != game_lang::kNoRegion &&
                             game_lang::source_pack(U.language, U.language_region) != nullptr;
    if (env_set) {
        note("WWHD_LANGUAGE=%s is set for this start and takes precedence.", env);
        if (!game_lang::is_available(env_language))
            warn("%s is not on this disc: the game runs in %s.", game_lang::name(env_language),
                 game_lang::name(game_lang::usable(env_language)));
    } else if (U.language_region != game_lang::kNoRegion && !from_source) {
        warn("%s (%s) is saved but its language source is missing (%s): the game runs in %s.",
             game_lang::name(U.language), game_lang::region_name(U.language_region),
             game_lang::sources_dir().c_str(), game_lang::name(game_lang::usable(U.language)));
    } else if (!from_source && !game_lang::is_available(U.language)) {
        warn("%s is saved but is not on this disc: the game runs in %s. Choose one of the languages above.",
             game_lang::name(U.language), game_lang::name(game_lang::usable(U.language)));
    } else if (U.language != U.language_at_start || (from_source ? U.language_region : 0) != U.language_region_at_start) {
        warn("%s is saved. Restart the game to apply it: the game reads the console language only when it starts.",
             game_lang::name(U.language));
    }
    heading("About");
    ImGui::Text("The Legend of Zelda: The Wind Waker HD - native port (%s host, %s renderer)", hostui::name(),
                render::api_name(render::active()));
    if (*build::variant())  // the decomp build (docs/decomp-build.md)
        ImGui::Text("%s (%s), %s build: the game code runs from the decompiled source (experimental)", build::version(),
                    build::commit(), build::variant());
    if (!ext::features().keyboard) {  // a controller only
        note("Settings overlay: hold Select / press Home on a controller. B closes. L / R switch tabs. The game keeps "
             "running and sees no input while this menu is open.");
        note("When the game asks for text (your name), a text window appears over the game: D-pad / left stick choose a "
             "key, A types it, B deletes, X adds a space, Y is Shift, L / R switch between letters, accents and symbols, "
             "Start confirms.");
    } else {
#ifdef __APPLE__
        note("Settings overlay: Esc or F1 (Fn+F1 on most Mac keyboards), Cmd+, or Settings... in the app menu, or hold Select / "
             "press Home on a controller. With a captured mouse, the first Esc frees the mouse. Esc, F1 or B closes. "
#else
        note("Settings overlay: Esc or F1, or hold Select / press Home on a controller. With a captured mouse, the first "
             "Esc frees the mouse. Esc, F1 or B closes. "
#endif
             "L / R switch tabs on a controller. The game keeps running and sees no input while this menu is open.");
        note("When the game asks for text (your name), a text window appears over the game: type on the keyboard "
             "(Enter = OK, Esc = Cancel), click the on-screen keys, or use a controller: D-pad / left stick choose a key, "
             "A types it, B deletes, X adds a space, Y is Shift, L / R switch between letters, accents and symbols, "
             "Start confirms.");
    }
    note("Built with Dear ImGui %s (MIT License, Omar Cornut and contributors).", IMGUI_VERSION);
    note("The world editor's handles use ImGuizmo (MIT License, Cedric Guillemet).");
}

void perf_window(bool menu_open) {
    const double t = now_s();
    if (U.fps_t0 == 0) { U.fps_t0 = t; U.fps_n0 = gx2::flips_presented(); }
    if (t - U.fps_t0 >= 0.5) {
        uint64_t n = gx2::flips_presented();
        U.fps = (double)(n - U.fps_n0) / (t - U.fps_t0);
        U.fps_t0 = t;
        U.fps_n0 = n;
    }
    float worst = 0, sum = 0;
    for (float f : U.frame_ms) { worst = std::max(worst, f); sum += f; }
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##perf", nullptr, fl)) {
        ImGui::Text("%.0f fps   Render interval %.1f ms (worst %.1f)", U.fps, sum / 120.0f, worst);
        ImGui::Text("Average %.1f fps   %.1f logic steps/s", g_average.fps, g_average.logic);
        {
            // slow motion: frame interpolation without Keep game speed below its frame target
            // Shown from the second 2-second window in a row below 29 logic steps/s (the game's rate is
            // 29.97; a single hitch or load does not show it), hidden again from a window at 29.5: a game
            // a few percent slow is noticeable over time, and the gap keeps it from flickering.
            static double t0 = 0; static uint64_t s0 = 0; static double rate = 30;
            static int slowWindows = 0;
            static bool slow = false;
            if (t0 == 0 || t - t0 < 0) { t0 = t; s0 = interp::executed_steps(); }
            else if (t - t0 >= 2.0) {
                rate = (double)(interp::executed_steps() - s0) / (t - t0); t0 = t; s0 = interp::executed_steps();
                slowWindows = rate > 1 && rate < 29.0 ? slowWindows + 1 : 0;
                slow = slow ? rate > 1 && rate < 29.5 : slowWindows >= 2;
            }
            if (slow && interp::mode() == 1 && !interp::paced_interpolation())
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Slow motion: %.1f of 30 logic steps/s. Turn on Keep game speed", rate);
        }
#ifdef __ANDROID__
        ImGui::TextUnformatted(android_telemetry().c_str());
#endif
        ImGui::Separator();
        ImGui::TextUnformatted(performance_device().c_str());
        ImGui::TextUnformatted(perf::summary().c_str());
        ImGui::PlotLines("##ft", U.frame_ms, 120, U.frame_i, nullptr, 0.0f, 50.0f, ImVec2(220, 36));
        ImGui::TextDisabled("%s  %gx  %s", render::api_name(render::active()), hostui::res_scale(), interp::mode_name());
        if (dynres::on()) ImGui::TextDisabled("%s", dynres::status().c_str());
        if (float share = interp::paced_drawn_share(); share >= 0)
            ImGui::TextDisabled("in-between frames drawn: %.0f%%", share * 100.0f);
    }
    ImGui::End();
    (void)menu_open;
}

void tab_audio() {
    float volume = audio::master_volume() * 100.0f;
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Master volume", &volume, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
        hostui::post([volume] { audio::set_master_volume(volume / 100.0f, false); });  // live while dragging
    if (ImGui::IsItemDeactivatedAfterEdit())
        hostui::post([volume] { audio::set_master_volume(volume / 100.0f); });  // saved once on release
    ImGui::SameLine();
    if (ImGui::Button("Mute##volume")) hostui::post([] { audio::set_master_volume(0.0f); });
    ImGui::SameLine();
    if (ImGui::Button("Default##volume")) hostui::post([] { audio::set_master_volume(1.0f); });
    help("Master volume for the game's sound and mod audio. Applies immediately and is remembered for the next start.");
    if (audio::master_volume_from_env())
        note("WWHD_AUDIO_VOLUME=%s is set for this start and takes precedence; changes here are not saved.",
             getenv("WWHD_AUDIO_VOLUME"));
    bool pad_sounds = audio::gamepad_sounds();
    if (ImGui::Checkbox("GamePad sounds through main speakers", &pad_sounds))
        hostui::post([pad_sounds] { audio::set_gamepad_sounds(pad_sounds); });
    float pad_volume = audio::gamepad_sounds_volume() * 100.0f;
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("GamePad sounds volume", &pad_volume, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
        hostui::post([pad_volume] { audio::set_gamepad_sounds_volume(pad_volume / 100.0f, false); });
    if (ImGui::IsItemDeactivatedAfterEdit())
        hostui::post([pad_volume] { audio::set_gamepad_sounds_volume(pad_volume / 100.0f); });
    help("Play sounds from the GamePad speaker through your main speakers during TV play. Applies immediately and is remembered. Master volume also applies.");
    if (audio::gamepad_sounds_from_env() || audio::gamepad_sounds_volume_from_env())
        note("GamePad audio test overrides are set for this start; overridden preferences are not saved.");
    if (ext::features().speaker_choice) {
        int speakers = audio::requested_surround() ? 1 : 0;
        if (combo("Speakers", &speakers, "Stereo\0Surround 5.1\0"))
            hostui::post([speakers] { audio::set_requested_surround(speakers == 1); });
        note("Restart the game after changing Speakers.");
    }
    if (audio::surround_fallback())
        note("Surround 5.1 unavailable on this output. Using stereo; restart after connecting a 5.1 device.");
    if (audio::output_device_changed())
        note("Output device changed. Audio is muted; restart the game to use the current device.");
    note("Current output: %s", audio::channels() == 6 ? "Surround 5.1" : "Stereo");
    if (const char *ch = audio::speaker_test_channel()) {
        note("Speaker test: %s", ch);
        if (ImGui::Button("Stop speaker test"))
            audio::stop_speaker_test();
    } else if (ImGui::Button("Test speakers"))
        audio::start_speaker_test();
    note("Plays each active channel in turn at a low level. Subwoofer uses an 80 Hz tone.");
}

// The bottom row of the Settings window (every tab): Quit Game, and on Android Close Settings before it.
// Quit Game asks first (quit_dialog.h); Quit then ends the game through the host (hostui::quit).
bool g_test_quit = false;  // WWHD_TEST_OVERLAY=quit: choose Quit Game once the menu is up
void quit_row(bool& open) {
#ifdef __ANDROID__
    bool close = false;
    bool ask = quit::bottom_row(true, &close);
    if (close) open = false;
#else
    (void)open;
    bool ask = quit::bottom_row(false, nullptr);
#endif
    if (ask) LOG("[quit] Settings > Quit Game: asking");
    if (g_test_quit && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        g_test_quit = false;
        ask = true;
        LOG("[quit] test: Settings > Quit Game chosen");
    }
    // test runs: WWHD_TEST_QUIT_ANSWER=quit|cancel answers 1.5 s after the question shows (as on macOS)
    static const quitprompt::Answer test = quitprompt::test_answer([](const char* n) -> const char* { return getenv(n); });
    static double asked = -1;
    if (ask) asked = ImGui::GetTime();
    quit::Answer forced = quit::Answer::None;
    if (asked >= 0 && ImGui::GetTime() - asked >= 1.5) {
        if (test == quitprompt::Answer::Quit) forced = quit::Answer::Quit;
        if (test == quitprompt::Answer::Cancel) forced = quit::Answer::Cancel;
    }
    const bool back = navigation_pressed(input_map::face_input(input_map::kB));
    const auto answer = quit::dialog(ask, ss::in_gameplay(), back, forced);
    if (answer == quit::Answer::None) return;
    asked = -1;
    if (answer == quit::Answer::Cancel) {
        if (back) g_pad_b_used = true;  // B answered: the menu stays open
        LOG("[quit] answer: Cancel; back to Settings");
        return;
    }
    LOG("[quit] answer: Quit");
    // posted: the settings changes posted before (each one saved as it is made) are done first
    hostui::post([] { hostui::quit(); });
}

void settings_window() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
    // the Controls drawing wants room: that tab gets (almost) the whole picture
#ifdef __ANDROID__
    const ImVec2 size(ds.x * 0.98f, ds.y * 0.97f);
#else
    const ImVec2 size = shown_builtin() == kControls ? ImVec2(std::min(1240.0f, ds.x * 0.98f), std::min(720.0f, ds.y * 0.97f))
                                           : ImVec2(std::min(860.0f, ds.x * 0.94f), std::min(620.0f, ds.y * 0.92f));
#endif
    ImVec2 panelSize = size, panelPos(ds.x * 0.5f, ds.y * 0.5f);
    bool placement = shown_builtin() == kDisplay &&
                     (gfx::g_mode == gfx::kDrcPip || gfx::g_mode == gfx::kDrcAuto || gfx::g_mode == gfx::kDrcOff);
#ifdef __ANDROID__
    placement = placement && !gfx::g_has_drc_window;
#endif
    // Moving the panel out of the preview's way: never while dragging (the panel would jump under the
    // pointer), and only to a space that keeps it usable; otherwise it stays centred over the preview.
    static ImVec2 lastPanelSize, lastPanelPos;
    static bool lastPlaced = false;
    if (placement && lastPlaced && ImGui::IsAnyItemActive()) {
        panelSize = lastPanelSize, panelPos = lastPanelPos;
    } else if (placement) {
        const auto b = gfx::pip_box(pip_tv_box(), gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
        const float gap = 8;
        const gfx::Box spaces[] = {
            {0, 0, std::max(0.0f, b.x - gap), ds.y},
            {b.x + b.w + gap, 0, std::max(0.0f, ds.x - b.x - b.w - gap), ds.y},
            {0, 0, ds.x, std::max(0.0f, b.y - gap)},
            {0, b.y + b.h + gap, ds.x, std::max(0.0f, ds.y - b.y - b.h - gap)}
        };
        auto usable = [&](gfx::Box area) { return std::min(area.w, size.x) * std::min(area.h, size.y); };
        auto best = spaces[0];
        for (auto area : spaces)
            if (usable(area) > usable(best)) best = area;
        const ImVec2 fitted(std::min(size.x, best.w), std::min(size.y, best.h));
        if (fitted.x >= size.x * 0.75f && fitted.y >= size.y * 0.6f) {
            panelSize = fitted;
            panelPos = ImVec2(best.x + best.w / 2, best.y + best.h / 2);
        }
    }
    lastPlaced = placement, lastPanelSize = panelSize, lastPanelPos = panelPos;
    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;  // docking: the editor's panels only
    bool open = true;
    if (U.just_opened) {
        ImGui::SetNextWindowFocus();  // keyboard / controller navigation starts in the menu
        U.just_opened = false;
    }
    if (ImGui::Begin("Wind Waker HD  -  Settings", &open, fl)) {
        const std::vector<ext::ShownTab>& tabs = ext::tabs();  // built-in and added (extensions.h)
        // L / R on a controller switch tabs
        if (U.cap_action < 0 && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            // in Mods (or an added tab with sub-tabs), L / R move between its areas first
            auto areas = [](int builtin, int direction) { return builtin == kMods && modsui::shoulder(direction); };
            for (int direction : {-1, +1})
                if (navigation_pressed(direction < 0 ? input_map::kPadLB : input_map::kPadRB))
                    if (const int t = ext::shoulder(U.tab, direction, areas); t >= 0) U.select_tab = t;
        }
        if(!setup_run.rebuilding)code_mod_dialog();
        restart_notice_frame(true);
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            for (int i = 0; i < int(tabs.size()); i++) {
                if (!tabs[i].visible()) continue;  // a tab only some launches have (the world editor's, with --editor)
                ImGuiTabItemFlags f = U.select_tab == i ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tabs[i].name.c_str(), nullptr, f)) {
                    if (U.tab != i) U.cap_action = -1, g_capturing_keys = false, U.cap_note.clear();
                    U.tab = i;
                    // the page ends one row above the bottom of the window: the bottom row (quit_row)
                    ImGui::BeginChild("page", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_NavFlattened);
                    switch (tabs[i].builtin) {
                    case kSaves: tab_saves(); break;
                    case kGraphics: tab_graphics(); break;
                    case kDisplay: tab_display(); break;
                    case kAudio: tab_audio(); break;
                    case kMods: tab_mods(); break;
                    case kControls: tab_controls(); break;
                    case kShortcuts: tab_shortcuts(); break;
                    case kAbout: tab_about(); break;
                    default: if (tabs[i].draw) tabs[i].draw(); break;  // an added tab
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            U.select_tab = -1;
            ImGui::EndTabBar();
        }
        quit_row(open);
    }
    ImGui::End();
    // B (not while choosing an input or in a list) or the close button closes the menu
    if (!open) set_open(false, OpenReason::CloseButton);
    if (U.cap_action < 0 && navigation_pressed(input_map::face_input(input_map::kB)) && !g_pad_b_used && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        set_open(false, OpenReason::B);
    g_pad_b_used = false;
}

}  // namespace

// ---------------------------------------------------------------- API
bool is_open() { return g_open.load(std::memory_order_relaxed); }
static std::atomic<bool> g_shortcut_took{false};  // a platform shortcut took a poll's input (shortcut_took_input)
void activate_shortcut(int action, KeyOrigin origin) {
    using namespace input_map;
    if (action < 0 || action >= kShortcutCount) return;
    if (action == kSettings) { set_open(!is_open(), OpenReason::F1, origin); return; }
    LOG("[shortcuts] %s", shortcut_info(action).id);
    hostui::post([action] {
        if (action >= kSave1 && action <= kSave5) { ss::request_save(action - kSave1 + 1); return; }
        if (action >= kLoad1 && action <= kLoad5) { ss::request_load(action - kLoad1 + 1); return; }
        switch (action) {
        case kFullscreen: hostui::set_fullscreen(!hostui::fullscreen()); break;
        case kPhoto: screenshot::request(); break;
        case kResolution: {
            const float scales[] = {1, 1.5f, 2, 3};
            float next = scales[0];
            for (float scale : scales) if (scale > hostui::res_scale() + 0.01f) { next = scale; break; }
            hostui::set_res_scale(next); break;
        }
        case kAO: if (render::feature_available(render::kFeatureAO)) render::set_ao_mode((render::ao_mode() + 1) % 3); break;
        case kAOHires: if (render::feature_available(render::kFeatureAOHires)) render::set_ao_hires(!render::ao_hires()); break;
        case kAniso: if (render::feature_available(render::kFeatureAniso)) render::set_aniso(!render::aniso()); break;
        case kInterpolation: interp::toggle_fps(60); break;
        case kTrue60: interp::set_mode(interp::mode() == 2 ? 0 : 2); break;
        case kFXAA: if (render::feature_available(render::kFeatureFXAA)) render::set_fxaa(!render::fxaa()); break;
        case kPiP:
            if (hostui::drc_mode() != gfx::kDrcPip) { hostui::set_drc_mode(gfx::kDrcPip); hostui::show_drc(true); }
            else hostui::show_drc(!hostui::drc_shown());
            break;
        case kPerformance: set_perf_shown(!perf_shown()); break;
        case kMute: {
            static float before = 1;
            float v = audio::master_volume(); if (v > 0) before = v;
            audio::set_master_volume(v > 0 ? 0 : before); break;
        }
        case kVolumeDown: audio::set_master_volume(std::max(0.0f, audio::master_volume() - 0.05f)); break;
        case kVolumeUp: audio::set_master_volume(std::min(1.0f, audio::master_volume() + 0.05f)); break;
        default: break;
        }
        hostui::graphics_changed();
    });
}
void external_binding_capture(bool active) {
    if (active) { g_external_capture = true; g_external_capture_finished = false; }
    else if (g_external_capture.load()) {
        g_external_capture_finished = true; // publish before releasing the active capture flag
        g_external_capture = false;
    }
}
void poll_controller_shortcuts(const float* values) {
    if (g_no_host && !getenv("WWHD_TEST_PAD")) return;
    PadValues pad;
    std::copy(values, values + input_map::kPadCount, pad.begin());
    static ReleaseGate capture_gate;
    capture_gate.filter(pad.data(), g_capturing_keys.load() || g_external_capture.load() || g_external_capture_finished.exchange(false));
    // the platform's shortcuts (extensions.h) first: the inputs one takes stay from the settings shortcut
    // until released, and from the game (shortcut_took_input)
    if (!ext::shortcuts().empty() && ext::poll_shortcuts(pad, is_open())) g_shortcut_took = true;
    static input_map::ShortcutPoll bindings;
    const auto m = input_map::current();
    const PadValues available = pad;
    const auto pressed = bindings.poll(m, available.data(), pad.data());
    // issue #267: a background controller press never opens Settings (settings_shortcut.h)
    const bool may_open = controller_may_open_settings(input::window_focused(), input::background_controllers());
    auto activate = [&](int a) {
        if (a == input_map::kSettings) {
            const auto& b = m.shortcuts[a];
            if (!is_open() && !may_open) return;
            set_open(!is_open(), !b.hold && b.button == input_map::kPadHome ? OpenReason::Home : OpenReason::API);
        } else activate_shortcut(a);
    };
    if (!captures()) for (int a : pressed) activate(a);
    else for (int a : pressed) if (a == input_map::kSettings && !g_capturing_keys.load()) activate(a);
    static SettingsShortcut shortcut;
    bool selectModifier = false;
    for (const auto& b : m.shortcuts) if (b.hold == input_map::kPadOptions && b.button) selectModifier = true;
    const auto action = shortcut.poll(now_s(), !g_capturing_keys.load() && !selectModifier && pad[input_map::kPadOptions] > 0.5f,
                                     false, is_open()); // Home is an ordinary, remappable Settings binding.

    if (action == SettingsShortcut::Close || (action == SettingsShortcut::Open && may_open))
        set_open(action == SettingsShortcut::Open, shortcut.reason());
}
bool shortcut_took_input() { return g_shortcut_took.exchange(false); }

void set_open(bool open, OpenReason reason, KeyOrigin origin) {
#ifdef WWHD_SDL_HOST
    if (open && window_target::selected.load() == window_target::Window::None) return;
#endif
    if (g_open.exchange(open) == open) return;
    if (!open) g_dialog_open = false;
    for (auto& k : g_held_keys) k = false;
    input::release_keys();  // keys held now belong to the menu (opening) or are not stuck in the game (closing)
    if (open) {
        mods::fast_forward_reset();
        mods::lock_on_reset();
        mods::quick_use_reset();
        mods::mouse_release();
        U.just_opened = true;
        U.slots_time = -1;
    }
    const auto message = open_message(open, reason, origin, render::frame_count());
    LOG("%s", message.c_str());
}
// inputs still held when the overlay closes stay from the game in the hosts' input pumps (release_gate.h)
bool blocks_input() { return captures(); }
// A platform's input layer (extensions.h, the world editor): its mode while the menu is closed, and
// whether its overlay windows were under the pointer at the last frame.
std::atomic<bool> g_layer_mouse{false};
std::atomic<float> g_quit_pointer_x{-1}, g_quit_pointer_y{-1};
int layer_mode() {
    if (!ext::sealed() || is_open()) return 0;
    const auto& layer = ext::input_layer();
    return layer.mode ? layer.mode() : 0;
}
bool layer_takes_pointer() {
    const int m = layer_mode();
    return file_select_quit::pointer(g_quit_pointer_x.load(), g_quit_pointer_y.load()) || m == 1 || (m == 2 && g_layer_mouse.load());
}
bool pointer_layer_active() { return file_select_quit::visible() || layer_mode() != 0; }
bool captures() { return is_open() || text_entry::active(); }
bool dialog_open() { return is_open() && g_dialog_open.load(); }
bool alive() { return now_s() - g_last_frame.load(std::memory_order_relaxed) < 1.0; }
bool perf_shown() { return g_perf.load(std::memory_order_relaxed); }
void set_perf_shown(bool on) {
    g_perf = on;
    hostui::post([on] { hostui::set("perfOverlay", on ? "1" : "0"); });
}

void focus_lost() {
    for (auto& k : g_held_keys) k = false;
    if (captures()) push({Event::Focus});
}

bool key(int code, bool down, bool repeat, int mods, KeyOrigin origin) {
    if (g_no_host_keys) return false;
#ifdef __ANDROID__
    if (!getenv("WWHD_ANDROID_KEYBOARD") && !captures()) return false;
#endif
#ifdef __ANDROID__
    const int action = getenv("WWHD_ANDROID_KEYBOARD") ? input_map::shortcut_for_key(input_map::current(), code, mods) : -1;
#else
    const int action = input_map::shortcut_for_key(input_map::current(), code, mods);
#endif
    const bool escape = code == kVK_Escape;
    // Esc with a captured mouse (mouse camera) only frees the mouse, whatever it is bound to; Settings
    // opens on the next Esc. Both hosts route keys here before their own mouse handling.
    if (escape && !captures() && mods::mouse_captured()) {
        if (down && !repeat) mods::mouse_release();
        return true;
    }
    // Next, an action of a platform layer (the world editor's placing, handle drag or quest connection):
    // the first Esc cancels it, its release and repeats stay with it; Settings opens on an Esc with nothing
    // to cancel.
    static LayerEscape layer_escape;
    if (escape) {
        static const std::function<bool()> none;
        const bool active = !captures() && layer_mode() != 0;  // layer_mode: only once the registry is sealed
        if (layer_escape.route(down, repeat, active, active ? ext::input_layer().cancel : none)) return true;
    }
    // Inside Settings or the game's text prompt Esc keeps its own meaning (close, cancel) below,
    // also when it is the Settings key.
    const bool settings = action == input_map::kSettings && !(escape && captures());
    if (!g_capturing_keys.load() && action >= 0 && (settings || !captures())) {
        static ToggleGuard settingsGuard;
        const bool activate = settings ? settingsGuard.press(down, repeat, now_s()) : down && !repeat;
        if (activate && settings && escape) set_open(!is_open(), OpenReason::Esc, origin);
        else if (activate) activate_shortcut(action, origin);
        return true;
    }
    if (const int layer = text_entry::active() ? 0 : layer_mode()) {
        // the world editor's overlays: their key handler first (Tab), then Dear ImGui sees the key too;
        // the game keeps it unless a text field of the overlays has the keyboard
        const bool typing = g_wants_text.load();
        const auto& filter = ext::input_layer().key;
        if (filter && filter(code, down, repeat, typing)) return true;
        if (layer == 1 || typing) {
            if (code >= 0 && code < 256 && !repeat) g_held_keys[code] = down;
            push({Event::Key, code, down, 0, 0, mods});
        }
        if (typing) return true;
    }
    if (!is_open()) {
        // the game's text prompt (text_entry.h) has the keyboard while it shows; F1 above still opens the menu
        if (!text_entry::active()) return false;
        text_entry::key(code, down, repeat);
        return true;
    }
    if (code >= 0 && code < 256 && !repeat) g_held_keys[code] = down;
    if (g_capturing_keys.load()) {
        if (down && !repeat) {
            g_capture_mods = mods; g_capture_key = code;
        }
        return true;
    }
    // a dialog or a list in Settings: Esc closes that (popup.h), the menu stays
    if (code == kVK_Escape && g_dialog_open.load()) {
        push({Event::Key, code, down, 0, 0, mods});
        return true;
    }
    if (code == kVK_Escape && down && !repeat) {
        set_open(false, OpenReason::Esc, origin);
        return true;
    }
    push({Event::Key, code, down, 0, 0, mods});
    return true;
}
bool text(const char* utf8) {
    if ((!is_open() && !(layer_mode() && g_wants_text.load())) || g_no_host_keys || !utf8 || !*utf8) return false;
    Event e{Event::Text};
    e.text = utf8;
    push(e);
    return true;
}
void set_clipboard_text(const char* utf8) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_clipboard = utf8 ? utf8 : "";
}
bool wants_text() { return (is_open() || layer_mode() != 0) && g_wants_text.load(); }
void set_stereo_pointer_visible(bool visible) { g_stereo_pointer_visible=visible; }
bool mouse_move(float nx, float ny, bool touch) {
    g_quit_pointer_x = nx; g_quit_pointer_y = ny;
    const int layer = layer_mode();
    if (g_no_host_mouse || (!captures() && stereo::requestedMode==stereo::Off && !layer && !file_select_quit::visible())) return false;
    push({Event::MousePos, 0, false, nx, ny, 0, touch});
    return captures() || layer_takes_pointer();
}
void test_pointer(float nx, float ny, bool down) {
    g_quit_pointer_x = nx; g_quit_pointer_y = ny;
    push({Event::MousePos, 0, false, nx, ny, 0, false});
    push({Event::MouseButton, 0, down, 0, 0, 0, false});
}
// The capture and navigation order is shared with the game-free SDL UI regression.
void prepare_menu_input(ImGuiIO& io, bool open, bool mouse_used) {
    U.capture_completed = false;
    const bool capturing_at_start = U.cap_action >= 0;
    apply_capture();
    std::copy(std::begin(U.prev), std::end(U.prev), U.nav_prev.begin());
    std::copy(std::begin(U.values), std::end(U.values), U.nav_values.begin());
    U.capture_navigation_gate.filter(U.nav_values.data(), capturing_at_start || U.capture_completed);
    const int face[4] = {input_map::face_input(input_map::kA), input_map::face_input(input_map::kB),
                          input_map::face_input(input_map::kX), input_map::face_input(input_map::kY)};
    U.gamepad_navigation.feed(io, U.nav_values.data(), U.nav_prev.data(), open && U.cap_action < 0 && !capturing_at_start, mouse_used, face);  // the text prompt reads the controller itself
}
bool pointer_shortcut(int code, bool down) {
    if (g_no_host_mouse || captures()) return false;
    const int action = input_map::shortcut_for_key(input_map::current(), code, 0);
    if (action < 0) return false;
    if (down) activate_shortcut(action);
    return true;
}
bool mouse_button(int button, bool down, bool touch) {
    if (!captures() && layer_takes_pointer() && !g_no_host_mouse) {
        if (button >= 0 && button < 5) push({Event::MouseButton, button, down, 0, 0, 0, touch});
        return true;
    }
    if (!captures() || g_no_host_mouse) return false;
    if (button < 0 || button >= 5) return true;
    if (!touch && g_capturing_keys.load() && down) {
        g_capture_mods = 0; g_capture_key = input_map::kMouse1 + button;
        return true;
    }
    push({Event::MouseButton, button, down, 0, 0, 0, touch});
    return true;
}
bool mouse_wheel(float dx, float dy) {
    if (!captures() && layer_takes_pointer() && !g_no_host_mouse) {
        push({Event::Wheel, 0, false, dx, dy, 0});
        return true;
    }
    if (!captures() || g_no_host_mouse) return false;
    if (g_capturing_keys.load() && dy != 0) {
        g_capture_mods = 0; g_capture_key = dy > 0 ? input_map::kWheelUp : input_map::kWheelDown;
        return true;
    }
    push({Event::Wheel, 0, false, dx, dy, 0});
    return true;
}
void set_android_screen_dpi(float dpi) { g_android_dpi = dpi; }
void set_density(float d) { g_density = d > 0.5f ? d : 1.0f; }

ImDrawData* frame(float pw, float ph, void (*renderer_init)()) {
    static TestSwitch test = parse_test();
    static bool prefs_read = false;
    if (!prefs_read) {
        prefs_read = true;
        std::string v;
        if (hostui::get("perfOverlay", v)) g_perf = v == "1";
        // the saved controller choice (WWHD_PRO_CONTROLLER wins); it also hides or shows the GamePad screen
        if (!getenv("WWHD_PRO_CONTROLLER") && hostui::get("proController", v))
            hostui::post([pro = v == "1"] { hostui::set_pro_controller(pro); });
        // the saved rumble choice (WWHD_RUMBLE wins)
        if (!rumble::env_override() && hostui::get("rumble", v)) rumble::set_enabled(v != "0");
#ifdef __ANDROID__
        if (!phone_rumble::env_override() && hostui::get("phoneRumble", v)) phone_rumble::choice = phone_rumble::parse(v.c_str());
#endif
        // the saved aspect ratio (WWHD_ASPECT wins; issue #108: it was not remembered)
        if(hostui::get("gameplayFovScale",v))aspect::set_fov_scale(strtof(v.c_str(),nullptr));
        // the saved dynamic resolution (WWHD_DYNRES_MIN wins)
        if (!dynres::min_from_env() && hostui::get("dynResMin", v)) dynres::set_min(strtof(v.c_str(), nullptr));
        if (!getenv("WWHD_ASPECT") && hostui::get("aspectMode", v)) {
            const int m = atoi(v.c_str());
            if (m >= aspect::kOriginal && m <= aspect::k32x9) aspect::set_mode(m);
        }
        // the saved gyro settings (WWHD_GYRO overrides the source)
        hostui::post([] { load_gyro(); });
    }
    if (!test.done && render::frame_count() + 1 >= test.at) {
        test.done = true;
        if (test.tab >= 0) U.select_tab = U.tab = test.tab;
        if (test.open) set_open(true);
        if (test.quit) g_test_quit = true;
        if (test.perf) g_perf = true;
        LOG("[overlay] test switch: %s", getenv("WWHD_TEST_OVERLAY"));
    }
#ifdef WWHD_HAS_VULKAN
    // debug: WWHD_TEST_PRESENT_MODE=mailbox@330 picks Graphics > Presentation at TV frame 330, as the
    // radio button does (the swapchains are recreated)
    static const char* tpm = getenv("WWHD_TEST_PRESENT_MODE");
    if (tpm) {
        const char* at = strchr(tpm, '@');
        if (render::frame_count() + 1 >= (at ? strtoull(at + 1, nullptr, 10) : 1)) {
            std::string m(tpm, at ? at - tpm : strlen(tpm));
            int mode = m == "mailbox" ? gfxvk::kPresentMailbox : m == "immediate" ? gfxvk::kPresentImmediate : gfxvk::kPresentFifo;
            LOG("[overlay] test: presentation %s", m.c_str());
            post_changed([mode] { gfxvk::set_present_mode(mode); });
            tpm = nullptr;
        }
    }
#endif
    // debug: WWHD_TEST_PROFILING=1@600 (or 0@N) sets Graphics > Detailed performance profiling at TV
    // frame N, as the checkbox does (saved too)
    static const char* tprof = getenv("WWHD_TEST_PROFILING");
    if (tprof) {
        const char* at = strchr(tprof, '@');
        if (render::frame_count() + 1 >= (at ? strtoull(at + 1, nullptr, 10) : 1)) {
            const bool on = tprof[0] == '1';
            LOG("[overlay] test: detailed performance profiling %s", on ? "on" : "off");
            rprof::set_requested(on);
            hostui::post([on] { hostui::set(rprof::kSettingKey, on ? "1" : "0"); });
            tprof = nullptr;
        }
    }
    // debug: WWHD_TEST_BG_SHADERS=fast@600 (or 0@N) sets Graphics > Shader compile at TV frame N, as the
    // radio buttons do (saved too)
    static const char* tbg = getenv("WWHD_TEST_BG_SHADERS");
    if (tbg) {
        const char* at = strchr(tbg, '@');
        if (render::frame_count() + 1 >= (at ? strtoull(at + 1, nullptr, 10) : 1)) {
            const std::string value(tbg, at ? size_t(at - tbg) : strlen(tbg));
            const auto l = bgshaders::parse_level(value.c_str());
            LOG("[overlay] test: shader compile %s", bgshaders::level_name(l));
            bgshaders::set_level(l);
            hostui::post([l] { hostui::set(bgshaders::kSettingKey, bgshaders::level_value(l)); });
            tbg = nullptr;
        }
    }
    const double t = now_s();
    if (U.last_present > 0) {
        U.frame_ms[U.frame_i] = (float)((t - U.last_present) * 1000.0);
        U.frame_i = (U.frame_i + 1) % 120;
    }
    U.last_present = t;
    g_last_frame = t;
    g_average.sample(t, gx2::flips_presented(), interp::executed_steps(), int(render::active()),
                     interp::mode(), interp::fps(), hostui::res_scale());
    read_controller();
    // the game's text prompt shows unless the menu is open over it (the menu has the input then)
    const bool open = is_open(), perf = perf_shown(), text = !open && text_entry::active();
    if (wwhd_trace::enabled()) {
        wwhd_trace::sink = [](const char* line) { LOG("[overlay-trace] %s",line); };
        wwhd_trace::open = open;
        static bool was_open = false;
        if (open && !was_open) { U.gamepad_navigation.reset_trace(); wwhd_trace::emit("open frame=%d", U.init ? ImGui::GetFrameCount() : 0); }
        was_open = open;
    }
    perf::set_demand(perf, open && shown_builtin() == kGraphics);
    // Headless report QA uses the same formatter as the clipboard button; no clipboard mutation.
    // WWHD_TEST_PERF_REPORT=file (at WWHD_TEST_PERF_REPORT_AT presented frames, default 300), or
    // file@frames,file@frames,... for several reports in one run
    struct TestReport { std::string path; uint64_t at; bool written; };
    static std::vector<TestReport> testReports = [] {
        std::vector<TestReport> list;
        const char* e = getenv("WWHD_TEST_PERF_REPORT");
        const char* a = getenv("WWHD_TEST_PERF_REPORT_AT");
        const uint64_t defaultAt = a ? strtoull(a, nullptr, 10) : 300;
        for (std::string rest = e ? e : ""; !rest.empty();) {
            const size_t comma = rest.find(',');
            std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
            const size_t at = item.rfind('@');
            if (at != std::string::npos && at + 1 < item.size() && isdigit((unsigned char)item[at + 1]))
                list.push_back({item.substr(0, at), strtoull(item.c_str() + at + 1, nullptr, 10), false});
            else if (!item.empty()) list.push_back({item, defaultAt, false});
        }
        return list;
    }();
    for (TestReport& r : testReports)
        if (!r.written && gx2::flips_presented() >= r.at)
            if (FILE* f = fopen(r.path.c_str(), "w")) {
                const std::string report = performance_report();
                fwrite(report.data(), 1, report.size(), f); fclose(f);
                r.written = true;
            }
    // save state notices (saved, refused during a cutscene, loaded into another Quest Log) show over the
    // game for a few seconds while the menu is closed (the window title is not visible everywhere)
    std::string toast = open ? std::string() : ss::last_message();
    // the platform's layers over the game (extensions.h): which draw this frame; their notice replaces
    // the save state notice
    const std::vector<ext::FrameLayer>& layers = ext::frame_layers();
    static std::vector<const ext::FrameLayer*> drawn_layers;
    drawn_layers.clear();
    if (!open)
        for (const ext::FrameLayer& layer : layers) {
            std::string line;
            if (layer.wanted(line)) drawn_layers.push_back(&layer);
            if (!line.empty()) toast = std::move(line);
        }
    const bool stereoPointer=stereo::frame.mode!=stereo::Off && g_stereo_pointer_visible.load()
#ifdef WWHD_SDL_HOST
        && window_target::selected.load()==window_target::Window::TV
#endif
        ;
    U.linearized = false;
    // a mod install keeps running (and shows its progress) while the menu is closed
    const bool mods_busy = !setup_run.id.empty() || (U.init && modsui::wants_background()) || modsui::startup_notice_pending();
    if (!open) gfx::g_pip_preview=false;
    if (!open && !perf && !text && toast.empty() && !guesthud::active() && !file_select_quit::visible() && !mods_busy && !stereoPointer && drawn_layers.empty()) {
        if (U.init) {  // forget events and pressed keys while nothing is shown
            std::lock_guard<std::mutex> lk(g_mu);
            g_events.clear();
        }
        return nullptr;
    }
    if (pw < 16 || ph < 16) return nullptr;
    if (!U.init) {
        init_context();
        if (renderer_init) renderer_init();
    }
    ImGuiIO& io = ImGui::GetIO();
    // UI scale: HiDPI backing scale, and larger on large windows (a TV across the room)
#ifdef __ANDROID__
    static const bool loadedUiSize = [] {
        std::string saved;
        if (hostui::get("uiSize", saved)) g_ui_size = std::clamp(atoi(saved.c_str()), 0, 3);
        return true;
    }();
    (void)loadedUiSize;
    // phones get a more compact layout than tablets and handhelds (mobile_ui.h); a UI size the player chose decides
    const mobile_ui::Layout ui = mobile_ui::layout(g_android_dpi, g_density, g_ui_size, float(pw), float(ph));
    mobile_ui::apply_touch_style(ImGui::GetStyle(), ui);
    const float scale = ui.scale;
#else
    const float scale = mobile_ui::desktop_scale(g_density.load(), float(ph));
#endif
    io.DisplaySize = ImVec2(pw / scale, ph / scale);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    io.MouseDrawCursor=stereoPointer;
    // Refresh a stationary pointer too: resizing/HiDPI can change DisplaySize.
    const ImVec2 pointer=pointer_position(U.mouse_position.x,U.mouse_position.y,io.DisplaySize);
    overlay_trace::mouse_pos(io,pointer.x,pointer.y);
    io.DeltaTime = U.last_time > 0 ? (float)std::clamp(t - U.last_time, 1e-4, 0.25) : 1.0f / 60.0f;
    U.last_time = t;
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        events.swap(g_events);
    }
    bool mouse_used = false;
    for (const Event& e : events) {
        if (wwhd_trace::enabled() && open && e.kind == Event::Key)
            wwhd_trace::emit("event kind=%d code=%d down=%d x=%.6f y=%.6f mods=%d", int(e.kind), e.code, e.down, e.x, e.y, e.mods);
        switch (e.kind) {
        case Event::Key:
            io.AddKeyEvent(ImGuiMod_Shift, e.mods & kShift);
            io.AddKeyEvent(ImGuiMod_Ctrl, e.mods & kCtrl);
            io.AddKeyEvent(ImGuiMod_Alt, e.mods & kAlt);
            io.AddKeyEvent(ImGuiMod_Super, e.mods & kSuper);
            if (ImGuiKey k = imgui_key(e.code); k != ImGuiKey_None) io.AddKeyEvent(k, e.down);
            break;
        case Event::MousePos:
            io.AddMouseSourceEvent(e.touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            mouse_used |= e.x != U.mouse_position.x || e.y != U.mouse_position.y;
            U.mouse_position = ImVec2(e.x, e.y);
            {
                const ImVec2 p=pointer_position(e.x,e.y,io.DisplaySize);
                overlay_trace::mouse_pos(io,p.x,p.y);
            }
            break;
        case Event::MouseButton: io.AddMouseSourceEvent(e.touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse); mouse_used = true; overlay_trace::mouse_button(io, e.code, e.down); break;
        case Event::Wheel: mouse_used |= e.x != 0 || e.y != 0; overlay_trace::wheel(io, e.x, e.y); break;
        case Event::Text: io.AddInputCharactersUTF8(e.text.c_str()); break;
        case Event::Focus:
            // Clear ImGui keys/buttons in event order; the overlay stays active on the other window.
            release_focus_inputs(io);
            break;
        default: break;
        }
    }
    prepare_menu_input(io, open, mouse_used);
    ImGui::NewFrame();
    setup_run_tick();
    modsui::tick(mods_host(), open);
    if (!open && !g_restart_notice.started()) restart_notice_frame(false);
    const int layer = open ? 0 : layer_mode();
    g_wants_text = (open || layer) && io.WantTextInput;
    guesthud::frame();
    if (!open && !text) file_select_quit::frame(io.DisplaySize.x, io.DisplaySize.y);
    gfx::g_pip_preview=false;
    if (open) settings_window();
    else modsui::background(mods_host());
    g_dialog_open = open && ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    pip_live_drag();
    if (text) {
        float pad[input_map::kPadCount];
        std::copy(std::begin(U.values), std::end(U.values), pad);
        if (g_no_host) test_pad(pad);  // WWHD_TEST_PAD drives the on-screen keyboard in test runs
        if (text_entry::draw(pad)) {
            // answered: the button that confirmed stays from the game until released (release_gate.h)
            for (auto& k : g_held_keys) k = false;
        }
    }
    bool covered = false;  // a platform layer drew over the whole picture: no notice
    for (const ext::FrameLayer* layer : drawn_layers) covered |= layer->draw(io.DisplaySize.x, io.DisplaySize.y);
    if (perf) perf_window(open);
    if (!toast.empty() && !covered) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(io.DisplaySize.x * 0.8f, FLT_MAX));
        const ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##savestate_notice", nullptr, fl)) {
            ImGui::PushTextWrapPos(io.DisplaySize.x * 0.75f);
            ImGui::TextUnformatted(toast.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
    }
    g_layer_mouse = !open && layer && io.WantCaptureMouse;
    // test aid: WWHD_TEST_LOG_HOVER=1 logs the window under the pointer when it changes, and each click
    static const bool log_hover = getenv("WWHD_TEST_LOG_HOVER") != nullptr;
    if (log_hover) {
        static std::string last;
        const ImGuiWindow* hw = ImGui::GetCurrentContext()->HoveredWindow;
        std::string now = hw ? hw->Name : "(none)";
        const ImGuiID hid = ImGui::GetHoveredID();
        if (now != last || io.MouseClicked[0] || io.MouseClicked[1])
            LOG("[overlay] hover: %s id %08x at %.0f %.0f (layer %d, clicked %d/%d, open %d)", now.c_str(), hid, io.MousePos.x,
                io.MousePos.y, layer, io.MouseClicked[0], io.MouseClicked[1], open);
        if (io.MouseClicked[0]) {  // the windows under the pointer's reach, back to front
            std::string order;
            for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
                if (w->Active && (!(w->Flags & ImGuiWindowFlags_ChildWindow) || w->DockNode))
                    order += std::string(w->Name) + (w->DockNode ? " (docked)" : "") + " | ";
            LOG("[overlay] windows back to front: %s", order.c_str());
        }
        last = std::move(now);
    }
    ImGui::Render();
    overlay_trace::state();
    return ImGui::GetDrawData();
}

void linearize_colors(ImDrawData* d,bool fresh_copy) {
    if (!d || (!fresh_copy && U.linearized)) return;
    if (!fresh_copy) U.linearized = true;
    static uint8_t table[256];
    static bool made = false;
    if (!made) {
        for (int i = 0; i < 256; i++) {
            float c = i / 255.0f;
            c = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
            table[i] = (uint8_t)std::lround(c * 255.0f);
        }
        made = true;
    }
    for (ImDrawList* l : d->CmdLists)
        for (ImDrawVert& v : l->VtxBuffer) {
            uint32_t c = v.col;
            v.col = (c & 0xFF000000u) | (uint32_t)table[(c >> 16) & 0xFF] << 16 | (uint32_t)table[(c >> 8) & 0xFF] << 8 |
                    table[c & 0xFF];
        }
}

}  // namespace overlay
