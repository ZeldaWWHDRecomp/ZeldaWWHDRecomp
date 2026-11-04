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
// Only the per-seed folder preference lives here; general settings stay shared.
inline std::string config_directory(const std::string& root) {
    return seed_directory(root);
}
}
