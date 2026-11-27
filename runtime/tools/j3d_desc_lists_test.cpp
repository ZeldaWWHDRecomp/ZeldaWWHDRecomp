// native_draw/j3d_desc_lists.h: a J3D description whose program display list was rewritten at the same address is
// defined again (issue #682); descriptions without a list, or with the define's bytes, hold.
#include "native_draw/j3d_desc_lists.h"
#include <cassert>
#include <cstdio>
#include <vector>

using native_draw::j3ddesc::kKeyListBytesWord;
using native_draw::j3ddesc::kKeyListWord;
using native_draw::j3ddesc::program_list_holds;

int main() {
    std::vector<uint32_t> key(41, 0);
    key[kKeyListWord] = 0x236AD200;
    key[kKeyListBytesWord] = 8;
    const std::vector<uint8_t> defined = {0x10, 0x02, 0, 0, 0x27, 0x6C, 0xBC, 0x00};
    std::vector<uint8_t> now = defined;
    // the define's bytes: holds
    assert(program_list_holds(key.data(), uint32_t(key.size()), defined, now.data()));
    // rewritten in place (another program address): defined again
    now[6] = 0xC0;
    assert(!program_list_holds(key.data(), uint32_t(key.size()), defined, now.data()));
    // not readable now: defined again (the define then finds it unreadable and leaves the draw to the site)
    assert(!program_list_holds(key.data(), uint32_t(key.size()), defined, nullptr));
    // the define kept another size than the key names: defined again
    const std::vector<uint8_t> shorter(defined.begin(), defined.begin() + 4);
    assert(!program_list_holds(key.data(), uint32_t(key.size()), shorter, defined.data()));
    // no list (the stage programs are bound): nothing to compare
    key[kKeyListBytesWord] = 0;
    assert(program_list_holds(key.data(), uint32_t(key.size()), {}, nullptr));
    // a key too short to name a list
    assert(program_list_holds(key.data(), kKeyListBytesWord, defined, nullptr));
    std::puts("j3d_desc_lists: ok");
    return 0;
}
