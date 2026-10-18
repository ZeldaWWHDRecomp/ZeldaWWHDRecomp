// The Settings dialogs (popups): one way to close them, the same in each one.
//  - an X at the right of the title bar, as tall as the title bar (on a touch screen as large a target
//    as any button);
//  - a tap or click outside the dialog;
//  - Esc, and B (or Back) on a controller: the host passes it as `back`;
//  - the dialog's own Close, Cancel or Later button, which stays on screen: a long dialog scrolls its body
//    (body_begin / body_end) between the title bar and a footer with the buttons.
// A list opened inside a dialog (a combo) closes first: while one is open, or was open the frame before,
// a tap outside, Esc or B is the list's. Every dialog is centred and at most 96% x 94% of the screen.
// Kept free of the game so the offscreen tests (runtime/tools/settings_popup_ui_test.cpp) drive it.
#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>

namespace overlay::popup {
constexpr float kMaxWidth = 0.96f, kMaxHeight = 0.94f;  // of the screen
constexpr double kSettle = 0.25;  // seconds after opening before a tap outside closes the dialog

namespace detail {
// the dialog's own window (also from inside its body) and its state
inline ImGuiWindow* dialog() { return ImGui::GetCurrentWindow()->RootWindow; }
inline ImGuiID key(const char* what) { return dialog()->GetID(what); }
inline ImGuiStorage* state() { return &dialog()->StateStorage; }
// the X: a square as tall as the title bar at its right end; true when pressed
inline bool close_button() {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const ImRect bar = w->TitleBarRect();
    const float h = bar.GetHeight();
    const ImRect r(ImVec2(bar.Max.x - h, bar.Min.y), bar.Max);
    const ImGuiID id = w->GetID("#popup_close");
    ImGui::PushClipRect(bar.Min, bar.Max, false);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);  // B closes it on a controller; the cursor stays in the dialog
    bool pressed = false;
    if (ImGui::ItemAdd(r, id)) {
        bool hovered = false, held = false;
        pressed = ImGui::ButtonBehavior(r, id, &hovered, &held);
        ImDrawList* dl = w->DrawList;
        if (hovered || held)
            dl->AddRectFilled(r.Min, r.Max, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                              ImGui::GetStyle().FrameRounding);
        const ImVec2 c = r.GetCenter();
        const float e = ImGui::GetFontSize() * 0.32f, t = std::max(1.5f, ImGui::GetFontSize() * 0.11f);
        const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddLine(ImVec2(c.x - e, c.y - e), ImVec2(c.x + e, c.y + e), col, t);
        dl->AddLine(ImVec2(c.x + e, c.y - e), ImVec2(c.x - e, c.y + e), col, t);
    }
    ImGui::PopItemFlag();
    ImGui::PopClipRect();
    return pressed;
}
// a list or another popup opened from inside the dialog (it takes a tap outside, Esc and B first)
inline bool child_open() { return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId); }
}  // namespace detail

// The smallest width that shows the title beside the X.
inline float min_width(const char* title) {
    const ImGuiStyle& s = ImGui::GetStyle();
    const float bar = ImGui::GetFontSize() + s.FramePadding.y * 2;
    return ImGui::CalcTextSize(title, nullptr, true).x + 2 * (bar + s.FramePadding.x) + s.WindowPadding.x;
}

// Opens the dialog `title` when `open`, and begins it while it is open: true then (end() it after).
// least_width: the dialog's smallest width (it always fits its title and the X). closable false: a
// dialog that cannot be closed now (a step that has to finish first): no X, and dismissed() is false.
inline bool begin(const char* title, bool open, float least_width = 0, bool closable = true) {
    if (open) ImGui::OpenPopup(title);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(.5f, .5f));
    const ImVec2 most(vp->Size.x * kMaxWidth, vp->Size.y * kMaxHeight);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(std::max(min_width(title), least_width), most.x), 0), most);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_NoMove))
        return false;
    ImGuiStorage* st = detail::state();
    const bool x = closable && detail::close_button();
    // a list open now or the frame before has the tap, Esc and B (closing it); a tap in the dialog's first
    // quarter second (the second tap of a double tap that opened it) is not a tap outside it
    const bool child = st->GetBool(detail::key("#child_before")) || detail::child_open();
    if (ImGui::IsWindowAppearing()) st->SetFloat(detail::key("#opened"), float(ImGui::GetTime()));
    const bool settled = ImGui::GetTime() - st->GetFloat(detail::key("#opened")) >= kSettle;
    const bool outside = closable && !child && settled && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                         !ImGui::GetCurrentWindow()->Rect().Contains(ImGui::GetIO().MousePos);
    st->SetBool(detail::key("#x"), x);
    st->SetBool(detail::key("#outside"), outside);
    st->SetBool(detail::key("#child"), child);
    st->SetBool(detail::key("#closable"), closable);
    return true;
}

// Inside the dialog: the player closed it without an answer (the X, a tap outside, Esc, or `back`:
// the host's controller B). The caller treats it as its Close / Cancel / Later and closes the dialog.
inline bool dismissed(bool back) {
    ImGuiStorage* st = detail::state();
    if (st->GetBool(detail::key("#x")) || st->GetBool(detail::key("#outside"))) return true;
    if (!st->GetBool(detail::key("#closable")) || st->GetBool(detail::key("#child")) || detail::child_open()) return false;
    return back || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

inline void end() {
    detail::state()->SetBool(detail::key("#child_before"), detail::child_open());
    ImGui::EndPopup();
}

// A text wrap position (for ImGui::PushTextWrapPos) `ems` font sizes from here, at most what the screen
// leaves beside the dialog's padding and a scroll bar.
inline float wrap_pos(float ems) {
    const ImGuiStyle& s = ImGui::GetStyle();
    const float room = ImGui::GetMainViewport()->Size.x * kMaxWidth - 2 * s.WindowPadding.x - s.ScrollbarSize - s.ItemSpacing.x;
    return ImGui::GetCursorPosX() + std::max(ImGui::GetFontSize() * 8, std::min(ImGui::GetFontSize() * ems, room));
}

// The dialog's body, scrolling when it is taller than the screen leaves above a footer of `footer_rows`
// rows of buttons (drawn after body_end, always on screen).
inline void body_begin(int footer_rows = 1) {
    const ImGuiStyle& s = ImGui::GetStyle();
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    const float title = ImGui::GetFontSize() + s.FramePadding.y * 2;
    const float footer = footer_rows * ImGui::GetFrameHeightWithSpacing() + s.ItemSpacing.y + 1;  // + separator
    const float most_h = std::max(ImGui::GetFrameHeight() * 2, vp.y * kMaxHeight - title - 2 * s.WindowPadding.y - footer);
    const float most_w = std::max(ImGui::GetFontSize() * 8, vp.x * kMaxWidth - 2 * s.WindowPadding.x);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(most_w, most_h));
    ImGui::BeginChild("##popup_body", ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);
}
inline void body_end() {
    ImGui::EndChild();
    ImGui::Separator();
}
}  // namespace overlay::popup
