// The world editor's resident type (overlay/editor_resident.h) with the real Dear ImGui code and no game:
// the Inspector's Type field shows a placed resident's type in words and a click picks another, and the
// Add menu's Resident submenu offers every villager type and gives the clicked one.
#include "../src/overlay/editor_resident.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/mods/mod_json.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace {
struct Frame {
    bool picked = false;
    std::string chosen;
};
std::string current = "uo2";
bool open_add = false;

// the Inspector's field, and the Add button's popup with the Resident submenu
Frame frame(bool inspector) {
    Frame f;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoDecoration);
    if (inspector) {
        ImGui::TextUnformatted("Type");
        ImGui::SetNextItemWidth(400);
        f.picked = editor_resident::combo("##resident_type", current, f.chosen);
    } else {
        if (open_add) ImGui::OpenPopup("add_menu"), open_add = false;
        if (ImGui::BeginPopup("add_menu")) {
            f.picked = editor_resident::menu("Resident", f.chosen);
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::Render();
    return f;
}
void click(ImVec2 at, bool inspector, Frame* out = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    frame(inspector);
    io.AddMouseButtonEvent(0, true);
    Frame a = frame(inspector);
    io.AddMouseButtonEvent(0, false);
    Frame b = frame(inspector);
    if (out) *out = a.picked ? a : b;
}
// the centre of the visible popup or combo list row whose text is `label`
ImVec2 row_of(const char* label) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : g.Windows) {
        if (!w->Active || !(w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu))) continue;
        // rows are laid out one per line from the window's content start
        const float line = ImGui::GetTextLineHeightWithSpacing();
        float y = w->ContentRegionRect.Min.y;
        for (const auto& t : editor_resident::types()) {
            if (std::strcmp(t.label, label) == 0 && w->ContentRegionRect.Max.y > y) return ImVec2(w->ContentRegionRect.Min.x + 30, y + line * 0.5f);
            y += line;
        }
    }
    return ImVec2(-1, -1);
}
}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    for (int i = 0; i < 2; i++) frame(true);

    // the manifest's parameters: a code, the legacy number or none (Um1)
    mods::json::Value a;
    a["parameters"]["resident"] = std::string("ym1");
    assert(editor_resident::code_of(a) == "ym1");
    mods::json::Value none;
    assert(editor_resident::code_of(none) == "um1");
    assert(editor_resident::label_of("uo2").find("Uo2") == 0 && editor_resident::label_of("zz9") == "zz9");

    // Inspector: the field shows the type in words; a click opens the list, a click on Um1 picks it
    ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
    assert(inspector);
    const ImVec2 field(inspector->ContentRegionRect.Min.x + 100, inspector->ContentRegionRect.Min.y + ImGui::GetTextLineHeightWithSpacing() + 8);
    Frame f;
    click(field, true, &f);
    assert(!f.picked && ImGui::IsPopupOpen("##ComboPopup_", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
    frame(true);  // the list lays out (an auto-sized popup is measured on its first frame)
    const ImVec2 um1 = row_of(editor_resident::types()[1].label);
    assert(um1.x > 0);
    click(um1, true, &f);
    std::printf("Inspector Type: picked %d -> %s\n", f.picked, f.chosen.c_str());
    assert(f.picked && f.chosen == "um1");
    // picking the shown type again changes nothing
    current = "um1";
    click(field, true, &f);
    frame(true);
    click(row_of(editor_resident::types()[1].label), true, &f);
    assert(!f.picked);

    // Add: the Resident submenu lists the three types; a click on the seated Ym1 gives it
    open_add = true;
    frame(false);
    frame(false);
    ImGuiWindow* popup = nullptr;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && (w->Flags & ImGuiWindowFlags_Popup)) popup = w;
    assert(popup && "the Add popup is open");
    const ImVec2 resident(popup->ContentRegionRect.Min.x + 20, popup->ContentRegionRect.Min.y + ImGui::GetTextLineHeight() * 0.5f);
    io.AddMousePosEvent(resident.x, resident.y);
    for (int i = 0; i < 4; i++) frame(false);  // hovering opens the submenu
    ImGuiWindow* sub = nullptr;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && (w->Flags & ImGuiWindowFlags_ChildMenu)) sub = w;
    assert(sub && "the Resident submenu is open");
    const float line = ImGui::GetTextLineHeightWithSpacing();
    size_t ym1_row = 0;
    while (std::strcmp(editor_resident::types()[ym1_row].code, "ym1") != 0) ym1_row++;
    const ImVec2 ym1(sub->ContentRegionRect.Min.x + 30, sub->ContentRegionRect.Min.y + line * float(ym1_row) + line * 0.5f);
    // move along the submenu's row first (as a person does), then click
    io.AddMousePosEvent(sub->ContentRegionRect.Min.x + 30, sub->ContentRegionRect.Min.y + line * 0.5f);
    frame(false);
    click(ym1, false, &f);
    std::printf("Add > Resident: picked %d -> %s\n", f.picked, f.chosen.c_str());
    assert(f.picked && f.chosen == "ym1");

    ImGui::DestroyContext();
    std::printf("editor_resident_ui_test: ok\n");
    return 0;
}
