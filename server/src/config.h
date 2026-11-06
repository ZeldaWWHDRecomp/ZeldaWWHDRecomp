// SPDX-License-Identifier: MPL-2.0
// The server's configuration (server.toml; docs/online-server.md lists every key).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace online::server {

struct Config {
    // identity and info
    std::string name = "Self-hosted server";
    std::string contact;            // the operator's name / contact, shown before the first connection
    std::string rules;
    bool public_server = false;     // false: LAN / friends; true: refuse to start with the insecure crypto backend
    bool invite_only = false;       // only key hashes in `allow` may join
    bool allow_insecure = false;    // public + plain crypto backend: start anyway
    std::vector<uint64_t> allow;    // key hashes (code40) allowed when invite_only

    // network
    std::string bind = "127.0.0.1";
    uint16_t port = 7777;
    int max_players = 1000;
    std::vector<uint64_t> game_data;  // accepted game-data hashes; empty: any (each hash gets its own world)

    // files (relative to the working directory)
    std::string key_file = "server.key";
    std::string ban_file = "bans.txt";
    std::string report_file = "reports.log";
    std::string admin_socket = "wwhd-online-admin.sock";  // "" disables the admin socket
    std::string team_state_file;    // "" keeps team states in memory only

    // world
    std::string sea_stage = "sea";  // the stage code of the open sea
    float sea_origin_x = -350000.0f, sea_origin_z = -350000.0f;  // the corner of square 0
    int copy_size = 64;
    std::vector<std::string> busy;  // "stage" or "stage:room": places with copies (interiors always have copies)
    std::vector<std::string> stages;  // known stage codes; empty: any well-formed code

    // interest (DESIGN.md 3.3)
    int tick_hz = 15;
    float near_radius = 3000.0f;
    float window_half = 18750.0f;   // the 3x3 sub-cell window as a box around the receiver
    float hysteresis = 0.15f;
    int near_cap_desktop = 32, near_cap_phone = 16, mid_cap = 48, far_cap = 128, marker_cap = 512;
    int mid_every = 4, far_every = 15, marker_every = 75;  // in ticks
    int player_in_per_tick = 64;

    // abuse protection
    uint8_t pow_bits = 18;          // ~0.3 s on a phone
    int per_ip = 4;
    bool loopback_exempt = false;   // per-IP limit and proof-of-work off for 127.0.0.0/8 (load tests)
    double packets_per_s = 60, bytes_per_s = 8192, runner_bytes_per_s = 16384, reliable_per_s = 60;
    double emote_per_s = 1, invite_every_s = 10, report_every_s = 60;
    double max_speed = 6000.0;      // units per second, any vehicle, with a burst allowance
    int strikes_kick = 200;         // rate / plausibility strikes within a minute before a kick
    int silence_s = 10;             // no packets: the session is lost (kept for resume)
    int resume_s = 60;              // a lost session can resume this long
    int runner_drop_ms = 3000;      // a runner silent this long is replaced
    bool keep_ip_hash = false;      // bans also keep a salted hash of the IP

    // parties
    int team_state_keep_h = 72;     // the last known team state is kept this long with no member online
    double app_per_s = 120;         // opaque app messages per second

    // shards (P4); count 1: one process runs everything
    int shard_index = 0, shard_count = 1;
    std::vector<int> shard_of_square;   // 49 entries; empty: squares split in row bands
    std::vector<std::string> shard_peers;  // "host:port" of each shard's client port, by index
    std::vector<std::string> shard_links;  // "host:port" of each shard's link port, by index
    std::string link_secret;        // shared secret of the shard links
    float handover_depth = 2000.0f;
    float border_band = 12500.0f;

    // metrics
    std::string metrics_file;       // "" none; else a line of counts every metrics_every_s
    int metrics_every_s = 10;
    bool log_addresses = false;     // never print IP addresses unless asked

    int shard_of(int square) const;
};

// parses TOML-like "key = value" lines with [sections] flattened as "section.key"; false + why on errors
bool load_config(const std::string& path, Config& c, std::string& why);
bool parse_config_text(const std::string& text, Config& c, std::string& why);

}  // namespace online::server
