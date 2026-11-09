// SPDX-License-Identifier: MPL-2.0
#include "chat.h"

#include <algorithm>
#include <sstream>

namespace online::proto {

void put_chat_rules(Writer& w, const ChatRules& r) {
    w.u8(uint8_t((r.enabled ? 1 : 0) | (r.global ? 2 : 0)));
    w.u16(r.max_length);
    w.u8(r.rate_count);
    w.u16(r.rate_window_s);
}
bool get_chat_rules(Reader& r, ChatRules& out) {
    const uint8_t f = r.u8();
    out.enabled = f & 1;
    out.global = (f & 2) != 0;
    out.max_length = r.u16();
    out.rate_count = r.u8();
    out.rate_window_s = r.u16();
    if (out.max_length > kChatMaxLength) out.max_length = kChatMaxLength;
    return r.ok();
}

Bytes encode_chat_say(const ChatSay& s) {
    Bytes b;
    Writer w(b);
    const size_t n = std::min(s.text.size(), kChatMaxBytes);
    w.u8(s.channel);
    w.u16(uint16_t(n));
    w.bytes(s.text.data(), n);
    return b;
}
bool decode_chat_say(const uint8_t* d, size_t n, ChatSay& out) {
    Reader r(d, n);
    out.channel = r.u8();
    const size_t len = r.u16();
    if (len > kChatMaxBytes) return false;
    out.text = std::string(r.view(len));
    return r.done();
}

Bytes encode_chat_line(const ChatLine& l) {
    Bytes b;
    Writer w(b);
    w.u16(l.from);
    w.u64(l.key_hash);
    w.u8(l.channel);
    w.u8(l.flags);
    w.str8(l.name.size() > kChatMaxName ? std::string_view(l.name).substr(0, kChatMaxName) : std::string_view(l.name));
    const size_t n = std::min(l.text.size(), kChatMaxBytes);
    w.u16(uint16_t(n));
    w.bytes(l.text.data(), n);
    return b;
}
bool decode_chat_line(const uint8_t* d, size_t n, ChatLine& out) {
    Reader r(d, n);
    out.from = r.u16();
    out.key_hash = r.u64();
    out.channel = r.u8();
    out.flags = r.u8();
    out.name = r.str8(kChatMaxName);
    const size_t len = r.u16();
    if (len > kChatMaxBytes) return false;
    out.text = std::string(r.view(len));
    return r.done();
}

const char* chat_result_text(uint8_t code) {
    switch (code) {
        case kChatSent: return "sent";
        case kChatOff: return "this server has no chat";
        case kChatNoGlobal: return "this server has no global chat";
        case kChatTooLong: return "too long";
        case kChatTooFast: return "too many messages: wait a moment";
        case kChatMuted: return "the server's operator muted your chat";
        case kChatEmpty: return "nothing to send";
        case kChatNoParty: return "you are not in a party";
        case kChatBadChannel: return "no such channel";
        default: return "not sent";
    }
}
Bytes encode_chat_result(uint8_t code, uint8_t channel, std::string_view text) {
    Bytes b;
    Writer w(b);
    w.u8(code);
    w.u8(channel);
    w.str8(text.substr(0, 200));
    return b;
}
bool decode_chat_result(const uint8_t* d, size_t n, uint8_t& code, uint8_t& channel, std::string& text) {
    Reader r(d, n);
    code = r.u8();
    channel = r.u8();
    text = r.str8(200);
    return r.done();
}

namespace {
// the next code point of `s` at `i`; false (and one byte skipped) for a bad sequence (overlong, surrogate, too large,
// cut short)
bool next_cp(std::string_view s, size_t& i, uint32_t& cp) {
    const uint8_t c = uint8_t(s[i]);
    if (c < 0x80) {
        cp = c, ++i;
        return true;
    }
    int more = 0;
    uint32_t min = 0;
    if ((c & 0xE0) == 0xC0) more = 1, cp = c & 0x1F, min = 0x80;
    else if ((c & 0xF0) == 0xE0) more = 2, cp = c & 0x0F, min = 0x800;
    else if ((c & 0xF8) == 0xF0) more = 3, cp = c & 0x07, min = 0x10000;
    else {
        ++i;
        return false;
    }
    if (i + size_t(more) >= s.size()) {  // cut short
        ++i;
        return false;
    }
    for (int k = 1; k <= more; ++k) {
        const uint8_t t = uint8_t(s[i + k]);
        if ((t & 0xC0) != 0x80) {
            ++i;
            return false;
        }
        cp = (cp << 6) | (t & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return false;
    }
    i += size_t(more) + 1;
    return true;
}
void put_cp(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) o += char(0xC0 | (cp >> 6)), o += char(0x80 | (cp & 0x3F));
    else if (cp < 0x10000) o += char(0xE0 | (cp >> 12)), o += char(0x80 | ((cp >> 6) & 0x3F)), o += char(0x80 | (cp & 0x3F));
    else o += char(0xF0 | (cp >> 18)), o += char(0x80 | ((cp >> 12) & 0x3F)), o += char(0x80 | ((cp >> 6) & 0x3F)), o += char(0x80 | (cp & 0x3F));
}
// characters a chat line never keeps
bool dropped(uint32_t c) {
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) return true;  // control characters
    if (c == 0xAD || c == 0x061C || c == 0x180E) return true;            // soft hyphen, Arabic letter mark, Mongolian vowel separator
    if (c >= 0x200B && c <= 0x200F) return true;                         // zero-width characters, LRM / RLM
    if (c >= 0x2028 && c <= 0x202E) return true;                         // line / paragraph separators, bidi embeddings and overrides
    if (c >= 0x2060 && c <= 0x206F) return true;                         // word joiner, invisible operators, bidi isolates
    if (c == 0xFEFF || (c >= 0xFFF9 && c <= 0xFFFB)) return true;        // byte order mark, interlinear annotation
    if (c >= 0xE000 && c <= 0xF8FF) return true;                         // private use (the game's button pictures)
    if (c >= 0xF0000) return true;                                       // supplementary private use
    if (c >= 0xE0000 && c <= 0xE007F) return true;                       // tags
    if ((c >= 0xFDD0 && c <= 0xFDEF) || (c & 0xFFFE) == 0xFFFE) return true;  // noncharacters
    return false;
}
bool spacing(uint32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A); }
}  // namespace

