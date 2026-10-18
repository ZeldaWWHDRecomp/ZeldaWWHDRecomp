// World editor MCP interface: the HTTP/1.1 listener (mcp.h has the design). Loopback addresses only.
#pragma once
#include <memory>
#include <string>

#include "mcp.h"

namespace editor_mcp {

class Server {
public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    // Listens on 127.0.0.1 (and ::1 when the system has it) at `port` or one of the next nine, else at any
    // free port; each connection runs on a thread of its own (kMaxConnections at once).
    bool start(std::shared_ptr<Handler> handler, int port, std::string& why);
    void stop();  // stops listening; open connections end within a second
    bool running() const;
    int port() const;
    int connections() const;

    struct State;

private:
    std::shared_ptr<State> s_;
};

}  // namespace editor_mcp
