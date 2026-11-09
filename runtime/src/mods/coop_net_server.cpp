// SPDX-License-Identifier: MPL-2.0
// Online co-op through a server (coop_net.h's second transport; GAPS C7): the same calls as the direct UDP transport,
// carried by online::proto::Client (runtime/src/online/proto, the server helper's library) to wwhd-online-server.
//
// The game side keeps the fork's model of players with local numbers (coop_sync.h; a party of up to kMaxParty): here number 0 is
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
#include "coop_block.h"
#include "coop_chat.h"

#if WWHD_ONLINE

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include <functional>
#include <mutex>
#include <map>

#include "coop_layout.h"
#include "coop_modset.h"
#include "coop_progress.h"
#include "coop_servers.h"
#include "mods.h"
#include "proto/client.h"
#include "../overlay/hostui.h"
#include "../platform/host.h"
#include "../savestate.h"

namespace mods::coop::net::srv {
namespace {

namespace op = online::proto;
static_assert(mods::coop::net::kMaxParty == online::proto::kMaxPartySize, "the game's party cap is the protocol's (C29)");

struct Peer {
    bool on = false;
    uint16_t sid = 0;   // server id
    uint64_t key = 0;
    std::string name;
    uint32_t look = 0;
    bool party = false;
};

// (C13) each player's tier as shown here, with the hold that keeps it from flickering
constexpr uint32_t kTierHoldMs = 250;
struct Tiered {
    uint8_t shown = op::kTierNear, next = op::kTierNear;
    uint32_t next_since = 0, shown_at = 0, switches = 0;
    uint32_t near_ms = 0, near_at = 0;  // the newest near pose's sender time, and when it came
    uint16_t sid = 0;                   // whose (a slot given to another player starts over)
};
Tiered g_tiered[kMaxPlayers];
#pragma pack(push, 1)
struct Afar { float x, y, z; uint16_t yaw, anim; uint8_t flags; };  // a mid or far pose to coop_sync (kMid, kFar)
#pragma pack(pop)
constexpr float kSeaOrigin = -350000.0f;  // the corner of square 0 (the server's default world.sea_origin)
std::mutex g_far_mu;
std::vector<Marker> g_markers;
uint32_t g_markers_at = 0;
// each party member's place from the server's PARTY_WHERE (1 Hz), by server id: what a mid pose lacks
struct Placed { op::Where where; uint32_t at = 0, since = 0; uint8_t square = 255; int32_t x = 0, z = 0; };
std::map<uint16_t, Placed> g_places;
std::vector<SeaMark> g_sea;  // (g_far_mu) the server's other players at sea, for the Sea Chart
uint32_t g_sea_at = 0;
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

// (C28) PvP at sea: the server's rules and this game's state (g_pvp_mu: the pages read them), the others' marks by
// server id, the events for coop_pvp (game thread), the toggle as last sent
std::mutex g_pvp_mu;
PvpRules g_pvp_rules;
uint8_t g_pvp_flags = 0;
uint32_t g_pvp_left = 0, g_pvp_left_at = 0;
std::map<uint16_t, uint8_t> g_pvp_marks;
std::vector<PvpEvent> g_pvp_events;
int g_pvp_sent = -1;  // on | busy << 1 as last sent (-1: not yet this session)
std::atomic<int> g_party_max{4};  // (C29) the server's party.max_size, as it says it
std::atomic<int> g_party_size{1};  // online members of this game's party (itself included), for the pages

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
    g_party_size = in_party() ? online_members() : 1;
    for (int i = 0; i < kMaxPlayers; i++)
        if (g_slot[i].on && i != me && g_slot[i].party && (old[i].sid != g_slot[i].sid || !old[i].on || !old[i].party || was_me != me))
            h.joined(i);
}

// every code of a spec: itself, or each non-empty line of the file after '@'
std::vector<std::string> read_codes(const std::string& spec) {
    if (spec.empty()) return {};
    if (spec[0] != '@') return {spec};
    std::ifstream f(spec.substr(1));
    std::vector<std::string> out;
    for (std::string line; std::getline(f, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
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

void tell_mute(uint64_t key, bool on) { if (g_client && key) g_client->mute(key, on); }
// text chat (coop_chat.h): the channels turned off, told at each welcome and on a change
std::atomic<uint8_t> g_chat_off{0};
void chat_send(int channel, const std::string& text) {
    std::lock_guard<std::mutex> l(g_ui_mu);
    g_actions.push_back([channel, text](op::Client& c) { c.send_chat(uint8_t(channel), text); });
}
void chat_prefs(uint8_t off) {
    if (g_chat_off.exchange(off) == off) return;
    std::lock_guard<std::mutex> l(g_ui_mu);
    g_actions.push_back([](op::Client& c) { c.chat_prefs(g_chat_off.load()); });
}
// (C5 through a server) the problems a server or a party named, and the party's set from its invitation
std::mutex g_mods_mu;
std::vector<ModIssue> g_mod_issues;
std::string g_party_set;  // the party's gameplay mods (coop_modset's wire form) after a party refused this game's
std::map<uint32_t, std::string> g_invite_sets;  // each invitation's party set
std::vector<ModIssue> issues_of(const std::vector<op::ModProblem>& ps) {
    std::vector<ModIssue> out;
    for (const op::ModProblem& p : ps) out.push_back({(uint8_t)p.problem, p.id, p.wanted, p.have});
    return out;
}
std::string set_of(const std::vector<op::ModRef>& refs) {
    modset::Set s;
    for (const op::ModRef& r : refs) {
        modset::ModRef m;
        m.id = r.id, m.version = r.version, m.hash = r.hash;
        s.push_back(std::move(m));
    }
    return modset::encode(s);
}
std::vector<ModIssue> mod_issues() { std::lock_guard<std::mutex> l(g_mods_mu); return g_mod_issues; }
std::string refused_mods() { std::lock_guard<std::mutex> l(g_mods_mu); return g_party_set; }
// a server's rules in words (the server list's "Rules")
std::string rules_words(const op::ServerInfo& info) {
    const op::ModRules& r = info.mods;
    if (r.vanilla_only) return "No mods that change the game.";
    if (r.empty()) return "Any mods.";
    std::string req, allow, ban;
    for (const op::ModRule& m : r.rules) {
        std::string& to = m.kind == op::ModRuleKind::kRequired ? req : m.kind == op::ModRuleKind::kBanned ? ban : allow;
        to += (to.empty() ? "" : ", ") + modset::name_of(m.id) + (m.version.empty() ? "" : " " + m.version);
    }
    std::string out;
    if (!req.empty()) out += "Needs " + req + ". ";
    if (!allow.empty()) out += "Allows only " + allow + (req.empty() ? "" : " (and the ones it needs)") + ". ";
    if (!ban.empty()) out += "Bans " + ban + ".";
    return out;
}
struct Query { op::Client* client = nullptr; std::string key; uint32_t since = 0; };
Query g_query;
std::map<std::string, std::string> g_rules_seen;  // "addr:port": the rules in words
void query_rules(const std::string& addr, int port) {
    std::lock_guard<std::mutex> l(g_mods_mu);
    if (g_query.client) return;  // (one at a time)
    op::ClientConfig cfg;
    cfg.host = addr, cfg.port = (uint16_t)port, cfg.game_data = ss::game_data_id(), cfg.query_only = true, cfg.tag = "wwhd";
    cfg.reconnect = false;
    std::string why;
    op::crypto::Identity id;
    if (!op::crypto::Identity::load_or_create(path_of_identity(), id, why)) return;
    cfg.identity = id;
    auto* c = new op::Client();
    const std::string key = addr + ":" + std::to_string(port);
    if (!c->start(cfg, why)) { delete c; g_rules_seen[key] = "Can't ask: " + why; return; }
    g_query = {c, key, now_ms()};
    g_rules_seen[key] = "Asking...";
}
std::string server_rules(const std::string& addr, int port) {
    std::lock_guard<std::mutex> l(g_mods_mu);
    const auto it = g_rules_seen.find(addr + ":" + std::to_string(port));
    return it == g_rules_seen.end() ? "" : it->second;
}
// each poll: the server list's question answered (or given up after 10 s)
void poll_query() {
    std::lock_guard<std::mutex> l(g_mods_mu);
    if (!g_query.client) return;
    bool done = false;
    for (const op::ClientEvent& e : g_query.client->poll(now_ms())) {
        if (e.kind == op::ClientEvent::kServerRules) g_rules_seen[g_query.key] = rules_words(e.info), done = true;
        else if (e.kind == op::ClientEvent::kRefused || e.kind == op::ClientEvent::kServerKeyChanged) g_rules_seen[g_query.key] = "No answer: " + e.text, done = true;
        else if (e.kind == op::ClientEvent::kStateChanged && e.state == op::ClientState::kStopped) done = true;
    }
    if (!done && now_ms() - g_query.since > 10000) g_rules_seen[g_query.key] = "No answer from the server", done = true;
    if (done) {
        mods::trace("coop: rules of %s: %s", g_query.key.c_str(), g_rules_seen[g_query.key].c_str());
        g_query.client->stop();
        delete g_query.client;
        g_query = {};
    }
}
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
    for (const modset::ModRef& m : modset::current()) {  // (C5) this game's gameplay mods, for the server's rules and parties
        op::ModRef r;
        r.id = m.id, r.version = m.version, r.hash = m.hash;
        cfg.mods.push_back(std::move(r));
    }
    {
        std::lock_guard<std::mutex> l(g_mods_mu);
        g_mod_issues.clear(), g_party_set.clear();
    }
    {
        std::string list;
        for (const op::ModRef& r : cfg.mods) list += " " + r.id + (r.version.empty() ? "" : "@" + r.version);
        mods::trace("coop: gameplay mods sent:%s", list.empty() ? " none" : list.c_str());
    }
    // test aid: WWHD_COOP_TEST_RULES=1: the server's mod rules asked for first, as the server list's button does
    if (getenv("WWHD_COOP_TEST_RULES")) query_rules(server, port);
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
    g_active = false, g_status = why ? why : "Off", g_me_sid = 0, g_peers.clear(), g_party = {}, g_party_size = 1, g_slot_of_me = 0, g_where_known = false;
    for (Tiered& t : g_tiered) t = Tiered{};
    chat::session_ended();
    { std::lock_guard<std::mutex> l(g_far_mu); g_markers.clear(), g_markers_at = 0, g_sea.clear(), g_sea_at = 0; }
    { std::lock_guard<std::mutex> l(g_pvp_mu); g_pvp_rules = {}, g_pvp_flags = 0, g_pvp_marks.clear(), g_pvp_events.clear(), g_pvp_sent = -1; }
    g_runner.clear(), g_places.clear();
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
    for (int i = 0, n = 0; i <= id; i++) {
        if (!member(i)) continue;
        if (n++ % 4 == 0) worn[0] = worn[1] = worn[2] = worn[3] = false;  // (C29: past four, the colours go round again)
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

// (X3) shared progress also to the server's team store, in any party (alone in it too): a member who comes later, with
// nobody else online, gets it from there. Our records (coop_progress.cpp: u16 offset, u8 size, the value, and for bit
// fields the changed bits) as the store's fields; counts as they come (small keys) are not stored
void store_progress(const std::string& sealed) {
    if (!g_client || !g_party.party) return;
    std::string body = sealed;
    if (!layout::open(body)) return;  // (its CRC-32: coop_layout.h)
    std::vector<op::TeamField> fields;
    const auto* d = (const uint8_t*)body.data();
    for (size_t i = 0; i + 3 <= body.size();) {
        const uint16_t off = (uint16_t)(d[i] | d[i + 1] << 8);
        const uint8_t size = d[i + 2];
        const int mode = progress::store_mode(off, size);
        const size_t len = 3 + size * (mode == 0 ? 2 : 1);
        if (i + len > body.size() || !size || size > op::kMaxTeamFieldBytes) break;
        if (mode >= 0) {
            op::TeamField f;
            f.offset = off, f.mode = (uint8_t)mode, f.value.assign((const char*)d + i + 3, size);
            if (mode != 0 || std::any_of(f.value.begin(), f.value.end(), [](char c) { return c != 0; })) fields.push_back(std::move(f));
        }
        i += len;
    }
    if (!fields.empty()) g_client->send_progress(fields);
}

// what this game uploads, by kind (0x000-0x0FF: an app message's type, 0x100 + kind: a state, 0x200 the pose), for
// the upload trace in poll(): the server strikes a game over its upload limit (bytes_per_s, 8 KB/s by default)
uint64_t g_up[0x201];
double g_up_limit = 8192;  // the server's upload limit (bytes/s): its default, or as its throttle notice names it
void send(int to, Msg type, const std::string& body) {
    g_up[(uint8_t)type] += body.size();
    if (type == kProgress) store_progress(body);  // (a member's changes, and its whole team state at a join: the store has all)
    if (type == kEmote) {  // (the server's EMOTE: to everyone near who sees this player, party or not; rate-limited there)
        if (g_client && body.size() == 1) g_client->send_emote((uint8_t)body[0]);
        return;
    }
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
    // The server strikes a game over its upload limit (limits.bytes_per_s, 8 KB/s unless its throttle named another):
    // poses are the bulk (sailing, the boat's models with Link's: ~530 bytes at 15 Hz, ~8 KB/s), so while this game's
    // upload of the last 2 s is over 3/4 of the limit a pose is left out, as long as one went in the last 200 ms
    // (5 a second at least)
    if (kind == 1) {
        static uint32_t last_pose = 0;
        static std::pair<uint32_t, uint64_t> mark[2] = {};  // (ms, bytes up) a second apart: the rate over 1-2 s
        const uint32_t t = now_ms();
        const uint64_t up = g_client->stats().bytes_up;
        if (up < mark[1].second) mark[0] = mark[1] = {};  // (a new connection: its own count)
        if (t - mark[1].first >= 1000) mark[0] = mark[1], mark[1] = {t, up};
        const double rate = mark[0].first && t > mark[0].first ? (double)(up - mark[0].second) * 1000.0 / (t - mark[0].first) : 0;
        if (rate > 0.75 * g_up_limit && t - last_pose < 200) {
            static uint32_t left_out = 0;
            if (left_out++ % 300 == 0) mods::trace("coop: server: upload %.0f B/s: poses thinned (%u left out so far)", rate, left_out);
            g_place_set = false;
            return;
        }
        last_pose = t;
    }
    g_up[kind == 1 ? 0x200 : 0x100 + kind] += body.size();
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
        if (body[13] & 0x08) h.flags |= 2;  // (sailing, coop_pose.h kSailing: a mid pose says so, protocol.h PoseHeader bit 1)
        {  // (a pose the server would refuse is a protocol strike, and enough of them a kick: never sent, told once)
            op::Bytes t;
            op::Writer w(t);
            op::put_pose_header(w, h);
            op::Reader r(t.data(), t.size());
            (void)op::get_pose_header(r);
            if (!r.ok() || body.size() > op::kMaxPoseBlob) {
                if (static uint32_t told = 0; told++ % 300 == 0)
                    mods::trace("coop: server: a pose not sent (%u so far): stage %.8s room %d at %f %f %f, %zu bytes", told, h.where.stage.data(),
                                h.where.room, h.x, h.y, h.z, body.size());
                g_place_set = false;
                return;
            }
        }
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
    // the upload of the last 10 s, traced when over 5 KB/s: the client's bytes (with the protocol's own) and the
    // biggest kinds
    if (static uint32_t since = 0; mods::trace_on() && now_ms() - since >= 10000) {
        static uint64_t was_up = 0, was[0x201];
        const uint64_t up = g_client->stats().bytes_up;
        if (since && up >= was_up && up - was_up > 50000) {
            std::vector<std::pair<uint64_t, int>> top;
            for (int k = 0; k <= 0x200; k++) if (g_up[k] > was[k]) top.push_back({g_up[k] - was[k], k});
            std::sort(top.rbegin(), top.rend());
            std::string list;
            for (size_t i = 0; i < top.size() && i < 5; i++)
                list += " " + std::string(top[i].second == 0x200 ? "pose" : top[i].second >= 0x100 ? "state " + std::to_string(top[i].second - 0x100)
                                                                                                    : "app " + std::to_string(top[i].second)) +
                        " " + std::to_string(top[i].first / 10) + " B/s";
            mods::trace("coop: server: upload %llu B/s:%s", (unsigned long long)(up - was_up) / 10, list.c_str());
        }
        was_up = up, since = now_ms();
        memcpy(was, g_up, sizeof was);
    }
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
            if (!e.resumed) block::resend();  // (C14: the server keeps blocks in memory only)
            g_client->chat_prefs(g_chat_off.load());  // (a handover's new worker too: it knows only what it is told)
            // (C25: UDP blocked, the client library went over TCP to the same address)
            mods::trace("coop: online over %s", g_client->transport() == op::Transport::kTcp ? "TCP" : "UDP");
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
            if (e.refusal == op::Refusal::kMods) {  // (C5) each mod named
                std::vector<modset::Problem> ps;
                for (const op::ModProblem& p : e.mod_problems) ps.push_back({(uint8_t)p.problem, p.id, p.wanted, p.have});
                g_end_why = "The server doesn't take your mods: " + modset::describe(ps) + ".";
                std::lock_guard<std::mutex> l(g_mods_mu);
                g_mod_issues = issues_of(e.mod_problems);
            }
            g_status = g_end_why;
            mods::trace("coop: server refused (%d): %s", (int)e.refusal, e.text.c_str());
            say(g_end_why, false);
            break;
        case op::ClientEvent::kServerKey:
            g_seen_name = e.info.name, g_seen_key = servers::to_hex(e.info.pub, 32);
            g_party_max = e.info.party_max ? std::clamp((int)e.info.party_max, 2, kMaxParty) : 4;  // (C29: 4 from a server that does not say)
            mods::trace("coop: server: \"%s\", key %s, parties of up to %d", e.info.name.c_str(), e.text.c_str(), g_party_max.load());
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
            {  // (C28) PvP active as they come into view (bit 3); later changes come as PVP_MARKS
                std::lock_guard<std::mutex> l(g_pvp_mu);
                if (e.player.flags & 8) g_pvp_marks[e.player.id] = 1;
                else g_pvp_marks.erase(e.player.id);
            }
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
                if (s < 0) continue;
                // (C13) the tier a player is shown at changes only once the new one has come for kTierHoldMs, or the
                // old one stopped coming that long: a player at a sub-cell border does not flicker between them
                Tiered& t = g_tiered[s];
                if (t.sid != p.id) t = Tiered{}, t.sid = p.id;
                const uint32_t now = now_ms();
                if (p.tier != t.shown) {
                    if (p.tier != t.next) t.next = p.tier, t.next_since = now;
                    if (now - t.next_since < kTierHoldMs && now - t.shown_at < kTierHoldMs && t.shown_at) continue;
                    t.shown = p.tier, t.switches++;
                    mods::trace("coop: player %d shown %s", s, p.tier == op::kTierNear ? "near" : p.tier == op::kTierMid ? "at mid range" : "far");
                }
                t.shown_at = now;
                if (p.tier == op::kTierNear && !p.blob.empty()) {
                    // (a server that stamps no time, ms 0: the time it came here, so the poses still follow one another)
                    if (p.ms) t.near_ms = p.ms, t.near_at = now;
                    h.state(s, 1, p.ms ? p.ms : now, p.blob.data(), p.blob.size());
                } else if (p.tier == op::kTierMid || p.tier == op::kTierFar) {
                    // mid: its place, heading and state for a cheap puppet; far: a dot on the radar. Their time: the
                    // sender's, from its last full one and the low 16 bits a mid pose carries (else this game's)
                    uint32_t ms = now;
                    if (p.tier == op::kTierMid && t.near_at) {
                        const uint32_t guess = t.near_ms + (now - t.near_at);
                        ms = (guess & 0xFFFF0000u) | (p.ms & 0xFFFFu);
                        if ((int32_t)(ms - guess) > 32768) ms -= 0x10000;
                        else if ((int32_t)(guess - ms) > 32768) ms += 0x10000;
                    }
                    Afar a{p.head.x, p.head.y, p.head.z, p.head.yaw, p.head.anim, p.head.flags};
                    h.state(s, p.tier == op::kTierMid ? 6 : 7, ms, (const uint8_t*)&a, sizeof a);
                }
            }
            if (static uint32_t last = 0; now_ms() - last >= 5000) {
                last = now_ms();
                int n[3] = {}, sw = 0;
                for (int i = 0; i < kMaxPlayers; i++)
                    if (g_slot[i].on && i != g_slot_of_me && g_tiered[i].shown_at && now_ms() - g_tiered[i].shown_at < 2000) n[g_tiered[i].shown]++, sw += g_tiered[i].switches;
                mods::trace("coop: tiers: %d near, %d at mid range, %d far; %d tier changes so far", n[0], n[1], n[2], sw);
            }
            break;
        case op::ClientEvent::kPartyWhere:  // where each party member is (the stage and room a mid pose does not carry)
            for (const op::ClientEvent::MemberWhere& m : e.members) {
                Placed& pl = g_places[m.id];
                if (!pl.at || !(pl.where == m.where)) {
                    if (pl.at) mods::trace("coop: server: player %u now in %.8s room %d", m.id, m.where.stage.data(), m.where.room);
                    pl.where = m.where, pl.since = now_ms();
                }
                pl.at = now_ms(), pl.square = m.square, pl.x = m.x, pl.z = m.z;
            }
            break;
        case op::ClientEvent::kSeaPlayers: {  // every other player at sea (the Sea Chart): their names and places
            std::lock_guard<std::mutex> l(g_far_mu);
            g_sea.clear();
            for (const op::SeaPlayer& p : e.sea_players) {
                const float sx = kSeaOrigin + float(p.square % op::kSeaSquares) * op::kSquareUnits, sz = kSeaOrigin + float(p.square / op::kSeaSquares) * op::kSquareUnits;
                g_sea.push_back({p.name, sx + (p.x + 0.5f) / 256.f * op::kSquareUnits, sz + (p.z + 0.5f) / 256.f * op::kSquareUnits});
            }
            g_sea_at = now_ms();
            break;
        }
        case op::ClientEvent::kMarkers: {  // (C13) the rest of the square: dots on the radar
            std::lock_guard<std::mutex> l(g_far_mu);
            g_markers.clear();
            for (const op::Marker& m : e.markers) {
                const int s = slot_of(m.id);
                if (s < 0) continue;
                const float sx = kSeaOrigin + float(m.square % op::kSeaSquares) * op::kSquareUnits, sz = kSeaOrigin + float(m.square / op::kSeaSquares) * op::kSquareUnits;
                g_markers.push_back({s, sx + (m.x + 0.5f) / 256.f * op::kSquareUnits, sz + (m.z + 0.5f) / 256.f * op::kSquareUnits, (m.flags & 1) != 0});
            }
            g_markers_at = now_ms();
            break;
        }
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
            // (the server's upload limit, as its throttle names it: the poses keep under it from now on)
            if (e.seconds == op::kLimitUpload)
                if (const size_t at = e.text.find("limit of "); at != std::string::npos)
                    if (const double lim = atof(e.text.c_str() + at + 9); lim >= 1024) g_up_limit = lim, mods::trace("coop: server: upload limit %.0f B/s", lim);
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
            {
                std::lock_guard<std::mutex> lm(g_mods_mu);
                g_invite_sets[e.party_id] = set_of(e.mods);  // (C5: the party's mods, for "use the party's mods")
            }
            if (g_invites.size() > 8) g_invites.erase(g_invites.begin());
            if (getenv("WWHD_ONLINE_ACCEPT")) g_client->party_accept(e.party_id), g_invites.pop_back();  // (tests)
        }
            mods::trace("coop: server: invited to party %u by %s", e.party_id, e.player.name.c_str());
            say(e.player.name + " invites you to their party: accept under Settings > Online (Invitations)", true);
            break;
        case op::ClientEvent::kPartyState:
            if (e.party.party != g_party.party) g_runner.clear();
            g_party = e.party;
            if (e.party.max) g_party_max = std::clamp((int)e.party.max, 2, kMaxParty);
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
            if (e.value == 10) {  // (C5) the party plays with other gameplay mods: each named, and its set kept
                std::vector<modset::Problem> ps;
                for (const op::ModProblem& p : e.mod_problems) ps.push_back({(uint8_t)p.problem, p.id, p.wanted, p.have});
                std::lock_guard<std::mutex> l(g_mods_mu);
                g_mod_issues = issues_of(e.mod_problems);
                for (const auto& [party, set] : g_invite_sets) if (!set.empty()) g_party_set = set;  // (the newest invitation's)
                say("Your mods that change the game differ from the party's: " + modset::describe(ps) + ".", false);
                mods::trace("coop: the party's mods differ: %s", modset::describe(ps).c_str());
                break;
            }
            if (e.value == 9) g_have_record = false;  // (not restored: invitations again)
            say(servers::party(e.value, e.text), true);
            break;
        case op::ClientEvent::kTeamState: {  // (X3) the server's team store: at a join, and others' changes
            if (!g_party.party || !party_flag(kShareProgress, true)) break;
            std::string m;
            for (const op::TeamField& f : e.fields) {
                if (f.value.empty() || f.value.size() > op::kMaxTeamFieldBytes || progress::store_mode(f.offset, (uint8_t)f.value.size()) != f.mode) continue;
                m += std::string{(char)(f.offset & 0xFF), (char)(f.offset >> 8), (char)f.value.size()};
                m += f.value;
                if (f.mode == 0) m += f.value;  // (bits: those set, as the changed ones)
            }
            mods::trace("coop: server: team state %u, %zu fields", e.version, e.fields.size());
            if (!m.empty()) m = layout::seal(m), progress::received((const uint8_t*)m.data(), m.size());
            break;
        }
        case op::ClientEvent::kEmote: {  // (C22) a player near by, in the party or not: only its puppet plays it
            const int s = slot_of(e.id);
            const uint8_t id = (uint8_t)e.value;
            if (s >= 0) h.message(s, kEmote, &id, 1);
            break;
        }
        case op::ClientEvent::kChatRules:  // the server's chat (coop_chat.h); none came: no chat
            chat::server_rules({e.chat_rules.enabled, e.chat_rules.global, (int)e.chat_rules.max_length});
            mods::trace("coop: server: chat %s, global %s, up to %u characters, %u lines per %u s", e.chat_rules.enabled ? "on" : "off",
                        e.chat_rules.global ? "on" : "off", e.chat_rules.max_length, e.chat_rules.rate_count, e.chat_rules.rate_window_s);
            break;
        case op::ClientEvent::kChatFrom: {
            chat::Line l;
            l.channel = e.chat.channel, l.key = e.chat.key_hash, l.name = e.chat.name, l.text = e.chat.text;
            l.party = (e.chat.flags & op::kChatFromParty) != 0, l.yours = (e.chat.flags & op::kChatYours) != 0;
            chat::server_line(l);
            break;
        }
        case op::ClientEvent::kChatResult:
            chat::server_result(e.chat.channel, e.text);
            mods::trace("coop: server: chat line not sent (%s line): %s", e.chat.channel ? "global" : "party", e.text.c_str());
            break;
        case op::ClientEvent::kPvpRules: {
            PvpRules r;
            r.mode = (int)e.pvp_rules.mode, r.protect_ms = e.pvp_rules.protect_ms, r.enter_delay_ms = e.pvp_rules.enter_delay_ms;
            r.hide_band = e.pvp_rules.hide_band, r.max_hit_dist = e.pvp_rules.max_hit_dist;
            r.ko_rupee_percent = e.pvp_rules.ko_rupee_percent, r.ko_rupee_max = e.pvp_rules.ko_rupee_max;
            for (const op::PvpZone& z : e.pvp_rules.zones) {
                const float cx = kSeaOrigin + (float(z.square % op::kSeaSquares) + 0.5f) * op::kSquareUnits + z.cx;
                const float cz = kSeaOrigin + (float(z.square / op::kSeaSquares) + 0.5f) * op::kSquareUnits + z.cz;
                r.zones.push_back({(int)z.square, cx, cz, z.radius});
            }
            mods::trace("coop: server: PvP rules: mode %d, %zu safe zones, protection %u ms, knockout %d%% (at most %d)", r.mode, r.zones.size(),
                        r.protect_ms, r.ko_rupee_percent, r.ko_rupee_max);
            std::lock_guard<std::mutex> l(g_pvp_mu);
            g_pvp_rules = std::move(r);
            break;
        }
        case op::ClientEvent::kPvpState: {
            std::lock_guard<std::mutex> l(g_pvp_mu);
            if (g_pvp_flags != (uint8_t)e.value) mods::trace("coop: server: PvP state %02X (protected %u ms)", e.value, e.value16);
            g_pvp_flags = (uint8_t)e.value, g_pvp_left = e.value16, g_pvp_left_at = now_ms();
            break;
        }
        case op::ClientEvent::kPvpMarks: {
            std::lock_guard<std::mutex> l(g_pvp_mu);
            for (const auto& [sid, f] : e.pvp_marks) {
                if (f) g_pvp_marks[sid] = f;
                else g_pvp_marks.erase(sid);
            }
            break;
        }
        case op::ClientEvent::kPvpHitOn:
        case op::ClientEvent::kPvpShotOf:
        case op::ClientEvent::kPvpKoOf:
        case op::ClientEvent::kPvpRupees:
        case op::ClientEvent::kPvpRespawn: {
            PvpEvent v{};
            const auto who = [&](uint16_t sid) { return sid == g_me_sid ? g_slot_of_me : slot_of(sid); };
            if (e.kind == op::ClientEvent::kPvpHitOn) {
                v.kind = PvpEvent::kHitOn, v.from = slot_of(e.pvp_hit.other), v.what = e.pvp_hit.kind, v.quarters = e.pvp_hit.damage;
                v.at[0] = e.pvp_hit.x, v.at[1] = e.pvp_hit.y, v.at[2] = e.pvp_hit.z;
            } else if (e.kind == op::ClientEvent::kPvpShotOf) {
                v.kind = PvpEvent::kShotOf, v.from = slot_of(e.pvp_shot.shooter), v.what = e.pvp_shot.kind;
                v.at[0] = e.pvp_shot.x, v.at[1] = e.pvp_shot.y, v.at[2] = e.pvp_shot.z, v.v[0] = e.pvp_shot.vx, v.v[1] = e.pvp_shot.vy, v.v[2] = e.pvp_shot.vz;
            } else if (e.kind == op::ClientEvent::kPvpKoOf) {
                v.kind = PvpEvent::kKoOf, v.from = who(e.id), v.by = who(e.value16);
                if (v.by >= 0) v.by_name = g_slot[v.by].name;
            } else if (e.kind == op::ClientEvent::kPvpRupees) {
                v.kind = PvpEvent::kRupees, v.value = e.value16;
            } else {
                v.kind = PvpEvent::kRespawn, v.value = e.value, v.at[0] = e.pvp_hit.x, v.at[2] = e.pvp_hit.z;
            }
            std::lock_guard<std::mutex> l(g_pvp_mu);
            if (g_pvp_events.size() < 64) g_pvp_events.push_back(std::move(v));
            break;
        }
        default: break;  // (markers, copies: later items)
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
        // test aid: WWHD_ONLINE_INVITE_EVERY=<s>: invited again every <s> seconds while nobody else is in the party (C5:
        // one refused for its mods comes back with the party's mods); else once
        static const int every = getenv("WWHD_ONLINE_INVITE_EVERY") ? atoi(getenv("WWHD_ONLINE_INVITE_EVERY")) : 0;
        static uint32_t last = 0;
        bool alone = true;
        for (const Peer& p : g_slot) alone &= !(p.on && p.party);
        // (C29) @file may list many codes, one a line; WWHD_ONLINE_INVITE_GAP=<ms> between them (the server limits invites)
        static std::vector<std::string> pending;
        static uint32_t gap_last = 0;
        static const uint32_t gap = getenv("WWHD_ONLINE_INVITE_GAP") ? (uint32_t)atoi(getenv("WWHD_ONLINE_INVITE_GAP")) : 0;
        const std::vector<std::string> codes = pending.empty() ? read_codes(g_auto_invite) : std::vector<std::string>{};
        if (!codes.empty() && (!last || (every > 0 && alone && now_ms() - last >= (uint32_t)every * 1000))) {
            last = now_ms();
            pending.assign(codes.rbegin(), codes.rend());
            if (every <= 0) g_auto_invite = "@-";  // (no file: nothing more after these)
        }
        while (!pending.empty() && (!gap_last || now_ms() - gap_last >= gap)) {
            std::string why;
            const std::string code = pending.back();
            pending.pop_back(), gap_last = now_ms();
            if (invite(code, why)) mods::trace("coop: server: invited %s", code.c_str());
            else say(why, true), mods::trace("coop: server: not invited %s: %s", code.c_str(), why.c_str());
            if (gap) break;
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
                   (srv_name.empty() ? "" : " on " + srv_name) + (g_client->transport() == op::Transport::kTcp ? " (TCP)" : "") +
                   (ping >= 0 ? ", ping " + std::to_string(ping) + " ms" : "") +
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
    if (g_party_size >= g_party_max)  // (C29; the server checks it too, counting invitations still open)
        return why = "Your party is full (" + std::to_string(g_party_size.load()) + " of up to " + std::to_string(g_party_max.load()) + " on this server)", false;
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


// ---- (C28) PvP at sea ----
PvpRules pvp_rules() { std::lock_guard<std::mutex> l(g_pvp_mu); return g_pvp_rules; }
uint8_t pvp_state(uint32_t* left_ms) {
    std::lock_guard<std::mutex> l(g_pvp_mu);
    if (left_ms) {
        const uint32_t gone = now_ms() - g_pvp_left_at;
        *left_ms = (g_pvp_flags & op::kPvpProtected) && g_pvp_left > gone ? g_pvp_left - gone : 0;
    }
    return g_pvp_flags;
}
uint8_t pvp_mark(int id) {
    if (id < 0 || id >= kMaxPlayers || !g_slot[id].on || id == g_slot_of_me) return 0;
    std::lock_guard<std::mutex> l(g_pvp_mu);
    const auto it = g_pvp_marks.find(g_slot[id].sid);
    return it == g_pvp_marks.end() ? 0 : it->second;
}
void pvp_flag(bool on, bool busy) {
    if (!g_client || g_client->state() != op::ClientState::kOnline) return;
    const int now = (on ? 1 : 0) | (busy ? 2 : 0);
    {
        std::lock_guard<std::mutex> l(g_pvp_mu);
        if (now == g_pvp_sent) return;
        g_pvp_sent = now;
    }
    g_client->pvp_flag(on, busy);
    mods::trace("coop: PvP toggle %s%s", on ? "on" : "off", busy ? ", busy" : "");
}
void pvp_hit(int victim, uint8_t kind, uint8_t quarters, const float at[3]) {
    if (!g_client || victim < 0 || victim >= kMaxPlayers || !g_slot[victim].on || victim == g_slot_of_me) return;
    op::PvpHit h;
    h.other = g_slot[victim].sid, h.kind = kind, h.damage = std::min<uint8_t>(quarters, 16), h.x = at[0], h.y = at[1], h.z = at[2];
    g_client->pvp_hit(h);
}
void pvp_shot(uint8_t kind, const float at[3], const float v[3]) {
    if (!g_client) return;
    op::PvpShot s;
    s.kind = kind, s.x = at[0], s.y = at[1], s.z = at[2], s.vx = v[0], s.vy = v[1], s.vz = v[2];
    g_client->pvp_shot(s);
}
void pvp_ko(int by, uint16_t wallet) {
    if (!g_client || by < 0 || by >= kMaxPlayers || !g_slot[by].on) return;
    g_client->pvp_ko(g_slot[by].sid, wallet);
}
std::vector<PvpEvent> pvp_take() {
    std::lock_guard<std::mutex> l(g_pvp_mu);
    std::vector<PvpEvent> out;
    out.swap(g_pvp_events);
    return out;
}
int party_max() { return g_party_max; }
int party_size() { return g_party_size; }

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

uint64_t player_key(int id) { return id >= 0 && id < kMaxPlayers && g_slot[id].on ? g_slot[id].key : 0; }
void tell_block(uint64_t key, bool on) { if (g_client && key) g_client->block(key, on); }
std::vector<Marker> markers() {
    std::lock_guard<std::mutex> l(g_far_mu);
    if (!g_markers_at || now_ms() - g_markers_at > 10000) return {};
    return g_markers;
}
std::vector<SeaMark> sea_players() {
    std::lock_guard<std::mutex> l(g_far_mu);
    if (!g_sea_at || now_ms() - g_sea_at > 5000) return {};
    return g_sea;
}

bool member_place(int id, Place& out) {
    if (!g_client || id < 0 || id >= kMaxPlayers || id == g_slot_of_me || !member(id)) return false;
    const auto it = g_places.find(g_slot[id].sid);
    if (it == g_places.end() || !it->second.at) return false;
    memcpy(out.stage, it->second.where.stage.data(), 8);
    out.room = it->second.where.room;
    out.age_ms = now_ms() - it->second.at, out.settled_ms = now_ms() - it->second.since;
    out.at_sea = it->second.square != 255, out.x = (float)it->second.x, out.z = (float)it->second.z;
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
