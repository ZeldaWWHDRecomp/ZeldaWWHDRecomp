// SPDX-License-Identifier: MPL-2.0
// Mod stages: the stage cache's files the game reads (hle/fs.cpp serves them). A unit of its own, free of the
// game side, so the file layer and its tests need nothing else of mod_stages (mod_stages.h registers).
#pragma once
#include <string>

namespace mod_stages {

// a stage file name ("<name>_Stage.szs", "<name>_Room<n>.szs") -> the cached file
void register_file(const std::string& file_name, const std::string& path);
// The cached archive of a stage file name, or "" (the editor's placement index, warp).
std::string cached_file(const std::string& file_name);
// The stage cache's file for a game read of /vol/content/Common/Stage/<name>_..., or "" (not a mod stage's).
std::string archive_for(const std::string& guest_path);

}  // namespace mod_stages
