// SPDX-License-Identifier: MPL-2.0
// wwhd-online-server's core (docs/online-server.md): the handshake, sessions, presence with interest management,
// copies of crowded places, parties with the enemy-runner election and the team progress store, abuse limits,
// the admin commands and the shard links. One thread runs it; the game can embed it ("Host on this computer")
// through start_thread().
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config.h"
#include "proto/channel.h"
#include "proto/crypto.h"
#include "proto/handshake.h"
#include "proto/messages.h"
#include "proto/protocol.h"
#include "proto/sha256.h"
#include "proto/udp.h"

namespace online::server {

using proto::Bytes;

struct Bucket {
    double rate = 0, burst = 0, level = 0;
    uint32_t at = 0;
    void init(double r, double b, uint32_t now) {
        rate = r;
        burst = b;
        level = b;
        at = now;
    }
    bool take(double n, uint32_t now) {
        level = std::min(burst, level + (now - at) * rate / 1000.0);
        at = now;
        if (level < n) return false;
        level -= n;
        return true;
    }
};

// a place where players can see each other: a stage (+ copy); at sea the copy only splits busy squares
struct PlaceKey {
    uint64_t world = 0;  // game data hash: different game data never meet
    std::array<char, 8> stage{};
    uint16_t copy = 0;
    bool operator==(const PlaceKey& o) const { return world == o.world && stage == o.stage && copy == o.copy; }
};
struct PlaceKeyHash {
    size_t operator()(const PlaceKey& k) const;
};

struct Seen {
    uint8_t tier = 0;
    uint32_t pose_seq = 0;   // newest pose of theirs sent at the near tier
    uint32_t tick = 0;       // when last sent
    bool marked = false;     // still visible this tick
};

struct Session {
    uint32_t conn_id = 0;
    proto::Address addr;
    proto::Channel chan;
    uint16_t id = 0;
    uint64_t key_hash = 0;
    uint8_t pub[32]{};
    uint8_t client_eph[32]{};
    Bytes welcome;        // resent for a repeated hello
    uint8_t token[32]{};
    std::string name;
    uint32_t look = 0;
    uint8_t device = 0;
    int near_cap = 32, mid_cap = 48;
    uint64_t world = 0;   // game data hash
    bool online = true;
    bool dead = false;    // removed; erased at the next tick
    uint32_t lost_at = 0, joined_at = 0;
    std::string ip;       // bytes, for the per-IP limit (memory only)

    // presence
    bool has_pose = false;
    proto::PoseHeader head;
    Bytes blob;
    uint32_t pose_ms = 0, pose_seq = 0, pose_rx = 0;
    proto::Where where;   // from STAGE (and poses)
    bool stage_changed = true;
    uint16_t copy = 0;
    PlaceKey place;
    std::string copy_key;  // the copies entry this player counts in ("" when the place has no copies)
    int square = -1;      // at sea
    bool hidden = true;   // no pose yet or in transition
    int32_t gx = 0, gz = 0;  // global sub-cell at sea
    std::unordered_map<uint16_t, Seen> seen;
    std::unordered_set<uint64_t> blocks;  // key hashes this player blocked
    uint32_t next_marker_tick = 0;

    // limits
    Bucket packets, bytes, reliable, emote, invite, report, app;
    int strikes = 0;
    uint32_t strikes_since = 0;
    uint32_t freeze_until = 0;  // an implausible move: poses held until then (or a stage change)

    // party
    uint32_t party = 0;

