// remote_pad_drive: a stand-in for the iPhone GamePad app (runtime/src/remote_pad), for tests on a device
// nobody is holding. It pairs with the game, reading the pairing code from the game's log (started with
// WWHD_PAD_LOG_CODE=1), keeps its token in a state file so later runs skip pairing, then plays a script of
// button presses. The game's port: Bonjour's _wwhd-gamepad._udp (`dns-sd -B _wwhd-gamepad._udp`), 26770 by default.
//
//   remote_pad_drive <host> <port> <state file> <game log> <script>
//   script: space-separated steps: "wait:<s>", "press:<button>[:<s>]" (held for that long, default 0.15),
//           buttons by position as the protocol names them: A B X Y LB RB L3 R3 Menu Options Home Up Down Left Right
#include "remote_pad/protocol.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

using namespace remote_pad;
static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    if (argc < 6) { fprintf(stderr, "usage: remote_pad_drive host port statefile gamelog script\n"); return 2; }
    const char* host = argv[1];
    const int port = atoi(argv[2]);
    const std::string stateFile = argv[3], codeLog = argv[4], script = argv[5];

    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) || !res) { fprintf(stderr, "cannot resolve %s\n", host); return 1; }
    int fd = socket(res->ai_family, SOCK_DGRAM, 0);
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) { perror("connect"); return 1; }
    timeval tv{0, 20000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    // the phone's identity: kept between runs, so pairing happens once
    Hello hello;
    hello.device = "remote_pad_drive";
    hello.controller = "Scripted";
    {
        std::ifstream in(stateFile, std::ios::binary);
        if (!in.read((char*)hello.id.data(), 16) || !in.read((char*)hello.token.data(), 16)) {
            std::random_device rd;
            for (auto& b : hello.id) b = uint8_t(rd());
            hello.token = {};
        }
    }
    auto save = [&] {
        std::ofstream out(stateFile, std::ios::binary);
        out.write((const char*)hello.id.data(), 16);
        out.write((const char*)hello.token.data(), 16);
    };
    hello.nonce = uint32_t(now_ns());

    uint32_t session = 0;
    uint8_t buf[2048];
    uint64_t lastHello = 0, lastPair = 0;
    const uint64_t start = now_ns();
    while (!session) {
        if (now_ns() - start > 120'000'000'000ull) { fprintf(stderr, "no session after 120 s\n"); return 1; }
        if (now_ns() - lastHello > 500'000'000ull) {
            auto p = encode_hello(hello);
            send(fd, p.data(), p.size(), 0);
            lastHello = now_ns();
        }
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) continue;
        auto h = parse_header(buf, size_t(n));
        if (!h) continue;
        if (h->type == kPairing) {
            auto pr = parse_pairing(buf, size_t(n));
            if (!pr || pr->state == kNoCode || now_ns() - lastPair < 1'000'000'000ull) continue;
            // the code the game logged (the newest one)
            std::ifstream log(codeLog);
            std::string line, code;
            static const std::regex re("pairing code ([0-9]{6})");
            std::smatch m;
            while (std::getline(log, line))
                if (std::regex_search(line, m, re)) code = m[1];
            if (code.empty()) continue;
            PairRequest req{hello.nonce, hello.id, uint32_t(atoi(code.c_str()))};
            auto p = encode_pair(req);
            send(fd, p.data(), p.size(), 0);
            lastPair = now_ns();
            printf("pairing with code %s\n", code.c_str());
        } else if (h->type == kPaired) {
            if (auto pd = parse_paired(buf, size_t(n))) {
                hello.token = pd->token;
                save();
                printf("paired\n");
                lastHello = 0;
            }
        } else if (h->type == kWelcome) {
            if (auto w = parse_welcome(buf, size_t(n)); w && w->nonce == hello.nonce) {
                session = h->session;
                save();
                printf("connected to %s (session %u)\n", w->host.c_str(), session);
            }
        }
    }

    static const std::map<std::string, uint32_t> names = {
        {"A", kA}, {"B", kB}, {"X", kX}, {"Y", kY}, {"LB", kLB}, {"RB", kRB}, {"L3", kL3}, {"R3", kR3},
        {"Menu", kMenu}, {"Options", kOptions}, {"Home", kHome}, {"Up", kDUp}, {"Down", kDDown},
        {"Left", kDLeft}, {"Right", kDRight}};
    Input in;
    in.controller = true;
    auto send_for = [&](uint32_t buttons, double seconds) {
        const uint64_t until = now_ns() + uint64_t(seconds * 1e9);
        uint64_t lastKeep = 0;
        do {
            in.seq++;
            in.time_ns = now_ns();
            in.buttons = buttons;
            auto p = encode_input(session, in);
            send(fd, p.data(), p.size(), 0);
            if (now_ns() - lastKeep > 1'000'000'000ull) {  // say Hello again now and then: the game keeps the session
                auto hp = encode_hello(hello);
                send(fd, hp.data(), hp.size(), 0);
                lastKeep = now_ns();
            }
            while (recv(fd, buf, sizeof buf, MSG_DONTWAIT) > 0) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        } while (now_ns() < until);
    };
    std::istringstream steps(script);
    std::string step;
    while (steps >> step) {
        std::vector<std::string> f;
        std::stringstream ss(step);
        for (std::string part; std::getline(ss, part, ':');) f.push_back(part);
        if (f[0] == "wait" && f.size() > 1) {
            send_for(0, atof(f[1].c_str()));
        } else if (f[0] == "press" && f.size() > 1 && names.count(f[1])) {
            printf("press %s\n", f[1].c_str());
            fflush(stdout);
            send_for(names.at(f[1]), f.size() > 2 ? atof(f[2].c_str()) : 0.15);
            send_for(0, 0.1);
        } else {
            fprintf(stderr, "unknown step %s\n", step.c_str());
        }
    }
    auto bye = encode_bye(session);
    send(fd, bye.data(), bye.size(), 0);
    printf("done\n");
    return 0;
}
