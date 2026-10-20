// SPDX-License-Identifier: MPL-2.0
// Mod stages: the stage cache's files (mod_stages_files.h).
#include "mod_stages_files.h"

#include <cstring>
#include <map>
#include <mutex>

namespace mod_stages {
namespace {
std::mutex g_mutex;
std::map<std::string, std::string> g_files;
}  // namespace

void register_file(const std::string& file_name, const std::string& path) {
    std::lock_guard lock(g_mutex);
    g_files[file_name] = path;
}

std::string cached_file(const std::string& file_name) {
    std::lock_guard lock(g_mutex);
    auto it = g_files.find(file_name);
    return it == g_files.end() ? std::string() : it->second;
}

std::string archive_for(const std::string& guest_path) {
    constexpr const char* prefix = "/vol/content/Common/Stage/";
    if (guest_path.rfind(prefix, 0) != 0) return {};
    return cached_file(guest_path.substr(strlen(prefix)));
}

}  // namespace mod_stages
