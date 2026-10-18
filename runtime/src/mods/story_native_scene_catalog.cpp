// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_catalog.h"
#include "story_scenes.h"
#include <set>
namespace story_native_scene_catalog {
StartupCatalog::StartupCatalog(story_native_scene_request::StartupWitness startup,decltype(ids_)ids,decltype(requests_)requests):startup_(std::move(startup)),ids_(std::move(ids)),requests_(std::move(requests)){}
bool StartupCatalog::current()const{try{
 if(story_scenes::native_scene_ids()!=ids_)return false;
 for(const auto&r:requests_)if(!r||!r->current())return false;
 return story_scenes::native_scene_ids()==ids_;
}catch(...){return false;}}
bool StartupCatalog::startup_current()const{
 if(!startup_.current())return false;
 for(const auto&r:requests_)if(!r->startup_current())return false;
 return current();
}
std::optional<StartupCatalog>collect(std::string&why){why.clear();try{
 auto startup=story_native_scene_request::current_startup();if(!startup){why="native catalog requires the actual committed startup window";return {};}
 auto ids=story_scenes::native_scene_ids();
 if(ids.size()>kMaxDeclarations){why="native catalog exceeds 128 declarations";return {};}
 std::set<story_scenes::NativeSceneId>unique;
 std::vector<std::shared_ptr<const story_native_scene_request::DeclaredRequest>>requests;
 for(const auto&id:ids)if(!unique.insert(id).second){why="duplicate native scene declaration ID";return {};}
 for(const auto&[mod,scene]:ids){
  auto request=story_native_scene_request::resolve(mod,scene,why);if(!request)return {};
  requests.push_back(std::move(request));
 }
 StartupCatalog catalog(std::move(*startup),std::move(ids),std::move(requests));
 if(!catalog.startup_current()){why="native declarations changed during catalog capture";return {};}
 return catalog;
}catch(...){why="native catalog capture failed";return {};}}
}
