#include "actor_model_cache.h"
#include "mod_hash.h"
#include "static_model_validation.h"
#include "skinned_model_validation.h"
#include "../platform/process.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fstream>
#include <map>
#include <mutex>
namespace actor_model_cache {
namespace {
struct Entry {std::string mod,model,path;unsigned triangles=0;story_notebook::WorldPoint minimum,maximum;std::vector<std::string> clips;mod_actor::ModelRig rig;std::shared_ptr<const story_world_collision::mesh::Mesh> collision;bool shoreline=false;std::string material;};
std::mutex mutex,prepare_mutex;
std::map<std::string,Entry> entries;
std::map<std::pair<std::string,std::string>,std::string> failures;  // (mod, model) -> why it could not be prepared
std::map<std::pair<std::string,std::string>,unsigned> running;      // (mod, model) -> prepare calls running
void check(bool ok,const char* reason){if(!ok)exception_report::raise(reason);}
std::string prepare_once(const std::string& mod,const story_notebook::ManifestModel& model,
                         const std::string& package_dir,const std::string& data_dir,
                         const std::vector<std::string>& python,const std::string& tool,const std::string& game_dir,bool collision,bool shoreline,const std::string& material_in,
                         unsigned generation);
}
std::string plain_reason(std::string text){
    std::string out;
    for(size_t i=0;i<text.size();++i){
        const unsigned char c=(unsigned char)text[i];
        if(c==0x1B){  // ESC [ parameters final-byte (a colour), or ESC and one more character
            if(i+1<text.size()&&text[i+1]=='['){i+=2;while(i<text.size()&&!((unsigned char)text[i]>=0x40&&(unsigned char)text[i]<=0x7E))++i;}
            else ++i;
            continue;
        }
        out+=c<0x20||c==0x7F?' ':char(c);
    }
    // runs of spaces left by removed sequences and line breaks
    std::string squeezed;for(char c:out)if(!(c==' '&&(squeezed.empty()||squeezed.back()==' ')))squeezed+=c;
    while(!squeezed.empty()&&squeezed.back()==' ')squeezed.pop_back();
    if(squeezed.size()>200)squeezed=squeezed.substr(0,200)+"...";
    return squeezed;
}
std::string failure_for(const std::string& mod,const std::string& model){std::lock_guard guard(mutex);auto found=failures.find({mod,model});return found==failures.end()?std::string():found->second;}
void note_failure(const std::string& mod,const std::string& model,const std::string& why){std::lock_guard guard(mutex);failures[{mod,model}]=why.empty()?std::string("it could not be prepared"):why;}
void clear_failure(const std::string& mod,const std::string& model){std::lock_guard guard(mutex);failures.erase({mod,model});}
bool preparing(const std::string& mod,const std::string& model){std::lock_guard guard(mutex);return running.contains({mod,model});}
void hold(const std::string& mod,const std::string& model){std::lock_guard guard(mutex);++running[{mod,model}];}
void release(const std::string& mod,const std::string& model){std::lock_guard guard(mutex);const auto found=running.find({mod,model});if(found!=running.end()&&!--found->second)running.erase(found);}
std::string prepare(const std::string& mod,const story_notebook::ManifestModel& model,
                    const std::string& package_dir,const std::string& data_dir,
                    const std::vector<std::string>& python,const std::string& tool,const std::string& game_dir,bool collision,bool shoreline,const std::string& material_in,
                    unsigned generation){
    const std::pair<std::string,std::string> key{mod,model.id};
    {std::lock_guard guard(mutex);++running[key];}
    struct Done {const std::pair<std::string,std::string>& key;~Done(){std::lock_guard guard(mutex);if(!--running[key])running.erase(key);}} done{key};
    try {
        auto identity=prepare_once(mod,model,package_dir,data_dir,python,tool,game_dir,collision,shoreline,material_in,generation);
        clear_failure(mod,model.id);
        return identity;
    } catch(const std::exception& e) {note_failure(mod,model.id,e.what());throw;}
}
namespace {
std::string prepare_once(const std::string& mod,const story_notebook::ManifestModel& model,
                         const std::string& package_dir,const std::string& data_dir,
                         const std::vector<std::string>& python,const std::string& tool,const std::string& game_dir,bool collision,bool shoreline,const std::string& material_in,
                         unsigned generation){
    std::lock_guard preparing(prepare_mutex);namespace fs=std::filesystem;
    const bool owner_ok=!mod.empty()&&mod.size()<=64&&mod.front()!='.'&&std::all_of(mod.begin(),mod.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';});
    check(owner_ok&&story_mail::valid_id(model.id),"Invalid model owner or id");
    mods::json::Value manifest,source;source["id"]=model.id;source["source"]=model.source;source["kind"]=model.kind;manifest["models"].type=mods::json::Value::Array;manifest["models"].array.push_back(source);
    std::vector<story_notebook::ManifestModel> validated;std::string why;check(story_notebook::parse_models(manifest,validated,why),"Invalid model source path");
    check(!package_dir.empty()&&!data_dir.empty()&&!game_dir.empty()&&!python.empty()&&!tool.empty(),"Static model conversion tools are unavailable");
    auto input=fs::path(package_dir);for(const auto& part:fs::path(model.source)){input/=part;check(!fs::is_symlink(input),"Model source paths may not use symlinks");}
    check(fs::is_regular_file(input)&&fs::file_size(input)<=8*1024*1024,"Missing or oversized static model source");
    const auto identity="mod"+mods::hash::sha256_text(mod+std::string(1,'\0')+model.id+(generation?std::string(1,'\0')+std::to_string(generation):std::string())).substr(0,28);
    const auto destination=fs::path(data_dir)/"models"/(identity+".szs");
    {
        std::lock_guard guard(mutex);auto old=entries.find(identity);
        check(old==entries.end()||(old->second.mod==mod&&old->second.model==model.id),"Static model archive identity collision");
    }
    // An archive prepared under this identity before stays served while it is prepared again (the editor's add_actor
    // asks once more for the collision of a model that the import prepared with it). The converter replaces its
    // files atomically, so a read meanwhile gets the old or the new archive whole. Taking the mapping away for the
    // whole conversion sent the game's read for an actor created meanwhile to a file that is not there: the load
    // "completed" without the archive, its model was missing, and large models (a longer conversion) lost that race
    // (#281). A failed rebuild still never exposes stale output: the mapping goes when the rebuild fails.
    struct Unpublish {const std::string& identity;bool committed=false;~Unpublish(){if(committed)return;std::lock_guard guard(mutex);entries.erase(identity);}} unpublish{identity};
    std::ifstream source_file(input,std::ios::binary);std::vector<uint8_t> bytes(8*1024*1024+1);source_file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));bytes.resize(size_t(source_file.gcount()));check(bytes.size()<=8*1024*1024&&!source_file.bad(),"Oversized or unreadable static model source");
    static_models::ModelInfo info;std::vector<std::string> clips;mod_actor::ModelRig rig;
    if(model.kind=="skinned"){
        skinned_models::ModelInfo skin;check(skinned_models::validate_glb(bytes,skin,why),"Invalid skinned model source");
        info=skin;rig={skin.joint_names,skin.joint_parents,skin.joint_look_compatible};for(const auto& clip:skin.clips)clips.push_back(clip.name);
    }else check(static_models::validate_glb(bytes,info,why,collision),"Invalid static model source");
    const story_notebook::WorldPoint minimum{info.minimum[0],info.minimum[1],info.minimum[2]},maximum{info.maximum[0],info.maximum[1],info.maximum[2]};
    check(!fs::is_symlink(fs::path(data_dir)/"models"),"Static model cache cannot use a symlink directory");
    fs::create_directories(destination.parent_path());
    for(const auto& suffix:{"",".tmp",".json",".json.tmp",".wcol",".wcol.tmp"})check(!fs::is_symlink(destination.string()+suffix),"Static model cache cannot use symlink outputs");
    // The converter verifies source/template/version hashes and the cached archive
    // digest before reuse. Keep the existing bytes available for that check;
    // mappings remain absent until a successful conversion or verified cache hit.

