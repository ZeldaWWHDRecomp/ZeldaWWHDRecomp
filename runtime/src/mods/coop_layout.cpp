// SPDX-License-Identifier: MPL-2.0
// Online co-op: the save data's layout, shared and local (coop_layout.h).
#include "coop_layout.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mods::coop::layout {
namespace {

std::vector<Field> make_fields() {
    using namespace sv;
    std::vector<Field> f = {
        {kStatusA + kMaxLife, 2, kMax, "max life"},  // heart containers and pieces
        {kStatusA + kSelectEquip, 3, kFill, "sword, shield, bracelets"},  // filled where empty, never taken away
        {kStatusA + kWalletSize, 1, kMax, "wallet size"},
        {kStatusA + kMaxMagic, 1, kMax, "max magic"},
        {kGetItem, 21, kBits, "items got"},
        {kItemMax + kItemMax2, 1, kMax, "item max"}, {kItemMax + kItemMax2 + 1, 1, kMax, "arrow max"},
        {kItemMax + kItemMax2 + 2, 1, kMax, "bomb max"},
        {kCollect, 13, kBits, "collected"},  // items, songs, Triforce, pearls
        {kMap, 0x84, kBits, "map"},          // charts, sea map
        {kOcean, 0x64, kBits, "salvage"},
        {kEvent, kEventRegs, kBits, "event bits"},  // the story (its registers after them: local, below)
    };
    for (uint16_t i = 0; i < kItemSlots; i++)
        f.push_back({(uint16_t)(kItem + i), 1, i >= kFirstBottle && i <= kLastBottle ? kBottle : kFill, "item slot"});
    for (int s = 0; s < kStages; s++) {
        f.push_back({memory(s, kTbox), 4, kBits, "chests"});
        f.push_back({memory(s, kSwitch), 16, kBits, "switches"});  // 0-127; 128-255 are dSv_danBit_c/dSv_zoneBit_c: local
        f.push_back({memory(s, kMemItem), 4, kBits, "pickups"});
        f.push_back({memory(s, kVisitedRoom), 8, kBits, "visited rooms"});
        f.push_back({memory(s, kKeyNum), 1, kSet, "small keys"});
        f.push_back({memory(s, kDungeonItem), 1, kBits, "dungeon items"});  // map, compass, big key, boss beaten, heart
    }
    std::sort(f.begin(), f.end(), [](const Field& a, const Field& b) { return a.off < b.off; });
    return f;
}

// C19. Event bits the game clears again (zeldaret/tww and wwhd_src: every offEventBit of a flag, debug menus left out):
// they describe a day, a week or one visit, not progress. The day's and week's (d_kankyo_dayproc: cleared as this game's
// own clock passes midnight or Sunday, and the clock is each player's own), the Hyrule statue moved (daObjYLzou, set or
// cleared from the place as it is entered), and one visit's states (a door's opening, daMdoor; a hint, daTag_Hint; the
// auction's bidder, daAuction; Medli's carry tag, daTag_MdCb; the cabana owner's visit, daNpc_Mt)
constexpr uint16_t kDayFlags[] = {0x1301, 0x1302, 0x1304, 0x2002, 0x2004, 0x2080, 0x2680, 0x2801, 0x2802, 0x2804, 0x2820,
                                  0x2940, 0x2980, 0x3B01, 0x3C02, 0x3C04, 0x3C08, 0x3C10, 0x3C20, 0x3C40, 0x3C80};
constexpr uint16_t kVisitFlags[] = {0x1140, 0x2F01, 0x3080, 0x3410, 0x3820, 0x3F01, 0x4008, 0x4020, 0x4040, 0x4080};

std::vector<Local> make_local() {
    using namespace sv;
    std::vector<Local> l = {
        {kStatusA + kLife, 2, 0xFF, "life (each player's own)"},
        {kStatusA + kRupee, 2, 0xFF, "rupees (each player's own wallet; gains shared only with the party's rupee switch)"},
        {kStatusA + 0x06, 3, 0xFF, "unknown status (field_0x6, field_0x8)"},
        {kStatusA + kSelectItem, 5, 0xFF, "the item buttons"},
        {kStatusA + kSelectEquip + 3, 1, 0xFF, "the fourth equipment slot (unknown)"},
        {kStatusA + kMagic, 4, 0xFF, "magic, unknown status, padding"},
        {kStatusB, 0x18, 0xFF, "time of day, date and wind (each game's own clock: else every join would jump it)"},
        {kReturnPlace, 0xC, 0xFF, "the return place (where this player starts after loading)"},
        {kItemRecord, 8, 0xFF, "ammunition, pictures, bottle contents, timer"},
        {kItemMax + 3, 5, 0xFF, "unknown item maxima"},
        {kBagItem, 0x18, 0xFF, "bags: spoils, bait, delivery (each player's own)"},
        {kBagItem + 0x18, 2, 0xFF, "padding"},
        {kGetBagItem, 0xC, 0xFF, "bags' got flags"},
        {kBagItemRecord, 0x18, 0xFF, "bags' counts"},
        {kCollect + 13, 3, 0xFF, "padding"},
        {kInfo, 0x5C, 0xFF, "name, save and death counts, puzzle, New Game+ count, the random salvage point"},
        {kConfig, 5, 0xFF, "options"},
        {kConfig + 5, 3, 0xFF, "padding"},
        {kPriest, 0x10, 0xFF, "a restart place (dSv_player_priest_c)"},
        {kStatusC, 4 * 0x70, 0xFF, "the inventory kept for the boss refights (dSv_player_status_c_c)"},
        {kStatusC + 4 * 0x70, 4, 0xFF, "padding"},
        {(uint16_t)(kEvent + kEventRegs), (uint16_t)(kEventSize - kEventRegs), 0xFF,
         "event registers: values, not flags (minigame records, letters, counters, an auction's item, a room): an OR of "
         "two would be garbage, and most are one visit's"},
        {kReserve, 0x50, 0xFF, "reserved"},
        {kReserve + 0x50, 4, 0xFF, "padding"},
    };
    for (int s = 0; s < kStages; s++) l.push_back({memory(s, kDungeonItem + 1), 2, 0xFF, "padding"});
    auto bits = [&](const uint16_t* f, size_t n, const char* why) {
        for (size_t i = 0; i < n; i++) l.push_back({event_byte(f[i]), 1, (uint8_t)f[i], why});
    };
    bits(kDayFlags, sizeof kDayFlags / sizeof *kDayFlags, "a day's or a week's event bit (cleared on this game's own clock)");
    bits(kVisitFlags, sizeof kVisitFlags / sizeof *kVisitFlags, "one visit's event bit (the game clears it again)");
    std::sort(l.begin(), l.end(), [](const Local& a, const Local& b) { return a.off < b.off; });
    return l;
}

struct Masks {
    uint8_t shared[kSaveSize];
    Masks() {
        memset(shared, 0, sizeof shared);
        for (const Field& f : fields())
            for (int b = f.off; b < f.off + f.size && b < kSaveSize; b++) shared[b] = 0xFF;
        for (const Local& e : local_state())
            for (int b = e.off; b < e.off + e.size && b < kSaveSize; b++) shared[b] &= (uint8_t)~e.mask;
    }
};
const Masks& masks() {
    static const Masks m;
    return m;
}

uint32_t value(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}

}  // namespace

