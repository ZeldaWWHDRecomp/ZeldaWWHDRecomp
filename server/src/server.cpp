// SPDX-License-Identifier: MPL-2.0
// wwhd-online-server core: handshake, sessions, presence and interest management (server.h).
#include "server.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace online::server {

using namespace online::proto;

namespace {
constexpr size_t kBatchLimit = 1140;  // one POSE_BATCH message, so it fits one datagram with its frame
constexpr size_t kMaxDatagramsPerFlush = 48;

uint64_t grid_key(uint64_t world, int32_t gx, int32_t gz, uint16_t copy) {
    return (uint64_t(uint16_t(gx)) << 48) | (uint64_t(uint16_t(gz)) << 32) | (uint64_t(copy) << 16) |
           (world & 0xFFFF);
}
uint64_t square_key(uint64_t world, int square) { return (world << 8) ^ uint64_t(square & 0xFF); }

int64_t unix_now() { return int64_t(std::time(nullptr)); }
}  // namespace

size_t PlaceKeyHash::operator()(const PlaceKey& k) const {
    uint64_t h = k.world * 0x9E3779B97F4A7C15ull;
    for (char c : k.stage) h = (h ^ uint8_t(c)) * 1099511628211ull;
    return size_t(h ^ (uint64_t(k.copy) << 7));
}

Server::Server(Config c) : cfg_(std::move(c)) {
    log = [](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); };
}

Server::~Server() {
    stop_ = true;
    join();
    save_team_states();
#ifndef _WIN32
    if (admin_fd_ >= 0 && !cfg_.admin_socket.empty()) std::remove(cfg_.admin_socket.c_str());
#endif
}

bool Server::start(std::string& why) {
    if (cfg_.public_server && !crypto::backend_secure() && !cfg_.allow_insecure) {
        why = std::string("public = true needs a secure crypto backend; this build has ") + crypto::backend_name() +
              " (set allow_insecure = true to start anyway)";
        return false;
    }
    if (!crypto::Identity::load_or_create(cfg_.key_file, identity_, why)) return false;
    crypto::random_bytes(cookie_key_, sizeof cookie_key_);
    // the IP salt must survive restarts for bans by IP hash: derived from the server key
    crypto::hash(identity_.secret, 32, ip_salt_);
    if (!sock_.open(cfg_.bind, cfg_.port, why)) return false;
    sock_.set_buffers(8 << 20);
    load_bans();
    load_team_states();
    if (!open_admin(why)) return false;
    if (cfg_.shard_count > 1 && !open_link(why)) return false;
    next_tick_ = monotonic_ms();
    last_metrics_ = next_tick_;
    log("wwhd-online-server: " + cfg_.name + " on " + cfg_.bind + ":" + std::to_string(sock_.local_port()) +
        ", key " + fingerprint() + ", crypto " + crypto::backend_name() +
        (cfg_.shard_count > 1 ? ", shard " + std::to_string(cfg_.shard_index) + "/" + std::to_string(cfg_.shard_count)
                              : std::string()));
    return true;
}

void Server::run() {
    while (!stop_) step(5);
}

