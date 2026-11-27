// D-sweep (dsweep.cpp): the actor sweep's bookkeeping that needs no game (issue #697).
//
// The actor sweep runs in the title's scene. Two things it did not know about stopped setup's sweep:
//
// - The title's idle story intro (dsweep_scene.h, about every 2200 logic steps) re-creates the title's actors, among
//   them its player (profile 0xA8), which the camera keeps (dCamera_c::mpPlayerActor; bumpCheck calls its
//   getGrabActorID every step). The sweep took an actor first drawn during the actor phase whose address it had not
//   seen for one its actor had made ("ours"), so a re-created title actor that came up at a new address while an
//   actor was shown was deleted with it. The player: "indirect branch to unknown address 00300000 (lr=024FD254)"
//   (the deleted player's base fopAc_ac_c vtable, slot 0xBC), blamed on whichever actor was shown (Hsh2 or Htetu1,
//   #697; Htobi3, #682). Or the intro never finished (its overlap request stayed up), and the first stage visit
//   after the actor sweep waited for it for good: a 60 s stall blamed on the last actor of the list (woodbx, #697).
//   Now no actor is shown across a title scene change: the one in hand is dropped and tried again after it (at most
//   ActorRetries::kRetries times), no new one starts until it has settled, and what is first drawn meanwhile is the
//   title's (kept by process id, never by address).
// - Actors that request others in their create (Bkm, Kalle Demos: 20 hands and 8 feet by fpcSCtRq_Request) were
//   deleted before what they requested came up, and only drawn actors were deleted with them. A hand left over
//   executes without its boss (daBmdhand_Execute: boss null, a read of 0x90): the next actor in the list got the
//   blame (Bms2, #697). Now an actor is shown until the game's create queue (g_fpcCtTg_Queue) is back to
//   what it held before the actor was requested, for kQuietFrames frames; every actor made since then (process id
//   from the shown actor's on) that is not the title's is deleted with it, drawn or not; deleted actors are held
//   still until their delete completes, and one still being created is deleted once it exists (its delete during its
//   resource load stops the game), or its create aborted after a while (the auction's people, Auc0-7, wait for their
//   manager for good, and a title scene change waits for the creates of its layer). WWHD_DSWEEP_ACTOR_CHILDREN=0
//   keeps the old way (drawn actors only, no wait); WWHD_DSWEEP_SCENE_GUARD=0 shows actors across scene changes.
#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace dsweep {

// an actor first drawn now: the title's (kept; never held still or deleted) or one the sweep's actor made
enum class Drawn { Title, Ours, Neither };
inline Drawn classify_drawn(bool actor_phase, bool title_sweep, bool scene_held, bool known_title_actor) {
    if (!actor_phase) return title_sweep ? Drawn::Title : Drawn::Neither;
    if (known_title_actor) return Drawn::Neither;
    // during a title scene change (and its settle time) the sweep shows no actor: what comes up is the title's
    return scene_held && title_sweep ? Drawn::Title : Drawn::Ours;
}

// whether what an actor requested has come up: the create queue back to its size before the actor was created
// (what the title had queued then is not the actor's), for kQuietFrames frames in a row
struct CreateWait {
    static constexpr unsigned kQuietFrames = 3;
    uint32_t before = 0;  // the queue's size when the actor was requested
    unsigned quiet = 0;   // frames in a row with no more than that
    void start(uint32_t queued) { before = queued, quiet = 0; }
    void frame(uint32_t queued) { quiet = queued <= before ? quiet + 1 : 0; }
    bool settled() const { return quiet >= kQuietFrames; }
};

// an actor dropped for a scene change goes again after it, at most kRetries times; then it counts as done
struct ActorRetries {
    static constexpr unsigned kRetries = 3;
    std::map<std::string, unsigned> count;
    bool again(const std::string& id) { return ++count[id] <= kRetries; }
};

}  // namespace dsweep
