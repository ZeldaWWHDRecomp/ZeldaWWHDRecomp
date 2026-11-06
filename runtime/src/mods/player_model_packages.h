// SPDX-License-Identifier: MPL-2.0
// GameCube player model packages (mod manager kind "player-model"): a player model pack the player has (an archive
// or folder with a Link.arc, made for the GameCube game) installed through Install from a file. The package keeps
// the pack's Link.arc (source/) and, once converted onto HD's Link (runtime/src/gcmodel, on a thread after the
// install), model/Link.szs and the conversion's report. It is not switched on like other mods: each save file
// chooses a model (or HD Link) and the game shows it at once (player_models.h).
//
// After GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), android/.../ModPacks.java (the import:
// every Link.arc in the archive is a model, its folder names it, metadata.txt "author:" credits it) and
// PlayerModels.java (a model per save file).
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace mods::player_model_packages {

inline constexpr const char* kKind = "player-model";
inline constexpr const char* kSource = "source/Link.arc";
inline constexpr const char* kModel = "model/Link.szs";
inline constexpr const char* kReport = "model/conversion.txt";
inline constexpr const char* kFailed = "model/failed.txt";  // the last conversion's error (not tried again until asked)

// the Link.arc files in an unpacked archive or folder (any case, no links), the shallowest first
std::vector<std::filesystem::path> find_arcs(const std::filesystem::path& tree);
// a package for the Link.arc at `arc` written into the empty folder `out`: manifest.json and source/ (Link.arc, the
// pack's metadata.txt when it has one); `fallback` names it when the Link.arc is at the archive's top. Its id.
std::string make_package(const std::filesystem::path& tree, const std::filesystem::path& arc, const std::filesystem::path& out,
                         const std::string& fallback);
// the package's model converted against the game's permanent_3d.pack: model/Link.szs and the report, or false and
// why (also written to model/failed.txt). progress(percent) now and then. Any thread; seconds.
bool convert(const std::filesystem::path& package, const std::filesystem::path& gamePack, const std::function<void(int)>& progress,
             std::string& error);
// the game's permanent_3d.pack under a game folder ("" none)
std::filesystem::path game_pack(const std::filesystem::path& game);

}  // namespace mods::player_model_packages