bool Server::start_thread(std::string& why) {
    if (!start(why)) return false;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Server::join() {
    if (thread_.joinable()) thread_.join();
}

size_t Server::players() const {
    size_t n = 0;
    for (auto& [id, s] : sessions_) n += s->online && !s->dead && !s->redirected ? 1 : 0;
    return n;
}

Session* Server::find_by_key(uint64_t kh) {
    auto it = by_key_.find(kh);
    if (it == by_key_.end()) return nullptr;
    auto s = sessions_.find(it->second);
    return s == sessions_.end() ? nullptr : s->second.get();
}

void Server::step(int wait_ms) {
    uint32_t now = monotonic_ms();
    int until_tick = int(int32_t(next_tick_ - now));
    int w = std::max(0, std::min(wait_ms, until_tick));
    if (sock_.wait(w)) {
        uint8_t buf[2048];
        Address from;
        for (int i = 0; i < 20000; ++i) {
            int n = sock_.recv_from(from, buf, sizeof buf);
            if (n < 0) break;
            metrics_.bytes_in += size_t(n);
            metrics_.packets_in++;
            on_datagram(buf, size_t(n), from, monotonic_ms());
        }
    }
    admin_poll();
    if (cfg_.shard_count > 1) link_poll(monotonic_ms());
    now = monotonic_ms();
    if (int32_t(now - next_tick_) >= 0) {
        uint64_t t0 = monotonic_us();
        tick(now);
        metrics_.tick_us.push_back(uint32_t(monotonic_us() - t0));
        metrics_.ticks++;
        next_tick_ += 1000 / cfg_.tick_hz;
        if (int32_t(now - next_tick_) > 1000) next_tick_ = now;  // fell far behind: don't spin
    }
}

// ---------------------------------------------------------------- handshake

void Server::salt_for(const Address& from, uint32_t bucket, uint8_t out[16]) const {
    Bytes b(from.storage, from.storage + from.len);
    Writer(b).u32(bucket);
    uint8_t h[32];
    hmac_sha256(cookie_key_, 32, b.data(), b.size(), h);
    std::memcpy(out, h, 16);
}

void Server::refuse(const Address& to, uint32_t nonce, Refusal r, const std::string& text, uint16_t retry) {
    Refuse f;
    f.nonce = nonce;
    f.reason = r;
    f.text = text.empty() ? refusal_text(r) : text;
    f.retry_s = retry;
    Bytes b = encode_refuse(f);
    sock_.send_to(to, b.data(), b.size());
    metrics_.bytes_out += b.size();
    metrics_.refused++;
}

uint16_t Server::new_id() {
    bool sharded = cfg_.shard_count > 1;
    for (int tries = 0; tries < 70000; ++tries) {
        uint16_t id;
        if (sharded) {
            uint16_t n = uint16_t(next_id_++ & 0x0FFF);
            if (!n) continue;
            id = uint16_t(((cfg_.shard_index + 1) << 12) | n);
        } else {
            id = next_id_++;
            if (!id) continue;
        }
        if (!by_id_.count(id) && !ghosts_.count(id)) return id;
    }
    return 0;
}

uint32_t Server::new_conn_id() {
    for (;;) {
        uint32_t c;
        crypto::random_bytes(&c, 4);
        if (c && !sessions_.count(c)) return c;
    }
}

void Server::on_datagram(const uint8_t* d, size_t n, const Address& from, uint32_t now) {
    if (n < 1 || n > kMaxDatagram) return;
    switch (d[0]) {
        case kPktConnect: on_connect(d, n, from, now); return;
        case kPktHello: on_hello(d, n, from, now); return;
        case kPktData: break;
        default: metrics_.dropped_bad++; return;
    }
    PacketHeader h;
    if (!read_header(d, n, h)) return;
    auto it = sessions_.find(h.conn_id);
    if (it == sessions_.end()) {
        metrics_.dropped_bad++;
        return;
    }
    Session& s = *it->second;
    if (s.dead || (!s.online && !s.redirected)) return;
    // rate limits come before any crypto work
    double brate = s.bytes.rate;
    (void)brate;
    if (!s.packets.take(1, now) || !s.bytes.take(double(n), now)) {
        metrics_.dropped_rate++;
        strike(s, now, "rate");
        return;
    }
    std::vector<Delivered> got;
    if (!s.chan.receive(d, n, now, got)) {
        metrics_.dropped_bad++;
        return;
    }
    if (!(s.addr == from)) {
        // the client moved (Wi-Fi to mobile): the connection id names it, not the address
        if (s.ip != from.ip_bytes()) {
            if (--per_ip_[s.ip] <= 0) per_ip_.erase(s.ip);
            s.ip = from.ip_bytes();
            per_ip_[s.ip]++;
        }
        s.addr = from;
    }
    for (auto& m : got) {
        if (!s.online) break;
        on_message(s, m, now);
    }
    if (s.chan.broken()) kick(s, kNoticeKick, "protocol error");
}

void Server::on_connect(const uint8_t* d, size_t n, const Address& from, uint32_t now) {
    Connect c;
    if (!decode_connect(d, n, c)) return;  // unpadded: no answer
    bool exempt = cfg_.loopback_exempt && from.is_loopback();
    if (!exempt) {
        auto& b = connect_rate_[from.ip_bytes()];
        if (b.rate == 0) b.init(10, 20, now);
        if (!b.take(1, now)) return;
        if (connect_rate_.size() > 100000) connect_rate_.clear();
    }
    if (restart_at_ || stop_) {
        refuse(from, c.nonce, Refusal::kShuttingDown, "", 30);
        return;
    }
    if (c.proto != kProtoVersion) {
        refuse(from, c.nonce, Refusal::kProtocol, "");
        return;
    }
    Challenge ch;
    ch.nonce = c.nonce;
    ch.bucket = uint32_t(now / 30000);
    salt_for(from, ch.bucket, ch.salt);
    ch.difficulty = exempt ? 0 : cfg_.pow_bits;
    ch.info.name = cfg_.name;
    ch.info.contact = cfg_.contact;
    ch.info.rules = cfg_.rules;
    ch.info.players = uint16_t(std::min<size_t>(players(), 65535));
    ch.info.capacity = uint16_t(std::min(cfg_.max_players, 65535));
    ch.info.flags = uint8_t((crypto::backend_secure() ? 1 : 0) | (cfg_.invite_only ? 2 : 0) | (cfg_.public_server ? 4 : 0));
    Bytes b = encode_challenge(ch, identity_);
    sock_.send_to(from, b.data(), b.size());
    metrics_.bytes_out += b.size();
}

void Server::on_hello(const uint8_t* d, size_t n, const Address& from, uint32_t now) {
    Hello h;
    size_t signed_len = 0;
    if (!decode_hello(d, n, h, signed_len)) {
        metrics_.dropped_bad++;
        return;
    }
    if (h.proto != kProtoVersion) return refuse(from, h.nonce, Refusal::kProtocol, "");
    if (!cfg_.game_data.empty() &&
        std::find(cfg_.game_data.begin(), cfg_.game_data.end(), h.game_data) == cfg_.game_data.end())
        return refuse(from, h.nonce, Refusal::kGameData, "");
    // the challenge cookie: this address asked for it in the last minute
    uint32_t bucket = uint32_t(now / 30000);
    if (h.bucket != bucket && h.bucket + 1 != bucket) return;
    uint8_t salt[16];
    salt_for(from, h.bucket, salt);
    if (std::memcmp(salt, h.salt, 16) != 0) return;
    uint64_t kh = key_hash(h.pub);

    // a repeated hello (our welcome was lost): the same answer again
    if (Session* old = find_by_key(kh); old && old->online && std::memcmp(old->client_eph, h.eph, 32) == 0) {
        if (old->addr == from) {
            sock_.send_to(from, old->welcome.data(), old->welcome.size());
            metrics_.bytes_out += old->welcome.size();
        }
        return;
    }

    // resume (session token) or shard handover (ticket): no proof-of-work needed
    std::array<uint8_t, 32> tok{};
    std::memcpy(tok.data(), h.token, 32);
    Session* resume = nullptr;
    Ticket* ticket = nullptr;
    if (h.resume) {
        auto t = by_token_.find(tok);
        if (t != by_token_.end()) {
            auto s = sessions_.find(t->second);
            if (s != sessions_.end() && std::memcmp(s->second->pub, h.pub, 32) == 0) resume = s->second.get();
        }
        if (!resume) {
            auto tk = tickets_.find(tok);
            if (tk != tickets_.end() && std::memcmp(tk->second.pub, h.pub, 32) == 0) ticket = &tk->second;
        }
    }
    bool exempt = cfg_.loopback_exempt && from.is_loopback();
    if (!resume && !ticket && !exempt && !pow_check(h.salt, h.pub, h.pow, cfg_.pow_bits))
        return refuse(from, h.nonce, Refusal::kBadProof, "");
    if (!hello_signature_ok(d, signed_len, h)) return refuse(from, h.nonce, Refusal::kBadProof, "");
    metrics_.handshakes++;
    std::string ban_reason;
    if (banned(kh, from.ip_bytes(), ban_reason))
        return refuse(from, h.nonce, Refusal::kBanned, ban_reason.empty() ? "" : "banned: " + ban_reason);
    if (cfg_.invite_only &&
        std::find(cfg_.allow.begin(), cfg_.allow.end(), code40_of(kh)) == cfg_.allow.end())
        return refuse(from, h.nonce, Refusal::kInviteOnly, "");

    crypto::Ephemeral eph = crypto::Ephemeral::generate();
    uint8_t tx[32], rx[32];
    if (!crypto::session_keys(eph, h.eph, false, tx, rx)) return refuse(from, h.nonce, Refusal::kMalformed, "");

    Session* s = nullptr;
    bool resumed = false;
    if (resume) {
        // same player, new keys and connection id; the world sees them again from their next pose
        s = resume;
        uint32_t old_conn = s->conn_id;
        auto owned = std::move(sessions_[old_conn]);
        sessions_.erase(old_conn);
        s->conn_id = new_conn_id();
        sessions_[s->conn_id] = std::move(owned);
        by_key_[kh] = s->conn_id;
        by_id_[s->id] = s->conn_id;
        if (s->online) leave_place(*s);
        s->seen.clear();
        resumed = true;
    } else {
        if (Session* old = find_by_key(kh)) {
            // signed in again without the token (a restart of the game): the old session ends
            if (old->online) notice(*old, kNoticeKick, 0, "signed in from another place");
            std::vector<Bytes> out;
            old->chan.flush(now, out, 2, 0);
            for (auto& b : out) sock_.send_to(old->addr, b.data(), b.size());
            drop(*old, "replaced", false);
        }
        if (int(players()) >= cfg_.max_players) return refuse(from, h.nonce, Refusal::kFull, "", 30);
        if (!exempt && per_ip_[from.ip_bytes()] >= cfg_.per_ip)
            return refuse(from, h.nonce, Refusal::kRateLimited, "too many players from one address", 60);
        auto owned = std::make_unique<Session>();
        s = owned.get();
        s->conn_id = new_conn_id();
        s->id = ticket ? ticket->who.id : new_id();
        if (!s->id || by_id_.count(s->id)) s->id = new_id();
        if (!s->id) return refuse(from, h.nonce, Refusal::kFull, "", 30);
        s->key_hash = kh;
        std::memcpy(s->pub, h.pub, 32);
        s->joined_at = now;
        sessions_[s->conn_id] = std::move(owned);
        by_key_[kh] = s->conn_id;
        by_id_[s->id] = s->conn_id;
        crypto::random_bytes(s->token, 32);
        std::array<uint8_t, 32> t{};
        std::memcpy(t.data(), s->token, 32);
        by_token_[t] = s->conn_id;
        if (ticket) {
            metrics_.handovers_in++;
            ghosts_.erase(s->id);
            tickets_.erase(tok);
        }
    }
    if (s->online && resumed) {
        // still counted from before
    }
    if (!resumed || !s->online || s->ip != from.ip_bytes()) {
        if (resumed && s->online && !s->ip.empty()) {
            if (--per_ip_[s->ip] <= 0) per_ip_.erase(s->ip);
        }
        s->ip = from.ip_bytes();
        per_ip_[s->ip]++;
    }
    s->addr = from;
    s->name = h.name;
    s->look = h.look;
    s->device = h.device;
    s->world = h.game_data;
    int near_default = h.device == kPhone ? cfg_.near_cap_phone : cfg_.near_cap_desktop;
    s->near_cap = h.near_cap ? std::min<int>(h.near_cap, near_default) : near_default;
    s->mid_cap = h.mid_cap ? std::min<int>(h.mid_cap, cfg_.mid_cap) : cfg_.mid_cap;
    std::memcpy(s->client_eph, h.eph, 32);
    s->online = true;
    s->redirected = false;
    s->has_pose = false;
    s->hidden = true;
    s->stage_changed = true;
    s->chan.setup(s->conn_id, tx, rx, false, now);
    s->packets.init(cfg_.packets_per_s, cfg_.packets_per_s * 2, now);
    s->bytes.init(cfg_.bytes_per_s, cfg_.bytes_per_s * 2 + 4096, now);
    s->reliable.init(cfg_.reliable_per_s, cfg_.reliable_per_s * 2, now);
    s->app.init(cfg_.app_per_s, cfg_.app_per_s * 2, now);
    s->emote.init(cfg_.emote_per_s, 2, now);
    s->invite.init(1.0 / cfg_.invite_every_s, 2, now);
    s->report.init(1.0 / cfg_.report_every_s, 1, now);

    Welcome w;
    w.nonce = h.nonce;
    w.conn_id = s->conn_id;
    std::memcpy(w.eph, eph.pub, 32);
    w.id = s->id;
    std::memcpy(w.token, s->token, 32);
    w.tick_hz = uint8_t(cfg_.tick_hz);
    w.resumed = resumed || ticket;  // a shard handover keeps the client's puppets too
    s->welcome = encode_welcome(w, identity_, h.eph, tx);
    sock_.send_to(from, s->welcome.data(), s->welcome.size());
    metrics_.bytes_out += s->welcome.size();
    if (ticket) s->party = ticket->party;
    party_member_online(*s, now);
}

// ---------------------------------------------------------------- session messages

void Server::notice(Session& s, uint8_t kind, uint16_t seconds, const std::string& text) {
    Bytes b;
    Writer w(b);
    w.u8(kind);
    w.u16(seconds);
    w.str8(text.substr(0, 200));
    s.chan.send_reliable(kMsgNotice, b);
}

void Server::kick(Session& s, uint8_t kind, const std::string& text) {
    notice(s, kind, 0, text);
    std::vector<Bytes> out;
    s.chan.flush(monotonic_ms(), out, 4, 0);
    for (auto& b : out) sock_.send_to(s.addr, b.data(), b.size());
    metrics_.kicked++;
    log("kick " + friend_code(s.key_hash) + ": " + text);
    drop(s, text, false);
}

void Server::strike(Session& s, uint32_t now, const char* what) {
    if (now - s.strikes_since > 60000) {
        s.strikes = 0;
        s.strikes_since = now;
    }
    ++s.strikes;
    if (s.strikes == cfg_.strikes_kick / 2) notice(s, kNoticeThrottle, 0, std::string("slow down (") + what + ")");
    if (s.strikes >= cfg_.strikes_kick) kick(s, kNoticeKick, std::string("too many messages (") + what + ")");
}

bool Server::sees(const Session& v, uint64_t other) const {
    if (v.blocks.count(other)) return false;
    auto it = by_key_.find(other);
    if (it == by_key_.end()) return true;
    auto s = sessions_.find(it->second);
    return s == sessions_.end() || !s->second->blocks.count(v.key_hash);
}

void Server::on_message(Session& s, const Delivered& m, uint32_t now) {
    if (m.reliable && !s.reliable.take(1, now)) {
        strike(s, now, "reliable");
        return;
    }
    Reader r(m.body);
    switch (m.type) {
        case kMsgPose: on_pose(s, m.body.data(), m.body.size(), now); break;
        case kMsgStage: {
            Where w = get_where(r);
            if (r.done()) on_stage(s, w, now);
            break;
        }
        case kMsgApp: on_app(s, m, now); break;
        case kMsgEmote: {
            uint8_t e = r.u8();
            if (!r.done() || !s.emote.take(1, now)) break;
            Bytes b;
            Writer w(b);
            w.u16(s.id);
            w.u8(e);
            for_viewers(s, [&](Session& v, uint8_t tier) {
                if (tier <= kTierMid) v.chan.send_reliable(kMsgEmoteOf, b);
            });
            break;
        }
        case kMsgBlock:
        case kMsgUnblock: {
            uint64_t kh = r.u64();
            if (!r.done()) break;
            if (m.type == kMsgBlock && s.blocks.size() < 4096) s.blocks.insert(kh);
            if (m.type == kMsgUnblock) s.blocks.erase(kh);
            break;
        }
        case kMsgReport: {
            uint64_t kh = r.u64();
            uint8_t reason = r.u8();
            if (r.done() && s.report.take(1, now)) append_report(s.key_hash, kh, reason);
            break;
        }
        case kMsgBye: drop(s, "bye", false); break;
        case kMsgSwitchCopy: {
            // the next copy of this crowded place that has room (a new one when all are full)
            if (s.copy_key.empty()) break;
            auto& count = copies_[s.copy_key].count;
            size_t from = s.copy, to = from;
            for (size_t k = 1; k <= count.size(); ++k) {
                size_t c = (from + k) % (count.size() + 1);
                if (c == count.size() || count[c] < cfg_.copy_size) {
                    to = c;
                    break;
                }
            }
            if (to == from) break;
            if (from < count.size() && count[from] > 0) count[from]--;
            if (to >= count.size()) count.resize(to + 1, 0);
            count[to]++;
            s.copy = uint16_t(to);
            s.place.copy = s.copy;
            Bytes b;
            Writer w(b);
            put_where(w, s.where);
            w.u16(s.copy);
            w.u16(uint16_t(count.size()));
            s.chan.send_reliable(kMsgCopy, b);
            break;
        }
        case kMsgPartyInvite: {
            uint64_t code = r.u64();
            if (r.done()) party_invite(s, code, now);
            break;
        }
        case kMsgPartyAccept: {
            uint32_t p = r.u32();
            if (r.done()) party_accept(s, p, now);
            break;
        }
        case kMsgPartyDecline: {
            uint32_t p = r.u32();
            if (r.done()) party_decline(s, p);
            break;
        }
        case kMsgPartyLeave: party_leave(s, now); break;
        case kMsgPartySettings: {
            uint16_t f = r.u16();
            if (r.done()) party_settings(s, f);
            break;
        }
        case kMsgProgress: party_progress(s, r); break;
        case kMsgTeamStateAsk:
            if (Party* p = party_of(s)) party_send_team(*p, s);
            break;
        case kMsgPinRunner: {
            uint8_t on = r.u8();
            Party* p = party_of(s);
            if (r.done() && p && p->leader == s.key_hash) {
                p->flags = on ? uint16_t(p->flags | kPartyRunnerPinned) : uint16_t(p->flags & ~kPartyRunnerPinned);
                party_send_state(*p);
            }
            break;
        }
        case kMsgPing: break;
        default: break;  // unknown types are dropped
    }
}

void Server::on_stage(Session& s, const Where& w, uint32_t now) {
    if (!cfg_.stages.empty() && !w.stage_name().empty() &&
        std::find(cfg_.stages.begin(), cfg_.stages.end(), w.stage_name()) == cfg_.stages.end()) {
        strike(s, now, "stage");
        return;
    }
    bool stage_differs = !(w.stage == s.where.stage);
    bool room_differs = w.room != s.where.room;
    if (!stage_differs && !room_differs && !s.stage_changed) return;
    // a busy sea square has copies: moving between squares re-places the player
    s.where = w;
    s.stage_changed = true;
    s.hidden = true;  // until the first pose in the new place
    s.freeze_until = 0;
    place_player(s, now);
    party_place_changed(s, now);
}

void Server::on_pose(Session& s, const uint8_t* d, size_t n, uint32_t now) {
    Reader r(d, n);
    PoseHeader h = get_pose_header(r);
    if (!r.ok() || r.left() > kMaxPoseBlob) {
        strike(s, now, "pose");
        return;
    }
    if (!cfg_.stages.empty() && !h.where.stage_name().empty() &&
        std::find(cfg_.stages.begin(), cfg_.stages.end(), h.where.stage_name()) == cfg_.stages.end()) {
        strike(s, now, "stage");
        return;
    }
    bool at_sea = h.where.stage_name() == cfg_.sea_stage;
    if (at_sea) {
        float lim = proto::kSquareUnits * 0.5f;
        float minx = cfg_.sea_origin_x - lim, maxx = cfg_.sea_origin_x + proto::kSeaSquares * proto::kSquareUnits + lim;
        float minz = cfg_.sea_origin_z - lim, maxz = cfg_.sea_origin_z + proto::kSeaSquares * proto::kSquareUnits + lim;
        if (h.x < minx || h.x > maxx || h.z < minz || h.z > maxz) {
            metrics_.implausible++;
            strike(s, now, "position");
            return;
        }
    }
    // a pose in another place than the last STAGE: the pose's place wins (the STAGE may still be on its way)
    if (!(h.where.stage == s.where.stage) || h.where.room != s.where.room) on_stage(s, h.where, now);
    if (!s.online) return;
    if (int32_t(now - s.freeze_until) < 0) return;
    if (s.has_pose && !s.stage_changed) {
        float dx = h.x - s.head.x, dy = h.y - s.head.y, dz = h.z - s.head.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        double dt = std::max<uint32_t>(now - s.pose_rx, 33) / 1000.0;
        double allowed = cfg_.max_speed * dt + 1500.0;
        if (dist > allowed && s.freeze_until == 0) {
            // implausible: hold the old pose a second, then take the new place as a teleport (warps, respawns)
            metrics_.implausible++;
            strike(s, now, "speed");
            s.freeze_until = now + 1000;
            return;
        }
    }
    s.freeze_until = 0;
    s.head = h;
    s.blob.assign(r.here(), r.here() + r.left());
    s.pose_seq++;
    s.pose_rx = now;
    s.has_pose = true;
    s.stage_changed = false;
    s.hidden = false;  // (a player in a cutscene stays visible; the client shows it idle by flag bit 2)
    if (at_sea) {
        int32_t gx = int32_t(std::floor((h.x - cfg_.sea_origin_x) / proto::kSubCellUnits));
        int32_t gz = int32_t(std::floor((h.z - cfg_.sea_origin_z) / proto::kSubCellUnits));
        s.gx = gx;
        s.gz = gz;
        int sx = int(std::floor(gx / double(proto::kSubCells))), sz = int(std::floor(gz / double(proto::kSubCells)));
        int sq = (sx >= 0 && sx < proto::kSeaSquares && sz >= 0 && sz < proto::kSeaSquares) ? sz * proto::kSeaSquares + sx : -1;
        s.square = sq;
    } else {
        s.square = -1;
    }
    if (cfg_.shard_count > 1) handover_check(s, now);
}

void Server::on_app(Session& s, const Delivered& m, uint32_t now) {
    AppHeader h;
    const uint8_t* body = nullptr;
    size_t bn = 0;
    if (!decode_app(m.body.data(), m.body.size(), h, body, bn)) return;
    if (!s.app.take(1, now)) {
        strike(s, now, "app");
        return;
    }
    Bytes out;
    Writer w(out);
    w.u16(s.id);
    w.u8(h.type);
    w.bytes(body, bn);
    auto deliver = [&](Session& to) {
        if (&to == &s || !to.online) return;
        if (m.reliable)
            to.chan.send_reliable(kMsgAppFrom, out);
        else
            to.chan.send_unreliable(kMsgAppFrom, out);
    };
    if (h.target == kToNear) {
        if (bn > kMaxAppBody) return;
        for_viewers(s, [&](Session& v, uint8_t tier) {
            if (tier == kTierNear) deliver(v);
        });
        return;
    }
    Party* p = party_of(s);
    if (!p) return;
    PartyPlace here{s.where.stage, s.where.room};
    for (uint64_t kh : p->members) {
        Session* t = online_member(kh);
        if (!t || t == &s) continue;
        switch (h.target) {
            case kToParty: deliver(*t); break;
            case kToMember:
                if (t->id == h.member) deliver(*t);
                break;
            case kToRunner: {
                auto it = p->runner.find(here);
                if (it != p->runner.end() && it->second == kh) deliver(*t);
                break;
            }
            case kToPlace:
                if (t->where.stage == s.where.stage && t->where.room == s.where.room) deliver(*t);
                break;
        }
    }
}

// ---------------------------------------------------------------- places and copies

void Server::leave_place(Session& s) {
    if (!s.copy_key.empty()) {
        auto it = copies_.find(s.copy_key);
        if (it != copies_.end()) {
            auto& c = it->second.count;
            if (s.copy < c.size() && c[s.copy] > 0) c[s.copy]--;
            while (c.size() > 1 && c.back() == 0) c.pop_back();  // empty copies at the end close
            if (c.size() == 1 && c[0] == 0) copies_.erase(it);
        }
    }
    s.copy_key.clear();
    s.copy = 0;
    s.place = PlaceKey();
}

void Server::place_player(Session& s, uint32_t now) {
    (void)now;
    // remember the old copy key to leave it
    leave_place(s);
    std::string stage = s.where.stage_name();
    bool at_sea = stage == cfg_.sea_stage;
    std::string room_key = stage + ":" + std::to_string(s.where.room);
    bool busy = std::find(cfg_.busy.begin(), cfg_.busy.end(), stage) != cfg_.busy.end() ||
                std::find(cfg_.busy.begin(), cfg_.busy.end(), room_key) != cfg_.busy.end();
    bool has_copies = !stage.empty() && (!at_sea || busy);
    s.place.world = s.world;
    s.place.stage = s.where.stage;
    s.copy = 0;
    if (has_copies) {
        std::string key = std::to_string(s.world) + "/" + (at_sea ? room_key : stage);
        s.copy_key = key;
        auto& count = copies_[key].count;
        if (count.empty()) count.push_back(0);
        int pick = -1;
        // with the party
        if (Party* p = party_of(s)) {
            for (uint64_t kh : p->members) {
                Session* t = online_member(kh);
                if (t && t != &s && t->place.world == s.world && t->where.stage == s.where.stage &&
                    (!at_sea || t->where.room == s.where.room)) {
                    pick = t->copy;
                    break;
                }
            }
        }
        if (pick < 0) {
            int best = -1;
            for (size_t i = 0; i < count.size(); ++i)
                if (count[i] < cfg_.copy_size && (best < 0 || count[i] > count[size_t(best)])) best = int(i);
            if (best < 0) {
                count.push_back(0);
                best = int(count.size() - 1);
            }
            pick = best;
        }
        if (size_t(pick) >= count.size()) count.resize(size_t(pick) + 1, 0);
        count[size_t(pick)]++;  // a party may go one over the size: friends stay together
        s.copy = uint16_t(pick);
        if (count.size() > 1 || s.copy) {
            Bytes b;
            Writer w(b);
            put_where(w, s.where);
            w.u16(s.copy);
            w.u16(uint16_t(count.size()));
            s.chan.send_reliable(kMsgCopy, b);
        }
    }
    s.place.copy = s.copy;
}

void Server::drop(Session& s, const std::string& why, bool keep) {
    uint32_t now = monotonic_ms();
    if (s.online) {
        leave_place(s);
        if (--per_ip_[s.ip] <= 0) per_ip_.erase(s.ip);
        s.online = false;
        party_member_offline(s, now);
    }
    s.has_pose = false;
    s.seen.clear();
    if (keep) {
        s.lost_at = now;
        return;
    }
    (void)why;
    remove(s.conn_id);
}

void Server::remove(uint32_t conn_id) {
    auto it = sessions_.find(conn_id);
    if (it == sessions_.end() || it->second->dead) return;
    Session& s = *it->second;
    if (s.online) {
        leave_place(s);
        if (--per_ip_[s.ip] <= 0) per_ip_.erase(s.ip);
        s.online = false;
    }
    std::array<uint8_t, 32> t{};
    std::memcpy(t.data(), s.token, 32);
    auto tk = by_token_.find(t);
    if (tk != by_token_.end() && tk->second == conn_id) by_token_.erase(tk);
    auto k = by_key_.find(s.key_hash);
    if (k != by_key_.end() && k->second == conn_id) by_key_.erase(k);
    auto i = by_id_.find(s.id);
    if (i != by_id_.end() && i->second == conn_id) by_id_.erase(i);
    // erased at the next tick: callers may still hold the reference
    s.dead = true;
    s.redirected = false;
    dead_.push_back(conn_id);
}

void Server::reap() {
    for (uint32_t c : dead_) {
        auto it = sessions_.find(c);
        if (it != sessions_.end() && it->second->dead) sessions_.erase(it);
    }
    dead_.clear();
}

// ---------------------------------------------------------------- tick and interest

void Server::rebuild_grid() {
    grid_.clear();
    interiors_.clear();
    squares_.clear();
    ghost_grid_.clear();
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead || !s.online || !s.has_pose || s.hidden) continue;
        if (s.head.where.stage_name() == cfg_.sea_stage) {
            grid_[grid_key(s.world, s.gx, s.gz, s.copy)].push_back(&s);
            if (s.square >= 0) squares_[square_key(s.world, s.square)].push_back(&s);
        } else {
            interiors_[s.place].push_back(&s);
        }
    }
    for (auto& [id, g] : ghosts_) ghost_grid_[grid_key(g.world, g.gx, g.gz, 0)].push_back(&g);
}

