// SPDX-License-Identifier: MPL-2.0
#include "randomizer_identity.h"
#include <cassert>
extern "C" {
extern const char g_guest_build_title_id[] = "0005000010143599";
extern const char g_guest_build_rpx_sha256[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
}
int main() {
    namespace fs = std::filesystem;
    const std::string seed = "randomizer-" + std::string(64, 'a');
    assert(randomizer::valid_identity());
    assert(!randomizer_paths::legacy_preflight_required(false, false));
    assert(!randomizer_paths::legacy_preflight_required(false, true));
    assert(!randomizer_paths::legacy_preflight_required(true, true));
    assert(randomizer_paths::legacy_preflight_required(true, false));
    assert(fs::path(randomizer::save_directory("data/save")) == fs::path("data/save-" + seed));
    assert(fs::path(randomizer::save_directory("data/save/")) == fs::path("data/save-" + seed));
    assert(fs::path(randomizer::save_directory("data/save-" + seed)) == fs::path("data/save-" + seed));
    assert(randomizer::save_directory("custom/exact/", true) == "custom/exact/");
    assert(fs::path(randomizer::seed_directory("states")) == fs::path("states/" + seed));
    assert(fs::path(randomizer::seed_directory("states/" + seed)) == fs::path("states/" + seed));
    assert(fs::path(randomizer::config_directory("config")) == fs::path("config/" + seed));
    assert(randomizer_paths::backups("data/save/", seed) == "data/save-backups-" + seed);
    assert(randomizer_paths::backups("data/save", "") == "data/save-backups");
    assert(!randomizer_paths::seed_name("randomizer-"));
    assert(!randomizer_paths::seed_name(seed + "b"));
}
