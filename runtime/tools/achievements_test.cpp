// SPDX-License-Identifier: MPL-2.0
// Achievements (runtime/src/mods/achievements_goals.h, achievements_store.h): the goals on save images built here,
// the earned record (for all save games and for each), the two-read rule, a new file in a slot, the pause by cheats.
// With WWHD_PROGRESS_SAVES=<cking.sav,...> it prints which goals each real Quest Log meets (the evidence for the
// goals marked kSaves: the finished test save meets them, new files don't).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "mods/achievements_goals.h"
#include "mods/achievements_store.h"

using namespace achievements;

namespace {
unsigned char d[GP_SAVE_LEN];
void fresh() {
    memset(d, 0, sizeof d);
    memset(d + GP_ITEMS, 0xFF, 21);
    d[GP_MAX_LIFE + 1] = 12;
    d[GP_STAGE_NO] = 0xFF;
}
const Goal* goal(const char* key) {
    for (const Goal& g : kAll)
        if (!strcmp(g.key, key)) return &g;
    return nullptr;
}
bool met(const char* key, const Read* prev = nullptr) {
    Read r;
    r.d = d, r.prev = prev;
    return goal(key)->met(r);
}
}  // namespace

int main() {
    // the table: 99 goals (the fork's 105 less the 6 unconfirmed), unique keys, points as the fork's
    assert(kGoals == 99);
    std::set<std::string> keys;
    for (const Goal& g : kAll) assert(keys.insert(g.key).second && g.points > 0 && g.group < kGroups);
    for (const Unconfirmed& u : kUnconfirmed) assert(!goal(u.key) && keys.insert(u.key).second);
    assert(keys.size() == 105);
    // a new file meets none (but the sea's two squares the game starts with)
    fresh();
    {
        Read r;
        r.d = d;
        for (const Goal& g : kAll) assert(!g.met(r));
    }
    // examples of each kind of check
    d[GP_PEARLS] = 2;
    assert(met("pearl_din") && !met("pearl_nayru"));
    d[GP_EVENTS + 0x13] = 0x40;
    assert(met("killer_bees"));
    d[GP_EVENTS + 0xD0] = 3;
    assert(met("orca_500") && !met("orca_1000"));
    d[GP_STAGES + 0 * GP_STAGE_SIZE + 0x0A] = 0x10;
    assert(met("set_sail"));
    d[GP_STAGES + 3 * GP_STAGE_SIZE + 0] = 0x00, d[GP_STAGES + 3 * GP_STAGE_SIZE + 2] = 0xFC, d[GP_STAGES + 3 * GP_STAGE_SIZE + 3] = 0xDF;
    assert(met("chests_drc"));
    d[GP_STAGE_NO] = 3, d[GP_STAGE_COPY + 3] = 0x20;  // (the working copy of the stage in play counts too)
    d[GP_STAGES + 3 * GP_STAGE_SIZE + 3] = 0xDF & ~0x20;
    assert(met("chests_drc"));
    d[GP_STAGE_NO] = 0xFF;
    d[GP_INFO + 0x58] = 1;
    assert(met("the_end") && !met("the_end_hero"));
    {
        Read r;
        r.d = d, r.hero = true;
        assert(goal("the_end_hero")->met(r) && goal("hero_mode")->met(r));
    }
    // the trees: all eight within 20 minutes of the first, timed from a change seen (a file loaded with some watered
    // doesn't count)
    {
        int64_t from = 0;
        unsigned char before[GP_SAVE_LEN];
        fresh();
        memcpy(before, d, sizeof d);
        Read p, r;
        p.d = before, p.trees_from = &from, p.now_ms = 1000;
        d[GP_EVENTS + 0x9E] = 0x01;
        r.d = d, r.prev = &p, r.trees_from = &from, r.now_ms = 2000;
        assert(!goal("trees_fast")->met(r) && from == 2000);
        memcpy(before, d, sizeof d);
        d[GP_EVENTS + 0x9E] = 0xFF;
        r.now_ms = 2000 + 19 * 60000;
        assert(goal("trees_fast")->met(r));
        r.now_ms = 2000 + 21 * 60000;
        assert(!goal("trees_fast")->met(r));
        int64_t none = 0;  // (loaded with all watered: no start seen)
        r.trees_from = &none, p.trees_from = &none;
        assert(!goal("trees_fast")->met(r));
    }

    // the record: two reads in a row, for all save games and for this one; a new file forgets its slot's
    {
        Store st;
        Checker ch;
        Options opt;
        fresh();
        d[GP_ITEMS + 0] = 0x20;  // (no goal: the telescope isn't one)
        d[GP_ITEMS + 2] = 0x22;  // the Wind Waker
        Earned e = ch.check(d, "", false, "1:Link", 0, 1000, st, opt, {});
        assert(e.keys.empty());                 // (the first read)
        e = ch.check(d, "", false, "1:Link", 0, 2000, st, opt, {});
        assert(e.keys.size() == 1 && e.keys[0] == "wind_waker" && e.new_for_all.size() == 1);
        assert(st.earned_all("wind_waker") == 2000 && st.earned_file("1:Link", "wind_waker") == 2000);
        e = ch.check(d, "", false, "1:Link", 0, 3000, st, opt, {});
        assert(e.keys.empty());                 // (kept, not again)
        // another save game has it too: earned for it, not new for all
        e = ch.check(d, "", false, "2:Tetra", 0, 4000, st, opt, {});
        e = ch.check(d, "", false, "2:Tetra", 0, 5000, st, opt, {});
        assert(e.keys.size() == 1 && e.new_for_all.empty() && st.earned_file("2:Tetra", "wind_waker") == 5000);
        // a new game started in slot 1: two reads with nothing met forget slot 1's record (not the one for all)
        fresh();
        ch.check(d, "", false, "1:Link", 0, 6000, st, opt, {});
        ch.check(d, "", false, "1:Link", 0, 7000, st, opt, {});
        assert(!st.earned_file("1:Link", "wind_waker") && st.earned_all("wind_waker") == 2000);
        // a state from before it was earned: it stays earned
        assert(st.earned_file("2:Tetra", "wind_waker"));
        // paused by a cheat: nothing earned, the reads start over
        fresh();
        d[GP_ITEMS + 5] = 0x2D;  // the boomerang
        e = ch.check(d, "", false, "2:Tetra", 0, 8000, st, opt, {"Infinite health"});
        e = ch.check(d, "", false, "2:Tetra", 0, 9000, st, opt, {"Infinite health"});
        assert(e.keys.empty() && e.paused && !st.earned_all("boomerang"));
        opt.with_cheats = true;  // "Earn with cheats"
        e = ch.check(d, "", false, "2:Tetra", 0, 10000, st, opt, {"Infinite health"});
        e = ch.check(d, "", false, "2:Tetra", 0, 11000, st, opt, {"Infinite health"});
        assert(e.keys.size() == 1 && e.keys[0] == "boomerang");
        // the record as text and back
        Store back;
        assert(back.parse(st.text()));
        assert(back.earned_all("boomerang") == 11000 && back.earned_file("2:Tetra", "wind_waker") == 5000);
        assert(!back.parse("{ not json"));
    }

    if (const char* saves = getenv("WWHD_PROGRESS_SAVES")) {
        std::string list = saves;
        for (size_t at = 0; at <= list.size();) {
            const size_t comma = list.find(',', at);
            const std::string path = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            at = comma == std::string::npos ? list.size() + 1 : comma + 1;
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) continue;
            std::vector<unsigned char> b(8966);
            const size_t n = fread(b.data(), 1, b.size(), f);
            fclose(f);
            for (unsigned slot = 0; slot < 3 && n >= 0x2000; slot++) {
                gp_card_to_memory(&b[slot * 0xA94], d);
                Read r;
                r.d = d, r.hero = b[0x1FBC + slot * 16 + 6] == 1;  // (cking.sav: the HD player record's byte 6)
                unsigned count = 0;
                std::string which;
                for (const Goal& g : kAll)
                    if (!g.once && g.met(r)) count++, which += std::string(" ") + g.key;
                printf("%s Quest Log %u%s: %u goals:%s\n", path.c_str(), slot + 1, r.hero ? " (Hero Mode)" : "", count, which.c_str());
            }
        }
    }
    puts("achievements: ok");
    return 0;
}