void Server::gather(Session& s, std::vector<Session*>& out, std::vector<Ghost*>* ghosts, float extra) {
    if (s.head.where.stage_name() == cfg_.sea_stage) {
        float half = cfg_.window_half * (1.0f + extra);
        int reach = int(std::ceil(half / proto::kSubCellUnits));
        for (int dz = -reach; dz <= reach; ++dz)
            for (int dx = -reach; dx <= reach; ++dx) {
                auto it = grid_.find(grid_key(s.world, s.gx + dx, s.gz + dz, s.copy));
                if (it != grid_.end())
                    for (Session* o : it->second) out.push_back(o);
                if (s.copy) {
                    // a busy square's copy still sees the plain sea around it
                    auto it0 = grid_.find(grid_key(s.world, s.gx + dx, s.gz + dz, 0));
                    if (it0 != grid_.end())
                        for (Session* o : it0->second) out.push_back(o);
                }
                if (ghosts) {
                    auto g = ghost_grid_.find(grid_key(s.world, s.gx + dx, s.gz + dz, 0));
                    if (g != ghost_grid_.end())
                        for (Ghost* o : g->second) ghosts->push_back(o);
                }
            }
    } else {
        auto it = interiors_.find(s.place);
        if (it != interiors_.end())
            for (Session* o : it->second)
                if (o->head.where.room == s.head.where.room) out.push_back(o);
    }
}

