// SPDX-License-Identifier: MPL-2.0
// The client side of the online protocol, for the game and for the headless bots. Non-blocking: call poll() once per
// game step; it reads the socket, runs the handshake (the proof-of-work is spread over polls, ~2 ms each), resends,
// reconnects with backoff and the session token, and returns what happened as a list of events.
//
//   online::proto::Client c;
//   ClientConfig cfg; cfg.host = "127.0.0.1"; cfg.game_data = ss::game_data_id(); cfg.identity = id; ...
//   c.start(cfg, why);
//   each step: for (auto& e : c.poll(now_ms)) ...;  c.send_pose(head, blob, n);
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "channel.h"
#include "crypto.h"
#include "handshake.h"
#include "messages.h"
#include "protocol.h"
#include "pvp.h"
#include "tcp.h"
#include "udp.h"

namespace online::proto {

enum class Transport { kUdp, kTcp };
// kAuto: UDP first, then TCP when no handshake answer has come 3 s after the first CONNECT; the environment variable
// WWHD_ONLINE_TCP=1 forces TCP and =0 forbids it (it wins over the config)
enum class TransportMode { kAuto, kUdpOnly, kTcpOnly };

struct ClientConfig {
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    uint64_t game_data = 0;          // the game data hash (the game: ss::game_data_id())
    crypto::Identity identity;       // the install's keypair (crypto::Identity::load_or_create)
    std::string name;                // display name (party / friends), <= 24
    uint32_t look = 0;               // tunic colour etc., opaque to the server
    DeviceClass device = kDesktop;
    uint8_t near_cap = 0, mid_cap = 0;  // 0: the server's default for the device
    std::string tag;                 // build tag for the server's logs, <= 16
    // the server key pinned on the first connection (trust on first use); unset: accept and report it
    std::optional<std::array<uint8_t, 32>> pinned_server;
    bool reconnect = true;           // reconnect with backoff after a loss (1, 2, 4 ... 30 s)
    int send_hz = 30;                // packets to the server at most this often (poll may be called more often)
    TransportMode transport_mode = TransportMode::kAuto;
    std::vector<ModRef> mods;        // the enabled gameplay mods (look-only mods are never listed)
    bool query_only = false;         // the server list: stop after the server's info and mod rules (no join)
};

enum class ClientState { kOff, kConnecting, kOnline, kReconnecting, kStopped };

struct ClientEvent {
    enum Kind {
        kStateChanged,   // state, text
        kWelcome,        // id (my short id), resumed
        kRefused,        // refusal, text; the client stops unless the reason is temporary
        kServerKey,      // server info seen (name, contact, rules, fingerprint) before joining: pin info.pub
        kServerKeyChanged,  // the server key is not the pinned one: the client stopped
        kPlayerIn,       // player
        kPlayerOut,      // id
        kPoses,          // poses (a POSE_BATCH)
        kMarkers,        // markers
        kSeaPlayers,     // sea_players: every other player at sea the server shows on the Sea Chart
        kEmote,          // id, value
        kApp,            // id (sender), app_type, body, reliable
        kNotice,         // value (NoticeKind), seconds, text
        kCopy,           // where, copy, copies
        kPartyInvited,   // party_id, id (from), player (key hash, name)
        kPartyState,     // party
        kRunners,        // runners
        kTeamState,      // version, fields
        kPartyResult,    // value (code), text
        kPartyWhere,     // members: where every online party member is (1 Hz)
        kServerRules,    // info (with info.mods: the server's mod rules), before joining; query_only stops after it
        kPvpRules,       // pvp_rules
        kPvpHitOn,       // pvp_hit (other = the attacker)
        kPvpShotOf,      // pvp_shot (with the shooter)
        kPvpKoOf,        // id = the victim, value16 = by
        kPvpRupees,      // value16 = rupees to remove (they vanish)
        kPvpState,       // value = PvpStateFlags, value16 = protection ms left
        kPvpRespawn,     // value = square, pvp_hit.x / z = where
        kPvpMarks,       // pvp_marks: {id, flags (bit 0 PvP active, bit 1 protected)} of players seen near or mid
    } kind;
    ClientState state = ClientState::kOff;
    std::string text;
    Refusal refusal = Refusal::kNone;
    ServerInfo info;
    uint16_t id = 0;
    bool resumed = false, reliable = false;
    uint8_t app_type = 0, value = 0;
    uint16_t seconds = 0, copy = 0, copies = 0;
    uint32_t party_id = 0, version = 0;
    Where where;
    PlayerIn player;
    std::vector<PoseEntry> poses;
    std::vector<Marker> markers;
    std::vector<SeaPlayer> sea_players;
    Bytes body;
    PartyState party;
    std::vector<RunnerEntry> runners;
    std::vector<TeamField> fields;
    struct MemberWhere {
        uint16_t id = 0;
        Where where;
        uint8_t square = 255;  // 255: not at sea
        int32_t x = 0, z = 0;
    };
    std::vector<MemberWhere> members;
    std::vector<ModProblem> mod_problems;  // kRefused with Refusal::kMods; kPartyResult with value 10
    std::vector<ModRef> mods;              // kPartyInvited: the party's mod set (its leader's)
    uint16_t value16 = 0;
    PvpRules pvp_rules;
    PvpHit pvp_hit;
    PvpShot pvp_shot;
    std::vector<std::pair<uint16_t, uint8_t>> pvp_marks;
};

struct ClientStats {
    uint64_t bytes_up = 0, bytes_down = 0, packets_up = 0, packets_down = 0;
};

class Client {
public:
    Client();
    // share an outside socket (bots: many clients on one socket); the owner feeds datagrams with on_datagram()
    explicit Client(UdpSocket* shared);
    ~Client();

