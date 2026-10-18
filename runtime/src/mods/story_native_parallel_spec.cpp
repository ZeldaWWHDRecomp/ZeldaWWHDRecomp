// SPDX-License-Identifier: MPL-2.0
#include "story_native_parallel_spec.h"
#include "mod_json.h"
#include <cmath>
namespace story_native_parallel_spec {namespace {
using V=mods::json::Value;
bool keys(const V&v,std::initializer_list<const char*>allowed){if(v.type!=V::Object)return false;for(const auto&[k,x]:v.object){(void)x;bool yes=false;for(auto a:allowed)yes|=k==a;if(!yes)return false;}return true;}
bool id(const std::string&s){if(s.empty()||s.size()>32)return false;for(char c:s)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;return true;}
bool tick(const V&v,uint32_t max,uint32_t&out){if(v.type!=V::Number||!std::isfinite(v.number)||v.number<1||v.number>max||std::floor(v.number)!=v.number)return false;out=uint32_t(v.number);return true;}
bool point(const V&v,story_native_camera_image::Point&p){if(!keys(v,{"x","y","z"}))return false;float*vals[]={&p.x,&p.y,&p.z};unsigned n=0;for(auto k:{"x","y","z"}){const auto&a=v.get(k);if(a.type!=V::Number||!std::isfinite(a.number)||std::fabs(a.number)>350000)return false;*vals[n++]=float(a.number);}return true;}
}
bool parse(const std::string&json,const std::string&owner,const std::string&scene,std::span<const ActorDeclaration>actors,std::span<const SceneDeclaration>scenes,Spec&out,std::string&why){
 out={};why.clear();auto fail=[&](const char*s){why=s;return false;};
 try{if(!id(owner)||!id(scene)||json.size()>16384)return fail("Invalid native block identity/size");const auto root=mods::json::parse(json);
 if(!keys(root,{"version","clock","actors","camera","wait_ticks","timekeeper_ticks","watchdog_ticks","fallback_scene"})||root.get("version").type!=V::Number||root.get("version").number!=1||root.get("clock").string()!="native30")return fail("Native parallel block needs version1/native30");
 Spec next;const auto&list=root.get("actors");if(list.type!=V::Array||list.array.size()!=2)return fail("Native parallel needs exactly two owned Uo2 actors");
 for(unsigned n=0;n<2;++n){const auto&a=list.array[n];if(!keys(a,{"actor","action","position"})||a.get("action").string()!="turn_to_pos"||a.get("actor").type!=V::String||!id(a.get("actor").text))return fail("Unsupported native actor action");
 next.actors[n]=a.get("actor").text;const ActorDeclaration*found=nullptr;for(const auto&d:actors)if(d.owner==owner&&d.id==next.actors[n]){if(found)return fail("Ambiguous actor declaration");found=&d;}
 if(!found||!found->uo2||!found->token||found->stage.empty()||found->stage.size()>7||found->room>63)return fail("Native TURN requires declared owned Uo2");
 next.tokens[n]=found->token;if(!n){next.stage=found->stage;next.room=found->room;}else if(found->stage!=next.stage||found->room!=next.room||next.actors[0]==next.actors[1]||next.tokens[0]==next.tokens[1])return fail("Native actors must be distinct in one room");
 if(!point(a.get("position"),n?next.program.actor_b_target:next.program.actor_a_target))return fail("Invalid native TURN position");}
 const auto&camera=root.get("camera");auto&c=next.program.camera;
 if(!keys(camera,{"action","start_eye","start_center","end_eye","end_center","hold_ticks","move_ticks","fovy"})||camera.get("action").string()!="fixed_to_unitrans"||!point(camera.get("start_eye"),c.start_eye)||!point(camera.get("start_center"),c.start_center)||!point(camera.get("end_eye"),c.end_eye)||!point(camera.get("end_center"),c.end_center)||!tick(camera.get("hold_ticks"),9000,c.hold_timer)||!tick(camera.get("move_ticks"),9000,c.move_timer))return fail("Unsupported native camera action");
 const auto&f=camera.get("fovy");if(f.type!=V::Number||!std::isfinite(f.number)||f.number<5||f.number>120)return fail("Invalid native FOV");c.fovy=float(f.number);
 if(!tick(root.get("wait_ticks"),9000,next.program.wait_timer)||!tick(root.get("timekeeper_ticks"),9000,next.program.timekeeper_timer)||!tick(root.get("watchdog_ticks"),18000,next.watchdog_ticks)||next.watchdog_ticks<=next.program.timekeeper_timer)return fail("Invalid native30 tick bounds");
 std::vector<uint8_t>image;if(!story_native_turn_camera_image::make(next.program,image,why))return false;
 if(root.object.contains("fallback_scene")){const auto&f=root.get("fallback_scene");if(f.type!=V::String||!id(f.text)||f.text==scene)return fail("Invalid fallback scene");unsigned matches=0;for(const auto&d:scenes)if(d.owner==owner&&d.id==f.text&&d.fallback_eligible)++matches;if(matches!=1)return fail("Fallback must name one same-owner declared scene");next.fallback_scene=f.text;}
 out=std::move(next);return true;
 }catch(const std::exception&){return fail("Invalid native parallel JSON");}
}
bool fallback_allowed(const Spec&s,bool published,bool mutation,bool refused){return !s.fallback_scene.empty()&&!published&&!mutation&&refused;}
}
