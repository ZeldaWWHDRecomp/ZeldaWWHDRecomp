// SPDX-License-Identifier: MPL-2.0
// Online co-op's save layout, local-state list and save safety (mods/coop_layout.cpp, GAPS C19/C20), without the game:
//   - every byte of dSv_save_c is in a shared field or in the local-state list; whole-byte local entries never overlap
//     a shared field, single local bits sit only inside shared bit fields; the shared fields do not overlap;
//   - the C19 entries: life, rupees, magic, ammunition, bags, time of day, return place, the event registers, the
//     day's and one visit's event bits are local; the story bits next to them shared;
//   - a server's team store keeps no local field (store_mode -1);
//   - diff never sends a local bit, team_state never gives one, merge never writes one: random images, random and
//     damaged messages (fuzz); a damaged message changes nothing;
//   - the union merge: bits as changed, maxima, fill where empty, bottles empty, small keys as they come;
//   - from_card reads a Quest Log of a cking.sav (checksums), and local_changes names what changed outside the shared
//     bits (the save check: after a party session the only differences to the backup that are not the player's own
//     play are shared fields).
// `coop_layout_test BACKUP.sav NOW.sav [file]` runs the save check on two real files instead (prints the local changes).
#include "mods/coop_layout.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace ly = mods::coop::layout;
namespace sv = mods::coop::layout::sv;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c), g_fail++; \
    } while (0)

static std::string rec(uint16_t off, std::vector<uint8_t> v, std::vector<uint8_t> mask = {}) {
    std::string r{(char)(off & 0xFF), (char)(off >> 8), (char)v.size()};
    r.append(v.begin(), v.end());
    r.append(mask.begin(), mask.end());
    return r;
}
static bool merge(std::vector<uint8_t>& img, const std::string& m) { return ly::merge(img.data(), (const uint8_t*)m.data(), m.size()); }

static std::vector<uint8_t> card(const std::vector<std::vector<uint8_t>>& packed) {  // a cking.sav of 3 packed files
    std::vector<uint8_t> f(3 * 0xA94 + 0x342 + 8, 0);
    for (int s = 0; s < 3; s++) {
        uint8_t* b = &f[s * 0xA94];
        memcpy(b, packed[s].data(), packed[s].size());
        uint32_t sum = 0;
        for (int i = 0; i < 0xA8C; i++) sum += b[i];
        const uint32_t inv = (uint32_t)(0xA8Cull * 0xFFFFFFFFull - sum);
        for (int i = 0; i < 4; i++) b[0xA8C + i] = (uint8_t)(sum >> (24 - 8 * i)), b[0xA90 + i] = (uint8_t)(inv >> (24 - 8 * i));
    }
    return f;
}

