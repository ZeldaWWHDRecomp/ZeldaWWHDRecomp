// Offscreen Settings > Controls options (overlay/controls_options.h) at phone and desktop sizes: in a
// phone's window (2376x1080 or 2340x1080 at density 450, as on the testers' phones) every option stays
// inside the page (Phone vibration and Gyro were cut off past the right edge) and the hint keeps a
// readable width; on desktop the options keep their two lines. No window, audio or game files.
// Optional argument: a folder for BMP pictures of each size.
#include "overlay/controls_options.h"
#include "overlay/mobile_ui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace overlay::controls_options;

struct Case { const char* name; int w, h; bool phone; float scale; };

static void run(const Case& c, const char* dir) {
    // the layout in logical units (the overlay's DisplaySize); the picture is at that size too
    const int lw = int(c.w / c.scale), lh = int(c.h / c.scale);
    SDL_Surface* surface = SDL_CreateSurface(lw, lh, SDL_PIXELFORMAT_RGBA32); assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface); assert(renderer);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(float(lw), float(lh));
    io.DeltaTime = 1.f / 60;
    // the overlay's metrics (overlay.cpp setup_style / setup_fonts): 17 px text, Android's touch-sized frames
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(16, 14); s.FramePadding = ImVec2(9, 5); s.ItemSpacing = ImVec2(10, 7);
    s.FrameBorderSize = 1; s.WindowRounding = 10; s.FrameRounding = 5;
    if (c.phone) { s.FramePadding = ImVec2(10, 16); s.ItemInnerSpacing = ImVec2(10, 8); s.ScrollbarSize = 49; s.GrabMinSize = 49; }
    const char* fonts[] = {"/System/Library/Fonts/Supplemental/Arial.ttf", "C:/Windows/Fonts/arial.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    bool font = false;
    for (const char* f : fonts) if (!font && std::filesystem::exists(f)) font = io.Fonts->AddFontFromFileTTF(f, 17.0f) != nullptr;
    if (!font) { ImFontConfig cfg; cfg.SizePixels = 17.0f; io.Fonts->AddFontDefaultVector(&cfg); }
    s.FontSizeBase = 17.0f;
    assert(ImGui_ImplSDLRenderer3_Init(renderer));

    Values v;
    v.phone_vibration = c.phone ? 1 : -1;
    v.gyro_label = c.phone ? "Gyro: Device gyro (phone/tablet)..." : "Gyro: Controller gyro...";
    Layout layout;
    ImRect page;
    float options_height = 0;
    for (int frame = 0; frame < 5; ++frame) {
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui::NewFrame();
        const ImVec2 ds = io.DisplaySize;
        // the Settings window as overlay.cpp sizes it for the Controls tab
        const ImVec2 size = c.phone ? ImVec2(ds.x * 0.98f, ds.y * 0.97f)
                                    : ImVec2(std::min(1240.0f, ds.x * 0.98f), std::min(720.0f, ds.y * 0.97f));
        ImGui::SetNextWindowPos(ImVec2(ds.x / 2, ds.y / 2), ImGuiCond_Always, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Wind Waker HD  -  Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            for (const char* t : {"Saves", "Graphics", "Display", "Audio", "Controls", "Shortcuts", "Mods", "About"})
                if (ImGui::BeginTabItem(t, nullptr, !strcmp(t, "Controls") ? ImGuiTabItemFlags_SetSelected : 0)) {
                    ImGui::BeginChild("page", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_NavFlattened);
                    page = ImGui::GetCurrentWindow()->Rect();
                    page.Max.x -= ImGui::GetCurrentWindow()->ScrollbarSizes.x;
                    // the tab's top rows and the drawing (stand-ins), then the status line and the options
                    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Keyboard and controllers act as");
                    const float bottom = ImGui::GetTextLineHeightWithSpacing() + std::max(options_height, ImGui::GetFrameHeightWithSpacing() * 2) + 4;
                    const float h = std::max(160.0f, ImGui::GetContentRegionAvail().y - bottom);
                    ImGui::BeginChild("drawing", ImVec2(0, h), ImGuiChildFlags_Borders); ImGui::TextDisabled("(controller drawing / list)"); ImGui::EndChild();
                    ImGui::TextUnformatted("Select a button on the controller or a chip to rebind it.");
                    const float top = ImGui::GetCursorPosY();
                    draw(v, &layout);
                    options_height = ImGui::GetCursorPosY() - top;
                    ImGui::SetScrollHereY(1.0f);  // the page scrolled down to the options (as a player would)
                    ImGui::EndChild();
                    ImGui::Button("Quit Game");
                    ImGui::EndTabItem();
                }
            ImGui::EndTabBar();
        }
        ImGui::End(); ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 23, 29, 255); SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer); SDL_RenderPresent(renderer);
    }
    // every option inside the page: nothing past the right edge, where there is no horizontal scrolling
    const float font_size = ImGui::GetFontSize();
    int lines = 0; float line_bottom = -1;  // an item starting below the line's lowest item opens a line
    for (int i = 0; i < kItemCount; ++i) {
        if (!layout.drawn[i]) { assert(i == kItemPhone && !c.phone); continue; }
        assert(layout.min[i].x >= page.Min.x - 0.5f && layout.max[i].x <= page.Max.x + 0.5f);
        if (layout.min[i].y >= line_bottom - 1) ++lines, line_bottom = layout.max[i].y;
        else line_bottom = std::max(line_bottom, layout.max[i].y);
    }
    if (getenv("DUMP")) for (int i = 0; i < kItemCount; ++i) std::printf("  item %d: %.0f,%.0f - %.0f,%.0f\n", i, layout.min[i].x, layout.min[i].y, layout.max[i].x, layout.max[i].y);
    // the hint reads as text: at least 18 font sizes wide, or the whole line when the page is narrower
    const float hint = layout.max[kItemHint].x - layout.min[kItemHint].x;
    assert(hint >= std::min(font_size * 18, page.GetWidth() - 40) - 1);
    std::printf("%-26s logical %4.0fx%-4.0f page %4.0f wide: %d lines of options, hint %3.0f px wide\n", c.name,
                io.DisplaySize.x, io.DisplaySize.y, page.GetWidth(), lines, hint);
    if (!c.phone && io.DisplaySize.x >= 1240) {
        // desktop as before: dead zone .. reset on one line; the stick ranges and the hint on the next
        for (Item i : {kItemInvert, kItemRumble, kItemGyro, kItemReset}) assert(layout.min[i].y < layout.max[kItemDeadzone].y);
        for (Item i : {kItemRangeRight, kItemHint}) assert(layout.min[i].y < layout.max[kItemRangeLeft].y);
        assert(layout.min[kItemRangeLeft].y >= layout.max[kItemDeadzone].y);
        assert(lines == 2);
    }
    if (c.phone) assert(lines >= 3);  // the options stack
    if (dir) {
        std::filesystem::create_directories(dir);
        assert(SDL_SaveBMP(surface, (std::filesystem::path(dir) / (std::string(c.name) + ".bmp")).string().c_str()));
    }
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
}

int main(int argc, char** argv) {
    const float phone = mobile_ui::scale(450, 450 / 160.0f, 0);  // density 450, Automatic UI size
    const Case cases[] = {
        {"phone-2376x1080", 2376, 1080, true, phone},
        {"phone-2340x1080", 2340, 1080, true, phone},
        {"phone-portrait-1080x2376", 1080, 2376, true, phone},
        {"phone-2376-wide-tall-window", 2376, 2700, true, phone},  // the same width, tall enough to show every option
        {"desktop-1920x1080", 1920, 1080, false, std::max(1.0f, 1080 / 820.0f)},
        {"desktop-2560x1440-hidpi", 2560, 1440, false, 2.0f},
        {"desktop-1280x720", 1280, 720, false, 1.0f},
    };
    for (const Case& c : cases) run(c, argc > 1 ? argv[1] : nullptr);
}
