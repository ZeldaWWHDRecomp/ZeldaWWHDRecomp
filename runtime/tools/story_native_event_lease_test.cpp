// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_lease.h"
#include "mods/story_native_event_observation.h"
#include <cassert>
#include <initializer_list>
using namespace story_native_event_lease;
int main(){Identity i{0x20000000,0x60000000,0x520,1,2,3,4};Observation o{i,false,false};Lease a;assert(a.prepare(i));assert(!a.prepare(i));assert(!a.started(o));assert(a.publish(o));assert(a.started(o));assert(a.finish(o));assert(!a.can_free(story_native_event_observation::Result{})&&!a.retire(o));o.consumers_detached=true;assert(a.retire(o)&&!a.can_free(story_native_event_observation::Result{})&&!a.can_restore_retail(story_native_event_observation::Result{}));
 for(unsigned k=0;k<7;++k){Lease b;assert(b.prepare(i));Identity x=i;
  if(k==0)x.manager+=4;if(k==1)x.image+=16;if(k==2)x.bytes+=16;if(k==3)x.stage++;if(k==4)x.room++;if(k==5)x.generation++;if(k==6)x.program++;
  assert(!b.publish({x,false,true}));assert(b.phase()==Phase::Quarantined&&!b.can_free(story_native_event_observation::Result{})&&!b.can_restore_retail(story_native_event_observation::Result{}));}
 Lease b;assert(b.prepare(i));assert(!b.publish({i,true,true})&&!b.can_free(story_native_event_observation::Result{}));
 for(Phase p:{Phase::Prepared,Phase::Published,Phase::Running,Phase::Draining}){Lease c;assert(c.prepare(i));assert(c.restore(i,p,o)&&c.phase()==p&&!c.can_free(story_native_event_observation::Result{}));}
 Lease c;assert(c.prepare(i));assert(!c.restore(i,Phase::Retired,o)&&!c.can_free(story_native_event_observation::Result{}));
 Lease d;assert(d.prepare(i));Identity stale=i;stale.generation++;assert(!d.restore(stale,Phase::Running,{stale,false,true}));
 Lease first,second;assert(first.prepare(i));Identity other=i;other.image+=0x1000;other.generation++;assert(second.prepare(other));assert(first.publish(o));assert(second.phase()==Phase::Prepared);first.invalidate();assert(second.phase()==Phase::Prepared);
}
