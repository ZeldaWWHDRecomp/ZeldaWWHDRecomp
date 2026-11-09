// SPDX-License-Identifier: MPL-2.0
// End-to-end tests of wwhd-online-server on 127.0.0.1: real sockets, the server and several protocol clients in one
// process, driven step by step.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
#include <random>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <thread>

#include "proto/client.h"
#include "server.h"

using namespace online;
using namespace online::proto;
using online::server::Session;

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

static std::string g_dir;

// the clock the bots hand to Client::poll: monotonic_ms() plus an offset, like a game's own clock (the client must
// never mix it with its own monotonic_ms(): that made redirects and the first connect time out at once)
static uint32_t g_clock_offset = 0x40000000u;
static uint32_t bot_now() { return monotonic_ms() + g_clock_offset; }

struct Bot {
    Client c;
    std::string name;
    crypto::Identity id;
    std::map<uint16_t, PlayerIn> players;
    std::map<uint16_t, PoseEntry> poses;
    std::map<uint16_t, uint32_t> near_rx, near_gap;  // near-tier poses of each player: last arrival, longest gap
    std::vector<ClientEvent> log;
    PoseHeader head;
    Bytes blob = Bytes(100, 7);
    bool online() const { return c.state() == ClientState::kOnline; }
    void poll(uint32_t) {
        uint32_t now = bot_now();
        for (auto& e : c.poll(now)) {
            if (e.kind == ClientEvent::kWelcome && !e.resumed) {
                players.clear();
                poses.clear();
            }
            if (e.kind == ClientEvent::kPlayerIn) players[e.id] = e.player;
            if (e.kind == ClientEvent::kPlayerOut) {
                if (std::getenv("SCALING_DEBUG")) std::printf("  %s: player %u out\n", name.c_str(), e.id);
                players.erase(e.id);
                poses.erase(e.id);
            }
            if (e.kind == ClientEvent::kPoses)
                for (auto& p : e.poses) {
                    poses[p.id] = p;
                    if (p.tier != kTierNear) continue;
                    auto it = near_rx.find(p.id);
                    if (it != near_rx.end()) {
                        if (std::getenv("SCALING_DEBUG") && now - it->second > 100)
                            std::printf("  %s: gap %u ms before a pose of %u at x %.0f\n", name.c_str(), now - it->second, p.id, p.head.x);
                        near_gap[p.id] = std::max(near_gap[p.id], now - it->second);
                    }
                    near_rx[p.id] = now;
                }
            if (std::getenv("SCALING_DEBUG") && (e.kind == ClientEvent::kWelcome || e.kind == ClientEvent::kStateChanged))
                std::printf("  %s: %u event %d resumed %d state %d %s\n", name.c_str(), monotonic_ms(), int(e.kind), e.resumed,
                            int(e.state), e.text.c_str());
            log.push_back(e);
        }
        if (online()) c.send_pose(head, blob.data(), blob.size());
    }
    bool saw(ClientEvent::Kind k) const {
        for (auto& e : log)
            if (e.kind == k) return true;
        return false;
    }
    const ClientEvent* last(ClientEvent::Kind k) const {
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            if (it->kind == k) return &*it;
        return nullptr;
    }
};

static server::Config base_config(uint16_t port) {
    server::Config c;
    c.port = port;
    c.pow_bits = 6;
    c.per_ip = 64;
    c.key_file = g_dir + "/server.key";
    c.ban_file = g_dir + "/bans.txt";
    c.report_file = g_dir + "/reports.log";
    c.admin_socket = "";
    c.game_data = {0x1111};
    c.copy_size = 2;
    c.team_state_file = "";  // tests that want it set it
    return c;
}

static TransportMode g_mode = TransportMode::kAuto;
static std::unique_ptr<Bot> make_bot(const std::string& name, uint16_t port, uint64_t game = 0x1111,
                                     const crypto::Identity* id = nullptr) {
    auto b = std::make_unique<Bot>();
    b->name = name;
    ClientConfig cfg;
    cfg.port = port;
    cfg.game_data = game;
    cfg.identity = id ? *id : crypto::Identity::generate();
    cfg.transport_mode = g_mode;
    b->id = cfg.identity;
    cfg.name = name;
    std::string why;
    CHECK(b->c.start(cfg, why));
    b->head.where = Where::of("sea", 1);
    return b;
}

// runs the server (if any) and the bots for `ms`, or until `done` holds
static bool run(server::Server* s, std::vector<Bot*> bots, uint32_t ms, std::function<bool()> done = nullptr) {
    uint32_t end = monotonic_ms() + ms;
    while (int32_t(monotonic_ms() - end) < 0) {
        if (s) s->step(1);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        uint32_t now = monotonic_ms();
        for (Bot* b : bots) b->poll(now);
        if (done && done()) return true;
    }
    return done ? done() : true;
}

static void test_presence_and_stage() {
    server::Server s(base_config(0));
    std::string why;
    CHECK(s.start(why));
    uint16_t port = s.port();
    auto a = make_bot("alpha", port), b = make_bot("beta", port);
    CHECK(run(&s, {a.get(), b.get()}, 5000, [&] { return a->online() && b->online(); }));
    a->head.x = 1000;
    b->head.x = 1500;
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] {
        return a->players.count(b->c.my_id()) && a->poses.count(b->c.my_id()) && b->poses.count(a->c.my_id());
    }));
    if (a->poses.count(b->c.my_id())) {
        auto& p = a->poses[b->c.my_id()];
        CHECK(p.tier == kTierNear && p.blob.size() == 100 && std::fabs(p.head.x - 1500) < 0.01f);
    }
    // poses carry the sender's time, rising from one pose to the next (receivers drop poses that are not newer)
    {
        uint32_t first = a->poses[b->c.my_id()].ms;
        CHECK(first != 0);
        CHECK(run(&s, {a.get(), b.get()}, 2000, [&] { return int32_t(a->poses[b->c.my_id()].ms - first) > 100; }));
    }
    // a stranger is named generically
    if (a->players.count(b->c.my_id())) CHECK(a->players[b->c.my_id()].name.rfind("Sailor ", 0) == 0);
    // mid tier further out, then gone outside the window
    b->head.x = 1000 + 9000;
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] {
        return a->poses.count(b->c.my_id()) && a->poses[b->c.my_id()].tier == kTierMid;
    }));
    b->head.x = 1000 + 30000;
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return !a->players.count(b->c.my_id()); }));
    // stage change: into an interior, out of sight
    b->head.x = 1200;
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return a->players.count(b->c.my_id()) != 0; }));
    b->c.send_stage(Where::of("room1", 0));
    b->head.where = Where::of("room1", 0);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return !a->players.count(b->c.my_id()); }));
    a->head.where = Where::of("room1", 0);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return a->players.count(b->c.my_id()) != 0; }));
    // another room of the same stage: not seen
    b->head.where = Where::of("room1", 2);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return !a->players.count(b->c.my_id()); }));
    // block: neither side sees the other
    b->head.where = Where::of("room1", 0);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return a->players.count(b->c.my_id()) != 0; }));
    a->c.block(b->c.key_hash(), true);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] {
        return !a->players.count(b->c.my_id()) && !b->players.count(a->c.my_id());
    }));
    std::printf("presence/stage/block: ok (ping %d ms)\n", a->c.ping_ms());
}

static void test_refusals_and_ban() {
    {
        // a public server needs the real crypto backend
        auto pc = base_config(0);
        pc.public_server = true;
        server::Server p(pc);
        std::string w;
        CHECK(p.start(w) == crypto::backend_secure());
    }
    server::Server s(base_config(0));
    std::string why;
    CHECK(s.start(why));
    auto bad = make_bot("other", s.port(), 0x2222);
    CHECK(run(&s, {bad.get()}, 3000, [&] { return bad->saw(ClientEvent::kRefused); }));
    if (auto* e = bad->last(ClientEvent::kRefused)) CHECK(e->refusal == Refusal::kGameData);
    CHECK(bad->c.state() == ClientState::kStopped);

    auto a = make_bot("alpha", s.port());
    CHECK(run(&s, {a.get()}, 3000, [&] { return a->online(); }));
    std::string code = a->c.friend_code();
    std::string out = s.admin("ban " + code + " 1h griefing");
    CHECK(out.find("banned") == 0);
    CHECK(run(&s, {a.get()}, 3000, [&] { return a->saw(ClientEvent::kNotice); }));
    CHECK(a->c.state() == ClientState::kStopped);
    // the same identity again: refused
    auto a2 = make_bot("alpha", s.port(), 0x1111, &a->id);
    CHECK(run(&s, {a2.get()}, 3000, [&] { return a2->saw(ClientEvent::kRefused); }));
    if (auto* e = a2->last(ClientEvent::kRefused)) CHECK(e->refusal == Refusal::kBanned);
    // the ban survives a server restart (the ban file)
    {
        server::Server s2(base_config(0));
        CHECK(s2.start(why));
        CHECK(s2.admin("bans").find(code) != std::string::npos);
        CHECK(s2.admin("unban " + code).find("unbanned") == 0);
    }
    std::printf("refusals/ban: ok\n");
}

static void test_restart_and_resume() {
    auto cfg = base_config(0);
    std::string why;
    auto s = std::make_unique<server::Server>(cfg);
    CHECK(s->start(why));
    uint16_t port = s->port();
    cfg.port = port;
    auto a = make_bot("alpha", port), b = make_bot("beta", port);
    CHECK(run(s.get(), {a.get(), b.get()}, 5000, [&] { return a->players.count(b->c.my_id()) != 0; }));
    // the server pauses 6 s: the clients lose it, then resume their sessions with the token
    run(nullptr, {a.get(), b.get()}, 6000);
    CHECK(a->c.state() == ClientState::kReconnecting);
    CHECK(run(s.get(), {a.get(), b.get()}, 8000, [&] { return a->online() && b->online(); }));
    if (auto* e = a->last(ClientEvent::kWelcome)) CHECK(e->resumed);
    // the server goes away entirely and comes back: fresh sessions, everyone sees each other again
    s.reset();
    run(nullptr, {a.get(), b.get()}, 6000);
    CHECK(!a->online());
    s = std::make_unique<server::Server>(cfg);
    CHECK(s->start(why));
    CHECK(run(s.get(), {a.get(), b.get()}, 20000, [&] {
        return a->online() && b->online() && a->players.count(b->c.my_id()) && b->players.count(a->c.my_id());
    }));
    std::printf("restart/resume: ok\n");
}