template <class F>
void Server::for_viewers(Session& s, F&& fn) {
    std::vector<Session*> around;
    gather(s, around, nullptr, cfg_.hysteresis);
    if (Party* p = party_of(s))
        for (uint64_t kh : p->members)
            if (Session* t = online_member(kh)) around.push_back(t);
    std::sort(around.begin(), around.end());
    around.erase(std::unique(around.begin(), around.end()), around.end());
    for (Session* v : around) {
        if (v == &s || !v->online) continue;
        auto it = v->seen.find(s.id);
        if (it != v->seen.end()) fn(*v, it->second.tier);
    }
}

namespace {
struct Cand {
    float key;   // distance with the hysteresis bonus
    float dist;
    Session* s;
    Ghost* g;
    bool party;
};
}  // namespace

void Server::interest(Session& s, uint32_t now) {
    (void)now;
    static thread_local std::vector<Session*> around;
    static thread_local std::vector<Ghost*> ghosts;
    static thread_local std::vector<Cand> cands;
    around.clear();
    ghosts.clear();
    cands.clear();
    bool at_sea = s.head.where.stage_name() == cfg_.sea_stage;
    if (s.has_pose && !s.hidden) gather(s, around, &ghosts, cfg_.hysteresis);
    Party* p = party_of(s);
    std::unordered_set<uint64_t> party_keys;
    if (p)
        for (uint64_t kh : p->members)
            if (kh != s.key_hash) {
                party_keys.insert(kh);
                Session* t = online_member(kh);
                if (t && t->has_pose && !t->hidden && t->world == s.world) around.push_back(t);
            }
    const float h = 1.0f + cfg_.hysteresis;
    auto consider = [&](Session* o, Ghost* g) {
        uint16_t id = o ? o->id : g->id;
        uint64_t kh = o ? o->key_hash : g->key_hash;
        if (id == s.id || !sees(s, kh)) return;
        const PoseHeader& oh = o ? o->head : g->head;
        float dx = oh.x - s.head.x, dy = oh.y - s.head.y, dz = oh.z - s.head.z;
        auto prev = s.seen.find(id);
        bool was = prev != s.seen.end();
        bool party = party_keys.count(kh) != 0;
        if (!party && at_sea) {
            float lim = cfg_.window_half * (was ? h : 1.0f);
            if (std::fabs(dx) > lim || std::fabs(dz) > lim) return;
        }
        float dist = std::sqrt(dx * dx + (at_sea ? 0 : dy * dy) + dz * dz);
        if (!s.has_pose || s.hidden) dist = 1e9f;  // a receiver between places sees only its party (markers)
        cands.push_back({was ? dist / h : dist, dist, o, g, party});
    };
    std::sort(around.begin(), around.end());
    around.erase(std::unique(around.begin(), around.end()), around.end());
    for (Session* o : around) consider(o, nullptr);
    for (Ghost* g : ghosts)
        if (!by_id_.count(g->id)) consider(nullptr, g);
    size_t keep = size_t(s.near_cap + s.mid_cap + (at_sea ? cfg_.far_cap : 0));
    if (cands.size() > keep + 8) {
        std::nth_element(cands.begin(), cands.begin() + long(keep), cands.end(),
                         [](const Cand& a, const Cand& b) { return (a.party ? -1.0f : a.key) < (b.party ? -1.0f : b.key); });
        cands.resize(keep);
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.key < b.key; });

    for (auto& [id, seen] : s.seen) seen.marked = false;
    int near_n = 0, mid_n = 0, far_n = 0, in_n = 0;
    Bytes batch;
    Writer bw(batch);
    auto flush_batch = [&] {
        if (!batch.empty()) s.chan.send_unreliable(kMsgPoseBatch, batch);
        batch.clear();
    };
    for (const Cand& c : cands) {
        uint16_t id = c.s ? c.s->id : c.g->id;
        auto it = s.seen.find(id);
        bool was = it != s.seen.end();
        uint8_t prev_tier = was ? it->second.tier : 255;
        uint8_t tier;
        float near_lim = cfg_.near_radius * (prev_tier == kTierNear ? h : 1.0f);
        if (c.dist < near_lim && near_n < s.near_cap) tier = kTierNear;
        else if (c.party || mid_n < s.mid_cap) tier = kTierMid;
        else if (at_sea && far_n < cfg_.far_cap) tier = kTierFar;
        else continue;
        if (!was) {
            if (in_n >= cfg_.player_in_per_tick) continue;  // the rest next tick
            ++in_n;
            PlayerIn pi;
            pi.id = id;
            pi.key_hash = c.s ? c.s->key_hash : c.g->key_hash;
            pi.look = c.s ? c.s->look : c.g->look;
            pi.flags = uint8_t((c.party ? 1 : 0) | (c.s && c.s->device == kPhone ? 4 : 0));
            pi.name = c.party ? (c.s ? c.s->name : c.g->name) : stranger_name(pi.key_hash);
            s.chan.send_reliable(kMsgPlayerIn, encode_player_in(pi));
            it = s.seen.emplace(id, Seen{}).first;
            it->second.tier = 255;
        }
        Seen& sn = it->second;
        sn.marked = true;
        bool changed = sn.tier != tier;
        const PoseHeader& oh = c.s ? c.s->head : c.g->head;
        uint32_t oseq = c.s ? c.s->pose_seq : c.g->pose_seq;
        uint32_t oms = c.s ? c.s->pose_ms : c.g->pose_ms;
        size_t before = batch.size();
        if (tier == kTierNear) {
            ++near_n;
            if (changed || sn.pose_seq != oseq) {
                const Bytes& blob = c.s ? c.s->blob : c.g->blob;
                size_t need = 2 + 1 + 4 + PoseHeader::kBytes + 2 + blob.size();
                if (batch.size() + need > kBatchLimit) flush_batch();
                put_near(bw, id, oms, oh, blob.data(), blob.size());
                sn.pose_seq = oseq;
                metrics_.pose_entries_near++;
            }
        } else if (tier == kTierMid) {
            ++mid_n;
            if (changed || (tick_no_ + id) % uint32_t(cfg_.mid_every) == 0) {
                if (batch.size() + kMidEntryBytes > kBatchLimit) flush_batch();
                put_mid(bw, id, oms, oh);
                metrics_.pose_entries_mid++;
            }
        } else {
            ++far_n;
            if (changed || (tick_no_ + id) % uint32_t(cfg_.far_every) == 0) {
                if (batch.size() + kFarEntryBytes > kBatchLimit) flush_batch();
                put_far(bw, id, oh);
                metrics_.pose_entries_far++;
            }
        }
        (void)before;
        sn.tier = tier;
        sn.tick = tick_no_;
    }
    flush_batch();
    // players no longer visible
    for (auto it = s.seen.begin(); it != s.seen.end();) {
        if (it->second.marked) {
            ++it;
            continue;
        }
        Bytes b;
        Writer(b).u16(it->first);
        s.chan.send_reliable(kMsgPlayerOut, b);
        it = s.seen.erase(it);
    }
    if (at_sea && s.square >= 0 && int32_t(tick_no_ - s.next_marker_tick) >= 0) {
        s.next_marker_tick = tick_no_ + uint32_t(cfg_.marker_every);
        send_markers(s);
    }
}