const std::vector<Field>& fields() {
    static const std::vector<Field> f = make_fields();
    return f;
}
const std::vector<Local>& local_state() {
    static const std::vector<Local> l = make_local();
    return l;
}

const Field* field_of(uint16_t off, uint8_t size) {
    for (const Field& k : fields())
        if (off >= k.off && off + size <= k.off + k.size) return &k;
    return nullptr;
}

uint8_t shared_bits(uint16_t off) { return off < kSaveSize ? masks().shared[off] : 0; }

std::string name_of(uint16_t off) {
    using namespace sv;
    struct Block { uint16_t off, size; const char* name; };
    static const Block kBlocks[] = {
        {kStatusA, 0x18, "status_a"}, {kStatusB, 0x18, "status_b"}, {kReturnPlace, 0xC, "return_place"}, {kItem, 21, "item"},
        {kGetItem, 21, "get_item"}, {kItemRecord, 8, "item_record"}, {kItemMax, 8, "item_max"}, {kBagItem, 0x1A, "bag_item"},
        {kGetBagItem, 0xC, "get_bag_item"}, {kBagItemRecord, 0x18, "bag_item_record"}, {kCollect, 0x10, "collect"},
        {kMap, 0x84, "map"}, {kInfo, 0x5C, "info"}, {kConfig, 8, "config"}, {kPriest, 0x10, "priest"},
        {kStatusC, 0x1C4, "status_c"}, {kOcean, 0x64, "ocean"}, {kEvent, kEventSize, "event"}, {kReserve, 0x54, "reserve"}};
    char t[64];
    if (off >= kMemory && off < kOcean) {
        const int s = (off - kMemory) / kMemSize, m = (off - kMemory) % kMemSize;
        const char* what = m < kSwitch ? "chests" : m < kMemItem ? "switches" : m < kVisitedRoom ? "pickups" : m < kKeyNum ? "visited rooms"
                         : m == kKeyNum ? "small keys" : m == kDungeonItem ? "dungeon items" : "padding";
        snprintf(t, sizeof t, "memory[%d].%s+%d", s, what, m);
        return t;
    }
    for (const Block& b : kBlocks)
        if (off >= b.off && off < b.off + b.size) {
            snprintf(t, sizeof t, "%s[0x%02X]", b.name, off - b.off);
            return t;
        }
    snprintf(t, sizeof t, "0x%03X", off);
    return t;
}