static void test_party() {
    server::Server s(base_config(0));
    std::string why;
    CHECK(s.start(why));
    uint16_t port = s.port();
    auto a = make_bot("alpha", port), b = make_bot("beta", port), c = make_bot("gamma", port);
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 5000, [&] { return a->online() && b->online() && c->online(); }));
    uint64_t code = 0;
    parse_friend_code(b->c.friend_code(), code);
    a->c.party_invite(code);
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
    uint32_t pid = b->last(ClientEvent::kPartyInvited)->party_id;
    b->c.party_accept(pid);
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 3000, [&] {
        auto* e = a->last(ClientEvent::kPartyState);
        return e && e->party.members.size() == 2;
    }));
    // party members see the display name
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 3000, [&] {
        return a->players.count(b->c.my_id()) && a->players[b->c.my_id()].name == "beta";
    }));
    // party members are seen anywhere (at least the mid tier), not only nearby: b sails far away, and a
    // third member who never was near gets a PLAYER_IN too
    b->head.x = 250000;
    b->head.z = 250000;
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 4000, [&] {
        return a->poses.count(b->c.my_id()) && std::fabs(a->poses[b->c.my_id()].head.x - 250000) < 2;
    }));
    // and every member learns where the others are, once a second
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 3000, [&] {
        auto* e = a->last(ClientEvent::kPartyWhere);
        if (!e || e->members.size() != 2) return false;
        for (auto& m : e->members)
            if (m.id == b->c.my_id()) return m.x == 250000 && m.square != 255;
        return false;
    }));
    b->head.x = 0;
    b->head.z = 0;
    // both in one dungeon room: the first to arrive runs the enemies
    a->head.where = Where::of("dun1", 3);
    a->c.send_stage(a->head.where);
    run(&s, {a.get(), b.get(), c.get()}, 300);
    b->head.where = Where::of("dun1", 3);
    b->c.send_stage(b->head.where);
    auto runner_of = [](Bot& bot) -> int {
        int r = -1;
        for (auto& e : bot.log)
            if (e.kind == ClientEvent::kRunners)
                for (auto& x : e.runners)
                    if (x.where.stage_name() == "dun1" && x.where.room == 3) r = x.runner;
        return r;
    };
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 3000, [&] { return runner_of(*b) == a->c.my_id(); }));
    // app relay: to the party and to the runner; a stranger gets nothing
    uint8_t hit[4] = {1, 2, 3, 4};
    AppHeader h;
    h.type = 23;
    h.target = kToRunner;
    b->c.send_app(h, hit, 4, true);
    h.target = kToParty;
    h.type = 24;
    c->c.send_app(h, hit, 4, true);  // not in a party: dropped
    if (!run(&s, {a.get(), b.get(), c.get()}, 5000, [&] { return a->saw(ClientEvent::kApp); })) {
        CHECK(false);
        std::printf("debug: a=%u b=%u\n%s%s\n", a->c.my_id(), b->c.my_id(), s.admin("parties").c_str(), s.admin("players").c_str());
    }
    if (auto* e = a->last(ClientEvent::kApp)) CHECK(e->app_type == 23 && e->id == b->c.my_id() && e->body.size() == 4);
    // team progress: union merge, forwarded live
    std::vector<TeamField> f1 = {{0x100, kFieldBits, std::string("\x01\x00", 2)}, {0x200, kFieldMax, std::string("\x00\x05", 2)},
                                 {0x300, kFieldFill, std::string("\x21\xFF", 2)}};
    std::vector<TeamField> f2 = {{0x100, kFieldBits, std::string("\x00\x80", 2)}, {0x200, kFieldMax, std::string("\x00\x03", 2)},
                                 {0x300, kFieldFill, std::string("\x30\x31", 2)}};
    a->c.send_progress(f1);
    run(&s, {a.get(), b.get(), c.get()}, 300);
    b->c.send_progress(f2);
    CHECK(run(&s, {a.get(), b.get(), c.get()}, 3000, [&] {
        auto* e = a->last(ClientEvent::kTeamState);
        return e && !e->fields.empty() && e->fields[0].value == std::string("\x01\x80", 2);
    }));
    // the runner leaves without a word (game killed): the other member takes over
    uint32_t t0 = monotonic_ms();
    std::vector<Bot*> rest = {b.get(), c.get()};
    a.reset();  // destroying it says bye; also cover silence below
    CHECK(run(&s, rest, 3000, [&] { return runner_of(*b) == b->c.my_id(); }));
    std::printf("party: runner handover after the runner left: %u ms\n", monotonic_ms() - t0);
    // the leader (a) left: the member in the party longest (b) leads now, and the clients are told
    CHECK(run(&s, rest, 3000, [&] {
        auto* e = b->last(ClientEvent::kPartyState);
        return e && e->party.leader == b->c.my_id();
    }));
    // silence (no bye): the new runner b goes quiet; c joins the party and the room, and takes over within ~3 s
    uint64_t ccode = 0;
    parse_friend_code(c->c.friend_code(), ccode);
    b->c.party_invite(ccode);
    CHECK(run(&s, rest, 3000, [&] { return c->saw(ClientEvent::kPartyInvited); }));
    c->c.party_accept(c->last(ClientEvent::kPartyInvited)->party_id);
    c->head.where = Where::of("dun1", 3);
    c->c.send_stage(c->head.where);
    CHECK(run(&s, rest, 3000, [&] { return runner_of(*c) == b->c.my_id(); }));
    t0 = monotonic_ms();
    CHECK(run(&s, {c.get()}, 6000, [&] { return runner_of(*c) == c->c.my_id(); }));
    std::printf("party: runner handover after the runner went silent: %u ms (limit %d ms)\n", monotonic_ms() - t0,
                s.config().runner_drop_ms);
    // ... but the silent leader (b, a game loading a stage for seconds) keeps the lead: no handover under leader_drop_ms
    {
        auto* e = c->last(ClientEvent::kPartyState);
        CHECK(e && e->party.leader == b->c.my_id());
        std::printf("party: the leader kept the lead through %u ms of silence (limit %d ms)\n", monotonic_ms() - t0,
                    s.config().leader_drop_ms);
    }
    // c (a fresh game, same identity) asks for the team state: the last known state is there
    c->c.ask_team_state();
    CHECK(run(&s, {c.get()}, 3000, [&] {
        auto* e = c->last(ClientEvent::kTeamState);
        // union: bits OR'ed, the counter's maximum, a filled slot kept and an empty one filled
        return e && e->fields.size() == 3 && e->fields[0].value == std::string("\x01\x80", 2) &&
               e->fields[1].value == std::string("\x00\x05", 2) && e->fields[2].value == std::string("\x21\x31", 2);
    }));
    std::printf("party/runner/team state: ok\n");
}

static void test_copies() {
    server::Server s(base_config(0));  // copy_size 2
    std::string why;
    CHECK(s.start(why));
    uint16_t port = s.port();
    auto a = make_bot("a", port), b = make_bot("b", port), c = make_bot("c", port);
    std::vector<Bot*> all = {a.get(), b.get(), c.get()};
    CHECK(run(&s, all, 5000, [&] { return a->online() && b->online() && c->online(); }));
    for (Bot* x : all) {
        x->head.where = Where::of("town", 0);
        x->c.send_stage(x->head.where);
        run(&s, all, 200);
    }
    run(&s, all, 1500);
    CHECK(c->saw(ClientEvent::kCopy));
    if (auto* e = c->last(ClientEvent::kCopy)) CHECK(e->copy == 1 && e->copies == 2);
    CHECK(a->players.count(b->c.my_id()) && !a->players.count(c->c.my_id()));
    // a friend of a (party) joins the full copy 0 with a, not the copy with room
    auto d = make_bot("d", port);
    all.push_back(d.get());
    CHECK(run(&s, all, 5000, [&] { return d->online(); }));
    uint64_t code = 0;
    parse_friend_code(d->c.friend_code(), code);
    a->c.party_invite(code);
    CHECK(run(&s, all, 3000, [&] { return d->saw(ClientEvent::kPartyInvited); }));
    d->c.party_accept(d->last(ClientEvent::kPartyInvited)->party_id);
    run(&s, all, 500);
    d->head.where = Where::of("town", 0);
    d->c.send_stage(d->head.where);
    CHECK(run(&s, all, 3000, [&] { return a->players.count(d->c.my_id()) && d->players.count(b->c.my_id()); }));
    std::printf("copies: ok\n");
}

// the safety valve at sea: an area over its cap sends arrivals to a copy; a friend can follow; copies merge back
static void test_sea_copies() {
    auto cfg = base_config(0);
    cfg.area_cap = 2;
    server::Server s(cfg);
    std::string why;
    CHECK(s.start(why));
    uint16_t port = s.port();
    auto a = make_bot("a", port), b = make_bot("b", port), c = make_bot("c", port);
    std::vector<Bot*> all = {a.get(), b.get(), c.get()};
    a->head.x = b->head.x = 1000;
    c->head.x = 60000;  // elsewhere first
    CHECK(run(&s, all, 5000, [&] { return a->players.count(b->c.my_id()) != 0; }));
    run(&s, all, 2500);  // the counts settle
    c->head.x = 1200;    // c sails into the full area: a copy
    CHECK(run(&s, all, 3000, [&] { return c->saw(ClientEvent::kCopy); }));
    if (auto* e = c->last(ClientEvent::kCopy)) CHECK(e->copy == 1);
    run(&s, all, 1000);
    CHECK(!a->players.count(c->c.my_id()) && !c->players.count(a->c.my_id()));
    // c goes to its friend a's copy (over the cap: friends are let in)
    c->c.join_copy_of(a->c.key_hash());
    CHECK(run(&s, all, 3000, [&] { return c->players.count(a->c.my_id()) != 0; }));
    // b leaves the area: copies 0 and 1 fit together, so they merge back (c is in 0 already; check counts settle)
    std::printf("sea copies: ok\n");
}

static void test_flood() {
    auto cfg = base_config(0);
    cfg.strikes_kick = 50;
    server::Server s(cfg);
    std::string why;
    CHECK(s.start(why));
    auto a = make_bot("flood", s.port());
    CHECK(run(&s, {a.get()}, 3000, [&] { return a->online(); }));
    // 500 emotes and 500 reports at once: over the reliable-message rate
    for (int i = 0; i < 2000; ++i) a->c.send_emote(1);
    CHECK(run(&s, {a.get()}, 5000, [&] { return !a->online(); }));
    CHECK(s.metrics().kicked >= 1);
    // the reason comes as a code: removed for a limit, which one
    if (auto* e = a->last(ClientEvent::kNotice)) CHECK(e->value == kNoticeKickLimit && e->seconds == kLimitReliable);
    std::printf("flood kick: ok\n");
}

