// One-shot no-draw requests across the passes of a logic step (runtime/src/interp_nodraw.h,
// issue #126): a simulation of fopAc_Draw (honours fopAcStts_NODRAW_e, then clears it) driven by a
// pass loop with 0, 1, 3 or 7 in-between passes per step. Every pass must hide exactly the actors
// the step's logic hid, as the 30 fps game hides them on its one frame per step.
#include "../src/interp_nodraw.h"

#include <cstdio>
#include <cstdint>
#include <map>
#include <vector>

using interp::OneShotNoDraw;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

// the game side: actor status words, and fopAc_Draw's handling of the bit
struct Game {
    std::map<uint32_t, uint32_t> status;
    // fopAc_Draw: drawn unless the bit is set; the bit is cleared either way
    bool draw(uint32_t actor, OneShotNoDraw* book, bool hold, uint64_t step) {
        uint32_t& s = status[actor];
        if (book) s = book->on_draw(actor, s, hold, step);
        const bool drawn = !(s & OneShotNoDraw::kNoDraw);
        s &= ~OneShotNoDraw::kNoDraw;
        return drawn;
    }
};

constexpr uint32_t kLink = 0x40001000, kNpc = 0x40002000, kOther = 0x40003000;

// which actors a step's logic hides (the talk camera's shoulder shot hides the listener every step)
static std::vector<uint32_t> hidden_by_logic(uint64_t step) {
    std::vector<uint32_t> v;
    if (step >= 10 && step < 20) v.push_back(kLink);  // a conversation shot over the NPC's shoulder
    if (step >= 15 && step < 25) v.push_back(kNpc);   // then over Link's
    if (step % 7 == 3) v.push_back(kOther);           // a single-step request
    return v;
}

// runs steps with `in_between` hold passes each; returns per (step, pass, actor) whether it was drawn
static void run(int in_between, bool with_book, std::map<uint64_t, std::vector<std::map<uint32_t, bool>>>& out) {
    Game g;
    OneShotNoDraw book;
    for (uint64_t step = 1; step <= 30; step++) {
        for (uint32_t a : hidden_by_logic(step)) g.status[a] |= OneShotNoDraw::kNoDraw;  // logic (execute)
        for (int pass = 0; pass <= in_between; pass++) {
            const bool hold = pass > 0;
            std::map<uint32_t, bool> drawn;
            for (uint32_t a : {kLink, kNpc, kOther}) drawn[a] = g.draw(a, with_book ? &book : nullptr, hold, step);
            out[step].push_back(drawn);
        }
    }
}

static void test_passes_match_logic() {
    for (int n : {0, 1, 3, 7}) {
        std::map<uint64_t, std::vector<std::map<uint32_t, bool>>> fixed, before;
        run(n, true, fixed);
        run(n, false, before);
        bool flickered_before = false;
        for (auto& [step, passes] : fixed) {
            const auto hidden = hidden_by_logic(step);
            for (uint32_t a : {kLink, kNpc, kOther}) {
                bool want = true;
                for (uint32_t h : hidden) want = want && h != a;
                for (size_t p = 0; p < passes.size(); p++) CHECK(passes[p].at(a) == want);
                // without the bookkeeping the logic pass hides it and the in-between passes draw it
                CHECK(before[step][0].at(a) == want);
                for (size_t p = 1; p < before[step].size(); p++)
                    if (before[step][p].at(a) != want) flickered_before = true;
            }
        }
        CHECK(flickered_before == (n > 0));
    }
}

// a hold pass keeps a request made by its own logic (true 60's 60 Hz processes), and adds none for
// actors the logic pass drew
static void test_hold_pass_requests() {
    OneShotNoDraw book;
    CHECK(book.on_draw(kLink, 0, false, 5) == 0);
    CHECK(book.on_draw(kLink, OneShotNoDraw::kNoDraw, true, 5) == OneShotNoDraw::kNoDraw);
    CHECK(book.on_draw(kLink, 0, true, 5) == 0);
    CHECK(book.on_draw(kNpc, 0x20, true, 5) == 0x20);  // other status bits untouched
}

