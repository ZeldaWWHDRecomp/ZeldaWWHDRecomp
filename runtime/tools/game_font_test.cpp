// Tests the name font reader (overlay/game_font.cpp) on a synthetic font: the CMAP block chain with
// its three mapping methods, and a missing game folder (null: the text prompt offers everything).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "overlay/game_font.h"
namespace config { std::string game_dir = "/nonexistent-wwhd-game", save_dir; }
void log_msg(const char*, ...) {}
static void be16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = x >> 8; v[at + 1] = x & 255; }
static void be32(std::vector<uint8_t>& v, size_t at, uint32_t x) { be16(v, at, x >> 16); be16(v, at + 2, x & 0xFFFF); }
int main() {
    // header (0x14) + FINF (0x20) + three CMAP blocks
    std::vector<uint8_t> f(0x200, 0);
    memcpy(&f[0], "FFNT", 4); f[4] = 0xFE; f[5] = 0xFF; be16(f, 6, 0x14);
    memcpy(&f[0x14], "FINF", 4);
    const size_t c1 = 0x40, c2 = 0x80, c3 = 0xC0;
    be32(f, 0x14 + 0x1C, c1 + 8);
    auto cmap = [&](size_t at, uint16_t first, uint16_t last, uint16_t method, size_t next) {
        memcpy(&f[at], "CMAP", 4); be16(f, at + 8, first); be16(f, at + 10, last); be16(f, at + 12, method);
        be32(f, at + 16, next ? next + 8 : 0);
    };
    cmap(c1, 'A', 'C', 0, c2);                                      // direct: A B C
    cmap(c2, 'a', 'c', 1, c3);                                      // table: a, (no b), c
    be16(f, c2 + 20, 0); be16(f, c2 + 22, 0xFFFF); be16(f, c2 + 24, 1);
    cmap(c3, 0, 0xFFFF, 2, 0);                                      // scan: あ 中
    be16(f, c3 + 20, 2); be16(f, c3 + 22, 0x3042); be16(f, c3 + 24, 5); be16(f, c3 + 26, 0x4E2D); be16(f, c3 + 28, 6);
    f[0x14 + 9] = 64; f[0x14 + 10] = 48;
    game_font::Metrics metrics;
    game_font::Glyphs g;
    assert(game_font::parse_bffnt(f.data(), f.size(), g, &metrics));
    assert(metrics.width == 48 && metrics.height == 64);
    assert(g.size() == 7 && g.count('A') && g.count('C') && g.count('a') && !g.count('b') && g.count('c') && g.count(0x3042) && g.count(0x4E2D));
    f[0] = 'X';
    game_font::Glyphs none;
    assert(!game_font::parse_bffnt(f.data(), f.size(), none));
    assert(!game_font::name_glyphs(1));  // no game files: the full set
    puts("game_font_test: CMAP direct / table / scan, bad data, missing game passed");
}
