// SPDX-License-Identifier: MPL-2.0
#include "channel.h"

#include <algorithm>
#include <cstring>

namespace online::proto {

namespace {
constexpr uint32_t kWindow = 1024;        // reliable messages in flight / held ahead
constexpr size_t kReliableFrame = 6;      // kind, seq 2, type, len 2
constexpr size_t kUnreliableFrame = 4;    // kind, type, len 2
constexpr size_t kTimes = 10;             // my ms, echo ms, hold
constexpr size_t kBodyRoom = kMaxDatagram - kHeaderBytes - kTagBytes;

uint64_t nonce_of(uint32_t pkt, bool from_client) { return uint64_t(pkt) | (from_client ? (1ull << 63) : 0); }
}  // namespace

bool read_header(const uint8_t* d, size_t n, PacketHeader& h) {
    Reader r(d, n);
    h.kind = r.u8();
    h.conn_id = r.u32();
    h.pkt = r.u32();
    h.ack = r.u32();
    h.ack_bits = r.u32();
    return r.ok();
}

void write_header(Bytes& out, const PacketHeader& h) {
    Writer w(out);
    w.u8(h.kind);
    w.u32(h.conn_id);
    w.u32(h.pkt);
    w.u32(h.ack);
    w.u32(h.ack_bits);
}

void Channel::setup(uint32_t conn_id, const uint8_t tx[32], const uint8_t rx[32], bool i_am_client, uint32_t now) {
    *this = Channel();
    conn_id_ = conn_id;
    std::memcpy(tx_, tx, 32);
    std::memcpy(rx_, rx, 32);
    client_ = i_am_client;
    last_heard_ = now;
    last_send_ms_ = now;
}

bool Channel::send_reliable(uint8_t type, const uint8_t* d, size_t n) {
    if (type & 0x80) return false;
    if (n > kMaxStream || out_bytes_ + n > max_reliable_queue_bytes() || out_.size() + n / kPieceBytes + 1 > queue_msgs_)
        return false;
    size_t at = 0;
    do {
        size_t take = std::min(n - at, kPieceBytes);
        OutMsg m;
        m.seq = next_seq_++;
        m.type = uint8_t(type | (at + take < n ? 0x80 : 0));
        m.body.assign(d + at, d + at + take);
        out_bytes_ += take;
        out_.push_back(std::move(m));
        at += take;
    } while (at < n);
    return true;
}

void Channel::send_unreliable(uint8_t type, const uint8_t* d, size_t n) {
    if (n + kUnreliableFrame > kBodyRoom - kTimes) {
        ++stats_.unreliable_dropped;
        return;
    }
    unreliable_.emplace_back(type, Bytes(d, d + n));
}

void Channel::flush(uint32_t now, std::vector<Bytes>& out, size_t max_datagrams, uint32_t keepalive_ms) {
    uint32_t rto = rtt_ms_ < 0 ? 200u : uint32_t(std::clamp(rtt_ms_ * 3 / 2 + 30, 50, 1000));
    // what is due: reliable messages never sent or past their resend time, within the window
    std::vector<OutMsg*> due;
    uint32_t window_end = out_.empty() ? 0 : out_.front().seq + kWindow;
    for (auto& m : out_) {
        if (m.seq >= window_end) break;
        if (m.acked) continue;
        if (!m.sent || now - m.last_sent >= rto) due.push_back(&m);
    }
    size_t ui = 0;
    size_t ri = 0;
    size_t made = 0;
    bool keepalive = ack_due_ || now - last_send_ms_ >= keepalive_ms;
    while (made < max_datagrams && (ri < due.size() || ui < unreliable_.size() || keepalive)) {
        keepalive = false;
        Bytes body;
        Writer w(body);
        w.u32(now);
        w.u32(peer_ms_);
        w.u16(uint16_t(std::min<uint32_t>(now - peer_ms_at_, 65535)));
        SentRec rec;
        rec.pkt = next_pkt_;
        while (ri < due.size() && body.size() + kReliableFrame + due[ri]->body.size() <= kBodyRoom) {
            OutMsg& m = *due[ri++];
            w.u8(kFrameReliable);
            w.u16(uint16_t(m.seq));
            w.u8(m.type);
            w.u16(uint16_t(m.body.size()));
            w.bytes(m.body.data(), m.body.size());
            if (m.sent) ++stats_.resends;
            m.sent = true;
            m.last_sent = now;
            rec.seqs.push_back(m.seq);
        }
        // after a burst from the peer, the first packet says which of it came (the header covers only 33)
        if (made == 0 && recv_since_flush_.size() > 30) {
            size_t room = kBodyRoom - body.size();
            if (room > 64) put_burst_acks(w, std::min<size_t>(room / 2, 600));
            recv_since_flush_.clear();
        }
        while (ui < unreliable_.size() &&
               body.size() + kUnreliableFrame + unreliable_[ui].second.size() <= kBodyRoom) {
            auto& u = unreliable_[ui++];
            w.u8(kFrameUnreliable);
            w.u8(u.first);
            w.u16(uint16_t(u.second.size()));
            w.bytes(u.second.data(), u.second.size());
        }
        PacketHeader h;
        h.kind = kPktData;
        h.conn_id = conn_id_;
        h.pkt = next_pkt_++;
        h.ack = top_pkt_;
        for (uint32_t i = 0; i < 32; ++i) {
            uint32_t p = top_pkt_ - 1 - i;
            if (top_pkt_ > i + 1 && seen_[p % 1024] == p) h.ack_bits |= 1u << i;
        }
        Bytes dg;
        dg.reserve(kHeaderBytes + body.size() + kTagBytes);
        write_header(dg, h);
        uint8_t tag[kTagBytes];
        crypto::seal(tx_, nonce_of(h.pkt, client_), dg.data(), kHeaderBytes, body.data(), body.size(), tag);
        dg.insert(dg.end(), body.begin(), body.end());
        dg.insert(dg.end(), tag, tag + kTagBytes);
        sent_[h.pkt % 1024] = std::move(rec);
        stats_.packets_out++;
        stats_.bytes_out += dg.size();
        out.push_back(std::move(dg));
        ++made;
        // a datagram that could take nothing more ends the loop when only oversized items remain
        if (ri < due.size() && kReliableFrame + due[ri]->body.size() > kBodyRoom - kTimes) ++ri;
    }
    stats_.unreliable_dropped += unreliable_.size() - ui;
    unreliable_.clear();
    if (made) {
        last_send_ms_ = now;
        ack_due_ = false;
    }
}

void Channel::on_ack(uint32_t pkt) {
    SentRec& r = sent_[pkt % 1024];
    if (r.pkt != pkt) return;
    for (uint32_t s : r.seqs) {
        // the queue is ordered by seq; find it from the front
        if (out_.empty() || s < out_.front().seq) continue;
        size_t i = s - out_.front().seq;
        if (i < out_.size() && out_[i].seq == s) out_[i].acked = true;
    }
    r.seqs.clear();
    r.pkt = 0;
    while (!out_.empty() && out_.front().acked) {
        out_bytes_ -= out_.front().body.size();
        out_.pop_front();
    }
}

// the burst's packet numbers as 64-packet bitmaps (12 bytes each), the newest first, within `room` bytes
void Channel::put_burst_acks(Writer& w, size_t room) {
    std::map<uint32_t, uint64_t, std::greater<uint32_t>> runs;
    for (uint32_t p : recv_since_flush_) runs[p & ~63u] |= uint64_t(1) << (p & 63);
    size_t n = std::min(runs.size(), std::min<size_t>((room - 2) / 12, 255));
    if (!n) return;
    w.u8(kFrameAcks);
    w.u8(uint8_t(n));
    size_t i = 0;
    for (auto& [base, bits] : runs) {
        if (i++ == n) break;
        w.u32(base);
        w.u64(bits);
    }
}

uint32_t Channel::expand_seq(uint16_t wire) const {
    // the full sequence closest to the next expected one
    uint32_t base = next_in_seq_;
    uint32_t cand = (base & ~0xFFFFu) | wire;
    if (cand + 0x8000u < base) cand += 0x10000u;
    else if (cand > base + 0x8000u && cand >= 0x10000u) cand -= 0x10000u;
    return cand;
}

void Channel::take_reliable(uint32_t seq, uint8_t type, const uint8_t* d, size_t n, std::vector<Delivered>& got) {
    auto deliver = [&](uint8_t t, const uint8_t* p, size_t len) {
        bool more = t & 0x80;
        if (in_stream_ || more) {
            if (!in_stream_) {
                stream_.clear();
                stream_type_ = uint8_t(t & 0x7F);
                in_stream_ = true;
            }
            if ((t & 0x7F) != stream_type_ || stream_.size() + len > kMaxStream) {
                broken_ = true;
                return;
            }
            stream_.insert(stream_.end(), p, p + len);
            if (!more) {
                got.push_back({true, stream_type_, std::move(stream_)});
                stream_.clear();
                in_stream_ = false;
            }
            return;
        }
        got.push_back({true, t, Bytes(p, p + len)});
    };
    if (seq < next_in_seq_) return;  // duplicate
    if (seq >= next_in_seq_ + kWindow) {
        broken_ = true;  // a peer that runs that far ahead is not following the protocol
        return;
    }
    if (seq != next_in_seq_) {
        ahead_.try_emplace(seq, type, Bytes(d, d + n));
        return;
    }
    deliver(type, d, n);
    ++next_in_seq_;
    for (auto it = ahead_.begin(); it != ahead_.end() && it->first == next_in_seq_;) {
        deliver(it->second.first, it->second.second.data(), it->second.second.size());
        ++next_in_seq_;
        it = ahead_.erase(it);
    }
}

bool Channel::receive(const uint8_t* d, size_t n, uint32_t now, std::vector<Delivered>& got) {
    PacketHeader h;
    if (n < kHeaderBytes + kTagBytes + kTimes || n > kMaxDatagram || !read_header(d, n, h) || h.kind != kPktData ||
        h.conn_id != conn_id_) {
        ++stats_.bad_packets;
        return false;
    }
    // replay / too old
    if (h.pkt == 0 || (top_pkt_ >= 1024 && h.pkt <= top_pkt_ - 1024) || seen_[h.pkt % 1024] == h.pkt) {
        ++stats_.dup_packets;
        return false;
    }
    size_t body_n = n - kHeaderBytes - kTagBytes;
    Bytes body(d + kHeaderBytes, d + kHeaderBytes + body_n);
    if (!crypto::open(rx_, nonce_of(h.pkt, !client_), d, kHeaderBytes, body.data(), body.size(), d + n - kTagBytes)) {
        ++stats_.bad_packets;
        return false;
    }
    seen_[h.pkt % 1024] = h.pkt;
    if (recv_since_flush_.size() < 4096) recv_since_flush_.push_back(h.pkt);
    if (h.pkt > top_pkt_) top_pkt_ = h.pkt;
    ack_due_ = true;
    last_heard_ = now;
    stats_.packets_in++;
    stats_.bytes_in += n;

    // acks
    if (h.ack) on_ack(h.ack);
    for (uint32_t i = 0; i < 32; ++i)
        if ((h.ack_bits >> i & 1) && h.ack > i + 1) on_ack(h.ack - 1 - i);

    Reader r(body);
    uint32_t their_ms = r.u32();
    uint32_t echo = r.u32();
    uint32_t hold = r.u16();
    if (int32_t(their_ms - peer_ms_) > 0 || peer_ms_at_ == 0) {
        peer_ms_ = their_ms;
        peer_ms_at_ = now;
    }
    if (echo) {
        int64_t sample = int64_t(int32_t(now - echo)) - int64_t(hold);
        if (sample >= 0 && sample < 10000) rtt_ms_ = rtt_ms_ < 0 ? int(sample) : (rtt_ms_ * 7 + int(sample)) / 8;
    }
    while (r.ok() && r.left()) {
        uint8_t kind = r.u8();
        if (kind == kFrameReliable) {
            uint16_t s = r.u16();
            uint8_t type = r.u8();
            uint16_t len = r.u16();
            const uint8_t* p = r.here();
            r.skip(len);
            if (!r.ok() || len > kPieceBytes) break;
            take_reliable(expand_seq(s), type, p, len, got);
        } else if (kind == kFrameUnreliable) {
            uint8_t type = r.u8();
            uint16_t len = r.u16();
            const uint8_t* p = r.here();
            r.skip(len);
            if (!r.ok()) break;
            got.push_back({false, type, Bytes(p, p + len)});
        } else if (kind == kFrameAcks) {
            size_t n = r.u8();
            for (size_t i = 0; i < n && r.ok(); ++i) {
                uint32_t base = r.u32();
                uint64_t bits = r.u64();
                if (!r.ok()) break;
                for (int b = 0; b < 64; ++b)
                    if (bits >> b & 1) on_ack(base + uint32_t(b));
            }
        } else {
            break;
        }
    }
    return true;
}

}  // namespace online::proto
