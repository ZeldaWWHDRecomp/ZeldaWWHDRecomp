// SPDX-License-Identifier: MPL-2.0
// Shards (DESIGN.md 3.3, P4): several server processes split the sea squares (and hash the interiors). A client talks
// to its home shard only. Shards send each other the players within one sub-cell of a square border they don't own
// ("ghosts"), so a client sees across the border without connecting to the neighbour; when a player is
// handover_depth inside a square another shard owns, the home shard hands the session over: the new shard gets a
// ticket over the link, the client a REDIRECT, and it reconnects with the ticket instead of a proof-of-work. Its short
// id stays the same, and the old shard keeps drawing it until the new shard's ghost of it arrives: no popping.
//
// Link datagrams (UDP, loopback or LAN): u8 type, u8 from shard, u16 count, body, 16-byte HMAC-SHA-256 tag (secret).
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "server.h"

namespace online::server {

using namespace online::proto;

namespace {
enum LinkType : uint8_t { kLinkBorder = 1, kLinkHandoff = 2 };
constexpr size_t kLinkTag = 16;

void put_ghost(Writer& w, const Ghost& g) {
    w.u16(g.id);
    w.u64(g.key_hash);
    w.u64(g.world);
    w.u32(g.look);
    w.u32(g.pose_ms);
    w.u32(g.pose_seq);
    put_pose_header(w, g.head);
    w.u16(uint16_t(g.blob.size()));
    w.bytes(g.blob.data(), g.blob.size());
}

bool get_ghost(Reader& r, Ghost& g) {
    g.id = r.u16();
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
    return r.ok();
}

Ghost ghost_of(const Session& s) {
    Ghost g;
    g.id = s.id;
    g.key_hash = s.key_hash;
    g.world = s.world;
    g.look = s.look;
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
}  // namespace

bool Server::open_link(std::string& why) {
    std::string host;
    uint16_t port = 0;
    link_addr_.clear();
    for (auto& l : cfg_.shard_links) {
        Address a;
        if (!split_host_port(l, host, port) || !resolve(host, port, a, why)) {
            why = "bad shard link address " + l;
            return false;
        }
        link_addr_.push_back(a);
    }
    split_host_port(cfg_.shard_links[size_t(cfg_.shard_index)], host, port);
    if (!link_.open(host, port, why)) return false;
    link_.set_buffers(8 << 20);
    return true;
}

void Server::link_send(int shard, uint8_t type, const Bytes& body) {
    if (shard < 0 || size_t(shard) >= link_addr_.size() || shard == cfg_.shard_index) return;
    Bytes b;
    Writer w(b);
    w.u8(type);
    w.u8(uint8_t(cfg_.shard_index));
    w.u16(0);
    w.bytes(body.data(), body.size());
    uint8_t key[32], tag[32];
    sha256(cfg_.link_secret.data(), cfg_.link_secret.size(), key);
    hmac_sha256(key, 32, b.data(), b.size(), tag);
    w.bytes(tag, kLinkTag);
    link_.send_to(link_addr_[size_t(shard)], b.data(), b.size());
}

int Server::shard_of_place(const Session& s) const {
    std::string stage = s.where.stage_name();
    if (stage.empty()) return cfg_.shard_index;
    if (stage == cfg_.sea_stage) return s.square >= 0 ? cfg_.shard_of(s.square) : cfg_.shard_index;
    uint32_t h = 2166136261u;
    for (char c : stage) h = (h ^ uint8_t(c)) * 16777619u;
    return int(h % uint32_t(cfg_.shard_count));
}

void Server::handover_check(Session& s, uint32_t now) {
    if (s.redirected || !s.online) return;
    int target = shard_of_place(s);
    if (target == cfg_.shard_index) return;
    if (s.where.stage_name() == cfg_.sea_stage) {
        if (!s.has_pose || s.square < 0) return;
        // deep enough inside the new square
        float sx = cfg_.sea_origin_x + float(s.square % kSeaSquares) * kSquareUnits;
        float sz = cfg_.sea_origin_z + float(s.square / kSeaSquares) * kSquareUnits;
        float in = std::min({s.head.x - sx, sx + kSquareUnits - s.head.x, s.head.z - sz, sz + kSquareUnits - s.head.z});
        if (in < cfg_.handover_depth) return;
    }
    Ticket t;
    t.at = now;
    t.who = ghost_of(s);
    std::memcpy(t.pub, s.pub, 32);
    t.device = s.device;
    t.near_cap = s.near_cap;
    t.mid_cap = s.mid_cap;
    t.party = 0;  // parties do not cross shards yet (docs/online-server.md)
    uint8_t ticket[32];
    crypto::random_bytes(ticket, 32);
    Bytes body;
    Writer w(body);
    w.bytes(ticket, 32);
    w.bytes(s.pub, 32);
    w.u8(s.device);
    w.u8(uint8_t(s.near_cap));
    w.u8(uint8_t(s.mid_cap));
    put_ghost(w, t.who);
    link_send(target, kLinkHandoff, body);
    std::string host;
    uint16_t port = 0;
    split_host_port(cfg_.shard_peers[size_t(target)], host, port);
    Bytes r;
    Writer rw(r);
    rw.str8(host);
    rw.u16(port);
    rw.bytes(ticket, 32);
    s.chan.send_reliable(kMsgRedirect, r);
    s.redirected = true;
    s.redirect_at = now;
    party_member_offline(s, now);
    metrics_.handovers_out++;
}

void Server::link_tick(uint32_t now) {
    for (auto it = tickets_.begin(); it != tickets_.end();) it = now - it->second.at > 30000 ? tickets_.erase(it) : std::next(it);
    // border ghosts, batched per neighbour shard
    std::vector<Bytes> per(size_t(cfg_.shard_count));
    std::vector<uint16_t> counts(size_t(cfg_.shard_count), 0);
    auto ship = [&](int sh) {
        if (counts[size_t(sh)] == 0) return;
        link_send(sh, kLinkBorder, per[size_t(sh)]);
        per[size_t(sh)].clear();
        counts[size_t(sh)] = 0;
    };
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead || !s.online || s.redirected || !s.has_pose || s.square < 0) continue;
        if (s.head.where.stage_name() != cfg_.sea_stage) continue;
        int col = s.square % kSeaSquares, row = s.square / kSeaSquares;
        bool sent_to[16] = {};
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                int c = col + dx, r = row + dz;
                if (c < 0 || r < 0 || c >= kSeaSquares || r >= kSeaSquares) continue;
                int owner = cfg_.shard_of(r * kSeaSquares + c);
                if (owner == cfg_.shard_index || sent_to[owner]) continue;
                float x0 = cfg_.sea_origin_x + float(c) * kSquareUnits, z0 = cfg_.sea_origin_z + float(r) * kSquareUnits;
                float ex = std::max({x0 - s.head.x, 0.0f, s.head.x - (x0 + kSquareUnits)});
                float ez = std::max({z0 - s.head.z, 0.0f, s.head.z - (z0 + kSquareUnits)});
                if (std::sqrt(ex * ex + ez * ez) > cfg_.border_band) continue;
                sent_to[owner] = true;
                Bytes one;
                Writer w(one);
                put_ghost(w, ghost_of(s));
                if (per[size_t(owner)].size() + one.size() > 1100) ship(owner);
                per[size_t(owner)].insert(per[size_t(owner)].end(), one.begin(), one.end());
                counts[size_t(owner)]++;
            }
    }
    for (int sh = 0; sh < cfg_.shard_count; ++sh) ship(sh);
}

