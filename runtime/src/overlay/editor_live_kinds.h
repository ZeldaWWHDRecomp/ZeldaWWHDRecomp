// The Inspector's "takes effect after a restart" note: whether the running game shows changes to an actor's
// kind at once, from list_actor_types' "live" answers. Pure (no ImGui, no game), unit-tested in
// runtime/tools/editor_test.cpp.
#pragma once
#include <map>
#include <string>

#include "../mods/mod_json.h"

namespace editor_live_kinds {

using Value = mods::json::Value;
using Live = std::map<std::string, bool>;

// the "live" answer of each type in a list_actor_types reply (the whole reply, kept alive by the caller)
inline Live read(const Value& reply) {
    Live out;
    for (const auto& t : reply.get("result").array) out[t.get("type").string()] = t.get("live").boolean;
    return out;
}
// the list_actor_types type of a placed actor (a mod model's kind from its combat / carry parts)
inline std::string type_of(const Value& a) {
    if (a.get("kind").string() == "mod") return a.get("combat").type != Value::Null ? "model_enemy" : a.get("carry").type != Value::Null ? "model_carry" : "model";
    return a.get("actor").string();
}
// whether changes to `a` wait for a restart. Without an answer for its type (not asked yet) the start-up-only
// kinds are assumed: native enemies, props, switches, doors, mod enemies and carried props.
inline bool restart_only(const Value& a, const Live& live, bool native_enemy, bool native_prop) {
    const std::string type = type_of(a);
    const auto it = live.find(type);
    if (it != live.end()) return !it->second;
    return native_enemy || native_prop || type == "switch" || type == "door" || type == "model_enemy" || type == "model_carry";
}

}  // namespace editor_live_kinds
