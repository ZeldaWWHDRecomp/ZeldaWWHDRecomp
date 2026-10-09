// Transport-independent catalogue staging. The manager remains the installer/trust authority.
#pragma once
#include "catalogue_io.h"
#include "mod_archive.h"
#include <atomic>
#include <chrono>
#include <functional>

namespace mods::catalogue {
using Fetch=std::function<void(const std::string&,const std::filesystem::path&,uint64_t)>;
struct Loaded {
    Index index;
    std::filesystem::path fixture_root;
};
inline Loaded load(const std::string& source,const Fetch& fetch,const std::filesystem::path& temporary) {
    if(https_url(source)) {
        require(bool(fetch),"HTTPS catalogue transport is unavailable");
        fetch(source,temporary,2*1024*1024);
        return {parse(read_bounded(temporary,2*1024*1024)),{}};
    }
    require(source.find("://")==std::string::npos,"Catalogue URL must use HTTPS");
    auto file=std::filesystem::absolute(source);
    return {parse(read_bounded(file,2*1024*1024),true),std::filesystem::canonical(file.parent_path())};
}
inline void check_manifest(const Entry& entry,const json::Value& manifest) {
    require(manifest.type==json::Value::Object,"Package manifest is missing");
    require(manifest.get("id").string()==entry.id,"Package ID differs from catalogue");
    require(manifest.get("version").string()==entry.version,"Package version differs from catalogue");
    require(manifest.get("kind").string()==entry.kind,"Package kind differs from catalogue");
    require(steps(manifest.get("setup"))==entry.setup,"Package setup differs from catalogue");
    std::set<std::string> expected(entry.dependencies.begin(),entry.dependencies.end()),actual;
    const auto& deps=manifest.get("dependencies");
    require(deps.type==json::Value::Null||deps.type==json::Value::Array,"Invalid package dependencies");
    for(const auto& dep:deps.array)actual.insert(text(dep,"id",80));
    require(actual==expected,"Package dependencies differ from catalogue");
}
class StagedPackage {
    std::filesystem::path directory_;
public:
    StagedPackage(const Entry& entry,const Version& port,const std::string& build,const std::string& platform,
                  const std::filesystem::path& work,const std::filesystem::path& fixtures,const Fetch& fetch) {
        namespace fs=std::filesystem;
        require(entry.compatible(port,build,platform),"This mod is incompatible with the installed port, game build or platform");
        const auto& download=entry.downloads.at(entry.downloads.contains(platform)?platform:"all");
        static std::atomic<uint64_t> serial{0};
        auto nonce=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++);
        fs::create_directories(work);require(!fs::is_symlink(work),"Catalogue work folder is a symlink");
        directory_=work/(".catalogue-"+nonce);
        require(fs::create_directory(directory_),"Cannot create catalogue work folder");
        try {
            auto archive=directory_/"package.zip";
            if(https_url(download.url)) {
                require(bool(fetch),"HTTPS package transport is unavailable");fetch(download.url,archive,download.size);
            }else {
                require(!fixtures.empty(),"Local packages require a local catalogue");
                auto source=confined_file(fixtures,download.url);verify_package(source,download);
                fs::copy_file(source,archive);
            }
            verify_package(archive,download);
            mods::archive::stage(archive,directory_/"package");
            check_manifest(entry,json::parse(read_bounded(path()/"manifest.json",1024*1024)));
        }catch(...){std::error_code error;fs::remove_all(directory_,error);throw;}
    }
    StagedPackage(const StagedPackage&)=delete;
    StagedPackage& operator=(const StagedPackage&)=delete;
    ~StagedPackage(){std::error_code error;std::filesystem::remove_all(directory_,error);}
    std::filesystem::path path()const{return directory_/"package";}
};
} // namespace mods::catalogue