// a request is for its own step only: not carried to the next step's passes, or after a reset
static void test_step_scope() {
    OneShotNoDraw book;
    CHECK(book.on_draw(kLink, OneShotNoDraw::kNoDraw | 0x4, false, 8) == (OneShotNoDraw::kNoDraw | 0x4));
    CHECK(book.on_draw(kLink, 0x4, true, 8) == (OneShotNoDraw::kNoDraw | 0x4));
    CHECK(book.hidden_count() == 1);
    // hold passes of a later step (its logic pass not seen, e.g. drawn without a step change)
    CHECK(book.on_draw(kLink, 0, true, 9) == 0);
    // the next logic pass forgets the previous step
    CHECK(book.on_draw(kNpc, 0, false, 9) == 0);
    CHECK(book.hidden_count() == 0);
    CHECK(book.on_draw(kLink, 0, true, 9) == 0);
    // the same actor drawn twice on a logic pass is recorded once
    book.on_draw(kLink, OneShotNoDraw::kNoDraw, false, 10);
    book.on_draw(kLink, OneShotNoDraw::kNoDraw, false, 10);
    CHECK(book.hidden_count() == 1);
    book.reset();
    CHECK(book.hidden_count() == 0);
    CHECK(book.on_draw(kLink, 0, true, 10) == 0);
}

// Issue #283: an actor the step's logic pass did not draw is saved before a hold pass draws it and
// put back at the start of the next pass (interp.cpp actor_keep / actors_put_back). The draw hook
// makes the step's request again on that pass; saved after that, the put-back handed the request
// to the next logic pass, which then counted as that step's logic asking, and the actor stayed
// hidden after the logic stopped hiding it (Link after a conversation). Simulated with the actor's
// status (the bit) and condition (fopAcCnd_NODRAW: not drawn on the last draw, kept on hold passes).
static std::vector<std::vector<bool>> run_kept(int in_between, bool save_first) {
    constexpr uint32_t kNotDrawn = 0x04;
    OneShotNoDraw book;
    uint32_t status = 0, condition = 0;
    bool saved = false;
    uint32_t saved_status = 0;
    std::vector<std::vector<bool>> drawn;  // per step, per pass
    for (uint64_t step = 1; step <= 40; step++) {
        drawn.emplace_back();
        for (int pass = 0; pass <= in_between; pass++) {
            if (saved) status = saved_status, saved = false;  // the put-back at the start of the pass
            const bool hold = pass > 0;
            if (!hold && step >= 10 && step < 20) status |= OneShotNoDraw::kNoDraw;  // the talk camera
            const bool kept = hold && (condition & kNotDrawn);
            auto keep = [&] { saved = true, saved_status = status; };
            if (save_first) {
                status = book.before_draw(kLink, status, hold, step, true, kept, keep);
            } else {  // the order before the fix: the request first, then the save
                status = book.on_draw(kLink, status, hold, step);
                if (kept) keep();
            }
            const uint32_t logic_condition = condition;
            const bool d = !(status & OneShotNoDraw::kNoDraw);  // fopAc_Draw
            condition = d ? condition & ~kNotDrawn : condition | kNotDrawn;
            status &= ~OneShotNoDraw::kNoDraw;
            if (hold) condition = logic_condition;  // (hold passes keep the logic pass's condition)
            drawn.back().push_back(d);
        }
    }
    return drawn;
}

static void test_kept_actor_put_back() {
    for (int n : {0, 1, 3, 7}) {
        const auto fixed = run_kept(n, true), before = run_kept(n, false);
        for (size_t i = 0; i < fixed.size(); i++) {
            const uint64_t step = i + 1;
            const bool want = !(step >= 10 && step < 20);
            for (bool d : fixed[i]) CHECK(d == want);
        }
        // the order before the fix: hidden for good after the conversation once there are hold passes
        CHECK(before.back()[0] == (n == 0));
    }
}

int main() {
    test_passes_match_logic();
    test_kept_actor_put_back();
    test_hold_pass_requests();
    test_step_scope();
    if (g_failed) {
        std::fprintf(stderr, "interp_nodraw: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("interp_nodraw: ok\n");
    return 0;
}
