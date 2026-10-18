// The world editor's own overlays in the editor launch mode (--editor, docs/mod-editor.md): a toolbar, an
// outliner, an inspector, a status bar, and in the picture the placed actors' labels, the selected one's
// collision cylinder and move / turn handles. Clicking into the picture selects an actor, or places a new
// one on the ground there. Play hides them all but a small Stop button. Render thread, through the
// overlay's frame and input layers (extensions.h); every change is a command (editor/commands.h).
#pragma once

namespace editor_shell {
void register_layers();  // main thread, at start-up (from editor_ui::register_tab)
void show_inspector();   // render thread: bring the inspector to the front (a problem was clicked)
void show_problems();
void show_quests();      // bring the Quests window to the front
void show_scenes();      // bring the Scenes window to the front
}  // namespace editor_shell
