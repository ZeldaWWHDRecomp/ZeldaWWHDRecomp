// SPDX-License-Identifier: MPL-2.0
// Online co-op's direct UDP transport (mods/coop_net.cpp) without the game, several processes on 127.0.0.1:
//   - a guest of another game data is refused with a reason;
//   - host migration: a host and two guests; the host goes; one guest hosts, the other joins it;
//   - malformed packets: random and cut datagrams at the host do not crash it or make a player of them.
#include "mods/coop_net.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

namespace mods {
void trace(const char* fmt, ...) {
    if (!getenv("COOP_NET_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%d] ", getpid());
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
}  // namespace mods
namespace ss { uint64_t game_data_id() { return 0x51F3510EA6EE8C1Full; } }

namespace net = mods::coop::net;

namespace {

struct Counter : net::Handler {
    int joins = 0, leaves = 0, messages = 0;
    std::string why;
    void message(int, net::Msg, const uint8_t*, size_t) override { messages++; }
    void state(int, uint8_t, uint32_t, const uint8_t*, size_t) override {}
    void joined(int) override { joins++; }
    void left(int, const char* w) override { leaves++, why = w ? w : ""; }
};

// run the transport for `ms`, stopping early when `done` says so
template <class F> void run(Counter& c, uint32_t ms, F done) {
    const uint32_t end = net::now_ms() + ms;
    while ((int32_t)(net::now_ms() - end) < 0 && !done()) {
        net::poll(c);
        usleep(10000);
    }
}
int connected() {
    int n = 0;
    for (int i = 0; i < net::kMaxPlayers; i++) n += net::connected(i);
    return n;
}

int fails = 0;
void check(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    fflush(stdout);
    if (!ok) fails++;
}

// a child process running `f`, its exit code its result
template <class F> pid_t child(F f) {
    const pid_t p = fork();
    if (p == 0) _exit(f());
    return p;
}
int wait_ok(pid_t p) {
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 99;
}

void refused_game(int port) {
    const pid_t host = child([&] {
        Counter c;
        std::string why;
        if (!net::start(net::Role::kHost, "", port, "Host", 0, why)) return 1;
        run(c, 3000, [] { return false; });
        net::stop(nullptr);
        return c.joins ? 2 : 0;  // (nobody may have joined)
    });
    usleep(200000);
    const pid_t guest = child([&] {
        setenv("WWHD_COOP_GAME", "1234", 1);
        Counter c;
        std::string why;
        if (!net::start(net::Role::kGuest, "127.0.0.1", port, "Guest", 1, why)) return 1;
        run(c, 2500, [&] { return net::role() == net::Role::kOff; });
        return c.why.find("Different game") != std::string::npos ? 0 : 3;
    });
    check(wait_ok(guest) == 0, "a guest of another game is refused with the reason");
    check(wait_ok(host) == 0, "the host made no player of it");
}

void migration(int port) {
    const pid_t host = child([&] {
        Counter c;
        std::string why;
        if (!net::start(net::Role::kHost, "", port, "Host", 0, why)) return 1;
        run(c, 2500, [] { return false; });
        return connected() == 3 ? 0 : 2;  // (both guests in; then gone without a word: _exit)
    });
    usleep(200000);
    auto guest = [&](const char* name, int delay_ms) {
        return child([=] {
            usleep(delay_ms * 1000);
            Counter c;
            std::string why;
            if (!net::start(net::Role::kGuest, "127.0.0.1", port, name, 1, why)) return 1;
            run(c, 2300 - delay_ms, [] { return false; });
            if (connected() != 3) return 2;
            const int was = net::my_id();
            // the host is gone: within the timeout plus the new host's start, two players again
            run(c, 12000, [] { return connected() == 2 && strncmp(net::status(), "Joining", 7) != 0; });
            if (connected() != 2) return 3;
            if (was == 1 && net::role() != net::Role::kHost) return 4;   // the lowest guest hosts
            if (was == 2 && (net::role() != net::Role::kGuest || net::my_id() == 0)) return 5;  // the other joined it
            run(c, 3000, [] { return false; });  // (both still there a while later)
            return connected() == 2 ? 0 : 6;
        });
    };
    const pid_t g1 = guest("One", 0), g2 = guest("Two", 400);
    check(wait_ok(host) == 0, "migration: the host had both guests");
    const int r1 = wait_ok(g1), r2 = wait_ok(g2);
    printf("  (guest one %d, guest two %d)\n", r1, r2);
    check(r1 == 0 && r2 == 0, "migration: after the host left, the first guest hosts and the second joined it");
}

void malformed(int port) {
    const pid_t host = child([&] {
        Counter c;
        std::string why;
        if (!net::start(net::Role::kHost, "", port, "Host", 0, why)) return 1;
        run(c, 2500, [] { return false; });
        return connected() == 1 ? 0 : 2;  // (only itself)
    });
    usleep(200000);
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    to.sin_family = AF_INET, to.sin_port = htons(port), to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::mt19937 rng(7);
    for (int k = 0; k < 4000; k++) {
        uint8_t b[1400];
        const size_t n = rng() % sizeof b;
        for (size_t i = 0; i < n; i++) b[i] = (uint8_t)rng();
        if (k % 2 && n >= 8) memcpy(b, "WWCO", 4), b[4] = net::kProto, b[5] = 0, b[6] = 0;  // (a valid head, then noise)
        sendto(s, b, n, 0, (const sockaddr*)&to, sizeof to);
        if (k % 200 == 0) usleep(5000);
    }
    close(s);
    check(wait_ok(host) == 0, "4000 malformed datagrams: the host runs on and makes no player of them");
}

}  // namespace

int main() {
    const int base = 40000 + getpid() % 20000;
    refused_game(base);
    migration(base + 1);
    malformed(base + 2);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
