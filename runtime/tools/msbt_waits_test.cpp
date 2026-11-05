// No pauses in texts (runtime/src/mods/msbt_waits.h): the waits of a message file set to 0 and put back.
// Fixture files: an MSBT header, a LBL1-like section before TXT2 (skipped), TXT2 strings with waits of various
// lengths, other tags (a colour tag of 4 bytes, a group-1 tag 6 with another size, a closing tag), a wait of 0
// frames, a string cut short, and files that are not message files.
#include "../src/mods/msbt_waits.h"

#include <cstdio>
#include <vector>

using namespace mods::msbt;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

namespace {
using Bytes = std::vector<uint8_t>;
void w16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
void w32(Bytes& b, uint32_t v) { w16(b, uint16_t(v >> 16)); w16(b, uint16_t(v)); }
void pad16(Bytes& b) { while (b.size() % 16) b.push_back(0xAB); }
Bytes text(const char* s) { Bytes b; for (; *s; s++) w16(b, uint16_t(*s)); return b; }
Bytes wait_code(uint16_t frames) { Bytes b; w16(b, 0x0E); w16(b, 1); w16(b, 6); w16(b, 2); w16(b, frames); return b; }
Bytes cat(std::initializer_list<Bytes> parts) { Bytes b; for (auto& p : parts) b.insert(b.end(), p.begin(), p.end()); return b; }

// an MSBT holding the strings (each ends with 0); a small other section first
Bytes msbt(const std::vector<Bytes>& strings) {
    Bytes f;
    for (char c : kMagic) f.push_back(uint8_t(c));
    w16(f, 0xFEFF); w16(f, 0); w16(f, 0x0103); w16(f, 2);  // BOM, -, version, sections
    w16(f, 0); w32(f, 0);                                   // -, size (later)
    while (f.size() < 0x20) f.push_back(0);
    // "LBL1" with bytes that look like a wait: never touched
    f.insert(f.end(), {'L', 'B', 'L', '1'}); w32(f, 10); w32(f, 0); w32(f, 0);
    Bytes fake = wait_code(30);
    f.insert(f.end(), fake.begin(), fake.end());
    pad16(f);
    Bytes data;
    w32(data, uint32_t(strings.size()));
    uint32_t at = 4 + 4 * uint32_t(strings.size());
    for (auto& s : strings) { w32(data, at); at += uint32_t(s.size()) + 2; }
    for (auto& s : strings) { data.insert(data.end(), s.begin(), s.end()); w16(data, 0); }
    f.insert(f.end(), {'T', 'X', 'T', '2'}); w32(f, uint32_t(data.size())); w32(f, 0); w32(f, 0);
    f.insert(f.end(), data.begin(), data.end());
    pad16(f);
    const uint32_t size = uint32_t(f.size());
    f[0x12] = uint8_t(size >> 24); f[0x13] = uint8_t(size >> 16); f[0x14] = uint8_t(size >> 8); f[0x15] = uint8_t(size);
    return f;
}
Bytes colour() { Bytes b; w16(b, 0x0E); w16(b, 0); w16(b, 3); w16(b, 4); w16(b, 0x0001); w16(b, 0x0006); return b; }
Bytes other_tag6() { Bytes b; w16(b, 0x0E); w16(b, 1); w16(b, 6); w16(b, 4); w16(b, 7); w16(b, 8); return b; }
Bytes closing() { Bytes b; w16(b, 0x0F); w16(b, 0); w16(b, 3); return b; }
}  // namespace

int main() {
    const std::vector<Bytes> strings = {
        cat({text("Hi"), wait_code(15), text("there"), wait_code(1)}),
        cat({colour(), text("red"), closing(), wait_code(60), text(".")}),
        cat({other_tag6(), text("x"), wait_code(0)}),  // a tag 6 of another size and a wait of 0 frames stay
        text("no waits"),
    };
    const Bytes original = msbt(strings);
    {  // all waits of the TXT2 strings to 0, the game's values kept, everything else the same
        Bytes f = original;
        Saved saved;
        CHECK(file_size(f.data(), f.size()) == f.size());
        CHECK(patch(f.data(), f.size(), saved) == 3);
        CHECK(saved.size() == 3);
        CHECK(saved[0].second == 15 && saved[1].second == 1 && saved[2].second == 60);
        size_t changed = 0;
        for (size_t i = 0; i < f.size(); i++) changed += f[i] != original[i];
        CHECK(changed == 3);  // the low bytes of 15, 1 and 60 (their high bytes were 0 already)
        for (auto& [off, v] : saved) CHECK(is_wait_frames(f.data(), off) && be16(f.data() + off) == 0);
        Saved again;
        CHECK(patch(f.data(), f.size(), again) == 0);  // a second patch finds nothing to do
        CHECK(restore(f.data(), f.size(), saved) == 3);
        CHECK(f == original);
    }
    {  // a wait the game changed since (not a 0 any more) or that is no wait any more stays as it is
        Bytes f = original;
        Saved saved;
        patch(f.data(), f.size(), saved);
        f[saved[0].first + 1] = 9;
        f[saved[1].first - 8 + 1] = 0x0D;  // the wait code itself overwritten
        CHECK(restore(f.data(), f.size(), saved) == 1);
        CHECK(be16(f.data() + saved[2].first) == 60);
        CHECK(be16(f.data() + saved[0].first) == 9);
    }
    {  // not a message file: little endian, size past the readable bytes, no magic, too short
        Bytes f = original;
        f[8] = 0xFF; f[9] = 0xFE;
        Saved saved;
        CHECK(patch(f.data(), f.size(), saved) == 0 && saved.empty());
        f = original;
        CHECK(file_size(f.data(), f.size() - 1) == 0);
        f[0] = 'X';
        CHECK(file_size(f.data(), f.size()) == 0);
        CHECK(file_size(original.data(), 0x10) == 0);
    }
    {  // a string cut short in the middle of a wait: nothing beyond the section is read or written
        Bytes s = cat({text("ab"), wait_code(20)});
        s.resize(s.size() - 2);  // the frames missing: the next bytes are the terminator
        Bytes f = msbt({s});
        Saved saved;
        CHECK(patch(f.data(), f.size(), saved) == 0);
    }
    if (g_failed) {
        std::fprintf(stderr, "msbt_waits_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("msbt_waits_test: ok\n");
    return 0;
}
