// SPDX-License-Identifier: MPL-2.0
// wwhd-online-bots: headless load-test clients for wwhd-online-server (no game). Many bots share one UDP socket per
// process; each sails or walks a random path and sends a pose of a fixed size at the pose rate, like a game would.
//
//   wwhd-online-bots --port 7777 --count 1000 --duration 60 --scenario mixed --report out.json
//
// Scenarios (DESIGN.md 3.6): quiet (spread over the sea), busy (crowded squares), town (one crowded interior with
// copies), mixed (50% quiet, 30% busy, 20% town). The report has per-bot download/upload rates per scenario.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "proto/client.h"

#ifndef _WIN32
#include <sys/resource.h>
#endif

using namespace online::proto;

namespace {

enum Scenario { kQuiet = 0, kBusy = 1, kTown = 2 };
const char* kScenarioName[] = {"quiet", "busy", "town"};

struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    int count = 100;
    int duration_s = 30;
    int warmup_s = 10;
    std::string scenario = "mixed";
    int pose_bytes = 140;  // DESIGN.md 3.5 average compact pose
    int pose_hz = 15;
    int ramp_per_s = 400;
    uint32_t seed = 1;
    uint64_t game_data = 0x5EED;
    std::string report;
    int max_rss_mb = 2048;
    int phone_percent = 0;
    int busy_squares = 3;
    float sea_origin = -350000.0f;
};

struct Bot {
    std::unique_ptr<Client> c;
    Scenario sc = kQuiet;
    float x = 0, z = 0, y = 0, heading = 0, speed = 0;
    float cx = 0, cz = 0, radius = 0;  // the area it stays in
    uint32_t next_at = 0;
    uint64_t down_at_warm = 0, up_at_warm = 0;
    bool started = false, counted = false;
    int players_seen = 0, near = 0, mid = 0, far = 0;
    uint64_t poses = 0;
};

uint64_t rss_mb() {
#ifndef _WIN32
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return uint64_t(ru.ru_maxrss) / (1024 * 1024);
#else
    return uint64_t(ru.ru_maxrss) / 1024;
#endif
#else
    return 0;
#endif
}

