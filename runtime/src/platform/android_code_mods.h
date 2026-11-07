// Code mods on Android: the app's Java side (WwhdActivity) prepares the guest mod build tools and runs
// the setup's rebuild of the game code. Only the Android build defines these (android_code_mods.cpp).
#pragma once
#include <string>
namespace host::android {
// Java lease/recovery support must be available before save_profile can activate.
bool save_profile_preparation_available();
// After native save selection, before any recovery/cloud/backup operation.
void prepare_game_save(const std::string& absolute_directory);
// guest-sdk.json for this app installation (AndroidGuestTools.java); raises with a message for the player
std::string guest_build_tools();
// where built guest modules are kept: the app's private storage (Android loads no library from the
// shared files folder, where the mod manager lives)
std::string guest_module_cache();
// Settings > Mods > Use code mods: "" when the setup's rebuild started, else a message for the player
std::string request_code_mod_build(bool enabled);
// the rebuild's progress as JSON: state ("building", "ready", "failed"), stage, done, total, error
std::string code_mod_build_status();
// Settings > Randomizer (randomizer_job.cpp): "" when the phone's setup can make randomized games, else why not
std::string randomizer_unavailable();
// a randomized game made or imported by the phone's setup, request as JSON ({import, preset, seed, permalink}; an
// import opens the folder picker first): "" when it started, else a message for the player
std::string request_randomizer(const std::string& request);
// its progress as JSON: state ("picking", "building", "ready", "failed", "cancelled"), stage, done, total, error,
// seed, hash, imported
std::string randomizer_status();
}
