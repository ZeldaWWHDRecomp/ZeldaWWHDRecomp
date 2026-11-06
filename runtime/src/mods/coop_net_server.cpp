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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include <functional>
#include <map>
#include <mutex>

#include "coop_servers.h"
#include "mods.h"
#include "proto/client.h"
#include "../overlay/hostui.h"
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
std::vector<Invite> g_invites;  // (g_ui_mu: the pages read it)
// the pages' actions (main thread) run at the game's next poll, on its thread, where the client lives
std::mutex g_ui_mu;
std::vector<std::function<void(op::Client&)>> g_actions;
std::string g_auto_invite;     // test aid: a friend code (or @file with it) to invite once online
Handler* g_h = nullptr;
// each place's enemy runner as the server names it (X2): its changes, kept; by stage and room
std::map<std::pair<std::string, int8_t>, uint16_t> g_runner;
// the party's record, signed by its leader (made here while leading, else the leader's newest): sent again after a
// fresh welcome so the party survives a server restart (DESIGN 2.6 / 3.4; proto/messages.h PartyRecord)
constexpr uint8_t kAppPartyRecord = 0x7E;
// the party's switches as the server keeps them, for the pages and the game (g_ui_mu)
uint16_t g_flags = 0;
bool g_flags_known = false, g_leading = false;
op::crypto::Identity g_identity;
op::PartyRecord g_record;
bool g_have_record = false;
std::string g_end_why;         // why the server ended the session (a refusal, a removal), for the status after it stops
std::string g_msg;             // the newest answer (a party action's, a warning), shown with the status for a while
uint32_t g_msg_at = 0;
constexpr uint32_t kMsgMs = 20000;
// the server this session went to, and what it showed before the welcome (pinned in the saved list once welcomed)
std::string g_addr;
int g_port = 0;
std::string g_seen_name, g_seen_key;

// the saved servers (settings online.servers): read on any thread, saved on the main thread
std::mutex g_list_mu;
std::vector<servers::Server> g_list;
bool g_list_read = false;
std::vector<servers::Server>& list_locked() {
    if (!g_list_read) {
        g_list_read = true;
        std::string v;
        if (hostui::get("online.servers", v)) g_list = servers::parse(v);
    }
    return g_list;
}
void save_list_locked() {
    const std::string text = servers::format(g_list);
    hostui::post([text] { hostui::set("online.servers", text); });
}

void say(const std::string& m, bool toast) {
    g_msg = m, g_msg_at = now_ms();
    mods::trace("coop: server: says \"%s\"", m.c_str());
    if (toast) ss::notice("Online: " + m);
}

std::string path_of_identity() {
    if (const char* e = getenv("WWHD_ONLINE_IDENTITY")) return e;  // (tests: one identity per game)
    return host::config_dir() + "/online-identity.key";
}

