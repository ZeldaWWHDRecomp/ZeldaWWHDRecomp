// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op over UDP (coop_net.h).
//
// A link is one other game this one talks to: the guest has one (the host), the host one a guest. Every packet starts
// with a Head: the protocol, the newest reliable message taken in order from that link (ack), and this game's clock
// with an echo of the other's, for the ping. Kind 0 carries the reliable messages not yet acked (each resent at every
// tick until it is); other kinds are a player's newest state, which the host passes on to the other guests as it comes.
// A game the host does not know becomes a link only by a valid hello as its first message.
#include "coop_net.h"

#ifndef WWHD_ONLINE
#define WWHD_ONLINE 0
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>

#include "mods.h"
#include "../savestate.h"

#ifdef _WIN32
using socket_t = SOCKET;
static constexpr socket_t kNoSocket = INVALID_SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
#else
using socket_t = int;
static constexpr socket_t kNoSocket = -1;
static void close_socket(socket_t s) { close(s); }
#endif

namespace mods::coop::net {
#if WWHD_ONLINE
namespace srv {  // coop_net_server.cpp
bool active();
bool start(const std::string& server, int port, const std::string& my_name, int my_tunic, std::string& why);
void stop(const char* why);
Role role();
int my_id();
const char* status();
int ping_ms();
const std::string& name(int id);
int tunic(int id);
bool connected(int id);
bool member(int id);
std::string friend_code();
void send(int to, Msg type, const std::string& body);
void put_state(uint8_t kind, const std::string& body);
void put_pose_place(const float at[3]);
void poll(Handler& h);
bool invite(const std::string& code, std::string& why);
std::vector<std::pair<uint32_t, std::string>> invites();
void answer(uint32_t party, bool yes);
void leave_party();
std::vector<servers::Server> saved_servers();
void forget_server(const std::string& addr, int port);
bool runners(std::vector<Runner>& out);
std::vector<Marker> markers();
uint64_t player_key(int id);
void tell_block(uint64_t key, bool on);
void tell_mute(uint64_t key, bool on);
std::vector<ModIssue> mod_issues();
std::string refused_mods();
void poll_query();
void query_rules(const std::string& addr, int port);
std::string server_rules(const std::string& addr, int port);
bool party_flags(uint16_t& flags, bool& leading);
bool set_party_flags(uint16_t flags, std::string& why);
int party_max();
int party_size();
PvpRules pvp_rules();
uint8_t pvp_state(uint32_t* left_ms);
uint8_t pvp_mark(int id);
void pvp_flag(bool on, bool busy);
void pvp_hit(int victim, uint8_t kind, uint8_t quarters, const float at[3]);
void pvp_shot(uint8_t kind, const float at[3], const float v[3]);
void pvp_ko(int by, uint16_t wallet);
std::vector<PvpEvent> pvp_take();
}  // namespace srv
#define VIA_SERVER(call) if (srv::active()) return srv::call
#else
#define VIA_SERVER(call)
#endif
namespace {

constexpr uint32_t kMagic = 0x4F435757;  // "WWCO"
constexpr uint8_t kControl = 0, kRefuse = 0xFE;
std::string g_hello_mods(1, '\0');  // (no mods: a count of 0)
HelloCheck g_hello_check = nullptr;
std::string g_refused_mods;
constexpr uint32_t kTick = 33, kKeepAlive = 1000;  // (a tick each game step: 30 Hz)
constexpr size_t kMtu = 1200;

#pragma pack(push, 1)
struct Head {
    uint32_t magic;
    uint16_t proto;
    uint8_t kind, origin;    // kind 0: reliable messages; else a state of player `origin`, taken at its time `ot`
    uint32_t ack, t, echo;   // newest message taken in order; this game's clock; the other's last clock seen
    uint16_t held;           // ms since that clock arrived
    uint32_t ot;
};
#pragma pack(pop)

struct Rel { uint32_t id; Msg type; uint8_t origin; std::string body; };
struct Link {
    bool on = false;
    sockaddr_in addr{};
    std::deque<Rel> out;
    uint32_t next = 1, got = 0, sent_got = ~0u, last_ctrl = 0, heard = 0, their_t = 0, their_t_at = 0;
    int ping = -1;
};
struct Player { bool on = false; std::string name; int tunic = 0; sockaddr_in addr{}; };  // addr: where the host reaches it

socket_t g_sock = kNoSocket;
Role g_role = Role::kOff;
int g_me = 0;
Link g_link[kMaxDirect];     // host: [i] guest i; guest: [0] the host
Player g_player[kMaxDirect];
std::map<uint8_t, std::pair<uint32_t, std::string>> g_states;  // this game's newest, by kind (time, bytes)
uint32_t g_next_tick = 0;
std::string g_status = "Off";
std::string g_my_name;
int g_my_tunic = 0;
std::string g_left_why;  // guest: why the host is gone (reported at the next poll)

void raw(const sockaddr_in& to, const std::string& p) { sendto(g_sock, p.data(), (int)p.size(), 0, (const sockaddr*)&to, sizeof to); }

std::string head(Link& l, uint8_t kind, uint8_t origin, uint32_t ot) {
    const uint32_t now = now_ms();
    Head h{kMagic, (uint16_t)kProto, kind, origin, l.got, now, l.their_t, (uint16_t)std::min<uint32_t>(now - l.their_t_at, 65535), ot};
    return std::string((const char*)&h, sizeof h);
}

void control(Link& l) {
    std::string p = head(l, kControl, (uint8_t)g_me, 0);
    if (!l.out.empty()) {
        p.append((const char*)&l.out.front().id, 4);
        for (const Rel& r : l.out) {
            if (p.size() + 4 + r.body.size() > kMtu) break;
            const uint16_t n = (uint16_t)r.body.size();
            p += (char)r.type, p += (char)r.origin, p.append((const char*)&n, 2), p += r.body;
        }
    }
    raw(l.addr, p);
    l.sent_got = l.got, l.last_ctrl = now_ms();
}

// a reliable message for a link: at most kMostBody bytes (a packet must carry it whole) and kMostQueued waiting (a
// link that far behind is dropped by the timeout rather than holding memory without bound)
constexpr size_t kMostBody = kMtu - sizeof(Head) - 8, kMostQueued = 1024;
void queue(Link& l, Msg type, uint8_t origin, const std::string& body) {
    if (body.size() > kMostBody || l.out.size() >= kMostQueued) {
        mods::trace("coop: message %d of %zu bytes not sent (%zu waiting)", (int)type, body.size(), l.out.size());
        return;
    }
    l.out.push_back({l.next++, type, origin, body});
}

// a player for the others: {u8 id, u8 tunic, u32 ip, u16 port (network order; 0: the host itself), name}. The
// addresses let the players take over when the host goes (migrate())
std::string player_line(int id) {
    std::string b(1, (char)id);
    b += (char)g_player[id].tunic;
    const sockaddr_in& a = id ? g_link[id].addr : sockaddr_in{};
    b.append((const char*)&a.sin_addr.s_addr, 4), b.append((const char*)&a.sin_port, 2);
    b += g_player[id].name;
    return b;
}
bool g_refused = false;  // guest: the host refused this game (no taking over)

void drop(int id, const char* why, Handler& h) {
    if (!g_link[id].on && !g_player[id].on) return;
    g_link[id] = Link{}, g_player[id].on = false;
    mods::trace("coop: player %d (%s) left: %s", id, g_player[id].name.c_str(), why);
    if (g_role == Role::kHost)
        for (Link& l : g_link) if (l.on) queue(l, kLeft, 0, std::string(1, (char)id));
    h.left(id, why);
}

// a hello from a game the host does not know yet: a new link, or a refusal sent straight back
void hello(const sockaddr_in& from, const uint8_t* d, size_t n, Handler& h) {
    // control body: u32 first id, then {type, origin, u16 len, body}; the hello must be message 1
    uint32_t first;
    std::string no;
    if (n < 8 || (memcpy(&first, d, 4), first != 1) || d[4] != kHello) return;
    uint16_t len;
    memcpy(&len, d + 6, 2);
    const uint8_t* b = d + 8;
    if (8u + len > n || len < 2) return;
    uint16_t proto;
    uint64_t game = 0;
    memcpy(&proto, b, 2);  // (first, as every protocol's hello has it: another protocol is refused by it)
    if (proto == kProto && (len < 12 || 12u + b[11] > len)) return;
    if (proto == kProto) memcpy(&game, b + 2, 8);
    int id = 0;
    for (int i = 1; i < kMaxDirect && !id; i++) if (!g_link[i].on) id = i;
    char t[160];
    if (proto != kProto) snprintf(t, sizeof t, "Different version of online co-op: host %d, you %d. Both need a version with the same co-op.", kProto, proto), no = t;
    else if (game != game_hash())
        snprintf(t, sizeof t, "Different game: host %016llX, you %016llX. Both need the same game (region, version, randomizer seed).",
                 (unsigned long long)game_hash(), (unsigned long long)game), no = t;
    else if (!id) no = "The host's game is full (4 players).";
    bool for_mods = false;
    if (no.empty() && g_hello_check) {  // (the party's gameplay mods: the host's)
        no = g_hello_check(b + 12 + b[11], len - 12 - b[11]);
        for_mods = !no.empty();
    }
    if (!no.empty()) {
        Link tmp;
        raw(from, head(tmp, kRefuse, 0, 0) + no + (for_mods ? std::string(1, '\0') + g_hello_mods : std::string()));
        mods::trace("coop: refused %s:%d: %s", inet_ntoa(from.sin_addr), ntohs(from.sin_port), no.c_str());
        return;
    }
    Link& l = g_link[id];
    l = Link{}, l.on = true, l.addr = from, l.heard = now_ms();
    // the tunic asked for, or the next one no one wears (green, red, blue, purple)
    int tunic = b[10] % 4;
    for (int k = 0; k < 4; k++, tunic = (b[10] + k) % 4) {
        bool worn = false;
        for (int i = 0; i < kMaxDirect; i++) worn |= i != id && g_player[i].on && g_player[i].tunic == tunic;
        if (!worn) break;
    }
    g_player[id] = {true, std::string((const char*)b + 12, std::min<size_t>(b[11], 31)), tunic};
    (void)h;  // (the hello itself is taken below, as message 1 of the new link)
}

bool from_guest(Msg t) {
    switch (t) {
    case kHello: case kFired: case kHit: case kEffect: case kArrived: case kBoatShot: case kDamage: case kProgress:
    case kDown: case kSound: case kPlayerHit: case kBombed: case kMonHit: case kMonGone: case kDoorAsk: case kDoorMove:
    case kRupees: case kEmote: return true;
    default: return false;
    }
}

void take(int li, const uint8_t* d, size_t n, Handler& h) {
    Link& l = g_link[li];
    uint32_t id;
    if (n < 4) return;
    memcpy(&id, d, 4);
    for (size_t o = 4; o + 4 <= n; id++) {
        const Msg type = (Msg)d[o];
        const uint8_t origin = d[o + 1];
        uint16_t len;
        memcpy(&len, d + o + 2, 2);
        const uint8_t* b = d + o + 4;
        o += 4 + len;
        if (o > n) return;
        if (id != l.got + 1) continue;  // taken already, or one before it is missing (it comes again)
        l.got = id;
        // (the host takes from a guest only what a guest sends: never the host's own messages, a guest's word that a
        // player left, joined or owns a place)
        if (g_role == Role::kHost && !from_guest(type)) { mods::trace("coop: player %d sent message %d, a host's: dropped", li, (int)type); continue; }
        if (type == kHello) {  // host: the new guest in, everyone told
            g_link[li].heard = now_ms();
            queue(l, kWelcome, 0, std::string{(char)li, (char)g_player[li].tunic});
            for (int i = 0; i < kMaxDirect; i++) if (g_player[i].on && i != li) queue(l, kJoined, 0, player_line(i));
            for (int i = 1; i < kMaxDirect; i++) if (i != li && g_link[i].on) queue(g_link[i], kJoined, 0, player_line(li));
            mods::trace("coop: %s joined as player %d from %s:%d", g_player[li].name.c_str(), li, inet_ntoa(l.addr.sin_addr), ntohs(l.addr.sin_port));
            h.joined(li);
        } else if (type == kWelcome && len >= 2) {
            g_me = b[0], g_player[g_me] = {true, g_my_name, b[1] % 4}, g_player[0].on = true;
            mods::trace("coop: welcomed as player %d", g_me);
        } else if (type == kJoined && len >= 8) {
            Player& p = g_player[b[0] % kMaxDirect];
            p = {true, std::string((const char*)b + 8, std::min<size_t>(len - 8, 31)), b[1] % 4};
            p.addr.sin_family = AF_INET, memcpy(&p.addr.sin_addr.s_addr, b + 2, 4), memcpy(&p.addr.sin_port, b + 6, 2);
            h.joined(b[0] % kMaxDirect);
        } else if (type == kLeft && len >= 1) {
            drop(b[0] % kMaxDirect, "left", h);
        } else {
            if (g_role == Role::kHost && (type == kFired || type == kEffect || type == kBoatShot || type == kProgress || type == kSound || type == kPlayerHit || type == kBombed || type == kMonHit || type == kMonGone || type == kDoorMove || type == kRupees || type == kEmote))  // passed on to the other guests
                for (int i = 1; i < kMaxDirect; i++) if (i != li && g_link[i].on) queue(g_link[i], type, origin, std::string((const char*)b, len));
            h.message(origin, type, b, len);
        }
    }
}

std::string hello_body() {
    std::string b(10, '\0');
    // (test aid: WWHD_COOP_PROTO, another protocol in the hello, as an older or newer build would send)
    const uint16_t proto = getenv("WWHD_COOP_PROTO") ? (uint16_t)atoi(getenv("WWHD_COOP_PROTO")) : (uint16_t)kProto;
    const uint64_t game = game_hash();
    memcpy(&b[0], &proto, 2), memcpy(&b[2], &game, 8);
    const std::string name = g_my_name.substr(0, 31);
    b += (char)g_my_tunic, b += (char)name.size(), b += name, b += g_hello_mods;
    return b;
}

int link_of(const sockaddr_in& a) {
    for (int i = 0; i < kMaxDirect; i++)
        if (g_link[i].on && g_link[i].addr.sin_addr.s_addr == a.sin_addr.s_addr && g_link[i].addr.sin_port == a.sin_port) return i;
    return -1;
}

}  // namespace

uint32_t now_ms() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t game_hash() {
    if (const char* e = getenv("WWHD_COOP_GAME")) return strtoull(e, nullptr, 16);  // (test aid: another game)
    return ss::game_data_id();
}

Role role() { VIA_SERVER(role()); return g_role; }
int my_id() { VIA_SERVER(my_id()); return g_me; }
const char* status() { VIA_SERVER(status()); return g_status.c_str(); }
// (numbers go up to kMaxPlayers for every transport; the direct one has kMaxDirect)
static bool direct_id(int id) { return id >= 0 && id < kMaxDirect; }
int ping_ms(int id) { VIA_SERVER(ping_ms()); return direct_id(id) ? g_link[g_role == Role::kGuest ? 0 : id].ping : -1; }
const std::string& name(int id) { VIA_SERVER(name(id)); static const std::string none; return direct_id(id) ? g_player[id].name : none; }
int tunic(int id) { VIA_SERVER(tunic(id)); return direct_id(id) ? g_player[id].tunic : 0; }
bool connected(int id) { VIA_SERVER(connected(id)); return direct_id(id) && g_player[id].on; }
bool member(int id) { VIA_SERVER(member(id)); return id >= 0 && id < kMaxDirect && g_player[id].on; }

void set_hello_mods(const std::string& encoded) { g_hello_mods = encoded.empty() ? std::string(1, '\0') : encoded; }
void set_hello_check(HelloCheck check) { g_hello_check = check; }
std::string refused_mods() {
#if WWHD_ONLINE
    if (const std::string s = srv::refused_mods(); !s.empty()) return s;  // (a party through a server)
#endif
    return g_refused_mods;
}

bool start(Role r, const std::string& addr, int port, const std::string& my_name, int my_tunic, std::string& why) {
    g_refused_mods.clear();
    stop(nullptr);
    if (r == Role::kOff) return true;
#ifdef _WIN32
    static const bool wsa = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    if (!wsa) { why = "no sockets"; return false; }
#endif
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == kNoSocket) { why = "no socket"; return false; }
#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(g_sock, FIONBIO, &nonblocking);
    // (SIO_UDP_CONNRESET off: else a packet to a guest that has gone makes the next recvfrom fail, and that stops poll's loop)
    BOOL report = FALSE;
    DWORD unused = 0;
    WSAIoctl(g_sock, 0x9800000C, &report, sizeof report, nullptr, 0, &unused, nullptr, nullptr);
#else
    fcntl(g_sock, F_SETFL, O_NONBLOCK);
#endif
    sockaddr_in me{};
    me.sin_family = AF_INET, me.sin_addr.s_addr = htonl(INADDR_ANY), me.sin_port = htons(r == Role::kHost ? port : 0);
    if (bind(g_sock, (sockaddr*)&me, sizeof me) < 0) {
        why = "port " + std::to_string(port) + " is in use";
        close_socket(g_sock), g_sock = kNoSocket;
        return false;
    }
    g_role = r, g_me = 0, g_my_name = my_name, g_my_tunic = my_tunic % 4, g_next_tick = now_ms(), g_left_why.clear(), g_refused = false;
    for (Player& p : g_player) p = Player{};
    if (r == Role::kHost) {
        g_player[0] = {true, my_name, g_my_tunic};
        g_status = "Hosting on port " + std::to_string(port);
    } else {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET, hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(addr.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
            why = "cannot find " + addr;
            stop(nullptr);
            return false;
        }
        Link& l = g_link[0];
        l.on = true, memcpy(&l.addr, res->ai_addr, sizeof l.addr), l.heard = now_ms();
        freeaddrinfo(res);
        queue(l, kHello, 0, hello_body());
        g_status = "Joining " + addr + ":" + std::to_string(port);
    }
    mods::trace("coop: %s (protocol %d, game %016llX): %s", r == Role::kHost ? "host" : "guest", kProto, (unsigned long long)game_hash(), g_status.c_str());
    return true;
}

