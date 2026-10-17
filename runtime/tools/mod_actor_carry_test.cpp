// SPDX-License-Identifier: MPL-2.0
#include "mods/mod_actor_carry.h"
#include <cassert>
#include <limits>
using namespace mod_actor::carry;
int main(){State a;a.pid=42;assert(valid(a,42));assert(!hold(a,43,7));assert(hold(a,42,7));assert(!hold(a,42,8));State snapshot=a;assert(release(a,true,19,34,16384));assert(a.phase==2&&std::fabs(a.vx-19)<.001&&std::fabs(a.vz)<.001);assert(advance(a)&&a.vy==32);assert(impact(a,true)&&a.phase==3);assert(!impact(a,true));assert(!advance(a));a=snapshot;assert(release(a,false,0,0,0)&&a.phase==0);assert(hold(a,42,7));assert(!release(a,true,std::numeric_limits<float>::infinity(),34,0)&&a.phase==1);assert(release(a,true,19,34,0));for(unsigned i=0;i<300;++i)assert(advance(a));assert(!advance(a));quarantine(a);assert(a.phase==4&&!hold(a,42,7));a=snapshot;a.link_pid=0;assert(!valid(a,42));a=snapshot;a.frames=1;assert(!valid(a,42));a=snapshot;a.vx=1;assert(!valid(a,42));a=snapshot;quarantine(a);assert(valid(a,42)&&a.link_pid==0);a=snapshot;a.pid=43;assert(!valid(a,42));a=snapshot;a.version=2;assert(!valid(a,42));a=snapshot;assert(release(a,true,19,34,0));assert(impact(a,false)&&a.phase==0);}
