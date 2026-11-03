#pragma once
#include "guest_addr.h"
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
// All launch paths, including direct executable launches, isolate ordinary saves and state slots.
// Keep settings and controls shared. The full RPX hash also separates identical seed names with
// different options or randomizer releases.
inline std::string seed_directory(const std::string& root) {
    if (!compiled_seed()) return root;
    const std::string name = std::string("randomizer-") + g_guest_build_rpx_sha256;
    const std::filesystem::path path(root);
    return path.filename() == name ? root : (path / name).string();
}
}
