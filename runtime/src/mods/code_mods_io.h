// Build metadata is atomically replaced by setup while the game polls it (issue #282).
#pragma once
#include "mod_json.h"
#include "../platform/shared_read.h"
#include <filesystem>
#include <optional>
#include <string>

namespace mods::code {
constexpr size_t build_json_limit = 1024 * 1024;
// Configuration the game needs (guest-sdk.json, code-mods-active.json, ready.json, the final status):
// unavailable or damaged raises the message the player sees.
inline json::Value read_build_json(const std::filesystem::path& path) {
    std::string data;
    if (!host::read_shared(path, data, build_json_limit))
        exception_report::raise("Code-mod build configuration is unavailable; run setup again from a complete release folder");
    return json::parse(data);
}
// Progress polled every frame while setup runs: a poll that finds no publication yet returns nothing
// and the next frame polls again. Never raises.
inline std::optional<json::Value> poll_build_json(const std::filesystem::path& path) {
    std::string data;
    if (!host::read_shared(path, data, build_json_limit) || data.empty()) return std::nullopt;
    try { return json::parse(data); } catch (const std::exception&) { return std::nullopt; }
}
}