    auto command=python;command.push_back(tool);command.insert(command.end(),{"--game",game_dir,"--glb",input.string(),"--identity",identity,"--out",destination.string()});
    const auto collision_path=destination.string()+".wcol";
    if(collision){check(model.kind=="static","Collision requires a static model");command.insert(command.end(),{"--collision-out",collision_path});}
    const std::string material=material_in=="foam"?std::string():material_in;
    check(material.empty()||story_notebook::water_surface_material(material),"Unknown shoreline material");
    check(shoreline||material.empty(),"A water surface is a shoreline model");
    if(shoreline){check(!collision&&model.kind=="static","Shoreline requires a collision-free static model");command.push_back("--shoreline");if(!material.empty())command.insert(command.end(),{"--shoreline-material",material});}
    const auto process=host::run_process(command);
    if(!process.error.empty()||process.code!=0){
        // the converter's own reason (its last line, "actor_model: ..."), so the editor says what went wrong
        std::string reason=process.error;
        if(reason.empty()){const auto end=process.output.find_last_not_of("\r\n");if(end!=std::string::npos){const auto start=process.output.rfind('\n',end);reason=process.output.substr(start==std::string::npos?0:start+1,end-(start==std::string::npos?0:start+1)+1);}}
        reason=plain_reason(reason);
        exception_report::raise("Static model conversion failed"+(reason.empty()?std::string():" ("+reason+")")+"; check the model and player game resources");
    }
    const auto end=process.output.find_last_not_of("\r\n");check(end!=std::string::npos,"Static model converter returned no result");
    const auto line=process.output.rfind('\n',end);const auto begin=line==std::string::npos?0:line+1;
    mods::json::Value result;try{result=mods::json::parse(process.output.substr(begin,end-begin+1));}catch(...){exception_report::raise("Static model converter returned invalid metadata");}
    check(result.get("shoreline").boolean==shoreline&&(!shoreline||result.get("shoreline_material").string()==(material.empty()?"foam":material)),"Wrong shoreline conversion mode");
    check(result.get("identity").string()==identity,"Static model converter returned another archive identity");
    check(static_models::integer(result.get("triangles"),1,static_models::kMaxTriangles)&&unsigned(result.get("triangles").number)==info.triangle_count,"Static model converter returned inconsistent render triangle metadata");
    check(fs::is_regular_file(destination)&&!fs::is_symlink(destination)&&fs::file_size(destination)>=16&&fs::file_size(destination)<=16*1024*1024,"Static model converter produced a missing or oversized archive");
    std::ifstream archive(destination,std::ios::binary);char magic[4]{};archive.read(magic,4);check(archive&&std::string(magic,4)=="Yaz0","Static model converter produced an invalid archive");
    std::shared_ptr<const story_world_collision::mesh::Mesh> collision_data;
    if(collision){
        check(fs::is_regular_file(collision_path)&&!fs::is_symlink(collision_path)&&fs::file_size(collision_path)<=story_world_collision::mesh::MaxWcolBytes,"Invalid collision sidecar");
        std::ifstream file(collision_path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
        auto decoded=std::make_shared<story_world_collision::mesh::Mesh>();
        check(story_world_collision::mesh::decode(bytes,*decoded,why),"Invalid converted collision geometry");
        check(result.get("collision_sha256").string()==mods::hash::sha256_text(std::string(bytes.begin(),bytes.end())),"Collision sidecar digest mismatch");
        collision_data=std::move(decoded);
    }
    {std::lock_guard guard(mutex);entries.insert_or_assign(identity,Entry{mod,model.id,destination.string(),info.triangle_count,minimum,maximum,std::move(clips),std::move(rig),std::move(collision_data),shoreline,material});}
    unpublish.committed=true;
    return identity;
}
}
bool register_external(const std::string& identity,const std::string& path,unsigned triangles){
    if(identity.size()!=31||path.empty())return false;
    std::lock_guard guard(mutex);auto found=entries.find(identity);
    if(found!=entries.end())return found->second.path==path&&found->second.mod.empty();
    entries.emplace(identity,Entry{"","",path,triangles});return true;
}
bool shoreline_for(const std::string& archive){std::lock_guard guard(mutex);auto found=entries.find(archive);return found!=entries.end()&&found->second.shoreline;}
bool water_for(const std::string& archive){return material_for(archive)=="water";}
std::string material_for(const std::string& archive){std::lock_guard guard(mutex);auto found=entries.find(archive);return found!=entries.end()&&found->second.shoreline?found->second.material:std::string();}
unsigned triangles_for(const std::string& archive){std::lock_guard guard(mutex);auto found=entries.find(archive);return found==entries.end()?0:found->second.triangles;}
std::shared_ptr<const story_world_collision::mesh::Mesh> collision_for(const std::string& archive){std::lock_guard guard(mutex);auto found=entries.find(archive);return found==entries.end()?nullptr:found->second.collision;}
std::string archive_for(const std::string& guest_path){
    constexpr const char* prefix="/vol/content/Common/Object/";constexpr size_t prefix_size=27;
    if(!guest_path.starts_with(prefix)||guest_path.size()!=prefix_size+31+4||!guest_path.ends_with(".szs"))return {};
    const auto identity=guest_path.substr(prefix_size,31);std::lock_guard guard(mutex);auto found=entries.find(identity);return found==entries.end()?std::string():found->second.path;
}
bool bounds_for(const std::string& archive,story_notebook::WorldPoint& minimum,story_notebook::WorldPoint& maximum){
    std::lock_guard guard(mutex);auto found=entries.find(archive);if(found==entries.end())return false;
    minimum=found->second.minimum;maximum=found->second.maximum;return true;
}
mod_actor::ModelRig rig_for(const std::string& archive){std::lock_guard guard(mutex);auto found=entries.find(archive);return found==entries.end()?mod_actor::ModelRig{}:found->second.rig;}
std::vector<std::string> clips_for(const std::string& archive){std::lock_guard guard(mutex);auto found=entries.find(archive);return found==entries.end()?std::vector<std::string>{}:found->second.clips;}
}
