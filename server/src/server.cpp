// SPDX-License-Identifier: MPL-2.0
// wwhd-online-server core: handshake, sessions, presence and interest management (server.h).
#include "server.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    pool_stop();
    stop_children();
    if (!abandoned_) save_team_states();
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
    // tick threads: the config's, else half the cores (at most 16; a big machine running one process uses them)
    workers_ = cfg_.threads > 0 ? std::min(cfg_.threads, 64)
                                : int(std::min(16u, std::max(1u, std::thread::hardware_concurrency() / 2)));
#ifdef _WIN32
    bool reuse = false;
#else
    bool reuse = workers_ > 1;
#endif
    if (!sock_.open(cfg_.bind, cfg_.port, why, reuse)) return false;
    sock_.set_buffers(8 << 20);
    pool_start();
    for (int i = 1; reuse && i < workers_; ++i) {
        auto extra = std::make_unique<UdpSocket>();
        std::string w;
        if (!extra->open(cfg_.bind, sock_.local_port(), w, true)) break;
        extra->set_buffers(4 << 20);
        send_socks_.push_back(std::move(extra));
    }
    load_bans();
    load_team_states();
    if (cfg_.tcp && !tcp_listen_.open(cfg_.bind, sock_.local_port(), why)) return false;
    if (cfg_.worker) cfg_.admin_socket.clear();  // the coordinator's admin socket runs the workers too
    if (!open_admin(why)) return false;
    if (linked() && !open_link(why)) return false;
    next_tick_ = monotonic_ms();
    last_metrics_ = next_tick_;
    log("wwhd-online-server: " + cfg_.name + " on " + cfg_.bind + ":" + std::to_string(sock_.local_port()) +
        ", key " + fingerprint() + ", crypto " + crypto::backend_name() +
        (cfg_.worker ? ", worker of " + cfg_.join
                     : linked() ? ", scaling " + cfg_.scale_mode + " (link port " + std::to_string(link_.local_port()) + ")"
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
    bool ready = sock_.wait(send_socks_.empty() ? w : std::min(w, 1));
    if (ready || !send_socks_.empty()) {
        uint8_t buf[2048];
        Address from;
        auto drain = [&](UdpSocket& u) {
            for (int i = 0; i < 20000; ++i) {
                int n = u.recv_from(from, buf, sizeof buf);
                if (n < 0) break;
                if (!cfg_.udp) continue;  // a TCP-only server (as if UDP were blocked on the way)
                metrics_.bytes_in += size_t(n);
                metrics_.packets_in++;
                on_datagram(buf, size_t(n), from, monotonic_ms());
            }
        };
        drain(sock_);
        for (auto& u : send_socks_) drain(*u);  // a port shared by several sockets may deliver to any of them
    }
    admin_poll();
    if (tcp_listen_.is_open()) tcp_poll(monotonic_ms());
    if (linked()) link_poll(monotonic_ms());
    now = monotonic_ms();
    if (int32_t(now - next_tick_) >= 0) {
        uint64_t t0 = monotonic_us();
        tick(now);
        metrics_.tick_us.push_back(uint32_t(monotonic_us() - t0));
        recent_ticks_.push_back(metrics_.tick_us.back());
        while (recent_ticks_.size() > size_t(cfg_.tick_hz)) recent_ticks_.pop_front();
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
    send_dg(sock_, to, b.data(), b.size());
    metrics_.bytes_out += b.size();
    metrics_.refused++;
}

uint16_t Server::new_id() {
    if (linked()) {
        // ids come in blocks of 256 from the coordinator, so they are unique over all workers
        for (uint16_t b : id_blocks_)
            for (int k = 0; k < 256; ++k) {
                uint16_t id = uint16_t(b + uint16_t((next_id_ + k) % 256));
                if (id && !by_id_.count(id) && !ghosts_.count(id) && !lent_.count(id)) {
                    next_id_ = uint16_t(next_id_ + k + 1);
                    return id;
                }
            }
        return 0;
    }
    for (int tries = 0; tries < 70000; ++tries) {
        uint16_t id = next_id_++;
        if (!id) continue;
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
    if (!s.packets.take(1, now)) {
        metrics_.dropped_rate++;
        strike(s, now, "packets");
        return;
    }
    if (!s.bytes.take(double(n), now)) {
        metrics_.dropped_rate++;
        strike(s, now, "upload");
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
    send_dg(sock_, from, b.data(), b.size());
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
            send_dg(sock_, from, old->welcome.data(), old->welcome.size());
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
    bool resumed = false, had_ticket = false;
    uint32_t join_party = 0;
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
            for (auto& b : out) send_dg(sock_, old->addr, b.data(), b.size());
            drop(*old, "replaced", false);
        }
        if (int(players()) >= cfg_.max_players) return refuse(from, h.nonce, Refusal::kFull, "", 30);
        if (!exempt && per_ip_[from.ip_bytes()] >= cfg_.per_ip)
            return refuse(from, h.nonce, Refusal::kRateLimited, "too many players from one address", 60);
        auto owned = std::make_unique<Session>();
        s = owned.get();
        s->conn_id = new_conn_id();
        if (ticket) {
            // back here soon after leaving: our old (redirected) copy of the player gives way
            auto old = by_id_.find(ticket->who.id);
            if (old != by_id_.end()) {
                auto os = sessions_.find(old->second);
                if (os != sessions_.end() && os->second->redirected && os->second->key_hash == kh) remove(old->second);
            }
        }
        s->id = ticket ? ticket->who.id : new_id();
        if (!s->id || by_id_.count(s->id)) s->id = new_id();
        if (!s->id) return refuse(from, h.nonce, Refusal::kFull, "the server is short of player ids; retrying", 2);
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
            if (std::getenv("WWHD_ONLINE_DEBUG_HANDOVER"))
                log("handover: player " + std::to_string(ticket->who.id) + " arrived on worker " + std::to_string(me_) +
                    " from worker " + std::to_string(ticket->from) + (h.resume ? "" : " (no ticket?)"));
            ghosts_.erase(s->id);
            s->lent_from = ticket->from == me_ ? -1 : ticket->from;
            // seen at once where the old worker last saw it: no gap until its first pose here
            s->head = ticket->who.head;
            s->blob = ticket->who.blob;
            s->pose_ms = ticket->who.pose_ms;
            s->pose_seq = ticket->who.pose_seq;
            s->where = s->head.where;
            if (s->where.stage_name() == cfg_.sea_stage) {
                s->has_pose = true;
                s->gx = int32_t(std::floor((s->head.x - cfg_.sea_origin_x) / kSubCellUnits));
                s->gz = int32_t(std::floor((s->head.z - cfg_.sea_origin_z) / kSubCellUnits));
                s->copy = ticket->who.copy;
                s->pose_rx = now;
            }
            if (ticket->from == me_) lent_.erase(s->id);  // one of our ids comes home
            join_party = ticket->join ? ticket->party : 0;
            tickets_.erase(tok);
            ticket = nullptr;
            had_ticket = true;
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
    s->handing = false;
    if (!had_ticket) {
        s->has_pose = false;
        s->hidden = true;
        s->stage_changed = true;
    } else {
        s->hidden = !s->has_pose;
        s->stage_changed = !s->has_pose;
    }
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
    w.resumed = resumed || had_ticket;  // a handover between workers keeps the client's puppets too
    s->welcome = encode_welcome(w, identity_, h.eph, tx);
    send_dg(sock_, from, s->welcome.data(), s->welcome.size());
    metrics_.bytes_out += s->welcome.size();
    party_member_online(*s, now);
    if (join_party) {
        // it came here to accept an invite to a party this worker holds
        auto p = parties_.find(join_party);
        if (p != parties_.end() && p->second.members.size() < size_t(kMaxPartySize)) {
            p->second.invites.erase(std::remove_if(p->second.invites.begin(), p->second.invites.end(),
                                                   [&](const Party::Invite& i) { return i.to == s->key_hash; }),
                                    p->second.invites.end());
            party_join(p->second, *s, now);
            party_result(*s, 0, "joined the party");
        }
    }
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
    kick_quiet(s, text);
}

// the notice is queued already: flush it and drop the session
void Server::kick_quiet(Session& s, const std::string& text) {
    std::vector<Bytes> out;
    s.chan.flush(monotonic_ms(), out, 4, 0);
    for (auto& b : out) send_dg(sock_, s.addr, b.data(), b.size());
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
    std::string w = what;
    uint16_t code = w == "upload" ? kLimitUpload : w == "packets" ? kLimitPackets : w == "reliable" ? kLimitReliable
                  : w == "app" ? kLimitApp : (w == "pose" || w == "speed" || w == "position") ? kLimitPose : kLimitOther;
    std::string text;
    char b[160];
    if (code == kLimitUpload)
        std::snprintf(b, sizeof b, "upload over this server's limit of %.0f bytes/s", s.bytes.rate);
    else if (code == kLimitPackets)
        std::snprintf(b, sizeof b, "more than %.0f packets/s", s.packets.rate);
    else
        std::snprintf(b, sizeof b, "too many messages (%s)", what);
    text = b;
    if (s.strikes == cfg_.strikes_kick / 2) notice(s, kNoticeThrottle, code, "slow down: " + text);
    if (s.strikes >= cfg_.strikes_kick) {
        notice(s, kNoticeKickLimit, code, text);
        kick_quiet(s, "limit: " + text);
    }
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
            if (s.where.stage_name() == cfg_.sea_stage && s.area >= 0) {
                for (uint16_t c = uint16_t(s.copy + 1);; ++c) {
                    int n = 0;
                    auto it = area_count_.find((uint64_t(s.area) << 16) | c);
                    if (it != area_count_.end()) n = it->second;
                    if (n < cfg_.area_cap || c > 64) {
                        set_sea_copy(s, c > 64 ? 0 : c);
                        break;
                    }
                }
                break;
            }
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
        case kMsgPartyRestore: party_restore(s, r, now); break;
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
        case kMsgJoinCopyOf: {
            uint64_t kh = r.u64();
            if (!r.done()) break;
            // a friend (anyone the player could see) in the same place: go to their copy
            uint16_t copy = 0;
            bool found = false;
            std::string place;
            if (Session* f = find_by_key(kh); f && f->online && f->world == s.world && sees(s, *f)) {
                copy = f->copy;
                found = f->where.stage == s.where.stage;
            } else {
                for (auto& [id, g] : ghosts_)
                    if (g.key_hash == kh && g.world == s.world) {
                        copy = g.copy;
                        found = g.head.where.stage == s.where.stage;
                    }
            }
            if (!found || copy == s.copy) break;
            if (s.where.stage_name() == cfg_.sea_stage) {
                set_sea_copy(s, copy);
            } else if (!s.copy_key.empty()) {
                auto& count = copies_[s.copy_key].count;
                if (copy >= count.size()) count.resize(size_t(copy) + 1, 0);
                if (s.copy < count.size() && count[s.copy] > 0) count[s.copy]--;
                count[copy]++;
                s.copy = copy;
                s.place.copy = copy;
                Bytes b;
                Writer w(b);
                put_where(w, s.where);
                w.u16(s.copy);
                w.u16(uint16_t(count.size()));
                s.chan.send_reliable(kMsgCopy, b);
            }
            break;
        }
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
    if (!stage_differs && w.stage_name() == cfg_.sea_stage && !s.stage_changed) {
        // sailing from one square to the next (the sea's room is the square): same place, same copy
        s.where.room = w.room;
        return;
    }
    // a busy sea square has copies: moving between squares re-places the player
    s.where = w;
    s.stage_changed = true;
    s.hidden = true;  // until the first pose in the new place
    s.freeze_until = 0;
    place_player(s, now);
    party_place_changed(s, now);
    if (linked()) handover_check(s, now);
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
    // the sender's own clock when it sent this pose (its packet's time stamp): receivers order and space poses
    // by it; the server's receive time if the client's clock is not known yet
    s.pose_ms = s.chan.peer_ms() ? s.chan.peer_ms() : now;
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
        int area = std::clamp(gz, 0, kGrid - 1) / 2 * (kGrid / 2) + std::clamp(gx, 0, kGrid - 1) / 2;
        if (area != s.area) {
            s.area = area;
            place_at_sea(s, now);
        }
    } else {
        s.square = -1;
    }
    if (linked()) handover_check(s, now);
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
    static const bool debug = std::getenv("WWHD_ONLINE_DEBUG_APP") != nullptr;
    if (debug) log("app " + std::to_string(h.type) + " from " + std::to_string(s.id) + " target " + std::to_string(h.target));
    auto deliver = [&](Session& to) {
        if (debug) log("  to " + std::to_string(to.id) + (to.online ? "" : " (offline)"));
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
    // interiors always have copies; at sea, copies are per area (place_at_sea)
    bool has_copies = !stage.empty() && !at_sea;
    s.area = -1;
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
    // an id lent by another worker goes back when the player leaves the server (not when handed on)
    if (s.lent_from >= 0 && !s.redirected) {
        Bytes b;
        Writer(b).u16(s.id);
        link_send(s.lent_from, kLIdFree, b);
    }
    if (s.lent_from < 0 && !s.redirected) lent_.erase(s.id);
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
    area_count_.clear();
    squares_.clear();
    ghost_grid_.clear();
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead || !s.online || !s.has_pose || s.hidden) continue;
        if (s.head.where.stage_name() == cfg_.sea_stage) {
            grid_[grid_key(s.world, s.gx, s.gz, s.copy)].push_back(&s);
            if (s.area >= 0) area_count_[(uint64_t(s.area) << 16) | s.copy]++;
            if (s.square >= 0) squares_[square_key(s.world, s.square)].push_back(&s);
        } else {
            interiors_[s.place].push_back(&s);
        }
    }
    for (auto& [id, g] : ghosts_) {
        ghost_grid_[grid_key(g.world, g.gx, g.gz, g.copy)].push_back(&g);
        area_count_[(uint64_t(std::max(0, g.gz) / 2 * 28 + std::max(0, g.gx) / 2) << 16) | g.copy]++;
    }
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
                if (ghosts) {
                    auto g = ghost_grid_.find(grid_key(s.world, s.gx + dx, s.gz + dz, s.copy));
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
    uint64_t party_keys[kMaxPartySize] = {};
    int party_n = 0;
    if (p)
        for (uint64_t kh : p->members)
            if (kh != s.key_hash && party_n < kMaxPartySize) {
                party_keys[party_n++] = kh;
                // party members are always announced, wherever they are (their poses only when they have one)
                Session* t = online_member(kh);
                if (t && t->world == s.world) around.push_back(t);
            }
    const float h = 1.0f + cfg_.hysteresis;
    auto consider = [&](Session* o, Ghost* g) {
        uint16_t id = o ? o->id : g->id;
        uint64_t kh = o ? o->key_hash : g->key_hash;
        if (id == s.id || (o ? !sees(s, *o) : !s.blocks.empty() && s.blocks.count(kh))) return;
        const PoseHeader& oh = o ? o->head : g->head;
        float dx = oh.x - s.head.x, dy = oh.y - s.head.y, dz = oh.z - s.head.z;
        auto prev = s.seen.find(id);
        bool was = prev != s.seen.end();
        bool party = std::find(party_keys, party_keys + party_n, kh) != party_keys + party_n;
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
    size_t keep = size_t(s.near_cap + s.mid_cap + (at_sea ? cfg_.far_cap : 0)) * 9 / 8;
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
        // caps with slack for who is already shown: a crowd at the cap does not flicker in and out
        int slack_near = prev_tier == kTierNear ? s.near_cap / 8 : 0;
        int slack_mid = prev_tier <= kTierMid ? s.mid_cap / 8 : 0;
        int slack_far = was ? cfg_.far_cap / 8 : 0;
        if (c.dist < near_lim && near_n < s.near_cap + slack_near) tier = kTierNear;
        else if (c.party || mid_n < s.mid_cap + slack_mid) tier = kTierMid;
        else if (at_sea && far_n < cfg_.far_cap + slack_far) tier = kTierFar;
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
        if (tier == kTierNear && c.s && (!c.s->has_pose || c.s->hidden)) tier = kTierMid;
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
            if (c.s && (!c.s->has_pose || c.s->hidden)) {
                // a party member between places: announced, no pose to send
            } else if (changed || (tick_no_ + id) % uint32_t(cfg_.mid_every) == 0) {
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
        // why: still around but past the caps or the window (churn), or gone from this worker's view (a gap)
        if (by_id_.count(it->first) || ghosts_.count(it->first)) metrics_.out_churn++;
        else metrics_.out_gone++;
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
        if (o == &s || n >= cfg_.marker_cap || !sees(s, *o) || o->square < 0) return;
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

static thread_local int t_worker = 0;

UdpSocket& Server::out_sock() {
    if (t_worker <= 0 || size_t(t_worker) > send_socks_.size()) return sock_;
    return *send_socks_[size_t(t_worker - 1)];
}

void Server::flush_one(Session& s, uint32_t now) {
    static thread_local std::vector<Bytes> out;
    out.clear();
    if (s.addr.tcp_id) {
        // a backed-up TCP stream: this tick's poses wait (are dropped); reliable messages stay queued
        auto it = tcp_.find(s.addr.tcp_id);
        if (it == tcp_.end() || it->second.s->queued() > kTcpBackedUp) {
            s.chan.drop_unreliable();
            return;
        }
    }
    s.chan.flush(now, out, kMaxDatagramsPerFlush);
    uint64_t bytes = 0;
    UdpSocket& u = out_sock();
    if (s.addr.tcp_id) {
        for (auto& b : out) send_dg(u, s.addr, b.data(), b.size());
    } else {
        u.send_many(s.addr, out);
    }
    for (auto& b : out) bytes += b.size();
    metrics_.bytes_out += bytes;
    metrics_.packets_out += out.size();
}

void Server::pool_start() {
    for (int i = 1; i < workers_; ++i)
        pool_.threads.emplace_back([this, i] {
            t_worker = i;
            uint64_t seen = 0;
            for (;;) {
                std::function<void(int)> job;
                {
                    std::unique_lock<std::mutex> lk(pool_.m);
                    pool_.cv.wait(lk, [&] { return pool_.quit || pool_.gen != seen; });
                    if (pool_.quit) return;
                    seen = pool_.gen;
                    job = pool_.job;
                }
                job(i);
                std::lock_guard<std::mutex> lk(pool_.m);
                if (--pool_.busy == 0) pool_.done_cv.notify_one();
            }
        });
}

void Server::pool_stop() {
    {
        std::lock_guard<std::mutex> lk(pool_.m);
        pool_.quit = true;
    }
    pool_.cv.notify_all();
    for (auto& t : pool_.threads) t.join();
    pool_.threads.clear();
}

// runs fn over the sessions on the worker threads: each call touches only its own session (its channel and what it
// has seen) and reads the others, which nothing changes during the tick
void Server::parallel(const std::vector<Session*>& work, void (Server::*fn)(Session&, uint32_t), uint32_t now) {
    if (pool_.threads.empty() || work.size() < 64) {
        for (Session* s : work) (this->*fn)(*s, now);
        return;
    }
    std::atomic<size_t> next{0};
    auto run = [&](int) {
        for (;;) {
            size_t i = next.fetch_add(16);
            if (i >= work.size()) break;
            for (size_t k = i; k < std::min(work.size(), i + 16); ++k) (this->*fn)(*work[k], now);
        }
    };
    {
        std::lock_guard<std::mutex> lk(pool_.m);
        pool_.job = run;
        pool_.busy = int(pool_.threads.size());
        pool_.gen++;
    }
    pool_.cv.notify_all();
    run(0);
    std::unique_lock<std::mutex> lk(pool_.m);
    pool_.done_cv.wait(lk, [&] { return pool_.busy == 0; });
}

void Server::flush_all(uint32_t now) {
    std::vector<Session*> work;
    for (auto& [cid, sp] : sessions_)
        if (!sp->dead && (sp->online || sp->redirected)) work.push_back(sp.get());
    parallel(work, &Server::flush_one, now);
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
        it = now - it->second.at > 1000 ? ghosts_.erase(it) : std::next(it);

    rebuild_grid();
    std::vector<Session*> work;
    for (auto& [cid, sp] : sessions_)
        if (sp->online && !sp->redirected && !sp->dead) work.push_back(sp.get());
    uint64_t t0 = monotonic_us();
    parallel(work, &Server::interest, now);
    metrics_.interest_us.push_back(uint32_t(monotonic_us() - t0));
    runners_tick(now);
    if (tick_no_ % uint32_t(cfg_.tick_hz) == 0) party_where_tick(now);
    if (linked()) link_tick(now);
    if (now - last_areas_ >= 2000) {
        last_areas_ = now;
        area_copies(now);
        reap_children();
    }
    t0 = monotonic_us();
    flush_all(now);
    for (auto& [id, c] : tcp_) c.s->pump();  // TCP peers get this tick's datagrams now
    metrics_.send_us.push_back(uint32_t(monotonic_us() - t0));
    if ((tick_no_ % uint32_t(cfg_.tick_hz * 10)) == 0) parties_gc(now);
    // the team-state file (when configured) is written within 2 s of a change, so a crash loses little
    if (!cfg_.team_state_file.empty() && team_dirty_ && now - last_team_save_ >= 2000) {
        last_team_save_ = now;
        team_dirty_ = false;
        save_team_states();
    }
    if (restart_at_ && int32_t(now - restart_at_) >= 0) {
        log("planned restart: stopping");
        stop_ = true;
    }
    if (!cfg_.metrics_file.empty() && now - last_metrics_ >= uint32_t(cfg_.metrics_every_s) * 1000) {
        write_metrics(now);
        last_metrics_ = now;
    }
}

// ---------------------------------------------------------------- the TCP fallback

void Server::send_dg(UdpSocket& u, const Address& to, const uint8_t* d, size_t n) {
    if (!to.tcp_id) {
        u.send_to(to, d, n);
        return;
    }
    auto it = tcp_.find(to.tcp_id);
    if (it != tcp_.end()) it->second.s->send_frame(d, n);  // written by tcp_poll (main thread)
}

void Server::tcp_poll(uint32_t now) {
    // new connections
    intptr_t fd;
    Address peer;
    for (int i = 0; i < 256 && tcp_listen_.accept(fd, peer); ++i) {
        bool exempt = cfg_.loopback_exempt && peer.is_loopback();
        std::string ip = peer.ip_bytes();
        if ((!exempt && tcp_per_ip_[ip] >= cfg_.per_ip * 2) || int(tcp_.size()) >= cfg_.max_players * 2) {
            TcpStream t;
            t.adopt(fd, peer);  // closed again at once
            continue;
        }
        uint32_t id = next_tcp_++;
        if (!next_tcp_) next_tcp_ = 1;
        TcpConn c;
        c.s = std::make_unique<TcpStream>();
        c.s->adopt(fd, peer);
        c.at = c.heard = now;
        tcp_[id] = std::move(c);
        tcp_per_ip_[ip]++;
    }
    // what came in, and what is waiting to go out
    std::vector<uint32_t> gone;
    uint8_t buf[kMaxTcpFrame];
    for (auto& [id, c] : tcp_) {
        c.s->pump();
        Address from = c.s->peer();
        from.tcp_id = id;
        for (int k = 0; k < 256; ++k) {
            int n = c.s->recv_frame(buf, sizeof buf);
            if (n < 0) break;
            c.heard = now;
            metrics_.bytes_in += size_t(n) + 2;
            metrics_.packets_in++;
            on_datagram(buf, size_t(n), from, now);
        }
        // closed, or a connection that never got a session going
        if (c.s->closed() || now - c.heard > uint32_t(cfg_.silence_s) * 1000) gone.push_back(id);
    }
    for (uint32_t id : gone) tcp_closed(id, now);
}

void Server::tcp_closed(uint32_t id, uint32_t now) {
    (void)now;
    auto it = tcp_.find(id);
    if (it == tcp_.end()) return;
    std::string ip = it->second.s->peer().ip_bytes();
    if (--tcp_per_ip_[ip] <= 0) tcp_per_ip_.erase(ip);
    tcp_.erase(it);
    // its session is lost at once (kept for resume, like a timeout)
    for (auto& [cid, sp] : sessions_)
        if (!sp->dead && sp->online && sp->addr.tcp_id == id) drop(*sp, "connection closed", true);
}

// ---------------------------------------------------------------- sea copies (the safety valve)

void Server::set_sea_copy(Session& s, uint16_t copy) {
    if (copy == s.copy) return;
    if (s.area >= 0) {
        auto it = area_count_.find((uint64_t(s.area) << 16) | s.copy);
        if (it != area_count_.end() && it->second > 0) it->second--;
        area_count_[(uint64_t(s.area) << 16) | copy]++;
    }
    uint16_t copies = 1;
    for (auto& [k, n] : area_count_)
        if (s.area >= 0 && (k >> 16) == uint64_t(s.area) && n > 0) copies = std::max<uint16_t>(copies, uint16_t((k & 0xFFFF) + 1));
    if (copy + 1 > copies) {
        copies = uint16_t(copy + 1);
        metrics_.copies_opened++;
    }
    s.copy = copy;
    s.place.copy = copy;
    metrics_.copy_moves++;
    Bytes b;
    Writer w(b);
    put_where(w, s.where);
    w.u16(copy);
    w.u16(copies);
    s.chan.send_reliable(kMsgCopy, b);
}

// a player came into a 2x2 sub-cell area of the sea: keep their copy unless it is full there (or empty while others
// are around); party members go to their party's copy even when it is full
void Server::place_at_sea(Session& s, uint32_t now) {
    (void)now;
    auto count = [&](uint16_t c) {
        auto it = area_count_.find((uint64_t(s.area) << 16) | c);
        return it == area_count_.end() ? 0 : it->second;
    };
    if (Party* p = party_of(s))
        for (uint64_t kh : p->members)
            if (Session* t = online_member(kh); t && t != &s && t->has_pose && t->where.stage == s.where.stage &&
                                                 std::abs(t->gx - s.gx) <= 2 && std::abs(t->gz - s.gz) <= 2) {
                set_sea_copy(s, t->copy);
                return;
            }
    int mine = count(s.copy);
    if (mine < cfg_.area_cap && (s.copy == 0 || mine > 0)) return;
    // the fullest copy with room; a new one when all are full
    int best = -1, bn = -1;
    uint16_t top = 0;
    for (auto& [k, n] : area_count_)
        if ((k >> 16) == uint64_t(s.area)) top = std::max<uint16_t>(top, uint16_t(k & 0xFFFF));
    for (uint16_t c = 0; c <= top + 1; ++c) {
        int n = count(c);  // (the player is not counted in an area it just came into)
        if (n < cfg_.area_cap && n > bn) {
            bn = n;
            best = c;
        }
    }
    if (best >= 0) set_sea_copy(s, uint16_t(best));
}

// copies merge back: when a higher copy of an area and a lower one fit together, the higher one's players move down
void Server::area_copies(uint32_t now) {
    (void)now;
    std::unordered_map<int, std::vector<std::pair<uint16_t, int>>> per_area;
    for (auto& [k, n] : area_count_)
        if (n > 0) per_area[int(k >> 16)].push_back({uint16_t(k & 0xFFFF), n});
    for (auto& [area, list] : per_area) {
        if (list.size() < 2) continue;
        std::sort(list.begin(), list.end());
        auto [hi, hn] = list.back();
        for (auto& [lo, ln] : list) {
            if (lo >= hi || (hn + ln) * 10 > cfg_.area_cap * 8) continue;
            for (auto& [cid, sp] : sessions_)
                if (!sp->dead && sp->online && !sp->redirected && sp->area == area && sp->copy == hi &&
                    sp->where.stage_name() == cfg_.sea_stage)
                    set_sea_copy(*sp, lo);
            break;
        }
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
    auto p99 = [](std::vector<uint32_t> v) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0u : v[std::min(v.size() - 1, size_t(0.99 * double(v.size())))];
    };
    uint32_t interest_p99 = p99(metrics_.interest_us), send_p99 = p99(metrics_.send_us);
    metrics_.interest_us.clear();
    metrics_.send_us.clear();
    size_t n = players();
    double out_s = (metrics_.bytes_out - last_out) / dt, in_s = (metrics_.bytes_in - last_in) / dt;
    char line[1024];
    std::snprintf(line, sizeof line,
                  "{\"t\":%u,\"worker\":%d,\"players\":%zu,\"out_Bps\":%.0f,\"in_Bps\":%.0f,\"out_per_player_Bps\":%.0f,"
                  "\"in_per_player_Bps\":%.0f,\"tick_p50_us\":%u,\"tick_p99_us\":%u,\"tick_max_us\":%u,"
                  "\"out_churn\":%llu,\"out_gone\":%llu,\"handovers_out\":%llu,\"handovers_in\":%llu,"
                  "\"interest_p99_us\":%u,\"send_p99_us\":%u,\"threads\":%d,"
                  "\"ticks\":%zu,\"cpu_pct\":%.1f,\"maxrss_mb\":%.1f,\"dropped_rate\":%llu,\"implausible\":%llu}\n",
                  now, me_, n, out_s, in_s, n ? out_s / double(n) : 0, n ? in_s / double(n) : 0, pct(0.5), pct(0.99),
                  t.empty() ? 0u : t.back(), (unsigned long long)metrics_.out_churn.load(),
                  (unsigned long long)metrics_.out_gone.load(), (unsigned long long)metrics_.handovers_out,
                  (unsigned long long)metrics_.handovers_in, interest_p99, send_p99, workers_, t.size(), cpu_pct, double(rss) / 1024.0,
                  (unsigned long long)metrics_.dropped_rate, (unsigned long long)metrics_.implausible);
    std::string mf = cfg_.metrics_file;
    if (cfg_.worker) mf += "." + std::to_string(me_);
    if (std::FILE* f = std::fopen(mf.c_str(), "a")) {
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
