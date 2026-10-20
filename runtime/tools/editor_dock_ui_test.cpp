// The world editor's window stack (overlay/editor_dock.h) with the real Dear ImGui docking code and no game:
// the docked panels (toolbar, Outliner, Inspector) get the pointer (hover and clicks) although the picture's
// full-display window is made first each frame; the open middle of the dockspace still lets the pointer
// through to the picture. Before the fix the picture's window sat in front of the dockspace host and took
// every hover and click (the panels got no mouse at all).
#include "../src/overlay/editor_dock.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "imgui_internal.h"

namespace {
struct Frame {
    std::string hovered;  // the window under the pointer
    bool play = false, outliner_button = false, picture_hovered = false, picture_clicked = false;
};
bool reset_layout = true;

Frame frame(float w, float h, bool with_inspector = true) {
    Frame f;
    ImGui::NewFrame();
    editor_dock::begin_picture(w, h);
    f.picture_hovered = ImGui::IsWindowHovered();
    f.picture_clicked = f.picture_hovered && ImGui::IsMouseClicked(0);
    ImGui::End();
    editor_dock::dockspace(ImVec2(w, h - 40), reset_layout);
    editor_dock::panel();
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    f.play = ImGui::Button("Play");
    ImGui::End();
    editor_dock::panel();
    ImGui::Begin("Outliner");
    f.outliner_button = ImGui::Button("Actor");
    ImGui::End();
    if (with_inspector) {
        editor_dock::panel();
        ImGui::Begin("Inspector");
        ImGui::TextUnformatted("fields");
        ImGui::End();
    }
    // the status bar below the dockspace, as editor_shell.cpp makes it (after the panels)
    ImGui::SetNextWindowPos(ImVec2(0, h), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::Begin("##editor_status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::TextUnformatted("status");
    ImGui::End();
    ImGui::Render();
    const ImGuiWindow* hw = ImGui::GetCurrentContext()->HoveredWindow;
    f.hovered = hw ? hw->Name : "";
    return f;
}
ImVec2 centre_of(const char* window) {
    ImGuiWindow* w = ImGui::FindWindowByName(window);
    assert(w && w->DockNode && "the panel is docked");
    return ImVec2(w->InnerRect.Min.x + 20, w->InnerRect.Min.y + 10);
}
}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    const float w = io.DisplaySize.x, h = io.DisplaySize.y;
    for (int i = 0; i < 3; i++) frame(w, h);  // the default layout settles

    // over the toolbar's Play button: the toolbar has the pointer, a click presses Play
    const ImVec2 play = centre_of("Editor");
    io.AddMousePosEvent(play.x, play.y);
    Frame f = frame(w, h);
    std::printf("over the toolbar (%.0f, %.0f): %s\n", play.x, play.y, f.hovered.c_str());
    assert(f.hovered == "Editor");
    assert(!f.picture_hovered);
    io.AddMouseButtonEvent(0, true);
    f = frame(w, h);
    assert(!f.picture_clicked);
    io.AddMouseButtonEvent(0, false);
    f = frame(w, h);
    assert(f.play && "a click on the docked toolbar presses its button");

    // the Outliner's button
    const ImVec2 actor = centre_of("Outliner");
    io.AddMousePosEvent(actor.x, actor.y);
    f = frame(w, h);
    assert(f.hovered == "Outliner");
    io.AddMouseButtonEvent(0, true);
    frame(w, h);
    io.AddMouseButtonEvent(0, false);
    f = frame(w, h);
    assert(f.outliner_button);

    // the Inspector
    ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
    io.AddMousePosEvent(inspector->InnerRect.GetCenter().x, inspector->InnerRect.GetCenter().y);
    f = frame(w, h);
    assert(f.hovered == "Inspector");

    // the open middle of the dockspace: the picture gets the pointer and the click (select, place)
    ImGuiWindow* outliner = ImGui::FindWindowByName("Outliner");
    const ImVec2 middle((outliner->Rect().Max.x + inspector->Rect().Min.x) * 0.5f, h * 0.45f);
    io.AddMousePosEvent(middle.x, middle.y);
    f = frame(w, h);
    std::printf("over the picture (%.0f, %.0f): %s\n", middle.x, middle.y, f.hovered.c_str());
    assert(f.hovered == "##editor_world" && f.picture_hovered);
    io.AddMouseButtonEvent(0, true);
    f = frame(w, h);
    assert(f.picture_clicked);
    io.AddMouseButtonEvent(0, false);
    frame(w, h);

    // a panel shown again later (made after the picture and the dockspace) is in front as well
    frame(w, h, false);
    frame(w, h, false);
    frame(w, h);
    frame(w, h);
    io.AddMousePosEvent(inspector->InnerRect.GetCenter().x, inspector->InnerRect.GetCenter().y);
    f = frame(w, h);
    std::printf("over the Inspector shown again (%.0f, %.0f, docked %d): %s\n", inspector->InnerRect.GetCenter().x, inspector->InnerRect.GetCenter().y, inspector->DockNode != nullptr, f.hovered.c_str());
    assert(f.hovered == "Inspector");

    // Reset layout keeps the order
    reset_layout = true;
    frame(w, h);
    io.AddMousePosEvent(play.x, play.y);
    frame(w, h);
    f = frame(w, h);
    assert(f.hovered == "Editor");

    ImGui::DestroyContext();
    std::printf("editor_dock_ui_test: ok\n");
    return 0;
}
