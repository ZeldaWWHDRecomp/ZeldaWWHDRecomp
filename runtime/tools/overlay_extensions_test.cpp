// overlay/extensions.h (issue #173): what a port registers at start-up and what the overlay reads back.
// The tab bar's order with added and hidden tabs, controller L / R over it, the Features defaults (today's
// behaviour), the registry seal, platform shortcuts with the #153 hand-back rule, and the overlay's text
// fields through a platform keyboard (text_entry::field, real Dear ImGui frames, no window or backend).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "input_map.h"
#include "overlay/extensions.h"
#include "overlay/layer_escape.h"
#include "overlay/release_gate.h"
#include "overlay/text_entry.h"

using namespace overlay;
using input_map::kPadCount;

namespace {

std::vector<std::string> ids() {
    std::vector<std::string> out;
    for (const auto& t : ext::tabs()) out.push_back(t.id);
    return out;
}

void defaults() {
    ext::reset_for_test();
    // nothing registered: the built-in tabs in their order, every feature on, no hooks
    const auto& tabs = ext::tabs();
    assert(int(tabs.size()) == ext::kBuiltinCount);
    for (int i = 0; i < ext::kBuiltinCount; i++) {
        assert(tabs[i].id == ext::kBuiltinTabs[i].id && tabs[i].name == ext::kBuiltinTabs[i].name);
        assert(tabs[i].builtin == i && ext::builtin_tab(i) == i && !tabs[i].draw);
    }
    assert((ids() == std::vector<std::string>{"saves", "graphics", "display", "audio", "mods", "controls", "shortcuts", "about"}));
    const ext::Features f = ext::features(), plain{};
    assert(f.clipboard && f.file_picker && f.screenshots && f.full_states);
    assert(plain.clipboard && plain.file_picker && plain.screenshots && plain.full_states);
    assert(f.code_mods && f.mod_downloads && f.speaker_choice && f.keyboard);
    assert(plain.code_mods && plain.mod_downloads && plain.speaker_choice && plain.keyboard);
    assert(!ext::font_loader() && ext::shortcuts().empty() && !ext::text_input() && ext::frame_layers().empty());
    // L / R wrap around the bar, as before
    assert(ext::next_tab(0, -1) == ext::kBuiltinCount - 1 && ext::next_tab(ext::kBuiltinCount - 1, +1) == 0);
    assert(ext::next_tab(2, +1) == 3);
    assert(ext::tab_index("mods") == 4 && ext::tab_index("warp") == -1 && ext::builtin_tab(99) == -1);
    // no shortcut: the poll takes nothing and leaves the input alone
    PadValues pad{};
    pad[input_map::kPadOptions] = 1;
    assert(!ext::poll_shortcuts(pad, false) && pad[input_map::kPadOptions] == 1);
}

void added_and_hidden() {
    ext::reset_for_test();
    int drawn = 0;
    ext::add_tab({"platform", "Platform", [&] { ++drawn; }, nullptr}, "mods");
    ext::add_tab({"warp", "Warp", [&] { drawn += 10; }, nullptr});            // default: before "about"
    ext::add_tab({"tools", "Tools", [] {}, nullptr}, "no-such-tab");           // unknown: before "about" too
    ext::add_tab({"mods", "Duplicate", [] {}, nullptr});                       // id taken: ignored
    ext::add_tab({"", "No id", [] {}, nullptr});                               // ignored
    ext::hide_tab("graphics");
    ext::hide_tab("display");
    ext::hide_tab("controls");
    ext::hide_tab("shortcuts");
    ext::hide_tab("tools");  // an added tab can be hidden too
    // a later registration may still name a hidden tab as its place
    ext::add_tab({"early", "Early", [] {}, nullptr}, "graphics");
    assert((ids() == std::vector<std::string>{"saves", "early", "audio", "platform", "mods", "warp", "about"}));
    const auto& tabs = ext::tabs();
    assert(tabs[3].builtin == -1 && tabs[3].name == "Platform" && tabs[4].builtin == 4 && tabs[6].builtin == 7);
    tabs[3].draw();
    tabs[5].draw();
    assert(drawn == 11);
    // controller L / R pass through the added tabs and wrap at the ends
    assert(ext::next_tab(ext::tab_index("audio"), +1) == ext::tab_index("platform"));
    assert(ext::next_tab(ext::tab_index("platform"), +1) == ext::tab_index("mods"));
    assert(ext::next_tab(ext::tab_index("about"), +1) == 0 && ext::next_tab(0, -1) == ext::tab_index("about"));
    // WWHD_TEST_OVERLAY=open:<id> finds added tabs; hidden ones are not shown
    assert(ext::tab_index("warp") == 5 && ext::tab_index("graphics") == -1 && ext::tab_index("tools") == -1);

    // "about" hidden: the default place is the end
    ext::reset_for_test();
    ext::hide_tab("about");
    ext::add_tab({"warp", "Warp", [] {}, nullptr});
    assert((ids() == std::vector<std::string>{"saves", "graphics", "display", "audio", "mods", "controls", "shortcuts", "warp"}));

    // a tab a setting turns on: in the list, but L / R skip it while it is off
    ext::reset_for_test();
    bool on = false;
    ext::Tab editor{"editor", "Editor", [] {}, nullptr};
    editor.shown = [&] { return on; };
    ext::add_tab(editor, "about");
    const int at = ext::tab_index("editor");
    assert(at >= 0 && !ext::tabs()[at].visible());
    assert(ext::next_tab(at - 1, +1) == at + 1 && ext::next_tab(at + 1, -1) == at - 1);
    on = true;
    assert(ext::tabs()[at].visible() && ext::next_tab(at - 1, +1) == at && ext::next_tab(at + 1, -1) == at);
}

void sealed_late() {
    ext::reset_for_test();
    ext::Features f;
    f.clipboard = false;
    f.file_picker = false;
    f.code_mods = false;
    f.keyboard = false;
    ext::set_features(f);
    assert(!ext::features().clipboard && !ext::features().file_picker && ext::features().screenshots);
    assert(!ext::features().code_mods && ext::features().mod_downloads && !ext::features().keyboard);
    assert(!ext::sealed());
    (void)ext::tabs();  // the overlay's first frame
    assert(ext::sealed());
    // too late: nothing changes
    ext::add_tab({"late", "Late", [] {}, nullptr});
    ext::hide_tab("saves");
    ext::set_features(ext::Features{});
    ext::add_shortcut([](const PadValues&, bool) { return true; });
    ext::set_font_loader([](ImGuiIO&) { return true; });
    ext::set_text_input([](const text_entry::Request&, text_entry::Done) {});
    ext::add_frame_layer({[](std::string&) { return true; }, [](float, float) { return true; }});
    assert(ext::tab_index("late") == -1 && ext::tab_index("saves") == 0);
    assert(!ext::features().clipboard && ext::shortcuts().empty() && !ext::font_loader() && !ext::text_input());
    assert(!ext::features().code_mods && !ext::features().keyboard);
    assert(ext::frame_layers().empty());
}

// A port's shortcut: ZL + ZR + Minus (Options) switches a view, once per press.
void shortcuts() {
    using namespace input_map;
    ext::reset_for_test();
    int switched = 0;
    bool latched = false;
    ext::add_shortcut([&](const PadValues& pad, bool menu_open) {
        const bool combo = !menu_open && pad[kPadLT] > 0.5f && pad[kPadRT] > 0.5f && pad[kPadOptions] > 0.5f;
        if (!combo) {
            if (pad[kPadOptions] <= 0.5f) latched = false;
            return false;
        }
        if (latched) return false;
        latched = true;
        ++switched;
        return true;
    });
    // the host's input pump: the shortcuts, then the game's gate (input_sdl.cpp: captured when a
    // shortcut took the poll's input)
    ReleaseGate game_gate;
    PadValues held{};
    struct Poll { PadValues settings, game; bool took; };
    auto poll = [&](bool menu_open = false) {
        Poll out;
        out.settings = held;
        out.took = ext::poll_shortcuts(out.settings, menu_open);
        out.game = held;
        game_gate.filter(out.game.data(), out.took);
        return out;
    };
    held[kPadLT] = held[kPadRT] = 1;
    Poll p = poll();
    assert(!p.took && p.game[kPadLT] == 1 && p.settings[kPadRT] == 1);  // the triggers alone are the game's
    held[kPadOptions] = 1;
    p = poll();
    assert(p.took && switched == 1);
    // the taken inputs reach neither the game nor the settings shortcut (Minus held must not open the menu)
    assert(p.game[kPadOptions] == 0 && p.game[kPadLT] == 0 && p.game[kPadRT] == 0);
    assert(p.settings[kPadOptions] == 0 && p.settings[kPadLT] == 0);
    for (int i = 0; i < 100; i++) {  // held on: once per press, still kept away
        p = poll();
        assert(!p.took && p.game[kPadOptions] == 0 && p.settings[kPadOptions] == 0 && p.game[kPadRT] == 0);
    }
    assert(switched == 1);
    // another button pressed meanwhile is the game's at once
    held[kPadA] = 1;
    p = poll();
    assert(p.game[kPadA] == 1 && p.settings[kPadA] == 1);
    // released one by one: each comes back on its own next press
    held[kPadLT] = 0;
    p = poll();
    assert(p.game[kPadLT] == 0 && p.game[kPadRT] == 0);
    held[kPadLT] = 1;
    p = poll();
    assert(p.game[kPadLT] == 1 && p.game[kPadRT] == 0 && !p.took);
    held = PadValues{};
    poll();
    held[kPadOptions] = 1;  // a plain Minus later: the game's and the settings shortcut's
    p = poll();
    assert(!p.took && p.game[kPadOptions] == 1 && p.settings[kPadOptions] == 1 && switched == 1);
}

// The overlay's text fields through a platform keyboard.
void text_fields() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.f / 60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    char name[16] = "Tetra";
    bool returned = false;
    auto frame = [&](const char* label = "Name") {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::Begin("Settings");
        returned = text_entry::field(label, name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue);
        const ImRect r = GImGui->LastItemData.Rect;
        ImGui::End();
        ImGui::Render();
        return r;
    };
    auto click = [&](const ImRect& r) {
        io.AddMousePosEvent(r.GetCenter().x, r.GetCenter().y);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };

