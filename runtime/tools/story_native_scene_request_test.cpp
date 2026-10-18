// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_scene_request.h"
#include "mods/story_scenes.h"
#include <cassert>
#include <thread>
#include <stdexcept>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <memory>
namespace R=story_native_scene_request;
namespace M=story_notebook;
static M::ManifestScene definition;
static std::array<story_actors::ResidentEventDeclaration,2> actors;
static unsigned reads=0;static bool mutate=false;
namespace story_scenes {bool declared_scene(const std::string&m,const std::string&s,M::ManifestScene&out){++reads;if(m!="demo"||s!="native_intro")return false;out=definition;if(mutate)++definition.native->wait_ticks;return true;}}
namespace story_actors {bool resident_event_declaration(const std::string&m,const std::string&id,ResidentEventDeclaration&out){++reads;if(m!="demo")return false;if(id=="npc_a")out=actors[0];else if(id=="npc_b")out=actors[1];else return false;return true;}}
namespace guestmods {class NativeSceneRequestIssuer {public:template<class F>static void run(F f){R::StartupWindow window;f();}};}
static mods::json::Value manifest(){return mods::json::parse(R"({"actors":[{"id":"npc_a","actor":"resident","stage":"sea","room":11,"position":{"x":250,"y":1040,"z":-201500},"parameters":{"resident":"uo2"}},{"id":"npc_b","actor":"resident","stage":"sea","room":11,"position":{"x":260,"y":1040,"z":-201500},"parameters":{"resident":"uo2"}}],"scenes":[{"id":"native_intro","steps":[{"type":"wait","frames":30},{"type":"end"}],"native":{"type":"turn_camera","actors":[{"id":"npc_a","turn_to":{"x":400,"y":1040,"z":-201500}},{"id":"npc_b","turn_to":{"x":100,"y":1040,"z":-201500}}],"wait_ticks":30,"camera":{"hold_ticks":30,"move_ticks":120,"start_eye":{"x":-500,"y":1350,"z":-201100},"start_target":{"x":250,"y":1100,"z":-201500},"end_eye":{"x":1000,"y":1350,"z":-201100},"end_target":{"x":250,"y":1100,"z":-201500},"fovy":45},"timekeeper_ticks":300,"watchdog_ticks":600}}]})");}

int main(){
 std::vector<M::ManifestScene> scenes;std::string why;assert(M::parse_scenes(manifest(),scenes,why));definition=scenes[0];const auto initial=definition;
 for(unsigned n=0;n<2;++n){actors[n].token=n+1;actors[n].room=11;actors[n].subtype=1;actors[n].stage="sea";actors[n].fingerprint[0]=uint8_t(n+8);}
 const auto originalActors=actors;
 assert(!R::startup_window_current()&&!R::resolve("demo","native_intro",why)&&reads==0);
 std::shared_ptr<const R::DeclaredRequest> captured;
 guestmods::NativeSceneRequestIssuer::run([&]{
  assert(R::startup_window_current());captured=R::resolve("demo","native_intro",why);assert(captured&&captured->current()&&captured->startup_current());
  assert(captured->compiled().actor_ids()[0]=="npc_a"&&captured->definition()==definition);
  const auto before=reads;std::thread foreign([&]{assert(!R::startup_window_current()&&!R::resolve("demo","native_intro",why));});foreign.join();assert(reads==before);
  for(unsigned bad=0;bad<9;++bad){definition=initial;actors=originalActors;switch(bad){case 0:definition.native.reset();break;case 1:definition.native->actors[0].id="missing";break;case 2:actors[0].token=0;break;case 3:actors[1].token=actors[0].token;break;case 4:actors[0].subtype=2;break;case 5:actors[0].stage="other";break;case 6:actors[0].room=12;break;case 7:definition.native->wait_ticks=0;break;case 8:mutate=true;break;}assert(!R::resolve("demo","native_intro",why));mutate=false;}
  definition=initial;actors=originalActors;
 });
 assert(!captured->startup_current()&&captured->current());
 guestmods::NativeSceneRequestIssuer::run([&]{assert(!captured->startup_current());auto fresh=R::resolve("demo","native_intro",why);assert(fresh&&fresh->startup_current()&&fresh->definition_hash()==captured->definition_hash());});
 for(unsigned bad=0;bad<6;++bad){definition=initial;actors=originalActors;switch(bad){case 0:definition.steps[0].then.push_back({});break;case 1:definition.steps[0].choices.push_back({});break;case 2:definition.steps[0].successors.push_back(1);break;case 3:++definition.native->camera.move_ticks;break;case 4:++actors[0].token;break;case 5:++actors[1].fingerprint[3];break;}assert(!captured->current());}
 definition=initial;actors=originalActors;
 try{guestmods::NativeSceneRequestIssuer::run([]{throw std::runtime_error("test");});assert(false);}catch(const std::runtime_error&){}assert(!R::startup_window_current());
 return 0;
}
