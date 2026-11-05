// SPDX-License-Identifier: MPL-2.0
// Game progress (runtime/src/mods/game_progress.h): the count of a save on byte images built here from the known
// offsets: empty, full, each part alone, the stage's working copy, the 99 % cap, the cking.sav block unpacking.
// With WWHD_PROGRESS_SAVES=<cking.sav>[,<cking.sav>...] it also prints the count of each Quest Log in those files (a
// look at real saves on this machine; CI has none).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mods/game_progress.h"
#include "mods/game_progress_places.h"

namespace {
unsigned char d[GP_SAVE_LEN];

void empty() {
    memset(d, 0, sizeof d);
    memset(d + GP_ITEMS, 0xFF, 21);  // (empty slots are 0xFF)
    d[GP_MAX_LIFE + 1] = 12;         // three hearts
    d[GP_STAGE_NO] = 0xFF;           // (no stage's working copy)
}

void set_bit(unsigned o, unsigned b) { d[o] |= (unsigned char)(1u << b); }
void set_chart(unsigned set, unsigned n) {
    unsigned i = n - 1, o = GP_MAP + set * 16 + (i >> 5) * 4 + 3 - ((i & 31) >> 3);
    d[o] |= (unsigned char)(1u << (i & 7));
}

void fill_dungeons() {
    for (int st = 2; st <= 7; st++) d[GP_STAGES + st * GP_STAGE_SIZE + GP_DUNGEON_ITEM] = st == 2 ? 0x1B : 0x1F;
}
void fill_hearts() { d[GP_MAX_LIFE + 1] = (unsigned char)(12 + 44 + 4 * 6); }  // 20 hearts: 80 quarters
void fill_items() {
    static const unsigned char have[21] = {0x20, 0x77, 0x22, 0x25, 0x24, 0x2D, 0x34, 0x21, 0x26, 0x29, 0x2A,
                                           0x2C, 0x36, 0x31, 0x50, 0x50, 0x50, 0x50, 0x30, 0x2F, 0x33};
    memcpy(d + GP_ITEMS, have, 21);
}
void fill_equipment() {
    d[GP_COLLECT] = 0x0F;
    d[GP_COLLECT + 1] = 0x03;
    d[GP_COLLECT + 2] = d[GP_COLLECT + 3] = d[GP_COLLECT + 4] = 1;
}
void fill_upgrades() {
    d[GP_WALLET] = 2;
    d[GP_BOMB_MAX] = d[GP_ARROW_MAX] = 99;
    d[GP_MAX_MAGIC] = 32;
}
void fill_songs() { d[GP_TACT] = 0x3F; }
void fill_quest() {
    d[GP_PEARLS] = 7;
    d[GP_TRIFORCE] = 0xFF;
}
void fill_charts() {
    for (unsigned n = 1; n <= 51; n++)
        if (n != 35 && n != 36) set_chart(1, n), set_chart(3, n);
}
void fill_sea() {
    for (unsigned n = 0; n < 49; n++) d[GP_FMAP + n] = 1;
}
void fill_gallery() {
    for (unsigned f = 0; f < GP_FIGURINES; f++) set_bit(GP_EVENTS + gp_figure_bytes[f / 8], f % 8);
}
void (*const fills[GP_PARTS])() = {fill_dungeons, fill_hearts, fill_items, fill_equipment, fill_upgrades,
                                   fill_songs, fill_quest, fill_charts, fill_sea, fill_gallery};

gp_result r;

void count() { gp_count(d, &r); }
}  // namespace

