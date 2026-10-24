// SDK v3 preview: bounded declarative world content. No game save bits are accepted here.
#pragma once
#include "story_world_calm.h"
#include "story_world_collision_mesh.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <string>
#include <vector>
#include "mod_json.h"
#include "story_mail_book.h"
#include "story_scenes_core.h"
#include "story_mod_combat_manifest.h"
#include "story_native_scene_manifest.h"
#include "story_resident_types.h"
#include "story_enemy_variants.h"
#include "story_room_path.h"
#include "story_enemies_summons.h"
#include "story_game_objects_catalogue.h"
#include "story_game_pieces_catalogue.h"
#include "demo_scene.h"
#include "demo_start_gate.h"
namespace story_notebook {
struct WorldPoint { float x=0,y=0,z=0; bool operator==(const WorldPoint&)const=default; };
struct StoryAction {
    // GiveChart: {"action": "give", "id": "chart:<id>"}, one of the mod's charts (once; never taken)
    // EnterStage: {"action": "stage", "id": "<stage id>", "room"?, "point"?}, one of the package's own stages (mod_stages); value = room << 8 | point
    enum class Kind { Give,Take,QuestStep,Flag,StartScene,QuestDone,GiveChart,EnterStage };
    Kind kind=Kind::Flag; std::string id; unsigned value=1;
    bool operator==(const StoryAction&)const=default;
};
// A model declaration. kind "kit": a rigid model drawn with the materials of a room of the game (`kit`: its stage
// and room; `material`: the default kit material, `materials`: model material -> kit material), as a mod stage's
// ground is; built on the player's machine (tools/story_mods/kit_model.py, story_kit_models.h) and drawn like a
// game piece, with the background light of the room it stands in (docs/world-objects.md, "Kit models").
struct ManifestModel { std::string id,source,kind="static"; std::string kit_stage; int kit_room=-1; std::string kit_material; std::vector<std::pair<std::string,std::string>> kit_materials; };
struct ManifestLookAt {
    bool enabled=false;std::string head,upper_bone;
    float yaw_degrees=0,pitch_degrees=0,rate_degrees=0,radius=0,upper_yaw_degrees=0,upper_fraction=0;
};
// A route ("path"): a mod's skinned actor walks `points` (2..16); a placed resident starts at its
// position and walks 1..15 further `points` with its own walk. Modes: once, loop (back to the start),
// back_and_forth (reverse at the ends). `pauses`: short strolling pauses on the way (the game's own
// walkers' rhythm). A resident's speed is optional (speed_given false: the character's own pace);
// a mod actor may name its walk and idle clips (empty: "walk" and "idle").
struct ManifestPath {std::vector<WorldPoint> points;float speed=100;bool loop=false;bool back_and_forth=false,pauses=false,speed_given=true;std::string walk_clip,idle_clip;
    const char* mode()const{return loop?"loop":back_and_forth?"back_and_forth":"once";}
    bool operator==(const ManifestPath&)const=default;};
struct ManifestPropDrop {std::string item;unsigned quantity=1;};
struct QuestCondition {std::string quest;unsigned step=0;bool operator==(const QuestCondition&)const=default;};
struct ManifestTalk {
    std::string quest,item,time,when_flag,when_not_flag,lines;unsigned quest_step=0;
    std::vector<QuestCondition> quests;std::vector<StoryAction> then;
};
// material (shoreline only): "" (the coast foam, the default; "foam" in a manifest), "water" (a still-water surface: a
// spring pool), "pond" (an island's calm shore water with its coast foam), "river" (flowing water, scrolling along the
// model's V), "waterfall" (falling water) or "lava" (Fire Mountain's moving lava)
struct ManifestVisualModel {std::string model,material;};
// a body of water's (or lava's) surface material: its object is a body (it lies on or in the island's ground)
inline bool water_surface_material(const std::string& m){return m=="water"||m=="pond"||m=="river"||m=="waterfall"||m=="lava";}
struct ManifestChartMarker {bool enabled=false;std::string label,when_flag;};
struct ManifestArrival {bool enabled=false;std::string name,flag;float radius=0;};
struct ManifestGrapplePoint {std::string id;WorldPoint position;float yaw=0;};
struct ManifestBuoyancy {float draft=0,damping=.25f,max_tilt=10;};
struct ManifestDrift {float wind_speed=0,max_speed=30;WorldPoint current;};
struct ManifestAnchor {bool enabled=false;WorldPoint point;float radius=0;};
struct ManifestActor {
    std::string id,actor,stage,kind,model,animation,prompt="check",texture,texture_digest; unsigned room=0,texture_token=0;
    float collision_radius=30,collision_height=100; bool pushable=false;
    std::string behaviour,collision_mesh,collision_shape,collision_surface="stone";WorldPoint collision_size{};
    ManifestBuoyancy buoyancy;ManifestDrift drift;ManifestAnchor anchor;
    std::vector<ManifestGrapplePoint> grapple_points;
    ManifestVisualModel distant,shoreline;ManifestChartMarker chart_marker;ManifestArrival arrival;
    story_world_calm::Settings calm_water;  // still water around a sea island (story_world_calm.h)
    bool world_object()const{return !behaviour.empty();}
    bool water_object()const{return behaviour=="floating"||behaviour=="drifting";}
    // a body of water (the editor's water tool, docs/world-objects.md "Water"): a static object whose shoreline is a
    // water, river, waterfall or lava surface; it may reach into the island's ground and walls (its floor and banks meet them)
    bool water_body()const{return behaviour=="static"&&water_surface_material(shoreline.material);}
    ManifestLookAt look_at;ManifestPath path;
    bool carry_enabled=false,carry_break_on_impact=false;
    bool combat_enabled=false,native_heart_drop=false;story_mod_combat::Config combat;
    WorldPoint position,rotation,scale{1,1,1}; uint32_t params=0; std::string variant; std::string hole; bool rat_bomb=false; /* a rat of a rat hole (runtime only) */ unsigned resident=9; // 19 is the seated Ym1 adapter, not a People subtype.
    uint8_t item_game=6;  // a chest's game item number ("parameters": {"item"}); kChestModItem with item_mod
    std::string item_mod; // a chest's "item:<id>": one of the mod's items (the id)
    std::string room_path;      // the id of one of the mod's room paths ("paths") that this actor uses ("path": "<id>")
    std::string rider_of;       // a Kargaroc's rider (runtime only): the Kargaroc's id
    std::string summon_of;      // a Wizzrobe summoner's pool member (runtime only): the summoner's id
    std::string defeat_switch;  // an enemy's: the same-mod switch actor whose flag is set when every enemy naming it is defeated
    std::string broken_flag,switch_flag,switch_actor,defeated_flag;std::vector<ManifestPropDrop> drops;
    std::string flag,quest,item,time,lines; unsigned quest_step=0;
    std::vector<StoryAction> then;
    std::string when_flag,when_not_flag;std::vector<QuestCondition> quests;std::vector<ManifestTalk> talks;
    // `variant` above is also a game object's catalogue variant ("actor": "game:<id>", story_game_objects_catalogue.h)
    bool game_object()const{return story_game_objects::is_reference(actor);}
    // game pieces ("actor": "piece:<id>", story_game_pieces_catalogue.h): the piece's neutral id; the placement
    // is a static world object whose model and collision are the piece's (model and collision_mesh "piece:<id>")
    std::string piece;
    // a composition (`"group"`): static world objects of one mod made together (an island's ground and the ruins on it)
    std::string group;
    bool game_piece()const{return !piece.empty();}
    // a static world object whose model is a kit model (ManifestModel kind "kit"): drawn like a game piece
    bool kit_model=false;
    bool background_drawn()const{return game_piece()||kit_model;}
};
// Parts of one composition: two static world objects of one mod that name the same "group" (an island's ground and the
// ruins standing on it, each with materials of its own kit room). Placement never compares their collisions with each
// other; everything else (the room, other objects, other mods) counts for each as for any world object. The caller
// checks that both are the same mod's.
inline bool one_composition(const ManifestActor& a,const ManifestActor& b){
    return !a.group.empty()&&a.group==b.group&&a.world_object()&&b.world_object()&&a.behaviour=="static"&&b.behaviour=="static"&&a.stage==b.stage&&a.room==b.room;
}
template<class Condition,class Quest,class Item,class Flag>
inline bool condition_matches(const Condition&c,Quest quest,Item item,Flag flag,bool day){
    if(!c.quest.empty()&&quest(c.quest)!=int32_t(c.quest_step))return false;
    for(const auto&q:c.quests)if(quest(q.quest)!=int32_t(q.step))return false;
    if(!c.item.empty()&&item(c.item.substr(5))<=0)return false;
    if(!c.when_flag.empty()&&!flag(c.when_flag))return false;
    if(!c.when_not_flag.empty()&&flag(c.when_not_flag))return false;
    return c.time.empty()||day==(c.time=="day");
}
struct SceneCameraKey {WorldPoint eye,target;unsigned frames=0;bool operator==(const SceneCameraKey&)const=default;};
struct SceneChoiceOption {std::string text,next;size_t next_step=0;bool operator==(const SceneChoiceOption&)const=default;};
struct SceneStep {
    enum class Kind { Camera,Say,Move,Face,Wait,Sound,Fade,End,CameraPath,CameraFollow,Animation,Walk,Choice,Music,Particle,Demo };
    Kind kind=Kind::Wait; std::string actor,target_actor,text,sound;
    std::string id,next,clip,quest_place,cue,music_action,particle,easing="linear";
    std::vector<size_t> successors;std::vector<SceneCameraKey> camera_keys;
    WorldPoint eye_offset,target_offset;float half_life=0.2f,blend_seconds=0,speed=100;bool loop=true;
    std::vector<WorldPoint> walk_points;std::vector<SceneChoiceOption> choices;unsigned default_choice=0,fade_frames=0;
    WorldPoint eye,target,position,eye_to,target_to; unsigned frames=0,particle_alpha=255; float particle_scale=1; bool pan=false,fade_in=false,particle_has_position=false;
    std::vector<StoryAction> then;
    std::optional<demo_scene::Demo> demo;  // Kind::Demo: tracks played by the game's demo system (demo_scene.h)
    bool operator==(const SceneStep&)const=default;
};
struct ManifestScene {
    std::string id; std::vector<SceneStep> steps;
    std::optional<story_native_scene_manifest::Definition> native;
    std::string trigger,stage,actor,quest,chart; unsigned room=0,quest_step=0;
    WorldPoint position; float radius=0;
    // an area trigger with "again": the scene can play again once Link has left the area (in the same room visit)
    bool again=false;
    bool operator==(const ManifestScene&)const=default;
};
// What a placed chest can hold ("parameters": {"item": ...}): the game items a treasure chart's chest can hold
// (story_manifest.h chart_game_item: rupees and heart pieces/containers, no story or key item, nothing that sets
// a save bit; the chest's own item bit is 255, none) and the mod's items. A mod item travels as the port's
// chest marker (story_treasure_chest.h kChestMarker, a pool number no game data names) and is swapped for the
// item-get stand-in when the chest opens.
inline constexpr uint8_t kChestModItem=0x1B;
inline uint8_t chest_game_item(const std::string& name){
    static const std::pair<const char*,uint8_t> kItems[]={{"rupee_green",0x01},{"rupee_blue",0x02},{"rupee_yellow",0x03},{"rupee_red",0x04},
        {"rupee_purple",0x05},{"rupee_orange",0x06},{"rupee_silver",0x0F},{"heart_piece",0x07},{"heart_container",0x08}};
    for(const auto&[n,v]:kItems)if(name==n)return v;
    return 0;
}
// actors one declaration places: a rat hole is the hole and its three rats
inline unsigned placed_actors(const ManifestActor& a){return a.actor=="rat_hole"&&a.kind.empty()?1+story_enemy_variants::kRatsPerHole:a.kind.empty()&&!story_enemy_variants::rider_kind(a.actor,a.variant).empty()?2:1;}
inline constexpr unsigned kMaxActorsPerRoom=16,kMaxActorsPerMod=64,kMaxScenesPerMod=16,kMaxSceneSteps=64,kMaxSceneFrames=18000;
// rat holes in one room: each places 1 + 3 actors (the hole and its rats)
inline constexpr unsigned kMaxRatHolesPerRoom=2;
// a rat's marker and flag: its hole's, with ".rat1".."rat3" added (ids are at most 32 bytes)
inline constexpr size_t kRatHoleIdBytes=story_mail::kMaxIdBytes-5;
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
// A readable flag must have a same-manifest producer. References cannot declare flags.
inline bool known_flag(const Value& m,const std::string& name) {
    const auto produces=[&](const Value&v){return flag_id(v)&&v.text==name;};
    auto writes=[&](const Value& list){
        if(list.type!=Value::Array||list.array.size()>8)return false;
        for(const auto&a:list.array)if(keys(a,{"action","id","value"})&&a.get("action").string()=="flag"&&
            produces(a.get("id"))&&(!a.object.contains("value")||a.get("value").type==Value::Bool))return true;
        return false;
    };
    for(const auto&a:m.get("actors").array){
        const auto type=a.get("actor").string();
        const bool sign=type=="sign"||type=="kanban"||type=="PROC_KANBAN";
        const bool chest=type=="chest"||type=="Tbox"||type=="takara"||type=="PROC_TBOX";
        const bool mod=a.get("kind").string()=="mod"&&!a.object.contains("actor");
        if((sign||chest||(mod&&!a.object.contains("combat")))&&produces(a.get("flag")))return true;
        if(mod&&a.get("behaviour").string()=="static"&&a.get("stage").string()=="sea"&&produces(a.get("arrival").get("flag")))return true;
        if(type=="switch"&&produces(a.get("switch_flag")))return true;
        if((type=="pot"||type=="barrel"||type=="crate"||(mod&&a.get("carry").get("break_on_impact").type==Value::Bool&&a.get("carry").get("break_on_impact").boolean))&&produces(a.get("broken_flag")))return true;
        if((type=="chuchu"||type=="bokoblin"||type=="moblin"||type=="keese"||type=="miniblin"||type=="peahat"||type=="darknut"||type=="rat_hole"||type=="bokobaba"||type=="bubble"||type=="octorok"||story_enemy_variants::more_enemy(type)||
            (mod&&a.get("combat").type==Value::Object))&&produces(a.get("defeated_flag")))return true;
        if(writes(a.get("then")))return true;
        for(const auto&t:a.get("talks").array)if(writes(t.get("then")))return true;
    }
    for(const auto&scene:m.get("scenes").array)for(const auto&step:scene.get("steps").array)
        if(step.get("type").string()=="end"&&writes(step.get("then")))return true;
    return false;
}
inline bool read_flag(const Value&m,const Value&v,std::string&out,std::string&why){
    if(v.type==Value::Null)return true;
    if(!flag_id(v)||!known_flag(m,v.text))return why="flag condition requires a same-manifest flag producer",false;
    out=v.text;return true;
}
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
        if(kind=="give"&&s.id.rfind("chart:",0)==0) {s.kind=StoryAction::Kind::GiveChart;
            if(!keys(a,{"action","id"}))return why="a chart is given once: give chart:<id> takes no count",false;
            if(!declared(m,"charts",s.id.substr(6)))return why="action names an undeclared mod chart (chart:<id> of \"charts\")",false;
        } else if(kind=="take"&&s.id.rfind("chart:",0)==0) {return why="a chart can't be taken: take names an item (item:<id>)",false;
        } else if(kind=="give"||kind=="take") {s.kind=kind=="give"?StoryAction::Kind::Give:StoryAction::Kind::Take;
            if(!keys(a,{"action","id","count"}))return why="unknown give/take action field",false;
            if(a.object.contains("count")){if(!integer(a.get("count"),1,254))return why="action count must be 1..254",false;s.value=unsigned(a.get("count").number);}
            if(s.id.rfind("item:",0)!=0||!declared(m,"items",s.id.substr(5))) return why="action names an undeclared mod item (give and take name item:<id>; give also chart:<id>)",false;
        } else if(kind=="quest_step") {s.kind=StoryAction::Kind::QuestStep;if(!keys(a,{"action","id","step"}))return why="unknown quest action field",false;
            if(!integer(a.get("step"),1,254)||!quest_step(m,s.id,unsigned(a.get("step").number))) return why="action names an undeclared quest step",false;
            s.value=unsigned(a.get("step").number);
        } else if(kind=="quest_done") {s.kind=StoryAction::Kind::QuestDone;
            if(!keys(a,{"action","id"})||!id(a.get("id"))||!declared(m,"quests",s.id))return why="quest_done names an undeclared same-mod quest",false;
        } else if(kind=="flag") {s.kind=StoryAction::Kind::Flag;if(!keys(a,{"action","id","value"}))return why="unknown flag action field",false;
            if(a.object.contains("value")){if(a.get("value").type!=Value::Bool)return why="flag value must be boolean",false;s.value=a.get("value").boolean?1:0;}if(!flag_id(a.get("id"))) return why="flag must be a mod-owned id",false;
        } else if(kind=="stage") {s.kind=StoryAction::Kind::EnterStage;
            if(!keys(a,{"action","id","room","point"})||!declared(m,"stages",s.id))return why="stage names one of the package's own stages (\"stages\")",false;
            unsigned room=0,point=0;
            if(a.object.contains("room")){if(!integer(a.get("room"),0,63))return why="stage room must be 0..63",false;room=unsigned(a.get("room").number);}
            if(a.object.contains("point")){if(!integer(a.get("point"),0,99))return why="stage point must be 0..99",false;point=unsigned(a.get("point").number);}
            s.value=room<<8|point;
        } else if(kind=="scene") {s.kind=StoryAction::Kind::StartScene;if(!keys(a,{"action","id"}))return why="unknown scene action field",false;if(!declared(m,"scenes",s.id)) return why="action names an undeclared scene",false;
        } else return why="unknown then action",false;
        out.push_back(s);
    } return true;
}
template<class Condition> inline bool actor_condition(const Value&m,const Value&when,Condition&out,std::string&why){
    if(when.type!=Value::Null&&!keys(when,{"quest","step","item","time","flag","not_flag","quests"}))return why="actor when must be an object",false;
        if(when.get("quest").type!=Value::Null){out.quest=when.get("quest").string();if(!integer(when.get("step"),1,254)||!quest_step(m,out.quest,unsigned(when.get("step").number)))return why="actor condition names an undeclared quest step",false;out.quest_step=unsigned(when.get("step").number);}
        if(!read_flag(m,when.get("flag"),out.when_flag,why)||!read_flag(m,when.get("not_flag"),out.when_not_flag,why))return false;
        if(when.get("step").type!=Value::Null&&when.get("quest").type==Value::Null)return why="actor step requires quest",false;
        const auto& multiple=when.get("quests");
        if(multiple.type!=Value::Null){
            if(multiple.type!=Value::Array||multiple.array.empty()||multiple.array.size()>8)return why="quests condition needs 1..8 quest steps",false;
            for(const auto&q:multiple.array){if(!keys(q,{"quest","step"})||!integer(q.get("step"),1,254)||!quest_step(m,q.get("quest").string(),unsigned(q.get("step").number)))return why="quests condition names an undeclared quest step",false;
                out.quests.push_back({q.get("quest").text,unsigned(q.get("step").number)});}
        }
        if(when.get("item").type!=Value::Null){out.item=when.get("item").string();if(out.item.rfind("item:",0)!=0||!declared(m,"items",out.item.substr(5)))return why="actor condition names an undeclared mod item",false;}
        if(when.get("time").type!=Value::Null){out.time=when.get("time").string();if(out.time!="day"&&out.time!="night")return why="actor time must be day or night",false;}
    return true;
}
inline bool place(const Value& v,std::string& stage,unsigned& room) {
    if(v.get("stage").type!=Value::String||v.get("stage").text.empty()||v.get("stage").text.size()>8||!integer(v.get("room"),0,63)) return false;
    stage=v.get("stage").text;
    for(char c:stage) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
    room=unsigned(v.get("room").number);return true;
}
// A game object ("actor": "game:<id>", story_game_objects_catalogue.h): a catalogued id, its variant,
// stage, room, position, a turn and a uniform scale inside the entry's limits, and nothing else (no
// params, flags, then, paths or dialogue). `need`: the manifest's requires.game_objects (0: none).
inline bool game_object_fields(const Value& v,unsigned need,ManifestActor& a,std::string& why,const story_game_objects::Catalogue& c=story_game_objects::catalogue()) {
    for(const auto&[key,value]:v.object){bool allowed=false;for(const char*name:{"id","actor","variant","stage","room","position","rotation","scale"})allowed|=key==name;
        if(!allowed)return why="actor "+a.id+": a game object takes only id, actor, variant, stage, room, position, rotation and scale (not \""+key+"\": no params, flags, then, paths or dialogue)",false;}
    const std::string ref=story_game_objects::reference(a.actor);const auto* e=c.find(ref);
    if(!e)return why="actor "+a.id+": \""+a.actor+"\" is not in the game objects catalogue",false;
    if(!need)return why="actor "+a.id+": a manifest with game objects names the catalogue version it needs (\"requires\": {\"game_objects\": "+std::to_string(e->since)+"})",false;
    if(e->since>need)return why="actor "+a.id+": \""+a.actor+"\" needs game objects catalogue version "+std::to_string(e->since)+" (the manifest requires "+std::to_string(need)+")",false;
    if(v.object.contains("variant")){if(v.get("variant").type!=Value::String||!e->variant(v.get("variant").text)){std::string names;for(const auto&x:e->variants)names+=(names.empty()?"":", ")+x.id;return why="actor "+a.id+": \""+a.actor+"\" has the variants "+names,false;}a.variant=v.get("variant").text;}
    else a.variant=e->variants.front().id;
    if(!place(v,a.stage,a.room)||!point(v.get("position"),a.position))return why="actor "+a.id+": invalid stage, room or position",false;
    if(v.get("rotation").type!=Value::Null&&!point(v.get("rotation"),a.rotation,360))return why="invalid actor rotation",false;
    if(e->yaw_only&&(a.rotation.x!=0||a.rotation.z!=0))return why="actor "+a.id+": a game object turns about the vertical axis only (rotation x and z are 0)",false;
    if(v.get("scale").type!=Value::Null&&!point(v.get("scale"),a.scale,10))return why="invalid actor scale",false;
    if(a.scale.x!=a.scale.y||a.scale.x!=a.scale.z||a.scale.x<e->scale_min||a.scale.x>e->scale_max){
        char limits[64];if(e->scale_min==e->scale_max)std::snprintf(limits,sizeof limits,"scale %g only",double(e->scale_min));else std::snprintf(limits,sizeof limits,"a uniform scale from %g to %g",double(e->scale_min),double(e->scale_max));
        return why="actor "+a.id+": \""+a.actor+"\" takes "+limits,false;}
    return true;
}
// The game objects of a list, room by room, against the catalogue's room budget (instances, each archive once).
inline bool game_object_budget(const std::vector<ManifestActor>& actors,std::string& why,const story_game_objects::Catalogue& c) {
    std::map<std::pair<std::string,unsigned>,story_game_objects::RoomUse> rooms;std::map<std::pair<std::string,std::string>,unsigned> batches;
    for(const auto&a:actors){if(!a.game_object())continue;const auto* e=c.find(story_game_objects::reference(a.actor));if(!e)continue;
        if(e->batch()){const auto*v=e->variant(a.variant);auto&n=batches[{a.stage,e->packet.kind}];n+=v?v->units:0;
            if(n>e->packet.mod_share)return why=e->packet.kind+" in stage "+a.stage+": mods may plant "+std::to_string(e->packet.mod_share)+" units per stage (the game's own use shares the rest)",false;}
        auto&use=rooms[{a.stage,a.room}];use.add(*e);
        if(use.bytes>story_game_objects::kRoomBudgetBytes)return why="game objects in "+a.stage+" room "+std::to_string(a.room)+" exceed the room's memory budget ("+std::to_string(story_game_objects::kRoomBudgetBytes/1024)+" KiB)",false;}
    return true;
}
// (the port's catalogue, built only when the list has game objects)
inline bool game_object_budget(const std::vector<ManifestActor>& actors,std::string& why) {
    return std::none_of(actors.begin(),actors.end(),[](const ManifestActor&a){return a.game_object();})||game_object_budget(actors,why,story_game_objects::catalogue());
}
// A game piece ("actor": "piece:<id>", story_game_pieces_catalogue.h): a catalogued id, stage, room, position
// and a turn about the vertical axis, nothing else (no scale: the island's baked light and collision stay as
// they are; no params, flags, then, paths or dialogue). It becomes a static world object drawing the piece.
inline bool game_piece_fields(const Value& v,unsigned need,ManifestActor& a,std::string& why,const story_game_pieces::Catalogue& c=story_game_pieces::catalogue()) {
    for(const auto&[key,value]:v.object){bool allowed=false;for(const char*name:{"id","actor","stage","room","position","rotation"})allowed|=key==name;
        if(!allowed)return why="actor "+a.id+": a game piece takes only id, actor, stage, room, position and rotation (not \""+key+"\")",false;}
    const std::string ref=story_game_pieces::reference(a.actor);const auto* p=c.find(ref);
    if(!p)return why="actor "+a.id+": \""+a.actor+"\" is not in the game pieces catalogue",false;
    if(!need)return why="actor "+a.id+": a manifest with game pieces names the catalogue version it needs (\"requires\": {\"game_objects\": "+std::to_string(p->since)+"})",false;
    if(p->since>need)return why="actor "+a.id+": \""+a.actor+"\" needs game objects catalogue version "+std::to_string(p->since)+" (the manifest requires "+std::to_string(need)+")",false;
    if(!place(v,a.stage,a.room)||!point(v.get("position"),a.position))return why="actor "+a.id+": invalid stage, room or position",false;
    if(v.get("rotation").type!=Value::Null&&!point(v.get("rotation"),a.rotation,360))return why="invalid actor rotation",false;
    if(a.rotation.x!=0||a.rotation.z!=0)return why="actor "+a.id+": a game piece turns about the vertical axis only (rotation x and z are 0)",false;
    a.piece=p->id;a.kind="mod";a.model=a.collision_mesh=story_game_pieces::model_key(p->id);a.behaviour="static";a.scale={1,1,1};
    // an island ground on the sea brings its own distant model, seen from the neighbouring squares
    if(p->distant_triangles&&a.stage=="sea")a.distant.model=story_game_pieces::distant_key(p->id);
    return true;
}
// The game pieces of a list, room by room, against the room budget (each set's archive once, each placement).
inline bool game_piece_budget(const std::vector<ManifestActor>& actors,std::string& why,const story_game_pieces::Catalogue& c) {
    std::map<std::pair<std::string,unsigned>,story_game_pieces::RoomUse> rooms;
    for(const auto&a:actors){if(!a.game_piece())continue;const auto* p=c.find(a.piece);if(!p)continue;auto&use=rooms[{a.stage,a.room}];use.add(c,*p);
        if(use.bytes>story_game_pieces::kRoomBudgetBytes)return why="game pieces in "+a.stage+" room "+std::to_string(a.room)+" exceed the room's memory budget for buildings ("+std::to_string(story_game_pieces::kRoomBudgetBytes/(1024*1024))+" MiB: each set's archive once and each placement)",false;}
    return true;
}
inline bool game_piece_budget(const std::vector<ManifestActor>& actors,std::string& why) {
    return std::none_of(actors.begin(),actors.end(),[](const ManifestActor&a){return a.game_piece();})||game_piece_budget(actors,why,story_game_pieces::catalogue());
}
// Per-type fields after common placement validation. Native enemy names stay
// outside parse_actors' allowlist until their runtime adapters are integrated.
inline bool native_enemy_fields(const Value& m,const Value& v,ManifestActor& a,std::string& why) {
    if(a.actor!="chuchu"&&a.actor!="bokoblin"&&a.actor!="moblin"&&a.actor!="keese"&&a.actor!="miniblin"&&a.actor!="peahat"&&a.actor!="darknut"&&a.actor!="rat_hole"&&a.actor!="bokobaba"&&a.actor!="bubble"&&a.actor!="octorok"&&!story_enemy_variants::more_enemy(a.actor))return why="unsupported native enemy",false;
    for(const char* key:{"kind","params","flag","broken_flag","switch_flag","switch","lines","then","model","animation","look_at","path","collision","attention"})
        if(v.object.contains(key)&&!((a.actor=="bokoblin"||a.actor=="moblin"||story_enemy_variants::takes_room_path(a.actor))&&std::string(key)=="path"))return why="native enemies cannot override parameters, behavior, dialogue or models",false;
    // a declared variant only (story_enemy_variants.h), never raw parameters
    if(v.object.contains("parameters")){
        const auto& named=v.get("parameters");
        if(named.type!=Value::Object||!keys(named,{"variant"})||named.get("variant").type!=Value::String)return why="native enemy parameters take only a variant",false;
        const std::string& name=named.get("variant").text;
        if(!story_enemy_variants::has_variants(a.actor))return why="a "+a.actor+" has no variants",false;
        if(name.empty()||!story_enemy_variants::find(a.actor,name))return why="a "+a.actor+" variant is "+story_enemy_variants::names(a.actor),false;
        a.variant=name;
    }
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
        if(!keys(v,{"id","source","kind","kit","material","materials"})||!id(v.get("id"))||v.get("source").type!=Value::String)return why="invalid model declaration",false;
        const std::string kind=v.object.contains("kind")?v.get("kind").string():"static";
        if(kind!="static"&&kind!="skinned"&&kind!="kit")return why="model kind must be static, skinned or kit",false;
        ManifestModel model{v.get("id").text,{},kind};
        if(kind=="kit"){
            // a kit room, as a mod stage room's: a stage of the game (up to 7 letters, digits, _) and a room 0..63
            const auto& kit=v.get("kit");const std::string stage=kit.get("stage").string();
            if(!keys(kit,{"stage","room"})||stage.empty()||stage.size()>7||stage.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos||
               !integer(kit.get("room"),0,63))return why="model "+model.id+": a kit model's kit is a stage of the game and a room 0..63",false;
            model.kit_stage=stage;model.kit_room=int(kit.get("room").number);
            auto material_name=[](const Value& x){return x.type==Value::String&&!x.text.empty()&&x.text.size()<=64&&std::all_of(x.text.begin(),x.text.end(),[](unsigned char c){return c>32&&c<127&&c!='"'&&c!='\\';});};
            if(v.object.contains("material")){if(!material_name(v.get("material")))return why="model "+model.id+": invalid kit material",false;model.kit_material=v.get("material").text;}
            if(v.object.contains("materials")){const auto& mm=v.get("materials");if(mm.type!=Value::Object||mm.object.size()>16)return why="model "+model.id+": materials maps up to 16 model materials to kit materials",false;
                for(const auto&[name,kit_name]:mm.object){if(!material_name(Value(name))||!material_name(kit_name))return why="model "+model.id+": invalid material mapping",false;model.kit_materials.emplace_back(name,kit_name.text);}}
        }else if(v.object.contains("kit")||v.object.contains("material")||v.object.contains("materials"))return why="model "+model.id+": kit, material and materials are for kind kit",false;
        const auto& source=v.get("source").text;
        if(source.empty()||source.size()>240||source.front()=='/'||source.find('\\')!=std::string::npos||source.find(':')!=std::string::npos||source.size()<5||source.substr(source.size()-4)!=".glb")return why="model source must be a package-relative .glb",false;
        size_t start=0;while(start<=source.size()){const auto end=source.find('/',start);const auto part=source.substr(start,end==std::string::npos?end:end-start);if(part.empty()||part=="."||part=="..")return why="invalid model source path",false;if(end==std::string::npos)break;start=end+1;}
        for(unsigned char c:source)if(c<32||c==127)return why="invalid model source path",false;
        for(const auto& old:parsed)if(old.id==v.get("id").text)return why="duplicate model id",false;
        model.source=source;parsed.push_back(std::move(model));
    }out=std::move(parsed);return true;
}
// A model declaration as a package declares it (parse_models' fields), from an editor request that may carry
// preparation options too ("replace", "collision", "generation", "shoreline"; issue #281): what parse_models reads.
inline mods::json::Value model_declaration(const mods::json::Value& request){
    mods::json::Value out;out.type=mods::json::Value::Object;
    for(const char* key:{"id","source","kind","kit","material","materials"})if(request.object.contains(key))out[key]=request.get(key);
    return out;
}
// a "path" object (ManifestPath); `resident`: a placed resident's route (1..15 points after its
// position, optional speed, no clips), else a mod actor's (2..16 points, speed, optional clips)
inline bool parse_path(const mods::json::Value& path,const WorldPoint& start,bool resident,ManifestPath& out,std::string& why){
    using namespace world_manifest;out=ManifestPath{};
    const std::initializer_list<const char*> fields=resident?std::initializer_list<const char*>{"points","speed","mode","pauses"}:std::initializer_list<const char*>{"points","speed","mode","pauses","walk","idle"};
    const size_t lo=resident?1:2,hi=resident?15:16;
    if(path.type!=Value::Object||!keys(path,fields)||path.get("points").type!=Value::Array||path.get("points").array.size()<lo||path.get("points").array.size()>hi)
        return why=resident?"a resident's path has 1 to 15 points after its position":"invalid bounded actor path",false;
    if(resident){out.speed_given=path.object.contains("speed");out.speed=0;if(out.speed_given&&!number(path.get("speed"),0,1500,out.speed,true))return why="path speed must be a number of game units per second",false;}
    else if(!number(path.get("speed"),0,1500,out.speed,true))return why="invalid bounded actor path",false;
    const auto mode=path.get("mode").string("once");
    if(mode!="once"&&mode!="loop"&&mode!="back_and_forth")return why="path mode must be once, loop or back_and_forth",false;
    out.loop=mode=="loop";out.back_and_forth=mode=="back_and_forth";
    if(path.object.contains("pauses")){if(path.get("pauses").type!=Value::Bool)return why="path pauses must be true or false",false;out.pauses=path.get("pauses").boolean;}
    for(const char* clip:{"walk","idle"})if(path.object.contains(clip)){if(!id(path.get(clip)))return why=std::string("path ")+clip+" must name a clip",false;(std::string(clip)=="walk"?out.walk_clip:out.idle_clip)=path.get(clip).text;}
    for(const auto& p:path.get("points").array){WorldPoint point_;if(!keys(p,{"x","y","z"})||!point(p,point_))return why="invalid path point",false;out.points.push_back(point_);}
    auto apart=[](const WorldPoint& p,const WorldPoint& q){return std::hypot(double(p.x)-q.x,double(p.z)-q.z)>1;};
    if(resident&&!apart(start,out.points.front()))return why="the first path point must be more than one unit from the position",false;
    for(size_t n=1;n<out.points.size();++n)if(!apart(out.points[n-1],out.points[n]))return why="path points must be separated horizontally by more than one unit",false;
    if(out.loop&&!apart(resident?start:out.points.front(),out.points.back()))return why="loop closing edge is too short",false;
    return true;
}
// The mod's room paths ("paths", story_room_path.h): lines and areas in a room that the mod's own actors
// name by id ("path": "<id>"), e.g. a wall cannon's firing area. Up to 16 per mod, 2..32 points each.
inline bool parse_room_paths(const mods::json::Value& m,std::vector<story_room_path::Path>& out,std::string& why){
    using namespace world_manifest;out.clear();const auto& list=m.get("paths");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>story_room_path::kMaxPerMod)return why="paths must be a list of up to 16",false;
    for(const auto& v:list.array){story_room_path::Path p;
        if(!keys(v,{"id","stage","room","points","closed","next"}))return why="a path takes id, stage, room, points, closed and next",false;
        if(v.object.contains("next")){if(!id(v.get("next")))return why="path "+v.get("id").string()+": next must name another of the mod's paths",false;p.next=v.get("next").text;}
        if(!id(v.get("id")))return why="invalid path id",false;p.id=v.get("id").text;
        for(const auto& old:out)if(old.id==p.id)return why="duplicate path id "+p.id,false;
        if(!place(v,p.stage,p.room))return why="path "+p.id+": stage and room (0..63) are required",false;
        if(v.object.contains("closed")){if(v.get("closed").type!=Value::Bool)return why="path "+p.id+": closed must be true or false",false;p.closed=v.get("closed").boolean;}
        const auto& pts=v.get("points");
        if(pts.type!=Value::Array)return why="path "+p.id+": points must be a list",false;
        for(const auto& q:pts.array){WorldPoint w;if(!keys(q,{"x","y","z","release"})||!point(q,w))return why="path "+p.id+": invalid path point",false;p.points.push_back({w.x,w.y,w.z});
            if(q.object.contains("release")){if(q.get("release").type!=Value::Bool)return why="path "+p.id+": a point's release must be true or false",false;
                if(q.get("release").boolean){p.release.resize(pts.array.size(),false);p.release[p.points.size()-1]=true;}}}
        if(const auto no=story_room_path::shape_problem(p);!no.empty())return why="path "+p.id+": "+no,false;
        out.push_back(std::move(p));
    }
    // a next path: another of the mod's paths in the same stage and room
    for(const auto&p:out)if(!p.next.empty()){
        const story_room_path::Path* n=nullptr;for(const auto&q:out)if(q.id==p.next)n=&q;
        if(!n||n==&p)return why="path "+p.id+": next must name another of the mod's paths",false;
        if(n->stage!=p.stage||n->room!=p.room)return why="path "+p.id+": its next path must be in the same stage and room",false;}
    return true;
}
inline const story_room_path::Path* find_room_path(const std::vector<story_room_path::Path>& paths,const std::string& id){
    for(const auto& p:paths)if(p.id==id)return &p;
    return nullptr;
}
inline bool parse_actors_impl(const mods::json::Value& m,std::vector<ManifestActor>& out,std::string& why) {
    using namespace world_manifest;out.clear();std::vector<ManifestModel> models;if(!parse_models(m,models,why))return false;const auto& list=m.get("actors");
    std::vector<story_room_path::Path> room_paths;if(!parse_room_paths(m,room_paths,why))return false;
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>kMaxActorsPerMod)return why="actors must be a list of up to 64",false;
    unsigned need=0;if(m.get("requires").type==Value::Object&&!story_game_objects::required_version(m,std::max(story_game_objects::catalogue().version,story_game_pieces::catalogue().version),need,why))return false;
    unsigned chart_marker_count=0,calm_water_count=0;
    for(const auto& v:list.array){ManifestActor a;
        if(!keys(v,{"id","actor","kind","model","animation","look_at","path","collision","behaviour","group","buoyancy","drift","anchor","grapple_points","distant","shoreline","chart_marker","arrival","calm_water","attention","stage","room","position","rotation","scale","params","parameters","flag","broken_flag","defeated_flag","drops","carry","combat","native_drops","switch_flag","switch","defeat_switch","when","lines","then","texture","talks","variant"}))return why="unknown actor field",false;
        if(!id(v.get("id")))return why="invalid actor id",false;a.id=v.get("id").text;a.actor=v.get("actor").string();
        if(story_game_pieces::is_reference(a.actor)){
            if(!game_piece_fields(v,need,a,why))return false;
            unsigned count=placed_actors(a);for(const auto& old:out){if(old.id==a.id)return why="duplicate actor id",false;if(old.stage==a.stage&&old.room==a.room)count+=placed_actors(old);}
            if(count>kMaxActorsPerRoom)return why="actors exceed the 16 per room limit",false;
            out.push_back(std::move(a));continue;
        }
        if(a.game_object()){
            if(!game_object_fields(v,need,a,why))return false;
            unsigned count=placed_actors(a);for(const auto& old:out){if(old.id==a.id)return why="duplicate actor id",false;if(old.stage==a.stage&&old.room==a.room)count+=placed_actors(old);}
            if(count>kMaxActorsPerRoom)return why="actors exceed the 16 per room limit",false;
            out.push_back(std::move(a));continue;
        }
        if(v.object.contains("variant"))return why="actor "+a.id+": variant is for game objects (\"actor\": \"game:<id>\")",false;
        if(v.object.contains("kind")) {
            if(v.get("kind").string()!="mod"||v.object.contains("actor"))return why="mod actor kind cannot name a native actor",false;
            a.kind="mod";a.actor="mod";a.model=v.get("model").string();
            if(!declared(m,"models",a.model))return why="mod actor names an undeclared model",false;
            bool skinned_model=false;for(const auto& model:models)if(model.id==a.model){skinned_model=model.kind=="skinned";a.kit_model=model.kind=="kit";}
            if(v.object.contains("look_at")){
                const auto& look=v.get("look_at");if(!skinned_model)return why="look_at requires a skinned model",false;
                if(!keys(look,{"head","yaw_degrees","pitch_degrees","rate_degrees","radius","upper_body"})||!bone_name(look.get("head"))||!number(look.get("yaw_degrees"),0,180,a.look_at.yaw_degrees)||!number(look.get("pitch_degrees"),0,90,a.look_at.pitch_degrees)||!number(look.get("rate_degrees"),0,1080,a.look_at.rate_degrees,true)||!number(look.get("radius"),0,1000000,a.look_at.radius,true))return why="invalid bounded look_at settings",false;
                a.look_at.enabled=true;a.look_at.head=look.get("head").text;
                if(look.object.contains("upper_body")){const auto& upper=look.get("upper_body");if(!keys(upper,{"bone","yaw_degrees","fraction"})||!bone_name(upper.get("bone"))||upper.get("bone").text==a.look_at.head||!number(upper.get("yaw_degrees"),0,180,a.look_at.upper_yaw_degrees)||!number(upper.get("fraction"),0,1,a.look_at.upper_fraction))return why="invalid upper_body look_at settings",false;a.look_at.upper_bone=upper.get("bone").text;}
            }
            if(v.object.contains("path")){
                if(!skinned_model)return why="path requires a skinned model",false;
                if(!parse_path(v.get("path"),a.position,false,a.path,why))return false;
            }
            if(v.object.contains("animation")){
                if(!id(v.get("animation")))return why="invalid initial animation name",false;
                bool skinned=false;for(const auto& model:models)if(model.id==a.model)skinned=model.kind=="skinned";
                if(!skinned)return why="animation requires a skinned model",false;a.animation=v.get("animation").text;
            }
            if(v.object.contains("params")||v.object.contains("parameters"))return why="mod actors cannot use native parameters",false;
            const auto& collision=v.get("collision");
            if(v.object.contains("behaviour")){
                a.behaviour=v.get("behaviour").string();
                if(a.behaviour!="static"&&!a.water_object())return why="world object behaviour must be static, floating or drifting",false;
                if(!a.water_object()&&(v.object.contains("buoyancy")||v.object.contains("drift")||v.object.contains("anchor")))return why="buoyancy, drift and anchor require a water object",false;
                if(a.water_object()){
                    const auto& b=v.get("buoyancy");if(b.type!=Value::Null){if(!keys(b,{"draft","damping","max_tilt"}))return why="invalid buoyancy settings",false;
                    if((b.object.contains("draft")&&!number(b.get("draft"),-1000,1000,a.buoyancy.draft))||(b.object.contains("damping")&&!number(b.get("damping"),0,10,a.buoyancy.damping))||(b.object.contains("max_tilt")&&!number(b.get("max_tilt"),0,30,a.buoyancy.max_tilt)))return why="buoyancy settings exceed their bounds",false;}
                    if(a.behaviour=="drifting"){const auto& d=v.get("drift");if(!keys(d,{"wind_speed","current","max_speed"}))return why="drifting requires bounded drift settings",false;
                        if((d.object.contains("wind_speed")&&!number(d.get("wind_speed"),0,100,a.drift.wind_speed))||(d.object.contains("max_speed")&&!number(d.get("max_speed"),0,100,a.drift.max_speed,true)))return why="invalid drift speed",false;
                        if(d.object.contains("current")){const auto& q=d.get("current");if(!keys(q,{"x","z"})||!number(q.get("x"),-100,100,a.drift.current.x)||!number(q.get("z"),-100,100,a.drift.current.z))return why="current needs bounded x and z velocities",false;}
                        if(a.drift.wind_speed==0&&a.drift.current.x==0&&a.drift.current.z==0)return why="drifting requires wind or current",false;
                        if(v.object.contains("anchor")){const auto& q=v.get("anchor");if(!keys(q,{"point","radius"})||!keys(q.get("point"),{"x","y","z"})||!point(q.get("point"),a.anchor.point)||!number(q.get("radius"),0,10000,a.anchor.radius,true))return why="anchor needs a world point and positive radius up to 10000",false;a.anchor.enabled=true;}
                    }else if(v.object.contains("drift")||v.object.contains("anchor"))return why="drift and anchor require drifting behaviour",false;
                }
                if(skinned_model||v.object.contains("carry")||v.object.contains("combat")||v.object.contains("path")||v.object.contains("animation")||v.object.contains("attention")||v.object.contains("talks")||v.object.contains("lines")||v.object.contains("then"))return why="world objects require a rigid model without actor interaction, animation or paths",false;
                if(collision.get("mesh").type!=Value::Null){
                    if(!keys(collision,{"mesh"})||!id(collision.get("mesh")))return why="invalid collision mesh reference",false;
                    a.collision_mesh=collision.get("mesh").text;
                    auto source=std::find_if(models.begin(),models.end(),[&](const auto& model){return model.id==a.collision_mesh;});
                    if(source==models.end()||(source->kind!="static"&&!(source->kind=="kit"&&a.collision_mesh==a.model)))return why="collision mesh must name a declared static model (a kit model's collision is its own model's)",false;
                    if(a.kit_model&&a.collision_mesh!=a.model)return why="a kit model's collision is its own model's",false;
                }else{
                    if(!keys(collision,{"shape","size","radius","height","surface"}))return why="invalid simple collision shape",false;
                    a.collision_shape=collision.get("shape").string();a.collision_surface=collision.get("surface").string("stone");
                    if(a.collision_surface!="stone"&&a.collision_surface!="grass"&&a.collision_surface!="wood"&&a.collision_surface!="sand"&&a.collision_surface!="metal"&&a.collision_surface!="wall")return why="unknown collision surface",false;
                    if(a.collision_shape=="box"){
                        const auto& size=collision.get("size");
                        if(collision.object.contains("radius")||collision.object.contains("height")||!keys(size,{"x","y","z"})||!number(size.get("x"),0,double(story_world_collision::mesh::MaxExtent),a.collision_size.x,true)||!number(size.get("y"),0,double(story_world_collision::mesh::MaxExtent),a.collision_size.y,true)||!number(size.get("z"),0,double(story_world_collision::mesh::MaxExtent),a.collision_size.z,true))return why="box collision needs positive size x/y/z up to 100000",false;
                    }else if(a.collision_shape=="cylinder"||a.collision_shape=="sphere"){
                        if(collision.object.contains("size")||!number(collision.get("radius"),0,double(story_world_collision::mesh::MaxExtent)/2,a.collision_radius,true))return why="shape radius must be positive and at most 50000",false;
                        if(a.collision_shape=="cylinder"){if(!number(collision.get("height"),0,double(story_world_collision::mesh::MaxExtent),a.collision_height,true))return why="cylinder height must be positive and at most 100000",false;}
                        else if(collision.object.contains("height"))return why="sphere collision does not take height",false;
                    }else return why="collision requires mesh or box, cylinder or sphere shape",false;
                }
            }else{
            if(!keys(collision,{"radius","height","mode"}))return why="mod actor needs cylinder collision",false;
            const auto& radius=collision.get("radius");const auto& height=collision.get("height");
            if(radius.type!=Value::Number||height.type!=Value::Number||!std::isfinite(radius.number)||!std::isfinite(height.number)||radius.number<=0||radius.number>1000||height.number<=0||height.number>2000)return why="collision radius must be 0..1000 and height 0..2000 (exclusive zero)",false;
            const auto mode=collision.get("mode").string();if(mode!="solid"&&mode!="pushable")return why="collision mode must be solid or pushable",false;
            a.collision_radius=float(radius.number);a.collision_height=float(height.number);if(a.collision_radius<=0||a.collision_height<=0)return why="collision dimensions are too small",false;a.pushable=mode=="pushable";
            }
            const auto& attention=v.get("attention");if(attention.type!=Value::Null){if(!keys(attention,{"prompt"}))return why="invalid mod actor attention",false;a.prompt=attention.get("prompt").string();if(a.prompt!="talk"&&a.prompt!="check")return why="attention prompt must be talk or check",false;}
            if(v.object.contains("flag")){if(!flag_id(v.get("flag")))return why="mod actor flag must be a mod-owned id",false;a.flag=v.get("flag").text;}
            if(v.object.contains("combat")){
                if(!skinned_model||!story_mod_combat::parse(v.get("combat"),a.combat,why))return why=why.empty()?"combat requires a skinned model":why,false;
                if(a.combat.stun.clip.empty()||a.combat.death.clip.empty())return why="combat requires named stun and death animation clips",false;
                for(const char* forbidden:{"flag","lines","then","talks","attention"})if(v.object.contains(forbidden))return why="combat actors cannot declare dialogue, prompts or generic flags",false;
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
        }else if(v.object.contains("model")||v.object.contains("animation")||v.object.contains("look_at")||(v.object.contains("path")&&a.actor!="resident"&&a.actor!="bokoblin"&&a.actor!="moblin"&&!story_enemy_variants::takes_room_path(a.actor))||v.object.contains("collision")||v.object.contains("attention")||v.object.contains("behaviour")||v.object.contains("buoyancy")||v.object.contains("drift")||v.object.contains("anchor"))return why="model, animation, look_at, path, collision and attention require kind mod",false;
        if(v.object.contains("carry")){
            const auto& carry=v.get("carry");if(a.kind!="mod"||a.combat_enabled||!keys(carry,{"break_on_impact"})||(carry.object.contains("break_on_impact")&&carry.get("break_on_impact").type!=Value::Bool))return why="carry requires a noncombat mod actor and boolean break_on_impact",false;
            a.carry_enabled=true;a.carry_break_on_impact=carry.get("break_on_impact").boolean;
            if(!v.object.contains("collision")||v.object.contains("attention")||v.object.contains("lines")||v.object.contains("then")||v.object.contains("talks")||v.object.contains("flag")||a.look_at.enabled||a.pushable||a.collision_radius>100||a.collision_height>200)return why="carry actors require a bounded solid body and no talk, look_at or pushable mode",false;
        }
        if((v.object.contains("combat")||v.object.contains("native_drops"))&&!a.combat_enabled)return why="combat and native_drops require a combat mod actor",false;
        if(a.actor=="kanban"||a.actor=="PROC_KANBAN")a.actor="sign";
        if(a.actor=="Tbox"||a.actor=="takara"||a.actor=="PROC_TBOX")a.actor="chest";
        if(a.actor=="NpcPeople"||a.actor=="PROC_NPC_PEOPLE")a.actor="resident";
        const bool prop=a.actor=="pot"||a.actor=="barrel"||a.actor=="crate";
        if(v.object.contains("texture")){if(!prop||!texture_path(v.get("texture")))return why="texture requires a placed pot, barrel or crate and a package PNG in textures/ or assets/",false;a.texture=v.get("texture").text;}
        const bool switch_actor=a.actor=="switch",door=a.actor=="door";
        const bool enemy=a.actor=="chuchu"||a.actor=="bokoblin"||a.actor=="moblin"||a.actor=="keese"||a.actor=="miniblin"||a.actor=="peahat"||a.actor=="darknut"||a.actor=="rat_hole"||a.actor=="bokobaba"||a.actor=="bubble"||a.actor=="octorok"||story_enemy_variants::more_enemy(a.actor);
        if(a.actor!="sign"&&a.actor!="resident"&&a.actor!="chest"&&!prop&&!switch_actor&&!door&&!enemy&&!(a.actor=="mod"&&a.kind=="mod"))return why="unknown or unsupported flag-setting actor: "+a.actor,false;
        if(!place(v,a.stage,a.room)||!point(v.get("position"),a.position))return why="actor "+a.id+": invalid stage, room or position",false;
        if(v.get("rotation").type!=Value::Null&&!point(v.get("rotation"),a.rotation,360))return why="invalid actor rotation",false;
        if(v.get("scale").type!=Value::Null&&(!point(v.get("scale"),a.scale,10)||a.scale.x<=0||a.scale.y<=0||a.scale.z<=0))return why="invalid actor scale",false;
        if(v.object.contains("group")){if(!id(v.get("group"))||!a.world_object()||a.behaviour!="static")return why="actor "+a.id+": group names the composition of static world objects it belongs to (an id)",false;a.group=v.get("group").text;}
        if((v.object.contains("buoyancy")||v.object.contains("drift")||v.object.contains("anchor"))&&!a.water_object())return why="motion settings require floating or drifting behaviour",false;
        // a kit model: a static world object turned about the vertical axis only, at its own size (as game pieces)
        if(a.kit_model&&(a.behaviour!="static"||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1||a.rotation.x!=0||a.rotation.z!=0))return why="actor "+a.id+": a kit model is a static world object, turned about the vertical axis only, at scale 1",false;
        const bool island_fields=v.object.contains("distant")||v.object.contains("shoreline")||v.object.contains("chart_marker")||v.object.contains("arrival")||v.object.contains("calm_water");
        if(island_fields&&(!a.world_object()||a.behaviour!="static"||a.stage!="sea"))return why="distant, shoreline, chart_marker, arrival and calm_water require a static sea world object",false;
        auto visual_model=[&](const char*key,ManifestVisualModel&out){
            if(!v.object.contains(key))return true;const auto&value=v.get(key);
            const bool shore=std::string(key)=="shoreline";
            if(!(shore?keys(value,{"model","material"}):keys(value,{"model"}))||!id(value.get("model")))return false;
            if(shore&&value.object.contains("material")){const auto&m=value.get("material");if(m.type!=Value::String||(m.text!="foam"&&!water_surface_material(m.text)))return false;if(m.text!="foam")out.material=m.text;}
            out.model=value.get("model").text;
            // a distant model may be a kit model (drawn with game materials, as the island it stands for); a shoreline may not
            return std::any_of(models.begin(),models.end(),[&](const auto&model){return model.id==out.model&&(model.kind=="static"||(!shore&&model.kind=="kit"));});
        };
        if(!visual_model("distant",a.distant)||!visual_model("shoreline",a.shoreline))return why="distant and shoreline must name declared same-package rigid static models (a shoreline's material is foam, water, pond, river, waterfall or lava)",false;
        auto island_text=[](const Value&value,std::string&out){std::u16string decoded;if(value.type!=Value::String||value.text.empty()||value.text.size()>96||!story_mail::append_utf16(value.text,false,decoded))return false;out=value.text;return true;};
        if(v.object.contains("arrival")){
            const auto&value=v.get("arrival");
            if(!keys(value,{"name","radius","flag"})||!island_text(value.get("name"),a.arrival.name)||!number(value.get("radius"),100,double(story_world_collision::mesh::MaxExtent),a.arrival.radius)||!flag_id(value.get("flag")))return why="arrival requires a single-line name up to 96 bytes, radius 100..100000 and a mod-owned flag",false;
            a.arrival.enabled=true;a.arrival.flag=value.get("flag").text;
        }
        if(v.object.contains("calm_water")){
            namespace calm=story_world_calm;const auto&value=v.get("calm_water");auto&c=a.calm_water;
            if(!keys(value,{"radius","falloff","offset"})||!number(value.get("radius"),calm::kMinRadius,calm::kMaxRadius,c.radius))
                return why="actor "+a.id+": calm_water requires a radius of 100..100000 (and may give falloff 800..25600 and offset {x, z})",false;
            if(value.object.contains("falloff")&&!number(value.get("falloff"),calm::kMinFalloff,calm::kMaxFalloff,c.falloff))return why="actor "+a.id+": calm_water falloff must be 800..25600",false;
            if(value.object.contains("offset")){const auto&o=value.get("offset");
                if(!keys(o,{"x","z"})||!number(o.get("x"),-calm::kMaxOffset,calm::kMaxOffset,c.offset_x)||!number(o.get("z"),-calm::kMaxOffset,calm::kMaxOffset,c.offset_z))return why="actor "+a.id+": calm_water offset must be {x, z} within 100000",false;}
            if(++calm_water_count>calm::kMaxPerMod)return why="a mod may declare at most eight calm_water zones",false;
            c.enabled=true;
        }
        if(v.object.contains("chart_marker")){
            const auto&value=v.get("chart_marker");
            if(!keys(value,{"label","when_flag"})||!island_text(value.get("label"),a.chart_marker.label))return why="chart_marker requires a single-line label up to 96 bytes",false;
            if(value.object.contains("when_flag")){if(!flag_id(value.get("when_flag"))||!known_flag(m,value.get("when_flag").text))return why="chart marker when_flag must name a known mod-owned producer",false;a.chart_marker.when_flag=value.get("when_flag").text;}
            if(++chart_marker_count>8)return why="a mod may declare at most eight island chart markers",false;
            a.chart_marker.enabled=true;
        }
        if(v.object.contains("grapple_points")){
            const auto&points=v.get("grapple_points");
            if(!a.world_object()||points.type!=Value::Array||points.array.size()>8)return why="grapple_points requires a world object and at most eight points",false;
            for(const auto&value:points.array){ManifestGrapplePoint p;
                if(!keys(value,{"id","position","yaw"})||!id(value.get("id"))||!keys(value.get("position"),{"x","y","z"})||!point(value.get("position"),p.position,story_world_collision::mesh::MaxExtent))return why="grapple point needs an id and bounded local position",false;
                p.id=value.get("id").text;
                if(value.object.contains("yaw")&&!number(value.get("yaw"),-360,360,p.yaw))return why="grapple point yaw must be within -360 to 360 degrees",false;
                if(std::any_of(a.grapple_points.begin(),a.grapple_points.end(),[&](const auto&old){return old.id==p.id;}))return why="grapple point ids must be unique within an object",false;
                a.grapple_points.push_back(std::move(p));
            }
        }
        if(a.water_object()&&(a.stage!="sea"||a.rotation.x!=0||a.rotation.z!=0))return why="water objects require sea placement and yaw-only base rotation",false;
        if(a.anchor.enabled&&std::hypot(double(a.position.x)-a.anchor.point.x,double(a.position.z)-a.anchor.point.z)>a.anchor.radius)return why="object position lies outside its anchor radius",false;
        if(enemy){
            if(!native_enemy_fields(m,v,a,why))return false;
            for(const auto& old:out)if(old.defeated_flag==a.defeated_flag)return why="enemies require distinct defeated_flag IDs",false;
            // a patrolling Bokoblin: its own route (1..15 points after its position), walked with the game's
            // patrol code: round (loop) or there and back (back_and_forth); its pace and pauses are its own
            if((a.actor=="bokoblin"||a.actor=="moblin")&&v.object.contains("path")){
                const auto& path=v.get("path");
                if(path.type==Value::Object&&(path.object.contains("speed")||path.object.contains("pauses")))return why="a Bokoblin's or Moblin's path takes points and mode (its pace and pauses are its own)",false;
                if(!parse_path(path,a.position,true,a.path,why))return false;
                if(!a.path.loop&&!a.path.back_and_forth)return why="a Bokoblin's or Moblin's path mode is loop or back_and_forth",false;
            }
            // an enemy on one of the mod's room paths ("paths"): its id, a path of its own stage and room
            // (an area where the kind needs one: a wall cannon's firing area); required by the kinds and
            // variants that take one (story_enemy_variants::room_path_rule), refused by the others
            if(story_enemy_variants::takes_room_path(a.actor)){
                const uint8_t rule=story_enemy_variants::room_path_rule(a.actor,a.variant);
                const auto& path=v.get("path");
                if(!rule){if(v.object.contains("path"))return why="actor "+a.id+": this "+a.actor+" takes no path (a variant that does: "+story_enemy_variants::names(a.actor)+")",false;}
                else{
                    if(path.type!=Value::String)return why=std::string("a ")+(a.variant.empty()?a.actor:a.variant+" "+a.actor)+" needs \"path\": the id of one of the mod's paths"+(rule==story_enemy_variants::kAreaPath?" (an area: its firing area)":""),false;
                    const auto* rp=find_room_path(room_paths,path.text);
                    if(!rp)return why="actor "+a.id+": no path \""+path.text+"\" in the mod's paths",false;
                    if(rp->stage!=a.stage||rp->room!=a.room)return why="actor "+a.id+": path \""+path.text+"\" is in another stage or room",false;
                    if(rule==story_enemy_variants::kAreaPath&&!rp->closed)return why="actor "+a.id+": path \""+path.text+"\" must be an area (closed)",false;
                    // a Wizzrobe summoner teleports between the points of its path and summons at the points of the
                    // path's next path: one is required; its pool members add .summon<N> to its id and flag
                    if(story_enemy_variants::summoner(a.actor,a.variant)){
                        if(rp->next.empty())return why="actor "+a.id+": a Wizzrobe summoner's path needs a next path (\"next\": the points where it summons)",false;
                        const unsigned pool=story_enemies_summons::size(story_enemy_variants::angle_z(a.actor,a.variant));
                        const size_t extra=story_enemies_summons::suffix(pool).size();
                        if(a.id.size()+extra>story_mail::kMaxIdBytes||a.defeated_flag.size()+extra>story_mail::kMaxIdBytes)return why="actor "+a.id+": its id and defeated_flag are at most "+std::to_string(story_mail::kMaxIdBytes-extra)+" characters (its summons add "+story_enemies_summons::suffix(pool)+")",false;}
                    // a Kargaroc with a rider lets it go at a release point of its path: one is required
                    if(!story_enemy_variants::rider_kind(a.actor,a.variant).empty()){
                        if(std::none_of(rp->release.begin(),rp->release.end(),[](bool r){return r;}))return why="actor "+a.id+": a Kargaroc with a rider needs a point of its path marked \"release\" (where it lets the rider go)",false;
                        if(a.id.size()+6>story_mail::kMaxIdBytes||a.defeated_flag.size()+6>story_mail::kMaxIdBytes)return why="actor "+a.id+": its id and defeated_flag are at most 26 characters (its rider adds .rider)",false;}
                    a.room_path=path.text;
                }
            }
        }else if(a.combat_enabled){
            if(a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1)return why="combat actors require yaw-only rotation and scale 1",false;
            for(const auto& old:out)if(old.defeated_flag==a.defeated_flag)return why="combat actors require distinct defeated_flag IDs",false;
        }else if(v.object.contains("defeated_flag"))return why="defeated_flag requires an enemy",false;
        if(v.object.contains("defeat_switch")){
            if(!enemy&&!a.combat_enabled)return why="defeat_switch requires an enemy",false;
            if(v.get("defeat_switch").type!=Value::String||!id(v.get("defeat_switch")))return why="defeat_switch must name a same-mod switch actor",false;
            a.defeat_switch=v.get("defeat_switch").text;
        }
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
            if(key=="resident"&&a.actor=="resident") {const auto* type=value.type==Value::String?story_resident_types::find(value.text):nullptr;if(type)a.resident=type->subtype;else if(integer(value,9,9))a.resident=9;else if(const char* no=value.type==Value::String?story_resident_types::refused(value.text):nullptr)return why=no,false;else return why="resident must be one of "+story_resident_types::codes()+" (legacy numeric 9)",false;}
            else if(key=="variant"&&enemy){}  // read by native_enemy_fields
            else if(key=="item"&&a.actor=="chest") {
                if(value.type==Value::String&&value.text.rfind("item:",0)==0){
                    if(!declared(m,"items",value.text.substr(5)))return why="chest item names an undeclared mod item (item:<id> of \"items\")",false;
                    a.item_mod=value.text.substr(5);a.item_game=kChestModItem;
                }else if(!(value.type==Value::String&&(a.item_game=chest_game_item(value.text))))
                    return why="chest item must be rupee_green, rupee_blue, rupee_yellow, rupee_red, rupee_purple, rupee_orange, rupee_silver, heart_piece, heart_container or item:<id>",false;
            }
            else return why="unsupported named actor parameter: "+key,false;
        }
        if(a.actor=="resident")if(const char* no=story_resident_types::place_problem(uint8_t(a.resident),a.stage,a.position.x,a.position.z))return why=no,false;
        if(a.actor=="resident"&&a.resident<256&&story_resident_types::goron(uint8_t(a.resident))&&(a.scale.x!=a.scale.y||a.scale.x!=a.scale.z||a.scale.x<0.5f||a.scale.x>1.5f))
            return why="a Goron merchant takes a uniform scale from 0.5 to 1.5",false;
        if(a.actor=="resident"&&v.object.contains("path")){
            if(const char* no=story_resident_types::no_walk(uint8_t(a.resident)))return why=no,false;
            if(!parse_path(v.get("path"),a.position,true,a.path,why))return false;
            const float pace=story_resident_types::walker(uint8_t(a.resident))->pace;
            if(a.path.speed_given&&(a.path.speed<pace*story_resident_types::kMinPaceFactor-0.001f||a.path.speed>pace*story_resident_types::kMaxPaceFactor+0.001f)){
                char text[160];std::snprintf(text,sizeof text,"path speed for this villager must be %.0f to %.0f units per second (own pace %.0f)",pace*story_resident_types::kMinPaceFactor,pace*story_resident_types::kMaxPaceFactor,pace);return why=text,false;}
        }
        if(a.actor=="sign"||a.actor=="chest"){if(!flag_id(v.get("flag")))return why="chests and signs require a mod-owned flag id",false;a.flag=v.get("flag").text;}
        if(!actor_condition(m,v.get("when"),a,why))return false;
        if(v.get("lines").type!=Value::Null&&!lines(v.get("lines"),a.lines))return why="invalid actor dialogue",false;
        if(!actions(m,v.get("then"),a.then,why))return false;
        if(!a.then.empty()&&a.lines.empty())return why="actor then requires dialogue lines",false;
        if(v.object.contains("talks")){
            const auto&talks=v.get("talks");
            const bool dialogue=a.actor=="resident"||a.actor=="sign"||(a.actor=="mod"&&!a.combat_enabled&&!a.carry_enabled);
            if(!dialogue||v.object.contains("lines")||v.object.contains("then"))return why="talks requires a dialogue actor and replaces its lines/then",false;
            if(talks.type!=Value::Array||talks.array.empty()||talks.array.size()>8)return why="talks must contain 1..8 ordered talks",false;
            for(const auto&t:talks.array){ManifestTalk talk;
                if(!keys(t,{"when","lines","then"})||!actor_condition(m,t.get("when"),talk,why)||!lines(t.get("lines"),talk.lines)||!actions(m,t.get("then"),talk.then,why))return why=why.empty()?"invalid actor talk":why,false;
                a.talks.push_back(std::move(talk));
            }
        }
        unsigned count=placed_actors(a);for(const auto& old:out){if(old.id==a.id)return why="duplicate actor id",false;if(old.stage==a.stage&&old.room==a.room)count+=placed_actors(old);}
        if(count>kMaxActorsPerRoom)return why="actors exceed the 16 per room limit",false;
        out.push_back(std::move(a));
    }
    for(const auto& a:out)if(!a.defeat_switch.empty()){
        const auto source=std::find_if(out.begin(),out.end(),[&](const ManifestActor& candidate){return candidate.id==a.defeat_switch;});
        if(source==out.end()||source->actor!="switch")return why="actor "+a.id+": defeat_switch must name a same-mod switch actor",false;
    }
    // a rat hole's rats are actors and enemies of their own: "<id>.ratN" with the defeated flag "<flag>.ratN"
    unsigned placed=0;for(const auto& a:out)placed+=placed_actors(a);
    if(placed>kMaxActorsPerMod)return why="actors exceed the 64 per mod limit (a rat hole counts 4: the hole and its three rats)",false;
    for(const auto& a:out)if(a.actor=="rat_hole"){
        if(a.id.size()>kRatHoleIdBytes||a.defeated_flag.size()>kRatHoleIdBytes)return why="rat hole "+a.id+": its id and defeated_flag are at most 27 characters (its rats add .rat1 to .rat3)",false;
        unsigned holes=0;for(const auto& b:out)if(b.actor=="rat_hole"&&b.stage==a.stage&&b.room==a.room)++holes;
        if(holes>kMaxRatHolesPerRoom)return why="at most two rat holes per room",false;
        for(unsigned n=1;n<=story_enemy_variants::kRatsPerHole;++n)for(const auto& b:out)
            if(b.id==a.id+".rat"+std::to_string(n)||(!b.defeated_flag.empty()&&b.defeated_flag==a.defeated_flag+".rat"+std::to_string(n)))return why="actor "+b.id+": the id or defeated_flag of rat hole "+a.id+"'s rats",false;
    }
    {unsigned summoners_here=0;for(const auto& a:out)if(story_enemy_variants::summoner(a.actor,a.variant)){
        for(const auto& b:out)if(&b!=&a&&story_enemy_variants::summoner(b.actor,b.variant)&&b.stage==a.stage&&b.room==a.room)return why="actor "+b.id+": at most one Wizzrobe summoner per room",false;
        if(++summoners_here>2)return why="a mod has at most two Wizzrobe summoners",false;
        for(const auto& b:out)if(b.id.rfind(a.id+".summon",0)==0)return why="actor "+b.id+": the id of Wizzrobe summoner "+a.id+"'s summons",false;}}
    for(const auto& a:out)if(!story_enemy_variants::rider_kind(a.actor,a.variant).empty())for(const auto& b:out)
        if(b.id==a.id+story_enemy_variants::kRiderSuffix||(!b.defeated_flag.empty()&&b.defeated_flag==a.defeated_flag+story_enemy_variants::kRiderSuffix))return why="actor "+b.id+": the id or defeated_flag of Kargaroc "+a.id+"'s rider",false;
    for(const auto& a:out)if(a.actor=="door"){
        const auto source=std::find_if(out.begin(),out.end(),[&](const ManifestActor& candidate){return candidate.id==a.switch_actor;});
        if(source==out.end()||source->actor!="switch")return why="door source must name a same-mod switch actor",false;
    }
    return game_object_budget(out,why)&&game_piece_budget(out,why);
}
inline bool parse_scenes_impl(const mods::json::Value& m,std::vector<ManifestScene>& out,std::string& why) {
    using namespace world_manifest;out.clear();const auto& list=m.get("scenes");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>kMaxScenesPerMod)return why="scenes must be a list of up to 16",false;
    for(const auto& v:list.array){ManifestScene s;if(!keys(v,{"id","trigger","steps","native"}))return why="unknown scene field",false;if(!id(v.get("id")))return why="invalid scene id",false;s.id=v.get("id").text;
        for(const auto& old:out)if(old.id==s.id)return why="duplicate scene id",false;
        const auto& trigger=v.get("trigger");if(trigger.type!=Value::Null){s.trigger=trigger.get("type").string();
            bool trigger_keys=false;
            if(s.trigger=="room")trigger_keys=keys(trigger,{"type","stage","room"});
            else if(s.trigger=="area")trigger_keys=keys(trigger,{"type","stage","room","position","radius","again"});
            else if(s.trigger=="talk")trigger_keys=keys(trigger,{"type","actor"});
            else if(s.trigger=="quest_step")trigger_keys=keys(trigger,{"type","quest","step"});
            else if(s.trigger=="salvage")trigger_keys=keys(trigger,{"type","chart"});
            if(!trigger_keys)return why="unknown scene trigger or field",false;
            if(s.trigger=="room"||s.trigger=="area"){if(!place(trigger,s.stage,s.room))return why="invalid scene room trigger",false;if(s.trigger=="area"){if(!point(trigger.get("position"),s.position)||trigger.get("radius").type!=Value::Number||!std::isfinite(trigger.get("radius").number)||trigger.get("radius").number<=0||trigger.get("radius").number>10000)return why="invalid scene area trigger",false;s.radius=float(trigger.get("radius").number);
                if(trigger.object.contains("again")){if(trigger.get("again").type!=Value::Bool)return why="a scene area trigger's again is true or false",false;s.again=trigger.get("again").boolean;}}}
            else if(s.trigger=="talk"){s.actor=trigger.get("actor").string();if(!declared(m,"actors",s.actor))return why="unknown trigger actor",false;}
            else if(s.trigger=="quest_step"){s.quest=trigger.get("quest").string();if(!integer(trigger.get("step"),1,254)||!quest_step(m,s.quest,unsigned(trigger.get("step").number)))return why="unknown trigger quest step",false;s.quest_step=unsigned(trigger.get("step").number);}
            else if(s.trigger=="salvage"){s.chart=trigger.get("chart").string();if(!declared(m,"charts",s.chart))return why="unknown trigger chart",false;}
            else return why="unknown scene trigger",false;
        }
        if(v.object.contains("native")){
            story_native_scene_manifest::Definition definition;
            if(!story_native_scene_manifest::parse(v.get("native"),definition,why))return false;
            std::vector<ManifestActor> placed;if(!parse_actors_impl(m,placed,why))return false;
            std::array<const ManifestActor*,2> references{};
            for(unsigned n=0;n<2;++n){for(const auto&a:placed)if(a.id==definition.actors[n].id)references[n]=&a;
                if(!references[n]||references[n]->actor!="resident"||references[n]->resident!=1)return why="native scene requires same-mod placed Uo2 resident actor references",false;
            }
            if(references[0]->stage!=references[1]->stage||references[0]->room!=references[1]->room)return why="native scene actors must share one stage and room",false;
            definition.stage=references[0]->stage;definition.room=references[0]->room;
            if((s.trigger=="room"||s.trigger=="area")&&(s.stage!=definition.stage||s.room!=definition.room))return why="native scene trigger and actor placements must share stage and room",false;
            s.native=std::move(definition);
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
            else if(type=="demo")step_keys=true;  // demo_scene::parse checks its own fields
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
            else if(type=="demo"){
                p.kind=SceneStep::Kind::Demo;demo_scene::Demo demo;
                if(!demo_scene::parse(fields,demo,why))return why="demo step: "+why,false;
                for(const auto&a:demo.actors)if(a.kind==demo_scene::Actor::Kind::Character&&!character(m,a.character))return why="demo step: unknown character "+a.character,false;
                // the step's deadline: the demo (30 Hz frames, scene units are 60 Hz), the wait for a previous demo's
                // cleanup (demo_start_gate.h) and reading time for each message
                p.frames=std::min<unsigned>(kMaxSceneFrames,demo.frames*2+120+2*demo_start_gate::kLimitTicks+unsigned(demo.messages.size())*1200);
                p.demo=std::move(demo);
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
// Internal render roles needed in addition to the authored actor declaration.
inline unsigned visual_variant_count(const ManifestActor&a){return unsigned(!a.distant.model.empty())+unsigned(!a.shoreline.model.empty());}
inline bool parse_actors(const mods::json::Value& m,std::vector<ManifestActor>& out,std::string& why) {
    out.clear();std::vector<ManifestActor> parsed;if(!parse_actors_impl(m,parsed,why))return false;out=std::move(parsed);return true;
}
inline bool parse_scenes(const mods::json::Value& m,std::vector<ManifestScene>& out,std::string& why) {
    out.clear();std::vector<ManifestScene> parsed;if(!parse_scenes_impl(m,parsed,why))return false;out=std::move(parsed);return true;
}
namespace world_manifest {
// Validate the selected package graph before any actor or scene registry is mutated.
inline bool validate_world_packages(const std::vector<std::pair<std::string,mods::json::Value>>& packages,std::string& why) {
    unsigned total=0,chart_markers=0;std::map<std::pair<std::string,unsigned>,unsigned> rooms;unsigned path_total=0;std::map<std::pair<std::string,unsigned>,unsigned> path_rooms;
    std::map<uint16_t,std::pair<std::string,std::string>> flags;std::vector<ManifestActor> game_objects;
    for(const auto& [mod,manifest]:packages){
        std::vector<ManifestActor> actors;std::vector<ManifestScene> scenes;
        if(!parse_actors(manifest,actors,why)||!parse_scenes(manifest,scenes,why)){why=mod+": "+why;return false;}
        std::vector<story_room_path::Path> paths;if(!parse_room_paths(manifest,paths,why)){why=mod+": "+why;return false;}
        for(const auto&p:paths){
            if(++path_total>story_room_path::kSlots)return why="enabled packages exceed 64 paths overall",false;
            if(++path_rooms[{p.stage,p.room}]>story_room_path::kMaxPerRoom)return why="enabled packages exceed 24 paths in "+p.stage+" room "+std::to_string(p.room),false;
        }
        for(const auto&actor:actors)if(actor.game_object()||actor.game_piece())game_objects.push_back(actor);
        if(!game_object_budget(game_objects,why)||!game_piece_budget(game_objects,why)){why="enabled packages: "+why;return false;}
        for(const auto&actor:actors)total+=placed_actors(actor)+(story_enemy_variants::summoner(actor.actor,actor.variant)?story_enemies_summons::size(story_enemy_variants::angle_z(actor.actor,actor.variant)):0u);
        if(total>128)return why="enabled packages exceed 128 actors overall (a Wizzrobe summoner's summons count here, not in a room's 16)",false;
        for(const auto&actor:actors)if(actor.chart_marker.enabled&&++chart_markers>32)return why="enabled packages exceed 32 island chart markers",false;
        std::vector<std::string> names;
        for(const auto& actor:actors){
            if((rooms[{actor.stage,actor.room}]+=placed_actors(actor))>kMaxActorsPerRoom)return why="enabled packages exceed 16 actors in "+actor.stage+" room "+std::to_string(actor.room),false;
            if(!actor.flag.empty())names.push_back(actor.flag);
            if(actor.arrival.enabled)names.push_back(actor.arrival.flag);
            if(actor.chart_marker.enabled&&!actor.chart_marker.when_flag.empty())names.push_back(actor.chart_marker.when_flag);
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
