// SPDX-License-Identifier: MPL-2.0
#include "randomizer_identity.h"
#include <cassert>
extern "C" {
extern const char g_guest_build_title_id[] = "0005000010143599";
extern const char g_guest_build_rpx_sha256[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
}
int main() {
    const std::string seed = "randomizer-" + std::string(64, 'a');
    assert(randomizer::valid_identity());
    assert(randomizer::save_directory("data/save") == "data/save-" + seed);
    assert(randomizer::save_directory("data/save/") == "data/save-" + seed);
    assert(randomizer::save_directory("data/save-" + seed) == "data/save-" + seed);
    assert(randomizer::save_directory("custom/exact/", true) == "custom/exact/");
    assert(randomizer::seed_directory("states") == "states/" + seed);
    assert(randomizer::seed_directory("states/" + seed) == "states/" + seed);
    assert(randomizer::config_directory("config") == "config/" + seed);
    assert(randomizer_paths::backups("data/save/", seed) == "data/save-backups-" + seed);
    assert(randomizer_paths::backups("data/save", "") == "data/save-backups");
    assert(!randomizer_paths::seed_name("randomizer-"));
    assert(!randomizer_paths::seed_name(seed + "b"));
}
