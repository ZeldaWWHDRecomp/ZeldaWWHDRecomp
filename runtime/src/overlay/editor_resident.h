// The world editor's choice of a resident's type (the manifest's parameters.resident: one of the
// villagers of mods/story_resident_types.h): the Add menu's Resident submenu and the Inspector's Type field.
#pragma once

#include <string>
#include <vector>
#include <string>

namespace editor_resident {

struct Type {
    const char* code;   // parameters.resident
    const char* label;  // what it looks like, in words ("Uo2: the man from the Windfall bar")
    const char* help;   // more about it (a hover tip)
};
// the types a resident can have, in the order the menus show them
const std::vector<Type>& types();
// the label of `code` (the code itself for one the editor does not know)
std::string label_of(const std::string& code);
// a placed resident's type from its manifest entry's parameters.resident: a code, the legacy number 9
// (Um1), or none (the game's default, Um1)
template <class JsonValue>
std::string code_of(const JsonValue& actor) {
    const auto& r = actor.get("parameters").get("resident");
    if (r.type == JsonValue::String) return r.text;
    return "um1";
}
// The Inspector's field: a combo showing `current`; true with `chosen` when another type is picked.
bool combo(const char* id, const std::string& current, std::string& chosen);
// The Add menu's entry: a submenu `label` with one item per type; true with `chosen` when one is picked.
bool menu(const char* label, std::string& chosen);

}  // namespace editor_resident