    // shard handover in progress
    bool redirected = false;
    uint32_t redirect_at = 0;
};

// a player held by another shard, seen across a border
struct Ghost {
    uint16_t id = 0;
    uint64_t key_hash = 0, world = 0;
    std::string name;
    uint32_t look = 0;
    proto::PoseHeader head;
    Bytes blob;
    uint32_t pose_ms = 0, pose_seq = 0, at = 0;
    int square = -1;
    int32_t gx = 0, gz = 0;
};

struct TeamStore {
    std::map<uint16_t, proto::TeamField> fields;  // by offset
    uint32_t version = 0;
    // merges; returns the fields that changed (to forward)
    std::vector<proto::TeamField> merge(const std::vector<proto::TeamField>& in, int& refused);
    std::vector<proto::TeamField> all() const;
};

struct PartyPlace {
    std::array<char, 8> stage{};
    int8_t room = 0;
    bool operator<(const PartyPlace& o) const { return std::tie(stage, room) < std::tie(o.stage, o.room); }
};

struct Party {
    uint32_t id = 0;
    uint64_t world = 0;
    uint64_t leader = 0;               // key hash
    std::vector<uint64_t> members;     // key hashes, in join order
    std::map<uint64_t, std::string> names;
    uint16_t flags = proto::kPartyDefaults;
    std::map<PartyPlace, uint64_t> runner;           // key hash of each place's runner
    std::map<uint64_t, std::pair<PartyPlace, uint32_t>> arrived;  // member -> place and when they arrived
    TeamStore team;
    uint32_t empty_since = 0;          // ms; 0 while a member is online
    struct Invite {
        uint64_t to;
        uint32_t at;
    };
    std::vector<Invite> invites;
};

struct Metrics {
    uint64_t bytes_out = 0, bytes_in = 0, packets_out = 0, packets_in = 0;
    uint64_t handshakes = 0, refused = 0, kicked = 0, dropped_rate = 0, dropped_bad = 0, implausible = 0;
    uint64_t ticks = 0;
    std::vector<uint32_t> tick_us;  // since the last report
    uint64_t pose_entries_near = 0, pose_entries_mid = 0, pose_entries_far = 0, markers = 0;
    uint64_t handovers_out = 0, handovers_in = 0, ghosts = 0;
};

struct Ban {
    uint64_t code40 = 0;
    int64_t until = 0;  // unix seconds; 0: permanent
    uint64_t ip_hash = 0;
    std::string reason;
};

class Server {
public:
    explicit Server(Config c);
    ~Server();

    bool start(std::string& why);    // opens the sockets, loads keys and bans
    void run();                      // until stop()
    void stop() { stop_ = true; }
    bool start_thread(std::string& why);  // embedded: start + run on a thread
    void join();

    // one iteration: read for up to `wait_ms`, tick when due (tests drive it directly)
    void step(int wait_ms);

    // admin commands ("kick KQ7F-9MZ2-XD", "status" ...); the answer text
    std::string admin(const std::string& line);

    const Config& config() const { return cfg_; }
    uint16_t port() const { return sock_.local_port(); }
    size_t players() const;
    const Metrics& metrics() const { return metrics_; }
    std::string status_text();
    std::string fingerprint() const { return proto::fingerprint(identity_.pub); }

    // test hooks
    Session* find_by_key(uint64_t key_hash);
    // fuzzing: a raw datagram from `from`, or one decrypted message (type, reliable, body) from a test session
    void fuzz_datagram(const uint8_t* d, size_t n, const proto::Address& from) { on_datagram(d, n, from, proto::monotonic_ms()); }
    void fuzz_message(uint8_t type, bool reliable, const uint8_t* d, size_t n);
    void fuzz_tick() { tick(proto::monotonic_ms()); }
    Party* party_of(const Session& s);
    std::function<void(const std::string&)> log;

private:
    friend struct Interest;
    // handshake
    void on_datagram(const uint8_t* d, size_t n, const proto::Address& from, uint32_t now);
    void on_connect(const uint8_t* d, size_t n, const proto::Address& from, uint32_t now);
    void on_hello(const uint8_t* d, size_t n, const proto::Address& from, uint32_t now);
    void refuse(const proto::Address& to, uint32_t nonce, proto::Refusal r, const std::string& text, uint16_t retry = 0);
    void salt_for(const proto::Address& from, uint32_t bucket, uint8_t out[16]) const;
    uint16_t new_id();
    uint32_t new_conn_id();

    // sessions
    void on_message(Session& s, const proto::Delivered& m, uint32_t now);
    void on_pose(Session& s, const uint8_t* d, size_t n, uint32_t now);
    void on_stage(Session& s, const proto::Where& w, uint32_t now);
    void on_app(Session& s, const proto::Delivered& m, uint32_t now);
    void place_player(Session& s, uint32_t now);
    void leave_place(Session& s);
    void drop(Session& s, const std::string& why, bool keep_for_resume);
    void remove(uint32_t conn_id);
    void reap();
    void strike(Session& s, uint32_t now, const char* what);
    void notice(Session& s, uint8_t kind, uint16_t seconds, const std::string& text);
    void kick(Session& s, uint8_t kind, const std::string& text);
    bool sees(const Session& viewer, uint64_t other_key) const;

