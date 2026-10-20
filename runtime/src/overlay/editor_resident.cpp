#include "editor_resident.h"

#include "imgui.h"
#include "../mods/story_resident_types.h"

namespace editor_resident {

const std::vector<Type>& types() {
    // the shared villager table: "Code: name" (the code as the manifest writes it, capitalised)
    static const std::vector<std::string> labels = [] {
        std::vector<std::string> out;
        for (const auto& t : story_resident_types::kTypes) {
            std::string code = t.code;
            code[0] = char(code[0] - 'a' + 'A');
            std::string name = t.label;
            if (!name.empty()) name[0] = char(name[0] >= 'A' && name[0] <= 'Z' ? name[0] - 'A' + 'a' : name[0]);
            out.push_back(code + ": " + name);
        }
        return out;
    }();
    static const std::vector<Type> list = [] {
        std::vector<Type> out;
        size_t i = 0;
        for (const auto& t : story_resident_types::kTypes) out.push_back({t.code, labels[i++].c_str(), t.help});
        return out;
    }();
    return list;
}

std::string label_of(const std::string& code) {
    for (const auto& t : types())
        if (code == t.code) return t.label;
    return code;
}

bool combo(const char* id, const std::string& current, std::string& chosen) {
    bool picked = false;
    const std::string shown = label_of(current);
    if (ImGui::BeginCombo(id, shown.c_str())) {
        for (const auto& t : types()) {
            const bool selected = current == t.code;
            if (ImGui::Selectable(t.label, selected) && !selected) chosen = t.code, picked = true;
            if (ImGui::IsItemHovered() && t.help) ImGui::SetTooltip("%s", t.help);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return picked;
}

bool menu(const char* label, std::string& chosen) {
    bool picked = false;
    if (ImGui::BeginMenu(label)) {
        for (const auto& t : types())
        {
            if (ImGui::MenuItem(t.label)) chosen = t.code, picked = true;
            if (ImGui::IsItemHovered() && t.help) ImGui::SetTooltip("%s", t.help);
        }
        ImGui::EndMenu();
    }
    return picked;
}

}  // namespace editor_resident
