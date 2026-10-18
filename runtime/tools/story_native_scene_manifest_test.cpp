// SPDX-License-Identifier: MPL-2.0
#include "mods/story_world_manifest.h"
#include <cassert>
#include <limits>
#include <string>
#include <vector>
namespace N=story_native_scene_manifest;
using mods::json::Value;
using namespace story_notebook;
static Value example(){return mods::json::parse(R"({"actors":[{"id":"npc_a","actor":"resident","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"parameters":{"resident":"uo2"}},{"id":"npc_b","actor":"resident","stage":"sea","room":11,"position":{"x":10,"y":0,"z":0},"parameters":{"resident":"uo2"}}],"scenes":[{"id":"intro","trigger":{"type":"room","stage":"sea","room":11},"steps":[{"type":"wait","frames":60},{"type":"end"}],"native":{"type":"turn_camera","actors":[{"id":"npc_a","turn_to":{"x":100,"y":0,"z":0}},{"id":"npc_b","turn_to":{"x":-100,"y":0,"z":0}}],"wait_ticks":30,"camera":{"hold_ticks":30,"move_ticks":30,"start_eye":{"x":0,"y":10,"z":20},"start_target":{"x":0,"y":0,"z":0},"end_eye":{"x":10,"y":10,"z":20},"end_target":{"x":10,"y":0,"z":0},"fovy":60},"timekeeper_ticks":300,"watchdog_ticks":301}}]})");}
static Value&native(Value&v){return v["scenes"].array[0]["native"];}
int main(){
 static_assert(N::public_selection_enabled());
 std::string why;std::vector<ManifestScene>scenes;
 auto v=example();assert(parse_scenes(v,scenes,why)&&scenes.size()==1&&scenes[0].native&&scenes[0].steps.size()==2&&scenes[0].steps[0].frames==60);
 const auto d=*scenes[0].native;assert(d.stage=="sea"&&d.room==11&&d.actors[0].id=="npc_a"&&d.actors[1].id=="npc_b"&&d.wait_ticks==30&&d.camera.move_ticks==30&&d.watchdog_ticks==301);
 v["scenes"].array[0].object.erase("native");assert(parse_scenes(v,scenes,why)&&!scenes[0].native&&scenes[0].steps[0].frames==60);
 v=example();auto&n=native(v);n["camera"]["hold_ticks"]=8999;n["camera"]["move_ticks"]=1;n["wait_ticks"]=9000;n["timekeeper_ticks"]=9000;n["watchdog_ticks"]=18000;assert(parse_scenes(v,scenes,why)&&scenes[0].native->watchdog_ticks==18000);n["watchdog_ticks"]=9001;assert(parse_scenes(v,scenes,why));
 // Every failure is atomic, even after a preceding valid scene was parsed.
 for(unsigned bad=0;bad<42;++bad){v=example();auto&n=native(v);auto&c=n["camera"];switch(bad){
 case 0:n["unknown"]=1;break;case 1:n["type"]="wait";break;case 2:n["actors"].array.pop_back();break;case 3:n["actors"].array.push_back(n["actors"].array[0]);break;case 4:n["actors"].array[1]["id"]="npc_a";break;case 5:n["actors"].array[0]["id"]="missing";break;case 6:n["actors"].array[0]["id"]="foreign:npc";break;case 7:n["actors"].array[0]["callback"]=123;break;case 8:n["actors"].array[0]["turn_to"]["x"]=350001;break;case 9:n["actors"].array[0]["turn_to"]["x"]=std::numeric_limits<double>::quiet_NaN();break;case 10:n["actors"].array[0]["turn_to"]["y"]=std::numeric_limits<double>::infinity();break;case 11:n["actors"].array[0]["turn_to"]["extra"]=0;break;
 case 12:c["hold_ticks"]=0;break;case 13:c["move_ticks"]=9001;break;case 14:c["hold_ticks"]=9000;break;case 15:c["move_ticks"]=1.5;break;case 16:c["fovy"]=4;break;case 17:c["fovy"]=121;break;case 18:c["fovy"]=std::numeric_limits<double>::infinity();break;case 19:c.object.erase("end_target");break;case 20:c["algorithm"]=11;break;
 case 21:c["start_eye"]=mods::json::parse(R"({"x":0,"y":0,"z":1})");c["start_target"]=mods::json::parse(R"({"x":0,"y":0,"z":0})");c["end_eye"]=mods::json::parse(R"({"x":0,"y":0,"z":-1})");c["end_target"]=c["start_target"];break;
 case 22:n["wait_ticks"]=0;break;case 23:n["wait_ticks"]=9001;break;case 24:n["timekeeper_ticks"]=59;break;case 25:n["timekeeper_ticks"]=9001;break;case 26:n["watchdog_ticks"]=300;break;case 27:n["watchdog_ticks"]=18001;break;case 28:n["watchdog_ticks"]=true;break;
 case 29:v["actors"].array[0]["parameters"]["resident"]="um1";break;case 30:v["actors"].array[0]["parameters"]["resident"]="ym1";break;case 31:v["actors"].array[1]["room"]=12;break;case 32:v["actors"].array[1]["stage"]="town";break;case 33:v["scenes"].array[0]["trigger"]["room"]=12;break;case 34:v["scenes"].array[0].object.erase("steps");break;case 35:v["scenes"].array[0]["steps"].array.clear();break;case 36:v["scenes"].array[0]["steps"].array.pop_back();break;case 37:n=Value{};break;case 38:v["actors"].array[1]["id"]="npc_a";break;case 39:v["actors"].array[0]["actor"]="sign";v["actors"].array[0].object.erase("parameters");v["actors"].array[0]["flag"]="read";break;case 40:n.object.erase("wait_ticks");break;case 41:c["start_eye"]["offset"]=1;break;
 }
 scenes.resize(1);assert(!parse_scenes(v,scenes,why)&&scenes.empty()&&!why.empty());
 auto second=v;auto first=example()["scenes"].array[0];first.object.erase("native");first["id"]="first";second["scenes"].array.insert(second["scenes"].array.begin(),first);assert(!parse_scenes(second,scenes,why)&&scenes.empty());
 }
 // Direct pure-data parser clears stale output, and exact camera boundary1 is
 // accepted while a sub-unit view vector is rejected.
 v=example();N::Definition definition;assert(N::parse(native(v),definition,why));native(v)["camera"]["fovy"]=std::numeric_limits<double>::quiet_NaN();assert(!N::parse(native(v),definition,why)&&definition==N::Definition{});
 v=example();for(const char*prefix:{"start_eye","end_eye"})native(v)["camera"][prefix]=mods::json::parse(R"({"x":0,"y":0,"z":1})");for(const char*prefix:{"start_target","end_target"})native(v)["camera"][prefix]=mods::json::parse(R"({"x":0,"y":0,"z":0})");native(v)["camera"]["fovy"]=5;assert(parse_scenes(v,scenes,why));native(v)["camera"]["fovy"]=120;assert(parse_scenes(v,scenes,why));native(v)["camera"]["start_eye"]["z"]=0.99;assert(!parse_scenes(v,scenes,why));
 v=example();v["actors"].array[0]["actor"]="NpcPeople";v["actors"].array[1]["actor"]="PROC_NPC_PEOPLE";assert(parse_scenes(v,scenes,why));
}