// how many players this game shows at most (itself included): an instance's 32 on a computer; on a phone 9 until C12's
// measurement sets it (each puppet is ~100 more draws a frame; the phone is render-thread bound). WWHD_ONLINE_SHOWN: tests
int shown_cap() {
    static const int cap = [] {
#ifdef __ANDROID__
        int n = 9;
#else
        int n = kMaxPlayers;
#endif
        if (const char* e = getenv("WWHD_ONLINE_SHOWN")) n = atoi(e);
        return std::clamp(n, 2, (int)kMaxPlayers);
    }();
    return cap;
}

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
    for (const Peer& p : g_peers) if (!p.party && order.size() < (size_t)shown_cap()) order.push_back(p.sid);
    int me = 0;
    for (size_t i = 0; i < order.size() && i < (size_t)kMaxPlayers; i++) {
        Peer& s = next[i];
        s.on = true, s.sid = order[i];
        if (order[i] == g_me_sid) { me = (int)i, s.name = g_my_name, s.look = (uint32_t)g_my_tunic, s.party = in_party(); continue; }
        for (const Peer& p : g_peers) if (p.sid == order[i]) s = p, s.on = true;
        s.party = member_sid(order[i]);
        if (s.party)  // (a member by the name in the party's list: it comes before its pose, and strangers' names are hidden)
            for (const op::PartyMember& m : g_party.members) if (m.id == order[i] && !m.name.empty()) s.name = m.name;
    }
    const int was_me = g_slot_of_me;
    for (int i = 0; i < kMaxPlayers; i++) {
        const bool same = g_slot[i].on == next[i].on && g_slot[i].sid == next[i].sid && g_slot[i].party == next[i].party && was_me == me;
        if (same) { g_slot[i].name = next[i].name, g_slot[i].look = next[i].look; continue; }
        if (g_slot[i].on && i != was_me) h.left(i, g_slot[i].party ? "moved" : kStrangerGone);  // (a stranger's: no notice)
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

bool set_party_flags(uint16_t flags, std::string& why);
bool invite(const std::string& code, std::string& why);
void leave_party();
bool active() { return g_active; }

bool start(const std::string& server, int port, const std::string& my_name, int my_tunic, std::string& why) {
    stop(nullptr);
    op::crypto::Identity id;
    if (!op::crypto::Identity::load_or_create(path_of_identity(), id, why)) return false;
    g_identity = id;
    op::ClientConfig cfg;
    cfg.host = server, cfg.port = (uint16_t)port, cfg.game_data = ss::game_data_id(), cfg.identity = id;
    if (const char* e = getenv("WWHD_COOP_GAME")) cfg.game_data = strtoull(e, nullptr, 16);
    cfg.name = my_name.substr(0, 24), cfg.look = (uint32_t)(my_tunic & 3), cfg.tag = "wwhd";
    cfg.near_cap = (uint8_t)(shown_cap() - 1);  // (the server's near tier: no more poses than are shown)
#ifdef __ANDROID__
    cfg.device = op::kPhone;
#endif
    {  // a server visited before: its key as pinned then (trust on first use)
        std::lock_guard<std::mutex> l(g_list_mu);
        if (const servers::Server* s = servers::find(list_locked(), server, port); s && !s->key.empty()) {
            std::array<uint8_t, 32> k{};
            if (servers::from_hex(s->key, k.data(), 32)) cfg.pinned_server = k;
        }
    }
    g_client = new op::Client();
    if (!g_client->start(cfg, why)) { delete g_client, g_client = nullptr; return false; }
    g_active = true, g_my_name = my_name, g_my_tunic = my_tunic & 3, g_status = "Connecting to " + server;
    g_auto_invite = getenv("WWHD_ONLINE_INVITE") ? getenv("WWHD_ONLINE_INVITE") : "";
    g_addr = server, g_port = port, g_seen_name.clear(), g_seen_key.clear(), g_end_why.clear(), g_msg.clear();
    mods::trace("coop: server %s:%d (game %016llX)", server.c_str(), port, (unsigned long long)cfg.game_data);
    return true;
}

void stop(const char* why) {
    if (g_client) g_client->stop(), delete g_client, g_client = nullptr, mods::trace("coop: server session stopped%s%s", why ? ": " : "", why ? why : "");
    g_active = false, g_status = why ? why : "Off", g_me_sid = 0, g_peers.clear(), g_party = {}, g_slot_of_me = 0, g_where_known = false;
    g_runner.clear();
    {
        std::lock_guard<std::mutex> l(g_ui_mu);
        g_flags = 0, g_flags_known = g_leading = false;
    }
    {
        std::lock_guard<std::mutex> l(g_ui_mu);
        g_invites.clear(), g_actions.clear();
    }
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
// this install's code: from its identity (made on first use, kept in the user folder), online or not; any thread
std::string friend_code() {
    std::lock_guard<std::mutex> l(g_ui_mu);
    static std::string code, path;
    if (code.empty() || path != path_of_identity()) {
        op::crypto::Identity id;
        std::string why;
        path = path_of_identity();
        code = op::crypto::Identity::load_or_create(path, id, why) ? op::friend_code(op::key_hash(id.pub)) : "";
    }
    return code;
}

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

float g_place[3];
bool g_place_set = false;
void put_pose_place(const float at[3]) { memcpy(g_place, at, 12), g_place_set = true; }
void put_state(uint8_t kind, const std::string& body) {
    if (!g_client) return;
    // (the server's near tier is 15 Hz and its default upload budget 8 KB/s: every other of the 30 Hz poses goes,
    // ~380 bytes in the compact form (coop_pose.cpp, ~5.7 KB/s), and the other states at most 10 Hz)
    static uint32_t calls[256];
    const uint32_t n = calls[kind]++;
    if (kind == 1 ? n % 2 : n % 3) { if (kind == 1) g_place_set = false; return; }
    if (kind == 1 && body.size() >= 28) {  // a pose (coop_pose.h pack: u32 size, stage[8], room, flags, pos[3], s16 yaw)
        op::PoseHeader h;
        memcpy(h.where.stage.data(), body.data() + 4, 8);
        h.where.room = (int8_t)body[12];
        float p[3];
        memcpy(p, body.data() + 14, 12);
        int16_t yaw;
        memcpy(&yaw, body.data() + 26, 2);
        h.x = p[0], h.y = p[1], h.z = p[2], h.yaw = (uint16_t)yaw;
        if (g_place_set) h.x = g_place[0], h.y = g_place[1], h.z = g_place[2];  // (a pose in a deck's frame: its world place)
        if (body[13] & 1) h.flags |= 1;  // (on a deck: its frame)
        if (!g_where_known || !(h.where == g_where_sent)) g_client->send_stage(h.where), g_where_sent = h.where, g_where_known = true;
        g_client->send_pose(h, (const uint8_t*)body.data(), body.size());
        g_place_set = false;
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
                const std::string why = !g_end_why.empty() ? g_end_why : e.text.empty() ? "Disconnected from the server" : e.text;
                for (int i = 0; i < kMaxPlayers; i++) if (g_slot[i].on && i != g_slot_of_me) h.left(i, why.c_str());
                stop(why.c_str());
                h.left(0, why.c_str());  // (with the session off: given up until the switch or the settings change)
                return;
            }
            if (e.state == op::ClientState::kReconnecting) g_status = "Connection to the server lost (" + e.text + "), connecting again";
            break;
        case op::ClientEvent::kWelcome:
            g_me_sid = e.id;
            g_end_why.clear();
            if (!e.resumed) {
                for (int i = 0; i < kMaxPlayers; i++) if (g_slot[i].on && i != g_slot_of_me) h.left(i, "reconnected");
                g_peers.clear(), g_party = {}, g_runner.clear();
                for (Peer& p : g_slot) p = Peer{};
            }
            g_status = "Online, friend code " + g_client->friend_code();
            {  // kept in the saved servers, most recent first, its key pinned
                std::lock_guard<std::mutex> l(g_list_mu);
                servers::remember(list_locked(), {g_seen_name.empty() ? g_client->server().name : g_seen_name, g_addr, g_port, g_seen_key});
                save_list_locked();
            }
            mods::trace("coop: server: welcomed as %u%s, friend code %s", e.id, e.resumed ? " (resumed)" : "", g_client->friend_code().c_str());
            if (const char* f = getenv("WWHD_ONLINE_CODE_FILE")) std::ofstream(f) << g_client->friend_code() << "\n";  // (tests)
            if (!e.resumed && g_have_record) {  // (a new session, e.g. the server restarted: the party asked back)
                g_client->party_restore(g_record);
                mods::trace("coop: server: party %u asked back (%zu members, issued %u)", g_record.party, g_record.members.size(), g_record.issued);
            }
            remap(h);
            break;
        case op::ClientEvent::kRefused:
            g_end_why = servers::refusal((int)e.refusal, e.text == op::refusal_text(e.refusal) ? std::string() : e.text, e.seconds);
            g_status = g_end_why;
            mods::trace("coop: server refused (%d): %s", (int)e.refusal, e.text.c_str());
            say(g_end_why, false);
            break;
        case op::ClientEvent::kServerKey:
            g_seen_name = e.info.name, g_seen_key = servers::to_hex(e.info.pub, 32);
            mods::trace("coop: server: \"%s\", key %s", e.info.name.c_str(), e.text.c_str());
            break;
        case op::ClientEvent::kServerKeyChanged:
            g_end_why = servers::key_changed(g_addr + ":" + std::to_string(g_port));
            g_status = g_end_why;
            mods::trace("coop: server: %s", e.text.c_str());
            say(g_end_why, false);
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
                // (a server that stamps no time, ms 0: the time it came here, so the poses still follow one another)
                h.state(s, 1, p.ms ? p.ms : now_ms(), p.blob.data(), p.blob.size());
            }
            break;
        case op::ClientEvent::kApp: {
            const int s = slot_of(e.id);
            if (s < 0 || !member_sid(e.id)) break;  // (presence: nothing of a stranger's acts here)
            if (e.app_type == kAppPartyRecord) {  // (the leader's record: kept if signed by it and newer)
                op::PartyRecord r;
                if (op::decode_party_record(e.body.data(), e.body.size(), r) && op::party_record_signed_by_leader(r) &&
                    r.party == g_party.party && (!g_have_record || r.issued >= g_record.issued))
                    g_record = r, g_have_record = true;
                break;
            }
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
        {
            mods::trace("coop: server notice %u (%u): %s", e.value, e.seconds, e.text.c_str());
            const std::string m = servers::notice(e.value, e.seconds, e.value == op::kNoticeRestart ? e.seconds : 0, e.text);
            const bool out = e.value == op::kNoticeKick || e.value == op::kNoticeBan || e.value == op::kNoticeKickLimit;
            if (out) g_end_why = m;
            say(m, !out);  // (a removal: told when the session stops)
            break;
        }
        case op::ClientEvent::kPartyInvited:
        {
            std::lock_guard<std::mutex> l(g_ui_mu);
            g_invites.erase(std::remove_if(g_invites.begin(), g_invites.end(), [&](const Invite& i) { return i.party == e.party_id; }), g_invites.end());
            g_invites.push_back({e.party_id, e.player.name});
            if (g_invites.size() > 8) g_invites.erase(g_invites.begin());
            if (getenv("WWHD_ONLINE_ACCEPT")) g_client->party_accept(e.party_id), g_invites.pop_back();  // (tests)
        }
            mods::trace("coop: server: invited to party %u by %s", e.party_id, e.player.name.c_str());
            say(e.player.name + " invites you to their party: accept under Settings > Online (Invitations)", true);
            break;
        case op::ClientEvent::kPartyState:
            if (e.party.party != g_party.party) g_runner.clear();
            g_party = e.party;
            {
                std::lock_guard<std::mutex> l(g_ui_mu);
                if (e.party.party && e.party.flags != g_flags)
                    mods::trace("coop: server: party switches %04X (progress %d, rupees %d, friendly fire %d, together %d)", e.party.flags,
                                !!(e.party.flags & op::kPartySharedProgress), !!(e.party.flags & op::kPartySharedRupees),
                                !!(e.party.flags & op::kPartyFriendlyFire), !!(e.party.flags & op::kPartyStayTogether));
                g_flags = e.party.flags, g_flags_known = e.party.party != 0 && online_members() > 1;
                g_leading = g_flags_known && leader() == g_me_sid;
            }
            if (e.party.party && e.party.leader == g_me_sid && e.party.members.size() > 1) {  // (leading: the record made and given out)
                g_record = op::make_party_record(e.party, g_identity, (uint32_t)time(nullptr)), g_have_record = true;
                const op::Bytes b = op::encode_party_record(g_record);
                op::AppHeader h;
                h.type = kAppPartyRecord;
                g_client->send_app(h, b.data(), b.size(), true);
            }
            {
                std::lock_guard<std::mutex> l(g_ui_mu);
                g_invites.erase(std::remove_if(g_invites.begin(), g_invites.end(), [&](const Invite& i) { return i.party == e.party.party; }), g_invites.end());
            }
            mods::trace("coop: server: party %u, leader %u (acting %u), %d of %zu members online", e.party.party, e.party.leader, leader(),
                        online_members(), e.party.members.size());
            for (Peer& p : g_peers) p.party = member_sid(p.sid);
            remap(h);
            break;
        case op::ClientEvent::kRunners:
            for (const op::RunnerEntry& r : e.runners) {
                const std::pair<std::string, int8_t> at{std::string((const char*)r.where.stage.data(), strnlen((const char*)r.where.stage.data(), 8)), r.where.room};
                if (r.runner) g_runner[at] = r.runner;
                else g_runner.erase(at);
                mods::trace("coop: server: runner of %s room %d: %u", at.first.c_str(), at.second, r.runner);
            }
            break;
        case op::ClientEvent::kPartyResult:
            mods::trace("coop: server: party result %u: %s", e.value, e.text.c_str());
            if (e.value == 9) g_have_record = false;  // (not restored: invitations again)
            say(servers::party(e.value, e.text), true);
            break;
        default: break;  // (markers, emotes, copies, runners, team state: later items)
        }
    }
    {
        std::vector<std::function<void(op::Client&)>> todo;
        {
            std::lock_guard<std::mutex> l(g_ui_mu);
            todo.swap(g_actions);
        }
        if (g_client->state() == op::ClientState::kOnline) for (auto& f : todo) f(*g_client);
        else if (!todo.empty()) say("Not online yet: try again once the status says Online", false);
    }
    // test aid: WWHD_ONLINE_PARTY_FLAGS=<hex>: the party's switches set to that once this game leads a party
    if (static const char* pf = getenv("WWHD_ONLINE_PARTY_FLAGS"); pf && g_leading) {
        std::string why;
        if (set_party_flags((uint16_t)strtoul(pf, nullptr, 16), why)) mods::trace("coop: server: party switches asked: %s", pf);
        pf = nullptr;
    }
    // test aid: WWHD_ONLINE_INVITE=<friend code> or @<file holding it>: invited once online (the file may come later)
    if (!g_auto_invite.empty() && g_client->state() == op::ClientState::kOnline) {
        const std::string code = read_code(g_auto_invite);
        if (!code.empty()) {
            std::string why;
            if (invite(code, why)) mods::trace("coop: server: invited %s", code.c_str());
            else say(why, true);
            g_auto_invite.clear();
        }
    }
    // test aids for drop-in / drop-out (C17), main thread: WWHD_ONLINE_LEAVE_EVERY=<s>,<n>: this game leaves its party
    // <s> seconds after each time it is in one, <n> times; WWHD_ONLINE_INVITE_AGAIN=<s>,<code or @file>: once this game
    // has been in a party and is no longer with anyone in it, it invites that code again <s> seconds later
    if (g_client->state() == op::ClientState::kOnline) {
        static int leave_s = -1, leave_n = 0, again_s = -1;
        static std::string again_code;
        static uint32_t in_since = 0, out_since = 0;
        static bool was_in = false, ever_in = false, left = false;
        if (leave_s < 0) {
            leave_s = 0, again_s = 0;
            if (const char* e = getenv("WWHD_ONLINE_LEAVE_EVERY")) sscanf(e, "%d,%d", &leave_s, &leave_n);
            if (const char* e = getenv("WWHD_ONLINE_INVITE_AGAIN"); e && strchr(e, ','))
                again_s = atoi(e), again_code = strchr(e, ',') + 1;
        }
        const uint32_t now = now_ms();
        const bool in = in_party();
        if (in != was_in) (in ? in_since : out_since) = now, was_in = in, ever_in |= in, left = false;
        if (in && !left && leave_s > 0 && leave_n > 0 && now - in_since >= (uint32_t)leave_s * 1000) {
            leave_n--, left = true;  // (once per stay)
            mods::trace("coop: test: leaving the party (%d more)", leave_n);
            leave_party();
        }
        if (!in && ever_in && again_s > 0 && !again_code.empty() && out_since && now - out_since >= (uint32_t)again_s * 1000) {
            out_since = 0;  // (once per absence)
            std::string why;
            const std::string code = read_code(again_code);
            if (invite(code, why)) mods::trace("coop: test: invited %s again", code.c_str());
            else mods::trace("coop: test: not invited again: %s", why.c_str());
        }
    }
    if (g_client->state() == op::ClientState::kOnline) {
        int n = 0;
        for (const Peer& p : g_slot) n += p.on && p.party;
        const int ping = g_client->ping_ms();
        const std::string& srv_name = g_client->server().name;
        g_status = std::string(in_party() ? (role() == Role::kHost ? "Online, leading a party of " : "Online, in a party of ") + std::to_string(n) : "Online") +
                   (srv_name.empty() ? "" : " on " + srv_name) + (ping >= 0 ? ", ping " + std::to_string(ping) + " ms" : "") +
                   ", friend code " + g_client->friend_code();
    }
    if (!g_msg.empty() && now_ms() - g_msg_at < kMsgMs && g_status.find(g_msg) == std::string::npos) g_status += ". " + g_msg;
}