void stop(const char* why) {
#if WWHD_ONLINE
    if (srv::active()) srv::stop(why);
#endif
    if (g_sock != kNoSocket) {
        for (Link& l : g_link)
            if (l.on) for (int k = 0; k < 3; k++) raw(l.addr, head(l, kControl, (uint8_t)g_me, 0) + std::string("\0\0\0\0", 4) + (char)kBye);
        close_socket(g_sock);
        mods::trace("coop: stopped%s%s", why ? ": " : "", why ? why : "");
    }
    g_sock = kNoSocket, g_role = Role::kOff, g_status = why ? why : "Off";
    for (Link& l : g_link) l = Link{};
    for (Player& p : g_player) p = Player{};
    g_states.clear();
}

void send(int to, Msg type, const std::string& body) {
    VIA_SERVER(send(to, type, body));
    for (int i = 0; i < kMaxDirect; i++)
        if (g_link[i].on && (to < 0 || i == (g_role == Role::kGuest ? 0 : to))) queue(g_link[i], type, (uint8_t)g_me, body);
}

void put_state(uint8_t kind, const std::string& body) { VIA_SERVER(put_state(kind, body)); g_states[kind] = {now_ms(), body}; }
// (through a server: the world place the next pose stands at, for the server's interest; the direct transport has none)
void put_pose_place(const float at[3]) {
#if WWHD_ONLINE
    if (srv::active()) srv::put_pose_place(at);
#else
    (void)at;
#endif
}
void add_state(uint8_t kind, const std::string& more) {
    VIA_SERVER(put_state(kind, more));
    auto& s = g_states[kind];
    if (s.second.empty()) s.first = now_ms();
    if (s.second.size() + more.size() <= 1000) s.second += more;  // (a late tick: the newest dropped, under the packet size)
}

