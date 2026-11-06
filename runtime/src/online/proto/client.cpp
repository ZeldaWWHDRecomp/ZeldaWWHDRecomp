// SPDX-License-Identifier: MPL-2.0
#include "client.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace online::proto {

namespace {
constexpr uint32_t kResendHandshakeMs = 500, kHandshakeTimeoutMs = 10000, kSilenceMs = 5000, kMaxBackoffS = 30;
constexpr uint32_t kPowBudgetUs = 2000;
}  // namespace

Client::Client() : sock_(&own_) {}
Client::Client(UdpSocket* shared) : sock_(shared) {}
Client::~Client() { stop(); }

bool Client::start(const ClientConfig& cfg, std::string& why) {
    stop();
    cfg_ = cfg;
    if (!resolve(cfg.host, cfg.port, server_, why)) return false;
    home_ = server_;
    home_host_ = cfg.host;
    home_port_ = cfg.port;
    if (sock_ == &own_ && !own_.is_open()) {
        // an ephemeral local port on the matching family; "0.0.0.0" so a LAN / self-hosted server can answer
        bool v6 = server_.len > 16;  // sockaddr_in is 16 bytes, sockaddr_in6 28
        if (!own_.open(v6 ? "::" : (server_.is_loopback() ? "127.0.0.1" : "0.0.0.0"), 0, why)) return false;
    }
    have_token_ = false;
    backoff_s_ = 1;
    mode_ = cfg.transport_mode;
    if (const char* e = std::getenv("WWHD_ONLINE_TCP")) {
        if (std::strcmp(e, "1") == 0) mode_ = TransportMode::kTcpOnly;
        if (std::strcmp(e, "0") == 0) mode_ = TransportMode::kUdpOnly;
    }
    if (sock_ != &own_) mode_ = TransportMode::kUdpOnly;  // a shared socket (bots) is UDP only
    transport_ = mode_ == TransportMode::kTcpOnly ? Transport::kTcp : Transport::kUdp;
    begin_connect(monotonic_ms());
    return true;
}

void Client::stop() {
    if (state_ == ClientState::kOnline) {
        chan_.send_reliable(kMsgBye, nullptr, 0);
        std::vector<Bytes> out;
        chan_.flush(monotonic_ms(), out, 2, 0);
        for (auto& b : out) send_raw(b);
    }
    if (state_ != ClientState::kOff && state_ != ClientState::kStopped) state_ = ClientState::kStopped;
    phase_ = Phase::kNone;
    if (transport_ == Transport::kTcp) tcp_.pump();  // the goodbye
    tcp_.close();
    if (sock_ == &own_) own_.close();
}

void Client::set_state(ClientState s, const std::string& text) {
    if (s == state_ && text.empty()) return;
    state_ = s;
    ClientEvent e{ClientEvent::kStateChanged};
    e.state = s;
    e.text = text;
    events_.push_back(std::move(e));
}

void Client::begin_connect(uint32_t now) {
    crypto::random_bytes(&nonce_, sizeof nonce_);
    if (!nonce_) nonce_ = 1;
    phase_ = Phase::kConnect;
    phase_at_ = 0;
    phase_started_ = now;
    pow_found_ = false;
    if (transport_ == Transport::kTcp) {
        std::string why;
        tcp_.connect(server_, why);  // a fresh connection for each attempt (a failure shows as no answer)
    }
    ClientState s = have_token_ ? ClientState::kReconnecting : ClientState::kConnecting;
    if (s != state_) set_state(s, "");
}

void Client::send_raw(const Bytes& b) {
    if (transport_ == Transport::kTcp) {
        if (tcp_.send_frame(b.data(), b.size())) {
            stats_.bytes_up += b.size() + 2;
            stats_.packets_up++;
        }
        tcp_.pump();
        return;
    }
    if (sock_->send_to(server_, b.data(), b.size())) {
        stats_.bytes_up += b.size();
        stats_.packets_up++;
    }
}

void Client::lost(uint32_t now, const std::string& why) {
    phase_ = Phase::kNone;
    pose_pending_ = false;
    // a worker we were redirected to is gone: back to the server's front door (the session resumes there or anew)
    if (!(server_ == home_)) {
        server_ = home_;
        cfg_.host = home_host_;
        cfg_.port = home_port_;
    }
    if (!cfg_.reconnect) {
        set_state(ClientState::kStopped, why);
        return;
    }
    retry_at_ = now + backoff_s_ * 1000;
    backoff_s_ = std::min(backoff_s_ * 2, kMaxBackoffS);
    set_state(ClientState::kReconnecting, why);
}

