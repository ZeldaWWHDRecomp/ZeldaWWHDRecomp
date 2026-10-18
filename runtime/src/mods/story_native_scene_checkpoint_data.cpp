// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_checkpoint.h"
#include "story_scenes.h"
namespace story_native_scene_checkpoint {
struct Checkpoint::State {
 std::shared_ptr<const story_native_scene_playback::Activation>activation;
 std::shared_ptr<story_native_scene_playback::RunOwnerCell>cell;
 std::shared_ptr<const void>life;std::string mod;story_notebook::ManifestScene scene;
 size_t index=0;uint64_t generation=0;
};
Checkpoint::Checkpoint(std::shared_ptr<const story_native_scene_playback::Activation>a,
 std::shared_ptr<story_native_scene_playback::RunOwnerCell>cell,std::shared_ptr<const void>life,
 std::string mod,story_notebook::ManifestScene scene,size_t index,uint64_t generation)
 :state_(std::make_shared<const State>(State{std::move(a),std::move(cell),std::move(life),std::move(mod),std::move(scene),index,generation})){}
const std::shared_ptr<const story_native_scene_playback::Activation>&Checkpoint::activation()const noexcept{return state_->activation;}
const std::shared_ptr<story_native_scene_playback::RunOwnerCell>&Checkpoint::cell()const noexcept{return state_->cell;}
const std::shared_ptr<const void>&Checkpoint::original_run_life()const noexcept{return state_->life;}
const std::string&Checkpoint::mod()const noexcept{return state_->mod;}
const story_notebook::ManifestScene&Checkpoint::definition()const noexcept{return state_->scene;}
size_t Checkpoint::scene_index()const noexcept{return state_->index;}
uint64_t Checkpoint::generation()const noexcept{return state_->generation;}
bool Checkpoint::definition_current()const noexcept{try{
 story_notebook::ManifestScene live;
 return state_->activation&&state_->cell&&state_->life&&state_->activation->definition_hash()==state_->activation->declaration()->definition_hash()&&
  state_->activation->declaration()->mod()==state_->mod&&state_->activation->declaration()->definition()==state_->scene&&
  story_scenes::declared_scene(state_->mod,state_->scene.id,live)&&live==state_->scene;
 }catch(...){return false;}}

}
