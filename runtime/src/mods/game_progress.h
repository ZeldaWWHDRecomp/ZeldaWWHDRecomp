/* SPDX-License-Identifier: MPL-2.0
 * Game progress: how far the save file in play is, counted from its save data (dSv_save_c as on the GameCube,
 * zeldaret/tww include/d/d_save.h; checked on HD saves), in ten parts that weigh the same, each a checklist of what is
 * collected and what is missing. Read only: nothing here writes the save.
 *
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), android/.../GameProgress.java and
 * runtime/src/mods/progress.cpp, taken over as plain C (usable from the port's C++ and from a guest mod):
 *   Dungeons: per dungeon (stage table entries 2..7: Forsaken Fortress .. Wind Temple, dSv_memBit_c mDungeonItem +0x21)
 *     its map (bit 0), compass (1), Big Key (2; the Forsaken Fortress has none), boss (3) and Heart Container (4);
 *     the stage in play from its working copy (save + 0x778, its stage number s8 at + 0x79C).
 *   Hearts: Pieces of Heart = max life (quarters) - 12 - 4 per Heart Container taken: 44.
 *   Items: the inventory slots (+0x3C), the Swift Sail (0x77 in the sail's slot), the Deluxe Picto Box (0x26), the bow's
 *     Fire & Ice and Light Arrows (its "got" bits +0x51 + 12: 2, 4), the four bottles as one row.
 *   Equipment: dSv_player_collect_c +0xB4: swords (byte 0), shields (1), Power Bracelets (2), Pirate's Charm (3),
 *     Hero's Charm (4).
 *   Upgrades: wallet (+0x12), bomb bag / quiver max (+0x70 / +0x6F), magic max (+0x13).
 *   Songs (+0xBD, 6 bits); Pearls (+0xBF, 3 bits) and Triforce Shards (+0xBE, 8 bits).
 *   Charts: dSv_player_map_c +0xC4 (u32 [4][4]: set 1 got, set 3 its treasure salvaged), the charts the game counts
 *     (1..51 without 35 and 36): 49 each.
 *   Sea chart: the 49 squares the fishman filled in (+0x104, bit 0 of a byte each).
 *   Nintendo Gallery: the 134 figurines (event bytes from d_a_npc_mt.cpp l_figure_comp, events at +0x624).
 * Not counted (no flag of their own): the Tingle Statues as a part, the Hurricane Spin, the trading quest, the last
 * fight. The percent is the mean of the parts, floored; 100 only when nothing is missing (else at most 99).
 */
#ifndef WWHD_GAME_PROGRESS_H
#define WWHD_GAME_PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GP_SAVE_LEN = 0x7A0, /* the bytes of dSv_save_c + the stage's working copy + its number that are read */
    GP_MAX_LIFE = 0x00, GP_WALLET = 0x12, GP_MAX_MAGIC = 0x13, GP_ITEMS = 0x3C, GP_GOT_ITEMS = 0x51,
    GP_ARROW_MAX = 0x6F, GP_BOMB_MAX = 0x70, GP_COLLECT = 0xB4, GP_TACT = 0xBD, GP_TRIFORCE = 0xBE, GP_PEARLS = 0xBF,
    GP_MAP = 0xC4, GP_FMAP = 0xC4 + 0x40, GP_INFO = 0x148, GP_STAGES = 0x380, GP_STAGE_SIZE = 0x24,
    GP_DUNGEON_ITEM = 0x21, GP_OCEAN = 0x5C0, GP_EVENTS = 0x624, GP_STAGE_COPY = 0x778, GP_STAGE_NO = 0x79C,
    GP_DUNGEONS = 6, GP_FIGURINES = 134, GP_PARTS = 10, GP_ENTRIES_MAX = 256
};

/* the parts, in the order shown */
enum { GP_PART_DUNGEONS, GP_PART_HEARTS, GP_PART_ITEMS, GP_PART_EQUIPMENT, GP_PART_UPGRADES, GP_PART_SONGS,
       GP_PART_QUEST, GP_PART_CHARTS, GP_PART_SEA, GP_PART_GALLERY };

