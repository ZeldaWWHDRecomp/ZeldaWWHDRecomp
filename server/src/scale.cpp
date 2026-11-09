// SPDX-License-Identifier: MPL-2.0
// Automatic scaling per sea square (docs/online-server.md, "Scaling").
//
// The world is a region map: rectangles of sea sub-cells (at first the 49 squares) and interior stages, each run by
// one worker process. The coordinator (worker 0, the server clients connect to first) measures every worker's CPU
// and egress each second. A worker over the thresholds sheds load: a busy region moves to a less loaded worker, a
// region too hot on its own is split in two at its players' median, and when no worker has room the coordinator
// starts another worker process on this machine (or uses one that registered from another machine). Halves merge
// back and idle workers stop when it calms down. When even the smallest region is over its cap, new arrivals go
// into a copy of that area (see area_copies in server.cpp's sea placement).
//
// Workers see across region borders through subscriptions: each second every worker tells every other one which
// sub-cells its players look into, and each tick gets the poses of the other's players in those cells ("ghosts").
// The overlap band is therefore exactly the viewers' interest window. A player who is handover_depth inside a region
// another worker runs is handed over: the new worker gets a ticket over the link, the client a REDIRECT, and the
// player keeps their id. Party members stay with the worker that hosts their party (the leader's), as guests in
// other regions; the party moves with its leader.
//
// The links between workers are proto::Channel sessions over UDP (loopback or LAN), keyed from the shared secret
// and each process's random epoch; the handful of plain link packets (hello, register) carry an HMAC tag.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "server.h"

