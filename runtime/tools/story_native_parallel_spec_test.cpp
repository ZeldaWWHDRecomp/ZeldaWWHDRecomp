// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_parallel_spec.h"
#include "mods/mod_json.h"
#include <cassert>
#include <string>
#include <vector>
using namespace story_native_parallel_spec;
int main(){const std::string json=R"({"version":1,"clock":"native30","actors":[{"actor":"a","action":"turn_to_pos","position":{"x":100,"y":150,"z":100}},{"actor":"b","action":"turn_to_pos","position":{"x":-100,"y":150,"z":100}}],"camera":{"action":"fixed_to_unitrans","start_eye":{"x":0,"y":100,"z":200},"start_center":{"x":0,"y":30,"z":0},"end_eye":{"x":100,"y":100,"z":200},"end_center":{"x":100,"y":30,"z":0},"hold_ticks":30,"move_ticks":30,"fovy":60},"wait_ticks":30,"timekeeper_ticks":300,"watchdog_ticks":900,"fallback_scene":"backup"})";
 std::vector<ActorDeclaration>actors={{"mod","a","sea",11,1,true},{"mod","b","sea",11,2,true}};std::vector<SceneDeclaration>scenes={{"mod","backup",true}};Spec spec;std::string why;
 assert(parse(json,"mod","main",actors,scenes,spec,why));assert(spec.tokens[0]==1&&spec.tokens[1]==2&&spec.program.camera.hold_timer==30&&spec.program.timekeeper_timer==300);
 assert(fallback_allowed(spec,false,false,true));assert(!fallback_allowed(spec,true,false,true));assert(!fallback_allowed(spec,false,true,true));assert(!fallback_allowed(spec,false,false,false));
 auto check=[&](auto value){return parse(mods::json::dump(value),"mod","main",actors,scenes,spec,why);};
 auto root=mods::json::parse(json);for(auto field:{"seconds","frames","steps","native_params"}){auto bad=root;bad[field]=1;assert(!check(bad)&&spec.actors[0].empty());}
 auto bad=root;bad["clock"]="fallback60";assert(!check(bad));bad=root;bad["version"]=2;assert(!check(bad));
 bad=root;bad["actors"].array[1]["actor"]="a";assert(!check(bad));bad=root;bad["actors"].array[0]["action"]="move_to_pos";assert(!check(bad));
 bad=root;bad["actors"].array[0]["position"]["x"]=350001;assert(!check(bad));
 for(auto n:{0,18001}){bad=root;bad["watchdog_ticks"]=n;assert(!check(bad));}bad=root;bad["watchdog_ticks"]=300;assert(!check(bad));bad=root;bad["wait_ticks"]=1.5;assert(!check(bad));
 bad=root;bad["fallback_scene"]="main";assert(!check(bad));bad=root;bad["fallback_scene"]="unknown";assert(!check(bad));
 auto old=actors;actors.push_back(actors[0]);assert(!parse(json,"mod","main",actors,scenes,spec,why));actors=old;actors[1].token=1;assert(!parse(json,"mod","main",actors,scenes,spec,why));actors=old;actors[1].uo2=false;assert(!parse(json,"mod","main",actors,scenes,spec,why));actors=old;actors[1].room=12;assert(!parse(json,"mod","main",actors,scenes,spec,why));actors=old;
 scenes[0].fallback_eligible=false;assert(!parse(json,"mod","main",actors,scenes,spec,why));scenes[0].fallback_eligible=true;
 auto duplicate=json;duplicate.insert(1,"\"version\":1,");assert(!parse(duplicate,"mod","main",actors,scenes,spec,why));
 duplicate=json;auto at=duplicate.find("\"actor\":\"a\"");duplicate.insert(at,"\"actor\":\"b\",");assert(!parse(duplicate,"mod","main",actors,scenes,spec,why));
}