    // tick
    void tick(uint32_t now);
    void interest(Session& s, uint32_t now);
    void send_markers(Session& s);
    void flush_all(uint32_t now);
    void rebuild_grid();
    template <class F> void for_viewers(Session& s, F&& fn);
    void gather(Session& s, std::vector<Session*>& out, std::vector<Ghost*>* ghosts, float extra);

    // parties (party.cpp)
    void party_invite(Session& s, uint64_t code40, uint32_t now);
    void party_accept(Session& s, uint32_t party, uint32_t now);
    void party_decline(Session& s, uint32_t party);
    void party_leave(Session& s, uint32_t now);
    void party_settings(Session& s, uint16_t flags);
    void party_progress(Session& s, proto::Reader& r);
    void party_send_state(Party& p);
    void party_send_team(Party& p, Session& to);
    void party_join(Party& p, Session& s, uint32_t now);
    void party_member_online(Session& s, uint32_t now);
    void party_member_offline(Session& s, uint32_t now);
    void party_place_changed(Session& s, uint32_t now);
    void runners_tick(uint32_t now);
    void party_result(Session& s, uint8_t code, const std::string& text);
    Session* online_member(uint64_t key_hash);
    void parties_gc(uint32_t now);
    void save_team_states();
    void load_team_states();

    // admin (admin.cpp)
    bool open_admin(std::string& why);
    void admin_poll();
    void load_bans();
    void save_bans();
    bool banned(uint64_t key_hash, const std::string& ip, std::string& reason);
    uint64_t ip_hash(const std::string& ip) const;
    void append_report(uint64_t from, uint64_t about, uint8_t reason);

    // shards (shard.cpp)
    bool open_link(std::string& why);
    void link_poll(uint32_t now);
    void link_tick(uint32_t now);
    void link_send(int shard, uint8_t type, const Bytes& body);
    void handover_check(Session& s, uint32_t now);
    int shard_of_place(const Session& s) const;

    void write_metrics(uint32_t now);

    Config cfg_;
    proto::crypto::Identity identity_;
    uint8_t cookie_key_[32]{};
    uint8_t ip_salt_[32]{};
    proto::UdpSocket sock_;
    std::atomic<bool> stop_{false};
    std::thread thread_;

    std::unordered_map<uint32_t, std::unique_ptr<Session>> sessions_;  // by connection id
    std::unordered_map<uint64_t, uint32_t> by_key_;                   // key hash -> conn id
    std::unordered_map<uint16_t, uint32_t> by_id_;
    std::unordered_map<std::string, int> per_ip_;
    std::map<std::array<uint8_t, 32>, uint32_t> by_token_;
    std::unordered_map<std::string, Bucket> connect_rate_;            // per IP
    uint16_t next_id_ = 1;
    std::vector<uint32_t> dead_;

    // places and copies
    struct PlaceInfo {
        std::vector<int> count;  // players per copy
    };
    std::unordered_map<PlaceKey, std::vector<Session*>, PlaceKeyHash> interiors_;  // rebuilt per tick
    std::unordered_map<uint64_t, std::vector<Session*>> grid_;                    // sea sub-cells, per tick
    std::unordered_map<uint64_t, std::vector<Ghost*>> ghost_grid_;
    std::unordered_map<uint64_t, std::vector<Session*>> squares_;  // (world, square) -> players at sea, per tick
    std::unordered_map<std::string, PlaceInfo> copies_;  // "world/stage[:room]" -> players per copy

    // parties
    std::unordered_map<uint32_t, Party> parties_;
    std::unordered_map<uint64_t, uint32_t> party_by_key_;
    uint32_t next_party_ = 1;

    // bans and admin
    std::vector<Ban> bans_;
    int admin_fd_ = -1;
    std::vector<int> admin_clients_;
    uint32_t restart_at_ = 0;

    // shards
    proto::UdpSocket link_;
    std::vector<proto::Address> link_addr_;
    std::unordered_map<uint16_t, Ghost> ghosts_;
    struct Ticket {
        uint32_t at;
        Ghost who;
        uint8_t pub[32];
        uint8_t device;
        int near_cap, mid_cap;
        uint32_t party;
    };
    std::map<std::array<uint8_t, 32>, Ticket> tickets_;

    uint32_t tick_no_ = 0, next_tick_ = 0, last_metrics_ = 0;
    uint64_t cpu_us_at_ = 0;
    Metrics metrics_;
    std::vector<Bytes> outq_;
};

}  // namespace online::server
