// dsweep_actors.h: the actor sweep's bookkeeping (issue #697): which first-drawn actors are the title's, when what an
// actor requested has come up, and how often an actor dropped for a title scene change goes again.
#include "dsweep_actors.h"
#include <cassert>
#include <cstdio>

int main() {
    using dsweep::Drawn;
    using dsweep::classify_drawn;
    // before the actor sweep: everything a title sweep sees drawn is the title's
    assert(classify_drawn(false, true, false, false) == Drawn::Title);
    assert(classify_drawn(false, true, true, false) == Drawn::Title);
    assert(classify_drawn(false, false, false, false) == Drawn::Neither);  // a stage sweep keeps no title actors
    // in the actor sweep: a known title actor stays the title's; a new one is the sweep's actor's ...
    assert(classify_drawn(true, true, false, true) == Drawn::Neither);
    assert(classify_drawn(true, true, false, false) == Drawn::Ours);
    // ... unless a title scene change is under way or settling (the re-created title's player: #697)
    assert(classify_drawn(true, true, true, false) == Drawn::Title);
    assert(classify_drawn(true, true, true, true) == Drawn::Neither);
    assert(classify_drawn(true, false, true, false) == Drawn::Ours);  // no title, no title scene change

    // the create queue: 2 queued by the title before the actor; the actor's 28 requests come and go
    dsweep::CreateWait w;
    w.start(2);
    assert(!w.settled());
    for (unsigned q : {3u, 30u, 25u, 10u}) w.frame(q), assert(!w.settled());
    w.frame(2);
    w.frame(1);
    assert(!w.settled());  // a few frames for what came up to draw
    w.frame(2);
    assert(w.settled());
    w.frame(3);  // something new queued: not settled again until it is gone
    assert(!w.settled());
    // an actor that requests nothing: settled after kQuietFrames
    dsweep::CreateWait none;
    none.start(0);
    for (unsigned i = 0; i < dsweep::CreateWait::kQuietFrames; ++i) assert(!none.settled()), none.frame(0);
    assert(none.settled());

    // dropped for a scene change: again up to kRetries times, then done
    dsweep::ActorRetries r;
    for (unsigned i = 0; i < dsweep::ActorRetries::kRetries; ++i) assert(r.again("Hsh2:000EF3FF:0:0"));
    assert(!r.again("Hsh2:000EF3FF:0:0"));
    assert(r.again("Htetu1:00000005:0:0"));
    std::puts("dsweep_actors: ok");
    return 0;
}
