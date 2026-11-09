// SPDX-License-Identifier: MPL-2.0
// Text chat through a server (docs/online-server.md, "Chat"): the server's chat rules, the messages, and the checks
// the server and the game both apply to a line (UTF-8 cleaning, length, the operator's word filter). Two channels:
// the party (the sender's party members) and global (everyone on the server, only where the operator allows it).
// The server checks every line and passes it on; it keeps nothing unless its operator turns the chat log on.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wire.h"

namespace online::proto {

enum ChatChannel : uint8_t { kChatParty = 0, kChatGlobal = 1, kChatChannels = 2 };

// a server's max_length can't be more than this (code points); and a line is never more than this many bytes on the wire
constexpr int kChatMaxLength = 200;
constexpr size_t kChatMaxBytes = 4 * kChatMaxLength;
constexpr size_t kChatMaxName = 24;

// CHAT_RULES (S->C, reliable, after WELCOME): u8 flags (bit 0 chat on, bit 1 global on), u16 max_length,
// u8 rate_count, u16 rate_window_s. A server that never sends it has no chat: the game shows none.
struct ChatRules {
    bool enabled = false, global = false;
    uint16_t max_length = 120;   // code points
    uint8_t rate_count = 5;      // lines ...
    uint16_t rate_window_s = 10; // ... per this many seconds
    bool operator==(const ChatRules&) const = default;
};
void put_chat_rules(Writer& w, const ChatRules& r);
bool get_chat_rules(Reader& r, ChatRules& out);

// CHAT (C->S, reliable): u8 channel, u16 n, n bytes of UTF-8
struct ChatSay {
    uint8_t channel = kChatParty;
    std::string text;
};
Bytes encode_chat_say(const ChatSay& s);
bool decode_chat_say(const uint8_t* d, size_t n, ChatSay& out);

// CHAT_FROM (S->C, reliable): u16 sender id, u64 sender key hash, u8 channel, u8 flags, str8 name (<= 24), u16 n, text.
// The name is the one this receiver sees elsewhere: the display name for party members, "Sailor 7Q2F" for strangers.
// The sender gets its own line back (kChatYours): the line as the others got it (filtered), and the sign it was sent.
enum ChatLineFlags : uint8_t { kChatFromParty = 1, kChatYours = 2 };
struct ChatLine {
    uint16_t from = 0;
    uint64_t key_hash = 0;
    uint8_t channel = kChatParty, flags = 0;
    std::string name, text;
};
Bytes encode_chat_line(const ChatLine& l);
bool decode_chat_line(const uint8_t* d, size_t n, ChatLine& out);

// CHAT_RESULT (S->C, reliable): u8 code, u8 channel, str8 text: a line of yours the server did not pass on, and why
enum ChatResult : uint8_t {
    kChatSent = 0,
    kChatOff = 1,        // the server has no chat
    kChatNoGlobal = 2,   // the server has no global channel
    kChatTooLong = 3,    // over max_length
    kChatTooFast = 4,    // over the rate limit
    kChatMuted = 5,      // the operator muted you
    kChatEmpty = 6,      // nothing left after cleaning
    kChatNoParty = 7,    // party channel without a party
    kChatBadChannel = 8,
};
const char* chat_result_text(uint8_t code);
Bytes encode_chat_result(uint8_t code, uint8_t channel, std::string_view text);
bool decode_chat_result(const uint8_t* d, size_t n, uint8_t& code, uint8_t& channel, std::string& text);

// The cleaned form of a typed line: valid UTF-8 only (bad bytes dropped), no control characters (C0, DEL, C1), no
// invisible format characters (zero-width ones, bidirectional overrides, the byte order mark), no line or paragraph
// separators, no private-use characters (the game's button pictures) and no noncharacters; tabs and line breaks become
// spaces, runs of spaces one, and it is trimmed. Returns the length in code points.
size_t chat_clean(std::string_view in, std::string& out);
size_t utf8_length(std::string_view s);  // code points of valid UTF-8 (a bad byte counts as one)

// The operator's word filter: one word or phrase per line ('#' starts a comment line); a trailing '*' also matches
// longer words ("dumb*": "dumbest"). Matching ignores ASCII case and only takes whole words (letters, digits and any
// non-ASCII character make up words). Each match becomes "***".
class ChatFilter {
public:
    void set_words(const std::string& text);
    size_t size() const { return words_.size(); }
    size_t apply(std::string& text) const;  // returns how many matches were replaced

private:
    struct Word {
        std::string w;  // lower case (ASCII)
        bool prefix = false;
    };
    std::vector<Word> words_;
};

}  // namespace online::proto
