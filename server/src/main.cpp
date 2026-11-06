// SPDX-License-Identifier: MPL-2.0
// wwhd-online-server: run the server, or send it an admin command over its local socket.
//   wwhd-online-server [--config server.toml] [--bind 127.0.0.1] [--port 7777]
//   wwhd-online-server admin [--socket wwhd-online-admin.sock] <command ...>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"
#include "proto/protocol.h"
#include "server.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace online;

static server::Server* g_server = nullptr;
static void on_signal(int) {
    if (g_server) g_server->stop();
}

static int admin_main(int argc, char** argv) {
#ifdef _WIN32
    std::fprintf(stderr, "the admin socket is not available on Windows yet\n");
    return 2;
#else
    std::string path = "wwhd-online-admin.sock", cmd;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--socket") && i + 1 < argc) {
            path = argv[++i];
            continue;
        }
        if (!cmd.empty()) cmd += ' ';
        cmd += argv[i];
    }
    if (cmd.empty()) cmd = "help";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        std::fprintf(stderr, "cannot reach the server's admin socket %s\n", path.c_str());
        return 1;
    }
    cmd += "\n";
    (void)send(fd, cmd.data(), cmd.size(), 0);
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof buf, 0)) > 0) fwrite(buf, 1, size_t(n), stdout);
    close(fd);
    return 0;
#endif
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "admin")) return admin_main(argc, argv);
    server::Config cfg;
    std::string why, config_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--config") {
            config_path = next();
            if (!server::load_config(config_path, cfg, why)) {
                std::fprintf(stderr, "%s: %s\n", config_path.c_str(), why.c_str());
                return 2;
            }
        } else if (a == "--bind") {
            cfg.bind = next();
        } else if (a == "--port") {
            cfg.port = uint16_t(std::atoi(next().c_str()));
        } else if (a == "--version") {
            std::printf("wwhd-online-server, protocol %u\n", unsigned(proto::kProtoVersion));
            return 0;
        } else {
            std::fprintf(stderr,
                         "usage: wwhd-online-server [--config server.toml] [--bind addr] [--port n]\n"
                         "       wwhd-online-server admin [--socket path] <status|players|kick|ban|unban|bans|notice|restart|parties|stop> ...\n");
            return 2;
        }
    }
    server::Server s(cfg);
    if (!s.start(why)) {
        std::fprintf(stderr, "wwhd-online-server: %s\n", why.c_str());
        return 1;
    }
    g_server = &s;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    s.run();
    std::fprintf(stderr, "wwhd-online-server: stopped\n");
    return 0;
}