int main() {
    static const unsigned totals[GP_PARTS] = {29, 44, 25, 9, 7, 6, 11, 98, 49, 134};
    empty();
    count();
    assert(gp_percent(&r) == 0);
    for (int p = 0; p < GP_PARTS; p++) {
        assert(r.done[p] == 0);
        assert(r.total[p] == totals[p]);
        assert(r.first[p] < r.first[p + 1]);
    }
    // everything
    for (auto f : fills) f();
    count();
    for (int p = 0; p < GP_PARTS; p++) assert(r.done[p] == r.total[p]);
    assert(gp_percent(&r) == 100);
    // each part alone: 10 %
    for (int p = 0; p < GP_PARTS; p++) {
        empty();
        fills[p]();
        count();
        // (the hearts part alone: the pieces count needs the containers, else 24 more quarters read as pieces)
        if (p == GP_PART_HEARTS) assert(r.done[p] == 44);
        assert(r.done[p] == r.total[p]);
        for (int q = 0; q < GP_PARTS; q++)
            if (q != p) assert(r.done[q] == 0);
        assert(gp_percent(&r) == 10);
    }
    // all but one figurine: 99, not 100 (99.9 floored, and never 100 with something missing)
    empty();
    for (auto f : fills) f();
    d[GP_EVENTS + gp_figure_bytes[0]] &= 0xFE;
    count();
    assert(r.done[GP_PART_GALLERY] == GP_FIGURINES - 1);
    assert(gp_percent(&r) == 99);
    // a Heart Container taken is not a piece: 3 hearts + one container = 0 pieces
    empty();
    d[GP_STAGES + 3 * GP_STAGE_SIZE + GP_DUNGEON_ITEM] = 0x10;
    d[GP_MAX_LIFE + 1] = 16;
    count();
    assert(r.done[GP_PART_HEARTS] == 0);
    assert(r.done[GP_PART_DUNGEONS] == 1);
    // the stage in play: its working copy counts (Dragon Roost Cavern = table entry 3, map + compass got there)
    empty();
    d[GP_STAGE_NO] = 3;
    d[GP_STAGE_COPY + GP_DUNGEON_ITEM] = 0x03;
    count();
    assert(r.done[GP_PART_DUNGEONS] == 2);
    {
        const gp_entry& e = r.e[r.first[GP_PART_DUNGEONS] + 1];
        assert(e.kind == GP_E_DUNGEON && e.arg == 2 && e.got_mask == 0x03 && e.parts_mask == 0x1F);
    }
    // ... but not another stage's copy
    d[GP_STAGE_NO] = 9;
    count();
    assert(r.done[GP_PART_DUNGEONS] == 0);
    // the Forsaken Fortress has no Big Key: bit 2 there doesn't count
    empty();
    d[GP_STAGES + 2 * GP_STAGE_SIZE + GP_DUNGEON_ITEM] = 0x04;
    count();
    assert(r.done[GP_PART_DUNGEONS] == 0 && r.e[r.first[GP_PART_DUNGEONS]].total == 4);
    // charts 35 and 36 are not counted
    empty();
    set_chart(1, 35);
    set_chart(1, 36);
    count();
    assert(r.done[GP_PART_CHARTS] == 0);
    set_chart(1, 1);
    set_chart(1, 51);
    count();
    assert(r.done[GP_PART_CHARTS] == 2);
    // the bow's arrows: Light Arrows by the bow's item (0x36) or its got bit
    empty();
    d[GP_ITEMS + 12] = 0x27;
    d[GP_GOT_ITEMS + 12] = 4;
    count();
    assert(r.done[GP_PART_ITEMS] == 2);  // the bow and the Light Arrows
    // the places: a full save finds every place but the weak ones only by count; an empty one none
    {
        unsigned char st[GP_PLACES];
        empty();
        gp_place_states(d, st);
        for (unsigned i = 0; i < GP_PLACES; i++) assert(st[i] == GP_MISSING || gp_places[i].flag == GP_F_SQUARE);
        assert(GP_PLACES == 279);
        unsigned per_kind[GP_KINDS] = {};
        for (const gp_place& pl : gp_places) {
            per_kind[pl.kind]++;
            assert(pl.square <= 49 && pl.kind < GP_KINDS);
        }
        assert(per_kind[GP_K_HEART] == 44 && per_kind[GP_K_CONTAINER] == 6 && per_kind[GP_K_SQUARE] == 49);
        assert(per_kind[GP_K_CHART] == 49 && per_kind[GP_K_SALVAGE] == 49 && per_kind[GP_K_SHARD] == 8);
        // a weak heart: found "maybe" when the pieces leave one over
        empty();
        d[GP_MAX_LIFE + 1] = 13;  // one piece, no sure flag set
        gp_place_states(d, st);
        unsigned maybe = 0;
        for (unsigned i = 0; i < GP_PLACES; i++) maybe += st[i] == GP_MAYBE && gp_places[i].weak;
        assert(maybe == 1);
        // the bottles: two in the bag, the submarine's chest opened: the submarine's and the first in story order
        empty();
        d[GP_ITEMS + 14] = d[GP_ITEMS + 15] = 0x50;
        d[GP_STAGES + 10 * GP_STAGE_SIZE + 3] |= 4;  // table 10 mTbox bit 2
        gp_place_states(d, st);
        unsigned found = 0;
        for (unsigned i = 0; i < GP_PLACES; i++)
            if (gp_places[i].flag == GP_F_BOTTLE && st[i] == GP_FOUND) found++, assert(gp_places[i].a == 0 || gp_places[i].a == 2);
        assert(found == 2);
    }
    // the entries fit
    assert(r.count == 247 && r.count <= GP_ENTRIES_MAX);
    // a cking.sav block: packed (no alignment gaps); collect at 0xB2 there, 0xB4 in memory; the events at 0x618
    {
        unsigned char card[0x768] = {}, m[GP_SAVE_LEN];
        card[0x01] = 80;     // max life
        card[0xB2] = 0x0F;   // swords
        card[0xBD] = 0x07;   // pearls (collect + 0xB)
        card[0x618 + 0x95] = 1;  // the first figurine's event byte
        gp_card_to_memory(card, m);
        assert(m[GP_MAX_LIFE + 1] == 80 && m[GP_COLLECT] == 0x0F && m[GP_PEARLS] == 0x07);
        assert(m[GP_EVENTS + 0x95] == 1 && (signed char)m[GP_STAGE_NO] == -1);
        assert(m[0x8E] == 0 && m[0x8F] == 0);  // (the gap before dSv_player_get_bag_item_c)
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
            for (unsigned slot = 0; slot < 3 && (slot + 1) * 0xA94 <= n; slot++) {
                gp_card_to_memory(&b[slot * 0xA94], d);
                count();
                printf("%s Quest Log %u: %d %%", path.c_str(), slot + 1, gp_percent(&r));
                for (int p = 0; p < GP_PARTS; p++) printf(" %u/%u", r.done[p], r.total[p]);
                // the places against the count: found hearts (sure + maybe) = the pieces, containers, charts got,
                // charts salvaged (of the 49 counted), squares
                unsigned char st[GP_PLACES];
                gp_place_states(d, st);
                unsigned hearts = 0, maybe = 0, cont = 0, charts = 0, salv = 0, squares = 0;
                for (unsigned i = 0; i < GP_PLACES; i++) {
                    const gp_place& pl = gp_places[i];
                    const bool f = st[i] != GP_MISSING;
                    if (pl.kind == GP_K_HEART) hearts += f, maybe += st[i] == GP_MAYBE;
                    if (pl.kind == GP_K_CONTAINER) cont += f;
                    if (pl.kind == GP_K_CHART && pl.a != 35 && pl.a != 36 && pl.a <= 51) charts += f;
                    if (pl.kind == GP_K_SALVAGE && pl.a != 35 && pl.a != 36 && pl.a <= 51) salv += f;
                    if (pl.kind == GP_K_SQUARE) squares += f;
                }
                const gp_entry* e = r.e;
                unsigned pieces = e[r.first[GP_PART_HEARTS]].done, life = d[0] << 8 | d[1];
                unsigned want = life > 12 + 4 * cont ? life - 12 - 4 * cont : 0;
                printf("  places: hearts %u (%u maybe) of pieces %u%s, containers %u, charts got %u of %u, salvaged %u of %u, squares %u of %u\n",
                       hearts, maybe, want, hearts == (want > 44 ? 44 : want) ? "" : hearts < want ? " (pieces without a set place flag)" : " MORE FLAGS THAN PIECES", cont, charts, (unsigned)e[r.first[GP_PART_CHARTS]].done,
                       salv, (unsigned)e[r.first[GP_PART_CHARTS] + 1].done, squares, (unsigned)r.done[GP_PART_SEA]);
                (void)pieces;
                printf("\n");
            }
        }
    }
    puts("game_progress: ok");
    return 0;
}
