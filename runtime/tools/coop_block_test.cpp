// SPDX-License-Identifier: MPL-2.0
// Online co-op's blocked and muted players (GAPS C14), the pure part of runtime/src/mods/coop_block.h: the list and
// its file form.
#include <cstdio>
#include <string>

#include "mods/coop_block.h"

using namespace mods::coop::block;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

int main() {
    List l;
    CHECK(!l.set(0, kBlocked, "nobody"));  // (no key: not a server's player)
    CHECK(l.set(0x1122334455667788ull, kBlocked, "Sailor 7Q2F"));
    CHECK(l.set(0xAAull, kMuted, "Noisy"));
    CHECK(l.blocked(0x1122334455667788ull) && l.silent(0x1122334455667788ull));
    CHECK(!l.blocked(0xAAull) && l.silent(0xAAull));
    CHECK(!l.blocked(0xBBull) && !l.silent(0xBBull));
    // a mute turned into a block and back, one entry
    CHECK(l.set(0xAAull, kBlocked, "") && l.blocked(0xAAull) && l.entries().size() == 2 && l.find(0xAAull)->name == "Noisy");
    CHECK(l.set(0xAAull, kMuted, "") && !l.blocked(0xAAull));
    // the file form round trip; bad lines left out; control characters out of names, names capped
    l.set(0xCCull, kBlocked, std::string("Bad\nName\x01") + std::string(40, 'x'));
    const List back = List::parse(l.text() + "junk line\nblock 123 short\nmute 00000000000000zz x\n");
    CHECK(back.entries().size() == 3);
    CHECK(back.blocked(0x1122334455667788ull) && back.find(0x1122334455667788ull)->name == "Sailor 7Q2F");
    CHECK(back.silent(0xAAull) && !back.blocked(0xAAull));
    CHECK(back.find(0xCCull) && back.find(0xCCull)->name.size() == kMaxName && back.find(0xCCull)->name.find('\n') == std::string::npos);
    // undone
    List u = back;
    CHECK(u.clear(0xAAull) && !u.silent(0xAAull) && !u.clear(0xAAull) && u.entries().size() == 2);
    std::printf(g_fail ? "coop_block: %d failed\n" : "coop_block: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
