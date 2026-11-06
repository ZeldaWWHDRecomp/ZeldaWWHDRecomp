// SPDX-License-Identifier: MPL-2.0
// Automatic scaling per sea square (docs/online-server.md, "Scaling"): the region map, the link messages between
// the coordinator and its workers, and their encodings.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "proto/protocol.h"
#include "proto/wire.h"

namespace online::server {

// the sea in sub-cells: 7 x 7 squares of 8 x 8
constexpr int kGrid = proto::kSeaSquares * proto::kSubCells;  // 56

// a rectangle of sub-cells [x0, x1) x [z0, z1) of the sea, or (stage non-empty) one interior stage with all its
// copies, run by one worker
struct Region {
    uint32_t id = 0, parent = 0;  // parent: the region it was split from (0: an original square or stage)
    int16_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    std::string stage;
    int worker = 0;
    bool pinned = false;  // placed by the operator (admin move / split): the coordinator does not move or merge it
    // the safety valve beyond the smallest region: copy k of this region runs on copy_worker[k-1] (copy 0 and any
    // copy past the list run on `worker`)
    std::vector<uint8_t> copy_worker;
    int worker_of_copy(int copy) const {
        return copy >= 1 && size_t(copy) <= copy_worker.size() ? copy_worker[size_t(copy - 1)] : worker;
    }
    int cells() const { return (x1 - x0) * (z1 - z0); }
    bool contains(int gx, int gz) const { return stage.empty() && gx >= x0 && gx < x1 && gz >= z0 && gz < z1; }
};

struct WorkerInfo {
    int index = 0;
    std::string host;        // clients reach it here (redirects)
    uint16_t port = 0;
    std::string link_host;
    uint16_t link_port = 0;
    int cores = 1;
    bool alive = false;
};

struct RegionMap {
    uint32_t version = 0;
    std::vector<Region> regions;
    std::vector<WorkerInfo> workers;  // by index
    uint32_t next_region = 1;

    // the 49 squares, all on worker 0
    void init_squares();
    void rebuild();  // after any change: the cell index
    int region_at(int gx, int gz) const;  // index into regions, -1 outside
    int owner_at(int gx, int gz, int copy = 0) const;
    int owner_of_stage(const std::string& stage) const;  // -1: no region, the coordinator (0) runs it
    const WorkerInfo* worker(int index) const;

    proto::Bytes encode() const;
    bool decode(const uint8_t* d, size_t n);

private:
    std::array<int16_t, kGrid * kGrid> cell_{};
};

// link messages (reliable on the link channel unless marked)
enum LinkMsg : uint8_t {
    kLMap = 1,        // coordinator -> workers: the region map
    kLLoad = 2,       // worker -> coordinator, 1 Hz: LoadReport
    kLSub = 3,        // worker -> worker, unreliable, 1 Hz: the sub-cells its players look into (bitmap)
    kLGhosts = 4,     // worker -> worker, unreliable: poses of its players in the cells the other subscribed
    kLHandoff = 5,    // worker -> worker: a session (ticket) and, for a party leader, the party
    kLIdAsk = 6,      // worker -> coordinator: a block of player ids, please
    kLIdBlock = 7,    // coordinator -> worker: u16 first id of a block of 256
    kLIdFree = 8,     // worker -> the worker that lent it: u16 id no longer used
    kLStop = 9,       // coordinator -> worker: exit
    kLInvite = 10,    // worker -> workers: a party invite for a friend code not on the sender
    kLInviteAnswer = 11,  // worker -> inviter's worker: declined / not here
    kLPartyPark = 12, // worker -> coordinator: a party with nobody online (its last known team state)
    kLBan = 13,       // coordinator -> workers: u64 code40, u64 until, reason (an operator ban)
    kLKick = 14,      // coordinator -> workers: u64 code40, reason
    kLHandoffAck = 15,  // worker -> worker: the ticket is stored; now the client is sent over
    kLPvpHit = 16,    // worker -> worker: u16 attacker id, u64 attacker key hash, u32 attacker party, PvpHit (victim)
};



struct RegionLoad {
    uint32_t region = 0;
    uint16_t players = 0;
    uint64_t out_Bps = 0;
    std::vector<uint16_t> hist;  // players per sub-cell, row by row (sea regions only)
    std::vector<uint16_t> copies;  // players per copy (sea regions only)
};
struct LoadReport {
    float cpu_pct = 0;
    uint32_t tick_p90_us = 0;  // the tick's own time: over the budget means the worker falls behind
    uint64_t out_Bps = 0;
    uint16_t players = 0;
    uint32_t map_version = 0;
    std::vector<RegionLoad> regions;
};
proto::Bytes encode_load(const LoadReport& l);
bool decode_load(const uint8_t* d, size_t n, LoadReport& l);

}  // namespace online::server