    // no platform keyboard: an ordinary ImGui field (typed text goes in)
    ext::reset_for_test();
    ImRect r = frame();
    frame();
    click(r);
    io.AddInputCharactersUTF8("s");
    frame();
    assert(!strcmp(name, "Tetras") && !text_entry::field_active());
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    assert(returned);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    io.AddMousePosEvent(-100, -100);
    io.AddMouseButtonEvent(0, true);
    frame();  // click outside: the field lets go
    io.AddMouseButtonEvent(0, false);
    frame();
    ImGui::DestroyContext();

    // a platform keyboard: choosing the field shows it; the answer arrives in a later frame
    ImGui::CreateContext();
    ImGuiIO& io2 = ImGui::GetIO();
    io2.IniFilename = nullptr;
    io2.DisplaySize = ImVec2(800, 600);
    io2.DeltaTime = 1.f / 60;
    io2.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ext::reset_for_test();
    int shown = 0;
    text_entry::Request asked;
    text_entry::Done answer;
    ext::set_text_input([&](const text_entry::Request& request, text_entry::Done done) {
        ++shown;
        asked = request;
        answer = std::move(done);
    });
    snprintf(name, sizeof name, "Tetra");
    r = frame("Name##field");
    frame("Name##field");
    io2.AddMousePosEvent(r.GetCenter().x, r.GetCenter().y);
    io2.AddMouseButtonEvent(0, true);
    frame("Name##field");
    io2.AddMouseButtonEvent(0, false);
    frame("Name##field");
    assert(shown == 1 && asked.initial == u"Tetra" && asked.hint == u"Name" && asked.max_len == 15);
    assert(text_entry::field_active());  // text_entry::active() too: the overlay and the game get no input
    assert(!GImGui->ActiveId);            // read-only: the field does not keep the focus for typing
    io2.AddInputCharactersUTF8("x");
    frame("Name##field");
    assert(!strcmp(name, "Tetra") && !returned);
    // cancelled: nothing changes
    answer(false, u"ignored");
    assert(!text_entry::field_active());
    frame("Name##field");
    assert(!returned && !strcmp(name, "Tetra"));
    // again, answered from another thread's callback: the text arrives (cut at a character boundary)
    io2.AddMouseButtonEvent(0, true);
    frame("Name##field");
    io2.AddMouseButtonEvent(0, false);
    frame("Name##field");
    assert(shown == 2 && text_entry::field_active());
    answer(true, u"Medli ééééé");  // 6 + 5 x 2 bytes: 15 bytes fit, the last é does not
    frame("Name##field");
    assert(returned && !strcmp(name, "Medli \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"));
    frame("Name##field");
    assert(!returned);  // once
    ImGui::DestroyContext();
    ext::reset_for_test();
}

