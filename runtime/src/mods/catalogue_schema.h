// Versioned catalogue metadata. Parsing never downloads or executes package content.
#pragma once
#include "../exception_report.h"
#include "mod_json.h"
#include <array>
#include <algorithm>
#include <compare>
#include <cstdint>
#include <charconv>
#include <set>

namespace mods::catalogue {
struct Version {
    std::array<uint32_t,3> parts{};
    auto operator<=>(const Version&) const=default;
    static Version parse(const std::string& text) {
        Version v;size_t start=0;
        for(size_t i=0;i<3;++i) {
            size_t end=text.find('.',start);if(i==2)end=text.size();
            if(end==std::string::npos||end==start||end-start>9)exception_report::raise("Expected three-part version");
            auto [p,ec]=std::from_chars(text.data()+start,text.data()+end,v.parts[i]);
            if(ec!=std::errc{}||p!=text.data()+end||(end-start>1&&text[start]=='0'))exception_report::raise("Invalid version");
            start=end+1;
        }
        return v;
    }
};
struct Download { std::string url,sha256; uint64_t size=0; };
struct Step {
    std::string id,type,title,explanation,game,tool,option;
    bool optional=false;
    std::vector<std::string> arguments,choices,outputs;
    bool operator==(const Step&)const=default;
};
struct Entry {
    std::string id,name,description,version,kind;
    Version minimum,maximum;bool bounded_maximum=false;
    std::vector<std::string> authors,licences,dependencies,builds;
    std::map<std::string,Download> downloads;
    std::vector<Step> setup;
    bool compatible(const Version& port,const std::string& build,const std::string& platform) const {
        if(port<minimum||(bounded_maximum&&port>=maximum))return false;
        if(!builds.empty()&&std::find(builds.begin(),builds.end(),build)==builds.end())return false;
        if(platform.starts_with("android")&&kind=="guest")return false;
        return downloads.contains(platform)||downloads.contains("all");
    }
};
struct Index { std::vector<Entry> entries; };
inline void require(bool valid,const std::string& why) {if(!valid)exception_report::raise(why);}
inline bool identifier(const std::string& id) {
    return !id.empty()&&id.size()<=64&&id.front()!='.'&&id.front()!='-'&&std::all_of(id.begin(),id.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';})&&id!="."&&id!="..";
}
inline std::string text(const json::Value& value,const char* field,size_t max,bool optional=false) {
    const auto& v=value.get(field);if(optional&&v.type==json::Value::Null)return {};
    require(v.type==json::Value::String&&!v.text.empty()&&v.text.size()<=max&&v.text.find('\0')==std::string::npos,
            std::string("Invalid catalogue field: ")+field);return v.text;
}
inline std::vector<std::string> strings(const json::Value& value,size_t max,size_t width,bool optional=false,bool unique=true) {
    if(optional&&value.type==json::Value::Null)return {};
    require(value.type==json::Value::Array&&value.array.size()<=max,"Invalid catalogue list");
    std::vector<std::string> result;std::set<std::string> seen;
    for(const auto& v:value.array) {
        require(v.type==json::Value::String&&!v.text.empty()&&v.text.size()<=width&&v.text.find('\0')==std::string::npos,
                "Invalid catalogue list item");
        require(!unique||seen.insert(v.text).second,"Duplicate catalogue list item");result.push_back(v.text);
    }
    return result;
}
inline bool relative_file(const std::string& path) {
    if(path.empty()||path.size()>512||path.front()=='/'||(path.find_first_of("\\:")!=std::string::npos||path.find('\0')!=std::string::npos))return false;
    size_t start=0;
    do {
        size_t end=path.find('/',start);if(end==std::string::npos)end=path.size();
        auto part=path.substr(start,end-start);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;
        for(unsigned char c:part)if(c<32||c==127)return false;
        auto device=part.substr(0,part.find('.'));
        for(char& c:device)if(c>='a'&&c<='z')c-=32;
        if(device=="CON"||device=="PRN"||device=="AUX"||device=="NUL"||
           (device.size()==4&&(device.starts_with("COM")||device.starts_with("LPT"))&&device[3]>='1'&&device[3]<='9'))return false;
        if(end==path.size())break;start=end+1;
    }while(true);
    return true;
}
inline bool https_url(const std::string& url) {
    if(!url.starts_with("https://")||url.size()>2048)return false;
    for(unsigned char c:url)if(c<=32||c==127||c=='\\')return false;
    size_t end=url.find_first_of("/?#",8);auto authority=url.substr(8,end==std::string::npos?end:end-8);
    return !authority.empty()&&authority.find('@')==std::string::npos&&url.find('#')==std::string::npos;
}
inline std::vector<Step> steps(const json::Value& value) {
    if(value.type==json::Value::Null)return {};
    require(value.type==json::Value::Array&&value.array.size()<=32,"Too many setup steps");
    std::vector<Step> out;std::set<std::string> ids;
    for(const auto& v:value.array) {
        require(v.type==json::Value::Object,"Invalid setup step");Step s;
        s.id=text(v,"id",64);require(identifier(s.id)&&ids.insert(s.id).second,"Invalid or duplicate setup step ID");
        s.type=text(v,"type",32);s.title=text(v,"title",256);s.explanation=text(v,"explanation",4096,true);
        const auto& optional=v.get("optional");require(optional.type==json::Value::Null||optional.type==json::Value::Bool,"Invalid optional flag");
        s.optional=optional.boolean;
        if(s.type=="game_path") {
            s.game=text(v,"game",32);require(std::set<std::string>{"gc_wind_waker","gc_usa","gc_eur","gc_jpn","wiiu_eur","wiiu_jpn"}.contains(s.game),"Unknown game source");
        }else if(s.type=="run_tool") {
            s.tool=text(v,"tool",512);require(relative_file(s.tool),"Setup tool must be package-relative");
            s.arguments=strings(v.get("arguments"),64,2048,true,false);s.outputs=strings(v.get("outputs"),32,512);
            require(!s.outputs.empty(),"Tool needs output checks");
            for(const auto& path:s.outputs)require(relative_file(path),"Tool output must be data-relative");
        }else if(s.type=="choice"||s.type=="confirm") {
            s.option=text(v,"option",64);require(identifier(s.option),"Invalid setup option");
            if(s.type=="choice"){s.choices=strings(v.get("choices"),64,256);require(!s.choices.empty(),"Choice needs values");}
        }else require(s.type=="build_guest_mod","Unknown setup step type");
        out.push_back(std::move(s));
    }
    return out;
}
inline Index parse(const std::string& source,bool local=false) {
    require(source.size()<=2*1024*1024,"Catalogue exceeds 2 MiB");auto value=json::parse(source);
    require(value.type==json::Value::Object&&value.get("format_version").type==json::Value::Number&&
            value.get("format_version").number==1,"Unsupported catalogue format");
    const auto& entries=value.get("mods");require(entries.type==json::Value::Array&&entries.array.size()<=1024,"Invalid catalogue entries");
    Index result;std::set<std::string> ids;
    for(const auto& v:entries.array) {
        require(v.type==json::Value::Object,"Invalid catalogue entry");Entry e;
        e.id=text(v,"id",64);require(identifier(e.id)&&ids.insert(e.id).second,"Invalid or duplicate mod ID");
        e.name=text(v,"name",256);e.description=text(v,"description",16384);e.version=text(v,"version",64);Version::parse(e.version);
        e.kind=text(v,"kind",32);require(std::set<std::string>{"guest","native","content","settings","cemu"}.contains(e.kind),"Unknown mod kind");
        e.authors=strings(v.get("authors"),32,256);require(!e.authors.empty(),"Mod needs an author");
        e.licences=strings(v.get("licences"),32,256);require(!e.licences.empty(),"Mod needs licence information");
        e.dependencies=strings(v.get("requires"),64,64,true);for(const auto& id:e.dependencies)require(identifier(id)&&id!=e.id,"Invalid mod dependency");
        e.builds=strings(v.get("builds"),16,16,true);for(const auto& b:e.builds)require(b=="USA"||b=="EU","Unknown game build");
        const auto& range=v.get("port_versions");require(range.type==json::Value::Object,"Missing port version range");
        e.minimum=Version::parse(text(range,"minimum",64));auto max=text(range,"maximum_exclusive",64,true);
        if(!max.empty()){e.maximum=Version::parse(max);e.bounded_maximum=true;require(e.maximum>e.minimum,"Empty port version range");}
        const auto& downloads=v.get("downloads");require(downloads.type==json::Value::Object&&!downloads.object.empty()&&downloads.object.size()<=16,"Invalid downloads");
        for(const auto& [platform,d]:downloads.object) {
            require(identifier(platform)&&d.type==json::Value::Object,"Invalid download platform");Download download;
            download.url=text(d,"url",2048);require(https_url(download.url)||(local&&relative_file(download.url)),"Packages require HTTPS (or local catalogue-relative fixtures)");
            download.sha256=text(d,"sha256",64);require(download.sha256.size()==64&&std::all_of(download.sha256.begin(),download.sha256.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid SHA-256");
            const auto& size=d.get("size");require(size.type==json::Value::Number&&size.number>=1&&size.number<=256*1024*1024&&std::floor(size.number)==size.number,"Package exceeds size limit");
            download.size=uint64_t(size.number);e.downloads.emplace(platform,std::move(download));
        }
        e.setup=steps(v.get("setup"));
        for(const auto& step:e.setup)require(step.type!="build_guest_mod"||e.kind=="guest","Guest build step needs a guest package");
        result.entries.push_back(std::move(e));
    }
    return result;
}
} // namespace mods::catalogue
