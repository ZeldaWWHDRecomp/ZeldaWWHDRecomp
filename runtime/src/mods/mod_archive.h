#pragma once
#include <filesystem>
#include <string>
namespace mods::archive {
void stage(const std::filesystem::path& source,const std::filesystem::path& destination);
bool relative_path(const std::string& path);
}
