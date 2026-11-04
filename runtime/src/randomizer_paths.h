// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <filesystem>
#include <string>

namespace randomizer_paths {
inline bool seed_name(const std::string& name) {
    return name.size() == 75 && name.rfind("randomizer-", 0) == 0 &&
           name.find_first_not_of("0123456789abcdef", 11) == std::string::npos;
}

// Vanilla launches must not inspect unrelated experimental folders or permissions.
inline bool legacy_preflight_required(bool randomized, bool warm_shaders) {
    return randomized && !warm_shaders;
}

// Siblings keep every vanilla copy/restore/cloud operation outside seed data.
inline std::filesystem::path sibling(const std::filesystem::path& root, const std::string& seed) {
    if (seed.empty()) return root;
    auto clean = root.lexically_normal();
    if (!clean.has_filename()) clean = clean.parent_path();
    const std::string suffix = "-" + seed, name = clean.filename().string();
    if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        return clean;
    return clean.parent_path() / (name + suffix);
}

inline std::filesystem::path backups(const std::filesystem::path& save, const std::string& seed) {
    auto clean = save.lexically_normal();
    if (!clean.has_filename()) clean = clean.parent_path();
    return sibling(clean.parent_path() / "save-backups", seed);
}

// Fail closed rather than silently start empty or move undownloaded cloud data.
inline bool legacy_children(const std::filesystem::path& root, std::string& why,
                            const std::filesystem::path& target_root = {}) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const bool exists = fs::exists(root, ec);
    auto failed = [&] {
        why = "Cannot inspect save folder " + root.string() + ": " + ec.message();
        return false;
    };
    if (ec) return failed();
    if (!exists) return true;
    fs::directory_iterator it(root, ec), end;
    if (ec) return failed();
    for (; it != end; it.increment(ec)) {
        if (ec) return failed();
        const std::string name = it->path().filename().string();
        if (!seed_name(name)) continue;
        why = "An older randomized save or backup is still inside a vanilla folder: " + it->path().string() +
              ". Close the game and move it to the separate sibling folder " +
              sibling(target_root.empty() ? root : target_root, name).string() +
              " before continuing. Do not merge or overwrite an existing folder; both copies have been left unchanged.";
        return false;
    }
    return ec ? failed() : true;
}

inline bool legacy_reference(const std::filesystem::path& path, std::string& why) {
    const std::string name = path.filename().string();
    if (!seed_name(name)) return true;
    // Refuse even after the player moved the folder: the remembered choice must
    // not make a new empty save at its old location.
    why = "An older save-folder choice still points to " + path.string() + ". Move that folder to " +
          sibling(path.parent_path(), name).string() +
          " if it has not already been moved, and update the saved folder choice before continuing. "
          "No files or folder choices have been changed.";
    return false;
}
} // namespace randomizer_paths
