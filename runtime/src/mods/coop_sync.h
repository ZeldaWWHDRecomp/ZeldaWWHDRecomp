// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the session (docs/online-coop.md): the menu's settings, hosting and joining, the other players as
// puppets, the join (the host's portable state loaded on the guest, saving off there) and back to the title when the
// host is gone. The world the players share (Tetra's ship, cannon shots, sea events, the host's monsters) is
// extras.cpp's, through world_messages() / host_state().
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "coop_net.h"

struct Cpu;

namespace mods::coop::sync {

enum Kind : uint8_t { kPose = 1, kShip, kMonsters, kVoice, kArea, kMid, kFar, kKinds };  // (kMid, kFar: a server's tiers, C13)  // the unreliable states (kVoice: coop_voice.h's
// frames; kArea: the monsters of the place a player runs, coop_monsters.h)

struct Settings {
    int role = 0;  // 0 host, 1 join (the catalogue's choice)
    std::string addr = "127.0.0.1", name = "Link";
    int port = net::kPort, tunic = 0;  // tunic: 0 green, 1 red, 2 blue, 3 purple (the Four Swords set)
    int character = 0;                 // who this game plays as (coop_pose.h's own_character)
    bool voice = false;                // proximity voice chat (coop_voice.h); off until chosen, as it asks for the microphone
    bool on = false;                   // online switched on by the player (mod.coop.enabled; WWHD_COOP in tests)
    bool server = false;               // through a server (online, the party there) instead of directly to a host
    std::string server_addr = "127.0.0.1";
    int server_port = 7777;
    bool radar = true;                 // (C13) far players as dots on a small sea radar (through a server, at sea)
    int strangers = 0;                 // how players outside the party look (presence): 0 like any Link, 1 see-through
    bool stay_together = false;        // the fork's way (a party option): the guest plays in the host's world, its saves
                                       // off, asked before going where the host isn't, back to the title when the host goes
    bool notice_ok = false;            // the privacy notice accepted (its version, online.notice_accepted)
};
// the privacy notice's version: a changed notice is shown and accepted again
constexpr const char* kNoticeVersion = "3";  // (3: what a server keeps)
constexpr const char* kNotice =
    "Online co-op connects your game to other players' games, directly or through a server. Played directly, the "
    "other players see your IP address (and you theirs); through a server, its operator sees it. An IP address can "
    "reveal your approximate location. A server keeps your party's player keys (behind your friend codes) and your "
    "party's shared progress for up to 72 hours after your party last played (its operator can change how long), so "
    "your party survives a server restart. The server program does not write IP addresses to disk; if you are "
    "banned, it may keep a scrambled form of yours. Whoever runs a server controls its data. Only play with people "
    "you trust, on servers you trust. Nothing is sent until you switch online co-op on, and it stops when you switch "
    "it off.";
Settings settings();
void set_settings(const Settings& s);
void save_settings(const Settings& s);  // the same, and kept (the Settings window, the pause menu's page: main thread)  // the menu (any thread); a running session restarts with them
const char* status();
// (any thread) a session given up on (refused, removed, the host gone) started again with the same settings
void retry();
constexpr const char* kTunics[] = {"Green", "Red", "Blue", "Purple"};

// every step, also at the title: the session started or stopped as the toggle says, the socket read and written
void poll(Cpu* c, bool on);
// every step in play, after the mods' monsters: this game's pose out, the other players drawn (`hull` Tetra's ship or
// the craft in tow, 0 none: whose deck carries poses in its frame; `aboard` this game's Link on it, extras' own check)
void step(Cpu* c, uint32_t link, uint32_t hull, bool aboard);

bool host();   // hosting
bool guest();  // joined someone's game (welcomed)
// another player's place in the world as drawn now (for pushing), i 0..kPuppets-1
bool place(int i, float out[3]);
// the player a monster at `at` goes for: this game's Link (`link`'s place) or another player shown here, the nearest,
// kept while no other is clearly nearer (by 30%) and it is still here; `target` is the monster's choice (player id, -1
// none yet), kept between calls. Out: where that player is
void nearest(const float at[3], uint32_t link, int& target, float out[3]);
// the guest's join, once its Link has arrived in the host's world: where the host stood on the ship's deck (its frame),
// for extras to set it down there once the ship is made (taken once)
bool take_deck_drop(float local[3]);
// the world's messages apply here: the host; a guest once its Link has arrived in the host's world (until then they
// would land in its own stage, wiped by the load)
bool arrived();
// every sound effect start the game makes (JAIZelBasic::seStart, interp.cpp's hook): one of Link's (its place inside his
// actor) goes to the other players, who play it at his puppet
// and false when this game's own Link's voice is not to sound (playing a character who has nothing for it; one she has
// is put in its place)
bool sound_started(Cpu* c);
// the game's HUD left over the title screen after a guest went back to it: hidden until it plays again (any thread)
bool hud_hidden();

// the world's reliable messages to this game (kFired, kSeaEvent, kHit, monster spawns and deaths), in order
struct WorldMsg { int from; net::Msg type; std::string body; };
std::vector<WorldMsg> take_world();
// the host's newest state of a kind, shown 100 ms behind: two snapshots around then and how far between (t 0..1)
bool host_state(Kind k, std::string& a, std::string& b, float& t);
// the same for any player's states
bool state_of(int from, Kind k, std::string& a, std::string& b, float& t);
// who runs the monsters of a place (stage, room): the host where the host is; elsewhere the player who came first,
// while they stay (Asad's hybrid; the host decides and tells the guests). -1: nobody is there
int owner(const char stage[8], int room);
// a player is in that stage (the owners' table)
bool anyone_in(const char stage[8]);
// (the menu, any thread) this game's Link taken beside player id, where their newest pose is (a warp, as the mods'
// warps go: their stage, room and place)
void go_to(int id);
// (C14) the player outside the party whose puppet stands nearest this game's Link (-1: none shown)
int nearest_stranger();

}  // namespace mods::coop::sync
