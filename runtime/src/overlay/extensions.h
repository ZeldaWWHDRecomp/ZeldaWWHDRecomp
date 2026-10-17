// Platform extensions of the settings overlay (issue #173): a port adds its own tabs, hides built-in ones,
// says which host features the built-in tabs can offer, and plugs in a font, controller shortcuts, a
// system keyboard and layers drawn over the game, without editing overlay.cpp.
//
// Registration: only at start-up, from the main thread, before the overlay draws its first frame and
// before the host's input pump first polls the controller shortcuts. That first frame or poll seals
// the registry: a later call changes nothing and logs "[overlay] extension registered too late". No
// locks are needed after that, as everything here is read only.
//
// Nothing registered is today's overlay: the built-in tabs in their order, every feature on, the
// platform fonts, the built-in shortcuts and the game's own text prompt. The per-frame cost is then an
// empty-vector check.
//
// Callbacks are std::function throughout (as hostui.h and text_entry.h).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "release_gate.h"  // PadValues
#include "text_entry.h"

struct ImGuiIO;

namespace overlay::ext {

// ---------------------------------------------------------------- tabs
// The built-in tabs in their default order. The ids name them for hide_tab, add_tab's before_id and
// WWHD_TEST_OVERLAY=open:<id>.
struct BuiltinTab {
    const char* id;
    const char* name;
};
inline constexpr BuiltinTab kBuiltinTabs[] = {
    {"saves", "Saves"}, {"graphics", "Graphics"}, {"display", "Display"}, {"audio", "Audio"},
    {"mods", "Mods"},   {"controls", "Controls"}, {"shortcuts", "Shortcuts"}, {"about", "Language / About"},
};
inline constexpr int kBuiltinCount = int(sizeof kBuiltinTabs / sizeof kBuiltinTabs[0]);

// An added tab behaves like a built-in one: controller L / R reach it, the overlay remembers it as the
// open tab for the session, and WWHD_TEST_OVERLAY=open:<id> opens it.
struct Tab {
    std::string id;    // unique, lower case ("warp")
    std::string name;  // the tab's label (unique: Dear ImGui keys tab items by label)
    // the page: render thread, inside the tab's scrolling child window, while the tab is selected
    std::function<void()> draw;
    // optional: controller L / R while the tab is selected (-1 / +1), for a tab with its own sub-tabs
    // as Mods has. True: the tab moved inside itself; false (at its ends): the overlay changes tabs.
    std::function<bool(int direction)> shoulder;
    // optional (any thread, cheap): false leaves the tab out of the tab bar for now (a setting turns it
    // on later, as the world editor's tab); L / R skip it then. Null: always shown.
    std::function<bool()> shown;
};
// Places the tab before the tab `before_id` (built-in or added); null or an unknown id: before "about"
// (at the end if "about" is hidden). Ids already taken are ignored.
void add_tab(Tab tab, const char* before_id = nullptr);
// Leaves a tab (built-in or added) out of the tab bar.
void hide_tab(const char* id);

// ---------------------------------------------------------------- host features
// What the built-in tabs may offer on this host. The defaults are every platform's behaviour today.
struct Features {
    bool clipboard = true;    // "Copy ..." buttons (Saves, Graphics); without: the text is shown instead
    bool file_picker = true;  // file and folder choosers (Mods: install from a file, the GameCube game)
    bool screenshots = true;  // the Screenshots section of Saves
    bool full_states = true;  // full save states and Crash Recovery's automatic states (both are full states)
    bool code_mods = true;    // code mods (Mods > More > Code-mod support); without: that section is left out and
                              // a catalogue code mod says it can't run on this platform
    bool mod_downloads = true;  // the online mod list (Mod list settings, mods available to download, the Story
                                // Notebook download); without: only the mods already in the mods folder
    bool speaker_choice = true; // Audio: the Speakers choice (stereo / surround 5.1); without: the host's output as is
    bool keyboard = true;       // keyboard and mouse hints (Saves, Language / About); without: the controller's only
};
void set_features(const Features& features);
const Features& features();  // any thread

// ---------------------------------------------------------------- hooks (each optional)
// Loads the overlay's font into io.Fonts (render thread, once, when the overlay first draws). False:
// the overlay loads its usual font.
using FontLoader = std::function<bool(ImGuiIO& io)>;
void set_font_loader(FontLoader load);

// A controller shortcut, polled with the settings shortcut (overlay::poll_controller_shortcuts: the
// host's input pump; the AppKit host polls on the overlay's frame). `pad` holds every device combined
// (input_map pad order, 0..1), `menu_open` whether the settings menu is open. True: the shortcut took
// this poll's input. The inputs held then stay from the game (release_gate.h, as after the overlay
// had the input, issue #153) and from the settings shortcut until each one is released. A shortcut that
// acts once per press keeps its own latch and returns true only on that poll.
using Shortcut = std::function<bool(const PadValues& pad, bool menu_open)>;
void add_shortcut(Shortcut poll);

// The platform's own keyboard for the overlay's text fields (text_entry::field), for hosts without a
// physical one. Shows the keyboard for `request` (initial text, hint, max_len in UTF-16 units) and calls
// `done` exactly once, from any thread. While it shows, text_entry::active() is true, so the game gets no
// input and the inputs held when it closes stay from the game until released, as with the game's
// text prompt. The game's own text requests keep the overlay's text prompt.
using TextInput = std::function<void(const text_entry::Request& request, text_entry::Done done)>;
void set_text_input(TextInput begin);

// Something drawn over the game while the settings menu is closed (render thread, every TV present).
struct FrameLayer {
    // Cheap: whether to draw this frame. `toast` may be set to a one-line notice that replaces the save
    // state notice at the bottom of the picture (it shows even when this returns false).
    std::function<bool(std::string& toast)> wanted;
    // Inside the overlay's Dear ImGui frame, `w` x `h` the display size in ImGui units. True: the layer
    // covered the whole picture, so the bottom notice is left out this frame.
    std::function<bool(float w, float h)> draw;
};
void add_frame_layer(FrameLayer layer);

// ---------------------------------------------------------------- read by the overlay (and the tests)
// Each call seals the registry (see the top of this file).
struct ShownTab {
    std::string id, name;
    int builtin = -1;  // index in kBuiltinTabs; -1: an added tab (draw and shoulder from its Tab)
    std::function<void()> draw;
    std::function<bool(int direction)> shoulder;
    std::function<bool()> shown;  // null: always
    bool visible() const { return !shown || shown(); }
};
const std::vector<ShownTab>& tabs();  // the tab bar, in order (check visible() for each)
int tab_index(const std::string& id);  // in tabs(); -1 if not shown
int builtin_tab(int index);            // kBuiltinTabs index of tabs()[index]; -1 for an added tab or out of range
// Controller L / R on the tab bar: the visible tab after (direction +1) or before (-1) `index`, wrapping around.
int next_tab(int index, int direction);
// Controller L / R while tabs()[index] is open: the tab to switch to, or -1 when the tab moved between
// its own sub-tabs instead (an added tab's shoulder, or `builtin_shoulder` for a built-in tab: Mods).
int shoulder(int index, int direction, const std::function<bool(int builtin, int direction)>& builtin_shoulder);
const FontLoader& font_loader();
const std::vector<Shortcut>& shortcuts();
// Runs the shortcuts on `pad` (overlay::poll_controller_shortcuts, one caller): true when one took this
// poll's input. `pad` comes back without the inputs a shortcut took, now or at an earlier poll and held
// since, for the settings shortcut.
bool poll_shortcuts(PadValues& pad, bool menu_open);
const TextInput& text_input();
const std::vector<FrameLayer>& frame_layers();
void seal();
bool sealed();

// Tests only: forget every registration and unseal.
void reset_for_test();

}  // namespace overlay::ext
