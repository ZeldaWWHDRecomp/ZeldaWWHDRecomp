// Settings > Quit Game (issue #231): a way to end the game from inside it, for a Steam Deck in Gaming
// Mode or an Android handheld played with a controller, where there is no window to close. The button
// sits in the Settings window's bottom row on every tab; it asks "Quit Wind Waker HD?" (the macOS quit
// prompt's wording, quit_prompt.h) with Cancel (first, in focus) and Quit, before the host quits the
// way closing the TV window (or the app menu's Quit) does (hostui::quit). Kept free of the game so
// runtime/tools/quit_menu_ui_test.cpp can drive it offscreen.
#pragma once
#include "../quit_prompt.h"
#include "imgui.h"
#include "popup.h"

namespace overlay::quit {
enum class Answer { None, Quit, Cancel };
constexpr const char* kButton = "Quit Game";
constexpr const char* kQuit = "Quit##quit_game";
constexpr const char* kCancel = "Cancel##quit_game";

// The Settings window's bottom row (the page above ends one row higher): Quit Game at the right; with
// close (Android: no window close button there) a wide Close Settings before it. Returns true when
// Quit Game was chosen; *close is set when Close Settings was.
inline bool bottom_row(bool with_close, bool* close) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(kButton).x + st.FramePadding.x * 2 + ImGui::GetFontSize() * 2;
    if (with_close) {
        if (ImGui::Button("Close Settings", ImVec2(-(w + st.ItemSpacing.x), 0)) && close) *close = true;
        ImGui::SameLine();
    } else {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
    }
    return ImGui::Button(kButton, ImVec2(w, 0));
}

// The confirmation. open: Quit Game was chosen this frame. in_game: a save file is being played (then it
// says that unsaved progress is lost). controller_back: B this frame (answers Cancel, as Esc does).
// forced: a test run's answer (WWHD_TEST_QUIT_ANSWER), given once the dialog is up.
inline Answer dialog(bool open, bool in_game, bool controller_back, Answer forced = Answer::None) {
    if (!popup::begin(quitprompt::kTitle, open)) return Answer::None;
    ImGui::PushTextWrapPos(popup::wrap_pos(26));
    if (in_game) ImGui::TextWrapped("%s", quitprompt::kProgressLost);
    else ImGui::TextWrapped("The game closes.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    auto answer = Answer::None;
    const ImVec2 size(ImGui::GetFontSize() * 7, 0);
    // Cancel first: keyboard and controller start on it (A or Enter right away keeps playing), and so does
    // a controller after the mouse or a touch (Dear ImGui then lands on the first button, not the default)
    if (ImGui::Button(kCancel, size)) answer = Answer::Cancel;
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(kQuit, size)) answer = Answer::Quit;
    if (popup::dismissed(controller_back)) answer = Answer::Cancel;  // the X, a tap outside, Esc or B
    if (answer == Answer::None) answer = forced;
    if (answer != Answer::None) ImGui::CloseCurrentPopup();
    popup::end();
    return answer;
}
}  // namespace overlay::quit
