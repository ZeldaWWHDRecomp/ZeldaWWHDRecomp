// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_pose_plan.h"
namespace story_native_people_pose_plan {
bool make(const Snapshot&s,const Identity&current,bool uo2,bool idle,bool detached,bool cancel,Plan&out){
 out={};const auto&i=s.identity;
 if(!(i==current)||!uo2||!idle||!detached||!i.actor||!i.pid||i.pid==~0u||!i.token||!i.stage||!i.room||!i.generation||!i.morf||!i.model||uint64_t(i.actor)+0x920>0x100000000ULL||s.cut[0x60]>1||s.cut[0x61]>1)return false;
 auto add=[&](uint32_t off,const auto&bytes){out.patches.push_back({i.actor+off,{bytes.begin(),bytes.end()}});};
 // Cut restores borrowed pointers/attention for either completion policy.
 add(0x3E0,s.cut);
 if(!cancel)return true; // normal completion retains native final facing/pose
 add(0x320,s.angles);add(0x328,s.shape);add(0x3AC,s.joint);add(0x884,s.target);add(0x8F8,s.velocity);
 out.patches.push_back({i.actor+0x8D8,{s.head_only}});out.patches.push_back({i.actor+0x8FD,{s.noticed}});
 out.patches.push_back({i.actor+0x8FF,{s.talk_ready}});out.patches.push_back({i.actor+0x90D,{s.target_mode}});
 // Only lease-owned status bits9800; producer must merge with fresh actual word.
 // Whole status restoration is deliberately not a patch.
 out.refresh_base=true;out.refresh_joints=true;return true;
}
}