/* what an entry is, for its name and icon (the caller looks those up in the game's texts and art) */
enum {
    GP_E_DUNGEON = 1,   /* arg: dungeon 1..6; parts: bits of map, compass, Big Key, boss, Heart Container */
    GP_E_HEART_PIECES,  /* count of 44 */
    GP_E_ITEM,          /* arg: item id (the game's item numbers, e.g. 0x20 Telescope) */
    GP_E_FIRE_ICE, GP_E_LIGHT,
    GP_E_BOTTLES,       /* count of 4 */
    GP_E_MASTER_HALF, GP_E_MASTER_FULL, GP_E_PIRATE,
    GP_E_UPGRADE,       /* arg: item id of the upgrade (0xAB..0xB0); GP_E_MAGIC for the magic meter */
    GP_E_MAGIC,
    GP_E_SONG,          /* arg: melody 0..5 */
    GP_E_PEARL,         /* arg: item id 0x69..0x6B */
    GP_E_SHARD,         /* arg: shard 1..8 */
    GP_E_CHARTS,        /* count of 49 */
    GP_E_SALVAGED,      /* count of 49 */
    GP_E_SQUARE,        /* arg: sea square 1..49 */
    GP_E_FIGURINE       /* arg: figurine 0..133 */
};

typedef struct {
    unsigned char part, kind;
    unsigned short arg;
    unsigned char done, total;
    unsigned char got_mask, parts_mask; /* dungeons: which of its five things are had / exist */
} gp_entry;

typedef struct {
    unsigned short done[GP_PARTS], total[GP_PARTS];
    unsigned short first[GP_PARTS + 1]; /* entries of part p: first[p] .. first[p + 1] - 1 */
    unsigned short count;
    gp_entry e[GP_ENTRIES_MAX];
} gp_result;

static inline unsigned gp_u8(const unsigned char* d, unsigned o) { return d[o]; }
static inline unsigned long gp_u32(const unsigned char* d, unsigned o) {
    return ((unsigned long)d[o] << 24) | ((unsigned long)d[o + 1] << 16) | ((unsigned long)d[o + 2] << 8) | d[o + 3];
}
static inline int gp_bit(const unsigned char* d, unsigned o, unsigned b) { return (d[o] >> b) & 1; }

/* a chart's bit in one of dSv_player_map_c's four 128-bit sets (dSv_player_map_c::isGetMap and friends), n 1-based */
static inline int gp_chart(const unsigned char* d, unsigned set, unsigned n) {
    unsigned i = n - 1;
    return (gp_u32(d, GP_MAP + set * 16 + (i >> 5) * 4) >> (i & 31)) & 1;
}

/* a byte of a stage's dSv_memBit_c, with the working copy of the stage in play */
static inline unsigned gp_stage_byte(const unsigned char* d, int stage, unsigned o) {
    unsigned v = d[GP_STAGES + stage * GP_STAGE_SIZE + o];
    if ((signed char)d[GP_STAGE_NO] == stage) v |= d[GP_STAGE_COPY + o];
    return v;
}

/* the figurines' event bytes, 8 figurines each (d_a_npc_mt.cpp l_figure_comp) */
static const unsigned char gp_figure_bytes[17] = {0x95, 0x94, 0x93, 0x92, 0x91, 0x90, 0x8F, 0x8E, 0x8D,
                                                  0x8C, 0xB1, 0x9C, 0x84, 0x83, 0x82, 0x81, 0x80};

static inline int gp_figurine(const unsigned char* d, unsigned f) {
    return gp_bit(d, GP_EVENTS + gp_figure_bytes[f / 8], f % 8);
}

static inline void gp_add(gp_result* r, unsigned part, unsigned kind, unsigned arg, unsigned done, unsigned total) {
    gp_entry* e;
    if (r->count >= GP_ENTRIES_MAX) return;
    e = &r->e[r->count++];
    e->part = (unsigned char)part;
    e->kind = (unsigned char)kind;
    e->arg = (unsigned short)arg;
    e->done = (unsigned char)(done < total ? done : total);
    e->total = (unsigned char)total;
    e->got_mask = e->parts_mask = 0;
    r->done[part] += e->done;
    r->total[part] += (unsigned short)total;
}

static inline void gp_flag(gp_result* r, unsigned part, unsigned kind, unsigned arg, int got) {
    gp_add(r, part, kind, arg, got ? 1u : 0u, 1);
}

