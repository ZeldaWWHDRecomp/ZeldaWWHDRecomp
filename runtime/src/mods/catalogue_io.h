// Bounded local reads and package verification, shared by fixtures and HTTPS downloads.
#pragma once
#include "catalogue_schema.h"
#include "mod_hash.h"
#include <filesystem>
#include <fstream>

namespace mods::catalogue {
inline std::string read_bounded(const std::filesystem::path& path,size_t limit) {
    require(!std::filesystem::is_symlink(path)&&std::filesystem::is_regular_file(path),"Catalogue file is missing or is a symlink");
    require(std::filesystem::file_size(path)<=limit,"Catalogue file exceeds size limit");
    std::ifstream input(path,std::ios::binary);require(bool(input),"Cannot open catalogue file");
    std::string result;char buffer[8192];
    while(input) {
        input.read(buffer,sizeof buffer);size_t bytes=size_t(input.gcount());
        require(bytes<=limit-result.size(),"Catalogue file exceeds size limit");result.append(buffer,bytes);
    }
    require(input.eof(),"Cannot read catalogue file");return result;
}
inline std::filesystem::path confined_file(const std::filesystem::path& root,const std::string& relative) {
    require(relative_file(relative),"Invalid catalogue-relative path");
    auto base=std::filesystem::canonical(root),candidate=base;
    // Reject every symlink component, including links which happen to point back inside the root.
    for(const auto& part:std::filesystem::path(relative)) {
        candidate/=part;require(!std::filesystem::is_symlink(candidate),"Catalogue-relative symlink refused");
    }
    require(std::filesystem::is_regular_file(candidate),"Catalogue package is missing");
    auto resolved=std::filesystem::canonical(candidate);
    auto r=resolved.begin(),b=base.begin();
    for(;b!=base.end();++b,++r)require(r!=resolved.end()&&*r==*b,"Catalogue package escaped its folder");
    return resolved;
}
inline void verify_package(const std::filesystem::path& file,const Download& metadata) {
    require(!std::filesystem::is_symlink(file)&&std::filesystem::is_regular_file(file),"Downloaded package is missing");
    require(std::filesystem::file_size(file)==metadata.size,"Downloaded package size differs from catalogue");
    require(hash::sha256_file(file)==metadata.sha256,"Downloaded package SHA-256 differs from catalogue");
}
} // namespace mods::catalogue