// (main thread) checked here, sent at the next poll
bool invite(const std::string& code, std::string& why) {
    uint64_t c40 = 0, mine = 0;
    if (!g_active) return why = "go online through a server first", false;
    if (!op::parse_friend_code(code, c40)) return why = servers::bad_code(), false;
    if (op::parse_friend_code(friend_code(), mine) && mine == c40) return why = servers::party(3, ""), false;
    std::lock_guard<std::mutex> l(g_ui_mu);
    g_actions.push_back([c40](op::Client& c) { c.party_invite(c40); });
    return true;
}
std::vector<std::pair<uint32_t, std::string>> invites() {
    std::lock_guard<std::mutex> l(g_ui_mu);
    std::vector<std::pair<uint32_t, std::string>> out;
    for (const Invite& i : g_invites) out.emplace_back(i.party, i.from);
    return out;
}
void answer(uint32_t party, bool yes) {
    std::lock_guard<std::mutex> l(g_ui_mu);
    g_actions.push_back([party, yes](op::Client& c) { yes ? c.party_accept(party) : c.party_decline(party); });
    g_invites.erase(std::remove_if(g_invites.begin(), g_invites.end(), [&](const Invite& i) { return i.party == party; }), g_invites.end());
}
void leave_party() {
    std::lock_guard<std::mutex> l(g_ui_mu);
    g_actions.push_back([](op::Client&) { g_have_record = false; });  // (left on purpose: not asked back)
    g_actions.push_back([](op::Client& c) { c.party_leave(); });
}