void Server::link_poll(uint32_t now) {
    uint8_t buf[2048];
    Address from;
    uint8_t key[32];
    sha256(cfg_.link_secret.data(), cfg_.link_secret.size(), key);
    for (int i = 0; i < 4096; ++i) {
        int n = link_.recv_from(from, buf, sizeof buf);
        if (n < 0) break;
        if (size_t(n) < 4 + kLinkTag) continue;
        uint8_t tag[32];
        hmac_sha256(key, 32, buf, size_t(n) - kLinkTag, tag);
        if (std::memcmp(tag, buf + n - int(kLinkTag), kLinkTag) != 0) continue;
        uint8_t type = buf[0], sh = buf[1];
        if (sh >= cfg_.shard_count || sh == cfg_.shard_index) continue;
        Reader r(buf + 4, size_t(n) - 4 - kLinkTag);
        if (type == kLinkBorder) {
            while (r.ok() && r.left()) {
                Ghost g;
                if (!get_ghost(r, g)) break;
                auto local = by_id_.find(g.id);
                if (local != by_id_.end()) {
                    auto s = sessions_.find(local->second);
                    if (s == sessions_.end()) continue;
                    if (!s->second->redirected) continue;  // ours: a stale ghost from before a handover
                    remove(local->second);  // handed over and now drawn from the new shard
                }
                g.at = now;
                if (g.head.where.stage_name() != cfg_.sea_stage) continue;
                g.gx = int32_t(std::floor((g.head.x - cfg_.sea_origin_x) / kSubCellUnits));
                g.gz = int32_t(std::floor((g.head.z - cfg_.sea_origin_z) / kSubCellUnits));
                ghosts_[g.id] = std::move(g);
                metrics_.ghosts++;
            }
        } else if (type == kLinkHandoff) {
            std::array<uint8_t, 32> t{};
            Ticket tk;
            r.bytes(t.data(), 32);
            r.bytes(tk.pub, 32);
            tk.device = r.u8();
            tk.near_cap = r.u8();
            tk.mid_cap = r.u8();
            if (!get_ghost(r, tk.who)) continue;
            tk.at = now;
            tk.party = 0;
            tickets_[t] = std::move(tk);
        }
    }
}

}  // namespace online::server
