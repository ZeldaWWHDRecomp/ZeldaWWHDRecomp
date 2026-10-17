// SDK v3 preview: bounded declarative world content. No game save bits are accepted here.
#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <utility>
#include <string>
#include <vector>
#include "mod_json.h"
#include "story_mail_book.h"
#include "story_scenes_core.h"
#include "story_mod_combat_manifest.h"
namespace story_notebook {
struct WorldPoint { float x=0,y=0,z=0; };
struct StoryAction {
    enum class Kind { Give,Take,QuestStep,Flag,StartScene };
    Kind kind=Kind::Flag; std::string id; unsigned value=1;
};
struct ManifestModel { std::string id,source,kind="static"; };
struct ManifestLookAt {
    bool enabled=false;std::string head,upper_bone;
    float yaw_degrees=0,pitch_degrees=0,rate_degrees=0,radius=0,upper_yaw_degrees=0,upper_fraction=0;
};
struct ManifestPath {std::vector<WorldPoint> points;float speed=100;bool loop=false;};
struct ManifestPropDrop {std::string item;unsigned quantity=1;};
struct ManifestActor {
    std::string id,actor,stage,kind,model,animation,prompt="check",texture,texture_digest; unsigned room=0,texture_token=0;
    float collision_radius=30,collision_height=100; bool pushable=false;
    ManifestLookAt look_at;ManifestPath path;
    bool carry_enabled=false,carry_break_on_impact=false;
    bool combat_enabled=false,native_heart_drop=false;story_mod_combat::Config combat;
    WorldPoint position,rotation,scale{1,1,1}; uint32_t params=0; unsigned resident=9; // 19 is the seated Ym1 adapter, not a People subtype.
    uint8_t item_game=6;
    std::string broken_flag,switch_flag,switch_actor,defeated_flag;std::vector<ManifestPropDrop> drops;
    std::string flag,quest,item,time,lines; unsigned quest_step=0;
    std::vector<StoryAction> then;
};
struct SceneCameraKey {WorldPoint eye,target;unsigned frames=0;};
struct SceneChoiceOption {std::string text,next;size_t next_step=0;};
struct SceneStep {
    enum class Kind { Camera,Say,Move,Face,Wait,Sound,Fade,End,CameraPath,CameraFollow,Animation,Walk,Choice,Music,Particle };
    Kind kind=Kind::Wait; std::string actor,target_actor,text,sound;
    std::string id,next,clip,quest_place,cue,music_action,particle,easing="linear";
    std::vector<size_t> successors;std::vector<SceneCameraKey> camera_keys;
    WorldPoint eye_offset,target_offset;float half_life=0.2f,blend_seconds=0,speed=100;bool loop=true;
    std::vector<WorldPoint> walk_points;std::vector<SceneChoiceOption> choices;unsigned default_choice=0,fade_frames=0;
    WorldPoint eye,target,position,eye_to,target_to; unsigned frames=0,particle_alpha=255; float particle_scale=1; bool pan=false,fade_in=false,particle_has_position=false;
    std::vector<StoryAction> then;
};
struct ManifestScene {
    std::string id; std::vector<SceneStep> steps;
    std::string trigger,stage,actor,quest,chart; unsigned room=0,quest_step=0;
    WorldPoint position; float radius=0;
};
inline constexpr unsigned kMaxActorsPerRoom=16,kMaxActorsPerMod=64,kMaxScenesPerMod=16,kMaxSceneSteps=64,kMaxSceneFrames=18000;
namespace world_manifest {
using mods::json::Value;
inline bool integer(const Value& v,double lo,double hi) {return v.type==Value::Number&&std::isfinite(v.number)&&v.number>=lo&&v.number<=hi&&std::floor(v.number)==v.number;}
inline bool id(const Value& v) {return v.type==Value::String&&story_mail::valid_id(v.text);}
inline bool keys(const Value& v,std::initializer_list<const char*> allowed) {
    if(v.type!=Value::Object)return false;
    for(const auto& [key,value]:v.object){bool found=false;for(const auto name:allowed)found|=key==name;if(!found)return false;}return true;
}
inline bool number(const Value&v,double low,double high,float&out,bool positive=false){
    if(v.type!=Value::Number||!std::isfinite(v.number)||v.number<low||v.number>high||(positive&&v.number<=0))return false;
    out=float(v.number);return std::isfinite(out)&&(!positive||out>0);
}
inline bool bone_name(const Value&v){if(v.type!=Value::String||v.text.empty()||v.text.size()>31)return false;const auto alpha=[](char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z');};if(!alpha(v.text[0]))return false;for(char c:v.text)if(!alpha(c)&&!(c>='0'&&c<='9')&&c!='_')return false;return true;}
inline bool texture_path(const Value&v){
    if(v.type!=Value::String||v.text.size()<12||v.text.size()>240||
       (!v.text.starts_with("textures/")&&!v.text.starts_with("assets/"))||!v.text.ends_with(".png"))return false;
    size_t start=0;while(start<v.text.size()){const size_t end=v.text.find('/',start);const auto part=v.text.substr(start,end==std::string::npos?end:end-start);if(part.empty()||part=="."||part=="..")return false;for(unsigned char c:part)if(c<32||c==127||c=='\\'||c==':')return false;if(end==std::string::npos)break;start=end+1;}return true;
}
inline bool flag_id(const Value& v) {return id(v)&&v.text.rfind("0x",0)!=0;}
inline bool point(const Value& v,WorldPoint& p,float limit=350000) {
    if(v.type!=Value::Object) return false;
    for(auto key:{"x","y","z"}) if(v.get(key).type!=Value::Number||!std::isfinite(v.get(key).number)||std::fabs(v.get(key).number)>limit) return false;
    p={float(v.get("x").number),float(v.get("y").number),float(v.get("z").number)}; return true;
}
inline bool declared(const Value& m,const char* section,const std::string& name) {
    for(const auto& v:m.get(section).array) if(v.get("id").type==Value::String&&v.get("id").text==name) return true;
    return false;
}
inline bool quest_step(const Value& m,const std::string& quest,unsigned step) {
    for(const auto& q:m.get("quests").array) if(q.get("id").string()==quest)
        for(const auto& s:q.get("steps").array) if(integer(s.get("n"),1,254)&&s.get("n").number==step) return true;
    return false;
}
inline bool character(const Value& m,const std::string& name) {return name=="link"||declared(m,"actors",name);}
inline bool resident_character(const Value& m,const std::string& name) {
    for(const auto& a:m.get("actors").array)if(a.get("id").string()==name){const auto type=a.get("actor").string();return a.get("parameters").get("resident").string()!="ym1"&&(type=="resident"||type=="NpcPeople"||type=="PROC_NPC_PEOPLE");}return false;
}
inline bool mod_character(const Value&m,const std::string&name,bool skinned=false){
    for(const auto&actor:m.get("actors").array)if(actor.get("id").string()==name&&actor.get("kind").string()=="mod"){
        if(!skinned)return true;for(const auto&model:m.get("models").array)if(model.get("id").string()==actor.get("model").string())return model.get("kind").string()=="skinned";
    }return false;
}
inline bool scene_duration(const Value&v,unsigned&frames,std::string&why){
    const bool f=v.object.contains("frames"),s=v.object.contains("seconds");if(f&&s)return why="use frames or seconds, not both",false;
    if(s){const auto&n=v.get("seconds");if(n.type!=Value::Number||!std::isfinite(n.number)||n.number<=0||n.number>300)return why="scene seconds must be in (0,300]",false;frames=unsigned(std::ceil(n.number*60));}
    else{if(!integer(v.get("frames"),1,kMaxSceneFrames))return why="scene step requires frames in 1..18000",false;frames=unsigned(v.get("frames").number);}return true;
}
inline bool scene_point(const Value&v,WorldPoint&p){return keys(v,{"x","y","z"})&&point(v,p);}
inline bool camera_separated(const WorldPoint&a,const WorldPoint&b){return std::hypot(std::hypot(double(a.x)-b.x,double(a.y)-b.y),double(a.z)-b.z)>0.0001;}
inline bool camera_segment(const SceneCameraKey&a,const SceneCameraKey&b){
    const double from[]={double(a.eye.x)-a.target.x,double(a.eye.y)-a.target.y,double(a.eye.z)-a.target.z};const double to[]={double(b.eye.x)-b.target.x,double(b.eye.y)-b.target.y,double(b.eye.z)-b.target.z};double dot=0,length=0;for(size_t k=0;k<3;++k){dot+=from[k]*(to[k]-from[k]);length+=(to[k]-from[k])*(to[k]-from[k]);}const double t=length?std::clamp(-dot/length,0.0,1.0):0;double distance=0;for(size_t k=0;k<3;++k){const double d=from[k]+(to[k]-from[k])*t;distance+=d*d;}return distance>1e-8;
}
inline bool actions(const Value& m,const Value& v,std::vector<StoryAction>& out,std::string& why) {
    if(v.type==Value::Null) return true;
    if(v.type!=Value::Array||v.array.size()>8) return why="then must be a list of up to 8 actions",false;
    for(const auto& a:v.array) {
        StoryAction s; const auto kind=a.get("action").string(); s.id=a.get("id").string();
        if(kind=="give"||kind=="take") {s.kind=kind=="give"?StoryAction::Kind::Give:StoryAction::Kind::Take;
            if(!keys(a,{"action","id","count"}))return why="unknown give/take action field",false;
            if(a.object.contains("count")){if(!integer(a.get("count"),1,254))return why="action count must be 1..254",false;s.value=unsigned(a.get("count").number);}
            if(s.id.rfind("item:",0)!=0||!declared(m,"items",s.id.substr(5))) return why="action names an undeclared mod item",false;
        } else if(kind=="quest_step") {s.kind=StoryAction::Kind::QuestStep;if(!keys(a,{"action","id","step"}))return why="unknown quest action field",false;
            if(!integer(a.get("step"),1,254)||!quest_step(m,s.id,unsigned(a.get("step").number))) return why="action names an undeclared quest step",false;
            s.value=unsigned(a.get("step").number);
        } else if(kind=="flag") {s.kind=StoryAction::Kind::Flag;if(!keys(a,{"action","id","value"}))return why="unknown flag action field",false;
            if(a.object.contains("value")){if(a.get("value").type!=Value::Bool)return why="flag value must be boolean",false;s.value=a.get("value").boolean?1:0;}if(!flag_id(a.get("id"))) return why="flag must be a mod-owned id",false;
        } else if(kind=="scene") {s.kind=StoryAction::Kind::StartScene;if(!keys(a,{"action","id"}))return why="unknown scene action field",false;if(!declared(m,"scenes",s.id)) return why="action names an undeclared scene",false;
        } else return why="unknown then action",false;
        out.push_back(s);
    } return true;
}
inline bool place(const Value& v,std::string& stage,unsigned& room) {
    if(v.get("stage").type!=Value::String||v.get("stage").text.empty()||v.get("stage").text.size()>8||!integer(v.get("room"),0,63)) return false;
    stage=v.get("stage").text;
    for(char c:stage) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
    room=unsigned(v.get("room").number);return true;
}
// Per-type fields after common placement validation. Native enemy names stay
// outside parse_actors' allowlist until their runtime adapters are integrated.
inline bool native_enemy_fields(const Value& m,const Value& v,ManifestActor& a,std::string& why) {
    if(a.actor!="chuchu"&&a.actor!="bokoblin"&&a.actor!="moblin"&&a.actor!="keese")return why="unsupported native enemy",false;
    for(const char* key:{"kind","params","parameters","flag","broken_flag","switch_flag","switch","lines","then","model","animation","look_at","path","collision","attention"})
        if(v.object.contains(key))return why="native enemies cannot override parameters, behavior, dialogue or models",false;
    if(a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1)return why="native enemies require yaw-only rotation and scale 1",false;
    if(!flag_id(v.get("defeated_flag")))return why="native enemies require a mod-owned defeated_flag",false;
    const auto& drops=v.get("drops");if(drops.type!=Value::Array||drops.array.size()>8)return why="native enemies require up to eight mod-item drops",false;
    std::vector<ManifestPropDrop> parsed;
    for(const auto& drop:drops.array){
        if(!keys(drop,{"item","quantity"})||drop.get("item").type!=Value::String||drop.get("item").text.rfind("item:",0)!=0||!declared(m,"items",drop.get("item").text.substr(5))||!integer(drop.get("quantity"),1,254))return why="invalid same-mod enemy drop",false;
        const auto item=drop.get("item").text.substr(5);for(const auto& old:parsed)if(old.item==item)return why="duplicate enemy drop item",false;
        parsed.push_back({item,unsigned(drop.get("quantity").number)});
    }
    a.defeated_flag=v.get("defeated_flag").text;a.drops=std::move(parsed);return true;
}
inline bool lines(const Value& v,std::string& text) {
    if(v.type==Value::String) text=v.text;
    else if(v.type==Value::Array&&v.array.size()<=3) {for(const auto& l:v.array){if(l.type!=Value::String)return false;if(!text.empty())text+='\n';text+=l.text;}}
    else return false;
    unsigned breaks=0;for(char c:text)breaks+=c=='\n';
    std::u16string ignored;return !text.empty()&&text.size()<=240&&breaks<=2&&story_mail::append_utf16(text,true,ignored);
}
}
inline bool parse_models(const mods::json::Value& m,std::vector<ManifestModel>& out,std::string& why) {
    using namespace world_manifest;out.clear();const auto& list=m.get("models");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>8)return why="models must be a list of up to 8",false;
    std::vector<ManifestModel> parsed;
    for(const auto& v:list.array){
        if(!keys(v,{"id","source","kind"})||!id(v.get("id"))||v.get("source").type!=Value::String)return why="invalid model declaration",false;
        const std::string kind=v.object.contains("kind")?v.get("kind").string():"static";
        if(kind!="static"&&kind!="skinned")return why="model kind must be static or skinned",false;
        const auto& source=v.get("source").text;
        if(source.empty()||source.size()>240||source.front()=='/'||source.find('\\')!=std::string::npos||source.find(':')!=std::string::npos||source.size()<5||source.substr(source.size()-4)!=".glb")return why="model source must be a package-relative .glb",false;
        size_t start=0;while(start<=source.size()){const auto end=source.find('/',start);const auto part=source.substr(start,end==std::string::npos?end:end-start);if(part.empty()||part=="."||part=="..")return why="invalid model source path",false;if(end==std::string::npos)break;start=end+1;}
        for(unsigned char c:source)if(c<32||c==127)return why="invalid model source path",false;
        for(const auto& old:parsed)if(old.id==v.get("id").text)return why="duplicate model id",false;
        parsed.push_back({v.get("id").text,source,kind});
    }out=std::move(parsed);return true;
}
inline bool parse_actors_impl(const mods::json::Value& m,std::vector<ManifestActor>& out,std::string& why) {
    using namespace world_manifest;out.clear();std::vector<ManifestModel> models;if(!parse_models(m,models,why))return false;const auto& list=m.get("actors");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>kMaxActorsPerMod)return why="actors must be a list of up to 64",false;
    for(const auto& v:list.array){ManifestActor a;
        if(!keys(v,{"id","actor","kind","model","animation","look_at","path","collision","attention","stage","room","position","rotation","scale","params","parameters","flag","broken_flag","defeated_flag","drops","carry","combat","native_drops","switch_flag","switch","when","lines","then","texture"}))return why="unknown actor field",false;
        if(!id(v.get("id")))return why="invalid actor id",false;a.id=v.get("id").text;a.actor=v.get("actor").string();
        if(v.object.contains("kind")) {
            if(v.get("kind").string()!="mod"||v.object.contains("actor"))return why="mod actor kind cannot name a native actor",false;
            a.kind="mod";a.actor="mod";a.model=v.get("model").string();
            if(!declared(m,"models",a.model))return why="mod actor names an undeclared model",false;
            bool skinned_model=false;for(const auto& model:models)if(model.id==a.model)skinned_model=model.kind=="skinned";
            if(v.object.contains("look_at")){
                const auto& look=v.get("look_at");if(!skinned_model)return why="look_at requires a skinned model",false;
                if(!keys(look,{"head","yaw_degrees","pitch_degrees","rate_degrees","radius","upper_body"})||!bone_name(look.get("head"))||!number(look.get("yaw_degrees"),0,180,a.look_at.yaw_degrees)||!number(look.get("pitch_degrees"),0,90,a.look_at.pitch_degrees)||!number(look.get("rate_degrees"),0,1080,a.look_at.rate_degrees,true)||!number(look.get("radius"),0,1000000,a.look_at.radius,true))return why="invalid bounded look_at settings",false;
                a.look_at.enabled=true;a.look_at.head=look.get("head").text;
                if(look.object.contains("upper_body")){const auto& upper=look.get("upper_body");if(!keys(upper,{"bone","yaw_degrees","fraction"})||!bone_name(upper.get("bone"))||upper.get("bone").text==a.look_at.head||!number(upper.get("yaw_degrees"),0,180,a.look_at.upper_yaw_degrees)||!number(upper.get("fraction"),0,1,a.look_at.upper_fraction))return why="invalid upper_body look_at settings",false;a.look_at.upper_bone=upper.get("bone").text;}
            }
            if(v.object.contains("path")){
                const auto& path=v.get("path");if(!skinned_model)return why="path requires a skinned model",false;
                if(!keys(path,{"points","speed","mode"})||path.get("points").type!=Value::Array||path.get("points").array.size()<2||path.get("points").array.size()>16||!number(path.get("speed"),0,1500,a.path.speed,true))return why="invalid bounded actor path",false;
                const auto mode=path.get("mode").string();if(mode!="once"&&mode!="loop")return why="path mode must be once or loop",false;a.path.loop=mode=="loop";
                for(const auto& p:path.get("points").array){WorldPoint point_;if(!keys(p,{"x","y","z"})||!point(p,point_))return why="invalid path point",false;a.path.points.push_back(point_);}
                for(size_t n=1;n<a.path.points.size();++n){const auto& p=a.path.points[n-1];const auto& q=a.path.points[n];if(std::hypot(double(p.x)-q.x,double(p.z)-q.z)<=1)return why="path points must be separated horizontally by more than one unit",false;}
                if(a.path.loop){const auto& p=a.path.points.front();const auto& q=a.path.points.back();if(std::hypot(double(p.x)-q.x,double(p.z)-q.z)<=1)return why="loop closing edge is too short",false;}
            }
            if(v.object.contains("animation")){
                if(!id(v.get("animation")))return why="invalid initial animation name",false;
                bool skinned=false;for(const auto& model:models)if(model.id==a.model)skinned=model.kind=="skinned";
                if(!skinned)return why="animation requires a skinned model",false;a.animation=v.get("animation").text;
            }
            if(v.object.contains("params")||v.object.contains("parameters"))return why="mod actors cannot use native parameters",false;
            const auto& collision=v.get("collision");
            if(!keys(collision,{"radius","height","mode"}))return why="mod actor needs cylinder collision",false;
            const auto& radius=collision.get("radius");const auto& height=collision.get("height");
            if(radius.type!=Value::Number||height.type!=Value::Number||!std::isfinite(radius.number)||!std::isfinite(height.number)||radius.number<=0||radius.number>1000||height.number<=0||height.number>2000)return why="collision radius must be 0..1000 and height 0..2000 (exclusive zero)",false;
            const auto mode=collision.get("mode").string();if(mode!="solid"&&mode!="pushable")return why="collision mode must be solid or pushable",false;
            a.collision_radius=float(radius.number);a.collision_height=float(height.number);if(a.collision_radius<=0||a.collision_height<=0)return why="collision dimensions are too small",false;a.pushable=mode=="pushable";
            const auto& attention=v.get("attention");if(attention.type!=Value::Null){if(!keys(attention,{"prompt"}))return why="invalid mod actor attention",false;a.prompt=attention.get("prompt").string();if(a.prompt!="talk"&&a.prompt!="check")return why="attention prompt must be talk or check",false;}
            if(v.object.contains("flag")){if(!flag_id(v.get("flag")))return why="mod actor flag must be a mod-owned id",false;a.flag=v.get("flag").text;}
            if(v.object.contains("combat")){
                if(!skinned_model||!story_mod_combat::parse(v.get("combat"),a.combat,why))return why=why.empty()?"combat requires a skinned model":why,false;
                if(a.combat.stun.clip.empty()||a.combat.death.clip.empty())return why="combat requires named stun and death animation clips",false;
                for(const char* forbidden:{"flag","lines","then","attention"})if(v.object.contains(forbidden))return why="combat actors cannot declare dialogue, prompts or generic flags",false;
                if(!flag_id(v.get("defeated_flag")))return why="combat requires a mod-owned defeated_flag",false;
                if(a.pushable||a.collision_radius>200||a.collision_height>400)return why="combat requires solid collision with radius up to 200 and height up to 400",false;
                a.combat_enabled=true;a.defeated_flag=v.get("defeated_flag").text;
                const auto& drops=v.get("drops");if(drops.type!=Value::Array||drops.array.size()>8)return why="combat requires up to eight mod-item drops",false;
                for(const auto& drop:drops.array){
                    if(!keys(drop,{"item","quantity"})||drop.get("item").type!=Value::String||drop.get("item").text.rfind("item:",0)!=0||!declared(m,"items",drop.get("item").text.substr(5))||!integer(drop.get("quantity"),1,254))return why="invalid same-mod combat drop",false;
                    const auto item=drop.get("item").text.substr(5);for(const auto& old:a.drops)if(old.item==item)return why="duplicate combat drop item",false;
                    a.drops.push_back({item,unsigned(drop.get("quantity").number)});
                }
                if(v.object.contains("native_drops")&&!story_mod_combat::parse_native_drops(v.get("native_drops"),a.native_heart_drop,why))return false;
            }
        }else if(v.object.contains("model")||v.object.contains("animation")||v.object.contains("look_at")||v.object.contains("path")||v.object.contains("collision")||v.object.contains("attention"))return why="model, animation, look_at, path, collision and attention require kind mod",false;
        if(v.object.contains("carry")){
            const auto& carry=v.get("carry");if(a.kind!="mod"||a.combat_enabled||!keys(carry,{"break_on_impact"})||(carry.object.contains("break_on_impact")&&carry.get("break_on_impact").type!=Value::Bool))return why="carry requires a noncombat mod actor and boolean break_on_impact",false;
            a.carry_enabled=true;a.carry_break_on_impact=carry.get("break_on_impact").boolean;
            if(!v.object.contains("collision")||v.object.contains("attention")||v.object.contains("lines")||v.object.contains("then")||v.object.contains("flag")||a.look_at.enabled||a.pushable||a.collision_radius>100||a.collision_height>200)return why="carry actors require a bounded solid body and no talk, look_at or pushable mode",false;
        }
        if((v.object.contains("combat")||v.object.contains("native_drops"))&&!a.combat_enabled)return why="combat and native_drops require a combat mod actor",false;
        if(a.actor=="kanban"||a.actor=="PROC_KANBAN")a.actor="sign";
        if(a.actor=="Tbox"||a.actor=="takara"||a.actor=="PROC_TBOX")a.actor="chest";
        if(a.actor=="NpcPeople"||a.actor=="PROC_NPC_PEOPLE")a.actor="resident";
        const bool prop=a.actor=="pot"||a.actor=="barrel"||a.actor=="crate";
        if(v.object.contains("texture")){if(!prop||!texture_path(v.get("texture")))return why="texture requires a placed pot, barrel or crate and a package PNG in textures/ or assets/",false;a.texture=v.get("texture").text;}
        const bool switch_actor=a.actor=="switch",door=a.actor=="door";
        const bool enemy=a.actor=="chuchu"||a.actor=="bokoblin"||a.actor=="moblin"||a.actor=="keese";
        if(a.actor!="sign"&&a.actor!="resident"&&a.actor!="chest"&&!prop&&!switch_actor&&!door&&!enemy&&!(a.actor=="mod"&&a.kind=="mod"))return why="unknown or unsupported flag-setting actor: "+a.actor,false;
        if(!place(v,a.stage,a.room)||!point(v.get("position"),a.position))return why="actor "+a.id+": invalid stage, room or position",false;
        if(v.get("rotation").type!=Value::Null&&!point(v.get("rotation"),a.rotation,360))return why="invalid actor rotation",false;
        if(v.get("scale").type!=Value::Null&&(!point(v.get("scale"),a.scale,10)||a.scale.x<=0||a.scale.y<=0||a.scale.z<=0))return why="invalid actor scale",false;
        if(enemy){
            if(!native_enemy_fields(m,v,a,why))return false;
            for(const auto& old:out)if(old.defeated_flag==a.defeated_flag)return why="enemies require distinct defeated_flag IDs",false;
        }else if(a.combat_enabled){
            if(a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1)return why="combat actors require yaw-only rotation and scale 1",false;
            for(const auto& old:out)if(old.defeated_flag==a.defeated_flag)return why="combat actors require distinct defeated_flag IDs",false;
        }else if(v.object.contains("defeated_flag"))return why="defeated_flag requires an enemy",false;
        if(a.carry_enabled&&(a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1))return why="carry actors require yaw-only rotation and scale 1",false;
        if(switch_actor||door){
            if(v.object.contains("params")||v.object.contains("parameters")||v.object.contains("flag")||v.object.contains("lines")||v.object.contains("then")||v.object.contains("broken_flag")||v.object.contains("drops"))return why="switches and doors cannot use native parameters, dialogue, drops or generic flags",false;
            if(a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1)return why="switches and doors require yaw-only rotation and scale 1",false;
            if(switch_actor){
                if(!flag_id(v.get("switch_flag"))||v.object.contains("switch"))return why="switch requires its own switch_flag and no source switch",false;
                a.switch_flag=v.get("switch_flag").text;
                for(const auto& old:out)if(!old.switch_flag.empty()&&old.switch_flag==a.switch_flag)return why="switches require distinct switch_flag IDs",false;
            }else{
                if(!id(v.get("switch"))||v.object.contains("switch_flag"))return why="door requires a same-mod switch actor id",false;
                a.switch_actor=v.get("switch").text;
            }
        }else if(v.object.contains("switch_flag")||v.object.contains("switch"))return why="switch_flag and switch require switch or door actors",false;
        if(prop||a.carry_break_on_impact){
            if(!flag_id(v.get("broken_flag")))return why="props require a mod-owned broken_flag",false;
            a.broken_flag=v.get("broken_flag").text;
            for(const auto& old:out)if(!old.broken_flag.empty()&&old.broken_flag==a.broken_flag)return why="props must have distinct broken_flag IDs",false;
            if(v.object.contains("params")||v.object.contains("parameters")||v.object.contains("flag")||v.object.contains("lines")||v.object.contains("then"))return why="props cannot use native parameters, dialogue or actor flags",false;
            if(a.rotation.x!=0||a.rotation.z!=0||a.scale.x<.1f||a.scale.x>4||a.scale.x!=a.scale.y||a.scale.x!=a.scale.z)return why="props require yaw-only rotation and uniform scale 0.1..4",false;
            const auto& drops=v.get("drops");if(drops.type!=Value::Array||drops.array.size()>8)return why="props require a drops list of up to eight items",false;
            for(const auto& drop:drops.array){
                if(!keys(drop,{"item","quantity"})||drop.get("item").type!=Value::String||drop.get("item").text.rfind("item:",0)!=0||!declared(m,"items",drop.get("item").text.substr(5))||!integer(drop.get("quantity"),1,254))return why="invalid same-mod prop drop",false;
                const std::string item=drop.get("item").text.substr(5);for(const auto& old:a.drops)if(old.item==item)return why="duplicate prop drop item",false;
                a.drops.push_back({item,unsigned(drop.get("quantity").number)});
            }
        }else if(v.object.contains("broken_flag")||(!enemy&&!a.combat_enabled&&v.object.contains("drops")))return why="broken_flag and drops require a prop or enemy actor",false;
        if(v.get("params").type!=Value::Null) {if(a.actor!="chest"||!integer(v.get("params"),0,0))return why="unsupported actor params: only ordinary chest params 0 are safe",false;}
        const auto& named=v.get("parameters");
        if(named.type!=Value::Null&&named.type!=Value::Object)return why="actor parameters must be an object",false;
        for(const auto& [key,value]:named.object) {
            if(key=="resident"&&a.actor=="resident") {if(value.type==Value::String&&(value.text=="uo2"||value.text=="um1"||value.text=="ym1"))a.resident=value.text=="uo2"?1:value.text=="ym1"?19:9;else if(integer(value,9,9))a.resident=9;else return why="resident must be uo2, um1 or seated ym1 (legacy numeric 9)",false;}
            else if(key=="item"&&a.actor=="chest") {static const char* names[]={"rupee_green","rupee_blue","rupee_yellow","rupee_red","rupee_purple","rupee_orange"};bool found=false;for(unsigned i=0;i<6;++i)if(value.type==Value::String&&value.text==names[i]){a.item_game=uint8_t(i+1);found=true;}if(!found)return why="chest item must be a rupee",false;}
            else return why="unsupported named actor parameter: "+key,false;
        }
        if(a.actor=="resident"&&a.resident==19&&std::fabs(a.position.x+202066.f)<10&&std::fabs(a.position.z-320123.f)<10)return why="ym1 position intersects native relocation area",false;
        if(a.actor=="sign"||a.actor=="chest"){if(!flag_id(v.get("flag")))return why="chests and signs require a mod-owned flag id",false;a.flag=v.get("flag").text;}
        const auto& when=v.get("when");if(when.type!=Value::Null&&!keys(when,{"quest","step","item","time"}))return why="actor when must be an object",false;
        if(when.get("quest").type!=Value::Null){a.quest=when.get("quest").string();if(!integer(when.get("step"),1,254)||!quest_step(m,a.quest,unsigned(when.get("step").number)))return why="actor condition names an undeclared quest step",false;a.quest_step=unsigned(when.get("step").number);}
        if(when.get("item").type!=Value::Null){a.item=when.get("item").string();if(a.item.rfind("item:",0)!=0||!declared(m,"items",a.item.substr(5)))return why="actor condition names an undeclared mod item",false;}
        if(when.get("time").type!=Value::Null){a.time=when.get("time").string();if(a.time!="day"&&a.time!="night")return why="actor time must be day or night",false;}
        if(v.get("lines").type!=Value::Null&&!lines(v.get("lines"),a.lines))return why="invalid actor dialogue",false;
        if(!actions(m,v.get("then"),a.then,why))return false;
        if(!a.then.empty()&&a.lines.empty())return why="actor then requires dialogue lines",false;
        unsigned count=1;for(const auto& old:out){if(old.id==a.id)return why="duplicate actor id",false;if(old.stage==a.stage&&old.room==a.room)++count;}
        if(count>kMaxActorsPerRoom)return why="actors exceed the 16 per room limit",false;
        out.push_back(std::move(a));
    }
    for(const auto& a:out)if(a.actor=="door"){
        const auto source=std::find_if(out.begin(),out.end(),[&](const ManifestActor& candidate){return candidate.id==a.switch_actor;});
        if(source==out.end()||source->actor!="switch")return why="door source must name a same-mod switch actor",false;
    }
    return true;
}
inline bool parse_scenes_impl(const mods::json::Value& m,std::vector<ManifestScene>& out,std::string& why) {
    using namespace world_manifest;out.clear();const auto& list=m.get("scenes");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>kMaxScenesPerMod)return why="scenes must be a list of up to 16",false;
    for(const auto& v:list.array){ManifestScene s;if(!keys(v,{"id","trigger","steps"}))return why="unknown scene field",false;if(!id(v.get("id")))return why="invalid scene id",false;s.id=v.get("id").text;
        for(const auto& old:out)if(old.id==s.id)return why="duplicate scene id",false;
        const auto& trigger=v.get("trigger");if(trigger.type!=Value::Null){s.trigger=trigger.get("type").string();
            bool trigger_keys=false;
            if(s.trigger=="room")trigger_keys=keys(trigger,{"type","stage","room"});
            else if(s.trigger=="area")trigger_keys=keys(trigger,{"type","stage","room","position","radius"});
            else if(s.trigger=="talk")trigger_keys=keys(trigger,{"type","actor"});
            else if(s.trigger=="quest_step")trigger_keys=keys(trigger,{"type","quest","step"});
            else if(s.trigger=="salvage")trigger_keys=keys(trigger,{"type","chart"});
            if(!trigger_keys)return why="unknown scene trigger or field",false;
            if(s.trigger=="room"||s.trigger=="area"){if(!place(trigger,s.stage,s.room))return why="invalid scene room trigger",false;if(s.trigger=="area"){if(!point(trigger.get("position"),s.position)||trigger.get("radius").type!=Value::Number||!std::isfinite(trigger.get("radius").number)||trigger.get("radius").number<=0||trigger.get("radius").number>10000)return why="invalid scene area trigger",false;s.radius=float(trigger.get("radius").number);}}
            else if(s.trigger=="talk"){s.actor=trigger.get("actor").string();if(!declared(m,"actors",s.actor))return why="unknown trigger actor",false;}
            else if(s.trigger=="quest_step"){s.quest=trigger.get("quest").string();if(!integer(trigger.get("step"),1,254)||!quest_step(m,s.quest,unsigned(trigger.get("step").number)))return why="unknown trigger quest step",false;s.quest_step=unsigned(trigger.get("step").number);}
            else if(s.trigger=="salvage"){s.chart=trigger.get("chart").string();if(!declared(m,"charts",s.chart))return why="unknown trigger chart",false;}
            else return why="unknown scene trigger",false;
        }
        const auto& steps=v.get("steps");if(steps.type!=Value::Array||steps.array.empty()||steps.array.size()>kMaxSceneSteps)return why="scene needs 1..64 steps",false;
        bool explicit_graph=false;for(const auto& st:steps.array){SceneStep p;const auto type=st.get("type").string();bool timed=false;
            if(st.object.contains("id")){if(!id(st.get("id")))return why="invalid scene step id",false;p.id=st.get("id").text;for(const auto&old:s.steps)if(old.id==p.id)return why="duplicate scene step id",false;}
            if(st.object.contains("next")){if(!id(st.get("next")))return why="invalid scene successor id",false;p.next=st.get("next").text;explicit_graph=true;}
            auto fields=st;fields.object.erase("id");fields.object.erase("next");
            bool step_keys=false;
            if(type=="camera")step_keys=keys(fields,{"type","eye","target","target_actor","pan","eye_to","target_to","frames","seconds"});
            else if(type=="say")step_keys=keys(fields,{"type","actor","text","frames","seconds"});
            else if(type=="move")step_keys=keys(fields,{"type","actor","position","frames","seconds"});
            else if(type=="face")step_keys=keys(fields,{"type","actor","position","target_actor"});
            else if(type=="wait")step_keys=keys(fields,{"type","frames","seconds"});
            else if(type=="sound")step_keys=keys(fields,{"type","name"});
            else if(type=="fade")step_keys=keys(fields,{"type","direction","frames","seconds"});
            else if(type=="end")step_keys=keys(fields,{"type","then"});
            else if(type=="camera_path")step_keys=keys(fields,{"type","points","easing"});
            else if(type=="camera_follow")step_keys=keys(fields,{"type","actor","eye_offset","target_offset","half_life","frames","seconds"});
            else if(type=="animation")step_keys=keys(fields,{"type","actor","clip","blend_seconds","loop"});
            else if(type=="walk")step_keys=keys(fields,{"type","actor","points","quest_place","speed","frames","seconds"});
            else if(type=="choice")step_keys=keys(fields,{"type","text","options","default","frames","seconds"});
            else if(type=="music")step_keys=keys(fields,{"type","cue","action","fade_frames"});
            else if(type=="particle")step_keys=keys(fields,{"type","effect","actor","position","scale","alpha"});
            if(!step_keys)return why="unknown scene step or field",false;
            if(type=="camera"){p.kind=SceneStep::Kind::Camera;timed=true;if(!point(st.get("eye"),p.eye))return why="camera needs an eye",false;if(st.get("target_actor").type!=Value::Null){p.target_actor=st.get("target_actor").string();if(!character(m,p.target_actor))return why="unknown camera character",false;}else if(!point(st.get("target"),p.target))return why="camera needs a target",false;if(st.get("pan").type!=Value::Null){if(st.get("pan").type!=Value::Bool)return why="pan must be boolean",false;p.pan=st.get("pan").boolean;if(p.pan&&(!point(st.get("eye_to"),p.eye_to)||!point(st.get("target_to"),p.target_to)))return why="pan needs eye_to and target_to",false;}}
            else if(type=="say"){p.kind=SceneStep::Kind::Say;timed=true;p.actor=st.get("actor").string();if(!character(m,p.actor)||!lines(st.get("text"),p.text))return why="say needs a known character and text",false;}
            else if(type=="move"||type=="face"){p.kind=type=="move"?SceneStep::Kind::Move:SceneStep::Kind::Face;timed=type=="move";p.actor=st.get("actor").string();if(!character(m,p.actor))return why="unknown scene character",false;if(!resident_character(m,p.actor)&&!mod_character(m,p.actor))return why="move/face requires a placed resident or mod actor",false;if(type=="face"&&st.get("target_actor").type!=Value::Null){p.target_actor=st.get("target_actor").string();if(!character(m,p.target_actor))return why="unknown face target",false;}else if(!point(st.get("position"),p.position))return why="move/face needs a position",false;}
            else if(type=="wait"){p.kind=SceneStep::Kind::Wait;timed=true;}
            else if(type=="sound"){p.kind=SceneStep::Kind::Sound;p.sound=st.get("name").string();if(p.sound!="SE_MENU_CANCEL"&&p.sound!="SE_MENU_DECIDE"&&p.sound!="SE_MENU_CURSOR"&&p.sound!="SE_MENU_MINI_WINDOW_OPEN"&&p.sound!="SE_MENU_MINI_WINDOW_CLOSE")return why="unknown scene sound",false;}
            else if(type=="fade"){p.kind=SceneStep::Kind::Fade;timed=true;const auto dir=st.get("direction").string();if(dir!="in"&&dir!="out")return why="fade direction must be in or out",false;p.fade_in=dir=="in";}
            else if(type=="end"){p.kind=SceneStep::Kind::End;if(!actions(m,st.get("then"),p.then,why))return false;if(!p.next.empty())return why="end cannot have a successor",false;}
            else if(type=="camera_path"){
                p.kind=SceneStep::Kind::CameraPath;const auto&keys_=st.get("points");if(keys_.type!=Value::Array||keys_.array.size()<2||keys_.array.size()>16)return why="camera_path needs 2..16 points",false;
                if(st.object.contains("easing")){p.easing=st.get("easing").string();if(p.easing!="linear"&&p.easing!="smoothstep")return why="unsupported camera easing",false;}
                for(size_t n=0;n<keys_.array.size();++n){const auto&key=keys_.array[n];SceneCameraKey camera;if(!keys(key,{"eye","target","frames"})||!scene_point(key.get("eye"),camera.eye)||!scene_point(key.get("target"),camera.target)||!camera_separated(camera.eye,camera.target))return why="invalid camera path key",false;
                    if(n+1==keys_.array.size()){if(key.object.contains("frames"))return why="final camera key has no outgoing duration",false;}
                    else{if(!integer(key.get("frames"),1,kMaxSceneFrames))return why="camera key needs bounded outgoing frames",false;camera.frames=unsigned(key.get("frames").number);if(camera.frames>kMaxSceneFrames-p.frames)return why="camera path exceeds five minute limit",false;p.frames+=camera.frames;}
                    if(!p.camera_keys.empty()&&!camera_segment(p.camera_keys.back(),camera))return why="camera path crosses a degenerate view",false;p.camera_keys.push_back(camera);
                }
            }
            else if(type=="camera_follow"){
                p.kind=SceneStep::Kind::CameraFollow;timed=true;p.actor=st.get("actor").string();if(!character(m,p.actor)||!scene_point(st.get("eye_offset"),p.eye_offset)||!scene_point(st.get("target_offset"),p.target_offset)||!camera_separated(p.eye_offset,p.target_offset))return why="invalid camera follow character or offsets",false;if(st.object.contains("half_life")&&!number(st.get("half_life"),0,10,p.half_life))return why="invalid camera follow half_life",false;
            }
            else if(type=="animation"){
                p.kind=SceneStep::Kind::Animation;p.actor=st.get("actor").string();if(!mod_character(m,p.actor,true)||!id(st.get("clip")))return why="animation needs an owned skinned actor and clip",false;p.clip=st.get("clip").text;
                if(st.object.contains("blend_seconds")&&!number(st.get("blend_seconds"),0,10,p.blend_seconds))return why="invalid animation blend_seconds",false;
                if(st.object.contains("loop")){if(st.get("loop").type!=Value::Bool)return why="animation loop must be boolean",false;p.loop=st.get("loop").boolean;}
            }
            else if(type=="walk"){
                p.kind=SceneStep::Kind::Walk;timed=true;p.actor=st.get("actor").string();if(!mod_character(m,p.actor,true)||!number(st.get("speed"),0,1500,p.speed,true))return why="walk needs an owned skinned actor and bounded speed",false;
                if(st.object.contains("points")==st.object.contains("quest_place"))return why="walk needs points or quest_place, exclusively",false;
                if(st.object.contains("quest_place")){if(!id(st.get("quest_place"))||!declared(m,"quests",st.get("quest_place").text))return why="walk quest_place must be an owned declared quest",false;p.quest_place=st.get("quest_place").text;}
                else{const auto&points=st.get("points");if(points.type!=Value::Array||points.array.size()<2||points.array.size()>16)return why="walk needs 2..16 points",false;for(const auto&value:points.array){WorldPoint point_;if(!keys(value,{"x","y","z"})||!point(value,point_))return why="invalid walk point",false;if(!p.walk_points.empty()&&std::hypot(double(point_.x)-p.walk_points.back().x,double(point_.z)-p.walk_points.back().z)<=1)return why="walk points need horizontal separation",false;p.walk_points.push_back(point_);}}
                p.loop=false;
            }
            else if(type=="choice"){
                p.kind=SceneStep::Kind::Choice;timed=true;explicit_graph=true;if(!p.next.empty()||!lines(st.get("text"),p.text))return why="choice needs bounded text and option successors",false;const auto&options=st.get("options");if(options.type!=Value::Array||options.array.size()!=2)return why="choice needs exactly two options",false;
                for(const auto&option:options.array){std::u16string label;if(!keys(option,{"text","next"})||option.get("text").type!=Value::String||option.get("text").text.empty()||!story_mail::append_utf16(option.get("text").text,false,label)||label.size()>32||!id(option.get("next")))return why="invalid choice label or successor",false;p.choices.push_back({option.get("text").text,option.get("next").text,0});}
                if(st.object.contains("default")){if(!integer(st.get("default"),0,1))return why="choice default must be 0 or 1",false;p.default_choice=unsigned(st.get("default").number);}
            }
            else if(type=="music"){
                p.kind=SceneStep::Kind::Music;if(!id(st.get("cue")))return why="music cue must be a textual id",false;p.cue=st.get("cue").text;p.music_action=st.get("action").string();if(p.music_action!="start"&&p.music_action!="stop")return why="music action must be start or stop",false;
                if(st.object.contains("fade_frames")){if(!integer(st.get("fade_frames"),0,600))return why="music fade_frames must be 0..600",false;p.fade_frames=unsigned(st.get("fade_frames").number);}
            }
            else if(type=="particle"){
                p.kind=SceneStep::Kind::Particle;
                if(st.get("effect").type!=Value::String||(st.get("effect").text!="auction-sparkle"&&st.get("effect").text!="beacon-sparkle"))return why="unknown curated particle effect",false;
                p.particle=st.get("effect").text;
                if(st.object.contains("actor")==st.object.contains("position"))return why="particle needs one actor or world position",false;
                if(st.object.contains("actor")){p.actor=st.get("actor").string();if(!character(m,p.actor))return why="particle actor must be a declared scene character",false;}
                else if(!scene_point(st.get("position"),p.position))return why="particle needs a world XYZ position",false;
                else p.particle_has_position=true;
                if(st.object.contains("scale")&&!number(st.get("scale"),0.1,4,p.particle_scale))return why="particle scale must be 0.1..4",false;
                if(st.object.contains("alpha")){if(!integer(st.get("alpha"),0,255))return why="particle alpha must be 0..255",false;p.particle_alpha=unsigned(st.get("alpha").number);}
            }
            else return why="unknown scene step",false;
            if(timed&&!scene_duration(st,p.frames,why))return false;
            if(p.kind!=SceneStep::Kind::End&&p.frames==0)p.frames=1;
            s.steps.push_back(std::move(p));
        }
        if(s.steps.back().kind!=SceneStep::Kind::End)return why="scene must finish with end",false;
        std::vector<story_scenes::Timing> graph;for(size_t n=0;n<s.steps.size();++n){auto&step=s.steps[n];auto resolve_next=[&](const std::string&name,size_t&index){for(size_t j=n+1;j<s.steps.size();++j)if(s.steps[j].id==name){index=j;return true;}return false;};
            if(!explicit_graph&&step.kind==SceneStep::Kind::End&&n+1!=s.steps.size())return why="end must be the final step",false;
            if(!step.next.empty()){size_t index;if(!resolve_next(step.next,index))return why="scene next must name a forward step",false;step.successors.push_back(index);}
            for(auto&choice:step.choices){if(!resolve_next(choice.next,choice.next_step))return why="choice must name a forward step",false;step.successors.push_back(choice.next_step);}
            graph.push_back({step.frames,step.kind==SceneStep::Kind::End,step.successors});
        }
        if(!story_scenes::valid_graph(graph))return why="scene graph must be reachable, forward, bounded and end on every path",false;
        out.push_back(std::move(s));
    }return true;
}
inline bool parse_actors(const mods::json::Value& m,std::vector<ManifestActor>& out,std::string& why) {
    out.clear();std::vector<ManifestActor> parsed;if(!parse_actors_impl(m,parsed,why))return false;out=std::move(parsed);return true;
}
inline bool parse_scenes(const mods::json::Value& m,std::vector<ManifestScene>& out,std::string& why) {
    out.clear();std::vector<ManifestScene> parsed;if(!parse_scenes_impl(m,parsed,why))return false;out=std::move(parsed);return true;
}
namespace world_manifest {
// Validate the selected package graph before any actor or scene registry is mutated.
inline bool validate_world_packages(const std::vector<std::pair<std::string,mods::json::Value>>& packages,std::string& why) {
    unsigned total=0;std::map<std::pair<std::string,unsigned>,unsigned> rooms;
    std::map<uint16_t,std::pair<std::string,std::string>> flags;
    for(const auto& [mod,manifest]:packages){
        std::vector<ManifestActor> actors;std::vector<ManifestScene> scenes;
        if(!parse_actors(manifest,actors,why)||!parse_scenes(manifest,scenes,why)){why=mod+": "+why;return false;}
        total+=unsigned(actors.size());if(total>128)return why="enabled packages exceed 128 actors overall",false;
        std::vector<std::string> names;
        for(const auto& actor:actors){
            if(++rooms[{actor.stage,actor.room}]>kMaxActorsPerRoom)return why="enabled packages exceed 16 actors in "+actor.stage+" room "+std::to_string(actor.room),false;
            if(!actor.flag.empty())names.push_back(actor.flag);
            for(const auto& action:actor.then)if(action.kind==StoryAction::Kind::Flag)names.push_back(action.id);
        }
        for(const auto& scene:scenes)for(const auto& step:scene.steps)for(const auto& action:step.then)
            if(action.kind==StoryAction::Kind::Flag)names.push_back(action.id);
        for(const auto& name:names){
            const auto identity=std::make_pair(mod,name);const uint16_t key=story_mail::letter_key(mod,"actor-flag:"+name);
            auto [found,inserted]=flags.emplace(key,identity);
            if(!inserted&&found->second!=identity)return why="mod-owned flag hash collision: "+mod+":"+name+" conflicts with "+found->second.first+":"+found->second.second,false;
        }
    }return true;
}
} // namespace world_manifest
} // namespace story_notebook