#ifndef _WIN32
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace online::server {

using namespace online::proto;

namespace {
enum LinkPacket : uint8_t { kLinkHello = 0xB1, kLinkRegister = 0xB2, kLinkRegistered = 0xB3 };
constexpr size_t kLinkTag = 16;
constexpr uint32_t kPeerSilentMs = 5000;
constexpr int kBlock = 256;

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void link_key(const std::string& secret, uint8_t out[32]) {
    std::string s = "wwhd-online link " + secret;
    sha256(s.data(), s.size(), out);
}

void put_ghost(Writer& w, const Ghost& g) {
    w.u16(g.id);
    w.u16(g.copy);
    w.u64(g.key_hash);
    w.u64(g.world);
    w.u32(g.look);
    w.u32(g.pose_ms);
    w.u32(g.pose_seq);
    put_pose_header(w, g.head);
    w.u16(uint16_t(g.blob.size()));
    w.bytes(g.blob.data(), g.blob.size());
    w.u8(uint8_t(g.look_record.size()));
    w.bytes(g.look_record.data(), g.look_record.size());
}

bool get_ghost(Reader& r, Ghost& g) {
    g.id = r.u16();
    g.copy = r.u16();
    g.key_hash = r.u64();
    g.world = r.u64();
    g.look = r.u32();
    g.pose_ms = r.u32();
    g.pose_seq = r.u32();
    g.head = get_pose_header(r);
    size_t n = r.u16();
    if (n > kMaxPoseBlob) return false;
    g.blob.resize(n);
    r.bytes(g.blob.data(), n);
    const size_t ln = r.u8();
    if (ln > kMaxLook) return false;
    g.look_record.resize(ln);
    r.bytes(g.look_record.data(), ln);
    return r.ok();
}

Ghost ghost_of(const Session& s) {
    Ghost g;
    g.id = s.id;
    g.copy = s.copy;
    g.key_hash = s.key_hash;
    g.world = s.world;
    g.look = s.look;
    g.look_record = s.look_record;
    g.name = s.name;
    g.head = s.head;
    g.blob = s.blob;
    g.pose_ms = s.pose_ms;
    g.pose_seq = s.pose_seq;
    return g;
}

bool split_host_port(const std::string& hp, std::string& host, uint16_t& port) {
    size_t c = hp.rfind(':');
    if (c == std::string::npos) return false;
    host = hp.substr(0, c);
    port = uint16_t(std::atoi(hp.c_str() + c + 1));
    return port != 0;
}

void tag_of(const uint8_t key[32], const uint8_t* d, size_t n, uint8_t out[kLinkTag]) {
    uint8_t t[32];
    hmac_sha256(key, 32, d, n, t);
    std::memcpy(out, t, kLinkTag);
}
}  // namespace

// ---------------------------------------------------------------- the region map

void RegionMap::init_squares() {
    regions.clear();
    for (int z = 0; z < kSeaSquares; ++z)
        for (int x = 0; x < kSeaSquares; ++x) {
            Region r;
            r.id = next_region++;
            r.x0 = int16_t(x * kSubCells);
            r.z0 = int16_t(z * kSubCells);
            r.x1 = int16_t(r.x0 + kSubCells);
            r.z1 = int16_t(r.z0 + kSubCells);
            r.worker = 0;
            regions.push_back(r);
        }
    rebuild();
}

void RegionMap::rebuild() {
    cell_.fill(-1);
    for (size_t i = 0; i < regions.size(); ++i) {
        const Region& r = regions[i];
        if (!r.stage.empty()) continue;
        for (int z = std::max<int>(0, r.z0); z < std::min<int>(kGrid, r.z1); ++z)
            for (int x = std::max<int>(0, r.x0); x < std::min<int>(kGrid, r.x1); ++x) cell_[size_t(z * kGrid + x)] = int16_t(i);
    }
}

int RegionMap::region_at(int gx, int gz) const {
    gx = clampi(gx, 0, kGrid - 1);
    gz = clampi(gz, 0, kGrid - 1);
    return cell_[size_t(gz * kGrid + gx)];
}

int RegionMap::owner_at(int gx, int gz, int copy) const {
    int r = region_at(gx, gz);
    return r < 0 ? 0 : regions[size_t(r)].worker_of_copy(copy);
}

int RegionMap::owner_of_stage(const std::string& stage) const {
    for (auto& r : regions)
        if (r.stage == stage) return r.worker;
    return -1;
}

const WorkerInfo* RegionMap::worker(int index) const {
    for (auto& w : workers)
        if (w.index == index) return &w;
    return nullptr;
}

Bytes RegionMap::encode() const {
    Bytes b;
    Writer w(b);
    w.u32(version);
    w.u32(next_region);
    w.u8(uint8_t(workers.size()));
    for (auto& k : workers) {
        w.u8(uint8_t(k.index));
        w.str8(k.host);
        w.u16(k.port);
        w.str8(k.link_host);
        w.u16(k.link_port);
        w.u8(uint8_t(std::min(k.cores, 255)));
        w.u8(k.alive ? 1 : 0);
    }
    w.u16(uint16_t(regions.size()));
    for (auto& r : regions) {
        w.u32(r.id);
        w.u32(r.parent);
        w.s16(r.x0);
        w.s16(r.z0);
        w.s16(r.x1);
        w.s16(r.z1);
        w.str8(r.stage);
        w.u8(uint8_t(r.worker | (r.pinned ? 0x80 : 0)));
        w.u8(uint8_t(std::min<size_t>(r.copy_worker.size(), 64)));
        for (size_t k = 0; k < r.copy_worker.size() && k < 64; ++k) w.u8(r.copy_worker[k]);
    }
    return b;
}

bool RegionMap::decode(const uint8_t* d, size_t n) {
    Reader r(d, n);
    RegionMap m;
    m.version = r.u32();
    m.next_region = r.u32();
    size_t nw = r.u8();
    for (size_t i = 0; i < nw && r.ok(); ++i) {
        WorkerInfo k;
        k.index = r.u8();
        k.host = r.str8(64);
        k.port = r.u16();
        k.link_host = r.str8(64);
        k.link_port = r.u16();
        k.cores = r.u8();
        k.alive = r.u8() != 0;
        m.workers.push_back(k);
    }
    size_t nr = r.u16();
    for (size_t i = 0; i < nr && r.ok(); ++i) {
        Region g;
        g.id = r.u32();
        g.parent = r.u32();
        g.x0 = r.s16();
        g.z0 = r.s16();
        g.x1 = r.s16();
        g.z1 = r.s16();
        g.stage = r.str8(8);
        g.worker = r.u8();
        g.pinned = (g.worker & 0x80) != 0;
        g.worker &= 0x7F;
        size_t nc = r.u8();
        for (size_t k = 0; k < nc; ++k) g.copy_worker.push_back(r.u8());
        m.regions.push_back(g);
    }
    if (!r.done()) return false;
    *this = std::move(m);
    rebuild();
    return true;
}

Bytes encode_load(const LoadReport& l) {
    Bytes b;
    Writer w(b);
    w.f32(l.cpu_pct);
    w.u32(l.tick_p90_us);
    w.u64(l.out_Bps);
    w.u16(l.players);
    w.u32(l.map_version);
    w.u16(uint16_t(l.regions.size()));
    for (auto& r : l.regions) {
        w.u32(r.region);
        w.u16(r.players);
        w.u64(r.out_Bps);
        w.u16(uint16_t(r.hist.size()));
        for (uint16_t h : r.hist) w.u16(h);
        w.u8(uint8_t(std::min<size_t>(r.copies.size(), 64)));
        for (size_t k = 0; k < r.copies.size() && k < 64; ++k) w.u16(r.copies[k]);
    }
    return b;
}

bool decode_load(const uint8_t* d, size_t n, LoadReport& l) {
    Reader r(d, n);
    l = LoadReport();
    l.cpu_pct = r.f32();
    l.tick_p90_us = r.u32();
    l.out_Bps = r.u64();
    l.players = r.u16();
    l.map_version = r.u32();
    size_t c = r.u16();
    for (size_t i = 0; i < c && r.ok(); ++i) {
        RegionLoad g;
        g.region = r.u32();
        g.players = r.u16();
        g.out_Bps = r.u64();
        size_t h = r.u16();
        if (h > size_t(kGrid * kGrid)) return false;
        for (size_t k = 0; k < h; ++k) g.hist.push_back(r.u16());
        size_t nc = r.u8();
        for (size_t k = 0; k < nc; ++k) g.copies.push_back(r.u16());
        l.regions.push_back(std::move(g));
    }
    return r.done();
}

// ---------------------------------------------------------------- links

bool Server::open_link(std::string& why) {
    if (cfg_.link_secret.empty())
        if (const char* e = std::getenv("WWHD_ONLINE_LINK_SECRET")) cfg_.link_secret = e;
    if (cfg_.link_secret.size() < 16) {
        why = "scaling needs scale.secret (16+ characters) or WWHD_ONLINE_LINK_SECRET";
        return false;
    }
    crypto::random_bytes(&epoch_, 4);
    if (!epoch_) epoch_ = 1;
    uint16_t lport = cfg_.link_port;
    if (!link_.open(cfg_.link_bind, lport, why)) return false;
    link_.set_buffers(16 << 20);
    std::string host = cfg_.public_host.empty() ? cfg_.bind : cfg_.public_host;
    if (cfg_.worker) {
        std::string h;
        uint16_t p = 0;
        if (!split_host_port(cfg_.join, h, p) || !resolve(h, p, coord_addr_, why)) {
            why = "bad --join address " + cfg_.join;
            return false;
        }
        me_ = -1;  // until registered
    } else {
        me_ = 0;
        map_.workers.clear();
        WorkerInfo w0;
        w0.index = 0;
        w0.host = host;
        w0.port = sock_.local_port();
        w0.link_host = cfg_.link_bind;
        w0.link_port = link_.local_port();
        w0.cores = int(std::thread::hardware_concurrency());
        w0.alive = true;
        map_.workers.push_back(w0);
        map_.init_squares();
        map_.version = 1;
        peers_.resize(1);
        for (int b = 1; b <= 4; ++b) id_blocks_.push_back(uint16_t(next_block_++ * kBlock));
        registered_ = true;
    }
    return true;
}

void Server::link_send(int worker, uint8_t type, const Bytes& body, bool reliable) {
    if (worker < 0 || size_t(worker) >= peers_.size() || worker == me_) return;
    Peer& p = peers_[size_t(worker)];
    if (!p.up) return;
    if (reliable)
        p.chan.send_reliable(type, body);
    else
        p.chan.send_unreliable(type, body);
}

static void setup_peer_channel(Peer& p, const uint8_t key[32], int me, int other, uint32_t my_epoch, uint32_t now) {
    int lo = std::min(me, other), hi = std::max(me, other);
    uint32_t e_lo = me == lo ? my_epoch : p.epoch, e_hi = me == lo ? p.epoch : my_epoch;
    Bytes seed;
    Writer w(seed);
    w.u8(uint8_t(lo));
    w.u8(uint8_t(hi));
    w.u32(e_lo);
    w.u32(e_hi);
    uint8_t conn_h[32], k_lohi[32], k_hilo[32];
    Bytes a = seed;
    a.push_back('c');
    hmac_sha256(key, 32, a.data(), a.size(), conn_h);
    a.back() = 'u';
    hmac_sha256(key, 32, a.data(), a.size(), k_lohi);
    a.back() = 'd';
    hmac_sha256(key, 32, a.data(), a.size(), k_hilo);
    uint32_t conn = uint32_t(conn_h[0]) | (uint32_t(conn_h[1]) << 8) | (uint32_t(conn_h[2]) << 16) | (uint32_t(conn_h[3]) << 24);
    bool lo_side = me == lo;
    p.chan.setup(conn ? conn : 1, lo_side ? k_lohi : k_hilo, lo_side ? k_hilo : k_lohi, lo_side, now);
    p.chan.set_queue_cap(64u << 20, 1u << 20);  // mass handovers queue many messages at once
    p.up = true;
    p.heard = now;
}

void Server::link_datagram(const uint8_t* d, size_t n, const Address& from, uint32_t now) {
    uint8_t key[32];
    link_key(cfg_.link_secret, key);
    if (n < 1) return;
    if (d[0] == kPktData) {
        PacketHeader h;
        if (!read_header(d, n, h)) return;
        for (size_t i = 0; i < peers_.size(); ++i) {
            Peer& p = peers_[i];
            if (!p.up || p.chan.conn_id() != h.conn_id) continue;
            std::vector<Delivered> got;
            if (!p.chan.receive(d, n, now, got)) return;
            p.heard = now;
            p.addr = from;
            for (auto& m : got) link_message(int(i), m, now);
            return;
        }
        return;
    }
    if (n < 1 + kLinkTag) return;
    uint8_t t[kLinkTag];
    tag_of(key, d, n - kLinkTag, t);
    if (std::memcmp(t, d + n - kLinkTag, kLinkTag) != 0) return;
    Reader r(d + 1, n - 1 - kLinkTag);
    if (d[0] == kLinkHello) {
        int idx = r.u8();
        uint32_t ep = r.u32();
        if (!r.done() || idx == me_ || idx >= 16 || me_ < 0) return;
        if (size_t(idx) >= peers_.size()) peers_.resize(size_t(idx) + 1);
        Peer& p = peers_[size_t(idx)];
        p.addr = from;
        if (p.epoch != ep || !p.up) {
            p.epoch = ep;
            setup_peer_channel(p, key, me_, idx, epoch_, now);
            if (me_ == 0) {  // a (re)started worker gets the map at once
                p.chan.send_reliable(kLMap, map_.encode());
            }
            if (!my_sub_.empty()) p.chan.send_reliable(kLSub, my_sub_);  // and what we look into
        }
        p.heard = now;
    } else if (d[0] == kLinkRegister && me_ == 0) {
        uint32_t ep = r.u32();
        WorkerInfo w;
        w.host = r.str8(64);
        w.port = r.u16();
        w.link_host = r.str8(64);
        w.link_port = r.u16();
        w.cores = r.u16();
        if (!r.done()) return;
        // the same worker again (its register was answered but the answer lost): the same index
        int idx = -1;
        for (auto& k : map_.workers)
            if (k.link_host == w.link_host && k.link_port == w.link_port && k.port == w.port) idx = k.index;
        if (idx < 0) {
            for (int i = 1; i < cfg_.max_workers && idx < 0; ++i)
                if (!map_.worker(i) || !map_.worker(i)->alive) idx = i;
            if (idx < 0) return;  // full
            w.index = idx;
            w.alive = true;
            map_.workers.erase(std::remove_if(map_.workers.begin(), map_.workers.end(), [&](auto& k) { return k.index == idx; }),
                               map_.workers.end());
            map_.workers.push_back(w);
            if (size_t(idx) >= peers_.size()) peers_.resize(size_t(idx) + 1);
            peers_[size_t(idx)].epoch = 0;
            peers_[size_t(idx)].up = false;
            peers_[size_t(idx)].idle_since = now;
            scale_log_.push_back(std::to_string(now) + " worker " + std::to_string(idx) + " registered (" + w.host + ":" +
                                 std::to_string(w.port) + ")");
            map_changed();
        }
        (void)ep;
        Bytes b;
        Writer o(b);
        o.u8(kLinkRegistered);
        o.u8(uint8_t(idx));
        o.u32(epoch_);
        uint8_t tg[kLinkTag];
        tag_of(key, b.data(), b.size(), tg);
        o.bytes(tg, kLinkTag);
        link_.send_to(from, b.data(), b.size());
    } else if (d[0] == kLinkRegistered && cfg_.worker && !registered_) {
        int idx = r.u8();
        uint32_t ep = r.u32();
        if (!r.done()) return;
        me_ = idx;
        registered_ = true;
        if (peers_.size() < 1) peers_.resize(1);
        peers_[0].addr = from;
        peers_[0].epoch = ep;
        setup_peer_channel(peers_[0], key, me_, 0, epoch_, now);
        log("worker " + std::to_string(me_) + " registered with the coordinator");
        send_subscription(now, true);
    }
}

void Server::link_poll(uint32_t now) {
    uint8_t buf[2048];
    Address from;
    for (int i = 0; i < 20000; ++i) {
        int n = link_.recv_from(from, buf, sizeof buf);
        if (n < 0) break;
        link_datagram(buf, size_t(n), from, now);
    }
}

void Server::map_changed() {
    map_.version++;
    map_.rebuild();
    Bytes b = map_.encode();
    for (size_t i = 1; i < peers_.size(); ++i)
        if (peers_[i].up) peers_[i].chan.send_reliable(kLMap, b);
    apply_map(monotonic_ms());
}

void Server::apply_map(uint32_t now) {
    // everyone hosted here who is now in another worker's region moves
    std::vector<Session*> all;
    for (auto& [cid, sp] : sessions_)
        if (!sp->dead && sp->online && !sp->redirected) all.push_back(sp.get());
    for (Session* s : all) handover_check(*s, now);
}

int Server::owner_of(const Session& s) const {
    std::string stage = s.where.stage_name();
    if (stage.empty()) return me_;
    if (stage == cfg_.sea_stage) {
        if (!s.has_pose) return me_;
        return map_.owner_at(s.gx, s.gz, s.copy);
    }
    int o = map_.owner_of_stage(stage);
    return o < 0 ? 0 : o;
}

void Server::handover_check(Session& s, uint32_t now) {
    // (WWHD_ONLINE_DEBUG_HANDOVER=1: every 5 s, why players who should move did not)
    static const bool debug = std::getenv("WWHD_ONLINE_DEBUG_HANDOVER") != nullptr;
    static uint32_t why[8] = {}, last = 0;
    if (debug && now - last > 5000) {
        last = now;
        log("handover debug (worker " + std::to_string(me_) + "): handing " + std::to_string(why[0]) + ", cap " +
            std::to_string(why[1]) + ", target down " + std::to_string(why[2]) + ", not deep " + std::to_string(why[3]) +
            ", started " + std::to_string(why[4]) + ", pending " + std::to_string(pending_.size()));
        std::fill(std::begin(why), std::end(why), 0);
    }
    if (!linked() || !registered_ || s.redirected || !s.online || s.dead) return;
    int target = owner_of(s);
    if (target == me_) return;
    if (s.handing) return void(++why[0]);
    if (handovers_this_tick_ >= 150) return void(++why[1]);  // a mass move is spread over ticks (on_pose asks again)
    const WorkerInfo* w = map_.worker(target);
    if (!w || !w->alive || size_t(target) >= peers_.size() || !peers_[size_t(target)].up) return void(++why[2]);
    ++why[4];
    if (s.where.stage_name() == cfg_.sea_stage) {
        // deep enough inside: the region a little way out on every side is the target's too
        float d = cfg_.handover_depth;
        auto cell = [&](float x, float z, int& gx, int& gz) {
            gx = int(std::floor((x - cfg_.sea_origin_x) / kSubCellUnits));
            gz = int(std::floor((z - cfg_.sea_origin_z) / kSubCellUnits));
        };
        int gx, gz;
        for (int k = 0; k < 4; ++k) {
            float x = s.head.x + (k == 0 ? d : k == 1 ? -d : 0), z = s.head.z + (k == 2 ? d : k == 3 ? -d : 0);
            cell(x, z, gx, gz);
            if (map_.owner_at(gx, gz, s.copy) != target) {
                --why[4];
                return void(++why[3]);
            }
        }
    }
    // the party stays together, on the worker of its host member (the leader, or the first member online here)
    Party* p = party_of(s);
    if (p) {
        Session* host = online_member(p->leader);
        if (!host || host->redirected)
            for (uint64_t kh : p->members)
                if (Session* t = online_member(kh)) {
                    host = t;
                    break;
                }
        if (host && host != &s) return;  // a guest here: it follows its host
        Bytes blob = encode_party(*p);
        hand_over(s, target, now, &blob, 0);
        for (uint64_t kh : p->members)
            if (Session* t = online_member(kh); t && t != &s) hand_over(*t, target, now, nullptr, 0);
        uint32_t pid = p->id;
        for (uint64_t kh : p->members) party_by_key_.erase(kh);
        parties_.erase(pid);
        return;
    }
    hand_over(s, target, now, nullptr, 0);
}

void Server::hand_over(Session& s, int target, uint32_t now, const Bytes* party_blob, uint32_t join_party) {
    const WorkerInfo* w = map_.worker(target);
    if (!w || s.handing) return;
    uint8_t ticket[32];
    crypto::random_bytes(ticket, 32);
    Bytes body;
    Writer wr(body);
    wr.bytes(ticket, 32);
    wr.bytes(s.pub, 32);
    wr.u8(s.device);
    wr.u8(uint8_t(s.near_cap));
    wr.u8(uint8_t(s.mid_cap));
    // whose id it is: ours, or the worker that lent it to us
    int owner = s.lent_from >= 0 ? s.lent_from : me_;
    wr.u8(uint8_t(owner));
    wr.u32(s.party ? s.party : join_party);
    wr.u8(join_party ? 1 : 0);
    put_ghost(wr, ghost_of(s));
    wr.u32(party_blob ? uint32_t(party_blob->size()) : 0);
    if (party_blob) wr.bytes(party_blob->data(), party_blob->size());
    // the account's blocks and mutes, and its gameplay mods
    auto put_set = [&](const std::unordered_set<uint64_t>& v) {
        wr.u16(uint16_t(std::min<size_t>(v.size(), 4096)));
        size_t k = 0;
        for (uint64_t x : v)
            if (k++ < 4096) wr.u64(x);
    };
    put_set(s.blocks);
    put_set(s.mutes);
    put_mod_refs(wr, s.mods);
    put_pvp_carry(wr, pvp_carry_of(s, now));  // updated once more at the redirect (kLPvpCarry)
    link_send(target, kLHandoff, body);
    if (owner == me_) lent_.insert(s.id);
    // the client is sent over once the target has the ticket (kLHandoffAck); it plays on here until then
    std::array<uint8_t, 32> t{};
    std::memcpy(t.data(), ticket, 32);
    pending_[t] = {s.conn_id, target, now};
    s.handing = true;
    s.party = 0;  // the party moved (or stays) on its own
    ++handovers_this_tick_;
}

void Server::redirect(Session& s, int target, const uint8_t ticket[32], uint32_t now) {
    const WorkerInfo* w = map_.worker(target);
    if (!w) return;
    Bytes r;
    Writer rw(r);
    rw.str8(w->host);
    rw.u16(w->port);
    rw.bytes(ticket, 32);
    s.chan.send_reliable(kMsgRedirect, r);
    s.redirected = true;
    s.redirect_to = target;
    // the PvP state as it is now (it played on here while the target stored the ticket)
    Bytes c;
    Writer cw(c);
    cw.bytes(ticket, 32);
    cw.u64(s.key_hash);
    put_pvp_carry(cw, pvp_carry_of(s, now));
    link_send(target, kLPvpCarry, c);
    s.redirect_at = now;
    metrics_.handovers_out++;
    static const bool debug = std::getenv("WWHD_ONLINE_DEBUG_HANDOVER") != nullptr;
    if (debug)
        log(std::to_string(now) + " handover: player " + std::to_string(s.id) + " at " + std::to_string(int(s.head.x)) + " " +
            std::to_string(int(s.head.z)) + " from worker " + std::to_string(me_) + " to worker " + std::to_string(target));
}

// ---------------------------------------------------------------- parties over the link

Bytes Server::encode_party(const Party& p) const {
    Bytes b;
    Writer w(b);
    w.u32(p.id);
    w.u64(p.world);
    w.u64(p.leader);
    w.u16(p.flags);
    w.u8(uint8_t(p.members.size()));
    for (uint64_t kh : p.members) {
        w.u64(kh);
        auto n = p.names.find(kh);
        w.str8(n == p.names.end() ? std::string() : n->second);
    }
    w.u32(p.team.version);
    put_team_fields(w, p.team.all());
    w.u8(p.mods_set ? 1 : 0);
    put_mod_refs(w, p.mods);
    return b;
}

bool Server::decode_party(const uint8_t* d, size_t n, Party& p) const {
    Reader r(d, n);
    p.id = r.u32();
    p.world = r.u64();
    p.leader = r.u64();
    p.flags = r.u16();
    size_t m = r.u8();
    if (m > size_t(kMaxPartySize)) return false;
    for (size_t i = 0; i < m; ++i) {
        uint64_t kh = r.u64();
        p.members.push_back(kh);
        p.names[kh] = r.str8(24);
    }
    p.team.version = r.u32();
    std::vector<TeamField> f;
    if (!get_team_fields(r, f)) return false;
    if (r.left()) {
        p.mods_set = r.u8() != 0;
        if (!get_mod_refs(r, p.mods)) return false;
    }
    if (!r.done()) return false;
    for (auto& x : f) p.team.fields[x.offset] = x;
    return true;
}

// ---------------------------------------------------------------- link messages

void Server::link_message(int from, const Delivered& m, uint32_t now) {
    Reader r(m.body);
    switch (m.type) {
        case kLMap: {
            if (from != 0) break;
            RegionMap nm;
            if (!nm.decode(m.body.data(), m.body.size())) break;
            if (nm.version <= map_.version && map_.version) break;
            map_ = std::move(nm);
            for (auto& w : map_.workers) {
                if (w.index == me_) continue;
                if (size_t(w.index) >= peers_.size()) peers_.resize(size_t(w.index) + 1);
                std::string why;
                Address a;
                if (resolve(w.link_host, w.link_port, a, why)) peers_[size_t(w.index)].addr = a;
                if (!w.alive) peers_[size_t(w.index)].up = peers_[size_t(w.index)].up && w.index == 0;
            }
            apply_map(now);
            break;
        }
        case kLLoad:
            if (me_ == 0 && size_t(from) < peers_.size()) {
                decode_load(m.body.data(), m.body.size(), peers_[size_t(from)].load);
                peers_[size_t(from)].load_at = now;
            }
            break;
        case kLSub:
            if (size_t(from) < peers_.size() && m.body.size() == size_t(kGrid * kGrid / 8)) peers_[size_t(from)].sub = m.body;
            break;
        case kLGhosts:
            while (r.ok() && r.left()) {
                Ghost g;
                if (!get_ghost(r, g)) break;
                g.from = from;
                auto local = by_id_.find(g.id);
                if (local != by_id_.end()) {
                    auto s = sessions_.find(local->second);
                    if (s == sessions_.end()) continue;
                    if (!s->second->redirected) continue;  // ours: a stale ghost from before a handover
                    remove(local->second);  // handed over and now drawn from the new worker
                }
                g.at = now;
                if (g.head.where.stage_name() != cfg_.sea_stage) continue;
                g.gx = int32_t(std::floor((g.head.x - cfg_.sea_origin_x) / kSubCellUnits));
                g.gz = int32_t(std::floor((g.head.z - cfg_.sea_origin_z) / kSubCellUnits));
                ghosts_[g.id] = std::move(g);
                metrics_.ghosts++;
            }
            break;
        case kLHandoff: {
            std::array<uint8_t, 32> t{};
            Ticket tk;
            r.bytes(t.data(), 32);
            r.bytes(tk.pub, 32);
            tk.device = r.u8();
            tk.near_cap = r.u8();
            tk.mid_cap = r.u8();
            tk.from = r.u8();
            tk.party = r.u32();
            tk.join = r.u8() != 0;
            if (!get_ghost(r, tk.who)) break;
            size_t pn = r.u32();
            if (pn > r.left()) break;
            if (pn) {
                Party p;
                if (decode_party(r.here(), pn, p)) {
                    p.empty_since = 0;
                    for (uint64_t kh : p.members) party_by_key_[kh] = p.id;
                    parties_[p.id] = std::move(p);
                }
                r.skip(pn);
            }
            for (auto* v : {&tk.blocks, &tk.mutes}) {
                size_t n = r.u16();
                for (size_t k = 0; k < n && k < 4096 && r.ok(); ++k) v->push_back(r.u64());
            }
            if (r.left()) get_mod_refs(r, tk.mods);
            if (r.left()) get_pvp_carry(r, tk.pvp);
            tk.at = now;
            tickets_[t] = std::move(tk);
            link_send(from, kLHandoffAck, Bytes(t.begin(), t.end()));
            break;
        }
        case kLHandoffAck: {
            if (m.body.size() != 32) break;
            std::array<uint8_t, 32> t{};
            std::memcpy(t.data(), m.body.data(), 32);
            auto it = pending_.find(t);
            if (it == pending_.end()) break;
            auto s = sessions_.find(it->second.conn_id);
            if (s != sessions_.end() && !s->second->dead && s->second->online)
                redirect(*s->second, it->second.target, t.data(), now);
            pending_.erase(it);
            break;
        }
        case kLIdAsk:
            for (int k = 0; k < 4 && me_ == 0 && next_block_ < 255; ++k) {
                Bytes b;
                Writer(b).u16(uint16_t(next_block_++ * kBlock));
                link_send(from, kLIdBlock, b);
            }
            break;
        case kLIdBlock: {
            uint16_t first = r.u16();
            if (r.done()) id_blocks_.push_back(first);
            id_asked_ = false;
            break;
        }
        case kLIdFree: {
            uint16_t id = r.u16();
            if (r.done()) lent_.erase(id);
            break;
        }
        case kLStop:
            if (from == 0) {
                log("worker " + std::to_string(me_) + ": stopped by the coordinator");
                stop_ = true;
            }
            break;
        case kLInvite: {
            uint32_t pid = r.u32();
            uint64_t code = r.u64();
            uint16_t from_id = r.u16();
            uint64_t from_kh = r.u64();
            std::string name = r.str8(24);
            uint64_t world = r.u64();
            std::vector<ModRef> pmods;
            if (r.ok() && r.left()) get_mod_refs(r, pmods);
            if (!r.done()) break;
            for (auto& [kh, cid] : by_key_)
                if (code40_of(kh) == code)
                    if (Session* t = online_member(kh); t && t->world == world && !t->blocks.count(from_kh) &&
                                                         !lists_[kh].blocks.count(from_kh)) {
                        remote_invites_[pid] = from;
                        Bytes b;
                        Writer w(b);
                        w.u32(pid);
                        w.u16(from_id);
                        w.u64(from_kh);
                        w.str8(name);
                        put_mod_refs(w, pmods);
                        t->chan.send_reliable(kMsgPartyInvited, b);
                    }
            break;
        }
        case kLInviteAnswer: {
            uint32_t pid = r.u32();
            uint8_t code = r.u8();
            std::string text = r.str8(200);
            auto p = parties_.find(pid);
            if (p != parties_.end() && r.done())
                if (Session* l = online_member(p->second.leader)) party_result(*l, code, text);
            break;
        }
        case kLPartyPark: {
            Party p;
            if (me_ != 0 || !decode_party(m.body.data(), m.body.size(), p)) break;
            p.empty_since = now ? now : 1;
            for (uint64_t kh : p.members) party_by_key_[kh] = p.id;
            parties_[p.id] = std::move(p);
            break;
        }
        case kLPvpHit:
            pvp_link_hit(m.body, now);
            break;
        case kLPvpCarry: {
            // the PvP state at the redirect: into the ticket, or onto the player that has just arrived
            std::array<uint8_t, 32> t{};
            r.bytes(t.data(), 32);
            uint64_t kh = r.u64();
            PvpCarry c;
            if (!get_pvp_carry(r, c) || !r.done()) break;
            if (auto tk = tickets_.find(t); tk != tickets_.end()) {
                if (tk->second.who.key_hash == kh) tk->second.pvp = std::move(c);
                break;
            }
            if (auto k = by_key_.find(kh); k != by_key_.end())
                if (auto sp = sessions_.find(k->second); sp != sessions_.end()) {
                    Session& s = *sp->second;
                    // only just after its arrival, before it fought here
                    if (s.online && !s.dead && s.pvp_carried_at && now - s.pvp_carried_at < 2000 &&
                        !(s.fight_at && int32_t(s.fight_at - s.pvp_carried_at) > 0))
                        apply_pvp_carry(s, c, now);
                }
            break;
        }
        case kLBan: {
            Ban b;
            b.code40 = r.u64();
            b.until = int64_t(r.u64());
            b.reason = r.str8(200);
            if (!r.done()) break;
            bans_.push_back(b);
            for (auto& [cid, sp] : sessions_)
                if (sp->online && !sp->dead && code40_of(sp->key_hash) == b.code40)
                    kick(*sp, kNoticeBan, b.reason.empty() ? "banned by the operator" : "banned: " + b.reason);
            break;
        }
        case kLChat: chat_link(m.body); break;
        case kLChatMute: {
            const uint64_t c40 = r.u64();
            const bool on = r.u8() != 0;
            const int64_t until = int64_t(r.u64());
            const std::string why = r.str8(200);
            if (r.done()) chat_mute_set(c40, on, until, why);
            break;
        }
        case kLKick: {
            uint64_t code = r.u64();
            std::string why = r.str8(200);
            for (auto& [cid, sp] : sessions_)
                if (sp->online && !sp->dead && code40_of(sp->key_hash) == code) kick(*sp, kNoticeKick, why);
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- the link tick

// my subscription: the cells my players look into. Worked out every tick and sent (reliably) as soon as it changes,
// every 2 s anyway, and at once to a peer whose link comes up: a viewer never waits a second for the poses of
// players just across a region border (a newly started worker was missed for up to that long)
void Server::send_subscription(uint32_t now, bool force) {
    std::vector<uint8_t> sub(size_t(kGrid * kGrid / 8), 0);
    int reach = int(std::ceil(cfg_.window_half * (1.0f + cfg_.hysteresis) / kSubCellUnits));
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead || !s.online || s.redirected || !s.has_pose || s.head.where.stage_name() != cfg_.sea_stage) continue;
        for (int dz = -reach; dz <= reach; ++dz)
            for (int dx = -reach; dx <= reach; ++dx) {
                int x = s.gx + dx, z = s.gz + dz;
                if (x < 0 || z < 0 || x >= kGrid || z >= kGrid) continue;
                size_t bit = size_t(z * kGrid + x);
                sub[bit / 8] |= uint8_t(1u << (bit % 8));
            }
    }
    bool changed = sub != my_sub_;
    if (!changed && !force && now - last_sub_sent_ < 2000) return;
    my_sub_ = std::move(sub);
    last_sub_sent_ = now;
    for (size_t i = 0; i < peers_.size(); ++i)
        if (int(i) != me_ && peers_[i].up) peers_[i].chan.send_reliable(kLSub, my_sub_);
}

void Server::link_tick(uint32_t now) {
    uint8_t key[32];
    link_key(cfg_.link_secret, key);
    for (auto it = tickets_.begin(); it != tickets_.end();) it = now - it->second.at > 30000 ? tickets_.erase(it) : std::next(it);
    handovers_this_tick_ = 0;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.at < 5000) {
            ++it;
            continue;
        }
        // no answer: the player stays here and is checked again later
        auto s = sessions_.find(it->second.conn_id);
        if (s != sessions_.end()) s->second->handing = false;
        it = pending_.erase(it);
    }

    // once a second: hellos (and registering), subscriptions, the load report
    if (now - last_sub_ >= 1000) {
        last_sub_ = now;
        if (cfg_.worker && !registered_) {
            Bytes b;
            Writer w(b);
            w.u8(kLinkRegister);
            w.u32(epoch_);
            w.str8(cfg_.public_host.empty() ? cfg_.bind : cfg_.public_host);
            w.u16(sock_.local_port());
            w.str8(cfg_.link_bind);
            w.u16(link_.local_port());
            w.u16(uint16_t(std::thread::hardware_concurrency()));
            uint8_t tg[kLinkTag];
            tag_of(key, b.data(), b.size(), tg);
            w.bytes(tg, kLinkTag);
            link_.send_to(coord_addr_, b.data(), b.size());
            return;
        }
        Bytes hello;
        Writer hw(hello);
        hw.u8(kLinkHello);
        hw.u8(uint8_t(me_));
        hw.u32(epoch_);
        uint8_t tg[kLinkTag];
        tag_of(key, hello.data(), hello.size(), tg);
        hw.bytes(tg, kLinkTag);
        if (cfg_.worker) link_.send_to(coord_addr_, hello.data(), hello.size());
        for (auto& w : map_.workers) {
            if (w.index == me_ || w.index == 0 || !w.alive) continue;
            Address a;
            std::string why;
            if (!resolve(w.link_host, w.link_port, a, why)) continue;
            link_.send_to(a, hello.data(), hello.size());
        }
        // a peer that went silent is down
        for (size_t i = 0; i < peers_.size(); ++i)
            if (int(i) != me_ && peers_[i].up && now - peers_[i].heard > kPeerSilentMs) {
                peers_[i].up = false;
                peers_[i].epoch = 0;
                peers_[i].sub.clear();
                if (me_ == 0) {
                    // its regions come back here; its players reconnect to us by themselves
                    for (auto& w : map_.workers)
                        if (w.index == int(i)) w.alive = false;
                    // its regions (and copies) go to the least loaded live worker, region by region
                    auto least = [&]() {
                        int best = 0;
                        double bl = 1e18;
                        for (auto& w : map_.workers) {
                            if (!w.alive || (w.index != 0 && (size_t(w.index) >= peers_.size() || !peers_[size_t(w.index)].up)))
                                continue;
                            const LoadReport& l = peers_[size_t(w.index)].load;
                            double L = std::max(l.cpu_pct / cfg_.split_cpu, double(l.out_Bps) / cfg_.split_bw);
                            if (L < bl) {
                                bl = L;
                                best = w.index;
                            }
                        }
                        return best;
                    };
                    std::string where;
                    for (auto& g : map_.regions) {
                        if (g.worker == int(i)) {
                            g.worker = least();
                            where += " " + std::to_string(g.id) + "->" + std::to_string(g.worker);
                            // count it on the new owner until its next report
                            peers_[size_t(g.worker)].load.out_Bps += uint64_t(cfg_.split_bw / 4);
                        }
                        for (auto& c : g.copy_worker)
                            if (c == i) c = uint8_t(g.worker);
                    }
                    peers_[i].load = LoadReport();
                    scale_log_.push_back(std::to_string(now) + " worker " + std::to_string(i) + " lost: its regions" + where);
                    map_changed();
                }
            }
        // the load report
        uint64_t cpu = 0, rss = 0;
#ifndef _WIN32
        rusage ru{};
        getrusage(RUSAGE_SELF, &ru);
        cpu = uint64_t(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000ull + uint64_t(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec);
#endif
        (void)rss;
        if (cpu_at_ms_) {
            double dt = (now - cpu_at_ms_) / 1000.0;
            if (dt > 0) {
                cpu_pct_ = float((cpu - cpu_at_) / (dt * 1e6) * 100.0);
                out_Bps_ = uint64_t(double(metrics_.bytes_out - out_at_) / dt);
            }
        }
        cpu_at_ = cpu;
        cpu_at_ms_ = now;
        out_at_ = metrics_.bytes_out;
        LoadReport l;
        l.cpu_pct = cpu_pct_;
        {
            std::vector<uint32_t> t(recent_ticks_.begin(), recent_ticks_.end());
            std::sort(t.begin(), t.end());
            l.tick_p90_us = t.empty() ? 0 : t[t.size() * 9 / 10];
        }
        l.out_Bps = out_Bps_;
        l.map_version = map_.version;
        std::unordered_map<int, RegionLoad> per;
        for (auto& [cid, sp] : sessions_) {
            Session& s = *sp;
            if (s.dead || !s.online || s.redirected) continue;
            l.players++;
            int ri = -1;
            if (s.where.stage_name() == cfg_.sea_stage && s.has_pose) ri = map_.region_at(s.gx, s.gz);
            else
                for (size_t k = 0; k < map_.regions.size(); ++k)
                    if (map_.regions[k].stage == s.where.stage_name()) ri = int(k);
            if (ri < 0) continue;
            RegionLoad& rl = per[ri];
            const Region& g = map_.regions[size_t(ri)];
            rl.region = g.id;
            rl.players++;
            if (g.stage.empty()) {
                if (rl.hist.empty()) rl.hist.assign(size_t(g.cells()), 0);
                int hx = clampi(s.gx - g.x0, 0, g.x1 - g.x0 - 1), hz = clampi(s.gz - g.z0, 0, g.z1 - g.z0 - 1);
                rl.hist[size_t(hz * (g.x1 - g.x0) + hx)]++;
                if (s.copy < 64) {
                    if (rl.copies.size() <= s.copy) rl.copies.resize(size_t(s.copy) + 1, 0);
                    rl.copies[s.copy]++;
                }
            }
        }
        for (auto& [ri, rl] : per) {
            rl.out_Bps = l.players ? out_Bps_ * rl.players / l.players : 0;
            l.regions.push_back(std::move(rl));
        }
        if (me_ == 0) {
            if (peers_.empty()) peers_.resize(1);
            peers_[0].load = l;
            peers_[0].load_at = now;
            coordinate(now);
        } else {
            link_send(0, kLLoad, encode_load(l));
        }
    }
    // ids: ask for another block before they run out
    if (me_ > 0 && !id_asked_) {
        size_t free_ids = 0;
        for (uint16_t b : id_blocks_)
            for (int k = 0; k < kBlock; ++k) {
                uint16_t id = uint16_t(b + k);
                if (id && !by_id_.count(id) && !lent_.count(id)) ++free_ids;
            }
        if (free_ids < 512) {
            link_send(0, kLIdAsk, {});
            id_asked_ = true;
        }
    } else if (me_ == 0 && next_block_ < 255) {
        size_t free_ids = 0;
        for (uint16_t b : id_blocks_)
            for (int k = 0; k < kBlock && free_ids < 512; ++k) {
                uint16_t id = uint16_t(b + k);
                if (id && !by_id_.count(id) && !lent_.count(id)) ++free_ids;
            }
        if (free_ids < 512) id_blocks_.push_back(uint16_t(next_block_++ * kBlock));
    }
    if (registered_) send_subscription(now, false);
    // ghosts: my players in the cells each peer subscribed
    std::vector<Bytes> per(peers_.size());
    auto ship = [&](size_t i) {
        if (per[i].empty()) return;
        peers_[i].chan.send_unreliable(kLGhosts, per[i]);
        per[i].clear();
    };
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        // (a player being handed over still goes out, with its last pose, until the new worker shows it)
        if (s.dead || !s.online || !s.has_pose || s.head.where.stage_name() != cfg_.sea_stage) continue;
        if (s.gx < 0 || s.gz < 0 || s.gx >= kGrid || s.gz >= kGrid) continue;
        size_t bit = size_t(s.gz * kGrid + s.gx);
        Bytes one;
        for (size_t i = 0; i < peers_.size(); ++i) {
            Peer& p = peers_[i];
            if (int(i) == me_ || !p.up || p.sub.size() != size_t(kGrid * kGrid / 8)) continue;
            if (!(p.sub[bit / 8] & (1u << (bit % 8)))) continue;
            if (one.empty()) {
                Writer w(one);
                put_ghost(w, ghost_of(s));
            }
            if (per[i].size() + one.size() > 1100) ship(i);
            per[i].insert(per[i].end(), one.begin(), one.end());
        }
    }
    for (size_t i = 0; i < peers_.size(); ++i) ship(i);
    // send
    for (size_t i = 0; i < peers_.size(); ++i) {
        Peer& p = peers_[i];
        if (int(i) == me_ || !p.up || !p.addr.valid()) continue;
        std::vector<Bytes> out;
        p.chan.flush(now, out, 1024, 250);  // paced: a burst over several ticks rather than overflowing the socket buffers
        link_.send_many(p.addr, out);
    }
}

