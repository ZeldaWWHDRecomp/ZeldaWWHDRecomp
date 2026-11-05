// Save file names up to 16 characters (runtime/src/mods/long_name.h), the pure parts:
//  - the whole name's records in a Quest Log's area: written, read back, replaced, erased; other records kept;
//    no room or foreign data refused; damaged records read as none;
//  - holds(): the whole name only with the save's own first 8 characters;
//  - fix_saved_names(): a 9-character name without an end (what the game saves while the whole name is in) cut
//    to 8 with its end and the CRC made again (the game's CRC counts such a name with the whole name's length);
//    8-character names and files with a wrong CRC left alone. Also the game's real save of a 16-character name,
//    when an argument names it (a folder with game16.sav).
// With an argument (a folder) it also writes the fixed fixture there, for tools/savegame/wwsave.py to check.
#include "../src/mods/long_name.h"

#include <cstdio>
#include <vector>

using namespace long_name;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

namespace {
// a cking.sav as the game writes it: names in the HD section (9-character ones without an end), the CRC
std::vector<uint8_t> save_file(const std::u16string names[3]) {
    std::vector<uint8_t> f(kFileSize, 0);
    for (size_t i = 3 * kSlot; i < names_at(); i++) f[i] = uint8_t(i * 13);
    for (int s = 0; s < 3; s++)
        for (size_t i = 0; i < names[s].size() && i < 9; i++) {
            f[names_at() + 18 * size_t(s) + 2 * i] = uint8_t(names[s][i] >> 8);
            f[names_at() + 18 * size_t(s) + 2 * i + 1] = uint8_t(names[s][i]);
        }
    story_block::put32(&f[kFileSize - 8], 4);
    // the game counts a 9-character name (the whole name's object) with the whole name's length + 1 bytes (seen: 17)
    size_t counts[3] = {0, 0, 0};
    for (int s = 0; s < 3; s++)
        if (names[s].size() >= 9) counts[s] = 17;
    story_block::put32(&f[kFileSize - 4], extra_crc(f.data(), counts));
    return f;
}
}  // namespace

int main(int argc, char** argv) {
    {  // records
        std::vector<uint8_t> area(story_block::kAreaSize, 0);
        CHECK(read(area.data()).empty());
        CHECK(write(area.data(), u"Abahbob the Brave"));  // 17: kept to 16
        CHECK(read(area.data()) == u"Abahbob the Brav");
        story_mail::Book b(area.data(), story_block::kAreaRecords);
        CHECK(b.set(story_mail::kRecordQuest, 0x1234, 7));  // a story mod's record
        CHECK(write(area.data(), u"Tetra the Pirate"));
        CHECK(read(area.data()) == u"Tetra the Pirate");
        CHECK(b.get(story_mail::kRecordQuest, 0x1234) == 7);
        CHECK(write(area.data(), u"Élodie-Françoise"));  // Latin letters beyond ASCII
        CHECK(read(area.data()) == u"Élodie-Françoise");
        CHECK(write(area.data(), u"Short"));  // 8 or fewer: no record
        CHECK(read(area.data()).empty() && b.get(story_mail::kRecordQuest, 0x1234) == 7);
        CHECK(b.free_records() == story_block::kAreaRecords - 1);
        // a damaged record set (a character missing) reads as none
        CHECK(write(area.data(), u"Abahbob the Brave"));
        b.erase(story_mail::kRecordLongName, 5);
        CHECK(read(area.data()).empty());
        // no room: refused, and no half name left
        std::vector<uint8_t> full(story_block::kAreaSize, 0);
        story_mail::Book fb(full.data(), story_block::kAreaRecords);
        for (uint16_t k = 0; fb.free_records() > 10; k++) fb.set(story_mail::kRecordQuest, k, 1);
        CHECK(!write(full.data(), u"Abahbob the Brave"));
        CHECK(read(full.data()).empty());
        // data this port did not write: left alone
        std::vector<uint8_t> foreign(story_block::kAreaSize, 0x5A);
        CHECK(!write(foreign.data(), u"Abahbob the Brave"));
        CHECK(foreign[0] == 0x5A && foreign[100] == 0x5A);
    }
    {  // holds
        CHECK(holds(u"Abahbob the Brav", u"Abahbob "));
        CHECK(!holds(u"Abahbob the Brav", u"Abahbob"));   // the save has another name now
        CHECK(!holds(u"Abahbob the Brav", u"Tetra   "));
        CHECK(!holds(u"Short", u"Short"));
        CHECK(!holds(u"", u""));
    }
    {  // the game's save while the whole name was in
        const std::u16string names[3] = {u"Abahbob t", u"Link", u""};  // file 1: 9 characters, no end
        std::vector<uint8_t> f = save_file(names);
        CHECK(own_name(f.data(), 0) == u"Abahbob t");
        const std::vector<uint8_t> before = f;
        CHECK(fix_saved_names(f.data(), f.size()) == 1);
        CHECK(own_name(f.data(), 0) == u"Abahbob " && own_name(f.data(), 1) == u"Link" && own_name(f.data(), 2).empty());
        CHECK(story_block::be32(&f[kFileSize - 4]) == extra_crc(f.data()));
        size_t changed = 0;
        for (size_t i = 0; i < f.size(); i++)
            if (f[i] != before[i]) {
                const bool name = i >= names_at() + 2 * kOwn && i < names_at() + 18;
                CHECK(name || i >= kFileSize - 4);
                changed++;
            }
        CHECK(changed > 0);
        CHECK(fix_saved_names(f.data(), f.size()) == 0);  // nothing more to do
        if (argc > 1) {
            const std::string path = std::string(argv[1]) + "/long_name_fixed.sav";
            if (FILE* out = std::fopen(path.c_str(), "wb")) {
                std::fwrite(f.data(), 1, f.size(), out);
                std::fclose(out);
            }
        }
        // a wrong CRC (not a file as the game writes it): left alone
        std::vector<uint8_t> g = save_file(names);
        g[kFileSize - 1] ^= 1;
        const std::vector<uint8_t> g0 = g;
        CHECK(fix_saved_names(g.data(), g.size()) == 0 && g == g0);
        // too short
        CHECK(fix_saved_names(g.data(), 100) == 0);
    }
    if (g_failed) {
        std::fprintf(stderr, "long_name_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("long_name_test: ok\n");
    return 0;
}
