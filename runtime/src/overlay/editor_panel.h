// The world editor's Editor tab of the settings overlay, shown only in the editor launch mode (--editor,
// docs/mod-editor.md). Everything it changes goes through the command layer (editor/commands.h).
#pragma once

namespace editor_ui {
void register_tab();  // main thread, at start-up (extensions.h): the tab shows only with --editor
void draw();          // the tab's page (render thread)
}  // namespace editor_ui
