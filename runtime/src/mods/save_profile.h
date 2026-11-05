// Declarative save ownership, selected from the trusted startup plan before save I/O.
#pragma once
#include "mod_json.h"
#include "save_profile_identity.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace mods::save_profile {
inline constexpr const char *kMinimumManagerVersion = "1.7.0";
class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
struct Declaration {
    bool present = false;
    std::string provider, identity, error;
};
inline Declaration parse(const json::Value &manifest, bool guest, bool version_guard) {
    Declaration out;
    out.present = manifest.object.contains("save_profile");
    if (!out.present)
        return out;
    const auto &value = manifest.get("save_profile");
    auto fail = [&](const char *text) {
        out.error = text;
        return out;
    };
    if (!guest)
        return fail("save_profile requires a guest package");
    if (!version_guard)
        return fail("save_profile requires minimum_manager_version 1.7.0 or newer");
    if (value.type != json::Value::Object)
        return fail("save_profile must be an object");
    for (const auto &[key, ignored] : value.object) {
        (void)ignored;
        if (key != "version" && key != "provider" && key != "identity")
            return fail("Unknown save_profile field");
    }
    if (value.get("version").type != json::Value::Number || value.get("version").number != 1)
        return fail("Unsupported save_profile version");
    if (value.get("provider").type != json::Value::String)
        return fail("save_profile provider must be a string");
    out.provider = value.get("provider").text;
    if (out.provider.empty() || out.provider.size() > 24 || out.provider.front() < 'a' ||
        out.provider.front() > 'z')
        return fail("save_profile provider must start with a lowercase letter and contain at most "
                    "24 characters");
    for (char c : out.provider)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return fail("save_profile provider accepts only lowercase letters, digits and hyphens");
    if (value.get("identity").type != json::Value::String)
        return fail("save_profile identity must be a string");
    out.identity = value.get("identity").text;
    if (out.identity.size() != 64)
        return fail("save_profile identity must be 64 lowercase hexadecimal characters");
    for (char c : out.identity)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return fail("save_profile identity must be 64 lowercase hexadecimal characters");
    return out;
}
inline std::filesystem::path named_path(const std::filesystem::path &path) {
    auto out = path.lexically_normal();
    if (!out.has_filename())
        out = out.parent_path();
    return out;
}
inline bool owned_basename(const std::filesystem::path &path, const std::string &key) {
    auto text = named_path(path).filename().string();
#if defined(_WIN32) || defined(__APPLE__)
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
#endif
    return text == key || text.ends_with("-" + key);
}
inline bool contains_path(const std::filesystem::path &parent, const std::filesystem::path &child) {
    auto p = parent.begin(), c = child.begin();
    for (; p != parent.end(); ++p, ++c) {
        if (c == child.end())
            return false;
        auto a = p->native(), b = c->native();
#if defined(_WIN32) || defined(__APPLE__)
        for (auto &value : a)
            if (value >= 'A' && value <= 'Z')
                value = value - 'A' + 'a';
        for (auto &value : b)
            if (value >= 'A' && value <= 'Z')
                value = value - 'A' + 'a';
#endif
        if (a != b)
            return false;
    }
    return true;
}
inline bool owned_location(const std::filesystem::path &path, const std::string &key,
                           const std::vector<std::filesystem::path> &protected_roots,
                           std::string &why) {
    if (key.empty())
        return true;
    if (!valid_namespace(key) || !owned_basename(path, key)) {
        why = "This folder does not belong to the selected save profile: " + path.string();
        return false;
    }
    std::error_code ec;
    const auto target = std::filesystem::weakly_canonical(named_path(path), ec);
    if (ec) {
        why = "Cannot verify the save profile folder: " + path.string() + ": " + ec.message();
        return false;
    }
    if (!owned_basename(target, key)) {
        why = "The save profile folder points to another folder: " + path.string();
        return false;
    }
    for (const auto &root : protected_roots) {
        const auto protected_path = std::filesystem::weakly_canonical(root, ec);
        if (ec) {
            why = "Cannot verify the normal save or backup folder: " + root.string() + ": " +
                  ec.message();
            return false;
        }
        if (contains_path(protected_path, target)) {
            why = "Choose a save profile folder outside the normal save and backup folders";
            return false;
        }
    }
    return true;
}
struct Claim {
    std::string owner, name, fingerprint;
    Declaration declaration;
};
struct Selection {
    std::string owner, name, provider, identity, fingerprint;
    bool empty() const { return owner.empty(); }
    std::string namespace_key() const {
        return empty() ? std::string() : "mod-" + provider + "-" + identity;
    }
    bool operator==(const Selection &) const = default;
};
// Only eligible, trusted startup-plan entries are supplied. Installed/skipped
// packages do not participate and cannot cause a save probe or activation error.
inline Selection select(const std::vector<Claim> &claims) {
    Selection out;
    for (const auto &claim : claims) {
        const auto &declaration = claim.declaration;
        if (!declaration.present)
            continue;
        if (!declaration.error.empty())
            throw Error(claim.name + ": " + declaration.error);
        if (!out.empty())
            throw Error("Only one save profile can be active: " + out.name + " and " + claim.name);
        if (!valid_owner(claim.owner) || !valid_hash(claim.fingerprint))
            throw Error("Save profile owner has no trusted package identity");
        out = {claim.owner, claim.name, declaration.provider, declaration.identity,
               claim.fingerprint};
    }
    return out;
}
struct Paths {
    std::filesystem::path save, backups, preferences;
    std::vector<std::filesystem::path> protected_roots;
    std::string namespace_key;
};
// The ordinary save folder a profile folder belongs to: --save save-<suffix> (the profile's own
// folder given again) names the same profile as --save save, so it doesn't nest a second suffix.
inline std::filesystem::path ordinary_base(const std::filesystem::path &path, const std::string &suffix) {
    auto base = path.lexically_normal();
    if (!base.has_filename())
        base = base.parent_path();
    if (!base.has_filename())
        throw Error("A mod save profile needs a named base save folder");
    const std::string name = base.filename().string(), tail = "-" + suffix;
    if (name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0)
        base = base.parent_path() / name.substr(0, name.size() - tail.size());
    return base;
}
inline std::filesystem::path sibling(const std::filesystem::path &path, const std::string &suffix) {
    const auto base = ordinary_base(path, suffix);
    return base.parent_path() / (base.filename().string() + "-" + suffix);
}
inline Paths paths(const Selection &selection, const std::filesystem::path &ordinary_save,
                   const std::filesystem::path &config, bool /*explicit_save*/) {
    Paths out;
    out.save = ordinary_save;
    out.preferences = config;
    if (selection.empty())
        return out; // Ordinary startup keeps its exact existing paths.
    out.namespace_key = selection.namespace_key();
    // --save is also supplied by official launchers. Always isolate a mod
    // profile from that ordinary save root, including explicit command lines.
    out.save = sibling(ordinary_save, out.namespace_key);
    const auto base = ordinary_base(ordinary_save, out.namespace_key);
    out.backups = base.parent_path() / ("save-backups-" + out.namespace_key);
    out.preferences = config / "save-profiles" / out.namespace_key;
    out.protected_roots = {base, base.parent_path() / "save-backups"};
    return out;
}
// Conservative Windows budget for the live write and backup snapshot/temporary
// paths, including the complete provider/hash namespace and collision suffixes.
inline constexpr size_t kWindowsPathLimit = 259;
inline bool windows_budget(size_t absolute_units, size_t tail) {
    return absolute_units <= kWindowsPathLimit && tail <= kWindowsPathLimit - absolute_units;
}
inline size_t path_units(const std::filesystem::path &path) {
#ifdef _WIN32
    return std::filesystem::absolute(path).native().size(); // UTF-16 code units, not UTF-8 bytes.
#else
    return std::filesystem::absolute(path)
        .string()
        .size(); // Native fixtures exercise the same bounds.
#endif
}
inline void check_budget(const Paths &paths, bool windows) {
    if (!windows || paths.namespace_key.empty())
        return;
    if (!windows_budget(path_units(paths.save), 48) ||
        !windows_budget(path_units(paths.backups), 96) ||
        !windows_budget(path_units(paths.preferences), 24))
        throw Error("The save profile path is too long for Windows saves and backups; choose a "
                    "shorter game or save location");
}
} // namespace mods::save_profile
