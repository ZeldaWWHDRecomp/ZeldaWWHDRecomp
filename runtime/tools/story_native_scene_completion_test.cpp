// SPDX-License-Identifier: MPL-2.0
#define main request_fixture_main_unused
#include "story_native_scene_request_test.cpp"
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
#undef main
#include "mods/story_native_scene_activation.h"
#include "mods/story_native_scene_completion.h"
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
// Synthetic issuers ONLY for the isolated value/one-use contract. Actual
// production issuers are runtime Context and the runtime activation frontend.
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer {public:static auto activation(std::shared_ptr<const R::DeclaredRequest>d){return std::shared_ptr<const story_native_scene_playback::Activation>(new story_native_scene_playback::Activation(std::move(d)));}};}
namespace story_native_wait_runtime_controller {class Context {public:static auto completion(story_native_event_observation::Expected e,std::shared_ptr<const story_native_scene_playback::Activation>a,std::shared_ptr<const void>run){return story_native_scene_completion::Completion(std::move(e),std::move(a),std::move(run));}};}
int main(){
 std::vector<M::ManifestScene>scenes;std::string why;assert(M::parse_scenes(manifest(),scenes,why));definition=scenes[0];
 for(unsigned n=0;n<2;++n){actors[n].token=n+1;actors[n].room=11;actors[n].subtype=1;actors[n].stage="sea";actors[n].fingerprint[0]=uint8_t(n+8);}
 std::shared_ptr<const R::DeclaredRequest>declaration;guestmods::NativeSceneRequestIssuer::run([&]{declaration=R::resolve("demo","native_intro",why);});assert(declaration&&declaration->current());
 auto a=story_native_wait_runtime::NativeScenePlaybackIssuer::activation(declaration),b=story_native_wait_runtime::NativeScenePlaybackIssuer::activation(declaration);assert(a!=b&&a->definition_hash()==b->definition_hash());
 story_native_event_observation::Expected expected;expected.identity={0x20010000,0x60010100,0xE60,1,2,3,4};expected.program=declaration->compiled().program();expected.frames=expected.program->frames();
 using Issuer=story_native_wait_runtime_controller::Context;auto run=std::make_shared<const unsigned>(7);
 auto good=Issuer::completion(expected,a,run);assert(good.expected()==expected);auto moved=std::move(good);assert(!good.consume(*a));assert(moved.consume(*a)&&!moved.consume(*a));
 auto wrong=Issuer::completion(expected,a,run);assert(!wrong.consume(*b)&&!wrong.consume(*a));
 auto noRun=Issuer::completion(expected,a,{});assert(!noRun.consume(*a));auto noActivation=Issuer::completion(expected,{},run);assert(!noActivation.consume(*a));
 auto durable=Issuer::completion(expected,a,run);run.reset();assert(durable.consume(*a)); // proof data survives issuer retirement
 auto once=Issuer::completion(expected,a,std::make_shared<const unsigned>(8));std::atomic<unsigned>winners=0;std::thread first([&]{if(once.consume(*a))++winners;}),second([&]{if(once.consume(*a))++winners;});first.join();second.join();assert(winners==1);
}
