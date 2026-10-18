// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_pose_plan.h"
#include <cassert>
using namespace story_native_people_pose_plan;
int main(){Snapshot s;s.identity={0x47000000,12,3,4,5,6,0x48000000,0x48001000};s.angles[2]=42;s.cut[0x60]=1;Plan p;
assert(make(s,s.identity,true,true,true,false,p)&&p.patches.size()==1&&!p.refresh_base);
assert(make(s,s.identity,true,true,true,true,p)&&p.patches.size()==10&&p.refresh_base&&p.refresh_joints);
assert(p.patches[1].address==s.identity.actor+0x320&&p.patches[1].bytes[2]==42);
for(const auto&v:p.patches)assert(v.address!=s.identity.actor+0x2E0&&v.address!=s.identity.actor+0x314);
auto stale=s.identity;++stale.pid;assert(!make(s,stale,true,true,true,true,p)&&p.patches.empty());
assert(!make(s,s.identity,false,true,true,true,p));assert(!make(s,s.identity,true,false,true,true,p));assert(!make(s,s.identity,true,true,false,true,p));
s.cut[0x60]=2;assert(!make(s,s.identity,true,true,true,true,p));}
