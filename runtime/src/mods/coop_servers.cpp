// SPDX-License-Identifier: MPL-2.0
// The saved servers and the player's words for the server's answers (coop_servers.h).
#include "coop_servers.h"

#include <algorithm>
#include <cstdlib>

#include "../online/proto/protocol.h"

namespace mods::coop::servers {
namespace {

namespace op = online::proto;

std::string clean(std::string s, size_t most) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '|' || c == ';' || (unsigned char)c < 0x20; }), s.end());
    if (s.size() > most) s.resize(most);
    return s;
}
std::vector<std::string> split(const std::string& s, char by) {
    std::vector<std::string> out(1);
    for (const char c : s) {
        if (c == by) out.emplace_back();
        else out.back().push_back(c);
    }
    return out;
}
// the operator's words after ours, unless they say nothing more than ours
std::string with(const std::string& ours, const std::string& theirs) {
    if (theirs.empty()) return ours;
    std::string a = ours, b = theirs;
    for (char& c : a) c = (char)tolower((unsigned char)c);
    for (char& c : b) c = (char)tolower((unsigned char)c);
    if (a.find(b) != std::string::npos) return ours;
    std::string head = ours;
    if (!head.empty() && head.back() == '.') head.pop_back();
    return head + " (the server says: " + theirs + ").";
}
std::string again(int retry_s) { return retry_s > 0 ? " Trying again in " + std::to_string(retry_s) + " s." : " Trying again soon."; }
std::string capital(std::string s) {
    if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
    return s;
}

}  // namespace

std::vector<Server> parse(const std::string& text) {
    std::vector<Server> out;
    if (text.empty()) return out;
    for (const std::string& e : split(text, ';')) {
        const std::vector<std::string> f = split(e, '|');
        if (f.size() < 3 || f[1].empty()) continue;
        Server s;
        s.name = f[0], s.addr = f[1], s.port = atoi(f[2].c_str());
        if (s.port < 1 || s.port > 65535) continue;
        if (f.size() > 3) {
            unsigned char k[32];
            if (from_hex(f[3], k, 32)) s.key = f[3];
        }
        if (s.name.empty()) s.name = s.addr;
        if (!find(out, s.addr, s.port) && out.size() < kMax) out.push_back(s);
    }
    return out;
}

std::string format(const std::vector<Server>& list) {
    std::string out;
    for (const Server& s : list) {
        if (!out.empty()) out += ';';
        out += clean(s.name, 48) + '|' + clean(s.addr, 120) + '|' + std::to_string(s.port) + '|' + s.key;
    }
    return out;
}

void remember(std::vector<Server>& list, Server s) {
    s.name = clean(s.name, 48), s.addr = clean(s.addr, 120);
    if (s.addr.empty() || s.port < 1 || s.port > 65535) return;
    if (s.name.empty()) s.name = s.addr;
    if (const Server* old = find(list, s.addr, s.port); old && s.key.empty()) s.key = old->key;
    forget(list, s.addr, s.port);
    list.insert(list.begin(), s);
    if (list.size() > kMax) list.resize(kMax);
}

bool forget(std::vector<Server>& list, const std::string& addr, int port) {
    const size_t n = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Server& s) { return s.addr == addr && s.port == port; }), list.end());
    return list.size() != n;
}

const Server* find(const std::vector<Server>& list, const std::string& addr, int port) {
    for (const Server& s : list) if (s.addr == addr && s.port == port) return &s;
    return nullptr;
}

std::string to_hex(const unsigned char* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; i++) out += d[b[i] >> 4], out += d[b[i] & 15];
    return out;
}

bool from_hex(const std::string& hex, unsigned char* out, size_t n) {
    if (hex.size() != n * 2) return false;
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < n; i++) {
        const int hi = v(hex[2 * i]), lo = v(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (unsigned char)(hi << 4 | lo);
    }
    return true;
}

