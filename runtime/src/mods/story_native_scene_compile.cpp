// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_compile.h"
#include <utility>
namespace story_native_scene_compile {namespace {
bool id(const std::string&s){if(s.empty()||s.size()>32)return false;for(char c:s)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;return true;}
bool stage(const std::string&s){if(s.empty()||s.size()>8)return false;for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;return true;}
story_native_camera_image::Point point(story_native_scene_manifest::Point p){return {p.x,p.y,p.z};}
}
Compiled::Compiled(const story_native_scene_manifest::Definition&d,story_native_event_program::Program p):actors_{d.actors[0].id,d.actors[1].id},stage_(d.stage),room_(d.room),program_(std::move(p)){}
std::optional<Compiled>compile(const story_native_scene_manifest::Definition&d,std::string&why){
 why.clear();if(!id(d.actors[0].id)||!id(d.actors[1].id)||d.actors[0].id==d.actors[1].id||!stage(d.stage)||d.room>63){why="native scene requires distinct valid placed actor IDs and stage/room";return {};}
 story_native_turn_camera_image::Program p;p.actor_a_target=point(d.actors[0].turn_to);p.actor_b_target=point(d.actors[1].turn_to);p.wait_timer=d.wait_ticks;p.timekeeper_timer=d.timekeeper_ticks;
 p.camera.hold_timer=d.camera.hold_ticks;p.camera.move_timer=d.camera.move_ticks;p.camera.start_eye=point(d.camera.start_eye);p.camera.start_center=point(d.camera.start_target);p.camera.end_eye=point(d.camera.end_eye);p.camera.end_center=point(d.camera.end_target);p.camera.fovy=d.camera.fovy;
 // This authoritative factory also validates direct C++ DTOs: finite bounded
 // points/FOV, continuous view separation, every timer and external watchdog.
 auto program=story_native_event_program::Program::turn(p,d.watchdog_ticks);if(!program){why="native scene does not match verified E60 TURN/camera grammar";return {};}
 return Compiled(d,std::move(*program));
}
}