/* d: GP_SAVE_LEN bytes of the save data as in guest memory (big-endian) */
static inline void gp_count(const unsigned char* d, gp_result* r) {
    static const unsigned char slots[17][2] = {{0, 0x20},  {1, 0x78},  {2, 0x22},  {3, 0x25},  {4, 0x24},  {5, 0x2D},
                                              {6, 0x34},  {7, 0x21},  {8, 0x23},  {9, 0x29},  {10, 0x2A}, {11, 0x2C},
                                              {12, 0x27}, {13, 0x31}, {18, 0x30}, {19, 0x2F}, {20, 0x33}};
    static const unsigned char pearl_item[3] = {0x69, 0x6A, 0x6B};
    unsigned p, n, s, containers = 0, life, pieces, bottles = 0, got = 0, salvaged = 0;
    unsigned char* z = (unsigned char*)r;
    for (n = 0; n < sizeof *r; n++) z[n] = 0;

    /* dungeons */
    r->first[GP_PART_DUNGEONS] = r->count;
    for (n = 1; n <= GP_DUNGEONS; n++) {
        unsigned v = gp_stage_byte(d, (int)n + 1, GP_DUNGEON_ITEM) & 0x1F;
        unsigned exist = n == 1 ? 0x1B : 0x1F; /* (no Big Key in the Forsaken Fortress) */
        unsigned have = v & exist, c = 0, b;
        gp_entry* e;
        for (b = 0; b < 5; b++) c += (have >> b) & 1;
        if (v & 0x10) containers++;
        gp_add(r, GP_PART_DUNGEONS, GP_E_DUNGEON, n, c, n == 1 ? 4 : 5);
        e = &r->e[r->count - 1];
        e->got_mask = (unsigned char)have;
        e->parts_mask = (unsigned char)exist;
    }
    /* hearts */
    r->first[GP_PART_HEARTS] = r->count;
    life = (gp_u8(d, GP_MAX_LIFE) << 8) | gp_u8(d, GP_MAX_LIFE + 1);
    pieces = life > 12 + 4 * containers ? life - 12 - 4 * containers : 0;
    gp_add(r, GP_PART_HEARTS, GP_E_HEART_PIECES, 0x07, pieces > 44 ? 44 : pieces, 44);
    /* items */
    r->first[GP_PART_ITEMS] = r->count;
    for (s = 0; s < 17; s++) {
        unsigned have = d[GP_ITEMS + slots[s][0]];
        gp_flag(r, GP_PART_ITEMS, GP_E_ITEM, slots[s][1], have != 0xFF);
        if (slots[s][0] == 1) gp_flag(r, GP_PART_ITEMS, GP_E_ITEM, 0x77, have == 0x77);
        if (slots[s][0] == 8) gp_flag(r, GP_PART_ITEMS, GP_E_ITEM, 0x26, have == 0x26);
        if (slots[s][0] == 12) {
            unsigned bow = d[GP_GOT_ITEMS + 12];
            gp_flag(r, GP_PART_ITEMS, GP_E_FIRE_ICE, 0, have != 0xFF && ((bow & 2) || have >= 0x35));
            gp_flag(r, GP_PART_ITEMS, GP_E_LIGHT, 0, have != 0xFF && ((bow & 4) || have == 0x36));
        }
    }
    for (s = 14; s < 18; s++)
        if (d[GP_ITEMS + s] != 0xFF) bottles++;
    gp_add(r, GP_PART_ITEMS, GP_E_BOTTLES, 0x50, bottles, 4);
    /* equipment */
    r->first[GP_PART_EQUIPMENT] = r->count;
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_ITEM, 0x38, gp_bit(d, GP_COLLECT, 0));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_ITEM, 0x39, gp_bit(d, GP_COLLECT, 1));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_MASTER_HALF, 0, gp_bit(d, GP_COLLECT, 2));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_MASTER_FULL, 0, gp_bit(d, GP_COLLECT, 3));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_ITEM, 0x3B, gp_bit(d, GP_COLLECT + 1, 0));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_ITEM, 0x3C, gp_bit(d, GP_COLLECT + 1, 1));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_ITEM, 0x28, gp_bit(d, GP_COLLECT + 2, 0));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_PIRATE, 0, gp_bit(d, GP_COLLECT + 3, 0));
    gp_flag(r, GP_PART_EQUIPMENT, GP_E_ITEM, 0x43, gp_bit(d, GP_COLLECT + 4, 0));
    /* upgrades */
    r->first[GP_PART_UPGRADES] = r->count;
    gp_flag(r, GP_PART_UPGRADES, GP_E_UPGRADE, 0xAB, d[GP_WALLET] >= 1);
    gp_flag(r, GP_PART_UPGRADES, GP_E_UPGRADE, 0xAC, d[GP_WALLET] >= 2);
    gp_flag(r, GP_PART_UPGRADES, GP_E_UPGRADE, 0xAD, d[GP_BOMB_MAX] >= 60);
    gp_flag(r, GP_PART_UPGRADES, GP_E_UPGRADE, 0xAE, d[GP_BOMB_MAX] >= 99);
    gp_flag(r, GP_PART_UPGRADES, GP_E_UPGRADE, 0xAF, d[GP_ARROW_MAX] >= 60);
    gp_flag(r, GP_PART_UPGRADES, GP_E_UPGRADE, 0xB0, d[GP_ARROW_MAX] >= 99);
    gp_flag(r, GP_PART_UPGRADES, GP_E_MAGIC, 0x52, d[GP_MAX_MAGIC] >= 32);
    /* songs */
    r->first[GP_PART_SONGS] = r->count;
    for (n = 0; n < 6; n++) gp_flag(r, GP_PART_SONGS, GP_E_SONG, n, gp_bit(d, GP_TACT, n));
    /* pearls (Nayru, Din, Farore by bit) and the Triforce */
    r->first[GP_PART_QUEST] = r->count;
    for (n = 0; n < 3; n++) gp_flag(r, GP_PART_QUEST, GP_E_PEARL, pearl_item[n], gp_bit(d, GP_PEARLS, n));
    for (n = 0; n < 8; n++) gp_flag(r, GP_PART_QUEST, GP_E_SHARD, n + 1, gp_bit(d, GP_TRIFORCE, n));
    /* charts the game counts, found and their treasure salvaged */
    r->first[GP_PART_CHARTS] = r->count;
    for (n = 1; n <= 51; n++) {
        if (n == 35 || n == 36) continue;
        got += (unsigned)gp_chart(d, 1, n);
        salvaged += (unsigned)gp_chart(d, 3, n);
    }
    gp_add(r, GP_PART_CHARTS, GP_E_CHARTS, 0xCC, got, 49);
    gp_add(r, GP_PART_CHARTS, GP_E_SALVAGED, 0, salvaged, 49);
    /* the sea chart's squares */
    r->first[GP_PART_SEA] = r->count;
    for (n = 1; n <= 49; n++) gp_flag(r, GP_PART_SEA, GP_E_SQUARE, n, gp_bit(d, GP_FMAP + n - 1, 0));
    /* figurines */
    r->first[GP_PART_GALLERY] = r->count;
    for (n = 0; n < GP_FIGURINES; n++) gp_flag(r, GP_PART_GALLERY, GP_E_FIGURINE, n, gp_figurine(d, n));
    r->first[GP_PARTS] = r->count;
    (void)p;
}

