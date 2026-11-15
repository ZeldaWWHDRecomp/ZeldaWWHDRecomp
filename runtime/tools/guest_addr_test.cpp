// SPDX-License-Identifier: MPL-2.0
#include "guest_addr.h"
#include <cassert>

int main() {
#ifdef TEST_IDENTITY
    assert(GC(0x02715310) == 0x02715310);
    assert(GD(0x101F84DC) == 0x101F84DC);
    assert(guest_code_valid(0x02593B18));
    assert(guest_code_unchanged(0x02593B10));
    assert(GI(0x5B34) == 0x5B34);
#elif defined(TEST_JP)
    assert(GC(0x02715310) == 0x02715DF8);
    assert(GD(0x101F84DC) == 0x101F8524);
    assert(!guest_code_valid(0x025FA6BC));  // a function the Japanese build does not have, entry and interior
    assert(!guest_code_valid(0x025FA6DC));
    assert(GC_OPTIONAL(0x025FA6BC) == 0);
    assert(GC_OPTIONAL(0x02715310) == 0x02715DF8);
    assert(GC(0x0264E754) == 0x0264F2E4);  // rewritten: paired through the static initializer table
    assert(!guest_code_unchanged(0x0264E754));
    assert(!guest_code_unchanged(0x025F65F4) && !guest_code_unchanged(0x025F7424) && !guest_code_unchanged(0x025F7500));
    assert(!guest_code_valid(0x0264E758));
    // play object members (g_dComIfG_gameInfo): 4 bytes earlier from +0x5AB0, 5 at +0x5BEF
    assert(GI(0x5134) == 0x5134);
    assert(GI(0x5AAC) == 0x5AAC);
    assert(GI(0x5AB0) == 0x5AAC);
    assert(GI(0x5B34) == 0x5B30);
    assert(GI(0x5BEF) == 0x5BEA);
    assert(GI(0x5BF0) == 0x5BEC);
    assert(GD(0x1046F0B0 + 0x5B34) == GD(0x1046F0B0) + GI(0x5B34));
#else
    assert(GC(0x02715310) == 0x02715BCC);
    assert(GC(0x02574144) == 0x02574148);
    assert(GC(0x025BA9FC) == 0x025BA9BC);
    assert(GD(0x101F84DC) == 0x101F84F4);
    assert(GD(0x104B5730) == 0x104B5748);
    assert(!guest_code_valid(0x02593B18));
    assert(guest_code_valid(0x02593B10));
    assert(!guest_code_unchanged(0x02593B10));  // the entry of a function the European build changed
    assert(guest_code_unchanged(0x025F65F4) && guest_code_unchanged(0x025F7424) && guest_code_unchanged(0x025F7500));
    assert(guest_code_unchanged(0x02715310));
    assert(!guest_code_valid(0x02000000));
    assert(!guest_data_valid(0x104DA1C8));
    assert(!guest_data_valid(0x145AC92C));
    assert(!guest_code_range_ok(0x02593B10, 0x02593B18));
    assert(GI(0x5B34) == 0x5B34);  // the European build keeps every member
#endif
}