// the host gone with other guests still there (host migration over the direct transport): the guest with the lowest
// number becomes the host on its own socket (its port is the one the old host knew); the others join it at the
// address the old host told them. Each keeps playing where it is; the new host sends the team state as at any join
bool migrate(Handler& h) {
    int next = 0;
    for (int i = 1; i < kMaxDirect && !next; i++) if (g_player[i].on) next = i;
    if (!next) return false;
    const std::string why = g_left_why;
    const std::string old_name = g_player[0].name;
    g_left_why.clear();
    h.left(0, ("host " + old_name + " left: " + why).c_str());  // (the old host's puppet gone; the session stays on)
    if (next == g_me) {
        mods::trace("coop: the host (%s) is gone: this game hosts now", why.c_str());
        g_role = Role::kHost, g_me = 0;
        for (Link& l : g_link) l = Link{};
        for (Player& p : g_player) p = Player{};
        g_player[0] = {true, g_my_name, g_my_tunic};
        g_status = "Hosting (took over from " + old_name + ")";
        return true;
    }
    const sockaddr_in to = g_player[next].addr;
    mods::trace("coop: the host (%s) is gone: joining player %d (%s) at %s:%d", why.c_str(), next, g_player[next].name.c_str(),
                inet_ntoa(to.sin_addr), ntohs(to.sin_port));
    g_me = 0;
    for (Link& l : g_link) l = Link{};
    for (Player& p : g_player) p = Player{};
    Link& l = g_link[0];
    l.on = true, l.addr = to, l.heard = now_ms() + 3000;  // (the new host may notice the old one gone up to a few s later)
    queue(l, kHello, 0, hello_body());
    g_status = "Joining the new host";
    return true;
}

