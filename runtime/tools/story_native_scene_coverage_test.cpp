// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_scene_coverage.h"
#include <cassert>
#include <limits>
namespace C=story_native_scene_coverage;namespace M=story_notebook;using K=M::SceneStep::Kind;
static M::ManifestScene base(){M::ManifestScene s;s.native.emplace();s.native->actors[0].id="first";s.native->actors[1].id="second";M::SceneStep wait,end;wait.kind=K::Wait;wait.frames=30;end.kind=K::End;end.then.push_back({M::StoryAction::Kind::Flag,"reward",1});s.steps={wait,end};return s;}
static bool native(const M::ManifestScene&s){return C::inspect(s).selection==C::Selection::NativeAlternative;}
int main(){auto s=base();assert(native(s));auto eligible=C::inspect(s);assert(eligible.end_step==1&&eligible.rewards==s.steps[1].then);s.steps[1].then[0].value=2;assert(eligible.rewards[0].value==1);
 for(unsigned family=0;family<15;++family){s=base();s.steps[0].kind=K(family);s.steps[0].actor="first";const bool allowed=K(family)==K::Camera||K(family)==K::Face||K(family)==K::Wait;assert(native(s)==allowed);if(!allowed)assert(C::inspect(s).rewards.empty());}
 s=base();s.steps[0].kind=K::Face;s.steps[0].actor="second";s.steps[0].position={17,28,39};s.native->actors[1].turn_to={40,50,60};assert(native(s)); // explicit parallel alternative, no claimed target translation
 s=base();s.steps[0].kind=K::Camera;s.steps[0].pan=true;s.steps[0].eye_to={5,6,7};assert(native(s));
 s=base();s.steps.erase(s.steps.begin());assert(native(s)&&C::inspect(s).end_step==0);
 s=base();s.steps[0].id="start";s.steps[1].id="finish";s.steps[0].next="finish";s.steps[0].successors={1};assert(native(s));
 for(unsigned bad=0;bad<13;++bad){s=base();switch(bad){case 0:s.native.reset();break;case 1:s.steps.clear();break;case 2:s.steps[0].then.push_back({});break;case 3:s.steps[0].successors={1,1};break;case 4:s.steps[0].successors={0};break;case 5:s.steps[0].next="missing";break;case 6:s.steps[1].successors={0};break;case 7:s.steps.push_back(s.steps.back());break;case 8:s.steps[0].kind=K::Face;s.steps[0].actor="foreign";break;case 9:s.steps[0].kind=K::Face;s.steps[0].actor="first";s.steps[0].target_actor="second";break;case 10:s.steps[0].kind=K::Camera;s.steps[0].target_actor="first";break;case 11:s.steps[0].kind=K::Camera;s.steps[0].eye.x=std::numeric_limits<float>::infinity();break;case 12:s.steps[0].frames=0;break;}auto result=C::inspect(s);assert(result.selection==C::Selection::WholeFallback&&result.rewards.empty()&&!result.reason.empty());}
}
