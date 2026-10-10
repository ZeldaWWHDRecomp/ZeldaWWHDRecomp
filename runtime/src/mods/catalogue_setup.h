// Declarative setup validation. Paths stay in local settings, never in diagnostic text.
#pragma once
#include "../exception_report.h"
#include "catalogue_io.h"
#include <array>
#include <cctype>

namespace mods::catalogue {
inline bool valid_game_source(const std::string& game,const std::filesystem::path& source) {
    namespace fs=std::filesystem;
    try {
        static const std::map<std::string,std::string> discs{{"gc_usa","GZLE01"},{"gc_eur","GZLP01"},{"gc_jpn","GZLJ01"}};
        auto it=discs.find(game);
        if(it!=discs.end()||game=="gc_wind_waker") {
            auto file=fs::is_directory(source)?source/"sys"/"boot.bin":source;
            if(fs::is_symlink(file)||!fs::is_regular_file(file))return false;
            std::ifstream input(file,std::ios::binary);std::array<unsigned char,32> header{};
            input.read(reinterpret_cast<char*>(header.data()),header.size());
            if(input.gcount()==32&&std::equal(header.begin(),header.begin()+4,"RVZ\1")) {
                // RVZ stores the original disc header uncompressed in its disc
                // descriptor. Resource decoding remains the setup tool's job.
                // Format: Dolphin docs/WiaAndRvz.md (file head 0x48, dhead +0x10).
                auto word=[&](size_t p){return (uint32_t(header[p])<<24)|(uint32_t(header[p+1])<<16)|
                    (uint32_t(header[p+2])<<8)|header[p+3];};
                if(word(8)>0x01000000||word(12)<0xDC||word(12)>4096)return false;
                input.seekg(0x48);std::array<unsigned char,4> type{};
                input.read(reinterpret_cast<char*>(type.data()),type.size());
                if(input.gcount()!=4||type!=std::array<unsigned char,4>{0,0,0,1})return false;
                input.seekg(0x58);input.read(reinterpret_cast<char*>(header.data()),header.size());
            }
            const std::string id(reinterpret_cast<char*>(header.data()),6);
            const bool known=game=="gc_wind_waker" ?
                std::any_of(discs.begin(),discs.end(),[&](const auto& disc){return disc.second==id;}) : id==it->second;
            return input.gcount()==32&&known&&
                   header[28]==0xC2&&header[29]==0x33&&header[30]==0x9F&&header[31]==0x3D;
        }
        std::string title;
        if(game=="wiiu_eur")title="0005000010143600";
        else if(game=="wiiu_jpn")title="0005000010143400";
        else return false;
        if(!fs::is_directory(source))return false;
        // Both metadata copies, when present, must agree. Updates/DLC are refused.
        bool found=false;
        for(const auto* relative:{"code/app.xml","meta/meta.xml"}) {
            auto file=source/relative;if(!fs::exists(file))continue;
            auto xml=read_bounded(file,65536);size_t start=xml.find("<title_id");
            if(start==std::string::npos)return false;
            start=xml.find('>',start);if(start==std::string::npos)return false;++start;
            while(start<xml.size()&&std::isspace(static_cast<unsigned char>(xml[start])))++start;
            auto id=xml.substr(start,16);for(char& c:id)if(c>='A'&&c<='F')c+=32;
            if(id!=title)return false;start+=16;
            while(start<xml.size()&&std::isspace(static_cast<unsigned char>(xml[start])))++start;
            if(xml.compare(start,11,"</title_id>")!=0)return false;
            found=true;
        }
        return found;
    }catch(const std::exception&){return false;}
}
inline bool uses_game_source(const Step& step,const std::string& game) {
    auto matches=[&](const std::string& id){return id==game||(game=="gc_wind_waker"&&id.starts_with("gc_"));};
    if(step.type=="game_path")return matches(step.game);
    if(step.type=="run_tool")for(const auto& arg:step.arguments) {
        for(const auto* id:{"gc_wind_waker","gc_usa","gc_eur","gc_jpn","wiiu_eur","wiiu_jpn"})
            if(matches(id)&&arg.find("{game:"+std::string(id)+"}")!=std::string::npos)return true;
    }
    return false;
}
class Sources {
    json::Value settings;
public:
    Sources()=default;
    explicit Sources(json::Value local_settings):settings(std::move(local_settings)){}
    std::string stored(const std::string& game) const {
        if(game.starts_with("gc_")) {
            // Migrate the old per-region preferences lazily; all GC mods use one copy.
            for(const auto* key:{"gc_wind_waker","gc_usa","gc_eur","gc_jpn"}) {
                auto path=settings.get(key).string();if(!path.empty())return path;
            }
            return {};
        }
        return settings.get(game).string();
    }
    bool set(const std::string& game,const std::filesystem::path& path) {
        if(!std::set<std::string>{"gc_wind_waker","gc_usa","gc_eur","gc_jpn","wiiu_eur","wiiu_jpn"}.contains(game))return false;
        if(!path.empty()&&!valid_game_source(game,path))return false;
        auto key=game.starts_with("gc_")?std::string("gc_wind_waker"):game;
        if(game.starts_with("gc_"))for(const auto* old:{"gc_usa","gc_eur","gc_jpn"})settings.object.erase(old);
        if(path.empty())settings.object.erase(key);
        else settings[key]=std::filesystem::canonical(path).string();return true;
    }
    std::string get(const std::string& game) const {
        auto path=stored(game);
        return !path.empty()&&valid_game_source(game,path)?path:std::string();
    }
    const json::Value& local_settings()const{return settings;}
};
inline bool outputs_satisfied(const Step& step,const std::filesystem::path& data) {
    if(step.outputs.empty())return false;
    try{for(const auto& output:step.outputs)confined_file(data,output);return true;}
    catch(const std::exception&){return false;}
}
inline bool option_satisfied(const Step& step,const json::Value& value) {
    if(step.type=="confirm")return value.type==json::Value::Bool&&value.boolean;
    return step.type=="choice"&&value.type==json::Value::String&&
        std::find(step.choices.begin(),step.choices.end(),value.text)!=step.choices.end();
}
// Substitutions preserve one argument per declaration; no shell or word splitting.
inline std::vector<std::string> tool_arguments(const Step& step,const Sources& sources,
        const std::filesystem::path& package,const std::filesystem::path& data) {
    require(step.type=="run_tool","Setup step is not a tool");
    std::vector<std::string> result{confined_file(package,step.tool).string()};
    for(auto arg:step.arguments) {
        size_t from=0;
        while((from=arg.find('{',from))!=std::string::npos) {
            auto end=arg.find('}',from);require(end!=std::string::npos,"Unclosed setup argument reference");
            auto key=arg.substr(from+1,end-from-1);std::string replacement;
            if(key=="data")replacement=std::filesystem::absolute(data).string();
            else if(key=="package")replacement=std::filesystem::absolute(package).string();
            else if(key.starts_with("game:")) {
                replacement=sources.get(key.substr(5));require(!replacement.empty(),"Required game source is missing or moved");
            }else exception_report::raise("Unknown setup argument reference");
            arg.replace(from,end-from+1,replacement);from+=replacement.size();
        }
        result.push_back(std::move(arg));
    }
    return result;
}
inline std::string tool_receipt(const Step& step,const Sources& sources,
        const std::filesystem::path& package,const std::filesystem::path& data,
        const std::string& package_fingerprint) {
    std::string inventory=package_fingerprint;
    for(const auto& argument:tool_arguments(step,sources,package,data)) {
        inventory.push_back('\0');inventory+=argument;
    }
    return hash::sha256_text(inventory);
}
} // namespace mods::catalogue