void poll(Handler& h) {
#if WWHD_ONLINE
    srv::poll_query();  // (the server list's question about a server's mod rules, online or not)
#endif
    VIA_SERVER(poll(h));
    if (g_sock == kNoSocket) return;
    const uint32_t now = now_ms();
    uint8_t buf[8192];
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    for (int n; (n = (int)recvfrom(g_sock, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl)) >= (int)sizeof(Head); fl = sizeof from) {
        Head hd;
        memcpy(&hd, buf, sizeof hd);
        const uint8_t* d = buf + sizeof hd;
        const size_t dn = n - sizeof hd;
        if (hd.magic != kMagic) continue;
        if (hd.kind == kRefuse) {
            const size_t text = strnlen((const char*)d, dn);  // (a refusal for the mods: the host's set after a 0)
            g_left_why = std::string((const char*)d, text), g_refused = true;
            g_refused_mods = text < dn ? std::string((const char*)d + text + 1, dn - text - 1) : std::string();
            mods::trace("coop: the host refused: %s", g_left_why.c_str());
            continue;
        }
        int li = link_of(from);
        if (li < 0 && g_role == Role::kHost && hd.kind == kControl) hello(from, d, dn, h), li = link_of(from);
        if (li < 0 || hd.proto != kProto) continue;
        Link& l = g_link[li];
        l.heard = now, l.their_t = hd.t, l.their_t_at = now;
        if (hd.echo) l.ping = (int)(now - hd.echo - hd.held);
        while (!l.out.empty() && l.out.front().id <= hd.ack) l.out.pop_front();
        if (hd.kind == kControl) {
            if (dn >= 5 && d[4] == kBye && !memcmp(d, "\0\0\0\0", 4)) {  // leaving
                if (g_role == Role::kGuest) g_left_why = "Host left";
                else drop(li, "left", h);
                continue;
            }
            take(li, d, dn, h);
        } else {
            const int origin = g_role == Role::kHost ? li : hd.origin % kMaxDirect;
            if (g_role == Role::kHost)  // passed on as it comes
                for (int i = 1; i < kMaxDirect; i++)
                    if (i != li && g_link[i].on) raw(g_link[i].addr, head(g_link[i], hd.kind, (uint8_t)li, hd.ot) + std::string((const char*)d, dn));
            h.state(origin, hd.kind, hd.ot, d, dn);
        }
    }
    // gone: 5 s of nothing (test aid: WWHD_COOP_TIMEOUT, in ms), or (guest) the host said so
    static const uint32_t kTimeout = getenv("WWHD_COOP_TIMEOUT") ? (uint32_t)strtoul(getenv("WWHD_COOP_TIMEOUT"), nullptr, 10) : 5000;
    for (int i = 0; i < kMaxDirect; i++)
        if (g_link[i].on && (int32_t)(now - g_link[i].heard) > (int32_t)kTimeout) {
            if (g_role == Role::kGuest) g_left_why = g_me ? "Connection lost" : "No answer from the host";
            else drop(i, "Connection lost", h);
        }
    if (g_role == Role::kGuest && !g_left_why.empty() && !g_refused && g_me && migrate(h)) return;
    if (g_role == Role::kGuest && !g_left_why.empty()) {
        const std::string why = g_left_why;
        stop(why.c_str());
        h.left(0, why.c_str());
        return;
    }
    if ((int32_t)(now - g_next_tick) < 0) return;
    g_next_tick = std::max(g_next_tick + kTick, now - kTick);
    for (Link& l : g_link) {
        if (!l.on) continue;
        if (g_role == Role::kHost || g_me)
            for (auto& [kind, s] : g_states) raw(l.addr, head(l, kind, (uint8_t)g_me, s.first) + s.second);
        if (!l.out.empty() || l.sent_got != l.got || now - l.last_ctrl >= kKeepAlive) control(l);
    }
    g_states.clear();
    int players = 0, worst = -1;
    for (int i = 0; i < kMaxDirect; i++) players += g_player[i].on, worst = std::max(worst, g_link[i].on ? g_link[i].ping : -1);
    if (g_role == Role::kHost || g_me)
        g_status = std::string(g_role == Role::kHost ? "Hosting" : "Joined") + ", " + std::to_string(players) + " player" +
                   (players == 1 ? "" : "s") + (worst >= 0 ? ", ping " + std::to_string(worst) + " ms" : "");
}

}  // namespace mods::coop::net