void Server::send_markers(Session& s) {
    auto it = squares_.find(square_key(s.world, s.square));
    Bytes b;
    Writer w(b);
    int n = 0;
    auto add = [&](Session* o) {
        if (o == &s || n >= cfg_.marker_cap || !sees(s, o->key_hash) || o->square < 0) return;
        Marker m;
        m.id = o->id;
        m.square = uint8_t(o->square);
        float sx = cfg_.sea_origin_x + float(o->square % proto::kSeaSquares) * proto::kSquareUnits;
        float sz = cfg_.sea_origin_z + float(o->square / proto::kSeaSquares) * proto::kSquareUnits;
        m.x = uint8_t(std::clamp((o->head.x - sx) / proto::kSquareUnits * 256.0f, 0.0f, 255.0f));
        m.z = uint8_t(std::clamp((o->head.z - sz) / proto::kSquareUnits * 256.0f, 0.0f, 255.0f));
        m.flags = o->party && o->party == s.party ? 1 : 0;
        put_marker(w, m);
        ++n;
        metrics_.markers++;
        if (b.size() + kMarkerBytes > kBatchLimit) {
            s.chan.send_unreliable(kMsgMarkers, b);
            b.clear();
        }
    };
    if (it != squares_.end())
        for (Session* o : it->second) add(o);
    if (Party* p = party_of(s))
        for (uint64_t kh : p->members) {
            Session* t = online_member(kh);
            if (t && t->square >= 0 && t->square != s.square && t->has_pose) add(t);
        }
    if (!b.empty()) s.chan.send_unreliable(kMsgMarkers, b);
}