int store_mode(uint16_t off, uint8_t size) {
    const Field* k = field_of(off, size);
    if (!k) return -1;
    bool any = false;
    for (int b = off; b < off + size; b++) any |= shared_bits((uint16_t)b) != 0;
    if (!any) return -1;
    return k->mode == kBits ? 0 : k->mode == kMax ? 1 : k->mode == kFill || k->mode == kBottle ? 2 : -1;
}

bool merge(uint8_t* save, const uint8_t* d, size_t n, std::string* trace) {
    // checked whole first: a damaged or foreign message changes nothing
    struct Rec { uint16_t off; uint8_t size; const Field* f; const uint8_t* v; };
    std::vector<Rec> recs;
    for (size_t i = 0; i < n;) {
        if (i + 3 > n) return false;
        const uint16_t off = (uint16_t)(d[i] | d[i + 1] << 8);
        const uint8_t size = d[i + 2];
        const Field* f = size ? field_of(off, size) : nullptr;
        if (!f || (f->mode != kBits && (off != f->off || size != f->size))) return false;  // (other fields go whole)
        const size_t len = 3 + size * (f->mode == kBits ? 2 : 1);
        if (i + len > n) return false;
        recs.push_back({off, size, f, d + i + 3});
        i += len;
    }
    for (const Rec& r : recs) {
        const uint16_t off = r.off;
        const uint8_t size = r.size, *v = r.v;
        uint8_t* p = save + off;
        switch (r.f->mode) {
        case kBits:
            for (int b = 0; b < size; b++) {
                const uint8_t mask = v[size + b] & shared_bits((uint16_t)(off + b));
                p[b] = (uint8_t)((p[b] & ~mask) | (v[b] & mask));
            }
            break;
        case kFill:
            for (int b = 0; b < size; b++)
                if (v[b] != 0xFF && (p[b] == 0xFF || p[b] == 0 || (p[b] < v[b] && off == sv::kStatusA + sv::kSelectEquip))) p[b] = v[b];
            break;
        case kBottle:
            if (p[0] == 0xFF && v[0] != 0xFF) p[0] = 0x50;  // dItemNo_EMPTY_BOTTLE_e
            break;
        default:
            if (r.f->mode == kSet || value(v, size) > value(p, size)) memcpy(p, v, size);
            break;
        }
        if (trace) {
            char t[16];
            snprintf(t, sizeof t, " %03X=%02X", off + size - 1, p[size - 1]);
            *trace += t;
        }
    }
    return true;
}

