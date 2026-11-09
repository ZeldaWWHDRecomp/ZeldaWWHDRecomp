// SPDX-License-Identifier: MPL-2.0
// Text chat (docs/online-server.md, "Chat"): the server checks every line (chat on, the global channel allowed, the
// length, the rate, the operator's mutes, the word filter) and passes it on to the sender's party or to everyone on
// the server. Anything else is dropped, with the reason in the log and the metrics, and the sender is told why. No
// line is kept unless the operator turns the chat log on (chat.log).
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "server.h"

namespace online::server {

using namespace online::proto;

namespace {
int64_t unix_now() { return int64_t(std::time(nullptr)); }
const char* channel_name(uint8_t c) { return c == kChatGlobal ? "global" : "party"; }
}  // namespace

void Server::chat_welcome(Session& s) {
    ChatRules r;
    r.enabled = cfg_.chat_enabled;
    r.global = cfg_.chat_enabled && cfg_.chat_global;
    r.max_length = uint16_t(cfg_.chat_max_length);
    r.rate_count = uint8_t(cfg_.chat_rate_count);
    r.rate_window_s = uint16_t(cfg_.chat_rate_window_s);
    Bytes b;
    Writer w(b);
    put_chat_rules(w, r);
    s.chan.send_reliable(kMsgChatRules, b);
}

void Server::on_chat_prefs(Session& s, Reader& r) {
    const uint8_t off = r.u8();
    if (r.done()) s.chat_off = off & 3;
}

void Server::chat_drop(Session& s, uint8_t channel, uint8_t code, const std::string& detail, uint32_t now) {
    if (code < std::size(metrics_.chat_drops)) metrics_.chat_drops[code]++;
    // (a player over the rate limit: written once per window, not every line)
    const bool quiet = code == kChatTooFast && s.chat_fast_logged && now - s.chat_fast_logged < uint32_t(cfg_.chat_rate_window_s) * 1000;
    if (!quiet) {
        if (code == kChatTooFast) s.chat_fast_logged = now ? now : 1;
        log("chat: dropped a " + std::string(channel_name(channel)) + " line from " + friend_code(s.key_hash) + ": " +
            chat_result_text(code) + (detail.empty() ? "" : " (" + detail + ")"));
    }
    s.chan.send_reliable(kMsgChatResult, encode_chat_result(code, channel, chat_result_text(code)));
}

bool Server::chat_deliver(Session& to, uint16_t from_id, uint64_t from_key, uint32_t from_party, const std::string& from_name,
                          uint8_t channel, const std::string& text, bool yours) {
    if (!to.online || to.dead || to.redirected) return false;
    if (!yours && (to.chat_off & (1u << channel))) return false;  // the receiver turned this channel off
    if (!yours && !sees(to, from_key)) {                          // a block, either way (sees: both lists)
        metrics_.blocked_drops++;
        return false;
    }
    const bool party = from_party && from_party == to.party;
    ChatLine l;
    l.from = from_id;
    l.key_hash = from_key;
    l.channel = channel;
    l.flags = uint8_t((party || yours ? kChatFromParty : 0) | (yours ? kChatYours : 0));
    // the name this receiver sees elsewhere (PLAYER_IN): the display name in the party, else the stranger name
    l.name = party || yours ? from_name : stranger_name(from_key);
    l.text = text;
    to.chan.send_reliable(kMsgChatFrom, encode_chat_line(l));
    if (!yours) metrics_.chat_delivered++;
    return true;
}

void Server::on_chat(Session& s, Reader& r, uint32_t now) {
    const uint8_t channel = r.u8();
    const size_t len = r.u16();
    if (len > kChatMaxBytes) return chat_drop(s, channel, kChatTooLong, std::to_string(len) + " bytes", now);
    std::string raw(r.view(len));
    if (!r.done()) return;
    if (!cfg_.chat_enabled) return chat_drop(s, channel, kChatOff, "", now);
    if (channel >= kChatChannels) return chat_drop(s, channel, kChatBadChannel, std::to_string(channel), now);
    if (channel == kChatGlobal && !cfg_.chat_global) return chat_drop(s, channel, kChatNoGlobal, "", now);
    std::string why;
    if (chat_muted(s.key_hash, &why)) return chat_drop(s, channel, kChatMuted, why, now);
    std::string text;
    const size_t n = chat_clean(raw, text);
    if (!n) return chat_drop(s, channel, kChatEmpty, "", now);
    if (n > size_t(cfg_.chat_max_length))
        return chat_drop(s, channel, kChatTooLong, std::to_string(n) + " > " + std::to_string(cfg_.chat_max_length) + " characters", now);
    Party* p = channel == kChatParty ? party_of(s) : nullptr;
    if (channel == kChatParty && !p) return chat_drop(s, channel, kChatNoParty, "", now);
    // the rate limit: at most rate_count lines in any rate_window_s
    const uint32_t window = uint32_t(cfg_.chat_rate_window_s) * 1000;
    while (!s.chat_times.empty() && now - s.chat_times.front() >= window) s.chat_times.pop_front();
    if (s.chat_times.size() >= size_t(cfg_.chat_rate_count))
        return chat_drop(s, channel, kChatTooFast,
                         std::to_string(cfg_.chat_rate_count) + " lines per " + std::to_string(cfg_.chat_rate_window_s) + " s", now);
    s.chat_times.push_back(now);
    chat_filter_check(now);
    if (const size_t hits = chat_filter_.apply(text)) metrics_.chat_filtered += hits;
    metrics_.chat_lines++;
    if (!cfg_.chat_log_file.empty())
        if (std::FILE* f = std::fopen(cfg_.chat_log_file.c_str(), "a")) {
            std::fprintf(f, "%lld %s %s %s: %s\n", (long long)unix_now(), channel_name(channel), friend_code(s.key_hash).c_str(),
                         s.name.c_str(), text.c_str());
            std::fclose(f);
        }
    if (p) {
        for (uint64_t kh : p->members)
            if (Session* t = online_member(kh); t && t != &s) chat_deliver(*t, s.id, s.key_hash, s.party, s.name, channel, text, false);
    } else {
        for (auto& [cid, up] : sessions_)
            if (up.get() != &s && up->world == s.world) chat_deliver(*up, s.id, s.key_hash, s.party, s.name, channel, text, false);
        if (linked()) {  // the other workers' players too
            Bytes b;
            Writer w(b);
            w.u64(s.world);
            w.u64(s.key_hash);
            w.u16(s.id);
            w.u32(s.party);
            w.str8(s.name.substr(0, kChatMaxName));
            w.u16(uint16_t(text.size()));
            w.bytes(text.data(), text.size());
            for (size_t i = 0; i < peers_.size(); ++i)
                if (int(i) != me_) link_send(int(i), kLChat, b);
        }
    }
    chat_deliver(s, s.id, s.key_hash, s.party, s.name, channel, text, true);  // the sender's own line, as passed on
}

void Server::chat_link(const Bytes& body) {
    Reader r(body);
    const uint64_t world = r.u64(), key = r.u64();
    const uint16_t id = r.u16();
    (void)r.u32();  // (its party: party ids are per worker, and a party stays on one worker: strangers here)
    const std::string name = r.str8(kChatMaxName);
    const size_t n = r.u16();
    if (n > kChatMaxBytes) return;
    const std::string text(r.view(n));
    if (!r.done() || !cfg_.chat_enabled || !cfg_.chat_global) return;
    for (auto& [cid, up] : sessions_)
        if (up->world == world && up->key_hash != key) chat_deliver(*up, id, key, 0, name, kChatGlobal, text, false);
}

// ---------------------------------------------------------------- the word filter

void Server::chat_filter_check(uint32_t now) {
    if (cfg_.chat_filter_file.empty()) return;
    if (chat_filter_mtime_ >= 0 && now - chat_filter_at_ < 5000) return;  // (looked at most every 5 s)
    chat_filter_at_ = now;
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(cfg_.chat_filter_file, ec);
    const int64_t mt = ec ? 0 : int64_t(t.time_since_epoch().count());
    if (mt == chat_filter_mtime_) return;
    chat_filter_mtime_ = mt;
    std::ifstream in(cfg_.chat_filter_file);
    std::stringstream ss;
    if (in) ss << in.rdbuf();
    chat_filter_.set_words(ss.str());
    log("chat: word filter " + cfg_.chat_filter_file + ": " + std::to_string(chat_filter_.size()) + " words" +
        (in ? "" : " (the file can't be read)"));
}

// ---------------------------------------------------------------- the operator's mutes

bool Server::chat_muted(uint64_t kh, std::string* reason) {
    auto it = chat_mutes_.find(code40_of(kh));
    if (it == chat_mutes_.end()) return false;
    if (it->second.until && it->second.until < unix_now()) {
        chat_mutes_.erase(it);
        return false;
    }
    if (reason) *reason = it->second.reason;
    return true;
}

void Server::load_chat_mutes() {
    chat_mutes_.clear();
    if (cfg_.chat_mutes_file.empty()) return;
    std::ifstream in(cfg_.chat_mutes_file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string code;
        ChatMute m;
        uint64_t c40 = 0;
        ls >> code >> m.until;
        std::getline(ls, m.reason);
        if (!m.reason.empty() && m.reason[0] == ' ') m.reason.erase(0, 1);
        if (parse_friend_code(code, c40)) chat_mutes_[c40] = m;
    }
}

void Server::save_chat_mutes() {
    if (cfg_.chat_mutes_file.empty() || me_ != 0) return;  // (workers keep theirs in memory: the coordinator's file)
    const int64_t now = unix_now();
    const std::string tmp = cfg_.chat_mutes_file + ".new";
    std::FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "# friend-code expiry(unix, 0 = until unmuted) reason\n");
    for (auto& [c40, m] : chat_mutes_)
        if (!m.until || m.until >= now) std::fprintf(f, "%s %lld %s\n", friend_code(c40).c_str(), (long long)m.until, m.reason.c_str());
    std::fclose(f);
    std::rename(tmp.c_str(), cfg_.chat_mutes_file.c_str());
}

