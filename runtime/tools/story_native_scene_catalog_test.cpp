// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_scene_catalog.h"
#include "mods/story_scenes.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
namespace C=story_native_scene_catalog;namespace R=story_native_scene_request;namespace M=story_notebook;
static std::vector<story_scenes::NativeSceneId>ids;
static M::ManifestScene scene;static unsigned enumerations=0,actor_reads=0;static bool change_ids=false,throw_ids=false,bad_actor=false;
namespace story_scenes {
std::vector<NativeSceneId>native_scene_ids(){++enumerations;if(throw_ids)throw std::runtime_error("registry");auto out=ids;if(change_ids&&enumerations>1)out.emplace_back("late","entry");return out;}
bool declared_scene(const std::string&m,const std::string&s,M::ManifestScene&out){for(const auto&id:ids)if(id==NativeSceneId{m,s}){out=scene;out.id=s;return true;}return false;}
}
namespace story_actors {bool resident_event_declaration(const std::string&,const std::string&id,ResidentEventDeclaration&out){++actor_reads;if(bad_actor)return false;const unsigned n=id=="first"?0:id=="second"?1:2;if(n==2)return false;out={n+1,0,1,"sea",{}};return true;}}
namespace guestmods {class NativeSceneRequestIssuer {public:template<class F>static void run(F f){R::StartupWindow window;f();}};}
static void setup(){scene={};scene.id="intro";story_native_scene_manifest::Definition d;d.stage="sea";d.actors[0].id="first";d.actors[1].id="second";d.camera.start_eye={0,10,20};d.camera.end_eye={10,10,20};d.camera.hold_ticks=d.camera.move_ticks=30;d.camera.fovy=45;d.wait_ticks=30;d.timekeeper_ticks=300;d.watchdog_ticks=600;scene.native=d;M::SceneStep end;end.kind=M::SceneStep::Kind::End;scene.steps={end};ids={{"one","intro"},{"two","intro"}};enumerations=actor_reads=0;change_ids=throw_ids=bad_actor=false;}
int main(){setup();std::string why;assert(!C::collect(why)&&enumerations==0&&actor_reads==0);
 std::optional<C::StartupCatalog>captured;
 guestmods::NativeSceneRequestIssuer::run([&]{captured=C::collect(why);assert(captured&&captured->requests().size()==2&&captured->startup_current());assert(captured->requests()[0]->mod()=="one"&&captured->requests()[1]->mod()=="two");});
 assert(captured->current()&&!captured->startup_current());
 guestmods::NativeSceneRequestIssuer::run([&]{assert(!captured->startup_current());});
 scene.steps[0].then.push_back({M::StoryAction::Kind::Flag,"changed",1});assert(!captured->current());setup();ids.push_back({"new","intro"});assert(!captured->current());
 for(unsigned bad=0;bad<5;++bad){setup();if(bad==0)ids.push_back(ids.front());if(bad==1){ids.clear();for(unsigned n=0;n<129;++n)ids.emplace_back("mod"+std::to_string(n),"intro");}if(bad==2)bad_actor=true;if(bad==3)change_ids=true;if(bad==4)throw_ids=true;
 guestmods::NativeSceneRequestIssuer::run([&]{assert(!C::collect(why)&&!why.empty());if(bad<2)assert(actor_reads==0);});}
 setup();ids.clear();for(unsigned n=0;n<C::kMaxDeclarations;++n)ids.emplace_back("mod"+std::to_string(n),"intro");guestmods::NativeSceneRequestIssuer::run([&]{auto max=C::collect(why);assert(max&&max->requests().size()==128&&max->startup_current());});
 setup();ids.clear();std::optional<C::StartupCatalog>empty;
 guestmods::NativeSceneRequestIssuer::run([&]{empty=C::collect(why);assert(empty&&empty->requests().empty()&&empty->startup_current());});
 assert(empty->current()&&!empty->startup_current());
 guestmods::NativeSceneRequestIssuer::run([&]{assert(!empty->startup_current());auto fresh=C::collect(why);assert(fresh&&fresh->startup_current());});
}
