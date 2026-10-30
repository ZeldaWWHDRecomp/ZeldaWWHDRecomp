#include "mods/map_hud_rules.h"
#include <cassert>
#include <cstdio>
using namespace guestmods::maphud;
int main(){
    CompassLease l;
    l.begin(1./30,true);assert(!l.held()); // hidden to hidden
    assert(l.request(1,true,true)==1);assert(l.request(2,true,true)==Busy);
    l.finish();assert(l.held());l.begin(1./30,true);l.finish();assert(!l.held()); // missed renewal
    for(int boundary:{249,250,449,450}){
        l.clear();l.begin(0,true);l.request(1,true,true);l.finish();
        // Mod owns its 250ms map exit, renewing while it remains visible.
        for(int t=1;t<=boundary;t++){
            l.begin(.001,true);
            if(t<250)l.request(1,true,true);
            if(t==250)l.request(1,false,true);
            if(t<450)assert(l.held());else assert(!l.held());
            l.finish();
        }
        assert(l.request(1,true,true)==1);l.finish();assert(l.held()); // reversal
    }
    for(int cause=0;cause<4;cause++){
        l.request(1,true,true);if(cause==0)l.revoke(2);else l.clear();
        assert(l.held()==(cause==0));l.revoke(1);assert(!l.held());
    }
    l.request(1,true,true);l.request(1,false,true);assert(l.request(2,true,true)==Busy);l.revoke(1);assert(!l.held());
    l.request(1,true,true);l.begin(.01,false);assert(!l.held()); // HUD gate/replacement
    ChartQueue q;
    assert(q.select(1,4,3,1)==1);assert(q.cancel(2)==Busy);assert(q.cancel(1)==1);
    assert(!q.take(1,true,false)); // cancel leaves native choice alone
    q.select(1,4,3,1);assert(!q.take(1,false,false)); // animation not interrupted
    assert(q.select(2,5,3,1)==Busy);assert(q.select(1,5,3,1)==1);
    auto r=q.take(1,true,false);assert(r&&r->index==5&&!q.pending());
    q.select(1,6,5,1);assert(!q.take(2,true,false)); // replacement cancels
    q.select(1,6,5,2);assert(!q.take(2,true,true)); // later GamePad input wins
    q.select(1,6,5,2);q.revoke(1);assert(!q.pending());
    assert(q.select(1,49,5,2)==Invalid);assert(q.select(1,5,5,2)==0);
    puts("map HUD lifecycle: ok");
}