void Server::chat_mute_set(uint64_t c40, bool on, int64_t until, const std::string& reason) {
    if (on) chat_mutes_[c40] = ChatMute{until, reason};
    else chat_mutes_.erase(c40);
    save_chat_mutes();
    log(std::string("chat: ") + (on ? "muted " : "unmuted ") + friend_code(c40) + (on && !reason.empty() ? ": " + reason : ""));
    // the player is told
    for (auto& [cid, sp] : sessions_)
        if (sp->online && !sp->dead && code40_of(sp->key_hash) == c40)
            notice(*sp, kNoticeInfo, 0, on ? "The server's operator muted your chat" + (reason.empty() ? std::string() : ": " + reason)
                                           : std::string("The server's operator unmuted your chat"));
}

// "chat-mute <code> [30m|12h|7d|perm] [reason]", "chat-unmute <code>", "chat-mutes"
std::string Server::chat_admin(const std::string& cmd, std::istringstream& in) {
    if (cmd == "chat-mutes") {
        std::ostringstream o;
        const int64_t now = unix_now();
        for (auto& [c40, m] : chat_mutes_)
            if (!m.until || m.until >= now)
                o << friend_code(c40) << " " << (m.until ? std::to_string(m.until) : std::string("until unmuted")) << " " << m.reason << "\n";
        return o.str().empty() ? "no chat mutes\n" : o.str();
    }
    std::string code;
    in >> code;
    uint64_t c40 = 0;
    if (!parse_friend_code(code, c40)) return "not a friend code: " + code;
    const bool on = cmd == "chat-mute";
    int64_t until = 0;
    std::string reason;
    if (on) {
        std::string rest;
        std::getline(in, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        // an optional duration first, as for a ban
        const std::string first = rest.substr(0, rest.find(' '));
        int64_t secs = -1;
        if (first == "perm" || first == "permanent") secs = 0;
        else if (!first.empty() && std::isdigit(uint8_t(first[0]))) {
            const char unit = first.back();
            const long long v = std::atoll(first.c_str());
            secs = unit == 'm' ? v * 60 : unit == 'h' ? v * 3600 : unit == 'd' ? v * 86400 : unit == 's' || std::isdigit(uint8_t(unit)) ? v : -1;
            if (v <= 0) secs = -1;
        }
        if (secs >= 0) rest = rest.size() > first.size() ? rest.substr(first.size() + 1) : "";
        until = secs > 0 ? unix_now() + secs : 0;
        reason = rest;
    } else if (!chat_mutes_.count(c40)) {
        return "no chat mute for " + friend_code(c40);
    }
    chat_mute_set(c40, on, until, reason);
    if (linked() && me_ == 0) {
        Bytes b;
        Writer w(b);
        w.u64(c40);
        w.u8(on ? 1 : 0);
        w.u64(uint64_t(until));
        w.str8(reason.substr(0, 200));
        for (size_t i = 1; i < peers_.size(); ++i) link_send(int(i), kLChatMute, b);
    }
    return std::string(on ? "chat muted " : "chat unmuted ") + friend_code(c40) +
           (on ? (until ? " until " + std::to_string(until) : std::string(" until unmuted")) : std::string());
}

}  // namespace online::server
