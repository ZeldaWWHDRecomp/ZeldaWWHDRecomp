// The world editor's window stack in the launch mode (editor_shell.cpp): the picture's window at the back,
// the dockspace with the docked panels over it. Dear ImGui only, so editor_dock_ui_test drives it without
// the game.
#pragma once
#include "imgui.h"

namespace editor_dock {
// Panels share this class: only they dock into the editor's dockspace (Settings and the other overlays never).
const ImGuiWindowClass& panel_class();
// Before each panel's Begin: it may dock with the other panels
void panel();
// The default layout: the toolbar along the top, the Outliner at the left, the Inspector (with Flags and
// Search as tabs) at the right, Quests, Scenes, Problems & export, Camera & test and Log as tabs along the bottom.
void default_layout(ImGuiID id, ImVec2 size);
// The picture's window (labels, cylinders, handles, clicks into the world), the whole display: Begin it
// (the caller Ends it). It is kept the back-most window every frame, so the dockspace host and the panels
// docked into it are always in front of it and get the pointer; the dockspace's open middle lets the
// pointer through to it.
void begin_picture(float w, float h);
// The dockspace over the picture, `size` from the top left (above the status bar), after begin_picture and
// before the panels. `reset_layout` (Reset layout, first start) applies the default layout and is cleared.
void dockspace(ImVec2 size, bool& reset_layout);
}  // namespace editor_dock
