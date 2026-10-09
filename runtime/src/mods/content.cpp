#include "../exception_report.h"
#include "content.h"
#include "cemu_pack.h"
#include "guest_addr.h"
#include "mod_archive.h"
#include "mod_json.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <sstream>
#include <atomic>
#include <stdexcept>
namespace mods::content {
namespace fs=std::filesystem;
namespace {
Files active;
std::atomic<bool> present{false};
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+= 'a'-'A';return s;}
void require(bool ok,const char* reason){if(!ok)exception_report::raise(reason);}
}
// The 2D language packs the game names (Common/Pack/permanent_2d_<Us|Eu|Jp><Language>.pack, cking.rpx
// 0x1048DD4C) and its other packs. A fan translation is usually one of the language packs.
const char* const kLanguagePacks[]={"permanent_2d_usenglish.pack","permanent_2d_usfrench.pack","permanent_2d_usspanish.pack",
    "permanent_2d_euenglish.pack","permanent_2d_eufrench.pack","permanent_2d_eugerman.pack","permanent_2d_euitalian.pack",
    "permanent_2d_euspanish.pack","permanent_2d_jpjapanese.pack"};
bool known_pack(const std::string& filename){
    auto name=lower(filename);
    for(auto allowed:{"permanent_3d.pack","first_szs_permanent.pack","szs_permanent0.pack","szs_permanent1.pack","szs_permanent2.pack"})if(name==allowed)return true;
    for(auto allowed:kLanguagePacks)if(name==allowed)return true;
    return false;
}
std::string pack_language(const std::string& filename){
    auto name=lower(filename);
    for(auto known:kLanguagePacks)if(name==known)return name.substr(15,name.size()-15-5); // after "permanent_2d_xx", before ".pack"
    return {};
}
namespace {
fs::path game_root;
// the installed game's content files by lower-case file name -> game-relative paths (for loose imports)
std::multimap<std::string,std::string> game_names(){
    std::multimap<std::string,std::string> out;std::error_code ec;
    if(game_root.empty())return out;
    auto content=game_root/"content";if(!fs::is_directory(content,ec))return out;
    for(fs::recursive_directory_iterator it(content,ec),end;!ec&&it!=end;it.increment(ec))
        if(it->is_regular_file(ec))out.emplace(lower(it->path().filename().string()),it->path().lexically_relative(content).generic_string());
    return out;
}
}
void set_game_root(const fs::path& game){game_root=game;}
void import_legacy(const fs::path& stage,const std::string& source_name){
    std::vector<fs::path> candidates,loose_packs;
    for(const auto& e:fs::recursive_directory_iterator(stage)){
        auto name=lower(e.path().filename().string());
        if(e.is_directory()){
            if(name=="content")candidates.push_back(e.path());
            require(name!="code"&&name!="meta"&&name!="aoc", "Only content replacements are supported; code, meta and DLC folders cannot be imported");
            if(name.size()==16&&name.starts_with("00050000"))require(name==lower(g_guest_build_title_id),(std::string("This SDCafiine pack targets another game or region (requires title ")+g_guest_build_title_id+", "+g_guest_build_name+")").c_str());
        }else{
            auto ext=lower(e.path().extension().string());
            if(ext==".pack")loose_packs.push_back(e.path());
            // Cemu code patches (patch_*.asm: hooks and code caves in the game's PowerPC code) can't run in the
            // recompiled game; importing only the files beside one would silently drop part of the mod.
            require(ext!=".asm","This mod includes a Cemu code patch (.asm), which this port cannot apply. To use only its content files, select its content folder instead");
            require(name!="patches.txt"&&!name.ends_with("_vs.txt")&&!name.ends_with("_ps.txt")&&ext!=".glsl"&&ext!=".rpx"&&ext!=".rpl", "This mod includes unsupported code patches or shaders; import the complete mod through a future adapter");
            if(name=="rules.txt"){
                require(e.file_size()<=1024*1024,"Oversized Cemu rules file");std::ifstream f(e.path());std::string text{std::istreambuf_iterator<char>(f),{}};auto rules=lower(text);
                // A file-only Cemu pack may have Definition metadata, but no patch/shader/texture rules.
                require(rules.find("[texture")==std::string::npos&&rules.find("[control")==std::string::npos&&rules.find("[preset")==std::string::npos,"Cemu texture/control/preset rules require an adapter; this import supports file-only packs");
                std::istringstream lines(rules);std::string line;
                while(std::getline(lines,line)){auto start=line.find_first_not_of(" \t\r");if(start!=std::string::npos&&line[start]=='['){auto end=line.find(']',start);require(end!=std::string::npos&&line.substr(start,end-start+1)=="[definition]","Only Definition metadata is supported in file-only Cemu packs");}}
                auto title=rules.find("titleids");if(title!=std::string::npos){auto end=rules.find('\n',title);auto line=rules.substr(title,end-title);auto eq=line.find('=');require(eq!=std::string::npos&&cemu::targets_title(line.substr(eq+1),g_guest_build_title_id),(std::string("Cemu pack does not target this version of the game (")+g_guest_build_name+", title "+g_guest_build_title_id+")").c_str());}
            }
        }
    }
    // Loose files (a fan translation's permanent_2d_*.pack, a replaced layout such as Title_00.szs): packs go to
    // Common/Pack; any other file whose name the installed game has exactly once goes to that game path.
    // Other files (read-me texts, pictures) are not used.
    std::string mapped,ignored;
    if(candidates.empty()){
        for(const auto& file:loose_packs)require(known_pack(file.filename().string()),"Unknown loose pack filename; use a content/ tree with its game-relative path");
        auto names=game_names();std::vector<std::pair<fs::path,std::string>> moves;
        for(const auto& e:fs::recursive_directory_iterator(stage)){
            if(!e.is_regular_file())continue;auto name=lower(e.path().filename().string());
            if(lower(e.path().extension().string())==".pack"){moves.emplace_back(e.path(),"Common/Pack/"+e.path().filename().string());continue;}
            auto hits=names.equal_range(name);auto n=std::distance(hits.first,hits.second);
            require(n<=1,"A loose file matches several game files; use a content/ tree with its game-relative path");
            if(n==1)moves.emplace_back(e.path(),hits.first->second);else ignored+=(ignored.empty()?"":", ")+e.path().filename().string();
        }
        require(!moves.empty(),"Select a single mod pack folder containing exactly one content/ directory");
        for(const auto& [from,to]:moves){
            auto dest=stage/"content"/fs::path(to);require(!fs::exists(dest),"Duplicate loose file name");
            fs::create_directories(dest.parent_path());fs::rename(from,dest);mapped+=(mapped.empty()?"":", ")+to;
        }
        candidates.push_back(stage/"content");
    }
    require(candidates.size()==1,"Select a single mod pack folder containing exactly one content/ directory");
    // Stable deterministic ID by source name. An explicit manifest can supply a different ID/version.
    auto name=fs::path(source_name).stem().string();require(!name.empty(),"Missing mod name");if(name.size()>128)name.resize(128);
    auto id=lower(name);for(char& c:id)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))c='-';if(id.size()>55)id.resize(55);id="content."+id;
    json::Value m;m["format_version"]=1;m["id"]=id;m["name"]=name;m["version"]="1.0.0";m["game_id"]="wwhd-usa";m["kind"]="content";m["minimum_manager_version"]="1.1.0";
    m["content_dir"]=candidates.front().lexically_relative(stage).generic_string();
    std::string description="Imported local content replacement. Requires restart. Model and archive compatibility must be checked in game.";
    if(!mapped.empty())description+=" Loose files placed at: "+mapped+".";
    if(!ignored.empty())description+=" Not used: "+ignored+".";
    m["description"]=description;
    std::ofstream out(stage/"manifest.json");out<<json::dump(m)<<'\n';out.close();require(bool(out),"Cannot write imported manifest");
}
Files index(const fs::path& directory){
    require(fs::is_directory(directory),"Content folder is missing");
    require(!fs::is_symlink(directory),"Content paths may not use symlinks");
    Files out;uint64_t total=0;size_t count=0;
    for(const auto& e:fs::recursive_directory_iterator(directory)){
        require(++count<=4096,"Too many content entries");require(!e.is_symlink(),"Content files may not use symlinks");
        if(e.is_directory())continue;
        require(e.is_regular_file(),"Unsupported content file type");require(!lower(e.path().filename().string()).starts_with(".deleted_"),"SDCafiine file hiding is not supported yet");auto n=e.file_size();total+=n;
        require(n<=128ull*1024*1024&&total<=512ull*1024*1024,"Content size limit exceeded");
        auto relative=e.path().lexically_relative(directory).generic_string();
        require(archive::relative_path(relative),"Invalid content path");
        require(out.emplace(lower(relative),e.path().string()).second,"Duplicate content path (case insensitive)");
    }
    require(!out.empty(),"Content mod contains no files");return out;
}
void activate(Files files){require(!present.load(),"Content overrides already activated");active=std::move(files);present.store(!active.empty(),std::memory_order_release);}
std::string replacement(const std::string& guest,const std::string& mode){
    if(!present.load(std::memory_order_acquire))return {};
    if(mode!="r"&&mode!="rb")return {}; // never redirect writers or update handles
    auto relative=guest;
    if(relative.starts_with("/vol/content/"))relative.erase(0,13);
    else if(relative.empty()||relative.front()=='/')return {};
    if(!archive::relative_path(relative))return {};
    auto key=lower(relative);auto it=active.find(key);if(it!=active.end())return it->second;
    // A translated 2D language pack applies to the pack of that language the game loads, whatever region its
    // file name has: a fan translation made as permanent_2d_EuEnglish.pack replaces the USA game's
    // permanent_2d_UsEnglish.pack (and the other way round, and for a language source's pack). The game may
    // also ask for it in another folder than Common/Pack: told the European region (language sources,
    // game_languages.h) it reads Cafe/JP/Pack/permanent_2d_Eu<Language>.pack through its "local" file
    // device, a folder no disc has. A pack with the exact name always comes first.
    auto slash=key.rfind('/');auto file=slash==std::string::npos?key:key.substr(slash+1);
    auto language=pack_language(file);
    if(!language.empty()&&(key=="pack/"+file||key.ends_with("/pack/"+file))){
        std::vector<std::string> names{file};
        for(auto region:{"us","eu","jp"}){auto alt=std::string("permanent_2d_")+region+language+".pack";if(alt!=file)names.push_back(alt);}
        for(const auto& name:names){
            if(auto j=active.find("common/pack/"+name);j!=active.end()&&("common/pack/"+name)!=key){
                static std::atomic<bool> logged{false};
                if(!logged.exchange(true))fprintf(stderr,"[mod-manager] %s is read from the content mod's %s\n",relative.c_str(),j->first.c_str());
                return j->second;
            }
        }
    }
    return {};
}
}
