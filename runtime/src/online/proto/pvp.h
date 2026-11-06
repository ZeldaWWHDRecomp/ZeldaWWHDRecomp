// SPDX-License-Identifier: MPL-2.0
// PvP at sea (C28; docs/online-server.md, "PvP at sea"): the rules a server announces and the messages of a fight.
// The server validates every hit and decides protection, safe zones and the rupee loss of a knockout; the games
// show and apply what it relays.
#pragma once
#include <cstdint>
#include <vector>

#include "wire.h"

namespace online::proto {

enum class PvpMode : uint8_t { kOff = 0, kOptIn = 1, kOn = 2 };

// a safe zone: a circle in a sea square, its centre given from the square's centre
struct PvpZone {
    uint8_t square = 0;
    float cx = 0, cz = 0, radius = 15000;
};

struct PvpRules {
    PvpMode mode = PvpMode::kOptIn;
    uint32_t protect_ms = 12000;      // protection after leaving a safe zone and after a knockout
    uint32_t enter_delay_ms = 10000;  // entering a zone within 10 s of a fight: safe only after this long
    float hide_band = 8000;           // PvP players in this band outside a zone are hidden from strangers' far view
    uint8_t ko_rupee_percent = 10;    // of the victim's wallet, at most ko_rupee_max; the rupees vanish
    uint16_t ko_rupee_max = 100;
    float max_hit_dist = 3000;        // attacker to victim
    uint8_t cooldown_kos = 2;         // this many knockouts of one victim by one attacker within the window ...
    uint16_t cooldown_window_s = 180;
    uint16_t cooldown_s = 300;        // ... and that attacker's hits on that victim are dropped this long
    std::vector<PvpZone> zones;
};
// the default safe zones: these squares' centres, radius 15,000 (a server can add, move or remove them)
std::vector<PvpZone> default_pvp_zones();

void put_pvp_rules(Writer& w, const PvpRules& r);
bool get_pvp_rules(Reader& r, PvpRules& out);

enum PvpKind : uint8_t { kPvpCannon = 1, kPvpArrow = 2, kPvpBomb = 3, kPvpBoomerang = 4, kPvpSword = 5 };

struct PvpHit {
    uint16_t other = 0;  // C->S: the victim; S->C: the attacker
    uint8_t kind = 0, damage = 0;  // damage in quarter hearts
    float x = 0, y = 0, z = 0;     // the hit point
};
void put_pvp_hit(Writer& w, const PvpHit& h);
bool get_pvp_hit(Reader& r, PvpHit& h);

struct PvpShot {
    uint16_t shooter = 0;  // S->C only
    uint8_t kind = 0;
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
};
void put_pvp_shot(Writer& w, const PvpShot& s, bool with_shooter);
bool get_pvp_shot(Reader& r, PvpShot& s, bool with_shooter);

// PVP_STATE flags
enum PvpStateFlags : uint8_t {
    kPvpActive = 1,      // PvP is on for me here
    kPvpProtected = 2,
    kPvpInSafeZone = 4,
    kPvpEntering = 8,    // in a safe zone, but its protection waits (a fight in the last 10 s)
    kPvpForcedOn = 16,   // the server's mode is "on": the game hides its toggle
};

void put_s24(Writer& w, float v);
float get_s24(Reader& r);

}  // namespace online::proto
