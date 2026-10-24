// An imported model's actions: Replace from file, Watch file, Remove (editor_model_actions.h).
#include "editor_model_actions.h"

#include <cfloat>

#include "imgui.h"

namespace editor_model_actions {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kBad(0.95f, 0.55f, 0.45f, 1);

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
// on the same line as the last item when `label` still fits there, else on the next (a narrow Outliner)
void same_line_if_fits(const char* label, float extra) {
    const float need = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2 + extra;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < need) ImGui::NewLine();
}
std::string join(const Value& list) {
    std::string s;
    for (const auto& v : list.array) s += (s.empty() ? "" : ", ") + v.string();
    return s;
}
}  // namespace

Value replace_params(const std::string& model, const std::string& file) {
    Value p;
    p["model"] = model;
    p["file"] = file;
    return p;
}
Value watch_params(const std::string& model, bool on, const std::string& file) {
    Value p;
    p["model"] = model;
    p["on"] = on;
    if (on && !file.empty()) p["file"] = file;
    return p;
}
Value remove_params(const std::string& model, bool with_placements) {
    Value p;
    p["model"] = model;
    p["confirm"] = true;
    if (with_placements) p["actors"] = true;
    return p;
}

void draw(State& s, const Value& model, const Value& files, const Run& run, const Choose& choose) {
    const std::string id = model.get("id").string();
    ImGui::PushID(id.c_str());
    if (ImGui::SmallButton("Replace from file...")) {
        choose([run, id](std::string path) { run("replace_model", replace_params(id, path), nullptr); });
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a new version of this model under the same name:\n"
                          "its placements keep their places and show it, collision included.");
    same_line_if_fits("Watch file", ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
    const std::string file = files.get("file").string();
    bool watching = files.get("watching").boolean;
    ImGui::BeginDisabled(file.empty() && !watching);
    if (ImGui::Checkbox("Watch file", &watching)) run("watch_model", watch_params(id, watching, file), nullptr);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(file.empty() ? "Import or replace the model from a file first: that file is the one watched."
                                       : "When the file changes on disk (saved again from Blender),\nthe model is replaced by itself.");
    same_line_if_fits("Remove", 0);
    if (ImGui::SmallButton("Remove")) {
        s.removing = id;
        ImGui::OpenPopup("Remove model");
    }
    if (!file.empty()) muted(std::string(watching ? "watching " : "from ") + file);
    // the confirmation: what goes with it, or why it can't go yet
    if (ImGui::BeginPopupModal("Remove model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Value& placements = files.get("placements");
        const Value& items = files.get("items");
        ImGui::Text("Remove the model %s from the mod?", id.c_str());
        if (!items.array.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kBad);
            ImGui::TextWrapped("%s", ("Held by the item" + std::string(items.array.size() > 1 ? "s " : " ") + join(items) +
                                      ": give " + (items.array.size() > 1 ? "them" : "it") + " another model first.").c_str());
            ImGui::PopStyleColor();
        } else if (!placements.array.empty()) {
            ImGui::TextWrapped("%s", ("Placements use it: " + join(placements) + ".").c_str());
            if (ImGui::Button("Remove those too")) {
                run("remove_model", remove_params(id, true), nullptr);
                s.removing.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        } else {
            muted("No placement uses it. Undo brings it back.");
            if (ImGui::Button("Remove")) {
                run("remove_model", remove_params(id, false), nullptr);
                s.removing.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) {
            s.removing.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

}  // namespace editor_model_actions
