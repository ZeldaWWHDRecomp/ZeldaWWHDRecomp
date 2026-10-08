// Portable save state format (runtime/src/portable_state.h): write and read back, version mismatch,
// checksums (file and save data), the size guard and the no-binary-blob guard.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "portable_state.h"
#include "full_state_header.h"

using namespace pstate;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);     \
            failures++;                                                 \
        }                                                               \
    } while (0)

static State sample() {
    State s;
    s.title_id = "0005000010143500";
    s.title_version = 0;
    s.game_hash = "0123456789abcdef";
    s.runtime = "v0.2.6 (abc1234)";
    s.created = "2026-10-08 14:03:11";
    s.file_slot = 1;
    s.controller = 2;
    s.player_name = "Link";
    s.stage = "sea";
    s.start_point = 0;
    s.start_room = 44;
    s.layer = -1;
    s.room = 44;
    s.pos[0] = -199836.5f;
    s.pos[1] = 0.25f;
    s.pos[2] = 315424.125f;
    s.angle_y = -16384;
    s.link_proc = 0x88;
    s.on_ship = true;
    s.has_ship = true;
    s.ship_pos[0] = -199830.25f;
    s.ship_pos[1] = -12.5f;
    s.ship_pos[2] = 315420.0f;
    s.ship_angle_y = 12345;
    s.time_of_day = 187.5f;
    s.date = 12;
    s.savedata.assign(kSaveDataSize, 0);
    for (size_t i = 0; i < kSaveDataUsed; i++) s.savedata[i] = (uint8_t)(i * 7 + 3);
    seal_savedata(s.savedata);
    s.hd_player.assign(kHdPlayerSize, 1);
    s.hd_status.assign(kHdStatusSize, 2);
    s.hd_event.assign(kHdEventSize, 3);
    s.hd_map.assign(kHdMapSize, 4);
    return s;
}

static std::string replace(std::string s, const std::string& a, const std::string& b) {
    size_t p = s.find(a);
    assert(p != std::string::npos);
    return s.replace(p, a.size(), b);
}

// recomputes the checksum line so a test can change a field and still pass the file checksum
static std::string reseal(std::string text) {
    size_t p = text.find("checksum = ");
    assert(p != std::string::npos);
    std::string body = text.substr(0, p);
    char ck[48];
    snprintf(ck, sizeof ck, "checksum = crc32:%08X\n", crc32(body.data(), body.size()));
    return body + ck;
}

