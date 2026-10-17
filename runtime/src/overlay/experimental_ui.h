#include "tooltip.h"
// Experimental mods in the Mods tab: their label and the "Save backups" section (list, Back up now,
// Restore at next start). The warning before switching one on is part of the mod's review (mods_tab.h),
// whose first step backs up the saves. Same ImGui navigation as the other overlay dialogs; the host
// supplies controller B.
#pragma once
#include "../mods/packages.h"
#include "../mods/save_backup.h"
#include "imgui.h"
#include "popup.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace overlay::experimental {
inline const ImVec4 kBadgeColour(1.0f, 0.62f, 0.2f, 1.0f);
inline constexpr const char* kBadge = "Experimental";
inline void badge() {
    ImGui::SameLine();
    ImGui::TextColored(kBadgeColour, "%s", kBadge);
}

// ---- Save backups section
struct Backups {
    std::string message;        // last action's result
    std::string restore_choice; // backup name awaiting the restore confirmation
    bool open_restore = false;
    std::string delete_choice;  // backup name awaiting the delete confirmation
    bool open_delete = false;
};
inline Backups backups;

// Description of the staged restore for the restart notice, or empty.
inline std::string staged_restore() {
    std::lock_guard guard(mods::saves::mutex());
    auto& store = mods::saves::store();
    auto name = store.pending();
    if (name.empty()) return {};
    for (const auto& info : store.list())
        if (info.name == name) return info.when() + " (" + info.description() + ")";
    return name;
}

// Returns true when controller B was used by the restore dialog.
inline bool backups_section(bool back) {
    using namespace mods::saves;
    std::lock_guard guard(mutex());
    auto& s = store();
    bool b_used = false;
    if (!startup_notice().empty()) {
        ImGui::TextWrapped("%s", startup_notice().c_str());
        if (ImGui::Button("Dismiss##restore_notice")) startup_notice().clear();
    }
    if (!s.configured()) {
        ImGui::TextDisabled("Save backups are unavailable: the save folder is unknown.");
        return false;
    }
    ImGui::TextDisabled("Enabling an experimental mod backs up your saves first. The newest %zu automatic backups are "
                        "kept; backups made with Back up now are never deleted.", kKeepAutomatic);
    ImGui::TextDisabled("Backups folder: %s", s.root().string().c_str());
    if (ImGui::Button("Back up now")) {
        auto result = s.create("manual");
        backups.message = result.ok ? (result.created ? result.message : std::string("No save data yet; nothing to back up."))
                                    : result.error;
    }
    if (!backups.message.empty()) ImGui::TextWrapped("%s", backups.message.c_str());
    auto staged = s.pending();
    const auto& list = s.list();
    if (!staged.empty()) {
        std::string what = staged;
        for (const auto& info : list) if (info.name == staged) what = info.when() + " (" + info.description() + ")";
        ImGui::TextColored(kBadgeColour, "Restore staged: %s. It is applied at the next start, before the game loads "
                           "your saves.", what.c_str());
        if (ImGui::Button("Cancel restore")) {
            std::string error;
            backups.message = s.cancel_restore(error) ? "Restore cancelled." : error;
        }
    }
    if (list.empty()) ImGui::TextDisabled("No save backups yet.");
    else if (ImGui::BeginTable("save_backups", 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("When");
        ImGui::TableSetupColumn("Reason");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const auto& info : list) {
            ImGui::PushID(info.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.when().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted((info.description() + (info.automatic ? "" : " (kept)")).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(size_text(info.bytes).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(info.name == staged);
            if (ImGui::SmallButton("Restore")) { backups.restore_choice = info.name; backups.open_restore = true; }
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(info.name == staged);
            if (ImGui::SmallButton("Delete")) { backups.delete_choice = info.name; backups.open_delete = true; }
            ImGui::EndDisabled();
            if (info.name == staged && overlay::tooltip_hovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Staged for restore. Cancel the restore to delete it.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // restore confirmation
    const bool open_restore = backups.open_restore;
    backups.open_restore = false;
    if (popup::begin("Restore save backup##restore_confirm", open_restore)) {
        std::string what = backups.restore_choice;
        for (const auto& info : list) if (info.name == backups.restore_choice) what = info.when() + " (" + info.description() + ")";
        ImGui::PushTextWrapPos(popup::wrap_pos(30));
        ImGui::TextWrapped("Restore your saves from the backup of %s?", what.c_str());
        ImGui::TextWrapped("The game has its save files open while it runs, so the restore happens at the next start, "
                           "before the game loads any save. Anything you save until then is replaced.");
        ImGui::TextWrapped("Your current saves are backed up first (\"Before a restore\"), so you can undo this.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        bool accept = ImGui::Button("Restore at next start");
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(120, 0));
        ImGui::SetItemDefaultFocus();
        if (popup::dismissed(back)) { cancel = true; accept = false; b_used = back; }  // the X, a tap outside, Esc or B
        if (accept) {
            std::string error;
            backups.message = s.restore(backups.restore_choice, error)
                                  ? "Restore staged. Restart the game to apply it."
                                  : error;
        }
        if (accept || cancel) { backups.restore_choice.clear(); ImGui::CloseCurrentPopup(); }
        popup::end();
    }
    // delete confirmation
    const bool open_delete = backups.open_delete;
    backups.open_delete = false;
    if (popup::begin("Delete save backup##delete_confirm", open_delete)) {
        std::string what = backups.delete_choice;
        for (const auto& info : s.list()) if (info.name == backups.delete_choice) what = info.when() + " (" + info.description() + ")";
        ImGui::PushTextWrapPos(popup::wrap_pos(30));
        ImGui::TextWrapped("Backup of %s", what.c_str());
        ImGui::TextWrapped("Delete this backup? This can't be undone.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        bool accept = ImGui::Button("Delete", ImVec2(120, 0));
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(120, 0));
        ImGui::SetItemDefaultFocus();  // keyboard and controller start on Cancel
        if (popup::dismissed(back)) { cancel = true; accept = false; b_used = b_used || back; }  // the X, a tap outside, Esc or B
        if (accept) {
            std::string error;
            backups.message = s.remove(backups.delete_choice, error) ? "Backup deleted." : error;
        }
        if (accept || cancel) { backups.delete_choice.clear(); ImGui::CloseCurrentPopup(); }
        popup::end();
    }
    return b_used;
}
}  // namespace overlay::experimental