void Client::step_pow(uint32_t now) {
    uint64_t t0 = monotonic_us();
    for (;;) {
        for (int i = 0; i < 256; ++i, ++pow_next_)
            if (pow_check(chal_.salt, cfg_.identity.pub, pow_next_, chal_.difficulty)) {
                pow_found_ = true;
                break;
            }
        if (pow_found_ || monotonic_us() - t0 > kPowBudgetUs) break;
    }
    if (!pow_found_) return;
    eph_ = crypto::Ephemeral::generate();
    Hello h;
    h.nonce = nonce_;
    h.game_data = cfg_.game_data;
    std::memcpy(h.salt, chal_.salt, 16);
    h.bucket = chal_.bucket;
    std::memcpy(h.eph, eph_.pub, 32);
    h.pow = pow_next_;
    h.name = cfg_.name;
    h.look = cfg_.look;
    h.device = cfg_.device;
    h.near_cap = cfg_.near_cap;
    h.mid_cap = cfg_.mid_cap;
    h.resume = have_token_;
    if (have_token_) std::memcpy(h.token, token_, 32);
    h.tag = cfg_.tag;
    hello_ = encode_hello(h, cfg_.identity);
    phase_ = Phase::kHello;
    phase_started_ = now;
    phase_at_ = 0;
}

const std::vector<ClientEvent>& Client::poll(uint32_t now) {
    // what on_datagram() produced since the last poll comes first
    out_.clear();
    out_.swap(events_);
    if (transport_ == Transport::kTcp) {
        tcp_.pump();
        uint8_t buf[kMaxTcpFrame];
        for (int i = 0; i < 512; ++i) {
            int n = tcp_.recv_frame(buf, sizeof buf);
            if (n < 0) break;
            on_datagram(buf, size_t(n), now);
        }
        if (tcp_.closed() && state_ == ClientState::kOnline) lost(now, "connection closed");
        else if (tcp_.closed() && phase_ != Phase::kNone) lost(now, "cannot reach the server over TCP");
    } else if (sock_ == &own_) {
        uint8_t buf[2048];
        Address from;
        for (int i = 0; i < 512; ++i) {
            int n = own_.recv_from(from, buf, sizeof buf);
            if (n < 0) break;
            if (!(from == server_)) continue;  // only the server talks to us
            on_datagram(buf, size_t(n), now);
        }
    }
    switch (phase_) {
        case Phase::kConnect:
            // no answer over UDP for 3 s: the network may block it; try TCP to the same address
            if (mode_ == TransportMode::kAuto && transport_ == Transport::kUdp && now - phase_started_ >= 3000) {
                transport_ = Transport::kTcp;
                begin_connect(now);
                break;
            }
            if (now - phase_started_ > kHandshakeTimeoutMs) {
                lost(now, "no answer from the server");
                break;
            }
            if (!phase_at_ || now - phase_at_ >= kResendHandshakeMs) {
                Connect c;
                c.nonce = nonce_;
                send_raw(encode_connect(c));
                phase_at_ = now;
            }
            break;
        case Phase::kPow:
            step_pow(now);
            if (phase_ != Phase::kHello) break;
            [[fallthrough]];
        case Phase::kHello:
            if (now - phase_started_ > kHandshakeTimeoutMs) {
                lost(now, "no answer from the server");
                break;
            }
            if (!phase_at_ || now - phase_at_ >= kResendHandshakeMs) {
                send_raw(hello_);
                phase_at_ = now;
            }
            break;
        case Phase::kNone:
            if (state_ == ClientState::kReconnecting && int32_t(now - retry_at_) >= 0) begin_connect(now);
            break;
    }
    if (state_ == ClientState::kOnline) {
        if (now - chan_.last_heard_ms() > kSilenceMs) {
            lost(now, "connection lost");
        } else if (now - last_flush_ >= uint32_t(1000 / std::max(1, cfg_.send_hz))) {
            last_flush_ = now;
            if (pose_pending_) {
                chan_.send_unreliable(kMsgPose, pose_);
                pose_pending_ = false;
            }
            outq_.clear();
            chan_.flush(now, outq_, 4);
            for (auto& b : outq_) send_raw(b);
            if (chan_.broken()) lost(now, "protocol error");
        }
    }
    for (auto& e : events_) out_.push_back(std::move(e));
    events_.clear();
    return out_;
}