// scaling in one process: a coordinator (pool mode) and two workers that register with it; regions split and move
// by admin command, players hand over between workers with their ids, parties work across workers
static void test_scaling() {
    std::string why;
    auto cc = base_config(0);
    cc.scale_mode = "pool";
    cc.link_port = 0;
    cc.link_secret = "test-link-secret-0123456789";
    cc.split_cpu = 1e9;  // no automatic changes in this test
    cc.split_bw = 1e18;
    auto coord = std::make_unique<server::Server>(cc);
    coord->log = [](const std::string& m) { if (std::getenv("SCALING_DEBUG")) std::printf("  coord: %s\n", m.c_str()); };
    CHECK(coord->start(why));
    std::unique_ptr<server::Server> wk[2];
    for (int i = 0; i < 2; ++i) {
        auto c = base_config(0);
        c.worker = true;
        c.join = "127.0.0.1:" + std::to_string(coord->link_port());
        c.link_port = 0;
        c.link_secret = cc.link_secret;
        c.key_file = cc.key_file;  // one server key for all workers
        wk[i] = std::make_unique<server::Server>(c);
        wk[i]->log = [](const std::string&) {};
        CHECK(wk[i]->start(why));
    }
    auto all3 = [&](std::vector<Bot*> bots, uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            if (coord) coord->step(1);
            for (auto& w : wk)
                if (w) w->step(0);
            for (Bot* b : bots) b->poll(monotonic_ms());
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    CHECK(all3({}, 5000, [&] {
        return wk[0]->worker_index() > 0 && wk[1]->worker_index() > 0 && coord->region_map().workers.size() == 3 &&
               wk[0]->region_map().version == coord->region_map().version && wk[1]->region_map().version == coord->region_map().version;
    }));
    int w1 = wk[0]->worker_index(), w2 = wk[1]->worker_index();
    uint16_t port = coord->port();
    auto a = make_bot("a", port), b = make_bot("b", port), c = make_bot("c", port);
    std::vector<Bot*> bots = {a.get(), b.get(), c.get()};
    // square 24 (the middle one) spans x, z in [-50000, 50000)
    a->head.x = -500;   // b crosses near a: the near tier, every pose
    c->head.x = -3000;
    b->head.x = 2400;
    CHECK(all3(bots, 5000, [&] {
        return a->players.count(b->c.my_id()) && b->players.count(a->c.my_id()) && a->players.count(c->c.my_id());
    }));
    uint16_t bid = b->c.my_id(), aid = a->c.my_id();
    uint32_t region = 0;
    for (auto& g : coord->region_map().regions)
        if (g.x0 == 24 && g.z0 == 24) region = g.id;
    // split the square: its east half (x >= 0) goes to worker w1, and b with it
    CHECK(coord->admin("split " + std::to_string(region) + " " + std::to_string(w1)).find("split into") == 0);
    bool a_lost_b = false, b_lost_a = false;
    auto watch = [&](uint32_t ms, std::function<bool()> done = nullptr) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            all3(bots, 30, nullptr);
            if (!a->players.count(bid)) a_lost_b = true;
            if (!b->players.count(aid)) b_lost_a = true;
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    auto on = [&](server::Server& sv, Bot& x) {
        Session* t = sv.find_by_key(x.c.key_hash());
        return t && t->online && !t->redirected && x.online();
    };
    CHECK(watch(8000, [&] { return on(*wk[0], *b); }));
    CHECK(b->c.my_id() == bid && b->online());
    std::printf("scaling: a's longest gap between b's poses through the handover: %u ms\n", a->near_gap[bid]);
    CHECK(a->near_gap[bid] <= 200);
    a->near_gap.clear();
    // b sails back west (x < 0, 2000 deep): back on the coordinator
    for (float x = 2400; x >= -6000; x -= 300) {
        b->head.x = x;
        watch(70);
    }
    CHECK(watch(8000, [&] { return on(*coord, *b); }));
    watch(1000);
    if (std::getenv("SCALING_DEBUG"))
        for (auto& e : b->log)
            if (e.kind == ClientEvent::kStateChanged || e.kind == ClientEvent::kWelcome || e.kind == ClientEvent::kRefused)
                std::printf("  b event %d state %d resumed %d text %s\n", int(e.kind), int(e.state), e.resumed, e.text.c_str());
    std::printf("scaling: ... and back: %u ms\n", a->near_gap[bid]);
    CHECK(a->near_gap[bid] <= 200);
    CHECK(!a_lost_b && !b_lost_a);
    std::printf("scaling: split, handover there and back, visible sets continuous (a lost b: %d, b lost a: %d)\n", a_lost_b,
                b_lost_a);
    // the west half moves to worker w2: a, b and c follow, and a party works across workers
    uint32_t west = 0;
    for (auto& g : coord->region_map().regions)
        if (g.parent == region && g.x0 == 24) west = g.id;
    a->head.x = -10000;  // a deeper into the west half, so the whole half's move below takes it along
    b->head.x = 20000;  // b goes east first (worker w1)
    CHECK(watch(8000, [&] { return on(*wk[0], *b); }));
    CHECK(coord->admin("move " + std::to_string(west) + " " + std::to_string(w2)) == "moved");
    CHECK(watch(8000, [&] { return on(*wk[1], *a) && on(*wk[1], *c); }));
    uint64_t code = 0;
    parse_friend_code(b->c.friend_code(), code);
    a->c.party_invite(code);
    CHECK(all3(bots, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
    b->c.party_accept(b->last(ClientEvent::kPartyInvited)->party_id);
    CHECK(all3(bots, 5000, [&] {
        auto* e = a->last(ClientEvent::kPartyState);
        return e && e->party.members.size() == 2 && b->online();
    }));
    // b joined a's party on a's worker, as a guest in a region another worker runs
    CHECK(wk[1]->find_by_key(b->c.key_hash()) != nullptr);
    CHECK(all3(bots, 3000, [&] { return b->players.count(aid) != 0; }));  // the party: seen anywhere
    // c sails east into worker w1's half: b, a guest of w2 there, sees c through w1's ghosts
    for (float x = -3000; x <= 16000; x += 400) {
        c->head.x = x;
        all3(bots, 40, nullptr);
    }
    CHECK(all3(bots, 8000, [&] { return wk[0]->find_by_key(c->c.key_hash()) && b->players.count(c->c.my_id()); }));
    CHECK(wk[1]->find_by_key(b->c.key_hash()) != nullptr);
    std::printf("scaling: region move, cross-worker invite, party guest: ok\n");
    // worker w2 goes away: its players come back through the coordinator
    wk[1].reset();
    CHECK(all3(bots, 25000, [&] { return a->online() && b->online() && c->online() && coord->find_by_key(a->c.key_hash()); }));
    std::printf("scaling: ok\n");
}

// many players: a region moves to a worker, then splits with one half back on the coordinator; players on each side
// end up on the half's worker
static void test_scaling_mass() {
    std::string why;
    auto cc = base_config(0);
    cc.scale_mode = "pool";
    cc.link_port = 0;
    cc.link_secret = "test-link-secret-0123456789";
    cc.split_cpu = 1e9;
    cc.split_bw = 1e18;
    cc.per_ip = 1000;
    cc.pow_bits = 0;
    cc.loopback_exempt = true;
    auto coord = std::make_unique<server::Server>(cc);
    coord->log = [](const std::string& m) { std::printf("  coord: %s\n", m.c_str()); };
    CHECK(coord->start(why));
    auto c = base_config(0);
    c.worker = true;
    c.join = "127.0.0.1:" + std::to_string(coord->link_port());
    c.link_port = 0;
    c.link_secret = cc.link_secret;
    c.key_file = cc.key_file;
    c.per_ip = 1000;
    c.pow_bits = 0;
    c.loopback_exempt = true;
    auto wk = std::make_unique<server::Server>(c);
    wk->log = [](const std::string& m) { std::printf("  worker: %s\n", m.c_str()); };
    CHECK(wk->start(why));
    std::vector<std::unique_ptr<Bot>> bots;
    std::vector<Bot*> all;
    auto go = [&](uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            coord->step(1);
            wk->step(0);
            for (Bot* b : all) b->poll(monotonic_ms());
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    CHECK(go(5000, [&] { return wk->worker_index() > 0 && coord->region_map().workers.size() == 2; }));
    for (int i = 0; i < 60; ++i) {
        bots.push_back(make_bot("m" + std::to_string(i), coord->port()));
        bots.back()->head.x = -45000.0f + float(i % 10) * 9500.0f;  // square 24: x in [-50000, 50000)
        bots.back()->head.z = -40000.0f + float(i / 10) * 13000.0f;
        all.push_back(bots.back().get());
    }
    CHECK(go(15000, [&] { return std::all_of(all.begin(), all.end(), [](Bot* b) { return b->online(); }); }));
    int w = wk->worker_index();
    CHECK(coord->admin("move 25 " + std::to_string(w)) == "moved");
    auto count = [&](server::Server& sv) {
        int n = 0;
        for (Bot* b : all)
            if (Session* t = sv.find_by_key(b->c.key_hash()); t && t->online && !t->redirected) ++n;
        return n;
    };
    CHECK(go(10000, [&] { return count(*wk) == 60; }));
    std::printf("scaling mass: after the move, worker %d coord %d\n", count(*wk), count(*coord));
    std::string r = coord->admin("split 25 0");
    std::printf("scaling mass: %s\n", r.c_str());
    go(8000, nullptr);
    int east = 0;
    for (Bot* b : all) east += b->head.x >= 2000 ? 1 : 0;
    std::printf("scaling mass: after the split, worker %d coord %d (east of the line, 2000 deep: %d)\n", count(*wk), count(*coord), east);
    CHECK(count(*coord) >= east - 6);
}

// a bad network between the clients and the server: loss, duplication, delay with jitter (reordering)
struct Relay {
    UdpSocket front;  // clients talk to this
    UdpSocket back;   // towards the server
    Address server, client;
    double loss, dup;
    uint32_t delay, jitter;
    std::mt19937 rng{7};
    struct Held {
        uint32_t at;
        Bytes d;
        bool up;
    };
    std::vector<Held> held;
    void pump() {
        uint8_t buf[2048];
        Address from;
        std::uniform_real_distribution<double> u(0, 1);
        auto take = [&](UdpSocket& s, bool up) {
            for (;;) {
                int n = s.recv_from(from, buf, sizeof buf);
                if (n < 0) break;
                if (up) client = from;
                int copies = u(rng) < loss ? 0 : (u(rng) < dup ? 2 : 1);
                for (int i = 0; i < copies; ++i)
                    held.push_back({monotonic_ms() + delay + uint32_t(rng() % (jitter + 1)), Bytes(buf, buf + n), up});
            }
        };
        take(front, true);
        take(back, false);
        uint32_t now = monotonic_ms();
        for (size_t i = 0; i < held.size();) {
            if (int32_t(now - held[i].at) >= 0) {
                if (held[i].up) back.send_to(server, held[i].d.data(), held[i].d.size());
                else if (client.valid()) front.send_to(client, held[i].d.data(), held[i].d.size());
                held.erase(held.begin() + long(i));
            } else {
                ++i;
            }
        }
    }
};

static void test_chaos(const char* profile, double loss, double dup, uint32_t delay, uint32_t jitter) {
    server::Server s(base_config(0));
    s.log = [](const std::string&) {};
    std::string why;
    CHECK(s.start(why));
    // one relay per client, so each keeps its own address
    Relay ra, rb;
    for (Relay* r : {&ra, &rb}) {
        r->loss = loss;
        r->dup = dup;
        r->delay = delay;
        r->jitter = jitter;
        CHECK(r->front.open("127.0.0.1", 0, why) && r->back.open("127.0.0.1", 0, why));
        resolve("127.0.0.1", s.port(), r->server, why);
    }
    auto a = make_bot("a", ra.front.local_port()), b = make_bot("b", rb.front.local_port());
    auto go = [&](uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            s.step(1);
            ra.pump();
            rb.pump();
            a->poll(monotonic_ms());
            b->poll(monotonic_ms());
            if (done()) return true;
        }
        return done();
    };
    uint32_t t0 = monotonic_ms();
    CHECK(go(20000, [&] { return a->players.count(b->c.my_id()) && b->players.count(a->c.my_id()); }));
    uint32_t seen_ms = monotonic_ms() - t0;
    // party traffic still arrives complete and in order
    uint64_t code = 0;
    parse_friend_code(b->c.friend_code(), code);
    a->c.party_invite(code);
    CHECK(go(10000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
    b->c.party_accept(b->last(ClientEvent::kPartyInvited)->party_id);
    CHECK(go(10000, [&] { return a->last(ClientEvent::kPartyState) && a->last(ClientEvent::kPartyState)->party.members.size() == 2; }));
    AppHeader h;
    h.type = 9;
    for (int i = 0; i < 50; ++i) {
        uint8_t v = uint8_t(i);
        a->c.send_app(h, &v, 1, true);
    }
    Bytes big(20000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 13);
    h.type = 10;
    a->c.send_app(h, big.data(), big.size(), true);
    auto got_all = [&] {
        int n = 0;
        bool order = true, big_ok = false;
        for (auto& e : b->log)
            if (e.kind == ClientEvent::kApp && e.app_type == 9) order = order && e.body.size() == 1 && e.body[0] == n++;
            else if (e.kind == ClientEvent::kApp && e.app_type == 10) big_ok = e.body == big;
        return n == 50 && order && big_ok;
    };
    CHECK(go(20000, got_all));
    std::printf("chaos %s (loss %.0f%%, dup %.0f%%, delay %u+%u ms): seen after %u ms, ping %d ms, 50 ordered + 20 KB app "
                "messages: %s\n",
                profile, loss * 100, dup * 100, delay, jitter, seen_ms, a->c.ping_ms(), got_all() ? "ok" : "FAILED");
}

// a party survives a server crash and restart: (A) from the saved team state, (B) from the records the games
// re-send (the leader's own claim, a member's leader-signed record)
static void test_party_restart() {
    for (int mode = 0; mode < 2; ++mode) {
        auto cfg = base_config(0);
        if (mode == 0) {
            cfg.team_state_file = g_dir + "/team-state-test.txt";
            if (std::FILE* f = std::fopen(cfg.team_state_file.c_str(), "w")) std::fclose(f);  // a clean start
        }
        std::string why;
        auto s1 = std::make_unique<server::Server>(cfg);
        s1->log = [](const std::string&) {};
        CHECK(s1->start(why));
        cfg.port = s1->port();
        auto a = make_bot("lead", cfg.port), b = make_bot("member", cfg.port);
        std::vector<Bot*> both = {a.get(), b.get()};
        CHECK(run(s1.get(), both, 5000, [&] { return a->online() && b->online(); }));
        uint64_t code = 0;
        parse_friend_code(b->c.friend_code(), code);
        a->c.party_invite(code);
        CHECK(run(s1.get(), both, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
        b->c.party_accept(b->last(ClientEvent::kPartyInvited)->party_id);
        CHECK(run(s1.get(), both, 3000, [&] {
            auto* e = a->last(ClientEvent::kPartyState);
            return e && e->party.members.size() == 2;
        }));
        PartyState st = a->last(ClientEvent::kPartyState)->party;
        std::vector<TeamField> f = {{0x6A4, kFieldBits, std::string("\x01", 1)}};
        b->c.send_progress(f);
        run(s1.get(), both, 3000);  // the save is due within 2 s of a change
        // the leader's record, given to the member while the party lasts
        PartyRecord rec = make_party_record(st, a->id, 1000);
        CHECK(party_record_signed_by_leader(rec));
        s1->abandon();  // a crash: no final save
        s1.reset();
        run(nullptr, both, 6000);
        auto s2 = std::make_unique<server::Server>(cfg);
        s2->log = [](const std::string&) {};
        CHECK(s2->start(why));
        a->log.clear();
        b->log.clear();
        CHECK(run(s2.get(), both, 20000, [&] { return a->online() && b->online(); }));
        if (mode == 1) {
            // the member comes first with the leader-signed record, then the leader claims its own
            b->c.party_restore(rec);
            run(s2.get(), both, 300);
            PartyRecord own = make_party_record(st, a->id, 1001);
            a->c.party_restore(own);
            b->c.send_progress(f);  // and the members re-send their progress
        }
        bool together = run(s2.get(), both, 5000, [&] {
            auto* ea = a->last(ClientEvent::kPartyState);
            auto* eb = b->last(ClientEvent::kPartyState);
            return ea && eb && ea->party.party == st.party && eb->party.party == st.party && ea->party.members.size() == 2;
        });
        CHECK(together);
        for (Bot* x : both)
            for (auto& e : x->log)
                if (e.kind == ClientEvent::kPartyState && e.party.party) CHECK(e.party.leader != 0);  // never "leader 0"
        a->c.ask_team_state();
        bool progress = run(s2.get(), both, 3000, [&] {
            auto* e = a->last(ClientEvent::kTeamState);
            if (!e) return false;
            for (auto& x : e->fields)
                if (x.offset == 0x6A4 && x.value == std::string("\x01", 1)) return true;
            return false;
        });
        CHECK(progress);
        std::printf("party restart (%s): same party %d, progress kept %d\n", mode == 0 ? "saved team state" : "re-sent records",
                    together, progress);
    }
}

// the fork's full-size poses (~680 B at 30 Hz) are over the default 8 KB/s upload: warned, then removed with
// the upload reason code
static void test_upload_limit() {
    server::Server s(base_config(0));
    std::string why;
    CHECK(s.start(why));
    auto a = make_bot("big", s.port());
    a->blob = Bytes(680, 1);
    CHECK(run(&s, {a.get()}, 20000, [&] { return a->c.state() == ClientState::kStopped; }));
    bool warned = false, removed = false;
    for (auto& e : a->log)
        if (e.kind == ClientEvent::kNotice) {
            if (e.value == kNoticeThrottle && e.seconds == kLimitUpload) warned = true;
            if (e.value == kNoticeKickLimit && e.seconds == kLimitUpload) removed = true;
        }
    CHECK(warned && removed);
    if (auto* e = a->last(ClientEvent::kNotice)) std::printf("upload limit: \"%s\"\n", e->text.c_str());
}

// the TCP fallback (C25): a session over TCP only; the automatic fallback when UDP gets no answer; a closed
// connection resumes
static void test_tcp() {
    for (int variant = 0; variant < 2; ++variant) {
        auto cfg = base_config(0);
        if (variant == 1) cfg.udp = false;  // UDP "blocked": only TCP gets through
        server::Server s(cfg);
        std::string why;
        CHECK(s.start(why));
        g_mode = variant == 0 ? TransportMode::kTcpOnly : TransportMode::kAuto;
        auto a = make_bot("a", s.port()), b = make_bot("b", s.port());
        g_mode = TransportMode::kAuto;
        uint32_t t0 = monotonic_ms();
        CHECK(run(&s, {a.get(), b.get()}, 15000, [&] {
            return a->players.count(b->c.my_id()) && b->players.count(a->c.my_id()) && a->poses.count(b->c.my_id());
        }));
        CHECK(a->c.transport() == Transport::kTcp && b->c.transport() == Transport::kTcp);
        std::printf("tcp (%s): both online and seeing each other after %u ms, status \"%s\"\n",
                    variant == 0 ? "TCP only" : "fallback from blocked UDP", monotonic_ms() - t0, a->c.status_line().c_str());
        // reliable messages and a party over TCP
        uint64_t code = 0;
        parse_friend_code(b->c.friend_code(), code);
        a->c.party_invite(code);
        CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
    }
    // a closed TCP connection: the session is kept and the client resumes it
    auto cfg = base_config(0);
    cfg.udp = false;
    server::Server s(cfg);
    std::string why;
    CHECK(s.start(why));
    g_mode = TransportMode::kTcpOnly;
    auto a = make_bot("a", s.port());
    g_mode = TransportMode::kAuto;
    CHECK(run(&s, {a.get()}, 5000, [&] { return a->online(); }));
    a->log.clear();
    s.drop_tcp();
    CHECK(run(&s, {a.get()}, 10000, [&] { return a->saw(ClientEvent::kWelcome) && a->online(); }));
    if (auto* e = a->last(ClientEvent::kWelcome)) CHECK(e->resumed);
    CHECK(a->c.transport() == Transport::kTcp);
    std::printf("tcp: a dropped connection resumed the session: ok\n");
}

// the server's mod rules: the config for each rule type, the rules shown before joining (pages), refusals that
// name each mod, and party matching
static ModRef mref(const char* id, const char* ver, uint8_t h) {
    ModRef m;
    m.id = id;
    m.version = ver;
    m.hash.fill(h);
    return m;
}

static void test_mod_rules_server() {
    // config: each rule type, and a bad entry refused
    {
        server::Config c;
        std::string why;
        CHECK(server::parse_config_text("[mods]\nrequired = [\"randomizer\"]\nallowed = [\"qol@1.2\"]\n"
                                        "banned = [\"speed@*\"]\n", c, why));
        CHECK(c.mods.rules.size() == 3 && !c.mods.vanilla_only);
        server::Config v;
        CHECK(server::parse_config_text("[mods]\nvanilla_only = true\n", v, why) && v.mods.vanilla_only);
        server::Config bad;
        CHECK(!server::parse_config_text("[mods]\nallowed = [\"Not An Id\"]\n", bad, why));
    }
    auto cfg = base_config(0);
    std::string why;
    CHECK(server::parse_config_text("[mods]\nrequired = [\"randomizer\"]\nallowed = [\"qol@1.2\"]\n", cfg, why));
    for (int i = 0; i < 40; ++i) {  // enough rules for more than one page
        ModRule r;
        parse_mod_rule("banned-mod-number-" + std::to_string(i) + "@1.0#" + std::string(64, 'b'), ModRuleKind::kBanned, r);
        cfg.mods.rules.push_back(r);
    }
    server::Server s(cfg);
    CHECK(s.start(why));
    uint16_t port = s.port();
    // the server list: the rules before joining, all pages
    {
        Bot q;
        ClientConfig qc;
        qc.port = port;
        qc.game_data = 0x1111;
        qc.identity = crypto::Identity::generate();
        qc.query_only = true;
        CHECK(q.c.start(qc, why));
        CHECK(run(&s, {&q}, 5000, [&] { return q.saw(ClientEvent::kServerRules) && q.c.state() == ClientState::kStopped; }));
        if (auto* e = q.last(ClientEvent::kServerRules)) {
            CHECK(e->info.mods.rules.size() == 42 && e->info.rule_pages > 1 && (e->info.mod_flags & 2));
            std::printf("mod rules: %zu rules in %u pages before joining\n", e->info.mods.rules.size(), unsigned(e->info.rule_pages));
        }
    }
    auto make = [&](const char* name, std::vector<ModRef> mods) {
        auto b = std::make_unique<Bot>();
        ClientConfig c;
        c.port = port;
        c.game_data = 0x1111;
        c.identity = crypto::Identity::generate();
        c.name = name;
        c.mods = mods;
        b->id = c.identity;
        std::string w;
        CHECK(b->c.start(c, w));
        return b;
    };
    // refused, with every problem named
    auto bad = make("bad", {mref("qol", "1.1", 1), mref("speed", "2", 2), mref("banned-mod-number-3", "1.0", 0xbb)});
    CHECK(run(&s, {bad.get()}, 5000, [&] { return bad->saw(ClientEvent::kRefused); }));
    if (auto* e = bad->last(ClientEvent::kRefused)) {
        CHECK(e->refusal == Refusal::kMods);
        auto has = [&](ModProblemKind k, const char* id) {
            return std::any_of(e->mod_problems.begin(), e->mod_problems.end(),
                               [&](const ModProblem& p) { return p.problem == k && p.id == id; });
        };
        CHECK(has(ModProblemKind::kMissing, "randomizer") && has(ModProblemKind::kWrongVersion, "qol") &&
              has(ModProblemKind::kNotAllowed, "speed") && has(ModProblemKind::kBanned, "banned-mod-number-3"));
        std::printf("mod rules: refused: \"%s\", %zu problems\n", e->text.c_str(), e->mod_problems.size());
    }
    // allowed in; party matching between two players who pass the rules but differ
    auto a = make("a", {mref("randomizer", "", 7), mref("qol", "1.2", 1)});
    auto b = make("b", {mref("randomizer", "", 8)});
    CHECK(run(&s, {a.get(), b.get()}, 5000, [&] { return a->online() && b->online(); }));
    uint64_t code = 0;
    parse_friend_code(b->c.friend_code(), code);
    a->c.party_invite(code);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
    auto* inv = b->last(ClientEvent::kPartyInvited);
    CHECK(inv && inv->mods.size() == 2);  // the party's mod set comes with the invite
    b->c.party_accept(inv->party_id);
    CHECK(run(&s, {a.get(), b.get()}, 3000, [&] {
        auto* e = b->last(ClientEvent::kPartyResult);
        return e && e->value == 10;
    }));
    if (auto* e = b->last(ClientEvent::kPartyResult)) CHECK(e->mod_problems.size() == 2);  // randomizer seed, qol
    CHECK(!b->last(ClientEvent::kPartyState) || b->last(ClientEvent::kPartyState)->party.members.size() < 2);
    std::printf("mod rules: party join refused with %zu differences\n", b->last(ClientEvent::kPartyResult)->mod_problems.size());
}

// block and mute (C14): what stops reaching whom, per account (it survives a reconnect), and lifting it
static void test_block_mute() {
    server::Server s(base_config(0));
    std::string why;
    CHECK(s.start(why));
    uint16_t port = s.port();
    auto a = make_bot("a", port), b = make_bot("b", port), c = make_bot("c", port);
    std::vector<Bot*> all = {a.get(), b.get(), c.get()};
    a->head.x = 1000;
    b->head.x = 1300;
    c->head.x = 1600;
    CHECK(run(&s, all, 5000, [&] { return a->players.count(b->c.my_id()) && a->players.count(c->c.my_id()); }));
    // a mutes b: b's emotes reach c, not a; b's poses still reach a
    a->c.mute(b->c.key_hash(), true);
    run(&s, all, 300);
    b->c.send_emote(3);
    CHECK(run(&s, all, 2000, [&] { return c->saw(ClientEvent::kEmote); }));
    bool a_emote = false;
    for (auto& e : a->log)
        if (e.kind == ClientEvent::kEmote) a_emote = true;
    CHECK(!a_emote && a->players.count(b->c.my_id()));
    CHECK(s.metrics().muted_drops >= 1);
    // a blocks c: neither sees the other, and c's party messages to a are dropped
    uint64_t code = 0;
    parse_friend_code(a->c.friend_code(), code);
    c->c.party_invite(code);
    CHECK(run(&s, all, 3000, [&] { return a->saw(ClientEvent::kPartyInvited); }));
    a->c.party_accept(a->last(ClientEvent::kPartyInvited)->party_id);
    CHECK(run(&s, all, 3000, [&] { return c->last(ClientEvent::kPartyState) && c->last(ClientEvent::kPartyState)->party.members.size() == 2; }));
    a->c.block(c->c.key_hash(), true);
    CHECK(run(&s, all, 3000, [&] { return !a->players.count(c->c.my_id()) && !c->players.count(a->c.my_id()); }));
    a->log.clear();
    AppHeader h;
    h.type = 50;
    uint8_t v = 1;
    c->c.send_app(h, &v, 1, true);
    run(&s, all, 1000);
    bool app = false;
    for (auto& e : a->log)
        if (e.kind == ClientEvent::kApp) app = true;
    CHECK(!app && s.metrics().blocked_drops >= 1);
    // per account: a comes back as a new game (same identity) and the block still holds
    auto id = a->id;
    a.reset();
    a = make_bot("a", port, 0x1111, &id);
    a->head.x = 1000;
    all = {a.get(), b.get(), c.get()};
    CHECK(run(&s, all, 5000, [&] { return a->online() && a->players.count(b->c.my_id()); }));
    run(&s, all, 1000);
    CHECK(!a->players.count(c->c.my_id()));
    // lifted
    a->c.block(c->c.key_hash(), false);
    CHECK(run(&s, all, 3000, [&] { return a->players.count(c->c.my_id()) != 0; }));
    std::printf("block/mute: muted drops %llu, blocked drops %llu: ok\n", (unsigned long long)s.metrics().muted_drops.load(),
                (unsigned long long)s.metrics().blocked_drops.load());
}

// a party of 16 (party.max_size = 16): enemy snapshots to 15, hits from 15, the runner leaving, team progress from
// 16, and the whole party carried across a handover between workers
static std::vector<std::unique_ptr<Bot>> make_party(server::Server& s, std::vector<server::Server*> others, int n,
                                                    uint16_t port, std::vector<Bot*>& all, float x0) {
    std::vector<std::unique_ptr<Bot>> v;
    for (int i = 0; i < n; ++i) {
        v.push_back(make_bot("p" + std::to_string(i), port));
        v.back()->head.x = x0 + float(i % 4) * 300.0f;
        v.back()->head.z = float(i / 4) * 300.0f;
        all.push_back(v.back().get());
    }
    auto go = [&](uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            s.step(1);
            for (auto* o : others) o->step(0);
            for (Bot* b : all) b->poll(0);
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    CHECK(go(15000, [&] { return std::all_of(v.begin(), v.end(), [](auto& b) { return b->online(); }); }));
    for (int i = 1; i < n; ++i) {
        uint64_t code = 0;
        parse_friend_code(v[size_t(i)]->c.friend_code(), code);
        v[0]->c.party_invite(code);
        CHECK(go(3000, [&] { return v[size_t(i)]->saw(ClientEvent::kPartyInvited); }));
        v[size_t(i)]->c.party_accept(v[size_t(i)]->last(ClientEvent::kPartyInvited)->party_id);
    }
    CHECK(go(5000, [&] {
        auto* e = v[0]->last(ClientEvent::kPartyState);
        return e && int(e->party.members.size()) == n;
    }));
    return v;
}

static void test_party_16() {
    // the setting's range
    {
        server::Config c;
        std::string why;
        CHECK(server::parse_config_text("[party]\nmax_size = 16\n", c, why) && c.party_max == 16);
        CHECK(!server::parse_config_text("[party]\nmax_size = 17\n", c, why) && why.find("between 2 and 16") != std::string::npos);
        CHECK(!server::parse_config_text("[party]\nmax_size = 1\n", c, why));
        server::Config d;
        CHECK(d.party_max == 8);
    }
    {
        server::Config c;
        std::string why;
        CHECK(server::parse_config_text("[limits]\ninvite_every_s = 30\n", c, why) && c.invite_every_s == 30);
        CHECK(!server::parse_config_text("[limits]\ninvite_every_s = 0\n", c, why));
    }
    // the default invite rate (one per 10 s): the leader fills the party of 16 at once, 15 invites in a row
    auto cfg = base_config(0);
    cfg.party_max = 16;
    cfg.per_ip = 64;
    server::Server s(cfg);
    s.log = [](const std::string&) {};
    std::string why;
    CHECK(s.start(why));
    std::vector<Bot*> all;
    auto v = make_party(s, {}, 16, s.port(), all, 1000);
    auto go = [&](uint32_t ms, std::function<bool()> done) { return run(&s, all, ms, done); };
    if (auto* e = v[0]->last(ClientEvent::kPartyState)) CHECK(e->party.max == 16 && e->party.members.size() == 16);
    // a 17th is refused: the party is full
    {
        auto extra = make_bot("p16", s.port());
        all.push_back(extra.get());
        CHECK(go(5000, [&] { return extra->online(); }));
        uint64_t code = 0;
        parse_friend_code(extra->c.friend_code(), code);
        v[0]->c.party_invite(code);
        CHECK(go(3000, [&] {
            auto* e = v[0]->last(ClientEvent::kPartyResult);
            return e && e->value == 5;
        }));
        all.pop_back();
    }
    // the burst is spent: one more invite (after a member leaves) waits for the refill
    {
        auto* last = v.back().get();
        last->c.party_leave();
        CHECK(go(3000, [&] { auto* e = v[0]->last(ClientEvent::kPartyState); return e && e->party.members.size() == 15; }));
        uint64_t code = 0;
        parse_friend_code(last->c.friend_code(), code);
        last->log.clear();
        v[0]->log.clear();
        v[0]->c.party_invite(code);
        CHECK(go(3000, [&] { auto* e = v[0]->last(ClientEvent::kPartyResult); return e && e->value == 1; }));
        uint32_t t0 = monotonic_ms(), next = t0 + 1000;
        CHECK(go(12000, [&] {
            if (int32_t(monotonic_ms() - next) >= 0 && !last->saw(ClientEvent::kPartyInvited)) {
                next += 1000;
                v[0]->c.party_invite(code);
            }
            return last->saw(ClientEvent::kPartyInvited);
        }));
        std::printf("party of 16: filled with 15 invites in a row; the next invite waited %u ms for the refill\n",
                    monotonic_ms() - t0);
        last->c.party_accept(last->last(ClientEvent::kPartyInvited)->party_id);
        CHECK(go(3000, [&] { auto* e = v[0]->last(ClientEvent::kPartyState); return e && e->party.members.size() == 16; }));
    }
    // all in one dungeon room: p0 (first there) runs the enemies
    for (auto& b : v) {
        b->head.where = Where::of("dun1", 3);
        b->c.send_stage(b->head.where);
        go(40, nullptr);
    }
    auto runner_seen = [&](Bot& b) {
        int r = -1;
        for (auto& e : b.log)
            if (e.kind == ClientEvent::kRunners)
                for (auto& x : e.runners)
                    if (x.where.stage_name() == "dun1" && x.where.room == 3) r = x.runner;
        return r;
    };
    CHECK(go(3000, [&] { return std::all_of(v.begin(), v.end(), [&](auto& b) { return runner_seen(*b) == v[0]->c.my_id(); }); }));
    // enemy snapshots from the runner to 15 members (unreliable, 400 bytes, 10 Hz for 2 s)
    for (auto& b : v) b->log.clear();
    uint8_t snap[400] = {};
    AppHeader sh;
    sh.type = 31;
    sh.target = kToParty;
    for (int k = 0; k < 20; ++k) {
        v[0]->c.send_app(sh, snap, sizeof snap, false);
        go(100, nullptr);
    }
    go(300, nullptr);
    int min_snaps = 1 << 30;
    for (size_t i = 1; i < v.size(); ++i) {
        int n = 0;
        for (auto& e : v[i]->log)
            if (e.kind == ClientEvent::kApp && e.app_type == 31 && e.body.size() == 400) ++n;
        min_snaps = std::min(min_snaps, n);
    }
    CHECK(min_snaps >= 18);
    // hit claims from the 15 others, to the runner
    v[0]->log.clear();
    AppHeader hh;
    hh.type = 32;
    hh.target = kToRunner;
    for (size_t i = 1; i < v.size(); ++i) {
        uint8_t hit[4] = {uint8_t(i), 1, 2, 3};
        v[i]->c.send_app(hh, hit, 4, true);
    }
    std::set<uint16_t> from;
    CHECK(go(3000, [&] {
        from.clear();
        for (auto& e : v[0]->log)
            if (e.kind == ClientEvent::kApp && e.app_type == 32) from.insert(e.id);
        return from.size() == 15;
    }));
    // team progress: each of the 16 sets its own bit; everyone ends with all 16
    for (size_t i = 0; i < v.size(); ++i) {
        std::string val(2, '\0');
        val[i / 8] = char(1 << (i % 8));
        v[i]->c.send_progress({{0x700, kFieldBits, val}});
    }
    go(1500, nullptr);
    v[15]->c.ask_team_state();
    CHECK(go(3000, [&] {
        auto* e = v[15]->last(ClientEvent::kTeamState);
        if (!e) return false;
        for (auto& f : e->fields)
            if (f.offset == 0x700) return f.value == std::string("\xFF\xFF", 2);
        return false;
    }));
    // the runner leaves: the next member there takes over, for all 15
    uint16_t old = v[0]->c.my_id();
    uint32_t t0 = monotonic_ms();
    all.erase(all.begin());
    v[0].reset();
    CHECK(go(3000, [&] {
        return std::all_of(v.begin() + 1, v.end(), [&](auto& b) { int r = runner_seen(*b); return r > 0 && r != old; });
    }));
    std::printf("party of 16: snapshots >= %d of 20 at every member, 15 hit claims, union of 16 bits, runner handed over in %u ms\n",
                min_snaps, monotonic_ms() - t0);
}

// a party of 16 crossing into another worker's region: all of it moves, together, and stays one party
static void test_party_16_handover() {
    std::string why;
    auto cc = base_config(0);
    cc.scale_mode = "pool";
    cc.link_port = 0;
    cc.link_secret = "test-link-secret-0123456789";
    cc.split_cpu = 1e9;
    cc.split_bw = 1e18;
    cc.party_max = 16;
    cc.invite_every_s = 0.01;
    cc.per_ip = 64;
    auto coord = std::make_unique<server::Server>(cc);
    coord->log = [](const std::string&) {};
    CHECK(coord->start(why));
    auto wc = cc;
    wc.worker = true;
    wc.scale_mode = "off";
    wc.join = "127.0.0.1:" + std::to_string(coord->link_port());
    wc.port = 0;
    auto wk = std::make_unique<server::Server>(wc);
    wk->log = [](const std::string&) {};
    CHECK(wk->start(why));
    std::vector<Bot*> none;
    CHECK(run(coord.get(), none, 1, nullptr));
    for (int i = 0; i < 200 && wk->worker_index() <= 0; ++i) {
        coord->step(1);
        wk->step(1);
    }
    std::vector<Bot*> all;
    // the party sails in square 24's west half, then the leader and everyone cross into the east half (worker 1)
    auto v = make_party(*coord, {wk.get()}, 16, coord->port(), all, -9000);
    std::string r = coord->admin("split 25 " + std::to_string(wk->worker_index()));
    CHECK(r.find("split into") == 0);
    auto go = [&](uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            coord->step(1);
            wk->step(0);
            for (Bot* b : all) b->poll(0);
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    go(1000, nullptr);
    v[0]->head.x = 9000;  // the leader into the east half: the party goes with it
    CHECK(go(8000, [&] {
        return std::all_of(v.begin(), v.end(), [&](auto& b) {
            Session* t = wk->find_by_key(b->c.key_hash());
            return t && t->online && !t->redirected;
        });
    }));
    CHECK(go(3000, [&] {
        auto* e = v[5]->last(ClientEvent::kPartyState);
        return e && e->party.members.size() == 16 && e->party.leader == v[0]->c.my_id() &&
               std::all_of(e->party.members.begin(), e->party.members.end(), [](auto& m) { return m.online; });
    }));
    std::printf("party of 16: carried across the handover together, leader kept, 16 of 16 online\n");
}

// PvP at sea (C28): the config, the rules and state after joining, what is passed on and what is dropped, knockouts
// with the rupee loss and the repeat-knockout cooldown, hiding in the band, the party's friendly fire
static void test_pvp() {
    {
        server::Config c;
        std::string why;
        CHECK(server::parse_config_text("[pvp]\nsea = \"on\"\nzones = [\"10\", \"12:r=20000\", \"-16\", \"24:x=500,z=-200,r=900\"]\n", c, why));
        CHECK(c.pvp.mode == PvpMode::kOn);
        auto has = [&](int sq) { return std::any_of(c.pvp.zones.begin(), c.pvp.zones.end(), [&](auto& z) { return z.square == sq; }); };
        CHECK(has(10) && has(12) && !has(16) && has(24) && has(0));
        server::Config r;
        CHECK(server::parse_config_text("[pvp]\nzones_replace = true\nzones = [\"5\"]\n", r, why) && r.pvp.zones.size() == 1);
        CHECK(!server::parse_config_text("[pvp]\nko_rupee_percent = 60\n", r, why) && why.find("between 0 and 50") != std::string::npos);
        CHECK(!server::parse_config_text("[pvp]\nsea = \"maybe\"\n", r, why));
    }
    auto cfg = base_config(0);
    std::string why;
    // square 24 is centred on 0,0: a safe zone at 20000,0 with radius 3000 (its band reaches 11000)
    CHECK(server::parse_config_text("[pvp]\nzones = [\"24:x=20000,r=3000\"]\nprotect_s = 1\nenter_delay_s = 1\nmax_hit_dist = 5000\n", cfg, why));
    server::Server s(cfg);
    s.log = [](const std::string&) {};
    CHECK(s.start(why));
    auto a = make_bot("a", s.port()), b = make_bot("b", s.port()), c = make_bot("c", s.port());
    std::vector<Bot*> all = {a.get(), b.get(), c.get()};
    a->head.x = -1000;
    b->head.x = -500;
    c->head.x = -40000;
    CHECK(run(&s, all, 5000, [&] { return a->online() && b->online() && c->online(); }));
    CHECK(run(&s, all, 3000, [&] { return b->saw(ClientEvent::kPvpRules) && b->saw(ClientEvent::kPvpState); }));
    run(&s, all, 1500);  // the protection after "leaving" the start (1 s here) runs out
    auto state = [&](Bot& x) { auto* e = x.last(ClientEvent::kPvpState); return e ? e->value : 0; };
    CHECK(state(*b) & kPvpActive);
    // a sees that b can fight (PvP marks)
    CHECK(run(&s, all, 2000, [&] {
        for (auto& e : a->log)
            if (e.kind == ClientEvent::kPvpMarks)
                for (auto& [id, f] : e.pvp_marks)
                    if (id == b->c.my_id() && (f & 1)) return true;
        return false;
    }));
    auto hit = [&](Bot& from, Bot& to, float dist_ok = 0) {
        PvpHit h;
        h.other = to.c.my_id();
        h.kind = kPvpSword;
        h.damage = 4;
        h.x = to.head.x + dist_ok;
        h.z = to.head.z;
        from.c.pvp_hit(h);
    };
    auto hits_on = [&](Bot& x) {
        int n = 0;
        for (auto& e : x.log)
            if (e.kind == ClientEvent::kPvpHitOn) ++n;
        return n;
    };
    // a valid hit
    hit(*a, *b);
    CHECK(run(&s, all, 2000, [&] { return hits_on(*b) == 1; }));
    // the victim's toggle off: dropped
    b->c.pvp_flag(false, false);
    run(&s, all, 300);
    hit(*a, *b);
    run(&s, all, 500);
    CHECK(hits_on(*b) == 1);
    b->c.pvp_flag(true, false);
    run(&s, all, 300);
    // a hit point not at the victim: dropped; too far: dropped
    hit(*a, *b, 2000);
    run(&s, all, 500);
    CHECK(hits_on(*b) == 1);
    // a block ends it
    b->c.block(a->c.key_hash(), true);
    run(&s, all, 300);
    hit(*a, *b);
    run(&s, all, 500);
    CHECK(hits_on(*b) == 1);
    b->c.block(a->c.key_hash(), false);
    run(&s, all, 300);
    // knockouts: 10% of the wallet (500 -> 50), the respawn at the zone, the cooldown after 2
    auto rupees = [&](Bot& x) {
        int sum = 0;
        for (auto& e : x.log)
            if (e.kind == ClientEvent::kPvpRupees) sum += e.value16;
        return sum;
    };
    for (int k = 0; k < 3; ++k) {
        run(&s, all, 1300);  // the victim's protection after a knockout (1 s here)
        int before = hits_on(*b);
        hit(*a, *b);
        bool landed = run(&s, all, 1500, [&] { return hits_on(*b) > before; });
        if (k < 2) CHECK(landed);
        else CHECK(!landed);  // the cooldown: a's hits on b are dropped
        if (landed) {
            b->c.pvp_ko(a->c.my_id(), 500);
            CHECK(run(&s, all, 2000, [&] { return rupees(*b) == 50 * (k + 1); }));
        }
    }
    CHECK(b->saw(ClientEvent::kPvpRespawn) && a->saw(ClientEvent::kPvpKoOf));
    if (auto* e = b->last(ClientEvent::kPvpRespawn)) CHECK(e->value == 24 && std::fabs(e->pvp_hit.x - 17500) < 600);
    // in the band outside the zone: b is off c's chart (c is a stranger in the same square)
    b->head.x = 14000;  // 6000 from the zone's centre: in the band
    run(&s, all, 6000);
    c->log.clear();
    run(&s, all, 6000);
    bool b_marked = false;
    for (auto& e : c->log)
        if (e.kind == ClientEvent::kMarkers)
            for (auto& m : e.markers)
                if (m.id == b->c.my_id()) b_marked = true;
    CHECK(!b_marked);
    // into the safe zone mid-fight: safe only after the entering delay (1 s here)
    b->head.x = -500;
    run(&s, all, 1500);
    c->head.x = 16500;  // outside the zone (3,500 from its centre); c (a and b are in the cooldown)
    b->head.x = 16900;
    run(&s, all, 1500);
    hit(*c, *b);  // a fight starts
    run(&s, all, 300);
    b->head.x = 20000;  // b flees into the zone
    CHECK(run(&s, all, 2500, [&] { return (state(*b) & kPvpEntering) != 0; }));  // (a 3,100-unit jump: held 1 s as implausible first)
    int fled = hits_on(*b);
    hit(*c, *b);
    CHECK(run(&s, all, 800, [&] { return hits_on(*b) > fled; }));  // still hittable while entering
    CHECK(run(&s, all, 2500, [&] { return (state(*b) & kPvpInSafeZone) != 0; }));
    fled = hits_on(*b);
    hit(*c, *b);
    run(&s, all, 600);
    CHECK(hits_on(*b) == fled);  // safe now
    c->head.x = -40000;
    // the party: hits only with friendly fire, then even with PvP off
    b->head.x = -500;
    uint64_t code = 0;
    parse_friend_code(b->c.friend_code(), code);
    a->c.party_invite(code);
    CHECK(run(&s, all, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
    b->c.party_accept(b->last(ClientEvent::kPartyInvited)->party_id);
    CHECK(run(&s, all, 3000, [&] { return b->last(ClientEvent::kPartyState) && b->last(ClientEvent::kPartyState)->party.members.size() == 2; }));
    run(&s, all, 1500);
    b->c.pvp_flag(false, false);
    run(&s, all, 300);
    int before = hits_on(*b);
    hit(*a, *b);
    run(&s, all, 800);
    CHECK(hits_on(*b) == before);  // no friendly fire
    a->c.party_settings(uint16_t(kPartyDefaults | kPartyFriendlyFire));
    run(&s, all, 500);
    hit(*a, *b);
    CHECK(run(&s, all, 1500, [&] { return hits_on(*b) == before + 1; }));
    std::printf("pvp: hits %llu, knockouts %llu, rupees %llu, dropped: off %llu safe %llu protected %llu distance %llu blocked %llu party %llu cooldown %llu\n",
                (unsigned long long)s.metrics().pvp_hits, (unsigned long long)s.metrics().pvp_kos,
                (unsigned long long)s.metrics().pvp_rupees, (unsigned long long)s.metrics().pvp_drops[0],
                (unsigned long long)s.metrics().pvp_drops[1], (unsigned long long)s.metrics().pvp_drops[2],
                (unsigned long long)s.metrics().pvp_drops[3], (unsigned long long)s.metrics().pvp_drops[5],
                (unsigned long long)s.metrics().pvp_drops[6], (unsigned long long)s.metrics().pvp_drops[7]);
}

// the measured island safe zones (the defaults): server.example.toml lists the same zones as the code; a player at
// each island's landing point is safe, one just outside the ring is not
static void test_pvp_islands() {
    auto same = [](const std::vector<PvpZone>& a, const std::vector<PvpZone>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].square != b[i].square || a[i].cx != b[i].cx || a[i].cz != b[i].cz || a[i].radius != b[i].radius)
                return false;
        return true;
    };
    std::string why;
    {
        server::Config c;
        CHECK(server::load_config(WWHD_SERVER_EXAMPLE, c, why));
        CHECK(same(c.pvp.zones, proto::default_pvp_zones()));
        CHECK(c.pvp.zones.size() == 9);
        // an entry with only a radius keeps the measured centre; a square without a default is centred
        server::Config r;
        CHECK(server::parse_config_text("[pvp]\nzones = [\"10:r=20000\", \"20\"]\n", r, why));
        for (auto& z : r.pvp.zones) {
            if (z.square == 10) CHECK(z.cx == -300 && z.cz == -100 && z.radius == 20000);
            if (z.square == 20) CHECK(z.cx == 0 && z.cz == 0 && z.radius == 15000);
        }
        // an array over several lines, with comments
        server::Config m;
        CHECK(server::parse_config_text("[pvp]\nzones_replace = true\nzones = [   # the list\n  \"5\",   # one\n  \"6:r=100\",\n]\nsea = \"on\"\n", m, why));
        CHECK(m.pvp.zones.size() == 2 && m.pvp.mode == PvpMode::kOn);
        CHECK(!server::parse_config_text("[pvp]\nzones = [\n  \"5\",\n", m, why) && why.find("not closed") != std::string::npos);
    }
    // the landing points (offsets from the square's centre), measured from the rooms' start points
    struct Dock {
        uint8_t square;
        float x, z;
    };
    const Dock docks[] = {{10, -1721, 5657}, {43, -1585, 31275}, {12, -4933, 3611}, {40, 17845, -7386}, {16, -8919, 22353},
                          {0, -1000, 10000}, {25, 0, 14000},     {44, 19900, 23000}, {3, 20000, -17100}};
    auto cfg = base_config(0);
    CHECK(cfg.pvp.zones.size() == 9);
    server::Server s(cfg);
    s.log = [](const std::string&) {};
    CHECK(s.start(why));
    std::vector<std::unique_ptr<Bot>> in, out;
    std::vector<Bot*> all;
    for (auto& d : docks) {
        float sx = cfg.sea_origin_x + proto::kSquareUnits * (d.square % 7 + 0.5f);
        float sz = cfg.sea_origin_z + proto::kSquareUnits * (d.square / 7 + 0.5f);
        const PvpZone* z = nullptr;
        for (auto& k : cfg.pvp.zones)
            if (k.square == d.square) z = &k;
        CHECK(z != nullptr);
        auto a = make_bot("dock" + std::to_string(d.square), s.port());
        a->head.x = sx + d.x;
        a->head.z = sz + d.z;
        auto b = make_bot("ring" + std::to_string(d.square), s.port());
        b->head.x = sx + z->cx + z->radius + 500;  // just outside the ring
        b->head.z = sz + z->cz;
        all.push_back(a.get());
        all.push_back(b.get());
        in.push_back(std::move(a));
        out.push_back(std::move(b));
    }
    CHECK(run(&s, all, 8000, [&] { return std::all_of(all.begin(), all.end(), [](Bot* b) { return b->online(); }); }));
    run(&s, all, 2000);
    auto state = [&](Bot& x) { auto* e = x.last(ClientEvent::kPvpState); return e ? e->value : 0; };
    int safe = 0, open = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        bool a = (state(*in[i]) & kPvpInSafeZone) != 0, b = (state(*out[i]) & kPvpInSafeZone) == 0;
        if (!a) std::printf("  square %d: the landing point is not safe\n", docks[i].square);
        if (!b) std::printf("  square %d: just outside the ring is safe\n", docks[i].square);
        CHECK(a && b);
        safe += a;
        open += b;
    }
    std::printf("pvp islands: example config = defaults (9 zones), %d of 9 landing points safe, %d of 9 just outside open\n",
                safe, open);
}

// PvP state across a worker border: protection keeps its remaining time, the repeat-knockout cooldown and the
// toggle survive, a fight goes on with the two players on different workers, and a knockout right after a crossing
// still counts (the carry's encoding: a unit check first)
static void test_pvp_handover() {
    {
        server::PvpCarry c;
        c.valid = true;
        c.on = false;
        c.in_zone = false;
        c.zone_age = 1234;
        c.protect_left = 2500;
        c.fighting = true;
        c.fight_age = 700;
        c.last_hit_by = 77;
        c.last_hit_age = 650;
        c.pairs.push_back({0x1122334455667788ull, 40000, {9000, 3000}});
        Bytes b;
        Writer w(b);
        server::put_pvp_carry(w, c);
        Reader r(b);
        server::PvpCarry d;
        CHECK(server::get_pvp_carry(r, d) && r.done() && d.valid);
        CHECK(!d.on && !d.busy && !d.in_zone && d.zone_age == 1234 && d.protect_left == 2500 && d.fighting &&
              d.fight_age == 700 && d.last_hit_by == 77 && d.last_hit_age == 650);
        CHECK(d.pairs.size() == 1 && d.pairs[0].attacker == 0x1122334455667788ull && d.pairs[0].blocked_left == 40000 &&
              d.pairs[0].ko_ages == std::vector<uint32_t>({9000, 3000}));
        Bytes cut(b.begin(), b.end() - 3);
        Reader rc(cut);
        CHECK(!server::get_pvp_carry(rc, d));
    }
    std::string why;
    auto cc = base_config(0);
    cc.scale_mode = "pool";
    cc.link_port = 0;
    cc.link_secret = "test-link-secret-0123456789";
    cc.split_cpu = 1e9;
    cc.split_bw = 1e18;
    cc.per_ip = 64;
    cc.handover_depth = 200;  // a short crossing (the hits need the players near each other)
    cc.pvp.protect_ms = 3000;
    cc.pvp.enter_delay_ms = 1000;
    cc.pvp.max_hit_dist = 5000;
    cc.pvp.cooldown_kos = 2;
    cc.pvp.cooldown_s = 120;
    auto coord = std::make_unique<server::Server>(cc);
    coord->log = [](const std::string&) {};
    CHECK(coord->start(why));
    if (!why.empty()) std::printf("  pvp handover: coordinator: %s\n", why.c_str());
    auto wc = cc;
    wc.worker = true;
    wc.scale_mode = "off";
    wc.join = "127.0.0.1:" + std::to_string(coord->link_port());
    wc.port = 0;
    auto wk = std::make_unique<server::Server>(wc);
    wk->log = [](const std::string&) {};
    CHECK(wk->start(why));
    for (uint32_t end = monotonic_ms() + 5000; wk->worker_index() <= 0 && int32_t(monotonic_ms() - end) < 0;) {
        coord->step(1);
        wk->step(1);
    }
    CHECK(wk->worker_index() > 0);
    // square 24 (open sea, centred on 0,0): its east half goes to the worker
    CHECK(coord->admin("split 25 " + std::to_string(wk->worker_index())).find("split into") == 0);
    auto a = make_bot("a", coord->port()), b = make_bot("b", coord->port()), c = make_bot("c", coord->port()),
         d = make_bot("d", coord->port());
    std::vector<Bot*> all = {a.get(), b.get(), c.get(), d.get()};
    a->head.x = -1500;
    b->head.x = -800;
    c->head.x = -1200;
    c->head.z = 600;
    d->head.x = -1000;
    d->head.z = -900;
    auto go = [&](uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            coord->step(1);
            wk->step(0);
            for (Bot* x : all) x->poll(0);
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    auto on = [&](server::Server& srv, Bot& x) {
        Session* t = srv.find_by_key(x.c.key_hash());
        return t && t->online && !t->redirected ? t : nullptr;
    };
    CHECK(go(8000, [&] { return std::all_of(all.begin(), all.end(), [&](Bot* x) { return on(*coord, *x); }); }));
    go(3500, nullptr);  // the protection after "leaving" the start
    auto hits_on = [&](Bot& x) {
        int n = 0;
        for (auto& e : x.log)
            if (e.kind == ClientEvent::kPvpHitOn) ++n;
        return n;
    };
    auto hit = [&](Bot& from, Bot& to) {
        PvpHit h;
        h.other = to.c.my_id();
        h.kind = kPvpSword;
        h.damage = 4;
        h.x = to.head.x;
        h.z = to.head.z;
        from.c.pvp_hit(h);
    };
    auto rupees = [&](Bot& x) {
        int sum = 0;
        for (auto& e : x.log)
            if (e.kind == ClientEvent::kPvpRupees) sum += e.value16;
        return sum;
    };
    // two knockouts of b by a: the cooldown; b protected for 3 s after the second
    uint32_t ko_at = 0;
    for (int k = 0; k < 2; ++k) {
        if (k) go(3300, nullptr);
        int before = hits_on(*b);
        hit(*a, *b);
        CHECK(go(2000, [&] { return hits_on(*b) > before; }));
        b->c.pvp_ko(a->c.my_id(), 500);
        CHECK(go(2000, [&] { return rupees(*b) == 50 * (k + 1); }));
        ko_at = monotonic_ms();
    }
    // b (protected) and d (PvP toggled off) cross into the worker's half
    d->c.pvp_flag(false, false);
    go(300, nullptr);
    b->head.x = 800;
    d->head.x = 900;
    CHECK(go(8000, [&] { return on(*wk, *b) && on(*wk, *d); }));
    uint32_t crossed = monotonic_ms() - ko_at;
    Session* wb = on(*wk, *b);
    CHECK(wb && int32_t(wb->protected_until - monotonic_ms()) > 0);
    // what is left of the 3 s, not a new protection
    CHECK(wb && int32_t(wb->protected_until - monotonic_ms()) <= int32_t(3000 - crossed) + 300);
    CHECK(on(*wk, *d) && !on(*wk, *d)->pvp_on);
    // protected for the rest of the 3 s only
    CHECK(go(5000, [&] {
        Session* t = on(*wk, *b);
        return t && int32_t(t->protected_until - monotonic_ms()) <= 0;
    }));
    uint32_t prot = monotonic_ms() - ko_at;
    CHECK(prot >= 2900 && prot < 3300);
    go(200, nullptr);
    // a (on the coordinator) is still in its cooldown on b (on the worker)
    uint64_t cool0 = wk->metrics().pvp_drops[7];
    int h0 = hits_on(*b);
    hit(*a, *b);
    go(800, nullptr);
    CHECK(hits_on(*b) == h0 && wk->metrics().pvp_drops[7] > cool0);
    // the fight goes on across the border: c (coordinator) hits b (worker); b crosses back and is knocked out by c at
    // once: it counts there (who hit last came along)
    hit(*c, *b);
    CHECK(go(2000, [&] { return hits_on(*b) == h0 + 1; }));
    b->head.x = -700;
    CHECK(go(8000, [&] { return on(*coord, *b) != nullptr; }));
    Session* cb = on(*coord, *b);
    CHECK(cb && cb->last_hit_by == c->c.my_id() && cb->fight_at != 0);
    go(300, nullptr);
    cb = on(*coord, *b);
    CHECK(cb && int32_t(cb->protected_until - monotonic_ms()) <= 0);  // no new protection from the crossing
    int r0 = rupees(*b);
    b->c.pvp_ko(c->c.my_id(), 400);
    CHECK(go(2000, [&] { return rupees(*b) == r0 + 40; }));
    // and a's cooldown came back with b
    go(3300, nullptr);
    uint64_t cool1 = coord->metrics().pvp_drops[7];
    int h1 = hits_on(*b);
    hit(*a, *b);
    go(800, nullptr);
    CHECK(hits_on(*b) == h1 && coord->metrics().pvp_drops[7] > cool1);
    // hits during the crossing itself (c hits b five times a second while b sails over the border at 2,000 a
    // second): none is lost, whichever worker has b when the hit arrives
    go(3300, nullptr);  // b's protection after the knockout
    int sent = 0, h2 = hits_on(*b);
    uint64_t fwd_drops = wk->metrics().pvp_drops[8] + coord->metrics().pvp_drops[8];
    {
        uint32_t t0 = monotonic_ms(), next_hit = t0;
        go(2600, [&] {
            uint32_t t = monotonic_ms() - t0;
            b->head.x = std::min(-700.0f + 2.0f * float(t), 900.0f);
            if (int32_t(monotonic_ms() - next_hit) >= 0 && t < 2000) {
                next_hit += 200;
                hit(*c, *b);
                ++sent;
            }
            return false;
        });
    }
    CHECK(go(2000, [&] { return hits_on(*b) - h2 == sent; }));
    CHECK(on(*wk, *b) != nullptr);
    int landed = hits_on(*b) - h2;
    uint64_t lost = wk->metrics().pvp_drops[8] + coord->metrics().pvp_drops[8] - fwd_drops;
    std::printf("pvp handover: %d of %d hits landed while the victim crossed (%llu unknown)\n", landed, sent,
                (unsigned long long)lost);
    std::printf("pvp handover: protection crossed %u ms after the knockout and ended %u ms after it (3,000), cooldown kept "
                "both ways, toggle kept, a cross-border hit landed, a knockout right after crossing back counted\n",
                crossed, prot);
}

// many players hitting each other as fast as they can: the hit rate holds, hits pass only where allowed, nothing
// breaks (a bot test of the PvP path)
static void test_pvp_stress() {
    auto cfg = base_config(0);
    cfg.per_ip = 64;
    std::string why;
    CHECK(server::parse_config_text("[pvp]\nzones_replace = true\nprotect_s = 0\nmax_hit_dist = 5000\n", cfg, why));
    server::Server s(cfg);
    s.log = [](const std::string&) {};
    CHECK(s.start(why));
    std::vector<std::unique_ptr<Bot>> v;
    std::vector<Bot*> all;
    for (int i = 0; i < 16; ++i) {
        v.push_back(make_bot("f" + std::to_string(i), s.port()));
        v.back()->head.x = float(i % 4) * 400.0f;
        v.back()->head.z = float(i / 4) * 400.0f;
        all.push_back(v.back().get());
    }
    CHECK(run(&s, all, 8000, [&] { return std::all_of(v.begin(), v.end(), [](auto& b) { return b->online(); }); }));
    run(&s, all, 500);
    // 3 s at 30 hits a second each, at random others; the server allows 8 a second each
    std::mt19937 rng(5);
    uint32_t end = monotonic_ms() + 3000;
    int sent = 0;
    while (int32_t(monotonic_ms() - end) < 0) {
        for (auto& b : v) {
            Bot& t = *v[rng() % v.size()];
            PvpHit h;
            h.other = t.c.my_id();
            h.kind = uint8_t(1 + rng() % 5);
            h.damage = uint8_t(1 + rng() % 8);
            h.x = t.head.x;
            h.z = t.head.z;
            b->c.pvp_hit(h);
            ++sent;
        }
        run(&s, all, 33);
    }
    run(&s, all, 1500);
    uint64_t ok = s.metrics().pvp_hits, rate = s.metrics().pvp_drops[4], self = s.metrics().pvp_drops[10];
    std::printf("pvp stress: %d hits sent, %llu passed, %llu over the rate, %llu at oneself\n", sent, (unsigned long long)ok,
                (unsigned long long)rate, (unsigned long long)self);
    CHECK(ok > 0 && rate > 0 && ok <= uint64_t(16 * (8 * 4.5 + 16)));
    CHECK(std::all_of(v.begin(), v.end(), [](auto& b) { return b->online(); }));
}

// text chat: the party channel and the global one, and every check the server makes (on, global allowed, length,
// rate, the word filter, the operator's mute, blocks, a player's channel turned off), with the reason told
static void test_chat() {
    auto lines = [](const Bot& b, uint8_t channel) {
        std::vector<ChatLine> v;
        for (auto& e : b.log)
            if (e.kind == ClientEvent::kChatFrom && e.chat.channel == channel) v.push_back(e.chat);
        return v;
    };
    auto results = [](const Bot& b) {
        std::vector<uint8_t> v;
        for (auto& e : b.log)
            if (e.kind == ClientEvent::kChatResult) v.push_back(e.value);
        return v;
    };
    std::vector<std::string> logged;
    const std::string filter = g_dir + "/chat-filter.txt", mutes = g_dir + "/chat-mutes.txt";
    std::remove(mutes.c_str());
    if (std::FILE* f = std::fopen(filter.c_str(), "w")) std::fputs("darn\nfoo*\n", f), std::fclose(f);
    {
        // global off (the default), a filter, 5 lines per 10 s
        server::Config cfg = base_config(0);
        cfg.chat_filter_file = filter;
        cfg.chat_mutes_file = mutes;
        server::Server s(cfg);
        s.log = [&](const std::string& t) { logged.push_back(t); };
        std::string why;
        CHECK(s.start(why));
        uint16_t port = s.port();
        auto a = make_bot("alpha", port), b = make_bot("beta", port), c = make_bot("gamma", port);
        std::vector<Bot*> all = {a.get(), b.get(), c.get()};
        CHECK(run(&s, all, 5000, [&] { return a->online() && b->online() && c->online() && c->saw(ClientEvent::kChatRules); }));
        if (auto* e = c->last(ClientEvent::kChatRules))
            CHECK(e->chat_rules.enabled && !e->chat_rules.global && e->chat_rules.max_length == 120 && e->chat_rules.rate_count == 5);
        // no party yet: a party line is refused
        a->c.send_chat(kChatParty, "anyone?");
        CHECK(run(&s, all, 2000, [&] { return !results(*a).empty(); }));
        CHECK(results(*a) == std::vector<uint8_t>{kChatNoParty});
        uint64_t code = 0;
        parse_friend_code(b->c.friend_code(), code);
        a->c.party_invite(code);
        CHECK(run(&s, all, 3000, [&] { return b->saw(ClientEvent::kPartyInvited); }));
        b->c.party_accept(b->last(ClientEvent::kPartyInvited)->party_id);
        CHECK(run(&s, all, 3000, [&] { auto* e = a->last(ClientEvent::kPartyState); return e && e->party.members.size() == 2; }));
        // a party line, cleaned and filtered: to b (named, a party member) and back to a (yours); never to c
        a->c.send_chat(kChatParty, "  Follow\x07 me,\tdarn  it!  ");
        CHECK(run(&s, all, 2000, [&] { return lines(*b, kChatParty).size() == 1 && lines(*a, kChatParty).size() == 1; }));
        if (!lines(*b, kChatParty).empty()) {
            const ChatLine l = lines(*b, kChatParty)[0];
            CHECK(l.text == "Follow me, *** it!" && l.name == "alpha" && (l.flags & kChatFromParty) && !(l.flags & kChatYours) &&
                  l.from == a->c.my_id() && l.key_hash == a->c.key_hash());
            CHECK((lines(*a, kChatParty)[0].flags & kChatYours) && lines(*a, kChatParty)[0].text == l.text);
        }
        b->c.send_chat(kChatParty, "Thanks!");
        CHECK(run(&s, all, 2000, [&] { return lines(*a, kChatParty).size() == 2; }));
        // global is off here: refused, nobody gets it
        a->c.send_chat(kChatGlobal, "hello world");
        CHECK(run(&s, all, 2000, [&] { return results(*a).size() == 2; }));
        CHECK(results(*a).back() == kChatNoGlobal && lines(*c, kChatGlobal).empty() && lines(*b, kChatGlobal).empty());
        // too long (121 characters; multi-byte ones count once)
        std::string longer;
        for (int i = 0; i < 121; i++) longer += "\xc3\xa9";
        a->c.send_chat(kChatParty, longer);
        CHECK(run(&s, all, 2000, [&] { return results(*a).size() == 3; }));
        CHECK(results(*a).back() == kChatTooLong);
        longer.resize(240);  // 120: fine
        a->c.send_chat(kChatParty, longer);
        CHECK(run(&s, all, 2000, [&] { return lines(*b, kChatParty).size() == 2; }));
        // the rate: 5 in 10 s; a has sent 2 lines that counted, so 3 more pass and the 4th is refused
        for (int i = 0; i < 4; i++) a->c.send_chat(kChatParty, "spam " + std::to_string(i));
        CHECK(run(&s, all, 2000, [&] { return results(*a).size() == 4; }));
        CHECK(results(*a).back() == kChatTooFast && lines(*b, kChatParty).size() == 6);  // (b's own "Thanks!" too)
        // the operator mutes a: refused; c sees nothing anyway; unmuted later
        CHECK(s.admin("chat-mute " + a->c.friend_code() + " 30m spamming").find("chat muted") == 0);
        CHECK(s.admin("chat-mutes").find(a->c.friend_code()) != std::string::npos);
        b->c.send_chat(kChatParty, "and you?");
        CHECK(run(&s, all, 2000, [&] { return lines(*a, kChatParty).size() == 7; }));  // (a's 5 own + b's 2)
        CHECK(s.admin("chat-unmute " + a->c.friend_code()).find("chat unmuted") == 0);
        CHECK(s.admin("chat-mute " + a->c.friend_code() + " perm").find("until unmuted") != std::string::npos);
        a->c.send_chat(kChatParty, "muted?");
        CHECK(run(&s, all, 2000, [&] {
            auto r = results(*a);
            return std::find(r.begin(), r.end(), uint8_t(kChatMuted)) != r.end();
        }));
        // kept in the file, for a restart
        std::FILE* mf = std::fopen(mutes.c_str(), "r");
        CHECK(mf != nullptr);
        if (mf) std::fclose(mf);
        CHECK(s.admin("chat-unmute " + a->c.friend_code()).find("chat unmuted") == 0);
        CHECK(s.admin("chat-unmute " + a->c.friend_code()).find("no chat mute") == 0);
        // the reasons in the log
        auto in_log = [&](const char* what) {
            for (auto& l : logged)
                if (l.find("chat: dropped") == 0 && l.find(what) != std::string::npos) return true;
            return false;
        };
        if (std::getenv("CHAT_DEBUG"))
            for (auto& l : logged) std::printf("  log: %s\n", l.c_str());
        CHECK(in_log("not in a party") && in_log("no global chat") && in_log("too long (121 > 120 characters)") &&
              in_log("too many messages: wait a moment (5 lines per 10 s)") && in_log("operator muted your chat"));
        CHECK(s.metrics().chat_filtered >= 1 && s.metrics().chat_drops[kChatTooFast] >= 1);
        std::printf("chat: party lines %zu at the member, drops %llu too long, %llu too fast, %llu muted: ok\n",
                    lines(*b, kChatParty).size(), (unsigned long long)s.metrics().chat_drops[kChatTooLong],
                    (unsigned long long)s.metrics().chat_drops[kChatTooFast], (unsigned long long)s.metrics().chat_drops[kChatMuted]);
    }
    {
        // global on: everyone on the server (strangers named as strangers), not who turned it off, not across a block
        server::Config cfg = base_config(0);
        cfg.chat_global = true;
        cfg.chat_mutes_file = mutes;
        server::Server s(cfg);
        s.log = [](const std::string&) {};
        std::string why;
        CHECK(s.start(why));
        CHECK(s.admin("chat-mutes").find("no chat mutes") == 0);  // (the earlier mutes were lifted and saved)
        uint16_t port = s.port();
        auto a = make_bot("alpha", port), b = make_bot("beta", port), c = make_bot("gamma", port);
        auto d = make_bot("delta", port, 0x2222);  // another game: another world, never reached
        std::vector<Bot*> all = {a.get(), b.get(), c.get(), d.get()};
        CHECK(run(&s, all, 5000, [&] { return a->online() && b->online() && c->online() && c->saw(ClientEvent::kChatRules); }));
        if (auto* e = c->last(ClientEvent::kChatRules)) CHECK(e->chat_rules.global);
        a->c.send_chat(kChatGlobal, "Over here");
        CHECK(run(&s, all, 2000, [&] { return lines(*b, kChatGlobal).size() == 1 && lines(*c, kChatGlobal).size() == 1; }));
        if (!lines(*c, kChatGlobal).empty()) {
            CHECK(lines(*c, kChatGlobal)[0].name.rfind("Sailor ", 0) == 0 && !(lines(*c, kChatGlobal)[0].flags & kChatFromParty));
            CHECK(lines(*c, kChatGlobal)[0].text == "Over here");
        }
        // c turns global off; b blocks a
        c->c.chat_prefs(1 << kChatGlobal);
        b->c.block(a->c.key_hash(), true);
        run(&s, all, 300);
        a->c.send_chat(kChatGlobal, "Let's go");
        CHECK(run(&s, all, 2000, [&] { return lines(*a, kChatGlobal).size() == 2; }));
        run(&s, all, 300);
        CHECK(lines(*b, kChatGlobal).size() == 1 && lines(*c, kChatGlobal).size() == 1 && lines(*d, kChatGlobal).empty());
        std::printf("chat: global lines reach strangers, not a channel turned off, a block or another game: ok\n");
    }
    {
        server::Config cfg = base_config(0);
        cfg.chat_enabled = false;
        cfg.chat_mutes_file = "";
        server::Server s(cfg);
        s.log = [](const std::string&) {};
        std::string why;
        CHECK(s.start(why));
        auto a = make_bot("alpha", s.port()), b = make_bot("beta", s.port());
        CHECK(run(&s, {a.get(), b.get()}, 5000, [&] { return a->online() && b->online() && a->saw(ClientEvent::kChatRules); }));
        if (auto* e = a->last(ClientEvent::kChatRules)) CHECK(!e->chat_rules.enabled && !e->chat_rules.global);
        a->c.send_chat(kChatGlobal, "hello?");
        CHECK(run(&s, {a.get(), b.get()}, 2000, [&] { return !results(*a).empty(); }));
        CHECK(results(*a) == std::vector<uint8_t>{kChatOff} && lines(*b, kChatGlobal).empty());
        std::printf("chat: off on the server: refused: ok\n");
    }
    std::remove(filter.c_str());
    std::remove(mutes.c_str());
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    g_dir = argc > 1 ? argv[1] : ".";
    std::string only = argc > 2 ? argv[2] : "";
    if (only.empty() || only == "presence") test_presence_and_stage();
    if (only.empty() || only == "ban") test_refusals_and_ban();
    if (only.empty() || only == "restart") test_restart_and_resume();
    if (only.empty() || only == "party") test_party();
    if (only.empty() || only == "copies") test_copies();
    if (only.empty() || only == "flood") test_flood();
    if (only.empty() || only == "upload") test_upload_limit();
    if (only.empty() || only == "partyrestart") test_party_restart();
    if (only.empty() || only == "tcp") test_tcp();
    if (only.empty() || only == "mods") test_mod_rules_server();
    if (only.empty() || only == "block") test_block_mute();
    if (only.empty() || only == "chat") test_chat();
    if (only.empty() || only == "party16") test_party_16();
    if (only.empty() || only == "party16ho") test_party_16_handover();
    if (only.empty() || only == "pvp") test_pvp();
    if (only.empty() || only == "pvpstress") test_pvp_stress();
    if (only.empty() || only == "pvpislands") test_pvp_islands();
    if (only.empty() || only == "pvphandover") test_pvp_handover();
    if (only.empty() || only == "seacopies") test_sea_copies();
    if (only.empty() || only == "scaling") test_scaling();
    if (only.empty() || only == "scalingmass") test_scaling_mass();
    if (only.empty() || only == "chaos") {
        test_chaos("good_wifi", 0.01, 0.0, 5, 10);
        test_chaos("mobile_tether", 0.05, 0.01, 40, 60);
        test_chaos("bad_wifi", 0.15, 0.03, 30, 120);
    }
    if (g_fail) {
        std::printf("server_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("server_test: all passed\n");
    return 0;
}