    bool start(const ClientConfig& cfg, std::string& why);
    void stop();  // says goodbye

    const std::vector<ClientEvent>& poll(uint32_t now_ms);
    // shared-socket mode: a datagram for this client (by nonce or connection id)
    void on_datagram(const uint8_t* d, size_t n, uint32_t now_ms);

    // ---- sending (dropped quietly unless online) ----
    void send_pose(const PoseHeader& head, const uint8_t* blob, size_t n);  // unreliable, latest wins
    void send_stage(const Where& w);
    void send_app(const AppHeader& h, const uint8_t* body, size_t n, bool reliable);
    void send_emote(uint8_t emote);
    void block(uint64_t key_hash, bool on);
    void mute(uint64_t key_hash, bool on);
    // PvP at sea (pvp.h): the player's toggle and whether it is busy (protected); a hit, a shot, a knockout
    void pvp_flag(bool on, bool busy);
    void pvp_hit(const PvpHit& h);   // h.other = the victim
    void pvp_shot(const PvpShot& s);
    void pvp_ko(uint16_t by, uint16_t wallet);  // that player's emotes, effects, sounds (and voice) stop reaching you
    void report(uint64_t key_hash, uint8_t reason);
    void switch_copy();
    void join_copy_of(uint64_t key_hash);  // the copy a friend (or anyone visible) is in
    void party_invite(uint64_t code40);
    void party_accept(uint32_t party);
    void party_decline(uint32_t party);
    void party_leave();
    void party_settings(uint16_t flags);
    void pin_runner(bool on);
    void send_progress(const std::vector<TeamField>& fields);
    // after a fresh (not resumed) kWelcome: the party this game was in (the leader's record; messages.h)
    void party_restore(const PartyRecord& record);
    void ask_team_state();

    ClientState state() const { return state_; }
    uint16_t my_id() const { return my_id_; }
    int ping_ms() const;
    std::string friend_code() const;
    uint64_t key_hash() const;
    const ServerInfo& server() const { return info_; }
    std::string status_line() const;
    const ClientStats& stats() const { return stats_; }
    uint32_t conn_id() const { return chan_.conn_id(); }
    uint32_t nonce() const { return nonce_; }
    Transport transport() const { return transport_; }

private:
    void set_state(ClientState s, const std::string& text);
    void begin_connect(uint32_t now);
    void handle(const uint8_t* d, size_t n, uint32_t now);
    void handle_message(const Delivered& m);
    void send_raw(const Bytes& b);
    void step_pow(uint32_t now);
    void lost(uint32_t now, const std::string& why);

    UdpSocket own_;
    TcpStream tcp_;
    Transport transport_ = Transport::kUdp;
    TransportMode mode_ = TransportMode::kAuto;
    uint32_t first_connect_sent_ = 0;
    // the caller's clock (poll's now_ms): every time in here is on it, never on monotonic_ms(), which the game's
    // clock need not match (mixing them made every redirect and the first connect time out at once)
    uint32_t now_ = 0;
    bool begin_pending_ = false;  // start() and REDIRECT begin at the next poll, on the caller's clock
    // a REDIRECT (handover between workers): the old session goes on (poses out, the world in) while the new one
    // is made with a HELLO straight away (the ticket needs no challenge); the game sees no state change
    bool redirecting_ = false, direct_hello_ = false, direct_fallback_ = false;
    Address old_server_;
    void send_raw_to(const Address& to, const Bytes& b);  // the TCP fallback counts from the first CONNECT really sent
    UdpSocket* sock_ = nullptr;
    Address server_, home_;  // home_: the address start() was given (redirects come back to it after a loss)
    std::string home_host_;
    uint16_t home_port_ = 0;
    ClientConfig cfg_;
    ClientState state_ = ClientState::kOff;
    enum class Phase { kNone, kConnect, kRules, kPow, kHello } phase_ = Phase::kNone;
    std::vector<bool> rules_got_;
    std::vector<ModRule> rules_;
    void rules_done(uint32_t now);
    uint32_t nonce_ = 0;
    Challenge chal_;
    ServerInfo info_;
    uint64_t pow_next_ = 0;
    bool pow_found_ = false;
    crypto::Ephemeral eph_;
    uint8_t tx_[32]{}, rx_[32]{};
    Bytes hello_;
    uint32_t last_flush_ = 0;
    uint32_t phase_at_ = 0, phase_started_ = 0, retry_at_ = 0, backoff_s_ = 1;
    bool have_token_ = false;
    uint8_t token_[32]{};
    uint16_t my_id_ = 0;
    Channel chan_;
    bool pose_pending_ = false;
    Bytes pose_;
    std::vector<ClientEvent> events_, out_;
    std::vector<Bytes> outq_;
    ClientStats stats_;
};

}  // namespace online::proto
