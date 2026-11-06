// SPDX-License-Identifier: MPL-2.0
// Online co-op through a server (coop_net.h's second transport; GAPS C7): the same calls as the direct UDP transport,
// carried by online::proto::Client (runtime/src/online/proto, the server helper's library) to wwhd-online-server.
//
// The game side keeps the fork's model of up to four players with local numbers 0..3 (coop_sync.h): here number 0 is
// the party's leader (or this game while it is in no party), the other party members follow by their server id, and
// players outside the party (presence: strangers the server shows nearby) take the numbers left, for their puppets
// only. The leader does what the direct transport's host does (who runs each place's monsters, the team state for
// a member who joins); this game is "host" while it leads or is in no party, "guest" while another member leads, so a
// leader who goes hands over with the server's next party state (no title, no reload).
//
// Messages go as the server's opaque app messages: the fork's reliable vocabulary (net::Msg) reliable to the party or
// one member, the unreliable states other than poses (sync's kinds) with app type 0x80 | kind and the sender's clock;
// poses as the server's pose with the fork's pose as its blob. Only party members' app messages are taken; strangers
// bring poses alone (presence: nothing of theirs acts on this game).
#include "coop_net.h"

#if WWHD_ONLINE

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "mods.h"
#include "proto/client.h"
#include "../platform/host.h"
#include "../savestate.h"

