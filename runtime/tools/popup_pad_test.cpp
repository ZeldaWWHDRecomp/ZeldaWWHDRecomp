// Settings dialogs with a controller (overlay/popup.h, overlay/gamepad_nav.h), real ImGui frames, no window:
//  - a dialog opened while the controller navigates shows its default button selected (Cancel), and A presses it;
//  - a dialog without a default selects its first button;
//  - A that hands the navigation back from the mouse, or that is held while the navigation turns on, presses
//    nothing until it is let go;
//  - the right stick scrolls a long body (down and back up), and nothing else moves;
//  - after the mouse took the navigation, no selection shows and the stick does not scroll.
#include "overlay/gamepad_nav.h"
#include "overlay/popup.h"
#include "imgui_internal.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <string>

using namespace input_map;
using Values = std::array<float, kPadCount>;

namespace {
struct Ui {
    overlay::GamepadNavigation nav;
    Values previous{};
    bool open = false, mouse = false, with_default = true;
    std::string pressed;
    float body_scroll = 0, body_max = 0;
    ImGuiID cancel_id = 0, ok_id = 0;
    Ui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {1000, 700};
        io.DeltaTime = 1.f / 60;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_RendererHasTextures;
    }
    ~Ui() { ImGui::DestroyContext(); }
    void frame(const Values& v, bool enabled = true) {
        auto& io = ImGui::GetIO();
        nav.feed(io, v.data(), previous.data(), enabled, mouse);
        overlay::popup::controller_nav() = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
        previous = v;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::Button("Behind");  // the Settings page's own item under the dialog
        if (overlay::popup::begin("Test dialog", open)) {
            open = false;
            overlay::popup::body_begin();
            for (int i = 0; i < 120; i++) ImGui::Text("A long line of the dialog's text, number %d", i);
            body_scroll = ImGui::GetScrollY();
            body_max = ImGui::GetScrollMaxY();
            overlay::popup::body_end();
            if (ImGui::Button("OK")) pressed = "OK";
            ok_id = ImGui::GetItemID();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) pressed = "Cancel";
            cancel_id = ImGui::GetItemID();
            if (with_default) ImGui::SetItemDefaultFocus();
            if (!pressed.empty()) ImGui::CloseCurrentPopup();
            overlay::popup::end();
        }
        ImGui::End();
        ImGui::Render();
    }
    void frames(const Values& v, int n) { for (int i = 0; i < n; i++) frame(v); }
};
Values none() { return Values{}; }
Values with(int input, float value = 1.f) {
    Values v{};
    v[input] = value;
    return v;
}
}  // namespace

int main() {
    {  // the default (Cancel) is selected from the start; A presses it
        Ui ui;
        ui.frames(none(), 3);
        ui.open = true;
        ui.frames(none(), 3);
        ImGuiContext& g = *GImGui;
        assert(g.NavCursorVisible && g.NavId == ui.cancel_id);
        ui.frame(with(kPadA));
        ui.frames(none(), 2);
        assert(ui.pressed == "Cancel");
    }
    {  // without a default: the first button
        Ui ui;
        ui.with_default = false;
        ui.frames(none(), 3);
        ui.open = true;
        ui.frames(none(), 3);
        ImGuiContext& g = *GImGui;
        assert(g.NavCursorVisible && g.NavId == ui.ok_id);
    }
    {  // A while the mouse has the navigation: it hands the navigation back but presses nothing until let go
        Ui ui;
        ui.mouse = true;
        ui.frames(none(), 3);
        ui.open = true;
        ui.frames(none(), 3);
        ui.mouse = false;
        ui.frames(with(kPadA), 10);
        assert(overlay::popup::controller_nav());
        assert(ui.pressed.empty());
        ui.frames(none(), 2);
        ui.frame(with(kPadA));
        ui.frames(none(), 2);
        assert(ui.pressed == "Cancel");
    }
    {  // A held in the game while the Settings open (the navigation turns on with A down): no press until let go
        Ui ui;
        ui.frames(none(), 3);
        ui.open = true;
        ui.frames(none(), 3);  // the dialog is up with Cancel selected
        for (int i = 0; i < 3; i++) ui.frame(with(kPadA), false);  // the overlay's navigation off (the game has A)
        for (int i = 0; i < 10; i++) ui.frame(with(kPadA), true);
        assert(ui.pressed.empty());
        ui.frames(none(), 2);
        ui.frame(with(kPadA));
        ui.frames(none(), 2);
        assert(ui.pressed == "Cancel");
    }
    {  // the right stick scrolls the body; the selection stays
        Ui ui;
        ui.frames(none(), 3);
        ui.open = true;
        ui.frames(none(), 3);
        assert(ui.body_max > 0 && ui.body_scroll == 0);
        ui.frames(with(kPadRSDown, 1.f), 20);
        const float down = ui.body_scroll;
        assert(down > 0);
        ui.frames(with(kPadRSDown, 0.5f), 10);
        const float half = ui.body_scroll - down;
        assert(half > 0 && half < down / 2);  // squared: half tilt is a quarter of the speed
        ui.frames(with(kPadRSUp, 1.f), 200);
        assert(ui.body_scroll == 0);
        assert(GImGui->NavId == ui.cancel_id && ui.pressed.empty());
    }
    {  // the mouse has the navigation: no selection shown, no stick scrolling
        Ui ui;
        ui.mouse = true;
        ui.frames(none(), 3);
        ui.open = true;
        ui.frames(none(), 3);
        assert(!overlay::popup::controller_nav());
        assert(!GImGui->NavCursorVisible);
        ui.frames(with(kPadRSDown, 1.f), 1);  // (the stick is no navigation press: the mouse keeps it)
        ui.frames(with(kPadRSDown, 1.f), 10);
        assert(ui.body_scroll == 0);
    }
    std::puts("popup_pad_test: dialogs with a controller passed");
    return 0;
}
