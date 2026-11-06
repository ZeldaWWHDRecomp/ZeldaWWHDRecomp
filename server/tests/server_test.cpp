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
    return c;
}

static std::unique_ptr<Bot> make_bot(const std::string& name, uint16_t port, uint64_t game = 0x1111,
                                     const crypto::Identity* id = nullptr) {
    auto b = std::make_unique<Bot>();
    b->name = name;
    ClientConfig cfg;
    cfg.port = port;
    cfg.game_data = game;
    cfg.identity = id ? *id : crypto::Identity::generate();
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
    std::printf("flood kick: ok\n");
}

// two shards in one process: rows 0-3 of the sea on shard 0, rows 4-6 on shard 1 (the border at z = 50,000)
static void test_shards() {
    std::string why;
    std::unique_ptr<server::Server> sh[2];
    for (int i = 0; i < 2; ++i) {
        auto c = base_config(uint16_t(47811 + i));
        c.key_file = g_dir + "/shard" + std::to_string(i) + ".key";
        c.shard_index = i;
        c.shard_count = 2;
        c.shard_peers = {"127.0.0.1:47811", "127.0.0.1:47812"};
        c.shard_links = {"127.0.0.1:47821", "127.0.0.1:47822"};
        c.link_secret = "test-link-secret-0123456789";
        sh[i] = std::make_unique<server::Server>(c);
        sh[i]->log = [](const std::string&) {};
        CHECK(sh[i]->start(why));
    }
    auto both = [&](std::vector<Bot*> bots, uint32_t ms, std::function<bool()> done) {
        uint32_t end = monotonic_ms() + ms;
        while (int32_t(monotonic_ms() - end) < 0) {
            if (sh[0]) sh[0]->step(1);
            if (sh[1]) sh[1]->step(0);
            for (Bot* b : bots) b->poll(monotonic_ms());
            if (done && done()) return true;
        }
        return done ? done() : true;
    };
    auto a = make_bot("a", 47811), b = make_bot("b", 47812), c = make_bot("c", 47811);
    std::vector<Bot*> all = {a.get(), b.get(), c.get()};
    a->head.z = 45000;   // shard 0, near the border
    c->head.z = 41000;   // shard 0
    b->head.z = 55000;   // shard 1, near the border
    CHECK(both(all, 5000, [&] { return a->online() && b->online() && c->online(); }));
    // across the border: a and b see each other through the shards' ghosts
    CHECK(both(all, 4000, [&] { return a->players.count(b->c.my_id()) && b->players.count(a->c.my_id()); }));
    uint16_t aid = a->c.my_id();
    // a sails into shard 1's square: handed over, same id, and c (on shard 0) never loses sight of it
    bool c_lost = false, b_lost = false;
    for (float z = 45000; z <= 58000; z += 400) {
        a->head.z = z;
        both(all, 70, nullptr);
        if (!c->players.count(aid)) c_lost = true;
        if (!b->players.count(aid)) b_lost = true;
    }
    CHECK(both(all, 4000, [&] { return a->online() && a->c.my_id() == aid && sh[1]->find_by_key(a->c.key_hash()); }));
    for (int i = 0; i < 20; ++i) {
        both(all, 70, nullptr);
        if (!c->players.count(aid)) c_lost = true;
        if (!b->players.count(aid)) b_lost = true;
    }
    CHECK(!c_lost);
    CHECK(!b_lost);
    CHECK(a->players.count(b->c.my_id()) && a->players.count(c->c.my_id()));
    CHECK(sh[0]->metrics().handovers_out == 1 && sh[1]->metrics().handovers_in == 1);
    std::printf("shards: handover kept id %u, visible sets continuous (c lost a: %d, b lost a: %d)\n", aid, c_lost, b_lost);
    // shard 1 goes down: its players reconnect (retrying), shard 0's players are unaffected
    sh[1].reset();
    both({a.get(), c.get()}, 6500, nullptr);
    CHECK(c->online());
    CHECK(!a->online());
    std::printf("shards: ok\n");
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

int main(int argc, char** argv) {
    g_dir = argc > 1 ? argv[1] : ".";
    std::string only = argc > 2 ? argv[2] : "";
    if (only.empty() || only == "presence") test_presence_and_stage();
    if (only.empty() || only == "ban") test_refusals_and_ban();
    if (only.empty() || only == "restart") test_restart_and_resume();
    if (only.empty() || only == "party") test_party();
    if (only.empty() || only == "copies") test_copies();
    if (only.empty() || only == "flood") test_flood();
    if (only.empty() || only == "shards") test_shards();
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
