// SPDX-License-Identifier: MPL-2.0
// Online co-op's saved servers and the player's words for the server's answers (mods/coop_servers.cpp, GAPS C8):
//   - the settings' form round-trips, broken entries are dropped, separators cannot sneak in through a name;
//   - remember keeps the newest first, at most kMax, the same server once, its pinned key when none is given;
//   - forget removes the server and its key; a key must be 64 hex digits;
//   - every refusal, notice, limit and party answer has its own plain sentence (none empty, none the bare fallback).
#include "mods/coop_servers.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "online/proto/protocol.h"

namespace sv = mods::coop::servers;
namespace op = online::proto;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c), g_fail++; \
    } while (0)

int main() {
    const std::string key(64, 'a'), key2 = std::string(63, '0') + "1";
    // round trip
    std::vector<sv::Server> l;
    sv::remember(l, {"Duo", "127.0.0.1", 47777, key});
    sv::remember(l, {"Friends|;evil", "play.example.org", 7777, ""});
    CHECK(l.size() == 2 && l[0].addr == "play.example.org" && l[0].name == "Friendsevil");
    const std::string text = sv::format(l);
    const std::vector<sv::Server> back = sv::parse(text);
    CHECK(back.size() == 2 && back[1].key == key && back[1].port == 47777 && back[0].key.empty());
    // broken entries
    CHECK(sv::parse("").empty());
    CHECK(sv::parse("x|a|0|;y||7777|;z|b|99999|").empty());
    const auto bad_key = sv::parse("n|host|7777|nothex");
    CHECK(bad_key.size() == 1 && bad_key[0].key.empty() && bad_key[0].name == "n");
    CHECK(sv::parse("|host|7777|")[0].name == "host");
    CHECK(sv::parse("a|h|1|;b|h|1|").size() == 1);  // (the same server once)
    // the pinned key kept when the server is seen again without one, replaced by a new one
    sv::remember(l, {"Duo again", "127.0.0.1", 47777, ""});
    CHECK(l[0].name == "Duo again" && l[0].key == key && l.size() == 2);
    sv::remember(l, {"Duo", "127.0.0.1", 47777, key2});
    CHECK(l[0].key == key2);
    // at most kMax, the oldest dropped
    for (int i = 0; i < 20; i++) sv::remember(l, {"s", "host" + std::to_string(i), 7777, ""});
    CHECK(l.size() == sv::kMax && l[0].addr == "host19" && !sv::find(l, "127.0.0.1", 47777));
    // forget
    CHECK(sv::forget(l, "host19", 7777) && !sv::find(l, "host19", 7777) && l.size() == sv::kMax - 1);
    CHECK(!sv::forget(l, "host19", 7777));
    sv::remember(l, {"", "", 7777, ""});
    sv::remember(l, {"", "h", 0, ""});
    CHECK(l.size() == sv::kMax - 1);
    // hex
    unsigned char b[32], c[32];
    for (int i = 0; i < 32; i++) b[i] = (unsigned char)(i * 37);
    const std::string hx = sv::to_hex(b, 32);
    CHECK(hx.size() == 64 && sv::from_hex(hx, c, 32) && memcmp(b, c, 32) == 0);
    CHECK(!sv::from_hex(hx.substr(1), c, 32) && !sv::from_hex(std::string(64, 'g'), c, 32));
    // words: each refusal its own
    std::set<std::string> seen;
    const std::string fallback = sv::refusal(200, "", 0);
    for (int r = (int)op::Refusal::kProtocol; r <= (int)op::Refusal::kKicked; r++) {
        const std::string t = sv::refusal(r, "", 0);
        CHECK(!t.empty() && t != fallback && seen.insert(t).second);
    }
    CHECK(sv::refusal((int)op::Refusal::kFull, "", 4).find("4 s") != std::string::npos);
    CHECK(sv::refusal((int)op::Refusal::kBanned, "cheating", 0).find("cheating") != std::string::npos);
    CHECK(sv::refusal((int)op::Refusal::kFull, "the server is full", 0).find("says") == std::string::npos);  // (no echo)
    // notices: removals, warnings per limit, restarts
    seen.clear();
    for (int k : {op::kNoticeKick, op::kNoticeBan, op::kNoticeInfo}) CHECK(seen.insert(sv::notice(k, 0, 0, "x")).second);
    for (int reason = op::kLimitUpload; reason <= op::kLimitOther; reason++) {
        if (reason == op::kLimitApp) continue;  // (the same words as reliable messages)
        CHECK(seen.insert(sv::notice(op::kNoticeKickLimit, reason, 0, "")).second);
        CHECK(seen.insert(sv::notice(op::kNoticeThrottle, reason, 0, "")).second);
    }
    CHECK(sv::notice(op::kNoticeRestart, 0, 30, "Server restarting").find("30 s") != std::string::npos);
    CHECK(sv::notice(op::kNoticeKick, 0, 0, "signed in from another place").find("another game") != std::string::npos);
    // party answers 1..8 each their own; 0 by the server's text
    seen.clear();
    for (int code = 1; code <= 8; code++) CHECK(seen.insert(sv::party(code, code == 8 ? "Bob declined" : "")).second);
    CHECK(sv::party(8, "Bob declined") == "Bob declined.");
    CHECK(sv::party(0, "invite sent").find("Invite sent") == 0 && sv::party(0, "joined the party") == "You joined the party.");
    CHECK(sv::key_changed("a:1").find("a:1") != std::string::npos && !sv::bad_code().empty());
    // and the friend codes the page shows parse back, a typo is caught
    const std::string code = op::friend_code(0x123456789ABCull);
    uint64_t c40 = 0;
    CHECK(op::parse_friend_code(code, c40) && c40 == op::code40_of(0x123456789ABCull));
    std::string typo = code;
    typo[0] = typo[0] == 'A' ? 'B' : 'A';
    CHECK(!op::parse_friend_code(typo, c40));
    if (g_fail) return fprintf(stderr, "%d failed\n", g_fail), 1;
    printf("coop_servers: ok\n");
    return 0;
}
