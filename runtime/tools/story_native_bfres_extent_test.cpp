// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_bfres_extent.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_bfres_extent;
namespace {
constexpr uint32_t Base = 0x60800000;
void w32(std::vector<uint8_t>& b, unsigned p, uint32_t n) {
    b[p] = uint8_t(n >> 24); b[p+1] = uint8_t(n >> 16);
    b[p+2] = uint8_t(n >> 8); b[p+3] = uint8_t(n);
}
void rel(std::vector<uint8_t>& b, unsigned from, unsigned to) {
    w32(b, from, uint32_t(int32_t(to) - int32_t(from)));
}
std::vector<uint8_t> fixture() {
    std::vector<uint8_t> b(0x400);
    std::memcpy(b.data(), "FRES", 4);
    b[8] = 0xfe; b[9] = 0xff;
    w32(b, 0x0c, uint32_t(b.size()));
    b[0x66] = 0; b[0x67] = 2;
    rel(b, 0x4c, 0x100);
    w32(b, 0x104, 2);
    rel(b, 0x120, 0x1a0); rel(b, 0x124, 0x200);
    rel(b, 0x130, 0x1c0); rel(b, 0x134, 0x210);
    const char* name = "event_list.dat";
    w32(b, 0x19c, uint32_t(std::strlen(name)));
    std::memcpy(b.data()+0x1a0, name, std::strlen(name)+1);
    w32(b, 0x1bc, 5); std::memcpy(b.data()+0x1c0, "other", 6);
    rel(b, 0x200, 0x240); w32(b, 0x204, 0x80);
    rel(b, 0x210, 0x300); w32(b, 0x214, 0x40);
    return b;
}
bool find_in(const std::vector<uint8_t>& b, Extent& e, uint32_t expected = Base+0x240,
             uint32_t size = 0x400) {
    Read read = [&](uint32_t a, std::span<uint8_t> dst) {
        if (a < Base || uint64_t(a-Base)+dst.size() > b.size()) return false;
        std::memcpy(dst.data(), b.data()+(a-Base), dst.size());
        return true;
    };
    return find(read, Base, size, "event_list.dat", expected, e);
}
}
int main() {
    auto b = fixture(); Extent e{};
    assert(find_in(b,e) && e.base == Base+0x240 && e.bytes == 0x80 && e.entry == Base+0x118);
    assert(!find_in(b,e,Base+0x241) && !e.base);
    assert(!find_in(b,e,Base+0x240,0x3ff));
    auto bad=b; w32(bad,0x204,0x300); assert(!find_in(bad,e));
    bad=b; w32(bad,0x104,3); assert(!find_in(bad,e));
    bad=b; rel(bad,0x130,0x1a0); assert(!find_in(bad,e));
    bad=b; w32(bad,0x19c,12); assert(!find_in(bad,e));
    bad=b; w32(bad,0x200,0); assert(!find_in(bad,e));
    bad=b; rel(bad,0x124,0x3fe); assert(!find_in(bad,e));
}
