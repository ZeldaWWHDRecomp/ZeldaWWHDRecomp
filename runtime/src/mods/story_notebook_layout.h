// The story notebook's page drawn as a game layout (local only, story mods are WIP): the Story Notebook
// package's "layout" (a layout description and its art, built into an archive on the player's machine,
// lyt_build.h) shown with the item screen through story_lyt_screen.h. The page itself stays
// story_notebook_page.h (input, state, sounds); this maps its state onto the layout's panes and plays the
// layout's animations: Open / Close (the window), TurnLeft / TurnRight (the content, on a tab change),
// Bump<n> (the selected row), Breathe (the cursor). Pane names: the layout description's
// (make_notebook_layout.py, kept with the package's art).
//
// Version 1 draws the list (tabs, rows, cursor, plates); the quest's detail view, the portraits and the
// mini chart stay on the HUD path. Without a layout, or while it isn't built (story_lyt_screen.h), the
// page is drawn on the HUD as before.
#pragma once
#include <string>
#include <vector>

#include "guest_hud.h"
#include "story_notebook_list.h"

namespace story_notebook {
class Page;

// The package's layout description (manifest "notebook": {"layout": ...}) and the folder for the built
// archive (the package's data folder); before install. WWHD_NOTEBOOK_LAYOUT=<description> overrides it.
void set_layout_description(const std::string& path, const std::string& data_dir);
// a quest's portrait (its package picture, as the notebook loaded it), false if it has none
bool portrait_pixels(uint16_t key, guestmods::hud::Image& out);
// the menu's floating (lifted) icon left out while it shows the notebook (the page opens over the bag)
void hide_floating_notebook_icon();
void restore_notebook_icons();
// The notebook drawn open (a PNG: the Story Notebook package's "notebook": {"icon_open"}, or
// WWHD_NOTEBOOK_ICON_OPEN): shown in its bag place while the page opens and closes. Before install.
void set_icon_open(const std::string& png_path);
const std::vector<uint8_t>& icon_open_png();  // empty: none
}  // namespace story_notebook

namespace story_notebook_layout {
// Builds the archive and makes the notebook story_lyt_screen's client (after the packages are loaded,
// before story_lyt_screen::install); false (logged) if there is no usable layout.
bool configure();
// The page's state this frame (story_notebook.cpp publish / publish_closing, game thread): true when the
// layout shows it (the HUD then draws none of the page, only its portraits)
bool update(const story_notebook::Page& page, const std::vector<story_notebook::Row>& all);
bool update_closed();  // the page is closed: true while the layout's closing animation still runs
void reset();  // discard a page even when no screen draw can finish its closing animation
bool drawing();        // the layout shows the page (or its closing) now
bool animating();      // the page's open or close animation runs now
}  // namespace story_notebook_layout
