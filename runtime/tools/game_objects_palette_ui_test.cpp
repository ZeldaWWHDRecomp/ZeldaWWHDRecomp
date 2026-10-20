// Offscreen "Game objects" palette and Inspector picker (overlay/game_objects_palette.h): the categories, the
// search field, a click on an object picks its type, an object the player's game files don't have is greyed out
// and can't be picked, and the Inspector picker's turn slider gives one rotate_actor when it is let go.
// No window, game or game files.
#include "overlay/game_objects_palette.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using overlay::game_objects::Value;

int main(int argc, char** argv) {
    SDL_Surface* surface = SDL_CreateSurface(960, 640, SDL_PIXELFORMAT_RGBA32); assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface); assert(renderer);
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(960, 640); io.DeltaTime = 1.f / 60;
    assert(ImGui_ImplSDLRenderer3_Init(renderer));
    const Value list = mods::json::parse(R"({"catalogue": 1, "categories": ["Furniture", "Trees and plants", "Buildings"], "objects": [
        {"type": "game:table", "id": "table", "label": "Wooden table", "category": "Furniture", "description": "a table or a stool", "available": false, "why": "not available in your game files", "live": true},
        {"type": "game:palm", "id": "palm", "label": "Palm tree", "category": "Trees and plants", "description": "a palm that sways", "available": true, "live": true},
        {"type": "game:tree-large", "id": "tree-large", "label": "Large tree", "category": "Trees and plants", "description": "a leafy tree", "available": true, "live": false},
        {"type": "piece:outset-house-small", "id": "outset-house-small", "label": "Small Outset house", "category": "Buildings", "description": "a small island house", "available": null, "live": true, "building": true, "preparing": true}]})");
    overlay::game_objects::Palette palette;
    std::string picked;
    auto frame = [&](const char* name = nullptr) {
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(600, 520));
        ImGui::Begin("Palette");
        const std::string type = overlay::game_objects::draw(palette, list);
        if (!type.empty()) picked = type;
        ImGui::End(); ImGui::Render(); SDL_SetRenderDrawColor(renderer, 20, 23, 29, 255); SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer); SDL_RenderPresent(renderer);
        if (name && argc >= 2) {
            std::filesystem::create_directories(argv[1]);
            assert(SDL_SaveBMP(surface, (std::filesystem::path(argv[1]) / (std::string(name) + ".bmp")).string().c_str()));
        }
    };
    auto click = [&](ImVec2 at) {
        io.AddMousePosEvent(at.x, at.y); frame();
        io.AddMouseButtonEvent(0, true); frame(); io.AddMouseButtonEvent(0, false); frame();
        io.AddMousePosEvent(-100, -100); frame();
    };
    for (int i = 0; i < 3; i++) frame();
    frame("game-objects-palette");
    // every object, by category
    assert(palette.shown.size() == 4 && palette.shown.count("game:palm") && palette.shown.count("game:table"));
    // an island building being prepared stays listed (and can be picked) under Buildings
    assert(palette.shown.count("piece:outset-house-small") && palette.shown["game:tree-large"].y < palette.shown["piece:outset-house-small"].y);
    assert(palette.shown["game:table"].y < palette.shown["game:palm"].y && palette.shown["game:palm"].y < palette.shown["game:tree-large"].y);
    // a click picks it
    click(palette.shown["game:palm"]);
    assert(picked == "game:palm");
    // not in the player's game files: greyed out, nothing picked
    picked.clear();
    click(palette.shown["game:table"]);
    assert(picked.empty());
    // the search field narrows the list; the category filter too
    std::snprintf(palette.query, sizeof palette.query, "%s", "LEAFY");
    frame();
    assert(palette.shown.size() == 1 && palette.shown.count("game:tree-large"));
    palette.query[0] = 0;
    palette.category = "Furniture";
    frame();
    assert(palette.shown.size() == 1 && palette.shown.count("game:table"));
    palette.category = "Nothing";
    frame();
    assert(palette.shown.empty());
    palette.category.clear();

    // the Inspector picker: no command without input; a turn slider dragged and let go gives one rotate_actor
    const Value info = mods::json::parse(R"({"type": "game:table", "description": "a table", "collision": true, "scale": {"min": 1, "max": 1},
        "variants": [{"id": "table", "label": "Table"}, {"id": "stool", "label": "Stool"}]})");
    const Value actor = mods::json::parse(R"({"id": "t", "actor": "game:table", "variant": "stool", "rotation": {"x": 0, "y": 90, "z": 0}})");
    overlay::game_objects::Picker picker;
    std::vector<std::pair<std::string, Value>> commands;
    auto inspect = [&] {
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(400, 400));
        ImGui::Begin("Inspector");
        for (auto& c : overlay::game_objects::inspector(picker, actor, info)) commands.push_back(c);
        ImGui::End(); ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    };
    for (int i = 0; i < 3; i++) inspect();
    assert(commands.empty() && picker.yaw == 90 && picker.id == "t");
    // the slider sits below "Variant", its combo and "Turn (degrees)": find it by hovering down the window
    ImGuiWindow* w = ImGui::FindWindowByName("Inspector");
    assert(w);
    const ImGuiID yaw_id = w->GetID("##game_object_yaw");
    float y = w->Pos.y + 20;
    for (; y < w->Pos.y + 300; y += 4) {
        io.AddMousePosEvent(w->Pos.x + 30, y); inspect();
        if (GImGui->HoveredId == yaw_id) break;
    }
    assert(GImGui->HoveredId == yaw_id);
    io.AddMouseButtonEvent(0, true); inspect();
    io.AddMousePosEvent(w->Pos.x + 200, y); inspect(); inspect();
    assert(commands.empty());  // held: nothing yet
    io.AddMouseButtonEvent(0, false); inspect(); inspect();
    assert(commands.size() == 1 && commands[0].first == "rotate_actor" && commands[0].second.get("id").string() == "t");
    const Value& rot = commands[0].second.get("rotation");
    assert(rot.get("x").number == 0 && rot.get("z").number == 0 && rot.get("y").number > 90 && rot.get("y").number <= 360);
    std::cout << "game_objects_palette_ui_test: ok\n";
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
    return 0;
}
