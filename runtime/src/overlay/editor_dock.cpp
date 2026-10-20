// The world editor's window stack in the launch mode (editor_dock.h).
#include "editor_dock.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "imgui_internal.h"

namespace editor_dock {

// The editor's panels dock to the screen's edges and into each other as tabs (Dear ImGui's docking
// branch, turned on in the launch mode only: Settings and every other overlay never dock). The middle
// of the dockspace stays open: the picture, its labels and handles take the clicks there.
const ImGuiWindowClass& panel_class() {
    static const ImGuiWindowClass c = [] {
        ImGuiWindowClass k;
        k.ClassId = ImHashStr("wwhd-editor-panel");
        k.DockingAllowUnclassed = false;  // only the editor's panels dock here
        return k;
    }();
    return c;
}
void panel() { ImGui::SetNextWindowClass(&panel_class()); }

void default_layout(ImGuiID id, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, size);
    ImGuiID rest = id;
    const float bar = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().WindowPadding.y * 2 + 4;
    const ImGuiID top = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Up, std::clamp(bar / std::max(1.0f, size.y), 0.05f, 0.4f), nullptr, &rest);
    const ImGuiID left = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.22f, nullptr, &rest);
    const ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.3f, nullptr, &rest);
    // test aid: WWHD_TEST_EDITOR_WINDOWS=...,tallbottom gives the bottom windows (Scenes, Quests) more room (pictures)
    const char* test_windows = std::getenv("WWHD_TEST_EDITOR_WINDOWS");
    const float bottom_share = test_windows && std::strstr(test_windows, "tallbottom") ? 0.75f : 0.4f;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, bottom_share, nullptr, &rest);
    if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(top)) n->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::DockBuilderDockWindow("Editor", top);
    ImGui::DockBuilderDockWindow("Outliner", left);
    for (const char* name : {"Inspector", "Flags", "Search"}) ImGui::DockBuilderDockWindow(name, right);
    for (const char* name : {"Quests", "Scenes", "Problems & export", "Camera & test", "Log"}) ImGui::DockBuilderDockWindow(name, bottom);
    ImGui::DockBuilderFinish(id);
}

void begin_picture(float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##editor_world", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);
    // Dear ImGui puts a new NoBringToFrontOnFocus window at the back of the display order, so the dockspace
    // host (also NoBringToFrontOnFocus, made after this window in the same frame) would end up behind this
    // full-display window, with every docked panel: the picture would take every hover and click. Its place
    // is at the very back, whatever was made when.
    ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
}

void dockspace(ImVec2 size, bool& reset_layout) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##editor_dock", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(3);
    const ImGuiID id = ImGui::GetID("editor_dockspace");
    if (reset_layout || !ImGui::DockBuilderGetNode(id)) {  // first start, an old layout without docking, or Reset layout
        reset_layout = false;
        default_layout(id, size);
    }
    ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode, &panel_class());
    ImGui::End();
}

}  // namespace editor_dock