std::string refusal(int code, const std::string& server_text, int retry_s) {
    switch ((op::Refusal)code) {
    case op::Refusal::kProtocol:
        return "This server runs another online version than your game. Update the port, or ask the server's operator to update the server.";
    case op::Refusal::kGameData:
        return "This server plays another game: another version, region or randomizer seed. Everyone needs the same game as the server.";
    case op::Refusal::kFull: return with("The server is full.", server_text) + again(retry_s);
    case op::Refusal::kBanned: return with("You are banned from this server.", server_text);
    case op::Refusal::kBadProof:
        return "The server did not accept this game's join proof. Choose Connect again; if it happens again, update the port.";
    case op::Refusal::kRateLimited: return "Too many connections from your network to this server." + again(retry_s);
    case op::Refusal::kInviteOnly:
        return with("This server is invite-only: ask its operator to let your friend code in.", server_text);
    case op::Refusal::kMalformed: return "The server could not read this game's request. Update the port, or ask the server's operator.";
    case op::Refusal::kShuttingDown: return with("The server is shutting down.", server_text) + again(retry_s);
    case op::Refusal::kKicked: return with("The server removed you.", server_text);
    case op::Refusal::kNone: break;
    }
    return with("The server refused the connection.", server_text);
}

std::string notice(int kind, int reason, int seconds, const std::string& server_text) {
    auto limit = [&](bool removed) -> std::string {
        const char* what = "";
        switch ((op::KickReason)reason) {
        case op::kLimitUpload: what = "this game sent more data than the server allows"; break;
        case op::kLimitPackets: what = "this game sent more packets than the server allows"; break;
        case op::kLimitReliable:
        case op::kLimitApp: what = "this game sent more messages than the server allows"; break;
        case op::kLimitPose: what = "this game sent poses the server could not use"; break;
        default: what = "this game went over one of the server's limits"; break;
        }
        return removed ? with(std::string("The server removed you: ") + what + ".", server_text) + " Choose Connect again to join again."
                       : with(std::string("The server asks to slow down: ") + what + ".", server_text);
    };
    switch (kind) {
    case op::kNoticeRestart:
        return seconds > 0 ? "The server restarts in " + std::to_string(seconds) + " s. The game connects again by itself."
                           : std::string("The server restarts. The game connects again by itself.");
    case op::kNoticeKick:
        if (server_text == "signed in from another place") return "You went online on this server from another game with the same identity, so this one was signed out.";
        return with("The server removed you.", server_text) + " Choose Connect again to join again.";
    case op::kNoticeBan: return with("You are banned from this server.", server_text);
    case op::kNoticeThrottle: return limit(false);
    case op::kNoticeKickLimit: return limit(true);
    case op::kNoticeInfo: return server_text.empty() ? std::string("A message from the server.") : "The server says: " + server_text;
    }
    return server_text.empty() ? std::string("A notice from the server.") : "The server says: " + server_text;
}

std::string party(int code, const std::string& server_text) {
    switch (code) {
    case 0:
        if (server_text == "invite sent") return "Invite sent. Your friend accepts it in Settings > Online.";
        if (server_text == "joined the party") return "You joined the party.";
        return capital(server_text.empty() ? "done" : server_text) + ".";
    case 1: return "Wait a little before the next invite.";
    case 2: return "That player is not online on this server. Both of you must be on the same server; check the code.";
    case 3: return "That is your own friend code. Enter your friend's code.";
    case 4: return "That player is already in your party.";
    case 5: return "The party is full (the server's limit).";
    case 6: return "That party is gone: its players left.";
    case 7: return "The invite expired. Ask your friend to invite you again.";
    case 8: return capital(server_text.empty() ? "your friend declined" : server_text) + ".";
    case 9: return "Your party could not be restored after the server came back. Invite your friends again.";
    }
    return capital(server_text.empty() ? "the party action failed" : server_text) + ".";
}

std::string key_changed(const std::string& server) {
    return "The key of " + server + " is not the one it had on your first visit: it may be another server using its address. "
           "If its operator says the key changed, choose Forget under Saved servers and connect again.";
}

std::string bad_code() {
    return "That is not a friend code. A friend code looks like KQ7F-9MZ2-XD; check the letters and numbers.";
}

}  // namespace mods::coop::servers