// ---------------------------------------------------------------- the coordinator

void Server::coordinate(uint32_t now) {
    if (cfg_.scale_mode == "off" || cfg_.worker) return;
    auto load_of = [&](int w) -> double {
        if (size_t(w) >= peers_.size()) return 0;
        const LoadReport& l = peers_[size_t(w)].load;
        // CPU, bandwidth, and whether the tick still fits its budget (80% of 1/tick_hz)
        double budget_us = 0.8e6 / cfg_.tick_hz;
        return std::max({l.cpu_pct / cfg_.split_cpu, double(l.out_Bps) / cfg_.split_bw, l.tick_p90_us / budget_us});
    };
    auto alive = [&](int w) {
        const WorkerInfo* k = map_.worker(w);
        return k && k->alive && (w == 0 || (size_t(w) < peers_.size() && peers_[size_t(w)].up));
    };
    auto players_in = [&](int w, uint32_t region) -> int {
        if (size_t(w) >= peers_.size()) return 0;
        for (auto& r : peers_[size_t(w)].load.regions)
            if (r.region == region) return r.players;
        return 0;
    };
    auto hist_of = [&](int w, uint32_t region) -> const std::vector<uint16_t>* {
        if (size_t(w) >= peers_.size()) return nullptr;
        for (auto& r : peers_[size_t(w)].load.regions)
            if (r.region == region) return &r.hist;
        return nullptr;
    };
    std::vector<int> live;
    for (auto& w : map_.workers)
        if (alive(w.index)) live.push_back(w.index);
    // over / under bookkeeping
    for (int w : live) {
        Peer& p = peers_[size_t(w)];
        double L = load_of(w);
        if (L > 1.0) {
            if (!p.over_since) p.over_since = now;
        } else {
            p.over_since = 0;
        }
        if (L < cfg_.merge_level) {
            if (!p.under_since) p.under_since = now;
        } else {
            p.under_since = 0;
        }
    }
    auto least_loaded = [&](int except) {
        int best = -1;
        double bl = 1e9;
        for (int w : live) {
            if (w == except) continue;
            double L = load_of(w);
            if (L < 0.6 && L < bl) {
                bl = L;
                best = w;
            }
        }
        return best;
    };
    // 1) shed load from the most loaded worker that has been over the thresholds for a while
    int hot = -1;
    double hl = 1.0;
    for (int w : live) {
        Peer& p = peers_[size_t(w)];
        if (p.over_since && now - p.over_since >= uint32_t(cfg_.scale_hold_s) * 1000 &&
            now - p.changed_at >= uint32_t(cfg_.scale_cooldown_s) * 1000 && load_of(w) > hl) {
            hl = load_of(w);
            hot = w;
        }
    }
    if (hot >= 0) {
        int target = least_loaded(hot);
        if (target < 0) {
            // nobody has room: another worker process (auto mode), else the copies are the valve
            if (cfg_.scale_mode == "auto" && int(live.size()) < cfg_.max_workers) spawn_worker(now);
            return;
        }
        // the hot worker's regions by players
        std::vector<std::pair<int, size_t>> mine;
        int total = 0;
        for (size_t i = 0; i < map_.regions.size(); ++i)
            if (map_.regions[i].worker == hot && !map_.regions[i].pinned) {
                int n = players_in(hot, map_.regions[i].id);
                total += n;
                if (n) mine.push_back({n, i});
            }
        if (mine.empty() || total == 0) return;
        std::sort(mine.rbegin(), mine.rend());
        size_t ri = mine.front().second;
        Region& r = map_.regions[ri];
        bool dominant = mine.front().first * 10 > total * 6;
        int side = std::max(r.x1 - r.x0, r.z1 - r.z0);
        std::string what;
        // (a region's copies split off separately: a region whose copies run elsewhere is moved, not split)
        if (dominant && r.stage.empty() && side >= 2 * cfg_.min_region && r.copy_worker.empty()) {
            // split at the players' median along the longer side
            bool along_x = (r.x1 - r.x0) >= (r.z1 - r.z0);
            int w = r.x1 - r.x0, h = r.z1 - r.z0, len = along_x ? w : h;
            std::vector<int> line(size_t(len), 0);
            if (auto* hs = hist_of(hot, r.id); hs && hs->size() == size_t(w * h))
                for (int z = 0; z < h; ++z)
                    for (int x = 0; x < w; ++x) line[size_t(along_x ? x : z)] += (*hs)[size_t(z * w + x)];
            int sum = 0, all = 0, cut = len / 2;
            for (int v : line) all += v;
            for (int i = 0; i < len; ++i) {
                sum += line[size_t(i)];
                if (sum * 2 >= all) {
                    cut = i + 1;
                    break;
                }
            }
            cut = clampi(cut, cfg_.min_region, len - cfg_.min_region);
            Region a = r, b = r;
            a.id = map_.next_region++;
            b.id = map_.next_region++;
            a.parent = b.parent = r.id;
            if (along_x) {
                a.x1 = int16_t(r.x0 + cut);
                b.x0 = a.x1;
            } else {
                a.z1 = int16_t(r.z0 + cut);
                b.z0 = a.z1;
            }
            // the half with fewer players moves
            int na = 0;
            for (int i = 0; i < cut; ++i) na += line[size_t(i)];
            (na * 2 <= all ? a : b).worker = target;
            char buf[200];
            std::snprintf(buf, sizeof buf, "split region %u (%dx%d sub-cells, %d players) at %d: %u -> worker %d, %u stays on %d",
                          r.id, w, h, mine.front().first, cut, (na * 2 <= all ? a : b).id, target,
                          (na * 2 <= all ? b : a).id, hot);
            what = buf;
            retired_[r.id] = r.parent;
            map_.regions[ri] = a;
            map_.regions.push_back(b);
            metrics_.splits++;
        } else if (mine.size() >= 2 || !dominant) {
            // several busy regions: move the busiest one that is not the whole load
            size_t pick = mine.front().second;
            for (auto& [n, i] : mine)
                if (n * 10 <= total * 6) {
                    pick = i;
                    break;
                }
            Region& m = map_.regions[pick];
            m.worker = target;
            what = "move region " + std::to_string(m.id) + " (" + std::to_string(players_in(hot, m.id)) + " players) from " +
                   std::to_string(hot) + " to worker " + std::to_string(target);
            metrics_.moves++;
        } else if (r.stage.empty()) {
            // one smallest region: its copies (the valve) go to other workers, the most populated one first
            std::vector<uint16_t> copies;
            for (auto& rl : peers_[size_t(hot)].load.regions)
                if (rl.region == r.id) copies = rl.copies;
            int pick = -1, pn = 0;
            for (size_t k = 1; k < copies.size(); ++k)
                if (r.worker_of_copy(int(k)) == hot && copies[k] > pn) {
                    pn = copies[k];
                    pick = int(k);
                }
            if (pick < 0) return;  // no copies yet: arrivals open them when the area cap is reached
            if (r.copy_worker.size() < size_t(pick)) {
                r.copy_worker.resize(size_t(pick), uint8_t(r.worker));
            }
            r.copy_worker[size_t(pick - 1)] = uint8_t(target);
            what = "move copy " + std::to_string(pick) + " of region " + std::to_string(r.id) + " (" + std::to_string(pn) +
                   " players) from " + std::to_string(hot) + " to worker " + std::to_string(target);
            metrics_.moves++;
        } else {
            return;
        }
        peers_[size_t(hot)].changed_at = now;
        peers_[size_t(target)].changed_at = now;
        scale_log_.push_back(std::to_string(now) + " " + what);
        log("scale: " + what);
        map_changed();
        return;
    }
    // 2) merge halves back when both sides are calm
    for (size_t i = 0; i < map_.regions.size(); ++i) {
        Region& a = map_.regions[i];
        if (!a.parent) continue;
        for (size_t j = i + 1; j < map_.regions.size(); ++j) {
            Region& b = map_.regions[j];
            if (b.parent != a.parent) continue;
            bool rect = (a.z0 == b.z0 && a.z1 == b.z1 && (a.x1 == b.x0 || b.x1 == a.x0)) ||
                        (a.x0 == b.x0 && a.x1 == b.x1 && (a.z1 == b.z0 || b.z1 == a.z0));
            if (!rect) continue;
            Peer& pa = peers_[size_t(a.worker)];
            Peer& pb = peers_[size_t(b.worker)];
            bool calm = pa.under_since && pb.under_since && now - pa.under_since >= uint32_t(cfg_.merge_hold_s) * 1000 &&
                        now - pb.under_since >= uint32_t(cfg_.merge_hold_s) * 1000;
            if (!calm || load_of(a.worker) + load_of(b.worker) > 0.6 || a.pinned || b.pinned) continue;
            Region m = a;
            m.id = a.parent;
            auto rp = retired_.find(a.parent);
            m.parent = rp == retired_.end() ? 0 : rp->second;
            m.x0 = std::min(a.x0, b.x0);
            m.z0 = std::min(a.z0, b.z0);
            m.x1 = std::max(a.x1, b.x1);
            m.z1 = std::max(a.z1, b.z1);
            m.worker = players_in(a.worker, a.id) >= players_in(b.worker, b.id) ? a.worker : b.worker;
            std::string what = "merge regions " + std::to_string(a.id) + " + " + std::to_string(b.id) + " -> " +
                               std::to_string(m.id) + " on worker " + std::to_string(m.worker);
            map_.regions[i] = m;
            map_.regions.erase(map_.regions.begin() + long(j));
            metrics_.merges++;
            scale_log_.push_back(std::to_string(now) + " " + what);
            log("scale: " + what);
            pa.changed_at = pb.changed_at = now;
            map_changed();
            return;
        }
    }
    // 3) a calm worker gives its regions back to the coordinator, so it can stop
    if (peers_[0].under_since && now - peers_[0].under_since >= uint32_t(cfg_.merge_hold_s) * 1000)
        for (int w : live) {
            if (w == 0) continue;
            Peer& p = peers_[size_t(w)];
            if (!p.under_since || now - p.under_since < uint32_t(cfg_.merge_hold_s) * 1000) continue;
            if (load_of(0) + load_of(w) > 0.6) continue;
            for (auto& g : map_.regions)
                if (g.worker == w && !g.parent && !g.pinned) {
                    g.worker = 0;
                    std::string what = "move region " + std::to_string(g.id) + " back from worker " + std::to_string(w) + " to 0";
                    scale_log_.push_back(std::to_string(now) + " " + what);
                    log("scale: " + what);
                    metrics_.moves++;
                    p.changed_at = now;
                    map_changed();
                    return;
                }
        }
    // 4) workers without regions and players stop after a while (only the ones this process started)
    for (int w : live) {
        if (w == 0) continue;
        Peer& p = peers_[size_t(w)];
        bool has_region = std::any_of(map_.regions.begin(), map_.regions.end(), [&](const Region& g) { return g.worker == w; });
        if (has_region || p.load.players) {
            p.idle_since = 0;
            continue;
        }
        if (!p.idle_since) p.idle_since = now;
        if (p.pid && now - p.idle_since >= uint32_t(cfg_.idle_worker_s) * 1000) {
            link_send(w, kLStop, {});
            // flush the stop now: the link to it is closed here (no "lost" alarm for a worker we stopped)
            std::vector<Bytes> out;
            p.chan.flush(now, out, 8, 0);
            link_.send_many(p.addr, out);
            p.up = false;
            p.epoch = 0;
            p.sub.clear();
            p.load = LoadReport();
            for (auto& k : map_.workers)
                if (k.index == w) k.alive = false;
            scale_log_.push_back(std::to_string(now) + " worker " + std::to_string(w) + " stopped (idle)");
            log("scale: worker " + std::to_string(w) + " stopped (idle)");
            metrics_.workers_stopped++;
            map_changed();
            return;
        }
    }
}

