// SPDX-License-Identifier: MPL-2.0
// Codecs for the structured messages (protocol.h lists them all).
#pragma once
#include <string>
#include <vector>

#include "protocol.h"
#include "wire.h"

namespace online::proto {

// ---- POSE_BATCH entries ----
// near: u16 id, u8 0, u32 sender ms, PoseHeader, u16 n, blob
// mid:  u16 id, u8 1, s24 x, s24 z, s16 y/2, u16 yaw, u16 anim, u8 frame, u8 flags, u8 boat, u16 sender ms low
// far:  u16 id, u8 2, s24 x, s24 z, u8 yaw/256, u8 flags
constexpr size_t kMidEntryBytes = 2 + 1 + 3 + 3 + 2 + 2 + 2 + 1 + 1 + 1 + 2;  // 20
constexpr size_t kFarEntryBytes = 2 + 1 + 3 + 3 + 1 + 1;                      // 11
constexpr size_t kMarkerBytes = 6;

struct PoseEntry {
    uint16_t id = 0;
    Tier tier = kTierNear;
    uint32_t ms = 0;  // near: full sender ms; mid: low 16 bits only
    PoseHeader head;  // near: exact; mid/far: rebuilt from the compact fields (stage/room not carried)
    Bytes blob;       // near only
};

void put_near(Writer& w, uint16_t id, uint32_t ms, const PoseHeader& h, const uint8_t* blob, size_t n);
void put_mid(Writer& w, uint16_t id, uint32_t ms, const PoseHeader& h);
void put_far(Writer& w, uint16_t id, const PoseHeader& h);
// decodes a whole POSE_BATCH body
bool get_pose_batch(const uint8_t* d, size_t n, std::vector<PoseEntry>& out);

void put_marker(Writer& w, const Marker& m);
bool get_markers(const uint8_t* d, size_t n, std::vector<Marker>& out);

// ---- PLAYER_IN ----
struct PlayerIn {
    uint16_t id = 0;
    uint64_t key_hash = 0;
    uint32_t look = 0;
    uint8_t flags = 0;  // bit 0: party member, bit 1: friend (name is the display name), bit 2: phone
    std::string name;   // display name for party/friends, "Sailor 7Q2F" for strangers
};
Bytes encode_player_in(const PlayerIn& p);
bool decode_player_in(const uint8_t* d, size_t n, PlayerIn& p);

// ---- team progress ----
void put_team_fields(Writer& w, const std::vector<TeamField>& f);
bool get_team_fields(Reader& r, std::vector<TeamField>& out);

// ---- app ----
Bytes encode_app(const AppHeader& h, const uint8_t* body, size_t n);
bool decode_app(const uint8_t* d, size_t n, AppHeader& h, const uint8_t*& body, size_t& body_n);

// ---- party ----
struct PartyMember {
    uint16_t id = 0;
    uint64_t key_hash = 0;
    std::string name;
    bool online = true;
};
struct PartyState {
    uint32_t party = 0;  // 0: not in a party
    uint16_t leader = 0;
    uint16_t flags = 0;
    std::vector<PartyMember> members;
};
Bytes encode_party_state(const PartyState& s);
bool decode_party_state(const uint8_t* d, size_t n, PartyState& s);

struct RunnerEntry {
    Where where;
    uint16_t runner = 0;  // 0: nobody (place left)
};
Bytes encode_runners(const std::vector<RunnerEntry>& r);
bool decode_runners(const uint8_t* d, size_t n, std::vector<RunnerEntry>& out);

// the generated stranger name: "Sailor 7Q2F" from the key hash
std::string stranger_name(uint64_t key_hash);

}  // namespace online::proto
