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
// addresses for host migration (5), plus the poses' rest mask (6)); a game of another protocol
// is refused. Players meet by protocol and game data (ss::game_data_id), not by executable: a recompiled and a decomp
// build of the same game play together
// kMaxPlayers: the players' numbers any transport gives (through a server: a party of up to 4 and the strangers shown
// nearby, GAPS C11: an instance's 32); kMaxDirect: the direct transport's (a host and three guests)
constexpr int kProto = 6, kPort = 7777, kMaxPlayers = 32, kMaxDirect = 4;

// the reliable messages (the game's side reads them through on_message)
enum Msg : uint8_t {
    kHello = 1,   // guest -> host: {u16 proto, u64 game data, u8 tunic, name}
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
};

enum class Role { kOff, kHost, kGuest };
Role role();
int my_id();                     // 0 is the host
const char* status();            // one line for the menu: "Hosting on 7777, 2 players, ping 3 ms"
uint64_t game_hash();            // the game data this game plays (ss::game_data_id; WWHD_COOP_GAME: a test aid)

// start or stop; WWHD_COOP=host[:port] or join:<addr>[:port] (with WWHD_COOP_NAME, WWHD_COOP_TUNIC) at the first step
bool start(Role r, const std::string& addr, int port, const std::string& my_name, int my_tunic, std::string& why);
void stop(const char* why);

// queue a reliable message to one player (-1: everyone this game talks to)
void send(int to, Msg type, const std::string& body);
// unreliable: this game's newest snapshot of kind k (a pose, the ship), sent at the next tick to everyone
void put_state(uint8_t kind, const std::string& body);
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
bool server_runners(std::vector<Runner>& out);

}  // namespace mods::coop::net
