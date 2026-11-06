// SPDX-License-Identifier: MPL-2.0
// Online co-op's settings pages (coop_settings.cpp): Settings > Online, and the Online tab of the pause menu's page.
#pragma once

namespace mods::coop::settings_ui {
void register_tab();         // the Settings window's tab (main.cpp, at start)
void register_page_items();  // the pause menu page's items (game_settings::register_builtin)
}  // namespace mods::coop::settings_ui
