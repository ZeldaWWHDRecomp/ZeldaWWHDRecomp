// Settings dialogs (overlay/popup.h) and Android's Settings sizes (overlay/mobile_ui.h), offscreen with real
// ImGui frames; no window, audio or game files.
//  - The Gyro aiming dialog (overlay/gyro_popup.h) closes by its X, a tap outside, Esc, B and its Close
//    button; a tap inside keeps it. On a phone held either way and on desktop it stays on screen and its
//    Close stays reachable below the scrolling options.
//  - A list opened inside a dialog takes the first tap outside and the first Esc (it closes, the dialog stays).
//  - The Quit dialog (overlay/quit_dialog.h) closes the same way: the X and a tap outside answer Cancel.
// Optional argument: a folder for BMP pictures of Settings > Controls and the Gyro dialog at phone, handheld
// and desktop sizes (the screen's pixels; the layout as the game picks it for that screen).
#include "overlay/gyro_popup.h"
#include "overlay/quit_dialog.h"
#include "overlay/controls_options.h"
#include "overlay/mobile_ui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>

// the game's log and clock, which the gyro code reports to (no game here)
void log_msg(const char*, ...) {}
namespace mods { double game_time() { return 0; } }

namespace {
struct Case {
    const char* name;
    int w, h;       // the screen (or desktop window) in pixels
    float dpi;      // Android: the screen's DPI; desktop: 0 (backing scale 2, a Retina Mac)
};

struct Ui {
    Case c;
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    mobile_ui::Layout layout;
    bool android = false;
    float scale = 1;

    explicit Ui(const Case& cs) : c(cs) {
        android = c.dpi > 0;
        if (android) layout = mobile_ui::layout(c.dpi, c.dpi / 160, mobile_ui::kAutomatic, float(c.w), float(c.h));
        scale = android ? layout.scale : mobile_ui::desktop_scale(2, float(c.h));
        surface = SDL_CreateSurface(c.w, c.h, SDL_PIXELFORMAT_RGBA32);
        assert(surface);
        renderer = SDL_CreateSoftwareRenderer(surface);
        assert(renderer);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = ImVec2(c.w / scale, c.h / scale);
        SDL_SetRenderScale(renderer, scale, scale);  // the picture at the screen's pixels, the layout in layout pixels
        io.DeltaTime = 1.f / 60;
        // the overlay's style (overlay.cpp setup_style; Android's touch sizes from the layout)
        ImGui::StyleColorsDark();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 10; s.ChildRounding = 6; s.FrameRounding = 5; s.PopupRounding = 6; s.GrabRounding = 5;
        s.WindowPadding = ImVec2(16, 14); s.FramePadding = ImVec2(9, 5); s.ItemSpacing = ImVec2(10, 7);
        s.WindowBorderSize = 1; s.FrameBorderSize = 1; s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        if (android) mobile_ui::apply_touch_style(s, layout);
        ImVec4* col = s.Colors;
        col[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.07f, 0.12f, 0.975f);
        col[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.11f, 0.18f, 0.98f);
        col[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.27f, 0.38f, 1.0f);
        col[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.22f, 0.32f, 0.85f);
        col[ImGuiCol_Button] = ImVec4(0.12f, 0.36f, 0.48f, 0.90f);
        col[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.95f, 0.85f, 1.0f);
        const char* fonts[] = {"/System/Library/Fonts/Supplemental/Arial.ttf", "C:/Windows/Fonts/arial.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};
        bool font = false;
        for (const char* f : fonts)
            if (!font && std::filesystem::exists(f)) font = io.Fonts->AddFontFromFileTTF(f, mobile_ui::kText) != nullptr;
        if (!font) { ImFontConfig cfg; cfg.SizePixels = mobile_ui::kText; io.Fonts->AddFontDefaultVector(&cfg); }
        s.FontSizeBase = mobile_ui::kText;
        assert(ImGui_ImplSDLRenderer3_Init(renderer));
    }
    ~Ui() {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
    void frame(const std::function<void()>& draw) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 23, 29, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    void save(const char* dir, const std::string& what) {
        if (!dir) return;
        std::filesystem::create_directories(dir);
        const auto path = std::filesystem::path(dir) / (std::string(c.name) + "-" + what + ".bmp");
        assert(SDL_SaveBMP(surface, path.string().c_str()));
    }
};

ImGuiWindow* window(const char* name) { return ImGui::FindWindowByName(name); }
// a dialog is open (between frames: by its window's name)
bool up(const char* title) {
    for (const ImGuiPopupData& p : ImGui::GetCurrentContext()->OpenPopupStack)
        if (p.Window && !strcmp(p.Window->Name, title)) return true;
    return false;
}
bool gyro_up() { return up(overlay::gyro::kTitle); }

// Settings > Controls as overlay.cpp draws it (the controller drawing a stand-in), with the Gyro dialog
struct Settings {
    bool open_gyro = false, back = false, b_closed = false;
    motion::Settings gyro;
    void draw(const Ui& ui) {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const ImVec2 size = ui.android ? ImVec2(ds.x * 0.98f, ds.y * 0.97f)
                                       : ImVec2(std::min(1240.0f, ds.x * 0.98f), std::min(720.0f, ds.y * 0.97f));
        ImGui::SetNextWindowPos(ImVec2(ds.x / 2, ds.y / 2), ImGuiCond_Always, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Wind Waker HD  -  Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            for (const char* t : {"Saves", "Graphics", "Display", "Audio", "Controls", "Shortcuts", "Mods", "About"})
                if (ImGui::BeginTabItem(t, nullptr, !strcmp(t, "Controls") ? ImGuiTabItemFlags_SetSelected : 0)) {
                    ImGui::BeginChild("page", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_NavFlattened);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Keyboard and controllers act as");
                    ImGui::SameLine();
                    ImGui::RadioButton("Wii U GamePad", true);
                    ImGui::SameLine();
                    ImGui::RadioButton("Pro Controller", false);
                    const float h = std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4);
                    ImGui::BeginChild("drawing", ImVec2(0, h), ImGuiChildFlags_Borders);
                    ImGui::TextDisabled("(controller drawing)");
                    ImGui::EndChild();
                    ImGui::TextUnformatted("Select a button on the controller or a chip to rebind it.");
                    overlay::controls_options::Values v;
                    v.phone_vibration = ui.android ? 1 : -1;
                    v.gyro_label = ui.android ? "Gyro: Device gyro (phone/tablet)..." : "Gyro: Controller gyro...";
                    if (overlay::controls_options::draw(v) & overlay::controls_options::kGyro) open_gyro = true;
                    ImGui::SetScrollHereY(1.0f);  // scrolled down to the options, where Gyro... is
                    ImGui::EndChild();
                    overlay::quit::bottom_row(ui.android, nullptr);
                    ImGui::EndTabItem();
                }
            ImGui::EndTabBar();
        }
        b_closed = overlay::gyro::dialog(open_gyro, gyro, back) || b_closed;
        open_gyro = back = false;
        ImGui::End();
    }
};

void click(Ui& ui, const std::function<void()>& draw, ImVec2 at) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y); ui.frame(draw);
    io.AddMouseButtonEvent(0, true); ui.frame(draw);
    io.AddMouseButtonEvent(0, false); ui.frame(draw);
    ui.frame(draw);
}
void key(Ui& ui, const std::function<void()>& draw, ImGuiKey k) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(k, true); ui.frame(draw);
    io.AddKeyEvent(k, false); ui.frame(draw);
    ui.frame(draw);
}
// a point outside the dialog: the screen's corner (over the Settings window or the game)
ImVec2 outside() { return ImVec2(4, ImGui::GetIO().DisplaySize.y - 4); }