size_t chat_clean(std::string_view in, std::string& out) {
    out.clear();
    size_t n = 0;
    bool space = false;
    for (size_t i = 0; i < in.size();) {
        uint32_t cp = 0;
        if (!next_cp(in, i, cp)) continue;
        if (spacing(cp)) {
            space = n > 0;
            continue;
        }
        if (dropped(cp)) continue;
        if (space) out += ' ', ++n, space = false;
        put_cp(out, cp);
        ++n;
    }
    return n;
}

size_t utf8_length(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++n) {
        uint32_t cp;
        next_cp(s, i, cp);
    }
    return n;
}

namespace {
char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
bool word_byte(char c) {
    const uint8_t u = uint8_t(c);
    return u >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
}  // namespace

void ChatFilter::set_words(const std::string& text) {
    words_.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        line.erase(0, a);
        if (line.empty() || line[0] == '#') continue;
        Word w;
        if (line.back() == '*') w.prefix = true, line.pop_back();
        if (line.empty()) continue;
        for (char& c : line) c = lower(c);
        w.w = line;
        words_.push_back(std::move(w));
    }
    // the longest first: "bad word" before "bad"
    std::stable_sort(words_.begin(), words_.end(), [](const Word& x, const Word& y) { return x.w.size() > y.w.size(); });
}

size_t ChatFilter::apply(std::string& text) const {
    if (words_.empty()) return 0;
    size_t hits = 0;
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const bool start = i == 0 || !word_byte(text[i - 1]);
        size_t end = 0;
        if (start && word_byte(text[i])) {
            for (const Word& w : words_) {
                if (i + w.w.size() > text.size()) continue;
                bool same = true;
                for (size_t k = 0; k < w.w.size() && same; ++k) same = lower(text[i + k]) == w.w[k];
                if (!same) continue;
                size_t e = i + w.w.size();
                if (w.prefix)
                    while (e < text.size() && word_byte(text[e])) ++e;
                else if (e < text.size() && word_byte(text[e]))
                    continue;
                end = e;
                break;
            }
        }
        if (end) {
            out += "***";
            ++hits;
            i = end;
        } else {
            out += text[i++];
        }
    }
    if (hits) text = std::move(out);
    return hits;
}

}  // namespace online::proto
