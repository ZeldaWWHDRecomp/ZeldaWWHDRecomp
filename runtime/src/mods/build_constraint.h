// Optional exact compiled-game identity restriction, evaluated during metadata preflight.
#pragma once
#include "mod_json.h"
#include "guest_addr.h"
#include <set>
#include <stdexcept>
#include <string>

namespace mods::packages {
inline std::string game_build_constraint_problem(const json::Value& manifest, bool version_guard) {
    if (!manifest.object.contains("supported_game_builds")) return {};
    if (!version_guard)
        throw std::invalid_argument("supported_game_builds requires minimum_manager_version 1.8.0 or newer");
    const auto& builds = manifest.get("supported_game_builds");
    if (builds.type != json::Value::Array || builds.array.empty() || builds.array.size() > 16)
        throw std::invalid_argument("supported_game_builds must contain 1 to 16 exact build names");
    std::set<std::string> names;
    std::string supported;
    for (const auto& build : builds.array) {
        if (build.type != json::Value::String || build.text.empty() || build.text.size() > 64)
            throw std::invalid_argument("Invalid supported game build name");
        for (const unsigned char c : build.text)
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
                throw std::invalid_argument("Invalid supported game build name");
        if (!names.insert(build.text).second)
            throw std::invalid_argument("Duplicate supported game build name");
        if (!supported.empty()) supported += ", ";
        supported += build.text;
    }
    // This identity is emitted by the code generator into the linked runtime,
    // never read from a player's setting or the manifest being checked.
    if (names.contains(g_guest_build_name)) return {};
    return "This mod supports game builds " + supported + "; this runtime was built for " +
        std::string(g_guest_build_name);
}
} // namespace mods::packages
