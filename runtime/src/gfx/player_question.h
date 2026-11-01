// A question for the player in a native macOS alert (the AppKit host; player_question.mm), for
// main.cpp's ask_player when no SDL host is built: the save folder's questions at start-up
// (mods/save_folder.h) come before any window exists.
#pragma once
#include <string>
#include <vector>

namespace gfx {
// Shows an app-modal NSAlert on the main thread (from another thread it waits for the main thread
// to answer) and returns the chosen button's index, or -1 when no alert can be shown (hidden test
// windows, or another thread while the main thread doesn't run AppKit's loop).
// Test aid: WWHD_TEST_ASK_ANSWERS=a,b,... shows each alert and clicks button a, then b, ... after
// 2 s (WWHD_TEST_ASK_DELAY_MS overrides the wait), also with hidden windows.
int ask_player_alert(const std::string& title, const std::string& text, const std::vector<std::string>& buttons);
}  // namespace gfx
