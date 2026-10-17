// Code mods on Android: the app's Java side (WwhdActivity) prepares the guest mod build tools and runs
// the setup's rebuild of the game code. Only the Android build defines these (android_code_mods.cpp).
#pragma once
#include <string>
namespace host::android {
// guest-sdk.json for this app installation (AndroidGuestTools.java); raises with a message for the player
std::string guest_build_tools();
// where built guest modules are kept: the app's private storage (Android loads no library from the
// shared files folder, where the mod manager lives)
std::string guest_module_cache();
// Settings > Mods > Use code mods: "" when the setup's rebuild started, else a message for the player
std::string request_code_mod_build(bool enabled);
// the rebuild's progress as JSON: state ("building", "ready", "failed"), stage, done, total, error
std::string code_mod_build_status();
}
