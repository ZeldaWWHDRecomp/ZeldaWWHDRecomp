#include "mods/catalogue_client.h"
#include <cassert>
#include <zlib.h>

// Tiny stored ZIP fixture, generated entirely from authored synthetic strings.
static void zip(const std::filesystem::path& path,const std::string& name,const std::string& data) {
    std::vector<uint8_t> out;
    auto u16=[&](uint16_t value){out.push_back(value);out.push_back(value>>8);};
    auto u32=[&](uint32_t value){u16(value);u16(value>>16);};
    auto bytes=[&](const std::string& value){out.insert(out.end(),value.begin(),value.end());};
    uint32_t crc=uint32_t(crc32(0,reinterpret_cast<const Bytef*>(data.data()),uInt(data.size())));
    u32(0x04034b50);u16(20);u16(0);u16(0);u16(0);u16(0);u32(crc);
    u32(data.size());u32(data.size());u16(name.size());u16(0);bytes(name);bytes(data);
    uint32_t central=uint32_t(out.size());
    u32(0x02014b50);u16(20);u16(20);u16(0);u16(0);u16(0);u16(0);u32(crc);
    u32(data.size());u32(data.size());u16(name.size());u16(0);u16(0);u16(0);u16(0);u32(0);u32(0);bytes(name);
    uint32_t size=uint32_t(out.size())-central;
    u32(0x06054b50);u16(0);u16(0);u16(1);u16(1);u32(size);u32(central);u16(0);
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(out.data()),out.size());
}
template<class F>static void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}assert(failed);}
int main() {
    namespace fs=std::filesystem;using namespace mods::catalogue;
    auto root=fs::temp_directory_path()/("wwhd-client-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"fixtures");fs::create_directories(root/"work");
    try {
        Entry entry;entry.id="pilot";entry.name="Pilot";entry.version="1.0.0";entry.kind="settings";
        entry.minimum=Version::parse("0.2.10");auto version=entry.minimum;
        auto manifest=mods::json::parse(R"({"format_version":1,"id":"pilot","version":"1.0.0","kind":"settings"})");
        auto package=root/"fixtures"/"pilot.zip";
        auto pack=[&](const std::string& name="manifest.json") {
            zip(package,name,mods::json::dump(manifest));
            entry.downloads["all"]={"pilot.zip",mods::hash::sha256_file(package),fs::file_size(package)};
        };
        pack();fs::path staged;
        {StagedPackage ready(entry,version,"EU","linux-x86_64",root/"work",root/"fixtures",{});
         staged=ready.path();assert(fs::is_regular_file(staged/"manifest.json"));}
        assert(!fs::exists(staged)&&fs::is_empty(root/"work"));
        entry.downloads["all"].sha256[0]^=1;
        rejects([&]{StagedPackage bad(entry,version,"USA","all",root/"work",root/"fixtures",{});});
        assert(fs::is_empty(root/"work"));
        manifest["id"]="other";pack();
        rejects([&]{StagedPackage bad(entry,version,"USA","all",root/"work",root/"fixtures",{});});
        manifest["id"]="pilot";pack("../manifest.json");
        rejects([&]{StagedPackage bad(entry,version,"USA","all",root/"work",root/"fixtures",{});});
        assert(fs::is_empty(root/"work")&&!fs::exists(root/"manifest.json"));
        {std::ofstream out(root/"fixtures"/"index.json");out<<"{\"format_version\":1,\"mods\":[]}";}
        assert(load((root/"fixtures"/"index.json").string(),{},root/"temporary").index.entries.empty());
        rejects([&]{load("http://example.org/index.json",{},root/"temporary");});
        rejects([&]{load("https://example.org/index.json",{},root/"temporary");});
        Fetch offline=[](const auto&,const auto&,uint64_t){throw std::runtime_error("Offline");};
        rejects([&]{load("https://example.org/index.json",offline,root/"temporary");});
        std::string url="https://example.org/index.json";
        Fetch synthetic=[](const auto&,const auto& path,uint64_t){std::ofstream(path)<<"{\"format_version\":1,\"mods\":[]}";};
        auto cache=root/"cache";
        assert(refresh_cached(url,synthetic,cache).index.entries.empty());
        assert(cached(url,cache).index.entries.empty());
        rejects([&]{cached("https://other.example/index.json",cache);});
        rejects([&]{refresh_cached(url,offline,cache);});
        assert(cached(url,cache).index.entries.empty());
        Fetch invalid=[](const auto&,const auto& path,uint64_t){std::ofstream(path)<<"invalid";};
        rejects([&]{refresh_cached(url,invalid,cache);});
        assert(cached(url,cache).index.entries.empty());
        assert(std::distance(fs::directory_iterator(cache),fs::directory_iterator{})==1);

    }catch(...){fs::remove_all(root);throw;}
    fs::remove_all(root);
}
