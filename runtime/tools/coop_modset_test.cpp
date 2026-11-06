// SPDX-License-Identifier: MPL-2.0
// Online co-op's gameplay mod sets (GAPS C5 party matching, server mod rules), the pure part of
// runtime/src/mods/coop_modset.h: the wire form, malformed lists refused, the comparison and its words.
#include <cstdio>
#include <string>

#include "mods/coop_modset.h"

using namespace mods::coop::modset;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static ModRef mod(const char* id, const char* v, uint8_t h) {
    ModRef r;
    r.id = id, r.version = v;
    r.hash.fill(h);
    return r;
}

int main() {
    // ids
    CHECK(valid_id("jump-anywhere") && valid_id("a.b_c-1") && !valid_id("") && !valid_id("Big") && !valid_id("a b") &&
          !valid_id(std::string(49, 'a')));
    // the wire form round trip, and malformed lists refused
    const Set a = {mod("jump-anywhere", "builtin", 1), mod("more-hearts", "1.2.0", 2), mod("randomizer", "", 3)};
    const std::string w = encode(a);
    Set b;
    size_t used = 0;
    CHECK(decode((const uint8_t*)w.data(), w.size(), b, &used) && used == w.size() && b.size() == 3 && same(a, b));
    for (size_t cut = 0; cut < w.size(); cut++) CHECK(!decode((const uint8_t*)w.data(), cut, b));  // (every truncation)
    std::string bad = w;
    bad[2] = 'X';  // (an id with a capital)
    CHECK(!decode((const uint8_t*)bad.data(), bad.size(), b));
    const uint8_t many[1] = {33};
    CHECK(!decode(many, 1, b));
    const uint8_t none[1] = {0};
    CHECK(decode(none, 1, b) && b.empty());
    CHECK(encode({}) == std::string(1, '\0'));
    // the same set in another order matches; another hash or version does not
    CHECK(same(a, {a[2], a[0], a[1]}));
    CHECK(!same(a, {a[0], a[1]}));
    Set other = a;
    other[1].hash[0] ^= 1;
    CHECK(!same(a, other));
    // what differs, named: the party's only, mine only, the same mod another build
    Set mine = {mod("more-hearts", "1.3.0", 2), mod("wall-climb", "builtin", 4), mod("randomizer", "", 3)};
    const auto ps = diff(a, mine);
    CHECK(ps.size() == 3);
    CHECK(ps[0].code == kPartyHas && ps[0].id == "jump-anywhere");
    CHECK(ps[1].code == kPartyOther && ps[1].id == "more-hearts" && ps[1].wanted == "1.2.0" && ps[1].have == "1.3.0");
    CHECK(ps[2].code == kOnlyYou && ps[2].id == "wall-climb");
    CHECK(diff(a, a).empty() && diff({}, {}).empty());
    const std::string text = words(ps);
    CHECK(text.find("jump-anywhere") != std::string::npos && text.find("the party plays with it, you don't") != std::string::npos);
    CHECK(text.find("more-hearts: the party has 1.2.0, you have 1.3.0") != std::string::npos);
    CHECK(text.find("wall-climb") != std::string::npos);
    // the same version with other files or settings says so; the server's codes have words too
    Set rebuilt = a;
    rebuilt[1].hash[5] ^= 1;
    CHECK(words(diff(a, rebuilt)[0]).find("other files or settings") != std::string::npos);
    for (uint8_t c = kMissing; c <= kPartyOther; c++) CHECK(!words(Problem{c, "x", "1", "2"}).empty());
    // many problems: the first named, then a count
    std::vector<Problem> lots(10, Problem{kOnlyYou, "m", "", "1"});
    CHECK(words(lots, 3).find("and 7 more") != std::string::npos);

    std::printf(g_fail ? "coop_modset: %d failed\n" : "coop_modset: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
