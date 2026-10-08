#include "driver_store.h"
#include "mods/mod_archive.h"
#include "mods/mod_json.h"
#include <array>
#include <fstream>
#include <random>
#include <stdexcept>
namespace gfxvk::drivers {
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
bool id_ok(const std::string& id) {return id.size()==32&&id.find_first_not_of("0123456789abcdef")==id.npos;}
std::string text(const fs::path& file,size_t limit=16384) {
    if(!fs::exists(file))return {};
    require(fs::is_regular_file(file)&&!fs::is_symlink(file)&&fs::file_size(file)<=limit,"Invalid driver metadata/state");
    std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
void elf(const fs::path& file) {
    require(fs::is_regular_file(file)&&!fs::is_symlink(file),"Driver library missing");
    std::array<unsigned char,64> h{};std::ifstream in(file,std::ios::binary);in.read(reinterpret_cast<char*>(h.data()),h.size());
    require(in.gcount()==64&&h[0]==127&&h[1]=='E'&&h[2]=='L'&&h[3]=='F'&&h[4]==2&&h[5]==1&&h[6]==1&&h[16]==3&&h[17]==0&&h[18]==183&&h[19]==0,"Driver must be an arm64 ELF shared library");
}
}
Store::Store(fs::path root):root_(std::move(root)) {fs::create_directories(root_);selected_=text(root_/"selected",64);require(selected_.empty()||id_ok(selected_),"Invalid selected driver ID");}
void Store::write(const fs::path& path,const std::string& value) {
    fs::create_directories(path.parent_path());auto temp=path;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<value;out.flush();require(bool(out),"Cannot persist driver state");}
    fs::rename(temp,path);
}
Driver Store::read(const fs::path& dir) const {
    require(!fs::is_symlink(dir),"Driver directory may not be a symbolic link");
    auto m=mods::json::parse(text(dir/"meta.json"));Driver d;d.id=dir.filename().string();d.directory=dir;
    auto string=[&](const char* key,bool required=true){const auto& v=m.get(key);require(!required||v.type==mods::json::Value::String,"Driver metadata field missing");auto s=v.string();require(s.size()<=512&&s.find_first_of("\r\n\0",0,3)==s.npos,"Invalid driver metadata text");return s;};
    const auto& schema=m.get("schemaVersion");require(schema.type==mods::json::Value::Number&&schema.number==1,"Unsupported driver metadata schema");
    d.library=string("libraryName");d.name=string("name");d.version=string("driverVersion",false);if(d.version.empty())d.version=string("packageVersion",false);
    require(!d.name.empty()&&!d.library.empty()&&d.library.ends_with(".so")&&d.library.find_first_of("/\\:")==d.library.npos&&d.library!="..","Invalid driver library name");
    auto api=m.get("minApi");require(api.type==mods::json::Value::Number&&std::isfinite(api.number)&&api.number>=0&&api.number<=1000&&std::floor(api.number)==api.number,"Invalid driver minimum API");d.min_api=int(api.number);
    for(const auto& entry:fs::directory_iterator(dir)) {
        require(entry.is_regular_file()&&!entry.is_symlink(),"Only top-level files are accepted in a driver package");
        if(entry.path().filename()=="meta.json")continue;
        if(entry.path().extension()==".so")elf(entry.path());
        else require(entry.path().filename()=="LICENSE"||entry.path().filename()=="LICENSE.txt"||entry.path().filename()=="README.md","Unexpected driver package file");
    }
    elf(dir/d.library);return d;
}
std::vector<Driver> Store::list() const {
    std::vector<Driver> out;
    for(const auto& entry:fs::directory_iterator(root_))if(entry.is_directory()&&!entry.is_symlink()&&id_ok(entry.path().filename().string()))
        try{out.push_back(read(entry.path()));}catch(const std::exception&){}
    return out;
}
std::string Store::install(const fs::path& zip,int api) {
    require(fs::is_regular_file(zip),"Choose a driver ZIP file");
    std::random_device random;std::string id;constexpr char hex[]="0123456789abcdef";
    do {id.clear();for(int n=0;n<32;++n)id+=hex[random()&15];}while(fs::exists(root_/id));
    auto stage=root_/("install-"+id);
    try {mods::archive::stage(zip,stage);auto d=read(stage);require(d.min_api<=api,"Driver needs a newer Android version");fs::rename(stage,root_/id);return id;}
    catch(...) {std::error_code ec;fs::remove_all(stage,ec);throw;}
}
void Store::select(const std::string& id) {
    require(id.empty()||id_ok(id),"Invalid driver ID");if(!id.empty())read(root_/id);
    write(root_/"selected",id);selected_=id;
}
void Store::remove(const std::string& id) {
    require(id_ok(id),"Invalid driver ID");require(id!=probing_,"Restart with the system driver before removing the active driver");
    if(id==selected_)select("");fs::remove_all(root_/id);fs::remove_all(cache(id));
}
bool Store::start() {
    require(probing_.empty(),"Driver probe already started");
    if(fs::exists(root_/"probing")) {select("");fs::remove(root_/"probing");return true;}
    if(selected_.empty())return false;
    read(root_/selected_);write(root_/"probing",selected_);probing_=selected_;frames_=0;return false;
}
void Store::rendered_frame() {if(!probing_.empty()&&frames_<120&&++frames_==120)fs::remove(root_/"probing");}
void Store::failed() {select("");fs::remove(root_/"probing");probing_.clear();frames_=0;}
fs::path Store::cache(const std::string& id) const {require(id.empty()||id_ok(id),"Invalid cache driver ID");return root_/"caches"/(id.empty()?"system":id);}
}
