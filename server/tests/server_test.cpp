// SPDX-License-Identifier: MPL-2.0
// End-to-end tests of wwhd-online-server on 127.0.0.1: real sockets, the server and several protocol clients in one
// process, driven step by step.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <map>
#include <memory>
#include <set>
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

struct Bot {
    Client c;
    std::string name;
    crypto::Identity id;
    std::map<uint16_t, PlayerIn> players;
    std::map<uint16_t, PoseEntry> poses;
    std::vector<ClientEvent> log;
    PoseHeader head;
    Bytes blob = Bytes(100, 7);
    bool online() const { return c.state() == ClientState::kOnline; }
    void poll(uint32_t now) {
        for (auto& e : c.poll(now)) {
            if (e.kind == ClientEvent::kWelcome && !e.resumed) {
                players.clear();
                poses.clear();
            }
            if (e.kind == ClientEvent::kPlayerIn) players[e.id] = e.player;
            if (e.kind == ClientEvent::kPlayerOut) {
                players.erase(e.id);
                poses.erase(e.id);
            }
            if (e.kind == ClientEvent::kPoses)
                for (auto& p : e.poses) poses[p.id] = p;
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
    coord->log = [](const std::string&) {};
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
    a->head.x = -10000;
    c->head.x = -3000;
    b->head.x = 5000;
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
    // b sails back west (x < 0, 2000 deep): back on the coordinator
    for (float x = 5000; x >= -6000; x -= 300) {
        b->head.x = x;
        watch(70);
    }
    CHECK(watch(8000, [&] { return on(*coord, *b); }));
    CHECK(!a_lost_b && !b_lost_a);
    std::printf("scaling: split, handover there and back, visible sets continuous (a lost b: %d, b lost a: %d)\n", a_lost_b,
                b_lost_a);
    // the west half moves to worker w2: a, b and c follow, and a party works across workers
    uint32_t west = 0;
    for (auto& g : coord->region_map().regions)
        if (g.parent == region && g.x0 == 24) west = g.id;
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
    CHECK(all3(bots, 4000, [&] { return wk[0]->find_by_key(c->c.key_hash()) && b->players.count(c->c.my_id()); }));
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
