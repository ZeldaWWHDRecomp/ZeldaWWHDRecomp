#pragma once
#include "guest_addr.h"
#include "randomizer_paths.h"
#include <cstring>
#include <filesystem>
#include <string>

namespace randomizer {
inline bool compiled_seed() {
    return !strcmp(g_guest_build_title_id, "0005000010143599");
}
inline bool valid_identity() {
    const std::string hash = g_guest_build_rpx_sha256;
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}
// Whether a start's --save names a folder of the player's own. Setup's Play button, launchers and shortcuts all
// pass the install's default folder ("--save save", "--save <data>/save"): that is no choice, and a randomized
// game keeps its own folder beside it then (save-randomizer-<sha>). Any other folder is used as it is.
inline bool explicit_save_folder(bool save_arg, const std::string& dir) {
    if (!save_arg) return false;
    std::filesystem::path p = std::filesystem::path(dir).lexically_normal();
    if (!p.has_filename()) p = p.parent_path();
    return p.filename() != "save";
}
// Explicit --save is handled by the caller and must never be rewritten.
inline std::string save_directory(const std::string& root, bool explicit_save = false) {
    if (explicit_save || !compiled_seed()) return root;
    return randomizer_paths::sibling(root, std::string("randomizer-") + g_guest_build_rpx_sha256).string();
}
// Existing state-slot namespaces remain in place; moving saves does not move states.
inline std::string seed_directory(const std::string& root) {
    if (!compiled_seed()) return root;
    const std::string seed = std::string("randomizer-") + g_guest_build_rpx_sha256;
    const std::filesystem::path path(root);
    return path.filename() == seed ? root : (path / seed).string();
}
// An in-app seed (setup --randomize) keeps only its changed files, in the install folder's
// randomizer/seeds/<first 16 hex digits of its RPX SHA-256>/out; "" for other builds.
inline std::filesystem::path overlay_directory(const std::filesystem::path& install_dir) {
    if (!compiled_seed() || !valid_identity() || install_dir.empty()) return {};
    return install_dir / "randomizer" / "seeds" / std::string(g_guest_build_rpx_sha256).substr(0, 16) / "out";
}
// The overlay this run plays (set once at startup, before the game reads files); "" none.
inline std::string& overlay_root() {
    static std::string root;
    return root;
}
inline std::string game_rpx(const std::string& game_dir) {
    return overlay_root().empty() ? game_dir + "/code/cking.rpx" : overlay_root() + "/code/cking.rpx";
}
// Only the per-seed folder preference lives here; general settings stay shared.
inline std::string config_directory(const std::string& root) {
    return seed_directory(root);
}
}
