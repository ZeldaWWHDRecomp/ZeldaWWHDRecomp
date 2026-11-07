// SPDX-License-Identifier: MPL-2.0
// Online co-op's stage codes (runtime/src/mods/coop_names.h clean_stage): the game's char[8] stage name, filled with
// strcpy, made the clean code the co-op messages and the server use (the name, then zeros). Checked against the
// server's own rule (online/proto get_where: printable ASCII, zero padded).
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mods/coop_names.h"
#include "proto/protocol.h"
#include "proto/wire.h"

using mods::coop::names::clean_stage;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

// the server's verdict on a pose header with this stage code
static bool server_takes(const char code[8]) {
    online::proto::PoseHeader h;
    std::memcpy(h.where.stage.data(), code, 8);
    online::proto::Bytes b;
    online::proto::Writer w(b);
    online::proto::put_pose_header(w, h);
    online::proto::Reader r(b.data(), b.size());
    (void)online::proto::get_pose_header(r);
    return r.ok();
}

int main() {
    char out[8];
    // "sea" written with strcpy over "M_NewD2": the old name's bytes after the NUL
    uint8_t raw[8];
    std::memcpy(raw, "M_NewD2", 8);
    std::strcpy(reinterpret_cast<char*>(raw), "sea");
    CHECK(!server_takes(reinterpret_cast<const char*>(raw)));  // (what the client sent before the fix)
    clean_stage(raw, out);
    CHECK(std::memcmp(out, "sea\0\0\0\0\0", 8) == 0);
    CHECK(server_takes(out));
    // a full 7-character name stays as it is
    std::memcpy(raw, "M_NewD2", 8);
    clean_stage(raw, out);
    CHECK(std::memcmp(out, "M_NewD2", 8) == 0 && server_takes(out));
    // 8 characters, no NUL: all kept (the server allows it)
    std::memcpy(raw, "ABCDEFGH", 8);
    clean_stage(raw, out);
    CHECK(std::memcmp(out, "ABCDEFGH", 8) == 0 && server_takes(out));
    // an unprintable byte ends the name
    const uint8_t odd[8] = {'s', 'e', 'a', 0x01, 'x', 0, 0, 0};
    clean_stage(odd, out);
    CHECK(std::memcmp(out, "sea\0\0\0\0\0", 8) == 0 && server_takes(out));
    // empty
    std::memset(raw, 0, 8);
    clean_stage(raw, out);
    CHECK(std::memcmp(out, "\0\0\0\0\0\0\0\0", 8) == 0);
    if (g_fail) return 1;
    std::printf("coop_names_test: ok\n");
    return 0;
}
