// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_scene_compile.h"
#include "mods/story_world_manifest.h"
#include <cassert>
#include <limits>
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
namespace N=story_native_scene_manifest;
namespace C=story_native_scene_compile;
static mods::json::Value manifest(){return mods::json::parse(R"({"actors":[{"id":"npc_a","actor":"resident","stage":"sea","room":11,"position":{"x":250,"y":1040,"z":-201500},"parameters":{"resident":"uo2"}},{"id":"npc_b","actor":"resident","stage":"sea","room":11,"position":{"x":260,"y":1040,"z":-201500},"parameters":{"resident":"uo2"}}],"scenes":[{"id":"native_intro","steps":[{"type":"wait","frames":30},{"type":"end"}],"native":{"type":"turn_camera","actors":[{"id":"npc_a","turn_to":{"x":400,"y":1040,"z":-201500}},{"id":"npc_b","turn_to":{"x":100,"y":1040,"z":-201500}}],"wait_ticks":30,"camera":{"hold_ticks":30,"move_ticks":120,"start_eye":{"x":-500,"y":1350,"z":-201100},"start_target":{"x":250,"y":1100,"z":-201500},"end_eye":{"x":1000,"y":1350,"z":-201100},"end_target":{"x":250,"y":1100,"z":-201500},"fovy":45},"timekeeper_ticks":300,"watchdog_ticks":600}}]})");}
int main(){
 std::vector<story_notebook::ManifestScene>scenes;std::string why;assert(story_notebook::parse_scenes(manifest(),scenes,why)&&scenes.size()==1&&scenes[0].native);const auto original=*scenes[0].native;
 const std::array<std::string,2>expected_ids{"npc_a","npc_b"};auto compiled=C::compile(original,why);assert(compiled&&why.empty()&&compiled->actor_ids()==expected_ids&&compiled->stage()=="sea"&&compiled->room()==11);
 // Independently reproduce the former private bootstrap literals; authoring
 // must produce the exact existing immutable E60 image, not a new grammar.
 story_native_turn_camera_image::Program old;old.wait_timer=30;old.timekeeper_timer=300;old.actor_a_target={400,1040,-201500};old.actor_b_target={100,1040,-201500};old.camera.start_eye={-500,1350,-201100};old.camera.start_center={250,1100,-201500};old.camera.end_eye={1000,1350,-201100};old.camera.end_center={250,1100,-201500};old.camera.hold_timer=30;old.camera.move_timer=120;old.camera.fovy=45;
 const auto prior=story_native_event_program::Program::turn(old,600);assert(prior&&compiled->program()==*prior&&std::equal(prior->pristine().begin(),prior->pristine().end(),compiled->program().pristine().begin()));
 assert(compiled->program().bytes()==0xE60&&compiled->program().completion_mask()==0x1FF&&compiled->program().watchdog()==600&&compiled->program().initial_cut(0)==7&&compiled->program().cut(0,7)->action==3&&compiled->program().cut(0,0)->action==0&&compiled->program().cut(2,2)->action==4&&compiled->program().cut(2,6)->action==5);
 story_native_turn_camera_image::Program decoded;std::vector<uint8_t>bytes(compiled->program().pristine().begin(),compiled->program().pristine().end());assert(story_native_turn_camera_image::validate(bytes,decoded,why)&&decoded.wait_timer==30&&decoded.camera.move_timer==120);
 auto d=original;d.camera.hold_ticks=8999;d.camera.move_ticks=1;d.wait_ticks=d.timekeeper_ticks=9000;d.watchdog_ticks=18000;auto boundary=C::compile(d,why);assert(boundary&&boundary->program().watchdog()==18000&&boundary->program().turn_definition()->timekeeper_timer==9000);d.watchdog_ticks=9001;assert(C::compile(d,why));
 for(unsigned bad=0;bad<31;++bad){d=original;switch(bad){
 case 0:d.actors[0].id.clear();break;case 1:d.actors[0].id=d.actors[1].id;break;case 2:d.actors[0].id="other:npc";break;case 3:d.actors[1].id=std::string(33,'a');break;case 4:d.actors[0].id="NPC";break;case 5:d.stage.clear();break;case 6:d.stage="012345678";break;case 7:d.stage="a/b";break;case 8:d.stage=std::string("sea\0x",5);break;case 9:d.room=64;break;
 case 10:d.actors[0].turn_to.x=std::numeric_limits<float>::quiet_NaN();break;case 11:d.actors[1].turn_to.y=std::numeric_limits<float>::infinity();break;case 12:d.actors[0].turn_to.z=350001;break;case 13:d.camera.fovy=4;break;case 14:d.camera.fovy=121;break;case 15:d.camera.fovy=std::numeric_limits<float>::quiet_NaN();break;case 16:d.camera.start_eye.x=350001;break;case 17:d.camera.end_target.y=-350001;break;
 case 18:d.camera.start_eye={0,0,1};d.camera.start_target={0,0,0};d.camera.end_eye={0,0,-1};d.camera.end_target={0,0,0};break;
 case 19:d.camera.hold_ticks=0;break;case 20:d.camera.move_ticks=0;break;case 21:d.camera.hold_ticks=9000;break;case 22:d.camera.move_ticks=std::numeric_limits<uint32_t>::max();break;case 23:d.wait_ticks=0;break;case 24:d.wait_ticks=9001;break;case 25:d.timekeeper_ticks=149;break;case 26:d.timekeeper_ticks=9001;break;case 27:d.watchdog_ticks=300;break;case 28:d.watchdog_ticks=18001;break;case 29:d.watchdog_ticks=0;break;case 30:d.room=std::numeric_limits<unsigned>::max();break;
 }assert(!C::compile(d,why)&&!why.empty());}
 d=original;d.stage="A_1";d.room=63;d.actors[0].id=std::string(32,'a');d.actors[0].turn_to={350000,-350000,350000};assert(C::compile(d,why));
 // Compiled values own their names and image; later DTO edits cannot alter it.
 d=original;auto owned=C::compile(d,why);assert(owned);d.actors[0].id="changed";d.camera.end_eye.x=99;assert(owned->actor_ids()[0]=="npc_a"&&owned->program()==*prior);
 static_assert(N::public_selection_enabled());
}