std::string diff(const uint8_t* was, const uint8_t* now, int* changed, std::string* said) {
    std::string out;
    int count = 0;
    for (const Field& f : fields()) {
        if (f.mode != kBits) {
            if (!memcmp(now + f.off, was + f.off, f.size)) continue;
            out += std::string{(char)(f.off & 0xFF), (char)(f.off >> 8), (char)f.size};
            out.append((const char*)now + f.off, f.size);
        } else {
            const size_t before = out.size();
            for (uint16_t b = f.off; b < f.off + f.size; b++)
                if (const uint8_t x = (uint8_t)((now[b] ^ was[b]) & shared_bits(b)))
                    out += std::string{(char)(b & 0xFF), (char)(b >> 8), (char)1, (char)(now[b] & shared_bits(b)), (char)x};
            if (out.size() == before) continue;
        }
        count++;
        if (char t[8]; said && said->size() < 64) snprintf(t, sizeof t, " %03X", f.off), *said += t;
    }
    if (changed) *changed = count;
    return out;
}

std::vector<std::string> team_state(const uint8_t* now, bool counts) {
    std::vector<std::string> out(1);
    auto add = [&](const std::string& rec) {
        if (out.back().size() + rec.size() > 1000) out.emplace_back();
        out.back() += rec;
    };
    for (const Field& f : fields()) {
        if (f.mode != kBits) {
            if (f.mode == kSet && !counts) continue;
            std::string rec{(char)(f.off & 0xFF), (char)(f.off >> 8), (char)f.size};
            rec.append((const char*)now + f.off, f.size);
            add(rec);
            continue;
        }
        for (uint16_t b = f.off; b < f.off + f.size; b++)
            if (const uint8_t v = now[b] & shared_bits(b)) add(std::string{(char)(b & 0xFF), (char)(b >> 8), (char)1, (char)v, (char)v});
    }
    return out;
}

bool from_card(const std::vector<uint8_t>& sav, int n, std::vector<uint8_t>& save) {
    constexpr size_t kSlot = 0xA94, kUsed = 0xA8C, kPacked = 0x768;
    if (n < 0 || n > 2 || sav.size() < 3 * kSlot) return false;
    const uint8_t* blk = sav.data() + n * kSlot;
    uint32_t sum = 0;
    for (size_t i = 0; i < kUsed; i++) sum += blk[i];
    const uint32_t a = value(blk + kUsed, 4), b = value(blk + kUsed + 4, 4);
    if (a != sum || b != (uint32_t)(kUsed * 0xFFFFFFFFull - sum)) return false;
    // the card's copy has no padding: dSv_save_c's padding bytes are left 0
    struct Gap { uint16_t off, size; };
    static const Gap kGaps[] = {{sv::kBagItem + 0x18, 2}, {sv::kCollect + 13, 3}, {sv::kConfig + 5, 3}, {sv::kStatusC + 4 * 0x70, 4},
                                {sv::kReserve + 0x50, 4}};
    save.assign(kSaveSize, 0);
    size_t from = 0;
    uint16_t to = 0;
    for (const Gap& g : kGaps) {
        memcpy(&save[to], blk + from, g.off - to);
        from += g.off - to, to = (uint16_t)(g.off + g.size);
    }
    return from == kPacked;
}

std::vector<std::string> local_changes(const uint8_t* before, const uint8_t* after) {
    std::vector<std::string> out;
    for (uint16_t o = 0; o < kSaveSize; o++)
        if (const uint8_t x = (uint8_t)((before[o] ^ after[o]) & ~shared_bits(o))) {
            char t[96];
            snprintf(t, sizeof t, "%03X %s: %02X -> %02X (local bits %02X)", o, name_of(o).c_str(), before[o], after[o], x);
            out.push_back(t);
        }
    return out;
}

}  // namespace mods::coop::layout
