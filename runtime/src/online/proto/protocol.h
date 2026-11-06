// SPDX-License-Identifier: MPL-2.0
// The online protocol: constants, message types and the fixed-size records shared by the game client, the server
// and the bots (docs/online-server.md has the wire description).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "wire.h"

namespace online::proto {

// the server protocol version; independent of the direct-UDP co-op protocol of the game's own link
constexpr uint16_t kProtoVersion = 3;  // 2: gameplay mods in HELLO, server mod rules; 3: party size per server
constexpr uint16_t kDefaultPort = 7777;

// datagram budget: no datagram is larger, so nothing fragments at the IP layer on common paths
constexpr size_t kMaxDatagram = 1200;
constexpr size_t kHeaderBytes = 17;        // kind 1, connection id 4, packet number 4, ack 4, ack bits 4
constexpr size_t kTagBytes = 16;
constexpr size_t kMaxPoseBlob = 1100;      // the game's opaque pose payload (the client caps it)
constexpr size_t kMaxAppBody = 1100;       // one opaque app message
constexpr size_t kMaxStream = 64 * 1024;   // one reliable message split in pieces (join / team state)
constexpr size_t kPieceBytes = 1000;       // piece size of a split reliable message

// the first byte of every datagram
enum PacketKind : uint8_t {
    kPktConnect = 0xA1,    // C->S, plain: asks for a challenge (padded, so the reply is never larger)
    kPktChallenge = 0xA2,  // S->C, plain: salt, difficulty, server key, server info
    kPktHello = 0xA3,      // C->S, plain + signed: proto, game data, keys, proof-of-work, name, resume token
    kPktWelcome = 0xA4,    // S->C, sealed with the new session keys
    kPktRefuse = 0xA5,     // S->C, plain: reason code + text
    kPktData = 0xA6,       // either way, sealed: the session's packets
    kPktRulesReq = 0xA7,   // C->S, plain, padded: a page of the server's mod rules, please
    kPktRules = 0xA8,      // S->C, plain, signed by the server: that page
};

enum class Refusal : uint8_t {
    kNone = 0,
    kProtocol,     // another protocol version
    kGameData,     // another game version / region / seed
    kFull,
    kBanned,
    kBadProof,     // proof-of-work or signature wrong
    kRateLimited,
    kInviteOnly,
    kMalformed,
    kShuttingDown,
    kKicked,
    kMods,         // gameplay mods: see the problem list (mods.h)
};
const char* refusal_text(Refusal r);

// how many leading zero bits a proof-of-work hash must have
uint32_t pow_zero_bits(const uint8_t hash[32]);
bool pow_check(const uint8_t salt[16], const uint8_t pub[32], uint64_t nonce, uint8_t difficulty);
uint64_t pow_solve(const uint8_t salt[16], const uint8_t pub[32], uint8_t difficulty);

// the 8-byte key hash that names a player on the server (first 8 bytes of hash(public key), little-endian)
uint64_t key_hash(const uint8_t pub[32]);
// "KQ7F-9MZ2-XD": 40 bits of the key hash plus a 10-bit check, Crockford base32
std::string friend_code(uint64_t key_hash);
// parses a friend code (dashes, spaces and case ignored; I/L read as 1, O as 0); false if the check fails.
// The result is the 40-bit code value (the key hash's low 40 bits).
bool parse_friend_code(std::string_view text, uint64_t& code40);
inline uint64_t code40_of(uint64_t key_hash) { return key_hash & 0xFFFFFFFFFFull; }

// ---- messages inside data packets ----
// Body of a data packet: u32 sender ms, u32 echo ms, u16 echo hold ms, then frames:
//   reliable:   0x01 | u16 seq | u8 type | u16 len | payload   (type bit 0x80: more pieces follow)
//   unreliable: 0x02 | u8 type | u16 len | payload
//   acks:       0x03 | u8 n | n x {u32 base (a multiple of 64), u64 bits}: packets received beyond the header's 33,
//               sent (last in a packet) only after a burst of more than 30 packets since the last flush
enum FrameKind : uint8_t { kFrameReliable = 1, kFrameUnreliable = 2, kFrameAcks = 3 };

enum Msg : uint8_t {
    // ---- client -> server ----
    kMsgPose = 1,         // unreliable: PoseHeader + opaque blob (<= kMaxPoseBlob)
    kMsgStage = 2,        // reliable: Where (the player went into a stage / room)
    kMsgApp = 3,          // reliable or unreliable: AppHeader + opaque body, relayed to a target
    kMsgEmote = 4,        // reliable: u8 emote id
    kMsgBlock = 5,        // reliable: u64 key hash
    kMsgUnblock = 6,      // reliable: u64 key hash
    kMsgReport = 7,       // reliable: u64 key hash, u8 reason
    kMsgBye = 8,          // reliable: leaving
    kMsgSwitchCopy = 9,   // reliable: go to another copy of the current crowded place
    kMsgPartyInvite = 10,   // reliable: u64 friend code (40-bit value)
    kMsgPartyAccept = 11,   // reliable: u32 party id
    kMsgPartyDecline = 12,  // reliable: u32 party id
    kMsgPartyLeave = 13,    // reliable
    kMsgPartySettings = 14, // reliable: u16 flags (leader only)
    kMsgProgress = 15,      // reliable: TeamField list, merged into the team state (union)
    kMsgTeamStateAsk = 16,  // reliable: please send me the team state
    kMsgPinRunner = 17,     // reliable: u8 on (leader pins enemy running to themselves)
    kMsgPing = 18,          // unreliable: keep-alive with nothing else to say
    kMsgJoinCopyOf = 19,    // reliable: u64 key hash: go to the copy that player is in (a friend's copy)
    kMsgPvpFlag = 23,       // reliable: u8 on, u8 busy (cutscene, talk, shop, menu, off the open sea: protected)
    kMsgPvpHit = 24,        // reliable: PvpHit (victim; pvp.h)
    kMsgPvpShot = 25,       // unreliable: PvpShot (a projectile fired, to show it)
    kMsgPvpKo = 26,         // reliable: u16 by, u16 wallet (the victim's game: knocked out by that player)
    kMsgMute = 21,          // reliable: u64 key hash (that player's emotes, effects, sounds and voice stop reaching you)
    kMsgUnmute = 22,        // reliable: u64 key hash
    kMsgPartyRestore = 20,  // reliable: PartyRecord (messages.h): after a fresh WELCOME (the server restarted),
                            // the party this game was in; the leader's own claim, or a record the leader signed,
                            // rebuilds it, and members with the same party id join. Then re-send your progress
                            // fields (the union merge fills what the server's last saved state lacks)