void gyro_case(const Case& c, const char* dir) {
    Ui ui(c);
    Settings s;
    s.gyro.source = ui.android ? motion::kDevice : motion::kController;
    auto draw = [&] { s.draw(ui); };
    for (int i = 0; i < 4; ++i) ui.frame(draw);
    ui.save(dir, "1-controls");
    auto show = [&] {
        s.open_gyro = true;
        ui.frame(draw);
        // a tap right after it opened (a double tap's second) does not close it
        click(ui, draw, outside());
        assert(gyro_up());
        for (int i = 0; i < 16; ++i) ui.frame(draw);  // a quarter second
        assert(gyro_up());
    };
    show();
    ui.save(dir, "2-gyro");
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGuiWindow* dlg = window(overlay::gyro::kTitle);
    assert(dlg);
    // on screen, whole
    const ImRect r = dlg->Rect();
    assert(r.Min.x >= 0 && r.Min.y >= 0 && r.Max.x <= ds.x + 0.5f && r.Max.y <= ds.y + 0.5f);
    // the body scrolls when the screen is too low for it; Close stays below it, on screen
    ImGuiWindow* body = nullptr;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->ParentWindow == dlg && (w->Flags & ImGuiWindowFlags_ChildWindow)) body = w;
    assert(body);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float close_y = body->Rect().Max.y + 1 + st.ItemSpacing.y + ImGui::GetFrameHeight() / 2;
    assert(close_y + ImGui::GetFrameHeight() / 2 <= r.Max.y + 0.5f);
    // help text on full lines: the dialog's text is at least 20 font sizes wide (or the screen's width)
    assert(body->Rect().GetWidth() >= std::min(ImGui::GetFontSize() * 20, ds.x * 0.8f));
    std::printf("%-28s scale %.2f, layout %4.0fx%-4.0f: dialog %4.0fx%-4.0f, body %s (scroll %.0f), control %.1f mm\n", c.name,
                ui.scale, ds.x, ds.y, r.GetWidth(), r.GetHeight(), body->ScrollMax.y > 0 ? "scrolls" : "fits   ", body->ScrollMax.y,
                ui.android ? ui.layout.control_mm(c.dpi) : 0.0f);
    // the X: a square as tall as the title bar at its right end
    const ImRect bar = dlg->TitleBarRect();
    click(ui, draw, ImVec2(bar.Max.x - bar.GetHeight() / 2, bar.GetCenter().y));
    assert(!gyro_up());
    // a tap outside
    show();
    click(ui, draw, outside());
    assert(!gyro_up());
    // a tap inside (on its text) keeps it
    show();
    click(ui, draw, ImVec2(dlg->Rect().Min.x + st.WindowPadding.x + 4, body->Rect().Min.y + 4));
    assert(gyro_up());
    // Esc
    key(ui, draw, ImGuiKey_Escape);
    assert(!gyro_up());
    // B (the host's controller back): closes it, and says so (the menu stays open)
    show();
    s.back = true;
    ui.frame(draw);
    ui.frame(draw);
    assert(!gyro_up() && s.b_closed);
    // Close, below the scrolling options
    show();
    click(ui, draw, ImVec2(dlg->Rect().Min.x + st.WindowPadding.x + ImGui::GetFontSize() * 2, close_y));
    assert(!gyro_up());
}

