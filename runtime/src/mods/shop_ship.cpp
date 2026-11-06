// Beedle's Shop Ship: the stored sea room made sure of while Link is inside (shop_ship.h).
// From Nycolazs/WindWakerHD-Android-Plus f9896fb and f001ff5 via GreenNaugahyde/ZeldaWWHDRecompAndroid v0.9.9
// (db65d60, runtime/src/mods/shop_ship.cpp), MPL-2.0; here from the frame's mods service instead of instruction hooks.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"
#include "shop_ship.h"

namespace mods {
namespace {
const uint32_t kPlay = GD(0x1046F0B0);       // g_dComIfG_gameInfo.play (dComIfG_getGameInfo 025200D4)
constexpr uint32_t kStartStage = 0x5134;     // dStage_startStage_c: name[8] (the current stage)
constexpr uint32_t kShipRoom = 0x5D16;       // s8: the sea room of the ship entered (IkadaShipBeforeRoomId)
constexpr uint32_t kShipId = 0x5D17;         // u8: that ship's id in its room
constexpr uint32_t kShipX = 0x5D18, kShipZ = 0x5D20;  // f32: the ship's position when entered

std::string current_stage() {
    std::string s;
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t ch = ld8(kPlay + kStartStage + i);
        if (!ch) break;
        s.push_back(char(ch));
    }
    return s;
}
}  // namespace

// game main thread, frame start (interp.cpp): exact guest values, the other threads idle
void shop_ship_service() {
    const std::string stage = current_stage();
    if (!shop_ship::ship_stage(stage)) return;
    // test aid: WWHD_TEST_SHIP=room[:id[:x:z]] stands for the ship entered from the sea (its sea room, its id in that
    // room, its position), once per stay in a ship (a shop reached by WWHD_TEST_WARP_AT has none); room -1 is the
    // invalid stored room the halt reports had
    static bool tested = false;
    static std::string tested_in;
    if (const char* e = getenv("WWHD_TEST_SHIP"); e && !(tested && tested_in == stage)) {
        int room = 0, id = 0;
        float x = 0, z = 0;
        const int n = sscanf(e, "%d:%d:%f:%f", &room, &id, &x, &z);
        if (n >= 1) {
            LOG("[ship] test: the ship entered in sea room %d (stored: %d, %d)", room, (int)(int8_t)ld8(kPlay + kShipRoom),
                (int)ld8(kPlay + kShipId));
            st8(kPlay + kShipRoom, uint8_t(room));
            if (n >= 2) st8(kPlay + kShipId, uint8_t(id));
            if (n >= 4) stf32(kPlay + kShipX, x), stf32(kPlay + kShipZ, z);
        }
        tested = true, tested_in = stage;
    }
    // test only: WWHD_SHIP_REPAIR=0 keeps the stored room as it is (the halt on the way out, as before)
    static const bool off = getenv("WWHD_SHIP_REPAIR") && !strcmp(getenv("WWHD_SHIP_REPAIR"), "0");
    if (off) return;
    uint8_t room = 0;
    const double x = ldf32(kPlay + kShipX), z = ldf32(kPlay + kShipZ);
    if (!shop_ship::repair(ld8(kPlay + kShipRoom), x, z, room)) return;
    LOG("[ship] in %s without the ship's sea room (%d): room %d, from the ship's position %.0f %.0f", stage.c_str(),
        (int)(int8_t)ld8(kPlay + kShipRoom), (int)room, x, z);
    st8(kPlay + kShipRoom, room);
}
}  // namespace mods