void Client::on_datagram(const uint8_t* d, size_t n, uint32_t now) {
    stats_.bytes_down += n;
    stats_.packets_down++;
    handle(d, n, now);
}

void Client::handle(const uint8_t* d, size_t n, uint32_t now) {
    if (n < 1) return;
    switch (d[0]) {
        case kPktChallenge: {
            if (phase_ != Phase::kConnect) return;
            Challenge c;
            if (!decode_challenge(d, n, c) || c.nonce != nonce_) return;
            if (c.proto != kProtoVersion) {
                ClientEvent e{ClientEvent::kRefused};
                e.refusal = Refusal::kProtocol;
                e.text = refusal_text(Refusal::kProtocol);
                events_.push_back(std::move(e));
                phase_ = Phase::kNone;
                set_state(ClientState::kStopped, e.text);
                return;
            }
            if (cfg_.pinned_server && std::memcmp(cfg_.pinned_server->data(), c.info.pub, 32) != 0) {
                ClientEvent e{ClientEvent::kServerKeyChanged};
                e.info = c.info;
                e.text = "the server's key changed (now " + fingerprint(c.info.pub) + ")";
                events_.push_back(e);
                phase_ = Phase::kNone;
                set_state(ClientState::kStopped, e.text);
                return;
            }
            bool first = std::memcmp(info_.pub, c.info.pub, 32) != 0;
            chal_ = c;
            info_ = c.info;
            if (first) {
                ClientEvent e{ClientEvent::kServerKey};
                e.info = c.info;
                e.text = fingerprint(c.info.pub);
                events_.push_back(std::move(e));
            }
            phase_ = Phase::kPow;
            crypto::random_bytes(&pow_next_, sizeof pow_next_);
            pow_next_ &= 0xFFFFFFFFull;
            break;
        }
        case kPktWelcome: {
            if (phase_ != Phase::kHello) return;
            Welcome w;
            if (!decode_welcome_outer(d, n, w) || w.nonce != nonce_) return;
            if (!crypto::session_keys(eph_, w.eph, true, tx_, rx_)) return;
            if (!decode_welcome_inner(d, n, info_.pub, eph_.pub, rx_, w)) return;
            chan_.setup(w.conn_id, tx_, rx_, true, now);
            my_id_ = w.id;
            std::memcpy(token_, w.token, 32);
            have_token_ = true;
            backoff_s_ = 1;
            phase_ = Phase::kNone;
            set_state(ClientState::kOnline, "");
            ClientEvent e{ClientEvent::kWelcome};
            e.id = w.id;
            e.resumed = w.resumed;
            events_.push_back(std::move(e));
            break;
        }
        case kPktRefuse: {
            if (phase_ != Phase::kConnect && phase_ != Phase::kHello) return;
            Refuse f;
            if (!decode_refuse(d, n, f) || f.nonce != nonce_) return;
            ClientEvent e{ClientEvent::kRefused};
            e.refusal = f.reason;
            e.text = f.text.empty() ? refusal_text(f.reason) : f.text;
            e.seconds = f.retry_s;
            events_.push_back(e);
            bool temporary = f.reason == Refusal::kFull || f.reason == Refusal::kRateLimited ||
                             f.reason == Refusal::kShuttingDown;
            if (temporary && cfg_.reconnect) {
                phase_ = Phase::kNone;
                retry_at_ = now + std::max<uint32_t>(f.retry_s, backoff_s_) * 1000;
                backoff_s_ = std::min(backoff_s_ * 2, kMaxBackoffS);
                set_state(ClientState::kReconnecting, e.text);
            } else {
                phase_ = Phase::kNone;
                have_token_ = false;
                set_state(ClientState::kStopped, e.text);
            }
            break;
        }
        case kPktData: {
            if (state_ != ClientState::kOnline) return;
            std::vector<Delivered> got;
            if (!chan_.receive(d, n, now, got)) return;
            for (auto& m : got) handle_message(m);
            break;
        }
        default: break;
    }
}