void Server::flush_all(uint32_t now) {
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead || (!s.online && !s.redirected)) continue;
        outq_.clear();
        s.chan.flush(now, outq_, kMaxDatagramsPerFlush);
        for (auto& b : outq_) {
            sock_.send_to(s.addr, b.data(), b.size());
            metrics_.bytes_out += b.size();
            metrics_.packets_out++;
        }
    }
}

void Server::tick(uint32_t now) {
    ++tick_no_;
    // timeouts
    reap();
    std::vector<uint32_t> gone;
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead) continue;
        if (s.redirected && now - s.redirect_at > 5000) {
            gone.push_back(cid);
            continue;
        }
        if (s.online && now - s.chan.last_heard_ms() > uint32_t(cfg_.silence_s) * 1000) drop(s, "timeout", true);
        if (!s.online && !s.redirected && now - s.lost_at > uint32_t(cfg_.resume_s) * 1000) gone.push_back(cid);
    }
    for (uint32_t c : gone) remove(c);
    reap();
    // ghosts from other shards expire
    for (auto it = ghosts_.begin(); it != ghosts_.end();)
        it = now - it->second.at > 1500 ? ghosts_.erase(it) : std::next(it);

    rebuild_grid();
    for (auto& [cid, sp] : sessions_)
        if (sp->online && !sp->redirected) interest(*sp, now);
    runners_tick(now);
    if (cfg_.shard_count > 1) link_tick(now);
    flush_all(now);
    if ((tick_no_ % uint32_t(cfg_.tick_hz * 10)) == 0) parties_gc(now);
    if (restart_at_ && int32_t(now - restart_at_) >= 0) {
        log("planned restart: stopping");
        stop_ = true;
    }
    if (!cfg_.metrics_file.empty() && now - last_metrics_ >= uint32_t(cfg_.metrics_every_s) * 1000) {
        write_metrics(now);
        last_metrics_ = now;
    }
}