// Esc and a layer's action (layer_escape.h): the first Esc cancels the editor's placing, handle drag or
// quest connection and is taken with its repeats and release; an Esc with nothing to cancel is Settings'.
void layer_escape() {
    using editor_escape::Cancel;
    assert(editor_escape::what(true, true, true) == Cancel::Drag);
    assert(editor_escape::what(false, true, true) == Cancel::Connect);
    assert(editor_escape::what(false, false, true) == Cancel::Placing);
    assert(editor_escape::what(false, false, false) == Cancel::None);
    editor_escape::Gate gate;
    LayerEscape esc;
    const std::function<bool()> cancel = [&] { return gate.take(); };
    // placing: the first Esc cancels it (taken), the release too; the render thread then stops placing
    gate.publish(Cancel::Placing);
    assert(esc.route(true, false, true, cancel));
    assert(esc.route(true, true, true, cancel));   // a held Esc's repeats stay with it
    assert(esc.route(false, false, true, cancel));  // and its release
    assert(gate.requested() == Cancel::Placing && gate.requested() == Cancel::None);
    // nothing going on: Esc is not the layer's (Settings opens)
    gate.publish(Cancel::None);
    assert(!esc.route(true, false, true, cancel) && !esc.route(false, false, true, cancel));
    // a taken Esc waits for its frame: a second Esc before it is Settings', and a publish meanwhile is ignored
    gate.publish(Cancel::Drag);
    assert(esc.route(true, false, true, cancel) && esc.route(false, false, true, cancel));
    gate.publish(Cancel::Drag);
    assert(!esc.route(true, false, true, cancel));
    esc.route(false, false, true, cancel);
    assert(gate.requested() == Cancel::Drag);
    // the layer off (Settings or the game's text prompt open, not the editor view): never the layer's
    gate.publish(Cancel::Connect);
    assert(!esc.route(true, false, false, cancel) && !esc.route(false, false, false, cancel));
    assert(gate.requested() == Cancel::None);
    // a layer without cancel
    assert(!esc.route(true, false, true, nullptr));
}

}  // namespace

int main() {
    defaults();
    added_and_hidden();
    sealed_late();
    shortcuts();
    text_fields();
    layer_escape();
    printf("overlay extensions: tabs, features, seal, shortcuts and text fields passed\n");
    return 0;
}