// A list inside a dialog: the first tap outside (or Esc) closes the list, the next one the dialog
void list_in_dialog() {
    Ui ui({"list", 1280, 752, 0});
    bool open = true, closed = false;
    ImRect combo;
    int choice = 0;
    auto draw = [&] {
        if (overlay::popup::begin("A dialog##list", open)) {
            if (ImGui::BeginCombo("Choice", choice ? "B" : "A")) {
                if (ImGui::Selectable("A")) choice = 0;
                if (ImGui::Selectable("B")) choice = 1;
                ImGui::EndCombo();
            }
            combo = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::TextUnformatted("Some text to make the dialog wider than the list.");
            if (ImGui::Button("Close") || overlay::popup::dismissed(false)) { ImGui::CloseCurrentPopup(); closed = true; }
            overlay::popup::end();
        }
        open = false;
    };
    auto dialog_up = [] { return up("A dialog##list"); };
    auto list_up = [] { return ImGui::GetCurrentContext()->OpenPopupStack.Size == 2; };
    for (int i = 0; i < 20; ++i) ui.frame(draw);  // past the quarter second after opening
    assert(dialog_up() && !list_up());
    click(ui, draw, combo.GetCenter());
    assert(list_up());
    click(ui, draw, outside());
    assert(dialog_up() && !list_up() && !closed);
    click(ui, draw, combo.GetCenter());
    assert(list_up());
    key(ui, draw, ImGuiKey_Escape);
    assert(dialog_up() && !list_up() && !closed);
    click(ui, draw, outside());
    assert(!dialog_up() && closed);
    std::printf("a list inside a dialog: the first tap outside and the first Esc close the list\n");
}

// The Quit dialog: the X and a tap outside answer Cancel, as Esc and B do
void quit_dialog() {
    Ui ui({"quit", 1280, 752, 0});
    using overlay::quit::Answer;
    bool ask = true;
    Answer last = Answer::None;
    auto draw = [&] {
        const Answer a = overlay::quit::dialog(ask, true, false);
        if (a != Answer::None) last = a;
        ask = false;
    };
    for (int i = 0; i < 20; ++i) ui.frame(draw);  // past the quarter second after opening
    assert(up(quitprompt::kTitle));
    const ImRect bar = window(quitprompt::kTitle)->TitleBarRect();
    click(ui, draw, ImVec2(bar.Max.x - bar.GetHeight() / 2, bar.GetCenter().y));
    assert(!up(quitprompt::kTitle) && last == Answer::Cancel);
    ask = true; last = Answer::None;
    for (int i = 0; i < 20; ++i) ui.frame(draw);  // past the quarter second after opening
    click(ui, draw, outside());
    assert(!up(quitprompt::kTitle) && last == Answer::Cancel);
    std::printf("Quit dialog: the X and a tap outside answer Cancel\n");
}
}  // namespace

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : nullptr;
    // phones at a phone's density (2400x1080 and 2340x1080 at about 400 DPI: OnePlus 8 Pro, Galaxy S25),
    // a 6" 16:9 handheld (AYN Thor, Odin 2), a tablet, and a desktop window on a Retina Mac
    const Case cases[] = {
        {"phone-2400x1080", 2400, 1080, 400},
        {"phone-1080x2400", 1080, 2400, 400},
        {"phone-2340x1080-s25", 2340, 1080, 416},
        {"handheld-1920x1080-6in", 1920, 1080, 367},
        {"tablet-2560x1600-11in", 2560, 1600, 274},
        {"desktop-1280x752-retina", 2560, 1504, 0},
    };
    for (const Case& c : cases) gyro_case(c, dir);
    list_in_dialog();
    quit_dialog();
    std::printf("settings_popup_ui: passed\n");
}