namespace mods::coop::net {
bool server_built() { return WWHD_ONLINE != 0; }
bool via_server() {
#if WWHD_ONLINE
    return srv::active();
#else
    return false;
#endif
}
bool start_server(const std::string& addr, int port, const std::string& my_name, int my_tunic, std::string& why) {
    stop(nullptr);
#if WWHD_ONLINE
    return srv::start(addr, port, my_name, my_tunic, why);
#else
    (void)addr, (void)port, (void)my_name, (void)my_tunic;
    why = "this build has no server connection";
    return false;
#endif
}
#if WWHD_ONLINE
std::string friend_code() { return srv::friend_code(); }
bool invite(const std::string& code, std::string& why) { return srv::invite(code, why); }
std::vector<std::pair<uint32_t, std::string>> invites() { return srv::invites(); }
void answer(uint32_t party, bool yes) { srv::answer(party, yes); }
void leave_party() { srv::leave_party(); }
std::vector<servers::Server> saved_servers() { return srv::saved_servers(); }
void forget_server(const std::string& addr, int port) { srv::forget_server(addr, port); }
bool server_runners(std::vector<Runner>& out) { return srv::active() && srv::runners(out); }
std::vector<Marker> server_markers() { return srv::active() ? srv::markers() : std::vector<Marker>{}; }
uint64_t player_key(int id) { return srv::active() ? srv::player_key(id) : 0; }
void tell_block(uint64_t key, bool on) { if (srv::active()) srv::tell_block(key, on); }
void tell_mute(uint64_t key, bool on) { if (srv::active()) srv::tell_mute(key, on); }
std::vector<ModIssue> mod_issues() { return srv::mod_issues(); }
void query_rules(const std::string& addr, int port) { srv::query_rules(addr, port); }
std::string server_rules(const std::string& addr, int port) { return srv::server_rules(addr, port); }
bool party_flags(uint16_t& flags, bool& leading) { return srv::active() && srv::party_flags(flags, leading); }
bool set_party_flags(uint16_t flags, std::string& why) {
    if (!srv::active()) return why = "not through a server", false;
    return srv::set_party_flags(flags, why);
}
PvpRules pvp_rules() { return srv::active() ? srv::pvp_rules() : PvpRules{}; }
uint8_t pvp_state(uint32_t* left_ms) { if (left_ms) *left_ms = 0; return srv::active() ? srv::pvp_state(left_ms) : 0; }
uint8_t pvp_mark(int id) { return srv::active() ? srv::pvp_mark(id) : 0; }
void pvp_flag(bool on, bool busy) { if (srv::active()) srv::pvp_flag(on, busy); }
void pvp_hit(int victim, uint8_t kind, uint8_t quarters, const float at[3]) { if (srv::active()) srv::pvp_hit(victim, kind, quarters, at); }
void pvp_shot(uint8_t kind, const float at[3], const float v[3]) { if (srv::active()) srv::pvp_shot(kind, at, v); }
void pvp_ko(int by, uint16_t wallet) { if (srv::active()) srv::pvp_ko(by, wallet); }
std::vector<PvpEvent> pvp_take() { return srv::active() ? srv::pvp_take() : std::vector<PvpEvent>{}; }
int party_max() { VIA_SERVER(party_max()); return kMaxDirect; }
int party_size() {
    VIA_SERVER(party_size());
    int n = 0;
    for (int i = 0; i < kMaxDirect; i++) n += g_player[i].on;
    return std::max(n, 1);
}
#else
PvpRules pvp_rules() { return {}; }
uint8_t pvp_state(uint32_t* left_ms) { if (left_ms) *left_ms = 0; return 0; }
uint8_t pvp_mark(int) { return 0; }
void pvp_flag(bool, bool) {}
void pvp_hit(int, uint8_t, uint8_t, const float*) {}
void pvp_shot(uint8_t, const float*, const float*) {}
void pvp_ko(int, uint16_t) {}
std::vector<PvpEvent> pvp_take() { return {}; }
int party_max() { return kMaxDirect; }
int party_size() {
    int n = 0;
    for (int i = 0; i < kMaxDirect; i++) n += g_player[i].on;
    return std::max(n, 1);
}
std::string friend_code() { return ""; }
bool invite(const std::string&, std::string& why) { why = "this build has no server connection"; return false; }
std::vector<std::pair<uint32_t, std::string>> invites() { return {}; }
void answer(uint32_t, bool) {}
void leave_party() {}
std::vector<servers::Server> saved_servers() { return {}; }
void forget_server(const std::string&, int) {}
bool server_runners(std::vector<Runner>& out) { out.clear(); return false; }
std::vector<Marker> server_markers() { return {}; }
uint64_t player_key(int) { return 0; }
void tell_block(uint64_t, bool) {}
void tell_mute(uint64_t, bool) {}
std::vector<ModIssue> mod_issues() { return {}; }
void query_rules(const std::string&, int) {}
std::string server_rules(const std::string&, int) { return ""; }
bool party_flags(uint16_t&, bool&) { return false; }
bool set_party_flags(uint16_t, std::string& why) { why = "this build has no server connection"; return false; }
#endif
bool party_flag(PartyFlag f, bool otherwise) {
    uint16_t flags = 0;
    bool leading = false;
    return party_flags(flags, leading) ? (flags & f) != 0 : otherwise;
}
}  // namespace mods::coop::net
