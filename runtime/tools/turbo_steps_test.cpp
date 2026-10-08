// Quick doors' extra logic steps against the game's process deletion (runtime/src/mods/turbo_steps.h,
// issue #61): a model of fpcDtTg_ToDeleteQ / fpcDtTg_Do (delete timer 1) and of the draw buffers
// (packets a process's draw enters stay listed until the next draw's frameInit). For every step of a
// door event in which a process can queue itself for deletion, and every number of extra steps, no
// process may be deleted while its packets are still listed. The schedule that also ran
// fpcDt_Handler in the extra steps must fail the same check (the crash of issue #61).
#include "../src/mods/turbo_steps.h"

#include <cstdio>
#include <vector>

using mods::StepParts;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

namespace {
struct Proc {
    bool queued = false, deleted = false, entered = false;
    int timer = 0;
    int queue_frame = -1, queue_step = -1;  // when its execute queues it for deletion
    int deleted_frame = -1;
};

struct Result {
    int deleted_while_entered = 0;
    int not_deleted = 0;
    int min_frames_to_delete = 1 << 30;
};

// frames 0..frames-1; frame f runs the normal step and then `extra` extra steps of the given kind
// while door_from <= f < door_to; then draws
Result run(const StepParts& extra_kind, int extra, int door_from, int door_to, int frames, std::vector<Proc>& ps) {
    Result r;
    auto step = [&](const StepParts& parts, int frame, int index) {
        if (parts.deletion)
            for (auto& p : ps) {
                if (!p.queued || p.deleted) continue;
                if (p.timer > 0) {
                    p.timer--;  // fpcDtTg_Do
                    continue;
                }
                p.deleted = true;  // fpcDt_deleteMethod -> heap destroy -> ~J3DPacket
                p.deleted_frame = frame;
                if (p.entered) r.deleted_while_entered++;
            }
        if (parts.execute)
            for (auto& p : ps)
                if (!p.queued && p.queue_frame == frame && p.queue_step == index) {
                    p.queued = true;  // fopAcM_delete -> fpcDtTg_ToDeleteQ: timer 1, off the draw list
                    p.timer = 1;
                }
    };
    for (int f = 0; f < frames; f++) {
        step(mods::kNormalStep, f, 0);
        if (f >= door_from && f < door_to)
            for (int i = 1; i <= extra; i++) step(extra_kind, f, i);
        // draw: frameInit clears the buffers, then every drawn process enters its packets
        for (auto& p : ps) p.entered = false;
        for (auto& p : ps)
            if (!p.queued && !p.deleted) p.entered = true;
    }
    for (auto& p : ps) {
        if (!p.deleted) r.not_deleted++;
        else if (p.deleted_frame - p.queue_frame < r.min_frames_to_delete)
            r.min_frames_to_delete = p.deleted_frame - p.queue_frame;
    }
    return r;
}

// one process per (frame of the door event, step in that frame) that queues itself there
Result door_event(const StepParts& extra_kind, int extra) {
    const int door_from = 5, door_to = 9, frames = 20;
    std::vector<Proc> ps;
    for (int f = door_from - 1; f <= door_to; f++)
        for (int s = 0; s <= (f >= door_from && f < door_to ? extra : 0); s++) {
            Proc p;
            p.queue_frame = f;
            p.queue_step = s;
            ps.push_back(p);
        }
    return run(extra_kind, extra, door_from, door_to, frames, ps);
}
}  // namespace

int main() {
    // the game without extra steps: deleted two frames after queueing, never while listed
    {
        std::vector<Proc> ps(1);
        ps[0].queue_frame = 3;
        ps[0].queue_step = 0;
        Result r = run(mods::kDoorExtraStep, 0, 0, 0, 10, ps);
        CHECK(r.deleted_while_entered == 0);
        CHECK(r.not_deleted == 0);
        CHECK(r.min_frames_to_delete == 2);
    }
    // quick doors (turbo.cpp runs 3 extra steps; also other WWHD_MOD_DOOR_EXTRA values)
    for (int extra = 1; extra <= 7; extra++) {
        Result r = door_event(mods::kDoorExtraStep, extra);
        CHECK(r.deleted_while_entered == 0);
        CHECK(r.not_deleted == 0);
        CHECK(r.min_frames_to_delete == 2);  // as in the game: counts frames, not steps
    }
    // the old schedule (fpcDt_Handler in every extra step) deletes listed processes (issue #61)
    StepParts old = mods::kDoorExtraStep;
    old.deletion = true;
    CHECK(door_event(old, 3).deleted_while_entered > 0);
    CHECK(door_event(old, 2).deleted_while_entered > 0);
    CHECK(!mods::kDoorExtraStep.deletion);
    if (g_failed) {
        std::fprintf(stderr, "turbo_steps: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("turbo_steps: ok\n");
    return 0;
}