void Server::spawn_worker(uint32_t now) {
#ifdef _WIN32
    (void)now;
#else
    if (now - last_spawn_ < 10000) return;  // one at a time: wait for it to register
    last_spawn_ = now;
    int idx = -1;
    for (int i = 1; i < cfg_.max_workers && idx < 0; ++i)
        if (!map_.worker(i) || !map_.worker(i)->alive) idx = i;
    if (idx < 0) return;
    char exe[4096] = {};
#ifdef __APPLE__
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) != 0) return;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
#endif
    std::string join = cfg_.link_bind + ":" + std::to_string(link_.local_port());
    std::string port = std::to_string(cfg_.worker_port_base + idx - 1);
    std::string lport = std::to_string(cfg_.link_port + idx);
    int nthreads = cfg_.worker_threads;
    if (nthreads <= 0) {
        int hw = int(std::max(1u, std::thread::hardware_concurrency()));
        nthreads = std::clamp(hw / std::max(1, cfg_.max_workers), 2, 16);
    }
    std::string threads = std::to_string(nthreads);
    std::vector<std::string> args = {exe, "--worker", "--join", join, "--port", port, "--link-port", lport, "--threads", threads};
    if (!cfg_.config_path.empty()) {
        args.push_back("--config");
        args.push_back(cfg_.config_path);
    }
    args.push_back("--bind");
    args.push_back(cfg_.bind);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::string sec = "WWHD_ONLINE_LINK_SECRET=" + cfg_.link_secret;
    std::vector<std::string> envs;
    for (char** e = environ; *e; ++e)
        if (std::strncmp(*e, "WWHD_ONLINE_LINK_SECRET=", 24) != 0) envs.push_back(*e);
    envs.push_back(sec);
    std::vector<char*> envp;
    for (auto& e : envs) envp.push_back(e.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, exe, nullptr, nullptr, argv.data(), envp.data()) != 0) {
        log("scale: cannot start a worker process");
        return;
    }
    if (size_t(idx) >= peers_.size()) peers_.resize(size_t(idx) + 1);
    peers_[size_t(idx)].pid = pid;
    spawned_.push_back(pid);
    metrics_.workers_started++;
    scale_log_.push_back(std::to_string(now) + " started worker process " + std::to_string(pid) + " (port " + port + ")");
    log("scale: started a worker process (pid " + std::to_string(pid) + ", port " + port + ")");
