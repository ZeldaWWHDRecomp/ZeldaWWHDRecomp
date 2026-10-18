// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "mod_json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
namespace story_native_scene_manifest {
// Declarative authoring only. Timers are native 30Hz logic ticks, independent
// of the fallback scene.steps timeline. No executable image/Program dependency,
// allocation, actor lookup or native call is provided by this schema.
struct Point {float x{},y{},z{};bool operator==(const Point&)const=default;};
struct ActorTurn {std::string id;Point turn_to;bool operator==(const ActorTurn&)const=default;};
struct Camera {
 uint32_t hold_ticks{},move_ticks{};Point start_eye,start_target,end_eye,end_target;float fovy{};
 bool operator==(const Camera&)const=default;
};
struct Definition {
 std::array<ActorTurn,2>actors{};Camera camera;uint32_t wait_ticks{},timekeeper_ticks{},watchdog_ticks{};
 // Derived from the two independently parsed placed actor declarations, never
 // accepted as extra JSON authority. The coordinator must re-resolve live IDs.
 std::string stage;unsigned room{};
 bool operator==(const Definition&)const=default;
};
// Runtime readiness and the complete native lifecycle independently gate entry.
inline constexpr bool public_selection_enabled(){return true;}
namespace detail {
using V=mods::json::Value;
inline bool keys(const V&v,std::initializer_list<const char*>allowed){if(v.type!=V::Object)return false;for(const auto&[key,value]:v.object){bool found=false;for(const auto name:allowed)found|=key==name;if(!found)return false;}return true;}
inline bool integer(const V&v,uint32_t low,uint32_t high,uint32_t&out){if(v.type!=V::Number||!std::isfinite(v.number)||std::floor(v.number)!=v.number||v.number<low||v.number>high)return false;out=uint32_t(v.number);return true;}
inline bool id(const V&v){if(v.type!=V::String||v.text.empty()||v.text.size()>32)return false;for(char c:v.text)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;return true;}
inline bool point(const V&v,Point&out){if(!keys(v,{"x","y","z"}))return false;for(const char*k:{"x","y","z"})if(v.get(k).type!=V::Number||!std::isfinite(v.get(k).number)||std::fabs(v.get(k).number)>350000)return false;out={float(v.get("x").number),float(v.get("y").number),float(v.get("z").number)};return true;}
inline bool separated(const Camera&c){
 const double a[]={double(c.start_eye.x)-c.start_target.x,double(c.start_eye.y)-c.start_target.y,double(c.start_eye.z)-c.start_target.z};
 const double z[]={double(c.end_eye.x)-c.end_target.x,double(c.end_eye.y)-c.end_target.y,double(c.end_eye.z)-c.end_target.z};
 double dot=0,length=0;for(unsigned n=0;n<3;++n){const double d=z[n]-a[n];dot+=a[n]*d;length+=d*d;}
 const double t=length?std::clamp(-dot/length,0.0,1.0):0;double squared=0;for(unsigned n=0;n<3;++n){const double v=a[n]+t*(z[n]-a[n]);squared+=v*v;}return squared>=1;
}
}
// Parses the exact isolated TURN_TO_POS->WAIT / FIXEDFRM->UNITRANS grammar.
// Actor declaration/placement resolution is a separate same-mod parser step.
// Output is cleared on every failure; accepted data does not enable execution.
inline bool parse(const mods::json::Value&v,Definition&out,std::string&why){
 out={};why.clear();using namespace detail;Definition d;
 auto fail=[&](const char*message){why=message;return false;};
 if(!keys(v,{"type","actors","wait_ticks","camera","timekeeper_ticks","watchdog_ticks"})||v.get("type").string()!="turn_camera")return fail("native scene requires exact turn_camera grammar");
 const auto&actors=v.get("actors");if(actors.type!=V::Array||actors.array.size()!=2)return fail("native scene requires exactly two actor references");
 for(unsigned n=0;n<2;++n){const auto&a=actors.array[n];if(!keys(a,{"id","turn_to"})||!id(a.get("id"))||!point(a.get("turn_to"),d.actors[n].turn_to))return fail("invalid native actor reference or TURN target");d.actors[n].id=a.get("id").text;}
 if(d.actors[0].id==d.actors[1].id)return fail("native scene actor references must be distinct");
 const auto&c=v.get("camera");auto&camera=d.camera;
 if(!keys(c,{"hold_ticks","move_ticks","start_eye","start_target","end_eye","end_target","fovy"})||!integer(c.get("hold_ticks"),1,9000,camera.hold_ticks)||!integer(c.get("move_ticks"),1,9000,camera.move_ticks)||camera.hold_ticks+camera.move_ticks>9000||!point(c.get("start_eye"),camera.start_eye)||!point(c.get("start_target"),camera.start_target)||!point(c.get("end_eye"),camera.end_eye)||!point(c.get("end_target"),camera.end_target)||c.get("fovy").type!=V::Number||!std::isfinite(c.get("fovy").number)||c.get("fovy").number<5||c.get("fovy").number>120)return fail("invalid bounded native camera");
 camera.fovy=float(c.get("fovy").number);if(!separated(camera))return fail("native camera eye and target must remain one unit apart throughout move");
 if(!integer(v.get("wait_ticks"),1,9000,d.wait_ticks)||!integer(v.get("timekeeper_ticks"),1,9000,d.timekeeper_ticks)||d.timekeeper_ticks<d.wait_ticks||d.timekeeper_ticks<camera.hold_ticks+camera.move_ticks||!integer(v.get("watchdog_ticks"),1,18000,d.watchdog_ticks)||d.watchdog_ticks<=d.timekeeper_ticks)return fail("invalid native WAIT/timekeeper/watchdog bounds");
 out=std::move(d);return true;
}
}
