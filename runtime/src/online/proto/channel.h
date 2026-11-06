// SPDX-License-Identifier: MPL-2.0
// One session's packet layer, socket-free so tests can drop, duplicate and reorder datagrams:
//  - each data packet: header (kind, connection id, packet number, newest packet seen, 32 earlier ones as bits),
//    then a sealed body (crypto.h) with the sender's clock, an echo of the peer's and the frames;
//  - reliable ordered messages are resent until a packet that carried them is acked (the Quake 3 scheme the fork
//    uses, with a resend timeout from the measured round trip instead of every tick); a message longer than one
//    piece goes in pieces and is put back together (up to kMaxStream);
//  - unreliable messages go once, in the next flush, if they fit.
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include "crypto.h"
#include "protocol.h"
#include "wire.h"

namespace online::proto {

struct Delivered {
    bool reliable = false;
    uint8_t type = 0;
    Bytes body;
};

struct ChannelStats {
    uint64_t packets_out = 0, packets_in = 0, bytes_out = 0, bytes_in = 0;
    uint64_t resends = 0, bad_packets = 0, dup_packets = 0, unreliable_dropped = 0;
};

class Channel {
public:
    void setup(uint32_t conn_id, const uint8_t tx[32], const uint8_t rx[32], bool i_am_client, uint32_t now_ms);
    uint32_t conn_id() const { return conn_id_; }

    // queue a reliable message (split in pieces if longer than kPieceBytes); false if the queue is over its cap
    bool send_reliable(uint8_t type, const uint8_t* d, size_t n);
    bool send_reliable(uint8_t type, const Bytes& b) { return send_reliable(type, b.data(), b.size()); }
    // an unreliable message for the next flush (dropped if it does not fit)
    void send_unreliable(uint8_t type, const uint8_t* d, size_t n);
    void send_unreliable(uint8_t type, const Bytes& b) { send_unreliable(type, b.data(), b.size()); }

    // builds the datagrams due now (at most max_datagrams); sends at least a keep-alive every keepalive_ms
    void flush(uint32_t now_ms, std::vector<Bytes>& out, size_t max_datagrams = 8, uint32_t keepalive_ms = 250);

    // a data datagram from the peer: false if it is not valid for this session (wrong id, tag, replay...)
    bool receive(const uint8_t* d, size_t n, uint32_t now_ms, std::vector<Delivered>& got);

    int rtt_ms() const { return rtt_ms_; }
    uint32_t last_heard_ms() const { return last_heard_; }
    size_t reliable_queued() const { return out_.size(); }
    size_t reliable_bytes_queued() const { return out_bytes_; }
    const ChannelStats& stats() const { return stats_; }
    bool broken() const { return broken_; }  // the peer broke the protocol (oversized stream...)

    static size_t max_reliable_queue_bytes() { return 512 * 1024; }

private:
    struct OutMsg {
        uint32_t seq;
        uint8_t type;  // with 0x80 when more pieces follow
        Bytes body;
        uint32_t last_sent = 0;
        bool sent = false, acked = false;
    };
    struct SentRec {
        uint32_t pkt = 0;
        std::vector<uint32_t> seqs;
    };

    void on_ack(uint32_t pkt);
    uint32_t expand_seq(uint16_t wire) const;
    void take_reliable(uint32_t seq, uint8_t type, const uint8_t* d, size_t n, std::vector<Delivered>& got);

    uint32_t conn_id_ = 0;
    uint8_t tx_[32]{}, rx_[32]{};
    bool client_ = false;

    // outgoing
    uint32_t next_pkt_ = 1, next_seq_ = 0;
    std::deque<OutMsg> out_;
    size_t out_bytes_ = 0;
    std::vector<std::pair<uint8_t, Bytes>> unreliable_;
    std::array<SentRec, 1024> sent_{};
    uint32_t last_send_ms_ = 0;
    bool ack_due_ = false;

    // incoming
    uint32_t top_pkt_ = 0;  // newest packet number taken
    std::array<uint32_t, 1024> seen_{};
    uint32_t next_in_seq_ = 0;
    std::map<uint32_t, std::pair<uint8_t, Bytes>> ahead_;
    Bytes stream_;  // pieces of a split message so far
    uint8_t stream_type_ = 0;
    bool in_stream_ = false;

    // clock
    uint32_t peer_ms_ = 0, peer_ms_at_ = 0, last_heard_ = 0;
    int rtt_ms_ = -1;

    ChannelStats stats_;
    bool broken_ = false;
};

// header helpers (also used by the handshake packets)
struct PacketHeader {
    uint8_t kind = 0;
    uint32_t conn_id = 0, pkt = 0, ack = 0, ack_bits = 0;
};
bool read_header(const uint8_t* d, size_t n, PacketHeader& h);
void write_header(Bytes& out, const PacketHeader& h);

}  // namespace online::proto
