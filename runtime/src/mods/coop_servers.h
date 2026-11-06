// SPDX-License-Identifier: MPL-2.0
// Online co-op through a server (GAPS C8): the player's saved servers and the words the Online page and the game's
// notices use for every refusal, removal and party answer the server gives. No network and no game here (the unit
// test runtime/tools/coop_servers_test.cpp runs it alone); coop_net_server.cpp keeps the list in the settings.
#pragma once
#include <string>
#include <vector>

namespace mods::coop::servers {

// a server this player has been on: kept most recent first, its key pinned on the first visit (trust on first use)
struct Server {
    std::string name;  // as the server calls itself (or the address)
    std::string addr;  // address or host name
    int port = 7777;
    std::string key;   // the server's key, 64 hex digits; empty: not pinned yet
};
constexpr size_t kMax = 10;

// the settings' form: entries "name|addr|port|key" separated by ';' (the separators are dropped from names)
std::vector<Server> parse(const std::string& text);
std::string format(const std::vector<Server>& list);
// the server first in the list; the same address and port replaced. An empty key keeps the one pinned before
void remember(std::vector<Server>& list, Server s);
bool forget(std::vector<Server>& list, const std::string& addr, int port);
const Server* find(const std::vector<Server>& list, const std::string& addr, int port);
// 64 hex digits <-> 32 bytes
std::string to_hex(const unsigned char* bytes, size_t n);
bool from_hex(const std::string& hex, unsigned char* out, size_t n);

// ---- what the player is told (plain words, what happened and what to do) ----
// the server refused the connection (online::proto::Refusal); `server_text` the operator's own words, if any;
// `retry_s` > 0 when the game tries again by itself
std::string refusal(int code, const std::string& server_text, int retry_s);
// a server notice (online::proto::NoticeKind; `reason` the KickReason of a limit notice, `seconds` a restart's delay)
std::string notice(int kind, int reason, int seconds, const std::string& server_text);
// the answer to a party action (the server's party result codes, server/src/party.cpp)
std::string party(int code, const std::string& server_text);
// the server's key is not the one pinned on the first visit
std::string key_changed(const std::string& server);
// a friend code the game cannot read (typed wrong)
std::string bad_code();

}  // namespace mods::coop::servers
