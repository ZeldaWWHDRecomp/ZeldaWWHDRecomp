#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace mods::code {
struct Status {
    bool requested=false, target=false, building=false, ready=false;
    unsigned done=0,total=0;
    std::string stage,error,exe;
    std::string mod; // package queued to be enabled after the rebuild ("" for none)
};
bool enabled();
const char* rebuild_duration();  // "about 5 minutes" (the phone: as long as its first setup)
// fixed_build: this executable is never replaced by a cached code-mod build (the decomp version)
void startup(int argc, char** argv, bool fixed_build = false);
bool restart(std::string& error);
// Starts this executable again with the arguments it was started with and ends this process (the world
// editor's Restart and return). False (and why) where the game can't start itself (Android).
bool relaunch(std::string& error);
void request(bool on, const std::string& pending_mod = {}); // confirmation is shown in the Mods page
Status status();
void begin();
void cancel();
void dismiss();
// startup(..., true): the decomp version (no code-mod builds, no randomized games)
bool fixed_build();
// The installed setup as a command (desktop): the Python and setup.py of guest-sdk.json with --data-dir; data_dir
// is set to the install folder (script: to setup.py). Raises with a message for the player when setup's tools
// are missing.
std::vector<std::string> setup_command(std::filesystem::path& data_dir, std::filesystem::path* script = nullptr);
}