void Server::fuzz_message(uint8_t type, bool reliable, const uint8_t* d, size_t n) {
    uint32_t now = monotonic_ms();
    // two test sessions in one party: messages from the first
    for (int i = 0; i < 2; ++i) {
        uint64_t kh = 0xF0F0F0F000000000ull + uint64_t(i);
        if (find_by_key(kh)) continue;
        auto s = std::make_unique<Session>();
        s->conn_id = new_conn_id();
        s->id = new_id();
        s->key_hash = kh;
        s->name = "fuzz";
        s->ip = "fuzz";
        per_ip_[s->ip]++;
        uint8_t k[32] = {};
        s->chan.setup(s->conn_id, k, k, false, now);
        s->packets.init(1e9, 1e9, now);
        s->bytes.init(1e9, 1e9, now);
        s->reliable.init(1e9, 1e9, now);
        s->app.init(1e9, 1e9, now);
        s->emote.init(1e9, 1e9, now);
        s->invite.init(1e9, 1e9, now);
        s->report.init(1e9, 1e9, now);
        Session* raw = s.get();
        by_key_[kh] = s->conn_id;
        by_id_[s->id] = s->conn_id;
        sessions_[s->conn_id] = std::move(s);
        if (i == 1) {
            Session* a = find_by_key(0xF0F0F0F000000000ull);
            Party p;
            p.id = next_party_++;
            p.leader = a->key_hash;
            parties_[p.id] = p;
            party_join(parties_[p.id], *a, now);
            party_join(parties_[p.id], *raw, now);
        }
    }
    Session* s = find_by_key(0xF0F0F0F000000000ull);
    if (!s || !s->online) {
        if (s) remove(s->conn_id);
        reap();
        return;
    }
    Delivered m;
    m.type = type;
    m.reliable = reliable;
    m.body.assign(d, d + n);
    on_message(*s, m, now);
}

