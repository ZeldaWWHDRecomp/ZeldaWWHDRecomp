// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_coverage.h"
#include <cmath>
namespace story_native_scene_coverage {
Coverage inspect(const story_notebook::ManifestScene&s){
 using K=story_notebook::SceneStep::Kind;Coverage out;
 auto fallback=[&](const char*why){out.reason=why;return out;};
 if(!s.native)return fallback("scene has no explicit native alternative");
 if(s.steps.empty()||s.steps.size()>story_notebook::kMaxSceneSteps)return fallback("fallback graph has no bounded terminal path");
 auto point=[](const auto&p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<=350000&&std::fabs(p.y)<=350000&&std::fabs(p.z)<=350000;};
 for(size_t n=0;n<s.steps.size();++n){const auto&step=s.steps[n];
  if(step.kind==K::End){
   if(n+1!=s.steps.size()||!step.successors.empty()||!step.next.empty())return fallback("native alternative requires one final END");
   out.selection=Selection::NativeAlternative;out.end_step=n;out.rewards=step.then;out.reason.clear();return out;
  }
  if(n+1==s.steps.size()||(!step.successors.empty()&&step.successors!=std::vector<size_t>{n+1})||(!step.next.empty()&&step.next!=s.steps[n+1].id))return fallback("fallback graph is not linear");
  if(!step.then.empty())return fallback("fallback has intermediate actions");
  if(!step.frames||step.frames>story_notebook::kMaxSceneFrames)return fallback("fallback step has invalid timing");
  if(step.kind==K::Wait)continue;
  if(step.kind==K::Face){
   if(!step.target_actor.empty()||(step.actor!=s.native->actors[0].id&&step.actor!=s.native->actors[1].id)||!point(step.position))return fallback("FACE is outside the two static native actor families");
   continue;
  }
  if(step.kind==K::Camera){
   if(!step.target_actor.empty()||!point(step.eye)||!point(step.target)||(step.pan&&(!point(step.eye_to)||!point(step.target_to))))return fallback("CAMERA requires static targets");
   continue;
  }
  return fallback("fallback step family is unsupported by the native alternative");
 }
 return fallback("fallback has no END");
}
}