#endif
}

void Server::reap_children() {
#ifndef _WIN32
    for (auto it = spawned_.begin(); it != spawned_.end();) {
        int st = 0;
        if (waitpid(*it, &st, WNOHANG) == *it) it = spawned_.erase(it);
        else ++it;
    }
#endif
}

void Server::stop_children() {
#ifndef _WIN32
    for (size_t i = 1; i < peers_.size(); ++i) link_send(int(i), kLStop, {});
    for (pid_t p : spawned_) kill(p, SIGTERM);
    for (pid_t p : spawned_) {
        int st = 0;
        for (int k = 0; k < 50 && waitpid(p, &st, WNOHANG) == 0; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    spawned_.clear();
#endif
}

std::string Server::regions_text() const {
    std::ostringstream o;
    o << "map v" << map_.version << ", " << map_.workers.size() << " workers\n";
    for (auto& w : map_.workers) {
        o << "  worker " << w.index << (w.alive ? "" : " (down)") << " " << w.host << ":" << w.port;
        if (size_t(w.index) < peers_.size()) {
            auto& l = peers_[size_t(w.index)].load;
            char b[120];
            std::snprintf(b, sizeof b, " cpu %.0f%% out %.1f MB/s players %u", l.cpu_pct, l.out_Bps / 1e6, unsigned(l.players));
            o << b;
        }
        o << "\n";
    }
    for (auto& g : map_.regions) {
        bool square = g.stage.empty() && g.x1 - g.x0 == kSubCells && g.z1 - g.z0 == kSubCells && g.x0 % kSubCells == 0 &&
                      g.z0 % kSubCells == 0;
        if (square && g.worker == 0) continue;  // the untouched squares on the coordinator
        o << "  region " << g.id << (g.parent ? " (from " + std::to_string(g.parent) + ")" : std::string()) << " ";
        if (!g.stage.empty()) o << "stage " << g.stage;
        else o << "cells " << g.x0 << "," << g.z0 << " - " << g.x1 << "," << g.z1;
        o << " on worker " << g.worker << "\n";
    }
    o << "log:\n";
    size_t from = scale_log_.size() > 30 ? scale_log_.size() - 30 : 0;
    for (size_t i = from; i < scale_log_.size(); ++i) o << "  " << scale_log_[i] << "\n";
    return o.str();
}

// admin: "regions", "split <id>", "move <id> <worker>"
std::string Server::scale_admin(const std::string& cmd, std::istringstream& in) {
    if (cmd == "regions") return regions_text();
    if (me_ != 0 || !linked()) return "scaling is off (scale.mode = \"off\")";
    uint32_t id = 0;
    in >> id;
    auto it = std::find_if(map_.regions.begin(), map_.regions.end(), [&](const Region& g) { return g.id == id; });
    if (it == map_.regions.end()) return "no region " + std::to_string(id);
    if (cmd == "move") {
        int w = -1;
        in >> w;
        const WorkerInfo* k = map_.worker(w);
        if (!k || !k->alive) return "no live worker " + std::to_string(w);
        it->worker = w;
        it->pinned = true;  // the operator's choice stays (no automatic move or merge)
        scale_log_.push_back(std::to_string(monotonic_ms()) + " admin: move region " + std::to_string(id) + " to " + std::to_string(w));
        metrics_.moves++;
        map_changed();
        return "moved";
    }
    if (cmd == "split") {
        int w = it->worker;
        in >> w;
        Region r = *it;
        bool along_x = (r.x1 - r.x0) >= (r.z1 - r.z0);
        int len = along_x ? r.x1 - r.x0 : r.z1 - r.z0;
        if (!r.stage.empty() || len < 2 * cfg_.min_region) return "region too small to split";
        Region a = r, b = r;
        a.id = map_.next_region++;
        b.id = map_.next_region++;
        a.parent = b.parent = r.id;
        if (along_x) a.x1 = b.x0 = int16_t(r.x0 + len / 2);
        else a.z1 = b.z0 = int16_t(r.z0 + len / 2);
        if (map_.worker(w) && map_.worker(w)->alive) b.worker = w;
        a.pinned = b.pinned = true;
        retired_[r.id] = r.parent;
        *it = a;
        map_.regions.push_back(b);
        scale_log_.push_back(std::to_string(monotonic_ms()) + " admin: split region " + std::to_string(id));
        metrics_.splits++;
        map_changed();
        return "split into " + std::to_string(a.id) + " and " + std::to_string(b.id);
    }
    return "?";
}

}  // namespace online::server
