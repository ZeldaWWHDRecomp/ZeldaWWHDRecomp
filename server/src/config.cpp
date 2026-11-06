// SPDX-License-Identifier: MPL-2.0
// A small reader for the subset of TOML the server config uses: [section] headers, key = value with strings,
// integers, floats, booleans and one-line arrays, and # comments.
#include "config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "proto/protocol.h"

namespace online::server {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// strips a comment outside of quotes
std::string uncomment(const std::string& s) {
    bool q = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\')) q = !q;
        if (s[i] == '#' && !q) return s.substr(0, i);
    }
    return s;
}

bool unquote(const std::string& v, std::string& out) {
    if (v.size() < 2 || v.front() != '"' || v.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < v.size(); ++i) {
        if (v[i] == '\\' && i + 2 < v.size()) {
            char n = v[++i];
            out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else {
            out += v[i];
        }
    }
    return true;
}

std::vector<std::string> split_array(const std::string& v, bool& ok) {
    std::vector<std::string> items;
    ok = v.size() >= 2 && v.front() == '[' && v.back() == ']';
    if (!ok) return items;
    std::string cur;
    bool q = false;
    for (size_t i = 1; i + 1 < v.size(); ++i) {
        char c = v[i];
        if (c == '"') q = !q;
        if (c == ',' && !q) {
            if (!trim(cur).empty()) items.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) items.push_back(trim(cur));
    return items;
}

bool to_bool(const std::string& v, bool& b) {
    if (v == "true") b = true;
    else if (v == "false") b = false;
    else return false;
    return true;
}

bool to_u64(const std::string& v0, uint64_t& out) {
    std::string v = v0;
    std::string s;
    if (unquote(v, s)) v = s;
    // a friend code is accepted where a key hash is expected
    uint64_t code = 0;
    if (proto::parse_friend_code(v, code)) {
        out = code;
        return true;
    }
    char* end = nullptr;
    out = std::strtoull(v.c_str(), &end, 0);
    return end && *end == 0 && !v.empty();
}

}  // namespace

bool parse_config_text(const std::string& text, Config& c, std::string& why) {
    std::istringstream in(text);
    std::string line, section;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = trim(uncomment(line));
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() != ']') {
                why = "line " + std::to_string(n) + ": bad section";
                return false;
            }
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            why = "line " + std::to_string(n) + ": expected key = value";
            return false;
        }
        std::string key = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (!section.empty()) key = section + "." + key;
        bool ok = true;
        auto str = [&](std::string& dst) { ok = unquote(v, dst); };
        auto boolean = [&](bool& dst) { ok = to_bool(v, dst); };
        auto integer = [&](int& dst) {
            char* e = nullptr;
            long x = std::strtol(v.c_str(), &e, 0);
            ok = e && *e == 0;
            dst = int(x);
        };
        auto number = [&](double& dst) {
            char* e = nullptr;
            dst = std::strtod(v.c_str(), &e);
            ok = e && *e == 0;
        };
        auto numf = [&](float& dst) {
            double d = 0;
            number(d);
            dst = float(d);
        };
        auto strings = [&](std::vector<std::string>& dst) {
            dst.clear();
            for (auto& it : split_array(v, ok)) {
                std::string s;
                if (!unquote(it, s)) ok = false;
                dst.push_back(s);
            }
        };
        auto u64s = [&](std::vector<uint64_t>& dst) {
            dst.clear();
            for (auto& it : split_array(v, ok)) {
                uint64_t x;
                if (!to_u64(it, x)) ok = false;
                dst.push_back(x);
            }
        };
        if (key == "name" || key == "server.name") str(c.name);
        else if (key == "contact" || key == "server.contact") str(c.contact);
        else if (key == "rules" || key == "server.rules") str(c.rules);
        else if (key == "public" || key == "server.public") boolean(c.public_server);
        else if (key == "invite_only" || key == "server.invite_only") boolean(c.invite_only);
        else if (key == "allow_insecure" || key == "server.allow_insecure") boolean(c.allow_insecure);
        else if (key == "allow" || key == "server.allow") u64s(c.allow);
        else if (key == "bind" || key == "network.bind") str(c.bind);
        else if (key == "port" || key == "network.port") {
            int p = 0;
            integer(p);
            ok = ok && p > 0 && p < 65536;
            c.port = uint16_t(p);
        } else if (key == "max_players" || key == "network.max_players") integer(c.max_players);
        else if (key == "network.tcp") boolean(c.tcp);
        else if (key == "network.udp") boolean(c.udp);
        else if (key == "game_data" || key == "network.game_data") u64s(c.game_data);
        else if (key == "files.key") str(c.key_file);
        else if (key == "files.bans") str(c.ban_file);
        else if (key == "files.reports") str(c.report_file);
        else if (key == "files.admin_socket") str(c.admin_socket);
        else if (key == "files.team_state") str(c.team_state_file);
        else if (key == "world.sea_stage") str(c.sea_stage);
        else if (key == "world.sea_origin_x") numf(c.sea_origin_x);
        else if (key == "world.sea_origin_z") numf(c.sea_origin_z);
        else if (key == "world.copy_size") integer(c.copy_size);
        else if (key == "world.busy") strings(c.busy);
        else if (key == "world.stages") strings(c.stages);
        else if (key == "interest.tick_hz") integer(c.tick_hz);
        else if (key == "interest.threads" || key == "threads") integer(c.threads);
        else if (key == "interest.near_radius") numf(c.near_radius);
        else if (key == "interest.window_half") numf(c.window_half);
        else if (key == "interest.hysteresis") numf(c.hysteresis);
        else if (key == "interest.near_cap_desktop") integer(c.near_cap_desktop);
        else if (key == "interest.near_cap_phone") integer(c.near_cap_phone);
        else if (key == "interest.mid_cap") integer(c.mid_cap);
        else if (key == "interest.far_cap") integer(c.far_cap);
        else if (key == "interest.marker_cap") integer(c.marker_cap);
        else if (key == "limits.pow_bits") {
            int b = 0;
            integer(b);
            ok = ok && b >= 0 && b <= 30;
            c.pow_bits = uint8_t(b);
        } else if (key == "limits.per_ip") integer(c.per_ip);
        else if (key == "limits.loopback_exempt") boolean(c.loopback_exempt);
        else if (key == "limits.packets_per_s") number(c.packets_per_s);
        else if (key == "limits.bytes_per_s") number(c.bytes_per_s);
        else if (key == "limits.runner_bytes_per_s") number(c.runner_bytes_per_s);
        else if (key == "limits.reliable_per_s") number(c.reliable_per_s);
        else if (key == "limits.app_per_s") number(c.app_per_s);
        else if (key == "limits.max_speed") number(c.max_speed);
        else if (key == "limits.strikes_kick") integer(c.strikes_kick);
        else if (key == "limits.silence_s") integer(c.silence_s);
        else if (key == "limits.resume_s") integer(c.resume_s);
        else if (key == "limits.keep_ip_hash") boolean(c.keep_ip_hash);
        else if (key == "party.runner_drop_ms") integer(c.runner_drop_ms);
        else if (key == "party.team_state_keep_h") integer(c.team_state_keep_h);
        else if (key == "scale.mode") {
            str(c.scale_mode);
            ok = ok && (c.scale_mode == "off" || c.scale_mode == "auto" || c.scale_mode == "pool");
        } else if (key == "scale.max_workers") integer(c.max_workers);
        else if (key == "scale.worker_threads") integer(c.worker_threads);
        else if (key == "scale.link_port") {
            int p = 0;
            integer(p);
            c.link_port = uint16_t(p);
        } else if (key == "scale.worker_port_base") {
            int p = 0;
            integer(p);
            c.worker_port_base = uint16_t(p);
        } else if (key == "scale.public_host") str(c.public_host);
        else if (key == "scale.link_bind") str(c.link_bind);
        else if (key == "scale.secret") str(c.link_secret);
        else if (key == "scale.split_cpu") number(c.split_cpu);
        else if (key == "scale.split_bw") number(c.split_bw);
        else if (key == "scale.merge_level") number(c.merge_level);
        else if (key == "scale.hold_s") integer(c.scale_hold_s);
        else if (key == "scale.cooldown_s") integer(c.scale_cooldown_s);
        else if (key == "scale.merge_hold_s") integer(c.merge_hold_s);
        else if (key == "scale.idle_worker_s") integer(c.idle_worker_s);
        else if (key == "scale.min_region") integer(c.min_region);
        else if (key == "scale.area_cap") integer(c.area_cap);
        else if (key == "scale.handover_depth") numf(c.handover_depth);
        else if (key == "scale.join") str(c.join);
        else if (key == "metrics.file") str(c.metrics_file);
        else if (key == "metrics.every_s") integer(c.metrics_every_s);
        else if (key == "metrics.log_addresses") boolean(c.log_addresses);
        else {
            why = "line " + std::to_string(n) + ": unknown key " + key;
            return false;
        }
        if (!ok) {
            why = "line " + std::to_string(n) + ": bad value for " + key;
            return false;
        }
    }
    if (c.max_workers < 1 || c.max_workers > 15 || c.min_region < 1 || c.min_region > 8) {
        why = "scale.max_workers (1..15) or scale.min_region (1..8) out of range";
        return false;
    }
    if (c.tick_hz < 5 || c.tick_hz > 60 || c.copy_size < 2 || c.max_players < 1) {
        why = "tick_hz, copy_size or max_players out of range";
        return false;
    }
    return true;
}

bool load_config(const std::string& path, Config& c, std::string& why) {
    std::ifstream in(path);
    if (!in) {
        why = "cannot read " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_config_text(ss.str(), c, why);
}

}  // namespace online::server
