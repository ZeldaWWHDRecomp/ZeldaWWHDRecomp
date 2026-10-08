#pragma once
#include <cstddef>
#include <cstring>
#include <string_view>
namespace crash_context {
struct Redactor {
    char home[1024] = {}, user[256] = {};
    static bool separator(char c) { return c == '/' || c == '\\'; }
    static char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
    static bool prefix(std::string_view text, std::string_view value, bool fold, bool slashes) {
        if (text.size() < value.size()) return false;
        for (size_t i = 0; i < value.size(); ++i) {
            if (slashes && separator(text[i]) && separator(value[i])) continue;
            if ((fold ? lower(text[i]) : text[i]) != (fold ? lower(value[i]) : value[i])) return false;
        }
        return true;
    }
    void initialize(const char* h, const char* u) {
        home[0] = user[0] = 0;
        if (h && std::strlen(h) > 1 && std::strlen(h) < sizeof home) std::memcpy(home, h, std::strlen(h) + 1);
        if (u && *u && std::strlen(u) < sizeof user) std::memcpy(user, u, std::strlen(u) + 1);
    }
    void write(int fd, std::string_view text, void (*out)(int, const char*, size_t)) const {
        char buffer[1024]; size_t n = 0;
        const std::string_view own_home(home), own_user(user);
        const bool windows = own_home.size() >= 3 && own_home[1] == ':' && separator(own_home[2]);
        auto flush = [&] { if (n) out(fd, buffer, n); n = 0; };
        for (size_t i = 0; i < text.size();) {
            std::string_view replacement, rest = text.substr(i);
            size_t skip = 0;
            if (!own_home.empty() && prefix(rest, own_home, windows, windows)) {
                // A longer account name (alice2 versus alice) must use the generic home rule.
                const size_t end = own_home.size();
                if (end == rest.size() || separator(rest[end]) || rest[end] == ' ' || rest[end] == '\n' ||
                    rest[end] == '\r' || rest[end] == '\t' || rest[end] == '"' || rest[end] == '\'' ||
                    rest[end] == ')' || rest[end] == ']' || rest[end] == ',' || rest[end] == ';') {
                    skip = end; replacement = "~";
                }
            }
            if (!skip) {
                size_t begin = 0;
                if (prefix(rest, "/Users/", true, false)) begin = 7;
                else if (prefix(rest, "/home/", true, false)) begin = 6;
                else if (rest.size() >= 9 && lower(rest[0]) >= 'a' && lower(rest[0]) <= 'z' && rest[1] == ':' &&
                         prefix(rest.substr(2), "/Users/", true, true)) begin = 9;
                if (begin) {
                    skip = begin;
                    // Spaces belong to account names too; prefer over-redaction for an unquoted root path.
                    while (skip < rest.size() && !separator(rest[skip]) && rest[skip] != '\n' &&
                           rest[skip] != '\r' && rest[skip] != '\t' && rest[skip] != '"' && rest[skip] != '\'') ++skip;
                    replacement = "~";
                }
            }
            if (!skip && !own_user.empty() && prefix(rest, own_user, windows, false)) {
                skip = own_user.size(); replacement = "<user>";
            }
            if (skip) {
                for (char c : replacement) { if (n == sizeof buffer) flush(); buffer[n++] = c; }
                i += skip;
            } else { if (n == sizeof buffer) flush(); buffer[n++] = text[i++]; }
        }
        flush();
    }
};
}