bool party_flags(uint16_t& flags, bool& leading) {
    std::lock_guard<std::mutex> l(g_ui_mu);
    if (!g_active || !g_flags_known) return false;
    flags = g_flags, leading = g_leading;
    return true;
}
bool set_party_flags(uint16_t flags, std::string& why) {
    std::lock_guard<std::mutex> l(g_ui_mu);
    if (!g_flags_known) return why = "you are in no party", false;
    if (!g_leading) return why = "only the party's leader changes these", false;
    g_actions.push_back([flags](op::Client& c) { c.party_settings(flags); });
    return true;
}

bool runners(std::vector<Runner>& out) {
    out.clear();
    if (!g_client || !in_party()) return false;
    for (const auto& [at, sid] : g_runner) {
        const int slot = sid == g_me_sid ? g_slot_of_me : slot_of(sid);
        if (slot < 0) continue;
        Runner r{};
        memcpy(r.stage, at.first.data(), std::min<size_t>(8, at.first.size()));
        r.room = at.second, r.slot = slot;
        out.push_back(r);
    }
    return true;
}

std::vector<servers::Server> saved_servers() {
    std::lock_guard<std::mutex> l(g_list_mu);
    return list_locked();
}
void forget_server(const std::string& addr, int port) {
    std::lock_guard<std::mutex> l(g_list_mu);
    if (servers::forget(list_locked(), addr, port)) save_list_locked();
}

}  // namespace mods::coop::net::srv

#endif  // WWHD_ONLINE
