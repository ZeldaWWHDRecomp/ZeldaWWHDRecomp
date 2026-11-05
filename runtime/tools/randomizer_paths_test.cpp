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
    // setup's Play button, launchers and shortcuts pass the install's own save folder: still a seed of its own
    for (const char* launcher : {"save", "save/", "/data/save", "/data/save/", "C:/Games/WWHD/data/save"}) {
        assert(!randomizer::explicit_save_folder(true, launcher));
        const fs::path seed_save = randomizer::save_directory(launcher, randomizer::explicit_save_folder(true, launcher));
        assert(seed_save.filename() == "save-" + seed);
    }
    assert(fs::path(randomizer::save_directory("/data/save", randomizer::explicit_save_folder(true, "/data/save"))) ==
           fs::path("/data/save-" + seed));
    // a folder the player names is used as it is; without --save there is nothing explicit
    assert(randomizer::explicit_save_folder(true, "/cloud/wwhd"));
    assert(randomizer::save_directory("/cloud/wwhd", randomizer::explicit_save_folder(true, "/cloud/wwhd")) == "/cloud/wwhd");
    assert(!randomizer::explicit_save_folder(false, "/cloud/wwhd"));
    // an in-app seed's changed files (setup --randomize): found by the build's own identity
    assert(randomizer::overlay_directory("data") == fs::path("data") / "randomizer" / "seeds" / std::string(16, 'a') / "out");
    assert(randomizer::overlay_directory("").empty());
    assert(randomizer::game_rpx("game") == "game/code/cking.rpx");
    randomizer::overlay_root() = "seed/out";
    assert(randomizer::game_rpx("game") == "seed/out/code/cking.rpx");
}
