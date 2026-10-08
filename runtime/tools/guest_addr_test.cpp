// SPDX-License-Identifier: MPL-2.0
#include "guest_addr.h"
#include <cassert>

int main() {
#ifdef TEST_IDENTITY
    assert(GC(0x02715310) == 0x02715310);
    assert(GD(0x101F84DC) == 0x101F84DC);
    assert(guest_code_valid(0x02593B18));
#else
    assert(GC(0x02715310) == 0x02715BCC);
    assert(GC(0x02574144) == 0x02574148);
    assert(GC(0x025BA9FC) == 0x025BA9BC);
    assert(GD(0x101F84DC) == 0x101F84F4);
    assert(GD(0x104B5730) == 0x104B5748);
    assert(!guest_code_valid(0x02593B18));
    assert(guest_code_valid(0x02593B10));
    assert(!guest_code_valid(0x02000000));
    assert(!guest_data_valid(0x104DA1C8));
    assert(!guest_data_valid(0x145AC92C));
    assert(!guest_code_range_ok(0x02593B10, 0x02593B18));
#endif
}