double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--host") o.host = next();
        else if (a == "--port") o.port = uint16_t(std::atoi(next().c_str()));
        else if (a == "--count") o.count = std::atoi(next().c_str());
        else if (a == "--duration") o.duration_s = std::atoi(next().c_str());
        else if (a == "--warmup") o.warmup_s = std::atoi(next().c_str());
        else if (a == "--scenario") o.scenario = next();
        else if (a == "--pose-bytes") o.pose_bytes = std::atoi(next().c_str());
        else if (a == "--pose-hz") o.pose_hz = std::atoi(next().c_str());
        else if (a == "--ramp") o.ramp_per_s = std::atoi(next().c_str());
        else if (a == "--seed") o.seed = uint32_t(std::atoi(next().c_str()));
        else if (a == "--report") o.report = next();
        else if (a == "--max-rss-mb") o.max_rss_mb = std::atoi(next().c_str());
        else if (a == "--phones") o.phone_percent = std::atoi(next().c_str());
        else if (a == "--busy-squares") o.busy_squares = std::atoi(next().c_str());
        else if (a == "--game-data") o.game_data = std::strtoull(next().c_str(), nullptr, 0);
        else {
            std::fprintf(stderr,
                         "usage: wwhd-online-bots [--host h] [--port p] [--count n] [--duration s] [--warmup s]\n"
                         "  [--scenario quiet|busy|town|mixed] [--pose-bytes n] [--pose-hz n] [--ramp n/s]\n"
                         "  [--seed n] [--report file.json] [--max-rss-mb n] [--phones %%] [--busy-squares n]\n");
            return 2;
        }
    }
    o.pose_bytes = std::clamp(o.pose_bytes, 0, int(kMaxPoseBlob));
    UdpSocket sock;
    std::string why;
    if (!sock.open("127.0.0.1", 0, why) && !sock.open("0.0.0.0", 0, why)) {
        std::fprintf(stderr, "bots: %s\n", why.c_str());
        return 1;
    }
    sock.set_buffers(32 << 20);

    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<float> u(0, 1);
    std::vector<Bot> bots(size_t(o.count));
    const float kSea = kSeaSquares * kSquareUnits;
    // the busy squares and their crowd centres
    std::vector<std::pair<float, float>> hot;
    for (int i = 0; i < std::max(1, o.busy_squares); ++i) {
        int sq = int(rng() % uint32_t(kSeaSquares * kSeaSquares));
        hot.push_back({o.sea_origin + (float(sq % kSeaSquares) + 0.5f) * kSquareUnits,
                       o.sea_origin + (float(sq / kSeaSquares) + 0.5f) * kSquareUnits});
    }
    for (int i = 0; i < o.count; ++i) {
        Bot& b = bots[size_t(i)];
        if (o.scenario == "quiet") b.sc = kQuiet;
        else if (o.scenario == "busy") b.sc = kBusy;
        else if (o.scenario == "town") b.sc = kTown;
        else {
            float r = u(rng);
            b.sc = r < 0.5f ? kQuiet : r < 0.8f ? kBusy : kTown;
        }
        switch (b.sc) {
            case kQuiet:
                b.cx = o.sea_origin + kSea / 2;
                b.cz = o.sea_origin + kSea / 2;
                b.radius = kSea / 2 - 1000;
                b.x = o.sea_origin + 1000 + u(rng) * (kSea - 2000);
                b.z = o.sea_origin + 1000 + u(rng) * (kSea - 2000);
                b.speed = 600 + u(rng) * 1000;
                break;
            case kBusy: {
                auto& h = hot[rng() % hot.size()];
                b.cx = h.first;
                b.cz = h.second;
                b.radius = 15000;
                float a = u(rng) * 6.2832f, d = std::sqrt(u(rng)) * b.radius;
                b.x = b.cx + std::cos(a) * d;
                b.z = b.cz + std::sin(a) * d;
                b.speed = 300 + u(rng) * 900;
                break;
            }
            case kTown: {
                b.cx = 0;
                b.cz = 0;
                b.radius = 2500;
                float a = u(rng) * 6.2832f, d = std::sqrt(u(rng)) * b.radius;
                b.x = std::cos(a) * d;
                b.z = std::sin(a) * d;
                b.speed = 150 + u(rng) * 250;
                break;
            }
        }
        b.heading = u(rng) * 6.2832f;
        b.c = std::make_unique<Client>(&sock);
    }

    Bytes blob(size_t(o.pose_bytes));
    for (auto& x : blob) x = uint8_t(rng());
    const uint32_t period = uint32_t(1000 / std::max(1, o.pose_hz));
    uint32_t t0 = monotonic_ms();
    uint32_t warm_at = t0 + uint32_t(o.warmup_s) * 1000, end_at = warm_at + uint32_t(o.duration_s) * 1000;
    std::unordered_map<uint32_t, Bot*> by_conn, by_nonce;
    uint32_t last_index = 0;
    int started = 0;
    uint8_t buf[2048];
    Address from;
    uint64_t unknown = 0, total_poses = 0;
    bool warmed = false;
    uint32_t last_print = t0;
    auto reindex = [&] {
        by_conn.clear();
        by_nonce.clear();
        for (auto& b : bots) {
            if (!b.started) continue;
            if (b.c->state() == ClientState::kOnline) by_conn[b.c->conn_id()] = &b;
            by_nonce[b.c->nonce()] = &b;
        }
        last_index = monotonic_ms();
    };
    while (true) {
        uint32_t now = monotonic_ms();
        if (int32_t(now - end_at) >= 0) break;
        if (rss_mb() > uint64_t(o.max_rss_mb)) {
            std::fprintf(stderr, "bots: over %d MB RSS, stopping\n", o.max_rss_mb);
            break;
        }
        // ramp up
        int want = std::min(o.count, int(uint64_t(now - t0) * uint64_t(o.ramp_per_s) / 1000 + 1));
        bool any_new = false;
        while (started < want) {
            Bot& b = bots[size_t(started)];
            ClientConfig cfg;
            cfg.host = o.host;
            cfg.port = o.port;
            cfg.game_data = o.game_data;
            cfg.identity = crypto::Identity::generate();
            cfg.name = "bot" + std::to_string(started);
            cfg.device = int(rng() % 100) < o.phone_percent ? kPhone : kBot;
            cfg.send_hz = o.pose_hz;
            cfg.tag = "bots";
            if (!b.c->start(cfg, why)) {
                std::fprintf(stderr, "bots: %s\n", why.c_str());
                return 1;
            }
            b.started = true;
            b.next_at = now + uint32_t(rng() % period);
            ++started;
            any_new = true;
        }
        if (any_new) reindex();
        // datagrams in
        if (sock.wait(1)) {
            for (int i = 0; i < 100000; ++i) {
                int n = sock.recv_from(from, buf, sizeof buf);
                if (n < 0) break;
                Bot* b = nullptr;
                if (buf[0] == kPktData && n >= 5) {
                    uint32_t cid = uint32_t(buf[1]) | (uint32_t(buf[2]) << 8) | (uint32_t(buf[3]) << 16) | (uint32_t(buf[4]) << 24);
                    auto it = by_conn.find(cid);
                    if (it == by_conn.end() && monotonic_ms() - last_index > 50) {
                        reindex();
                        it = by_conn.find(cid);
                    }
                    if (it != by_conn.end()) b = it->second;
                } else {
                    uint32_t nonce = 0;
                    if (handshake_nonce(buf, size_t(n), nonce)) {
                        auto it = by_nonce.find(nonce);
                        if (it == by_nonce.end() && monotonic_ms() - last_index > 50) {
                            reindex();
                            it = by_nonce.find(nonce);
                        }
                        if (it != by_nonce.end()) b = it->second;
                    }
                }
                if (b) b->c->on_datagram(buf, size_t(n), monotonic_ms());
                else ++unknown;
            }
        }
        // bots due
        now = monotonic_ms();
        if (!warmed && int32_t(now - warm_at) >= 0) {
            warmed = true;
            for (auto& b : bots) {
                b.down_at_warm = b.c->stats().bytes_down;
                b.up_at_warm = b.c->stats().bytes_up;
                b.counted = b.c->state() == ClientState::kOnline;
                b.poses = 0;
            }
            total_poses = 0;
        }
        bool reidx = false;
        for (int i = 0; i < started; ++i) {
            Bot& b = bots[size_t(i)];
            if (int32_t(now - b.next_at) < 0) continue;
            b.next_at += period;
            if (int32_t(now - b.next_at) > int32_t(period) * 4) b.next_at = now + period;
            // move: wander inside the area
            float dt = period / 1000.0f;
            b.heading += (u(rng) - 0.5f) * 0.3f;
            float dx = b.x - b.cx, dz = b.z - b.cz;
            if (dx * dx + dz * dz > b.radius * b.radius) b.heading = std::atan2(-dz, -dx) + (u(rng) - 0.5f);
            b.x += std::cos(b.heading) * b.speed * dt;
            b.z += std::sin(b.heading) * b.speed * dt;
            ClientState before = b.c->state();
            for (auto& e : b.c->poll(now)) {
                if (e.kind == ClientEvent::kPoses) {
                    for (auto& p : e.poses) {
                        if (p.tier == kTierNear) ++b.near;
                        else if (p.tier == kTierMid) ++b.mid;
                        else ++b.far;
                    }
                    b.poses += e.poses.size();
                    total_poses += e.poses.size();
                } else if (e.kind == ClientEvent::kPlayerIn) {
                    ++b.players_seen;
                } else if (e.kind == ClientEvent::kPlayerOut) {
                    --b.players_seen;
                }
            }
            if (b.c->state() != before) reidx = true;
            if (b.c->state() == ClientState::kOnline) {
                PoseHeader h;
                h.where = b.sc == kTown ? Where::of("town", 0) : Where::of("sea", 1);
                h.x = b.x;
                h.z = b.z;
                h.y = 0;
                h.yaw = uint16_t(b.heading / 6.2832f * 65536.0f);
                h.anim = 3;
                h.flags = b.sc == kTown ? 0 : 3;
                b.c->send_pose(h, blob.data(), blob.size());
            }
        }
        if (reidx) reindex();
        if (now - last_print > 5000) {
            last_print = now;
            int online = 0;
            for (int i = 0; i < started; ++i) online += bots[size_t(i)].c->state() == ClientState::kOnline;
            std::fprintf(stderr, "bots: %d/%d online, rss %llu MB, unknown datagrams %llu\n", online, o.count,
                         (unsigned long long)rss_mb(), (unsigned long long)unknown);
        }
    }
    // report
    double secs = o.duration_s;
    std::vector<double> down[3], up[3], seen[3];
    int online_end = 0;
    for (auto& b : bots) {
        bool on = b.c->state() == ClientState::kOnline;
        online_end += on;
        if (!b.counted || !on) continue;
        down[b.sc].push_back(double(b.c->stats().bytes_down - b.down_at_warm) / secs);
        up[b.sc].push_back(double(b.c->stats().bytes_up - b.up_at_warm) / secs);
        seen[b.sc].push_back(b.players_seen);
    }
    std::string j = "{\"bots\":" + std::to_string(o.count) + ",\"online_at_end\":" + std::to_string(online_end) +
                    ",\"duration_s\":" + std::to_string(o.duration_s) + ",\"pose_bytes\":" + std::to_string(o.pose_bytes) +
                    ",\"rss_mb\":" + std::to_string(rss_mb()) + ",\"scenarios\":{";
    bool first = true;
    for (int s = 0; s < 3; ++s) {
        if (down[s].empty()) continue;
        char b[600];
        double sum = 0;
        for (double d : down[s]) sum += d;
        std::snprintf(b, sizeof b,
                      "%s\"%s\":{\"n\":%zu,\"down_mean_Bps\":%.0f,\"down_p50_Bps\":%.0f,\"down_p90_Bps\":%.0f,"
                      "\"down_p99_Bps\":%.0f,\"down_max_Bps\":%.0f,\"up_p50_Bps\":%.0f,\"up_max_Bps\":%.0f,"
                      "\"visible_p50\":%.0f,\"visible_max\":%.0f}",
                      first ? "" : ",", kScenarioName[s], down[s].size(), sum / double(down[s].size()), pct(down[s], 0.5),
                      pct(down[s], 0.9), pct(down[s], 0.99), pct(down[s], 1.0), pct(up[s], 0.5), pct(up[s], 1.0),
                      pct(seen[s], 0.5), pct(seen[s], 1.0));
        j += b;
        first = false;
    }
    j += "}}\n";
    std::fputs(j.c_str(), stdout);
    if (!o.report.empty())
        if (std::FILE* f = std::fopen(o.report.c_str(), "w")) {
            std::fputs(j.c_str(), f);
            std::fclose(f);
        }
    for (auto& b : bots) b.c->stop();
    return 0;
}