// ---------------------------------------------------------------- metrics

static void usage(uint64_t& cpu_us, uint64_t& rss_kb) {
#ifndef _WIN32
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    cpu_us = uint64_t(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000ull + uint64_t(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec);
#ifdef __APPLE__
    rss_kb = uint64_t(ru.ru_maxrss) / 1024;  // bytes on macOS
#else
    rss_kb = uint64_t(ru.ru_maxrss);
#endif
#else
    cpu_us = 0;
    rss_kb = 0;
#endif
}

void Server::write_metrics(uint32_t now) {
    static uint64_t last_out = 0, last_in = 0;
    static uint32_t last_at = 0;
    double dt = last_at ? (now - last_at) / 1000.0 : cfg_.metrics_every_s;
    uint64_t cpu = 0, rss = 0;
    usage(cpu, rss);
    double cpu_pct = cpu_us_at_ ? (cpu - cpu_us_at_) / (dt * 1e6) * 100.0 : 0;
    cpu_us_at_ = cpu;
    auto t = metrics_.tick_us;
    std::sort(t.begin(), t.end());
    auto pct = [&](double q) { return t.empty() ? 0u : t[std::min(t.size() - 1, size_t(q * double(t.size())))]; };
    size_t n = players();
    double out_s = (metrics_.bytes_out - last_out) / dt, in_s = (metrics_.bytes_in - last_in) / dt;
    char line[512];
    std::snprintf(line, sizeof line,
                  "{\"t\":%u,\"players\":%zu,\"out_Bps\":%.0f,\"in_Bps\":%.0f,\"out_per_player_Bps\":%.0f,"
                  "\"in_per_player_Bps\":%.0f,\"tick_p50_us\":%u,\"tick_p99_us\":%u,\"tick_max_us\":%u,"
                  "\"ticks\":%zu,\"cpu_pct\":%.1f,\"maxrss_mb\":%.1f,\"dropped_rate\":%llu,\"implausible\":%llu}\n",
                  now, n, out_s, in_s, n ? out_s / double(n) : 0, n ? in_s / double(n) : 0, pct(0.5), pct(0.99),
                  t.empty() ? 0u : t.back(), t.size(), cpu_pct, double(rss) / 1024.0,
                  (unsigned long long)metrics_.dropped_rate, (unsigned long long)metrics_.implausible);
    if (std::FILE* f = std::fopen(cfg_.metrics_file.c_str(), "a")) {
        std::fputs(line, f);
        std::fclose(f);
    }
    metrics_.tick_us.clear();
    last_out = metrics_.bytes_out;
    last_in = metrics_.bytes_in;
    last_at = now;
}

std::string Server::status_text() {
    size_t parties = 0;
    for (auto& [id, p] : parties_) parties += p.empty_since == 0 ? 1 : 0;
    char b[512];
    std::snprintf(b, sizeof b,
                  "%s: %zu players (max %d), %zu active parties, %zu kept team states, %zu bans; key %s; crypto %s; "
                  "out %.1f MB, in %.1f MB, %llu refused, %llu kicked",
                  cfg_.name.c_str(), players(), cfg_.max_players, parties, parties_.size() - parties, bans_.size(),
                  fingerprint().c_str(), crypto::backend_name(), metrics_.bytes_out / 1e6, metrics_.bytes_in / 1e6,
                  (unsigned long long)metrics_.refused, (unsigned long long)metrics_.kicked);
    return b;
}

}  // namespace online::server