static int check_files(const char* a, const char* b, int n) {
    auto load = [](const char* p) {
        std::ifstream f(p, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
    };
    std::vector<uint8_t> x, y;
    if (!ly::from_card(load(a), n, x) || !ly::from_card(load(b), n, y)) return fprintf(stderr, "not a cking.sav, or Quest Log %d damaged\n", n + 1), 2;
    const auto l = ly::local_changes(x.data(), y.data());
    int shared = 0;
    for (uint16_t o = 0; o < ly::kSaveSize; o++) shared += ((x[o] ^ y[o]) & ly::shared_bits(o)) != 0;
    printf("Quest Log %d: %d bytes changed in shared fields, %zu outside them\n", n + 1, shared, l.size());
    for (const std::string& s : l) printf("  %s\n", s.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3) return check_files(argv[1], argv[2], argc > 3 ? atoi(argv[3]) - 1 : 0);

    // the whole of dSv_save_c is covered, once
    const auto& fs = ly::fields();
    for (size_t i = 1; i < fs.size(); i++) CHECK(fs[i - 1].off + fs[i - 1].size <= fs[i].off);
    for (const auto& f : fs) CHECK(f.off + f.size <= ly::kSaveSize);
    std::vector<uint8_t> local(ly::kSaveSize, 0);
    for (const auto& e : ly::local_state()) {
        CHECK(e.off + e.size <= ly::kSaveSize && e.mask);
        const ly::Field* f = ly::field_of(e.off, 1);
        if (e.mask == 0xFF) {
            for (int b = e.off; b < e.off + e.size; b++) CHECK(!ly::field_of((uint16_t)b, 1));
        } else {
            CHECK(f && f->mode == ly::kBits && e.size == 1);
        }
        for (int b = e.off; b < e.off + e.size; b++) local[b] |= e.mask;
    }
    for (uint16_t o = 0; o < ly::kSaveSize; o++) {
        const bool in_field = ly::field_of(o, 1) != nullptr;
        if (!in_field && local[o] != 0xFF) fprintf(stderr, "FAIL: byte %03X (%s) neither shared nor local\n", o, ly::name_of(o).c_str()), g_fail++;
        CHECK((ly::shared_bits(o) & local[o]) == 0 && (ly::shared_bits(o) | local[o]) == 0xFF);
    }

    // the C19 entries
    for (uint16_t o : {sv::kStatusA + sv::kLife, sv::kStatusA + sv::kRupee, sv::kStatusA + sv::kMagic, sv::kStatusA + sv::kSelectItem})
        CHECK(ly::shared_bits((uint16_t)o) == 0);
    for (uint16_t o = sv::kStatusB; o < sv::kStatusB + 0x18; o++) CHECK(!ly::shared_bits(o));                  // time of day
    for (uint16_t o = sv::kReturnPlace; o < sv::kReturnPlace + 0xC; o++) CHECK(!ly::shared_bits(o));          // return place
    for (uint16_t o = sv::kItemRecord; o < sv::kItemRecord + 8; o++) CHECK(!ly::shared_bits(o));              // ammunition
    for (uint16_t o = sv::kBagItem; o < sv::kCollect; o++) CHECK(!ly::shared_bits(o));                        // bags
    for (uint16_t o = sv::kEvent + sv::kEventRegs; o < sv::kEvent + sv::kEventSize; o++) CHECK(!ly::shared_bits(o));  // registers
    CHECK(ly::shared_bits(ly::event_byte(0x1301)) == 0xF8);  // the day's 1301/1302/1304 local, the byte's others shared
    CHECK(ly::shared_bits(ly::event_byte(0x3C02)) == 0x01);
    CHECK(ly::shared_bits(ly::event_byte(0x3820)) == 0xDF);  // the Hyrule statue moved
    CHECK(ly::shared_bits(ly::event_byte(0x4080)) == 0x17);
    CHECK(ly::shared_bits(ly::event_byte(0x0101)) == 0xFF && ly::shared_bits(ly::event_byte(0x4101)) == 0xFF);  // story
    CHECK(ly::shared_bits(ly::memory(3, sv::kSwitch)) == 0xFF && ly::shared_bits(ly::memory(3, sv::kKeyNum)) == 0xFF);
    CHECK(ly::shared_bits(ly::memory(3, sv::kDungeonItem + 1)) == 0);
    CHECK(ly::shared_bits(sv::kStatusA + sv::kSelectEquip + 2) == 0xFF && ly::shared_bits(sv::kStatusA + sv::kSelectEquip + 3) == 0);

    // a server's store keeps none of it
    CHECK(ly::store_mode(sv::kStatusA + sv::kLife, 2) == -1 && ly::store_mode(sv::kStatusB + 0xC, 4) == -1);
    CHECK(ly::store_mode(sv::kEvent + 0x80, 1) == -1 && ly::store_mode(ly::memory(0, sv::kKeyNum), 1) == -1);
    CHECK(ly::store_mode(sv::kEvent + 0x13, 1) == 0 && ly::store_mode(sv::kStatusA, 2) == 1 && ly::store_mode(sv::kItem, 1) == 2);
    for (uint16_t o = 0; o < ly::kSaveSize; o++)
        if (!ly::shared_bits(o)) CHECK(ly::store_mode(o, 1) == -1);

    // the merge's modes
    std::vector<uint8_t> img(ly::kSaveSize, 0);
    img[sv::kItem] = 0xFF, img[sv::kItem + 14] = 0xFF, img[sv::kStatusA + sv::kSelectEquip] = 0x38;
    CHECK(merge(img, rec(ly::event_byte(0x1301), {0xFF}, {0xFF})));
    CHECK(img[ly::event_byte(0x1301)] == 0xF8);  // the local bits not taken
    CHECK(merge(img, rec(ly::event_byte(0x1340), {0x00}, {0x40})) && img[ly::event_byte(0x1301)] == 0xB8);  // a clear goes too
    CHECK(merge(img, rec(sv::kStatusA, {0x00, 0x30})) && img[1] == 0x30);
    CHECK(merge(img, rec(sv::kStatusA, {0x00, 0x20})) && img[1] == 0x30);  // max
    CHECK(merge(img, rec(sv::kItem, {0x20})) && img[sv::kItem] == 0x20);
    CHECK(merge(img, rec(sv::kItem, {0x21})) && img[sv::kItem] == 0x20);  // filled only where empty
    CHECK(merge(img, rec(sv::kItem + 14, {0x52})) && img[sv::kItem + 14] == 0x50);  // a bottle as an empty one
    CHECK(merge(img, rec(sv::kStatusA + sv::kSelectEquip, {0x39, 0x3B, 0xFF})) && img[sv::kStatusA + sv::kSelectEquip] == 0x39 &&
          img[sv::kStatusA + sv::kSelectEquip + 1] == 0x3B);  // the better sword, a shield where none
    img[ly::memory(2, sv::kKeyNum)] = 3;
    CHECK(merge(img, rec(ly::memory(2, sv::kKeyNum), {1})) && img[ly::memory(2, sv::kKeyNum)] == 1);  // a count as it comes

    // damaged messages change nothing
    const std::vector<uint8_t> before = img;
    for (const std::string& bad : {rec(sv::kStatusA + sv::kLife, {0x50, 0x50}),                          // a local field
                                   rec(sv::kEvent + 0x80, {0x07}, {0x07}),                                // an event register
                                   rec(sv::kStatusA, {0x00, 0x40}) + std::string("\x51\x00", 2),          // cut short
                                   rec(sv::kStatusA, {0x00, 0x40}) + rec(sv::kStatusA + 1, {0x40}),       // inside a whole field
                                   rec(sv::kGetItem, {0xFF}, {0xFF}).substr(0, 4),                        // no mask
                                   rec(0x700, {}),                                                        // size 0
                                   std::string("\xFF\xFF\x01\x01\x01", 5)})                               // outside
        CHECK(!merge(img, bad) && img == before);

    // fuzz: no local bit ever goes out, comes in or is kept
    std::mt19937 rng(20261106);
    for (int round = 0; round < 2000; round++) {
        std::vector<uint8_t> a(ly::kSaveSize), b(ly::kSaveSize);
        for (auto& x : a) x = (uint8_t)rng();
        b = a;
        for (int k = 0, n = (int)(rng() % 30); k < n; k++) b[rng() % ly::kSaveSize] = (uint8_t)rng();
        const std::string d = ly::diff(a.data(), b.data());
        std::vector<uint8_t> c(ly::kSaveSize);
        for (auto& x : c) x = (uint8_t)rng();
        const std::vector<uint8_t> c0 = c;
        CHECK(merge(c, d));
        for (uint16_t o = 0; o < ly::kSaveSize; o++) CHECK(((c[o] ^ c0[o]) & ~ly::shared_bits(o)) == 0);
        for (const std::string& m : ly::team_state(b.data())) {
            std::vector<uint8_t> e = c0;
            CHECK(m.size() <= 1000 && merge(e, m));
            for (uint16_t o = 0; o < ly::kSaveSize; o++) CHECK(((e[o] ^ c0[o]) & ~ly::shared_bits(o)) == 0);
        }
        // random and flipped messages: accepted or not, never a local bit
        std::string r = d;
        if (!r.empty()) r[rng() % r.size()] ^= (char)(1 << (rng() % 8));
        if (rng() % 4 == 0) { r.resize(rng() % 64); for (auto& ch : r) ch = (char)rng(); }
        std::vector<uint8_t> f = c0;
        if (!merge(f, r)) CHECK(f == c0);
        for (uint16_t o = 0; o < ly::kSaveSize; o++) CHECK(((f[o] ^ c0[o]) & ~ly::shared_bits(o)) == 0);
    }
    // a diff of a local-only change is empty
    {
        std::vector<uint8_t> a(ly::kSaveSize, 0), b = a;
        b[sv::kStatusB + 0xC] = 0x42, b[sv::kEvent + 0x90] = 5, b[ly::event_byte(0x2680)] = 0x80, b[sv::kReturnPlace] = 'A';
        int changed = -1;
        CHECK(ly::diff(a.data(), b.data(), &changed).empty() && changed == 0);
        b[ly::event_byte(0x2640)] = 0xC0;  // with a shared bit of the same byte: that one only
        const std::string d = ly::diff(a.data(), b.data(), &changed);
        CHECK(changed == 1 && d == rec(ly::event_byte(0x2640), {0x40}, {0x40}));
    }

    // the save check on cking.sav files
    {
        std::vector<std::vector<uint8_t>> packed(3, std::vector<uint8_t>(0x768));
        for (auto& p : packed) for (auto& x : p) x = (uint8_t)rng();
        std::vector<uint8_t> x, y;
        CHECK(ly::from_card(card(packed), 1, x) && x.size() == ly::kSaveSize);
        // memory layout from the packed one: the event block sits at 0x624 (12 padding bytes before it: packed 0x618)
        CHECK(!memcmp(&x[sv::kEvent], &packed[1][0x618], 0x100) && !memcmp(&x[sv::kMemory], &packed[1][0x374], 0x240));
        CHECK(x[sv::kBagItem + 0x18] == 0 && x[0x774] == 0 && !memcmp(&x[sv::kGetBagItem], &packed[1][0x8E], 0xC));
        auto after = packed;
        after[1][0x624 - 12 + 0x10] ^= 0x01;   // a story bit (event byte 0x10): shared
        CHECK(ly::from_card(card(after), 1, y) && ly::local_changes(x.data(), y.data()).empty());
        after[1][0x18 + 0xC] ^= 0x01;          // the time of day: local
        CHECK(ly::from_card(card(after), 1, y));
        const auto l = ly::local_changes(x.data(), y.data());
        CHECK(l.size() == 1 && l[0].find("status_b[0x0C]") != std::string::npos);
        auto broken = card(after);
        broken[0xA94 + 5] ^= 1;  // a damaged Quest Log is refused
        CHECK(!ly::from_card(broken, 1, y) && ly::from_card(broken, 0, y));
    }

    if (g_fail) return fprintf(stderr, "%d checks failed\n", g_fail), 1;
    printf("coop_layout: ok (%zu shared fields, %zu local entries)\n", fs.size(), ly::local_state().size());
    return 0;
}
