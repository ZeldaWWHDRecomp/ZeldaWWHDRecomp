// SPDX-License-Identifier: MPL-2.0
// Online co-op's looks: which tinted copy of a Link texture a puppet's look is drawn from (runtime/src/mods/coop_looks.h,
// the pure part of coop_pose.cpp's worn()). A model of the cache as worn() keeps it: copies tinted for a look, at most
// kLooks, reused when no shown puppet wears their look any more.
#include <cstdio>
#include <vector>

#include "mods/coop_looks.h"

using namespace mods::coop;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

using Key = std::vector<uint8_t>;
static Key look(int n) { return n < 0 ? Key{1, 0, 0} : Key{1, 1, (uint8_t)n, (uint8_t)(n >> 8)}; }  // (look(-1): the game's colours, as encode({}) gives it)
static const Key kGame = look(-1);
constexpr size_t kLooks = 8;
constexpr int kPuppets = 31;

struct Copy {
    Key key;            // the look it is tinted in
    uint32_t obj = 0;   // its texture object
    uint64_t used = 0;
};

// one texture's copies, and the puppets: what worn() does with a pick
struct Cache {
    std::vector<Copy> copies;
    uint64_t clock = 0;
    uint32_t next_obj = 0x1000;
    int made = 0;  // copies made (new) or tinted again
    bool shown[kPuppets] = {};
    Key wears[kPuppets];

    bool worn_now(const Key& k) const {
        for (int i = 0; i < kPuppets; i++)
            if (shown[i] && wears[i] == k) return true;
        return false;
    }
    // the copy drawn for a look: its texture object, and the look it really shows
    const Copy& draw(const Key& k) {
        const looks::Choice c = looks::pick(copies, k, kLooks, [&](const Key& x) { return worn_now(x); }, kGame);
        if (c.pick == looks::Pick::kNew) {
            CHECK(c.index == copies.size());
            copies.push_back({});
            copies.back().obj = next_obj++;
        }
        Copy& w = copies[c.index];
        if (c.pick == looks::Pick::kNew || c.pick == looks::Pick::kRetint) w.key = k, made++;
        w.used = ++clock;
        return w;
    }
    // a frame: every shown puppet's model draws its look
    void frame() {
        for (int i = 0; i < kPuppets; i++)
            if (shown[i]) draw(wears[i]);
    }
};

int main() {
    // one puppet cycling through 20 looks (the colour page's live preview: each step a new look) always shows its latest
    {
        Cache c;
        c.shown[0] = true;
        for (int n = 0; n < 20; n++) {
            c.wears[0] = look(n);
            c.frame();
            CHECK(c.draw(c.wears[0]).key == look(n));
            CHECK(c.copies.size() <= kLooks);
        }
        CHECK(c.copies.size() == kLooks);  // (memory bounded: the cap, no more)
        CHECK(c.made == 20);                // (each look tinted once, a frame's draws after the first reuse it)
        // back to a look still in a copy: no new tint
        const int before = c.made;
        c.wears[0] = look(19);
        c.frame();
        CHECK(c.made == before);
    }

    // the least recently drawn copy is the one tinted again
    {
        Cache c;
        c.shown[0] = true;
        for (int n = 0; n < (int)kLooks; n++) c.wears[0] = look(n), c.frame();
        c.wears[0] = look(0), c.frame();  // (look 0 drawn again: look 1 is now the oldest)
        c.wears[0] = look(100), c.frame();
        bool has0 = false, has1 = false;
        for (const Copy& x : c.copies) has0 |= x.key == look(0), has1 |= x.key == look(1);
        CHECK(has0 && !has1);
    }

    // 8 puppets in 8 looks fill the copies; a 9th look shows the game's colours (none free), and the 8 keep theirs
    {
        Cache c;
        for (int i = 0; i < (int)kLooks; i++) c.shown[i] = true, c.wears[i] = look(10 + i);
        c.frame();
        CHECK(c.copies.size() == kLooks);
        c.shown[8] = true, c.wears[8] = look(50);
        c.frame();
        CHECK(c.copies.size() == kLooks);
        for (int i = 0; i < (int)kLooks; i++) CHECK(c.draw(c.wears[i]).key == look(10 + i));
        const Copy& f = c.draw(look(50));
        CHECK(f.key != look(50));
        CHECK(f.obj == c.copies[0].obj);  // (no copy in the game's colours among them: the first, as before)

        // with one of the 8 in the game's colours, the 9th look is drawn from that copy
        Cache g;
        for (int i = 0; i < (int)kLooks; i++) g.shown[i] = true, g.wears[i] = i == 3 ? kGame : look(10 + i);
        g.frame();
        g.shown[8] = true, g.wears[8] = look(50);
        CHECK(g.draw(look(50)).key == kGame);

        // a puppet leaves: its copy is free, and the 9th player's look gets it
        c.shown[2] = false;
        CHECK(c.draw(look(50)).key == look(50));
        for (int i = 0; i < (int)kLooks; i++)
            if (i != 2) CHECK(c.draw(c.wears[i]).key == look(10 + i));
        CHECK(c.copies.size() == kLooks);

        // a puppet whose look changes stops holding its old copy: all 8 copies are worn (looks 10, 11, 50, 13..17);
        // puppet 0 changes to look 60 and gets its own old copy (look 10) tinted again
        const uint32_t obj10 = c.draw(look(10)).obj;
        c.wears[0] = look(60);
        c.frame();
        CHECK(c.draw(look(60)).key == look(60) && c.draw(look(60)).obj == obj10);
        // a 10th player's look: all 8 worn again, so the fallback; once puppet 1 leaves, look 11's copy is its
        c.shown[9] = true, c.wears[9] = look(61);
        CHECK(c.draw(look(61)).key != look(61));
        c.shown[1] = false;
        CHECK(c.draw(look(61)).key == look(61));
        for (int i = 0; i < 10; i++)
            if (c.shown[i]) CHECK(c.draw(c.wears[i]).key == c.wears[i]);
        CHECK(c.copies.size() == kLooks);
    }

    // copies made for a texture found before (their keys cleared) are used before any new one
    {
        std::vector<Copy> v(3);
        for (size_t i = 0; i < v.size(); i++) v[i].obj = 0x100 + (uint32_t)i, v[i].key = look((int)i);
        v[1].key.clear();
        const auto ch = looks::pick(v, look(9), kLooks, [](const Key&) { return true; }, kGame);
        CHECK(ch.pick == looks::Pick::kRetint && ch.index == 1);
    }

    std::printf(g_fail ? "coop_looks: %d failed\n" : "coop_looks: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