/* A cking.sav file block's save data (0x768 bytes, packed by dSv_info_c::memory_to_card; the blocks of
 * tools/savegame/wwsave.py) laid out as in memory (the structures' alignment gaps zero, no stage in play),
 * for reading save files outside the game (tests, the progress of a Quest Log not loaded). */
static inline void gp_card_to_memory(const unsigned char* card, unsigned char* d) {
    /* packed size, offset in memory */
    static const unsigned short blocks[][2] = {
        {0x18, 0x000}, {0x18, 0x018}, {0x0C, 0x030}, {0x15, 0x03C}, {0x15, 0x051}, {0x08, 0x066}, {0x08, 0x06E},
        {0x18, 0x076}, {0x0C, 0x090}, {0x18, 0x09C}, {0x0D, 0x0B4}, {0x84, 0x0C4}, {0x5C, 0x148}, {0x05, 0x1A4},
        {0x10, 0x1AC}, {0x70, 0x1BC}, {0x70, 0x22C}, {0x70, 0x29C}, {0x70, 0x30C}, {0x240, 0x380}, {0x64, 0x5C0},
        {0x100, 0x624}, {0x50, 0x724}};
    unsigned i, n, p = 0;
    for (i = 0; i < GP_SAVE_LEN; i++) d[i] = 0;
    for (i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        for (n = 0; n < blocks[i][0]; n++) d[blocks[i][1] + n] = card[p + n];
        p += blocks[i][0];
    }
    d[GP_STAGE_NO] = 0xFF;
}

/* 0..100; 100 only when every part is full */
static inline int gp_percent(const gp_result* r) {
    double sum = 0;
    int all = 1, p, pc;
    for (p = 0; p < GP_PARTS; p++) {
        sum += r->total[p] ? (double)r->done[p] / r->total[p] : 1.0;
        if (r->done[p] < r->total[p]) all = 0;
    }
    pc = (int)(100.0 * sum / GP_PARTS);
    if (all) return 100;
    return pc > 99 ? 99 : pc;
}

#ifdef __cplusplus
}
#endif
#endif