int main() {
    std::string why;
    // ---- write and read back
    State s = sample();
    std::string text = write(s, why);
    CHECK(!text.empty());
    CHECK(text.size() < 8 * 1024);  // a few KB
    printf("sample file: %zu bytes\n", text.size());
    State r;
    CHECK(read(text, r, why));
    CHECK(r.title_id == s.title_id && r.runtime == s.runtime && r.created == s.created);
    CHECK(r.controller == 2);
    bool pro = false; // switched to GamePad after saving
    pro = ss::restored_pro_controller(r.controller, pro);
    CHECK(pro);
    State legacy;
    CHECK(read(reseal(replace(text, "controller = 2\n", "")), legacy, why));
    CHECK(legacy.controller == 0);
    CHECK(!ss::restored_pro_controller(legacy.controller, false));
    CHECK(ss::restored_pro_controller(legacy.controller, true));
    for (const char* invalid : {"-1", "3", "Pro", "999999999999999999999"}) {
        State x;
        CHECK(!read(reseal(replace(text, "controller = 2", std::string("controller = ") + invalid)), x, why));
    }
    State bad_mode = s;
    bad_mode.controller = 3;
    CHECK(write(bad_mode, why).empty());
    CHECK(r.file_slot == 1 && r.player_name == "Link" && r.stage == "sea");
    CHECK(r.start_point == 0 && r.start_room == 44 && r.layer == -1 && r.room == 44);
    CHECK(r.pos[0] == s.pos[0] && r.pos[1] == s.pos[1] && r.pos[2] == s.pos[2]);  // exact (9 digits)
    CHECK(r.angle_y == -16384 && r.link_proc == 0x88 && r.on_ship);
    CHECK(r.has_ship && r.ship_angle_y == 12345 && r.ship_pos[0] == s.ship_pos[0] && r.ship_pos[1] == s.ship_pos[1] &&
          r.ship_pos[2] == s.ship_pos[2]);
    {
        State x;  // on the boat without a boat: refused
        CHECK(!read(reseal(replace(text, "has_ship = 1", "has_ship = 0")), x, why));
    }
    CHECK(r.time_of_day == 187.5f && r.date == 12);
    CHECK(r.savedata == s.savedata && r.hd_player == s.hd_player && r.hd_status == s.hd_status &&
          r.hd_event == s.hd_event && r.hd_map == s.hd_map);
    // CRLF line ends (a file passed through a Windows editor or mail client) still read
    {
        std::string crlf;
        for (char c : text) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        // the checksum covers the bytes as written: CRLF changes it, so the reader must refuse it
        State x;
        CHECK(!read(crlf, x, why));
        CHECK(why.find("checksum") != std::string::npos);
    }

    // ---- version mismatch
    {
        State x;
        CHECK(!read(reseal(replace(text, "format = 1", "format = 2")), x, why));
        CHECK(why.find("format version 2") != std::string::npos);
    }
    // ---- file checksum: any change is refused
    {
        State x;
        CHECK(!read(replace(text, "room = 44", "room = 45"), x, why));
        CHECK(why.find("checksum") != std::string::npos);
        CHECK(!read(text.substr(0, text.size() / 2), x, why));  // truncated
        CHECK(!read(text + "extra = 1\n", x, why));               // data after the checksum
    }
    // ---- save data checksum (the game's own pair at +0xA8C)
    {
        State bad = sample();
        bad.savedata[0x10] ^= 0xFF;  // changed after sealing
        std::string t = write(bad, why);
        CHECK(!t.empty());
        State x;
        CHECK(!read(t, x, why));
        CHECK(why.find("save data checksum") != std::string::npos);
    }
    // ---- wrong blob sizes are refused on write
    {
        State bad = sample();
        bad.savedata.resize(0x10000);  // a memory dump must never fit
        CHECK(write(bad, why).empty());
        CHECK(why.find("savedata") != std::string::npos);
        bad = sample();
        bad.hd_map.push_back(0);
        CHECK(write(bad, why).empty());
    }
    // ---- guard: unknown fields, oversized fields and files over the limit
    {
        CHECK(blob_check(text, why));
        std::string big(40000, 'a');
        std::string t = reseal(replace(text, "date = 12\n", "date = 12\nmemory = " + big + "\n"));
        State x;
        CHECK(!blob_check(t, why));
        CHECK(!read(t, x, why));
        t = reseal(replace(text, "player_name = Link", "player_name = " + std::string(200, 'x')));
        CHECK(!blob_check(t, why));
        // a file over 64 KB is refused before anything else
        std::string huge = text;
        while (huge.size() <= kMaxFileSize) huge = "# padding " + std::string(1000, '-') + "\n" + huge;
        CHECK(!blob_check(huge, why));
        CHECK(why.find("larger") != std::string::npos);
        CHECK(!read(huge, x, why));
        // a field repeated (to smuggle a second save data block) is refused
        size_t p = text.find("hd_map = ");
        std::string line = text.substr(p, text.find('\n', p) - p + 1);
        t = reseal(replace(text, line, line + line));
        CHECK(!read(t, x, why));
    }
    // ---- long player names and control characters are cleaned on write
    {
        State x = sample();
        x.player_name = "Li\nnk\x01";
        std::string t = write(x, why);
        State y;
        CHECK(read(t, y, why) && y.player_name == "Link");
    }
    // ---- CRC-32 reference value
    CHECK(crc32("123456789", 9) == 0xCBF43926u);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("portable_state_test: all passed\n");
    return 0;
}