namespace mods::coop::net::srv {
namespace {

namespace op = online::proto;

struct Peer {
    bool on = false;
    uint16_t sid = 0;   // server id
    uint64_t key = 0;
    std::string name;
    uint32_t look = 0;
    bool party = false;
};

op::Client* g_client = nullptr;
bool g_active = false;
std::string g_status = "Off";
uint16_t g_me_sid = 0;
std::vector<Peer> g_peers;     // everyone the server told us of (not this game)
op::PartyState g_party;
int g_slot_of_me = 0;
Peer g_slot[kMaxPlayers];      // local numbers: [0] the leader (or this game), then members, then strangers
std::string g_my_name;
int g_my_tunic = 0;
op::Where g_where_sent;
bool g_where_known = false;
struct Invite { uint32_t party; std::string from; };
std::vector<Invite> g_invites;
std::string g_auto_invite;     // test aid: a friend code (or @file with it) to invite once online
Handler* g_h = nullptr;

int online_members() {
    int n = 0;
    for (const op::PartyMember& m : g_party.members) n += m.online || m.id == g_me_sid;
    return n;
}
bool in_party() { return g_party.party != 0 && online_members() > 1; }
// the party's leader as this game acts on it: the server's, while that member is online; else (gone, not yet replaced
// by the server) the online member with the lowest id, so somebody always does the host's part
uint16_t leader() {
    for (const op::PartyMember& m : g_party.members) if (m.id == g_party.leader && (m.online || m.id == g_me_sid)) return m.id;
    uint16_t best = g_me_sid;
    for (const op::PartyMember& m : g_party.members) if ((m.online || m.id == g_me_sid) && m.id && m.id < best) best = m.id;
    return best;
}
bool member_sid(uint16_t sid) {
    return in_party() && std::any_of(g_party.members.begin(), g_party.members.end(), [&](const op::PartyMember& m) { return m.id == sid; });
}
int slot_of(uint16_t sid) {
    for (int i = 0; i < kMaxPlayers; i++) if (g_slot[i].on && g_slot[i].sid == sid && i != g_slot_of_me) return i;
    return -1;
}

// the local numbers again, from the party and the players shown; a number that now names another player is left and
// joined again (its puppet and its pose history start over)
void remap(Handler& h) {
    Peer next[kMaxPlayers];
    std::vector<uint16_t> order;
    if (in_party()) {
        order.push_back(leader());
        std::vector<uint16_t> rest;
        for (const op::PartyMember& m : g_party.members) if (m.id != leader() && (m.online || m.id == g_me_sid)) rest.push_back(m.id);
        std::sort(rest.begin(), rest.end());
        order.insert(order.end(), rest.begin(), rest.end());
    } else {
        order.push_back(g_me_sid);
    }
    for (const Peer& p : g_peers) if (!p.party && order.size() < (size_t)kMaxPlayers) order.push_back(p.sid);
    int me = 0;
    for (size_t i = 0; i < order.size() && i < (size_t)kMaxPlayers; i++) {
        Peer& s = next[i];
        s.on = true, s.sid = order[i];
        if (order[i] == g_me_sid) { me = (int)i, s.name = g_my_name, s.look = (uint32_t)g_my_tunic, s.party = in_party(); continue; }
        for (const Peer& p : g_peers) if (p.sid == order[i]) s = p, s.on = true;
        s.party = member_sid(order[i]);
    }
    const int was_me = g_slot_of_me;
    for (int i = 0; i < kMaxPlayers; i++) {
        const bool same = g_slot[i].on == next[i].on && g_slot[i].sid == next[i].sid && g_slot[i].party == next[i].party && was_me == me;
        if (same) { g_slot[i].name = next[i].name, g_slot[i].look = next[i].look; continue; }
        if (g_slot[i].on && i != was_me) h.left(i, "moved");
    }
    Peer old[kMaxPlayers];
    std::copy(std::begin(g_slot), std::end(g_slot), old);
    std::copy(std::begin(next), std::end(next), g_slot);
    g_slot_of_me = me;
    for (int i = 0; i < kMaxPlayers; i++)
        if (g_slot[i].on && i != me && g_slot[i].party && (old[i].sid != g_slot[i].sid || !old[i].on || !old[i].party || was_me != me))
            h.joined(i);
}

std::string read_code(const std::string& spec) {
    if (spec.empty() || spec[0] != '@') return spec;
    std::ifstream f(spec.substr(1));
    std::string code;
    std::getline(f, code);
    return code;
}

}  // namespace

bool active() { return g_active; }

bool start(const std::string& server, int port, const std::string& my_name, int my_tunic, std::string& why) {
    stop(nullptr);
    std::string path = host::config_dir() + "/online-identity.key";
    if (const char* e = getenv("WWHD_ONLINE_IDENTITY")) path = e;  // (tests: one identity per game)
    op::crypto::Identity id;
    if (!op::crypto::Identity::load_or_create(path, id, why)) return false;
    op::ClientConfig cfg;
    cfg.host = server, cfg.port = (uint16_t)port, cfg.game_data = ss::game_data_id(), cfg.identity = id;
    if (const char* e = getenv("WWHD_COOP_GAME")) cfg.game_data = strtoull(e, nullptr, 16);
    cfg.name = my_name.substr(0, 24), cfg.look = (uint32_t)(my_tunic & 3), cfg.tag = "wwhd";
    g_client = new op::Client();
    if (!g_client->start(cfg, why)) { delete g_client, g_client = nullptr; return false; }
    g_active = true, g_my_name = my_name, g_my_tunic = my_tunic & 3, g_status = "Connecting to " + server;
    g_auto_invite = getenv("WWHD_ONLINE_INVITE") ? getenv("WWHD_ONLINE_INVITE") : "";
    mods::trace("coop: server %s:%d (game %016llX)", server.c_str(), port, (unsigned long long)cfg.game_data);
    return true;
}

void stop(const char* why) {
    if (g_client) g_client->stop(), delete g_client, g_client = nullptr, mods::trace("coop: server session stopped%s%s", why ? ": " : "", why ? why : "");
    g_active = false, g_status = why ? why : "Off", g_me_sid = 0, g_peers.clear(), g_party = {}, g_slot_of_me = 0, g_where_known = false;
    g_invites.clear();
    for (Peer& p : g_slot) p = Peer{};
}

Role role() {
    if (!g_active) return Role::kOff;
    return in_party() && leader() != g_me_sid ? Role::kGuest : Role::kHost;
}
int my_id() { return g_slot_of_me; }
const char* status() { return g_status.c_str(); }
int ping_ms() { return g_client ? g_client->ping_ms() : -1; }
const std::string& name(int id) { static const std::string none; return id >= 0 && id < kMaxPlayers ? g_slot[id].name : none; }
// the tunic each wears: the one asked for, or (a party member before it in the numbers wears it) the next no one wears,
// as the direct transport's host gives them out
bool member(int id);
int tunic(int id) {
    if (id < 0 || id >= kMaxPlayers || !g_slot[id].on) return 0;
    if (!member(id)) return (int)(g_slot[id].look & 3);  // (a stranger: as they chose)
    bool worn[4] = {};
    for (int i = 0; i <= id; i++) {
        if (!member(i)) continue;
        int t = (int)(g_slot[i].look & 3);
        for (int k = 0; k < 4 && worn[t]; k++) t = (t + 1) % 4;
        if (i == id) return t;
        worn[t] = true;
    }
    return 0;
}
bool connected(int id) { return id >= 0 && id < kMaxPlayers && g_slot[id].on; }
bool member(int id) { return connected(id) && (g_slot[id].party || id == g_slot_of_me); }
std::string friend_code() { return g_client && g_client->state() == op::ClientState::kOnline ? g_client->friend_code() : ""; }

void send(int to, Msg type, const std::string& body) {
    if (!g_client || !in_party()) return;
    op::AppHeader h;
    h.type = (uint8_t)type;
    if (to >= 0) {
        if (!connected(to) || to == g_slot_of_me) return;
        h.target = op::kToMember, h.member = g_slot[to].sid;
    }
    g_client->send_app(h, (const uint8_t*)body.data(), body.size(), true);
}

void put_state(uint8_t kind, const std::string& body) {
    if (!g_client) return;
    // (the server's near tier is 15 Hz and its default upload budget 8 KB/s: every other of the 30 Hz poses goes,
    // ~380 bytes in the compact form (coop_pose.cpp, ~5.7 KB/s), and the other states at most 10 Hz)
    static uint32_t calls[256];
    const uint32_t n = calls[kind]++;
    if (kind == 1 ? n % 2 : n % 3) return;
    if (kind == 1 && body.size() >= 28) {  // a pose (coop_pose.h pack: u32 size, stage[8], room, flags, pos[3], s16 yaw)
        op::PoseHeader h;
        memcpy(h.where.stage.data(), body.data() + 4, 8);
        h.where.room = (int8_t)body[12];
        float p[3];
        memcpy(p, body.data() + 14, 12);
        int16_t yaw;
        memcpy(&yaw, body.data() + 26, 2);
        h.x = p[0], h.y = p[1], h.z = p[2], h.yaw = (uint16_t)yaw;
        if (body[13] & 1) h.flags |= 1;  // (on a deck: its frame)
        if (!g_where_known || !(h.where == g_where_sent)) g_client->send_stage(h.where), g_where_sent = h.where, g_where_known = true;
        g_client->send_pose(h, (const uint8_t*)body.data(), body.size());
        return;
    }
    if (!in_party()) return;
    const uint32_t ms = now_ms();
    std::string b((const char*)&ms, 4);
    b += body;
    op::AppHeader h;
    h.type = (uint8_t)(0x80 | kind);
    g_client->send_app(h, (const uint8_t*)b.data(), b.size(), false);
}

void poll(Handler& h) {
    if (!g_client) return;
    g_h = &h;
    for (const op::ClientEvent& e : g_client->poll(now_ms())) {
        switch (e.kind) {
        case op::ClientEvent::kStateChanged:
            g_status = e.text.empty() ? g_status : e.text;
            mods::trace("coop: server: %s", e.text.c_str());
            if (e.state == op::ClientState::kStopped) {
                const std::string why = e.text.empty() ? "Disconnected" : e.text;
                for (int i = 0; i < kMaxPlayers; i++) if (g_slot[i].on && i != g_slot_of_me) h.left(i, why.c_str());
                stop(why.c_str());
                return;
            }
            break;
        case op::ClientEvent::kWelcome:
            g_me_sid = e.id;
            if (!e.resumed) {
                for (int i = 0; i < kMaxPlayers; i++) if (g_slot[i].on && i != g_slot_of_me) h.left(i, "reconnected");
                g_peers.clear(), g_party = {};
                for (Peer& p : g_slot) p = Peer{};
            }
            g_status = "Online, friend code " + g_client->friend_code();
            mods::trace("coop: server: welcomed as %u%s, friend code %s", e.id, e.resumed ? " (resumed)" : "", g_client->friend_code().c_str());
            if (const char* f = getenv("WWHD_ONLINE_CODE_FILE")) std::ofstream(f) << g_client->friend_code() << "\n";  // (tests)
            remap(h);
            break;
        case op::ClientEvent::kRefused:
            g_status = "Refused: " + e.text;
            mods::trace("coop: server refused: %s", e.text.c_str());
            break;
        case op::ClientEvent::kServerKeyChanged:
            g_status = "The server's key changed: not connected";
            break;
        case op::ClientEvent::kPlayerIn: {
            auto it = std::find_if(g_peers.begin(), g_peers.end(), [&](const Peer& p) { return p.sid == e.player.id; });
            Peer p{true, e.player.id, e.player.key_hash, e.player.name, e.player.look, (e.player.flags & 1) != 0};
            if (it == g_peers.end()) g_peers.push_back(p), mods::trace("coop: server: player %u (%s) in", p.sid, p.name.c_str());
            else *it = p;
            remap(h);
            break;
        }
        case op::ClientEvent::kPlayerOut:
            g_peers.erase(std::remove_if(g_peers.begin(), g_peers.end(), [&](const Peer& p) { return p.sid == e.id; }), g_peers.end());
            mods::trace("coop: server: player %u out", e.id);
            remap(h);
            break;
        case op::ClientEvent::kPoses:
            for (const op::PoseEntry& p : e.poses) {
                const int s = slot_of(p.id);
                if (s < 0 || p.tier != op::kTierNear || p.blob.empty()) continue;  // (mid / far tiers: puppets come with C13)
                h.state(s, 1, p.ms, p.blob.data(), p.blob.size());
            }
            break;
        case op::ClientEvent::kApp: {
            const int s = slot_of(e.id);
            if (s < 0 || !member_sid(e.id)) break;  // (presence: nothing of a stranger's acts here)
            if (e.app_type & 0x80) {
                if (e.body.size() < 4) break;
                uint32_t ms;
                memcpy(&ms, e.body.data(), 4);
                h.state(s, (uint8_t)(e.app_type & 0x7F), ms, e.body.data() + 4, e.body.size() - 4);
            } else {
                h.message(s, (Msg)e.app_type, e.body.data(), e.body.size());
            }
            break;
        }
        case op::ClientEvent::kNotice:
            mods::trace("coop: server notice %u: %s", e.value, e.text.c_str());
            if (e.value == op::kNoticeKick || e.value == op::kNoticeBan) g_status = "Removed by the server: " + e.text;
            break;
        case op::ClientEvent::kPartyInvited:
            g_invites.push_back({e.party_id, e.player.name});
            mods::trace("coop: server: invited to party %u by %s", e.party_id, e.player.name.c_str());
            if (getenv("WWHD_ONLINE_ACCEPT")) g_client->party_accept(e.party_id), g_invites.pop_back();  // (tests)
            break;
        case op::ClientEvent::kPartyState:
            g_party = e.party;
            mods::trace("coop: server: party %u, leader %u (acting %u), %d of %zu members online", e.party.party, e.party.leader, leader(),
                        online_members(), e.party.members.size());
            for (Peer& p : g_peers) p.party = member_sid(p.sid);
            remap(h);
            break;
        case op::ClientEvent::kPartyResult:
            mods::trace("coop: server: party: %s", e.text.c_str());
            if (!e.text.empty()) g_status = e.text;
            break;
        default: break;  // (markers, emotes, copies, runners, team state: later items)
        }
    }
    // test aid: WWHD_ONLINE_INVITE=<friend code> or @<file holding it>: invited once online (the file may come later)
    if (!g_auto_invite.empty() && g_client->state() == op::ClientState::kOnline) {
        const std::string code = read_code(g_auto_invite);
        uint64_t c40 = 0;
        if (!code.empty() && op::parse_friend_code(code, c40)) {
            g_client->party_invite(c40);
            mods::trace("coop: server: invited %s", code.c_str());
            g_auto_invite.clear();
        }
    }
    if (g_client->state() == op::ClientState::kOnline) {
        int n = 0;
        for (const Peer& p : g_slot) n += p.on && p.party;
        const int ping = g_client->ping_ms();
        g_status = std::string(in_party() ? (role() == Role::kHost ? "Online, leading a party of " : "Online, in a party of ") + std::to_string(n) : "Online") +
                   (ping >= 0 ? ", ping " + std::to_string(ping) + " ms" : "") + ", friend code " + g_client->friend_code();
    }
}

bool invite(const std::string& code, std::string& why) {
    uint64_t c40 = 0;
    if (!g_client || g_client->state() != op::ClientState::kOnline) return why = "not online", false;
    if (!op::parse_friend_code(code, c40)) return why = "that is not a friend code", false;
    g_client->party_invite(c40);
    return true;
}
std::vector<std::pair<uint32_t, std::string>> invites() {
    std::vector<std::pair<uint32_t, std::string>> out;
    for (const Invite& i : g_invites) out.emplace_back(i.party, i.from);
    return out;
}
void answer(uint32_t party, bool yes) {
    if (!g_client) return;
    yes ? g_client->party_accept(party) : g_client->party_decline(party);
    g_invites.erase(std::remove_if(g_invites.begin(), g_invites.end(), [&](const Invite& i) { return i.party == party; }), g_invites.end());
}
void leave_party() { if (g_client) g_client->party_leave(); }

}  // namespace mods::coop::net::srv

#endif  // WWHD_ONLINE