    // ---- server -> client ----
    kMsgPoseBatch = 64,   // unreliable: n x {u16 id, u8 tier, tier payload}
    kMsgPlayerIn = 65,    // reliable: u16 id, u64 key hash, u32 look, u8 flags, name
    kMsgPlayerOut = 66,   // reliable: u16 id
    kMsgMarkers = 67,     // unreliable: n x Marker
    kMsgEmoteOf = 68,     // reliable: u16 id, u8 emote
    kMsgAppFrom = 69,     // reliable or unreliable: u16 from id, u8 app type, body
    kMsgNotice = 70,      // reliable: u8 kind, u16 seconds, text
    kMsgCopy = 71,        // reliable: Where + u16 copy, u16 copies (where you were placed)
    kMsgPartyInvited = 72,  // reliable: u32 party id, u16 from id, u64 key hash, name
    kMsgPartyState = 73,    // reliable: u32 party id, u16 leader, u16 flags, n x {u16 id, u64 key hash, name}
    kMsgRunner = 74,        // reliable: n x {stage[8], s8 room, u16 runner id}
    kMsgTeamState = 75,     // reliable (split): u32 version, TeamField list
    kMsgPartyResult = 76,   // reliable: u8 code, text (invite failed, party full...)
    kMsgRedirect = 77,      // reliable: host text, u16 port, u8 ticket[32] (shard handover)
    kMsgPvpRules = 79,      // reliable: PvpRules (after WELCOME and when they change)
    kMsgPvpHitOn = 80,      // reliable: PvpHit (attacker): a validated hit on you
    kMsgPvpShotOf = 81,     // unreliable: PvpShot with the shooter
    kMsgPvpKoOf = 82,       // reliable: u16 victim, u16 by
    kMsgPvpRupees = 83,     // reliable: u16 amount to remove from the wallet (they vanish)
    kMsgPvpState = 84,      // reliable: u8 PvpStateFlags, u16 protection ms left (after WELCOME and on change)
    kMsgPvpMarks = 86,      // reliable: n x {u16 id, u8 flags (bit 0 PvP active, bit 1 protected)}: who of those you
                            // see near or mid can fight now (on change; PLAYER_IN flags bit 3 says it at first)
    kMsgPvpRespawn = 85,    // reliable: u8 square, s24 x, s24 z: after your knockout, the nearest safe zone's edge
    kMsgPartyWhere = 78,    // unreliable, 1 Hz: n x {u16 id, Where, u8 square (255: not at sea), s24 x, s24 z}
                            // where every online party member is, wherever they are
};

// Where a player is. `stage` is the game's internal stage code (up to 8 bytes, zero padded).
struct Where {
    std::array<char, 8> stage{};
    int8_t room = 0;
    uint8_t layer = 0;
    bool operator==(const Where&) const = default;
    std::string stage_name() const;
    static Where of(std::string_view stage, int room = 0, int layer = 0);
};
void put_where(Writer& w, const Where& p);
Where get_where(Reader& r);

// What the server needs to know about a pose: kept small and fixed, the game's own joints follow it opaquely.
struct PoseHeader {
    Where where;
    float x = 0, y = 0, z = 0;  // world position
    uint16_t yaw = 0;           // heading, full turn = 65536
    uint16_t anim = 0;          // animation id (mid tier: the cheap puppet plays it)
    uint8_t frame = 0;          // animation frame (0..255 of its length)
    uint8_t flags = 0;          // bit 0: on the boat, bit 1: sailing, bit 2: in a cutscene (shown idle), bit 3: keyframe
    uint8_t boat = 0;           // sail / steer state
    static constexpr size_t kBytes = 10 + 12 + 2 + 2 + 1 + 1 + 1;  // 29
};
void put_pose_header(Writer& w, const PoseHeader& h);
PoseHeader get_pose_header(Reader& r);

// interest tiers (DESIGN.md 3.3)
enum Tier : uint8_t { kTierNear = 0, kTierMid = 1, kTierFar = 2 };
// tier payloads inside a POSE_BATCH entry:
//   near: u32 sender ms, PoseHeader, u16 blob len, blob
//   mid (20 B): s16 x, s16 y, s16 z quantised to the receiver's window (units of 4), u16 yaw, u16 anim, u8 frame,
//               u8 flags, u8 boat, u8 square, u32 sender ms   (6+2+2+1+1+1+1+4 = 18; + id/tier = 21)
//   far (10 B): s16 x, s16 z (units of 16), u8 yaw (256 steps), u8 flags, u8 square, u8 sub-cell, u16 reserved
struct MidPose {
    int32_t x = 0, y = 0, z = 0;  // world units, after quantisation
    uint16_t yaw = 0, anim = 0;
    uint8_t frame = 0, flags = 0, boat = 0, square = 0;
    uint32_t ms = 0;
};
struct FarPose {
    int32_t x = 0, z = 0;
    uint8_t yaw = 0, flags = 0, square = 0, cell = 0;
};
// a sea-chart marker (6 B): u16 id, u8 square, u8 cell x (0..255 within the square), u8 cell z, u8 flags
struct Marker {
    uint16_t id = 0;
    uint8_t square = 0, x = 0, z = 0, flags = 0;
};

// app messages: the game's own vocabulary (opaque to the server), routed to a target
enum AppTarget : uint8_t {
    kToParty = 0,     // every other party member
    kToMember = 1,    // one party member (id follows)
    kToNear = 2,      // the players who see you at the near tier (effects, sounds)
    kToRunner = 3,    // the runner of the sender's current place (hits on enemies)
    kToPlace = 4,     // party members in the sender's place (stage + room)
};
struct AppHeader {
    uint8_t type = 0;   // the game's message type
    uint8_t target = kToParty;
    uint16_t member = 0;  // for kToMember
};

// team progress (P6): a field of the save data at an offset, merged as a union
enum FieldMode : uint8_t {
    kFieldBits = 0,  // OR, byte by byte (flags are only ever set)
    kFieldMax = 1,   // unsigned big-endian counter, MAX
    kFieldFill = 2,  // per byte: set only where the stored byte is empty (0x00 or 0xFF); items are never taken away
};
struct TeamField {
    uint16_t offset = 0;
    uint8_t mode = kFieldBits;
    std::string value;  // 1..16 bytes
};
constexpr size_t kMaxTeamFieldBytes = 16;
constexpr size_t kMaxTeamFields = 2048;  // one TEAM_STATE stays under kMaxStream

// party settings flags
enum PartyFlags : uint16_t {
    kPartySharedProgress = 1 << 0,
    kPartySharedRupees = 1 << 1,
    kPartyFriendlyFire = 1 << 2,
    kPartyDeckRiding = 1 << 3,
    kPartyStayTogether = 1 << 4,
    kPartyRunnerPinned = 1 << 5,
};
constexpr uint16_t kPartyDefaults = kPartySharedProgress | kPartyDeckRiding;
constexpr int kMaxPartySize = 16;    // the protocol's cap; each server's own maximum (party.max_size, default 8)
                                     // comes in ServerInfo::party_max and PartyState::max

// notices
enum NoticeKind : uint8_t {
    kNoticeRestart = 1,
    kNoticeKick = 2,       // removed by the operator or for a protocol error
    kNoticeBan = 3,
    kNoticeInfo = 4,
    kNoticeThrottle = 5,   // a warning: over a limit (`seconds` carries the limit's reason, KickReason)
    kNoticeKickLimit = 6,  // removed for staying over a limit; `seconds` carries the KickReason
};
// why a client was throttled or removed for a limit (in the NOTICE's u16 field)
enum KickReason : uint16_t {
    kLimitNone = 0,
    kLimitUpload = 1,     // bytes per second up (limits.bytes_per_s; the text names the limit)
    kLimitPackets = 2,    // packets per second
    kLimitReliable = 3,   // reliable messages per second
    kLimitApp = 4,        // app messages per second
    kLimitPose = 5,       // malformed or implausible poses
    kLimitOther = 6,
};

// the sea (DESIGN.md 3.3): 7x7 squares of 100,000 units, each split into 8x8 sub-cells of 12,500
constexpr int kSeaSquares = 7;
constexpr float kSquareUnits = 100000.0f;
constexpr int kSubCells = 8;
constexpr float kSubCellUnits = kSquareUnits / kSubCells;

}  // namespace online::proto