void Client::handle_message(const Delivered& m) {
    const uint8_t* d = m.body.data();
    size_t n = m.body.size();
    Reader r(d, n);
    switch (m.type) {
        case kMsgPoseBatch: {
            ClientEvent e{ClientEvent::kPoses};
            if (get_pose_batch(d, n, e.poses)) events_.push_back(std::move(e));
            break;
        }
        case kMsgPlayerIn: {
            ClientEvent e{ClientEvent::kPlayerIn};
            if (decode_player_in(d, n, e.player)) {
                e.id = e.player.id;
                events_.push_back(std::move(e));
            }
            break;
        }
        case kMsgPlayerOut: {
            ClientEvent e{ClientEvent::kPlayerOut};
            e.id = r.u16();
            if (r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgMarkers: {
            ClientEvent e{ClientEvent::kMarkers};
            if (get_markers(d, n, e.markers)) events_.push_back(std::move(e));
            break;
        }
        case kMsgEmoteOf: {
            ClientEvent e{ClientEvent::kEmote};
            e.id = r.u16();
            e.value = r.u8();
            if (r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgAppFrom: {
            ClientEvent e{ClientEvent::kApp};
            e.id = r.u16();
            e.app_type = r.u8();
            if (!r.ok()) break;
            e.body.assign(r.here(), r.here() + r.left());
            e.reliable = m.reliable;
            events_.push_back(std::move(e));
            break;
        }
        case kMsgNotice: {
            ClientEvent e{ClientEvent::kNotice};
            e.value = r.u8();
            e.seconds = r.u16();
            e.text = r.str8(200);
            if (!r.done()) break;
            bool out = e.value == kNoticeKick || e.value == kNoticeBan || e.value == kNoticeKickLimit;
            std::string text = e.text;
            events_.push_back(std::move(e));
            if (out) {
                // removed by the server: no automatic reconnect (the player may try again by hand)
                phase_ = Phase::kNone;
                have_token_ = false;
                set_state(ClientState::kStopped, text);
            }
            break;
        }
        case kMsgCopy: {
            ClientEvent e{ClientEvent::kCopy};
            e.where = get_where(r);
            e.copy = r.u16();
            e.copies = r.u16();
            if (r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgPartyInvited: {
            ClientEvent e{ClientEvent::kPartyInvited};
            e.party_id = r.u32();
            e.id = r.u16();
            e.player.key_hash = r.u64();
            e.player.name = r.str8(24);
            if (r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgPartyState: {
            ClientEvent e{ClientEvent::kPartyState};
            if (decode_party_state(d, n, e.party)) events_.push_back(std::move(e));
            break;
        }
        case kMsgRunner: {
            ClientEvent e{ClientEvent::kRunners};
            if (decode_runners(d, n, e.runners)) events_.push_back(std::move(e));
            break;
        }
        case kMsgTeamState: {
            ClientEvent e{ClientEvent::kTeamState};
            e.version = r.u32();
            if (get_team_fields(r, e.fields) && r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgPartyResult: {
            ClientEvent e{ClientEvent::kPartyResult};
            e.value = r.u8();
            e.text = r.str8(200);
            if (r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgPartyWhere: {
            ClientEvent e{ClientEvent::kPartyWhere};
            auto s24 = [&] {
                uint32_t u = r.u8();
                u |= uint32_t(r.u8()) << 8;
                u |= uint32_t(r.u8()) << 16;
                if (u & 0x800000) u |= 0xFF000000u;
                return int32_t(u);
            };
            while (r.ok() && r.left() && e.members.size() < size_t(kMaxPartySize)) {
                ClientEvent::MemberWhere m;
                m.id = r.u16();
                m.where = get_where(r);
                m.square = r.u8();
                m.x = s24();
                m.z = s24();
                if (r.ok()) e.members.push_back(m);
            }
            if (r.done()) events_.push_back(std::move(e));
            break;
        }
        case kMsgRedirect: {
            // shard handover: reconnect to the named shard with the ticket as the resume token
            std::string host = r.str8(64);
            uint16_t port = r.u16();
            uint8_t ticket[32];
            r.bytes(ticket, 32);
            if (!r.done()) break;
            std::string why;
            Address a;
            if (!resolve(host.empty() ? cfg_.host : host, port, a, why)) break;
            server_ = a;
            cfg_.port = port;
            if (!host.empty()) cfg_.host = host;
            std::memcpy(token_, ticket, 32);
            have_token_ = true;
            begin_connect(monotonic_ms());  // (reports kReconnecting: the handover is under way)
            break;
        }
        default: break;  // unknown types are ignored
    }
}

void Client::send_pose(const PoseHeader& head, const uint8_t* blob, size_t n) {
    if (state_ != ClientState::kOnline || n > kMaxPoseBlob) return;
    pose_.clear();
    Writer w(pose_);
    put_pose_header(w, head);
    w.bytes(blob, n);
    pose_pending_ = true;
}

void Client::send_stage(const Where& where) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer w(b);
    put_where(w, where);
    chan_.send_reliable(kMsgStage, b);
}

void Client::send_app(const AppHeader& h, const uint8_t* body, size_t n, bool reliable) {
    if (state_ != ClientState::kOnline) return;
    if (n > (reliable ? kMaxStream : kMaxAppBody)) return;
    Bytes b = encode_app(h, body, n);
    if (reliable)
        chan_.send_reliable(kMsgApp, b);
    else
        chan_.send_unreliable(kMsgApp, b);
}

void Client::send_emote(uint8_t emote) {
    if (state_ == ClientState::kOnline) chan_.send_reliable(kMsgEmote, &emote, 1);
}

void Client::block(uint64_t kh, bool on) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer(b).u64(kh);
    chan_.send_reliable(on ? kMsgBlock : kMsgUnblock, b);
}

void Client::report(uint64_t kh, uint8_t reason) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer w(b);
    w.u64(kh);
    w.u8(reason);
    chan_.send_reliable(kMsgReport, b);
}

void Client::switch_copy() {
    if (state_ == ClientState::kOnline) chan_.send_reliable(kMsgSwitchCopy, nullptr, 0);
}

void Client::join_copy_of(uint64_t kh) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer(b).u64(kh);
    chan_.send_reliable(kMsgJoinCopyOf, b);
}

void Client::party_invite(uint64_t code40) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer(b).u64(code40);
    chan_.send_reliable(kMsgPartyInvite, b);
}

void Client::party_accept(uint32_t party) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer(b).u32(party);
    chan_.send_reliable(kMsgPartyAccept, b);
}

void Client::party_decline(uint32_t party) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer(b).u32(party);
    chan_.send_reliable(kMsgPartyDecline, b);
}

void Client::party_leave() {
    if (state_ == ClientState::kOnline) chan_.send_reliable(kMsgPartyLeave, nullptr, 0);
}

void Client::party_settings(uint16_t flags) {
    if (state_ != ClientState::kOnline) return;
    Bytes b;
    Writer(b).u16(flags);
    chan_.send_reliable(kMsgPartySettings, b);
}

void Client::pin_runner(bool on) {
    if (state_ != ClientState::kOnline) return;
    uint8_t v = on ? 1 : 0;
    chan_.send_reliable(kMsgPinRunner, &v, 1);
}

void Client::send_progress(const std::vector<TeamField>& fields) {
    if (state_ != ClientState::kOnline || fields.empty()) return;
    Bytes b;
    Writer w(b);
    put_team_fields(w, fields);
    chan_.send_reliable(kMsgProgress, b);
}

void Client::party_restore(const PartyRecord& record) {
    if (state_ != ClientState::kOnline || record.members.size() < 1) return;
    chan_.send_reliable(kMsgPartyRestore, encode_party_record(record));
}

void Client::ask_team_state() {
    if (state_ == ClientState::kOnline) chan_.send_reliable(kMsgTeamStateAsk, nullptr, 0);
}

int Client::ping_ms() const { return state_ == ClientState::kOnline ? chan_.rtt_ms() : -1; }
uint64_t Client::key_hash() const { return proto::key_hash(cfg_.identity.pub); }
std::string Client::friend_code() const { return proto::friend_code(key_hash()); }

std::string Client::status_line() const {
    switch (state_) {
        case ClientState::kOff: return "Offline";
        case ClientState::kStopped: return "Offline";
        case ClientState::kConnecting: return "Connecting…";
        case ClientState::kReconnecting: return "Online: reconnecting…";
        case ClientState::kOnline: {
            std::string s = "Online · " + (info_.name.empty() ? std::string("server") : info_.name);
            if (transport_ == Transport::kTcp) s += " (TCP)";
            if (ping_ms() >= 0) s += " · " + std::to_string(ping_ms()) + " ms";
            return s;
        }
    }
    return "";
}

}  // namespace online::proto
