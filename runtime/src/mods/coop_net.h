// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op over UDP (docs/online-coop.md): one socket a game, the host's world, guests as thin copies.
// Each packet carries the sender's reliable messages until they are acked (Quake 3's way) and its newest acks; poses
// and the host's ship go unreliably at 30 Hz (each game step) and are shown 60 ms behind, between the two snapshots
// around that time.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "coop_servers.h"

struct Cpu;

namespace mods::coop::net {

// kProto: this port's co-op protocol (the fork's 3, plus the game-data check in the hello (4), plus the players'
// addresses for host migration (5), plus the poses' rest mask (6), plus the gameplay mods in the hello (7)); a game of
// another protocol
// is refused. Players meet by protocol and game data (ss::game_data_id), not by executable: a recompiled and a decomp
// build of the same game play together
// kMaxPlayers: the players' numbers any transport gives (through a server: a party of up to kMaxParty and the strangers
// shown nearby, GAPS C11: an instance's 32); kMaxDirect: the direct transport's (a host and three guests: its host relays
// every message to every guest, GAPS C29); kMaxParty: the largest party any server may allow (its party.max_size)
constexpr int kProto = 7, kPort = 7777, kMaxPlayers = 32, kMaxDirect = 4, kMaxParty = 16;
inline constexpr const char* kStrangerGone = "a stranger out of sight";  // (Handler::left: a stranger's puppet, no notice)

// the reliable messages (the game's side reads them through on_message)
enum Msg : uint8_t {
    kHello = 1,   // guest -> host: {u16 proto, u64 game data, u8 tunic, u8 name length, name, gameplay mods (coop_modset.h)}
    kWelcome,     // host -> guest: {u8 id, u8 tunic: the one asked for, or the next no one wears}
    kReject,      // host -> guest: text
    kJoinState,   // host -> guest: {u32 total, u32 at, bytes}: the host's portable state, in pieces
    kJoined,      // host -> all: {u8 id, u8 tunic, name}
    kLeft,        // host -> all: {u8 id}
    kBye,         // either way: leaving (sent a few times, unreliably)
    kFired,       // any -> host -> others: a cannon shot (step 3)
    kSeaEvent,    // host -> guests (step 4)
    kHit,         // guest -> host (step 4)
    kSpawn,       // host -> guests: one of the host's monsters made (step 4)
    kKill,        // host -> guests: it is gone
    kEffect,      // any -> host -> others: the particles a player's Link started in a step (relative to him)
    kArrived,     // guest -> host: its Link is in the host's world (the join): the running event and monsters to it
    kBoatShot,    // any -> host -> others: the boat's own cannon fired {f32 pos[3], s16 angle[3], f32 speedF, speed y, gravity}
    kDamage,      // guest -> host: {u16 n, u8 health}: its hit on the host's monster n (the host's health counts)
    kProgress,    // any -> host -> others: save data fields a player changed (coop_progress.h)
    kDown,        // guest -> host: its Link fell (no health left): brought back beside the host
    kSound,       // any -> host -> others: the sound effects a player's Link started in a step (JAIZelBasic::seStart)
    kPlayerHit,   // any -> host -> others: {u8 player hit, u8 quarter hearts, s16 the way the blow goes}
    kBombed,      // any -> host -> others: {stage[8], s8 room, f32 pos[3]}: a player's bomb went off there (coop_items.h)
    kOwner,       // host -> guests: who runs each place's monsters: {stage[8], s8 room, u8 player} each (coop_monsters.h)
    kMonHit,      // guest -> host -> others: {stage[8], u32 key, u8 health}: its hit on a monster another player runs
    kMonGone,     // any -> host -> others: {stage[8], u32 key, s8 room, u16 set id}: a monster killed
    kDead,        // host -> guests: the placed monsters killed in the stages the players are in: {stage[8], s8 room, u16 set id} each
    kDoorAsk,     // guest -> host: {stage[8]}: it would go into a stage the host isn't in (coop_sync.cpp, doors)
    kDoorAnswer,  // host -> guest: {stage[8], u8 let in}
    kCutscene,    // host -> guests: {stage[8], s8 room, name[32]}: a cutscene of the stage's event list started (no name: over)
    kDoorMove,    // any -> host -> others: {stage[8], s16 proc, f32 home x, z, u8 open, u8 way}: a player's door moving
    kRupees,      // any -> host -> others: {s32 rupees}: what a player just gained, gained by everyone
    kEmote,       // any -> host -> others: {u8 emote} (coop_emotes.h; through a server its EMOTE, strangers near by too)
    kClock,       // leader -> party: its time of day, day and sea weather (coop_clock.h)
    kClockJump,   // member -> leader: its own new time (the Song of Passing), for the party
    kGust,        // any -> host -> others: {f32 pos[3], s16 yaw, f32 reach}: a Deku Leaf's gust (coop_world.cpp)
};

enum class Role { kOff, kHost, kGuest };
Role role();
int my_id();                     // 0 is the host
const char* status();            // one line for the menu: "Hosting on 7777, 2 players, ping 3 ms"
uint64_t game_hash();            // the game data this game plays (ss::game_data_id; WWHD_COOP_GAME: a test aid)

// start or stop; WWHD_COOP=host[:port] or join:<addr>[:port] (with WWHD_COOP_NAME, WWHD_COOP_TUNIC) at the first step
bool start(Role r, const std::string& addr, int port, const std::string& my_name, int my_tunic, std::string& why);
void stop(const char* why);

// the party's gameplay mods (GAPS C5, coop_modset.h) on the direct transport: this game's set, sent in its hello (a
// guest) or given back with a refusal (the host, whose set is the party's); the host's check of a guest's set (its
// refusal in words, "" to let it in); after a refusal for its mods, the host's set ("" otherwise)
void set_hello_mods(const std::string& encoded);
using HelloCheck = std::string (*)(const uint8_t* mods, size_t n);
void set_hello_check(HelloCheck check);
std::string refused_mods();

// queue a reliable message to one player (-1: everyone this game talks to)
void send(int to, Msg type, const std::string& body);
// unreliable: this game's newest snapshot of kind k (a pose, the ship), sent at the next tick to everyone
void put_state(uint8_t kind, const std::string& body);
// the world place of the pose put next, when its own is in another frame (on a friend's boat, C16): what a server's
// interest uses
void put_pose_place(const float at[3]);
// unreliable, a stream: added to what of kind k goes at the next tick (voice frames: none overwritten)
void add_state(uint8_t kind, const std::string& more);

// each logic step: read the socket, call back, send what is due
struct Handler {
    virtual void message(int from, Msg type, const uint8_t* d, size_t n) = 0;
    virtual void state(int from, uint8_t kind, uint32_t ms, const uint8_t* d, size_t n) = 0;
    virtual void joined(int id) = 0;
    virtual void left(int id, const char* why) = 0;
};
void poll(Handler& h);
uint32_t now_ms();
int ping_ms(int id);
const std::string& name(int id);
int tunic(int id);
bool connected(int id);
// a player who shares the game with this one (the direct transport: everyone; through a server: the party, not the
// strangers shown nearby, whose puppets alone come here)
bool member(int id);

// ---- through a server (GAPS C7; built with WWHD_ONLINE) ----
bool server_built();                    // this build has the server transport
// start through a server instead of the direct transport (any role: the party decides who leads)
bool start_server(const std::string& addr, int port, const std::string& my_name, int my_tunic, std::string& why);
bool via_server();                      // the session runs through a server
std::string friend_code();              // this install's (its identity made on first use; "" without the server transport)
// (main thread: the pages) the actions go at the session's next poll; invite false + why for a code that cannot be one
bool invite(const std::string& code, std::string& why);  // into this game's party (made if none)
std::vector<std::pair<uint32_t, std::string>> invites();  // invitations waiting: party, from whom
void answer(uint32_t party, bool yes);
void leave_party();
// the servers this player has been on (most recent first, each key pinned on the first visit), and forgetting one (its
// pinned key too: the next visit pins the key it shows then)
std::vector<servers::Server> saved_servers();
void forget_server(const std::string& addr, int port);
// each place's enemy runner as the server names it (GAPS X2), as this game's player numbers; false while this game
// is in no party through a server (then the host's own table, coop_sync, decides)
struct Runner { char stage[8]; int8_t room; int slot; };
// (C13, through a server) the players of this sea square shown on no puppet: their places as the server's markers give
// them (about 400 units exact), party members marked; empty without a server or when none came for 10 s
struct Marker { int slot; float x, z; bool party; };
std::vector<Marker> server_markers();
// (through a server) where a party member is, as the server reports every member once a second (PARTY_WHERE): its
// stage and room, how old that report is and how long it has stayed in that place; false without a server, for a
// stranger, or before the first report. A mid-range pose carries no stage or room: this is its place (coop_sync)
struct Place { char stage[8]; int8_t room; uint32_t age_ms, settled_ms; bool at_sea; float x, z; };  // (at_sea: x, z its place on the sea)
bool member_place(int id, Place& out);
// (through a server) the other players at sea it shows on the Sea Chart (not the party, not blocked): their names as
// shown to strangers and their places; empty without a server, a server that doesn't send them, or none for 5 s
struct SeaMark { std::string name; float x, z; };
std::vector<SeaMark> sea_players();
// (C14, through a server) a player's key hash (0: none, or the direct transport), and telling the server a block
uint64_t player_key(int id);
void tell_block(uint64_t key, bool on);
void tell_mute(uint64_t key, bool on);
// (through a server) text chat (coop_chat.h): a line on a channel (0 the party, 1 global; the server checks it and
// passes it on, the answer comes to coop_chat), and the channels this player turned off (a bit each: the server sends
// nothing of them; sent again at each welcome)
void chat_send(int channel, const std::string& text);
void chat_prefs(uint8_t off_bits);
// (server mod rules, GAPS C5) why the server or a party turned this game away for its gameplay mods (the problems'
// codes as coop_modset.h's); empty otherwise
struct ModIssue { uint8_t code; std::string id, wanted, have; };
std::vector<ModIssue> mod_issues();
// a server's mod rules in words, asked for without joining (query_rules starts the question; "" until the answer)
void query_rules(const std::string& addr, int port);
std::string server_rules(const std::string& addr, int port);
bool server_runners(std::vector<Runner>& out);
// The party's switches (GAPS C15; the server's PartyFlags, the same bits): through a server in a party, as its leader
// set them (kept by the server, the same for every member); else `otherwise` (the direct transport: the host's own
// settings, as before). The leader changes them (others: refused here)
enum PartyFlag : uint16_t { kShareProgress = 1, kShareRupees = 2, kFriendlyFire = 4, kDeckRiding = 8, kStayTogether = 16 };
bool party_flag(PartyFlag f, bool otherwise);
bool party_flags(uint16_t& flags, bool& leading);  // false: in no party through a server
bool set_party_flags(uint16_t flags, std::string& why);
// (C29) the largest party here: the server's party.max_size (from its info, then its party state; 4 from a server that
// does not say), the direct transport's kMaxDirect; and how many are in this game's party now (itself included; 1 alone)
int party_max();
int party_size();

// ---- (C28, through a server) PvP at sea: the server referees (proto/pvp.h); coop_pvp.h plays it in the game ----
struct PvpZone { int square; float x, z, r; };  // a safe zone: its centre in the world, its radius
struct PvpRules {
    int mode = -1;  // -1: no rules (no server, or not said yet); 0 off, 1 opt-in, 2 on for everyone at sea
    uint32_t protect_ms = 12000, enter_delay_ms = 10000;
    float hide_band = 8000, max_hit_dist = 3000;
    int ko_rupee_percent = 10, ko_rupee_max = 100;
    std::vector<PvpZone> zones;
};
PvpRules pvp_rules();                     // any thread
uint8_t pvp_state(uint32_t* left_ms = nullptr);  // the server's PvP state flags for this game (proto PvpStateFlags)
uint8_t pvp_mark(int id);                 // another player's: bit 0 PvP active, bit 1 protected (0: not or unknown)
void pvp_flag(bool on, bool busy);        // this game's toggle and whether it is busy (sent on change)
void pvp_hit(int victim, uint8_t kind, uint8_t quarters, const float at[3]);
void pvp_shot(uint8_t kind, const float at[3], const float v[3]);
void pvp_ko(int by, uint16_t wallet);
struct PvpEvent {
    enum Kind : uint8_t { kHitOn, kShotOf, kKoOf, kRupees, kRespawn } kind;
    int from = -1;        // kHitOn: the attacker; kShotOf: the shooter; kKoOf: the victim (my_id(): this game)
    int by = -1;          // kKoOf: who knocked them out (my_id(): this game; -1 unknown here)
    uint8_t what = 0, quarters = 0;  // the weapon (proto PvpKind), the damage
    float at[3] = {}, v[3] = {};
    uint32_t value = 0;   // kRupees: how many; kRespawn: the square
    std::string by_name;  // kKoOf: the attacker's name as known here
};
std::vector<PvpEvent> pvp_take();         // game thread: what came since the last call

}  // namespace mods::coop::net
