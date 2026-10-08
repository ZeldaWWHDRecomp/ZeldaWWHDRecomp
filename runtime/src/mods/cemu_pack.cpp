#include "cemu_pack.h"
#include "guest_addr.h"
#include "mod_archive.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <regex>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
namespace mods::cemu {
namespace fs=std::filesystem;
namespace {
void require(bool yes,const std::string& why){if(!yes)throw std::runtime_error(why);}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+='a'-'A';return s;}
std::string unquote(std::string s){s=trim(s);if(s.size()>=2&&s.front()=='"'&&s.back()=='"')s=s.substr(1,s.size()-2);return s;}
std::string read(const fs::path& path){require(fs::is_regular_file(path)&&!fs::is_symlink(path)&&fs::file_size(path)<=1024*1024,"Missing or oversized Cemu file");std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
std::string comment(std::string s){bool quoted=false;for(size_t i=0;i<s.size();i++){if(s[i]=='"')quoted=!quoted;if(!quoted&&s[i]=='#')return s.substr(0,i);}return s;}
struct Section {std::string name;std::map<std::string,std::string> fields;};
std::vector<Section> ini(const std::string& text){
    std::vector<Section> sections;std::istringstream input(text);std::string line;
    while(std::getline(input,line)){
        line=trim(comment(line));if(line.empty())continue;
        if(line.front()=='['){require(line.back()==']',"Malformed Cemu section");sections.push_back({lower(trim(line.substr(1,line.size()-2))),{}});continue;}
        auto equal=line.find('=');require(equal!=std::string::npos&&!sections.empty(),"Malformed Cemu rules assignment");
        auto key=trim(line.substr(0,equal)),value=trim(line.substr(equal+1));
        require(!key.empty()&&!value.empty(),"Empty Cemu rules assignment");if(key.front()!='$')key=lower(key);
        require(sections.back().fields.emplace(key,value).second,"Duplicate Cemu rule: "+key);
    }
    require(sections.size()<=512,"Too many Cemu sections");return sections;
}
struct Expr {
    const std::string& text;const std::map<std::string,double>& vars;size_t pos=0,depth=0;
    void space(){while(pos<text.size()&&isspace(static_cast<unsigned char>(text[pos])))++pos;}
    bool take(char c){space();if(pos<text.size()&&text[pos]==c){++pos;return true;}return false;}
    double atom(){
        require(++depth<=32,"Cemu expression nesting limit");double value=0;
        if(take('+'))value=atom();else if(take('-'))value=-atom();
        else if(take('(')){value=sum();require(take(')'),"Missing expression parenthesis");}
        else {space();require(pos<text.size(),"Incomplete Cemu expression");
            if(text[pos]=='$'){size_t start=pos++;while(pos<text.size()&&(isalnum(static_cast<unsigned char>(text[pos]))||text[pos]=='_'))++pos;auto id=text.substr(start,pos-start);auto it=vars.find(id);require(it!=vars.end(),"Unknown Cemu variable: "+id);value=it->second;}
            else if(isalpha(static_cast<unsigned char>(text[pos]))){size_t start=pos++;while(pos<text.size()&&isalpha(static_cast<unsigned char>(text[pos])))++pos;auto fn=text.substr(start,pos-start);require(take('('),"Unsupported Cemu expression name");auto a=sum();if(fn=="min"||fn=="max"){require(take(','),"Missing function argument");auto b=sum();value=fn=="min"?std::min(a,b):std::max(a,b);}else if(fn=="floor")value=std::floor(a);else if(fn=="ceil")value=std::ceil(a);else if(fn=="round")value=std::round(a);else throw std::runtime_error("Unsupported Cemu function: "+fn);require(take(')'),"Missing function parenthesis");}
            else {char* end=nullptr;value=std::strtod(text.c_str()+pos,&end);require(end!=text.c_str()+pos,"Invalid Cemu expression");pos=end-text.c_str();}
        }
        --depth;require(std::isfinite(value),"Nonfinite Cemu expression");return value;
    }
    double product(){auto a=atom();for(;;){if(take('*'))a*=atom();else if(take('/')){auto b=atom();require(b!=0,"Division by zero in Cemu expression");a/=b;}else if(take('%')){auto b=atom();require(b!=0,"Division by zero in Cemu expression");a=std::fmod(a,b);}else return a;require(std::isfinite(a),"Nonfinite Cemu expression");}}
    double sum(){auto a=product();for(;;){if(take('+'))a+=product();else if(take('-'))a-=product();else return a;require(std::isfinite(a),"Nonfinite Cemu expression");}}
};
std::map<std::string,double> variables(const Pack& pack,const json::Value& config){
    auto raw=pack.defaults;
    for(size_t i=0;i<pack.categories.size();i++){
        auto chosen=config.get("preset-"+std::to_string(i)).string();const Preset* selected=nullptr;
        for(const auto& p:pack.presets)if(p.category==pack.categories[i]){if(!selected)selected=&p;if(p.name==chosen){selected=&p;break;}}
        if(selected)for(const auto& [id,value]:selected->variables)raw[id]=value;
    }
    std::map<std::string,double> values;
    for(size_t pass=0;!raw.empty()&&pass<=raw.size()+256;pass++){
        bool progress=false;
        for(auto it=raw.begin();it!=raw.end();){try{auto v=expression(it->second,values);values[it->first]=v;it=raw.erase(it);progress=true;}catch(const std::exception&){++it;}}
        require(progress||raw.empty(),"Unresolved Cemu variables (missing dependency, invalid arithmetic or cycle)");
    }
    require(raw.empty(),"Too many Cemu variables");return values;
}
uint32_t number(const std::string& text,const std::map<std::string,double>& vars,uint32_t limit=16384,uint32_t minimum=1){auto n=expression(text,vars);require(n>=minimum&&n<=limit,"Cemu dimension/filter is out of bounds");return uint32_t(n);}
std::vector<uint32_t> numbers(const std::string& text,const std::map<std::string,double>& vars){std::vector<uint32_t> result;std::istringstream in(text);std::string word;while(std::getline(in,word,',')){result.push_back(number(trim(word),vars,0xffff,0));require(result.size()<=64,"Too many Cemu format filters");}return result;}
struct PreparedRule {std::string owner;bool shaders=false;uint32_t width=0,height=0,depth=0;std::vector<uint32_t> formats,tiles;uint32_t out_width=0,out_height=0;};
struct PreparedShader {std::string owner,source;};
using ShaderKey=std::tuple<uint64_t,uint64_t,bool>;
struct Prepared {std::vector<PreparedRule> rules;std::map<ShaderKey,PreparedShader> shaders;float aspect=0;bool aspect_shaders=false;};
std::atomic<bool> present{false},shader_present{false},is_vulkan{false};Prepared active;
struct Diagnostics {unsigned applied=0,rejected=0;std::string reason;};
std::mutex diagnostics_mutex;std::map<std::string,Diagnostics> diagnostics;
std::string expand(const std::string& source,const std::map<std::string,double>& vars){
    std::string result;bool line=false,block=false;
    for(size_t i=0;i<source.size();){
        if(line){auto end=source.find('\n',i);if(end==std::string::npos){result.append(source,i,std::string::npos);break;}result.append(source,i,end-i+1);i=end+1;line=false;continue;}
        if(block){auto end=source.find("*/",i);require(end!=std::string::npos,"Unterminated shader comment");result.append(source,i,end-i+2);i=end+2;block=false;continue;}
        if(source.compare(i,2,"//")==0){line=true;continue;}if(source.compare(i,2,"/*")==0){result+="/*";i+=2;block=true;continue;}
        if(source[i]!='$'){result+=source[i++];continue;}
        size_t start=i++;while(i<source.size()&&(isalnum(static_cast<unsigned char>(source[i]))||source[i]=='_'))++i;
        auto id=source.substr(start,i-start);auto it=vars.find(id);require(it!=vars.end(),"Unknown shader variable: "+id);
        std::ostringstream value;value<<std::setprecision(17)<<it->second;auto text=value.str();if(text.find_first_of(".eE")==std::string::npos)text+=".0";result+="("+text+")";
    }
    auto version=result.find("#version");require(version!=std::string::npos,"Cemu shader has no GLSL version");auto end=result.find('\n',version);require(end!=std::string::npos,"Invalid shader version");result.replace(version,end-version,"#version 450\n#ifndef VULKAN\n#define VULKAN 1\n#endif");return result;
}
Prepared prepare(const std::vector<Selection>& selections){
    Prepared out;
    for(const auto& selection:selections){auto vars=variables(selection.pack,selection.config);
        if(!selection.pack.aspect_expression.empty()){require(out.aspect==0,"Multiple Cemu aspect packs conflict");auto ratio=expression(selection.pack.aspect_expression,vars);require(ratio>=1&&ratio<=4,"Cemu aspect ratio outside native 1:1–4:1 range");out.aspect=float(ratio);out.aspect_shaders=!selection.pack.shaders.empty();}
        for(const auto& rule:selection.pack.textures){PreparedRule p;p.owner=selection.id;p.shaders=!selection.pack.shaders.empty();
            for(const auto& [key,value]:rule.fields){if(key=="width")p.width=number(value,vars);else if(key=="height")p.height=number(value,vars);else if(key=="depth")p.depth=number(value,vars);else if(key=="formats")p.formats=numbers(value,vars);else if(key=="tilemodes")p.tiles=numbers(value,vars);else if(key=="overwritewidth")p.out_width=number(value,vars);else if(key=="overwriteheight")p.out_height=number(value,vars);else throw std::runtime_error("Unsupported Cemu texture rule: "+key);}
            require(p.out_width||p.out_height,"Texture rule has no supported overwrite dimensions");
            out.rules.push_back(std::move(p));
        }
        for(const auto& shader:selection.pack.shaders){auto [it,inserted]=out.shaders.emplace(ShaderKey{shader.base,shader.aux,shader.vertex},PreparedShader{selection.id,expand(shader.source,vars)});require(inserted,"Cemu shader conflict between "+selection.id+" and "+it->second.owner);}
    }
    for(size_t i=0;i<out.rules.size();i++)for(size_t j=i+1;j<out.rules.size();j++){
        const auto& a=out.rules[i];const auto& b=out.rules[j];if(a.owner==b.owner)continue;
        auto overlaps=[](const auto& x,const auto& y){return x.empty()||y.empty()||std::any_of(x.begin(),x.end(),[&](auto n){return std::find(y.begin(),y.end(),n)!=y.end();});};
        bool dimensions=(!a.width||!b.width||a.width==b.width)&&(!a.height||!b.height||a.height==b.height)&&(!a.depth||!b.depth||a.depth==b.depth);
        require(!(dimensions&&overlaps(a.formats,b.formats)&&overlaps(a.tiles,b.tiles)),"Cemu texture rule conflict between "+a.owner+" and "+b.owner);
    }
    return out;
}
}
double expression(const std::string& text,const std::map<std::string,double>& vars){require(text.size()<=4096,"Oversized Cemu expression");Expr parser{text,vars};auto value=parser.sum();parser.space();require(parser.pos==text.size(),"Unsupported Cemu expression operator");return value;}
Pack parse(const fs::path& folder){
    Pack pack;auto sections=ini(read(folder/"rules.txt"));bool definition=false;
    for(const auto& section:sections){auto field=[&](const char* key){auto it=section.fields.find(key);return it==section.fields.end()?std::string{}:unquote(it->second);};
        if(section.name=="definition"){
            require(!definition,"Multiple Cemu definitions");definition=true;bool usa=false;std::istringstream titles(lower(field("titleids")));std::string title;while(std::getline(titles,title,','))usa|=trim(title)=="0005000010143500";require(usa,"Cemu pack does not target WWHD USA");
            auto version=field("version");require(version=="4"||version=="5","Only Cemu graphics pack versions 4 and 5 are supported");
            pack.name=field("name");require(!pack.name.empty(),"Cemu pack has no name");pack.description=field("description");
        }else if(section.name=="default"){
            for(const auto& [id,value]:section.fields){require(id.starts_with("$"),"Unsupported Cemu Default field: "+id);pack.defaults[id]=value;}
        }else if(section.name=="preset"){
            Preset preset;preset.name=field("name");preset.category=field("category");if(preset.category.empty())preset.category="Preset";require(!preset.name.empty(),"Cemu preset has no name");
            for(const auto& [id,value]:section.fields){if(id.starts_with("$"))preset.variables[id]=value;else require(id=="name"||id=="category"||id=="default","Unsupported Cemu preset field: "+id);}
            require(std::none_of(pack.presets.begin(),pack.presets.end(),[&](const auto& p){return p.name==preset.name&&p.category==preset.category;}),"Duplicate Cemu preset name");
            if(std::find(pack.categories.begin(),pack.categories.end(),preset.category)==pack.categories.end())pack.categories.push_back(preset.category);
            // Default markers move this category's choice to the front without changing other groups.
            if(lower(field("default"))=="1"||lower(field("default"))=="true")pack.presets.insert(pack.presets.begin(),std::move(preset));else pack.presets.push_back(std::move(preset));
        }else if(section.name=="textureredefine")pack.textures.push_back({section.fields});
        else throw std::runtime_error("Unsupported Cemu section: "+section.name+" (code patches/control rules need another adapter)");
    }
    require(!fs::exists(folder/"content"),"Mixed content and graphics/shader packs need separate packages in this adapter");
    require(definition&&pack.presets.size()<=256&&pack.categories.size()<=32,"Invalid or oversized Cemu preset catalogue");
    std::regex filename("([0-9a-fA-F]{16})_([0-9a-fA-F]{16})_(vs|ps)\\.txt");
    for(const auto& e:fs::recursive_directory_iterator(folder)){
        require(!e.is_symlink(),"Cemu packs may not contain symlinks");auto name=lower(e.path().filename().string());
        if(name=="patches.txt"){require(fs::equivalent(e.path().parent_path(),folder),"Nested Cemu patches are unsupported");continue;}
        require(name!="code"&&name!="meta"&&name!="aoc","Cemu code patches, code/meta and DLC are unsupported");
        if(!e.is_regular_file())continue;
        auto extension=lower(e.path().extension().string());
        require(extension!=".asm","Cemu code patches (.asm) cannot run in this port; only graphics rules and shaders are supported");
        require(extension!=".pack"&&extension!=".bfres"&&extension!=".bflim"&&extension!=".gtx"&&extension!=".dds"&&extension!=".png"&&extension!=".rpx"&&extension!=".rpl"&&extension!=".arc"&&extension!=".szs","Cemu resource replacements need a separate content package");
        std::smatch match;
        auto shader_name=e.path().filename().string();if(std::regex_match(shader_name,match,filename)){
            require(fs::equivalent(e.path().parent_path(),folder),"Select one Cemu pack folder; shader files must be next to rules.txt");
            pack.shaders.push_back({std::stoull(match[1],nullptr,16),std::stoull(match[2],nullptr,16),match[3]=="vs",read(e.path())});
        }else require(!name.ends_with("_vs.txt")&&!name.ends_with("_ps.txt")&&!name.ends_with("_gs.txt")&&!name.ends_with(".glsl"),"Unsupported Cemu shader filename/stage");
    }
    if(fs::exists(folder/"patches.txt")) {
        // Only the official WWHD resolution pack's aspect constants are adapted.
        // No instruction patch, arbitrary guest address, or executable payload runs.
        const std::map<std::string,std::map<std::string,std::string>> expected={
            {"wwhdaspecteur",{{"modulematches","0xb7e748de"},{"0x1004aaf0",".float ($aspectratio)"},{"0x101417e0",".float ($aspectratio)"},{"0x101658a8",".float ($aspectratio)"}}},
            {"wwhdaspectjap",{{"modulematches","0x74bd3f6a"},{"0x1004aaf0",".float ($aspectratio)"},{"0x101417f8",".float ($aspectratio)"},{"0x101658c0",".float ($aspectratio)"}}},
            {"wwhdaspectusa",{{"modulematches","0x475bd29f"},{"0x1004aaf0",".float ($aspectratio)"},{"0x101417d0",".float ($aspectratio)"},{"0x10165898",".float ($aspectratio)"}}}};
        // the section of the build this port was made from: its addresses are the ones the game has
        const std::string build=g_guest_build_name;
        const std::string want=build=="EU"?"wwhdaspecteur":build=="JP"?"wwhdaspectjap":"wwhdaspectusa";
        bool mine=false;std::set<std::string> seen;
        for(auto section:ini(read(folder/"patches.txt"))) {
            require(expected.contains(section.name)&&seen.insert(section.name).second,"Unsupported Cemu patch section");
            for(auto& [key,value]:section.fields)value=lower(trim(value));
            require(section.fields==expected.at(section.name),"Only official WWHD aspect data patches are supported");
            mine|=section.name==want;
        }
        require(mine,"Missing WWHD "+build+" aspect patch");pack.aspect_expression="$aspectRatio";
    }
    require(!pack.textures.empty()||!pack.shaders.empty(),"Cemu pack contains no supported graphics or shader changes");
    prepare({Selection{"validation",pack,{}}});return pack;
}
json::Value options(const Pack& pack){
    json::Value result;result.type=json::Value::Array;
    for(size_t i=0;i<pack.categories.size();i++){json::Value option;option["id"]="preset-"+std::to_string(i);option["name"]=pack.categories[i];option["type"]="enum";option["description"]="Changes apply after restarting the game.";option["choices"].type=json::Value::Array;
        for(const auto& p:pack.presets)if(p.category==pack.categories[i])option["choices"].array.emplace_back(p.name);
        option["default"]=option.get("choices").array.front();result.array.push_back(std::move(option));
    }
    return result;
}
void import_legacy(const fs::path& stage,const std::string& source_name){
    std::vector<fs::path> rules;for(const auto& e:fs::recursive_directory_iterator(stage))if(e.is_regular_file()&&lower(e.path().filename().string())=="rules.txt")rules.push_back(e.path());
    require(rules.size()==1,"Select a single Cemu pack with one rules.txt");auto pack=parse(rules.front().parent_path());
    auto id=lower(fs::path(source_name).stem().string());for(char& c:id)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))c='-';if(id.size()>58)id.resize(58);require(!id.empty(),"Missing Cemu pack name");
    json::Value m;m["format_version"]=1;m["id"]="cemu."+id;m["name"]=pack.name;m["version"]="1.0.0";m["game_id"]="wwhd-usa";m["minimum_manager_version"]="1.2.0";m["kind"]="cemu";auto relative=rules.front().parent_path().lexically_relative(stage).generic_string();m["cemu_dir"]=relative=="."?"":relative;m["description"]=pack.description;m["options"]=options(pack);
    std::ofstream out(stage/"manifest.json");out<<json::dump(m)<<'\n';out.close();require(bool(out),"Cannot write imported Cemu manifest");
}
void validate(const std::vector<Selection>& selections){prepare(selections);}
void activate(const std::vector<Selection>& selections){require(!present.load(),"Cemu graphics packs already activated");active=prepare(selections);shader_present.store(!active.shaders.empty(),std::memory_order_release);present.store(!active.rules.empty()||!active.shaders.empty(),std::memory_order_release);}
void set_vulkan(bool yes){is_vulkan.store(yes,std::memory_order_release);}
bool vulkan(){return is_vulkan.load(std::memory_order_acquire);}
bool legacy_pixel_uniforms(uint64_t base){if(!has_shaders())return false;for(const auto& [key,shader]:active.shaders)if(std::get<0>(key)==base&&!std::get<2>(key)&&shader.source.find("uf_fragCoordScale")!=std::string::npos)return true;return false;}
float aspect_ratio(){return present.load(std::memory_order_acquire)&&(!active.aspect_shaders||vulkan())?active.aspect:0;}
bool has_shaders(){return shader_present.load(std::memory_order_acquire)&&vulkan();}
bool texture_extent(uint32_t width,uint32_t height,uint32_t format,uint32_t depth,uint32_t tile,uint32_t& ow,uint32_t& oh){
    if(!present.load(std::memory_order_acquire))return false;
    for(const auto& r:active.rules){if(r.shaders&&!vulkan())continue;if(r.width&&r.width!=width)continue;if(r.height&&r.height!=height)continue;if(r.depth&&r.depth!=depth)continue;if(!r.formats.empty()&&std::find(r.formats.begin(),r.formats.end(),format)==r.formats.end())continue;if(!r.tiles.empty()&&std::find(r.tiles.begin(),r.tiles.end(),tile)==r.tiles.end())continue;ow=r.out_width?r.out_width:width;oh=r.out_height?r.out_height:height;
        if(std::getenv("WWHD_TEST_CEMU_TRACE")){thread_local std::set<std::tuple<std::string,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t>> seen;if(seen.emplace(r.owner,width,height,format,ow,oh).second)std::fprintf(stderr,"[cemu-pack] %s target %ux%u format %x -> %ux%u\n",r.owner.c_str(),width,height,format,ow,oh);}
        return true;}return false;
}
void report_shader(uint64_t base,uint64_t aux,bool vertex,bool accepted,const std::string& reason){
    auto it=active.shaders.find({base,aux,vertex});if(it==active.shaders.end())return;
    std::lock_guard lock(diagnostics_mutex);auto& d=diagnostics[it->second.owner];if(accepted)++d.applied;else{++d.rejected;d.reason=reason.substr(0,256);}
}
std::string runtime_status(const std::string& id){std::lock_guard lock(diagnostics_mutex);auto it=diagnostics.find(id);if(it==diagnostics.end())return {};const auto& d=it->second;return "Shader overrides: "+std::to_string(d.applied)+" applied, "+std::to_string(d.rejected)+" rejected"+(d.reason.empty()?"":". "+d.reason);}
std::string shader_source(uint64_t base,uint64_t aux,bool vertex){if(!has_shaders())return {};auto it=active.shaders.find({base,aux,vertex});return it==active.shaders.end()?std::string{}:it->second.source;}
}
