// SPDX-License-Identifier: MPL-2.0
// The server's configuration (server.toml; docs/online-server.md lists every key).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "proto/mods.h"
#include "proto/pvp.h"

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
    bool tcp = true;                // also listen on TCP at the same port (for networks that block UDP)
    bool udp = true;                // false: UDP datagrams are ignored (TCP only; tests of the fallback)
    int max_players = 1000;
    std::vector<uint64_t> game_data;  // accepted game-data hashes; empty: any (each hash gets its own world)

    // files (relative to the working directory)
    std::string key_file = "server.key";
    std::string ban_file = "bans.txt";
    std::string report_file = "reports.log";
    std::string admin_socket = "wwhd-online-admin.sock";  // "" disables the admin socket
    std::string team_state_file = "team-state.txt";  // parties and their team progress over restarts ("": memory only)

    // PvP at sea (docs/online-server.md, "PvP at sea")
    proto::PvpRules pvp = [] {
        proto::PvpRules r;
        r.zones = proto::default_pvp_zones();
        return r;
    }();
    double pvp_hits_per_s = 8, pvp_shots_per_s = 4;
    uint8_t pvp_max_damage = 16;  // quarter hearts in one hit

    // gameplay-mod rules (docs/online-server.md, "Mod rules"): none by default (anything goes)
    proto::ModRules mods;

    // world
    std::string sea_stage = "sea";  // the stage code of the open sea
    float sea_origin_x = -350000.0f, sea_origin_z = -350000.0f;  // the corner of square 0
    int copy_size = 64;
    std::vector<std::string> busy;  // "stage" or "stage:room": places with copies (interiors always have copies)
    std::vector<std::string> stages;  // known stage codes; empty: any well-formed code

    // interest (DESIGN.md 3.3)
    int tick_hz = 15;
    int threads = 0;                // worker threads for the tick (interest, sending); 0: automatic (up to 8)
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
    int party_max = 8;              // party.max_size: the largest party (2..16)
    int team_state_keep_h = 72;     // the last known team state is kept this long with no member online
    double app_per_s = 120;         // opaque app messages per second

    // scaling (docs/online-server.md, "Scaling"). "off": one process runs everything (small servers, the game's
    // "Host on this computer"). "auto": this process is the coordinator and worker 0; it starts worker processes
    // on this machine when the load needs them and accepts workers from other machines. "pool": the same without
    // starting local processes (only registered workers are used).
    std::string scale_mode = "off";
    int max_workers = 8;            // worker processes including the coordinator (at most 15)
    int worker_threads = 0;         // tick threads of a started worker; 0: the machine's cores shared by max_workers
                                    // (at least 2, at most 16)
    uint16_t link_port = 7900;      // the coordinator's link port (loopback / LAN); workers: link_port + index
    uint16_t worker_port_base = 7778;  // client ports of started workers: base + index - 1
    std::string public_host;        // the host name clients use for redirects ("" : the bind address)
    std::string link_bind = "127.0.0.1";  // the link socket's address (a LAN address for pool workers)
    std::string link_secret;        // shared secret of the links (16+ chars; or WWHD_ONLINE_LINK_SECRET)
    double split_cpu = 120.0;       // worker CPU % (all its threads) above which its load is shed
    double split_bw = 50e6;         // worker egress bytes/s above which its load is shed
    double merge_level = 0.35;      // both halves below this share of the thresholds: merge back
    int scale_hold_s = 3;           // over the threshold this long before a split or move
    int scale_cooldown_s = 8;       // between two changes of one worker's regions
    int merge_hold_s = 30;
    int idle_worker_s = 60;         // a started worker without regions this long is stopped
    int min_region = 1;             // smallest region side, in sub-cells (1 = 12,500 units)
    int area_cap = 200;             // players per 2x2 sub-cell area and copy at sea before arrivals go to a copy
    float handover_depth = 2000.0f; // this far inside another worker's region before the handover

    // worker mode (wwhd-online-server --worker --join host:port)
    bool worker = false;
    std::string config_path;        // the file this config came from (started workers read it too)
    std::string join;               // the coordinator's link address

    // metrics
    std::string metrics_file;       // "" none; else a line of counts every metrics_every_s
    int metrics_every_s = 10;
    bool log_addresses = false;     // never print IP addresses unless asked

};

// parses TOML-like "key = value" lines with [sections] flattened as "section.key"; false + why on errors
bool load_config(const std::string& path, Config& c, std::string& why);
bool parse_config_text(const std::string& text, Config& c, std::string& why);

}  // namespace online::server
