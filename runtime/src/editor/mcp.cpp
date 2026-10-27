// World editor MCP interface (mcp.h): HTTP parsing, the checks, JSON-RPC and the tools. No game dependencies.
#include "mcp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>  // arc4random_buf
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace editor_mcp {
namespace {
namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}
const char* reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}
std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += t[v >> 18], out += t[(v >> 12) & 63], out += t[(v >> 6) & 63], out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = uint8_t(in[i]) << 16;
        out += t[v >> 18], out += t[(v >> 12) & 63], out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out += t[v >> 18], out += t[(v >> 12) & 63], out += t[(v >> 6) & 63], out += '=';
    }
    return out;
}
Value rpc_error(const Value& id, int code, const std::string& message) {
    Value v;
    v["jsonrpc"] = "2.0";
    v["id"] = id;
    v["error"]["code"] = code;
    v["error"]["message"] = message;
    return v;
}
Value rpc_result(const Value& id, const Value& result) {
    Value v;
    v["jsonrpc"] = "2.0";
    v["id"] = id;
    v["result"] = result;
    return v;
}
Value text_content(const std::string& text) {
    Value c;
    c["type"] = "text";
    c["text"] = text;
    return c;
}
Value tool_result(const Value& content, bool error) {
    Value r;
    r["content"].type = Value::Array;
    if (content.type == Value::Array) r["content"] = content;
    else r["content"].array.push_back(content);
    r["isError"] = error;
    return r;
}
Value failed(const std::string& why) {
    Value v;
    v["ok"] = false;
    v["error"] = why;
    return v;
}
bool supported(const std::string& version) {
    for (const char* v : kProtocolVersions)
        if (version == v) return true;
    return false;
}
// a host name without its port: 127.0.0.1, localhost, ::1 (brackets removed)
bool local_name(std::string h) {
    h = lower(h);
    if (h.size() >= 2 && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
    return h == "127.0.0.1" || h == "localhost" || h == "::1";
}
const char* kInstructions =
    "This is the world editor of a running Wind Waker HD game (a mod maker's tool). The tools are the editor's own "
    "commands: they change the open mod's package (placed actors, dialogue, quests, scenes), warp Link and check the "
    "result. Start with read_state and list_actors; screenshot shows what the player sees. Changes are undo steps "
    "(undo / redo). Tools marked destructive need \"confirm\": true and the player's Allow in the game. Files can be "
    "read only inside the open package or where the player picked them; anything else asks the player.";
}  // namespace

// ---------------------------------------------------------------- HTTP
std::string Request::header(const std::string& name) const {
    const std::string n = lower(name);
    for (const auto& [k, v] : headers)
        if (lower(k) == n) return v;
    return {};
}
bool Request::has_header(const std::string& name) const {
    const std::string n = lower(name);
    for (const auto& [k, v] : headers)
        if (lower(k) == n) return true;
    return false;
}
bool Request::keep_alive() const {
    const std::string c = lower(header("connection"));
    if (version == "HTTP/1.0") return c.find("keep-alive") != std::string::npos;
    return c.find("close") == std::string::npos;
}

Parsed parse_request(const std::string& data, Request& out, size_t& used, std::string& why) {
    const size_t end = data.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (data.size() > kMaxHeaders) return why = "headers too large", Parsed::TooLarge;
        return Parsed::Incomplete;
    }
    if (end > kMaxHeaders) return why = "headers too large", Parsed::TooLarge;
    out = Request{};
    size_t line_end = data.find("\r\n");
    const std::string line = data.substr(0, line_end);
    const size_t s1 = line.find(' '), s2 = s1 == std::string::npos ? s1 : line.find(' ', s1 + 1);
    if (s1 == std::string::npos || s2 == std::string::npos || line.find(' ', s2 + 1) != std::string::npos)
        return why = "malformed request line", Parsed::Bad;
    out.method = line.substr(0, s1);
    out.target = line.substr(s1 + 1, s2 - s1 - 1);
    out.version = line.substr(s2 + 1);
    if (out.method.empty() || out.target.empty() || (out.version != "HTTP/1.1" && out.version != "HTTP/1.0"))
        return why = "malformed request line", Parsed::Bad;
    for (char c : out.method)
        if (!std::isupper((unsigned char)c)) return why = "malformed method", Parsed::Bad;
    for (unsigned char c : out.target)
        if (c <= 32 || c >= 127) return why = "malformed target", Parsed::Bad;
    size_t at = line_end + 2;
    while (at < end + 2) {
        const size_t e = data.find("\r\n", at);
        const std::string h = data.substr(at, e - at);
        at = e + 2;
        if (h.empty()) break;
        const size_t colon = h.find(':');
        if (colon == std::string::npos || colon == 0) return why = "malformed header", Parsed::Bad;
        const std::string name = h.substr(0, colon);
        for (unsigned char c : name)
            if (c <= 32 || c >= 127 || c == ':') return why = "malformed header name", Parsed::Bad;
        for (unsigned char c : h.substr(colon + 1))
            if ((c < 32 && c != '\t') || c == 127) return why = "malformed header value", Parsed::Bad;
        if (out.headers.size() >= 100) return why = "too many headers", Parsed::TooLarge;
        out.headers.push_back({name, trim(h.substr(colon + 1))});
    }
    if (out.has_header("transfer-encoding")) return why = "chunked bodies are not supported (send Content-Length)", Parsed::Bad;
    size_t length = 0;
    bool seen = false;
    for (const auto& [k, v] : out.headers) {
        if (lower(k) != "content-length") continue;
        if (v.empty() || v.size() > 12 || v.find_first_not_of("0123456789") != std::string::npos) return why = "bad Content-Length", Parsed::Bad;
        const size_t n = size_t(std::strtoull(v.c_str(), nullptr, 10));
        if (seen && n != length) return why = "conflicting Content-Length", Parsed::Bad;
        seen = true;
        length = n;
    }
    if (length > kMaxBody) return why = "request too large (1 MB at most)", Parsed::TooLarge;
    const size_t body_at = end + 4;
    if (data.size() < body_at + length) return Parsed::Incomplete;
    out.body = data.substr(body_at, length);
    used = body_at + length;
    return Parsed::Complete;
}

std::string response_text(const Response& r, bool keep_alive) {
    std::string s = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
    if (!r.body.empty()) s += "Content-Type: " + r.type + "\r\n";
    s += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    s += "Cache-Control: no-store\r\n";
    s += "X-Content-Type-Options: nosniff\r\n";
    for (const auto& [k, v] : r.headers) s += k + ": " + v + "\r\n";
    s += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    s += "\r\n";
    return s + r.body;
}
Response error_response(int status, const std::string& message) {
    Response r;
    r.status = status;
    Value v = rpc_error(Value(), -32600, message);
    r.body = mods::json::dump(v);
    return r;
}

// ---------------------------------------------------------------- checks
bool host_allowed(const std::string& host, int port) {
    const std::string h = trim(host);
    const size_t colon = h.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    if (h.find(']') != std::string::npos && colon < h.find(']')) return false;  // [::1] without a port
    const std::string name = h.substr(0, colon), p = h.substr(colon + 1);
    if (p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos || std::atoi(p.c_str()) != port) return false;
    if (name.find(':') != std::string::npos && !(name.front() == '[' && name.back() == ']')) return false;  // a bare IPv6 address
    return local_name(name);
}
bool origin_allowed(const std::string& origin) {
    const std::string o = lower(trim(origin));
    if (o.empty()) return true;
    std::string rest;
    if (o.rfind("http://", 0) == 0) rest = o.substr(7);
    else if (o.rfind("https://", 0) == 0) rest = o.substr(8);
    else return false;  // "null", file:, other schemes
    if (rest.find('/') != std::string::npos || rest.find('@') != std::string::npos) return false;
    std::string name = rest;
    const size_t bracket = rest.find(']');
    const size_t colon = rest.rfind(':');
    if (colon != std::string::npos && (bracket == std::string::npos || colon > bracket)) {
        const std::string p = rest.substr(colon + 1);
        if (p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos) return false;
        name = rest.substr(0, colon);
    }
    if (name.find(':') != std::string::npos && !(name.front() == '[' && name.back() == ']')) return false;
    return local_name(name);
}
bool token_matches(const std::string& authorization, const std::string& token) {
    if (token.size() < 32) return false;  // no token: nothing passes
    const std::string a = trim(authorization);
    if (a.size() < 7 || lower(a.substr(0, 7)) != "bearer ") return false;
    const std::string given = trim(a.substr(7));
    // constant time over the token's length: no early exit on the first difference
    unsigned char diff = given.size() == token.size() ? 0 : 1;
    for (size_t i = 0; i < token.size(); i++) diff |= (unsigned char)(token[i] ^ (i < given.size() ? given[i] : 0));
    return diff == 0;
}
std::string new_token() {
    unsigned char b[32] = {};
    bool ok = false;
#if defined(_WIN32)
    ok = BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    arc4random_buf(b, sizeof b);
    ok = true;
#else
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t got = 0;
        while (got < sizeof b) {
            const ssize_t n = read(fd, b + got, sizeof b - got);
            if (n <= 0) break;
            got += size_t(n);
        }
        close(fd);
        ok = got == sizeof b;
    }
#endif
    if (!ok) return {};
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (unsigned char c : b) s += hex[c >> 4], s += hex[c & 15];
    std::memset(b, 0, sizeof b);
    return s;
}
std::string canonical(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(fs::path(path), ec), ec);
    if (ec) p = fs::path(path).lexically_normal();
    return p.lexically_normal().string();
}
bool inside(const std::string& path, const std::string& root) {
    if (path.empty() || root.empty()) return false;
    const fs::path p(canonical(path)), r(canonical(root));
    auto pi = p.begin(), ri = r.begin();
    for (; ri != r.end(); ++ri, ++pi) {
        if (ri->empty() && std::next(ri) == r.end()) break;  // a trailing separator
        if (pi == p.end() || *pi != *ri) return false;
    }
    for (; pi != p.end(); ++pi)
        if (*pi == "..") return false;
    return true;
}

// ---------------------------------------------------------------- tools
Value input_schema(const editor::CommandInfo& c) {
    Value s;
    s["type"] = "object";
    s["properties"].type = Value::Object;
    Value required;
    required.type = Value::Array;
    for (const auto& p : c.params) {
        Value v;
        if (p.type == "point") {
            v["type"] = "object";
            for (const char* k : {"x", "y", "z"}) v["properties"][k]["type"] = "number";
            v["required"].type = Value::Array;
            for (const char* k : {"x", "y", "z"}) v["required"].array.push_back(k);
            v["additionalProperties"] = false;
        } else if (p.type == "string" || p.type == "integer" || p.type == "number" || p.type == "boolean" || p.type == "object" ||
                   p.type == "array")
            v["type"] = p.type;
        else if (p.type.size() > 5 && p.type.compare(p.type.size() - 5, 5, "|null") == 0) {
            v["type"].type = Value::Array;  // the value, or null to remove it
            v["type"].array = {Value(p.type.substr(0, p.type.size() - 5)), Value("null")};
        } else
            v.type = Value::Object;  // any
        std::string help = p.help;
        if (p.type == "point" && help.empty()) help = "a point {x, y, z} in world units";
        if (!help.empty()) v["description"] = help;
        s["properties"][p.name] = v;
        if (p.required) required.array.push_back(p.name);
    }
    if (!required.array.empty()) s["required"] = required;
    s["additionalProperties"] = false;
    return s;
}
Value tool_list() {
    Value list;
    list.type = Value::Array;
    for (const auto& c : editor::command_list()) {
        Value t;
        t["name"] = c.name;
        std::string d = c.help;
        if (c.destructive) d += " Destructive: needs \"confirm\": true, then the player's Allow in the game.";
        t["description"] = d;
        t["inputSchema"] = input_schema(c);
        t["annotations"]["destructiveHint"] = c.destructive;
        list.array.push_back(t);
    }
    Value shot;
    shot["name"] = "screenshot";
    shot["description"] = "A picture of the game as the player sees it now (without the editor's panels), at most 1280 pixels wide.";
    shot["inputSchema"]["type"] = "object";
    shot["inputSchema"]["properties"]["screen"]["type"] = "string";
    shot["inputSchema"]["properties"]["screen"]["enum"].array = {Value("tv"), Value("gamepad")};
    shot["inputSchema"]["properties"]["screen"]["enum"].type = Value::Array;
    shot["inputSchema"]["properties"]["screen"]["description"] = "tv (default) or gamepad (when the GamePad picture is shown)";
    shot["inputSchema"]["additionalProperties"] = false;
    shot["annotations"]["readOnlyHint"] = true;
    list.array.push_back(shot);
    return list;
}
const std::vector<PathParam>& path_params() {
    static const std::vector<PathParam> v = {
        {"check_model", "file", false},
        {"import_model", "file", false},
        {"replace_model", "file", false},
        {"watch_model", "file", false},
        {"import_ground", "file", false},
        {"add_item", "icon_file", false},
        {"set_item", "icon_file", false},
        {"export_package", "path", true},
    };
    return v;
}
bool valid_token(const std::string& token) {
    return token.size() == 64 && token.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string saved_or_new_token(const std::function<bool(std::string&)>& load, const std::function<void(const std::string&)>& save) {
    std::string t;
    if (load && load(t) && valid_token(t)) return t;
    t = new_token();
    if (!valid_token(t)) return {};
    if (save) save(t);
    return t;
}
LaunchMcp launch_choice(bool editor, bool mcp, bool available_here) {
    if (!mcp) return LaunchMcp::Off;
    if (!available_here) return LaunchMcp::Unavailable;
    return editor ? LaunchMcp::On : LaunchMcp::NeedsEditor;
}
std::string claude_code_command(int port, const std::string& token) {
    return "claude mcp add --transport http wwhd-editor http://127.0.0.1:" + std::to_string(port) + "/mcp --header \"Authorization: Bearer " + token + "\"";
}
std::string claude_desktop_config(int port, const std::string& token) {
    // (the token is 64 hex digits and the port a number: nothing to escape)
    return "{\n"
           "  \"mcpServers\": {\n"
           "    \"wwhd-editor\": {\n"
           "      \"command\": \"npx\",\n"
           "      \"args\": [\"-y\", \"mcp-remote\", \"http://127.0.0.1:" + std::to_string(port) + "/mcp\", \"--transport\", \"http-only\", \"--header\", \"Authorization:${WWHD_EDITOR_AUTH}\"],\n"
           "      \"env\": {\"WWHD_EDITOR_AUTH\": \"Bearer " + token + "\"}\n"
           "    }\n"
           "  }\n"
           "}\n";
}
bool SessionAllows::has(const std::string& command) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& c : commands_)
        if (c == command) return true;
    return false;
}
void SessionAllows::add(const std::string& command) {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& c : commands_)
        if (c == command) return;
    commands_.push_back(command);
}
void SessionAllows::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    commands_.clear();
}
bool needs_allow(const std::string& command, const Value& args) {
    const editor::CommandInfo* c = editor::find_command(command);
    if (!c) return false;
    if (c->destructive) return true;
    if (command == "replace_model" || (command == "watch_model" && args.get("on").boolean)) return true;  // replaces a model's file (now, or when it changes)
    return (command == "import_model" || command == "import_ground") && args.get("confirm").type == Value::Bool && args.get("confirm").boolean;  // replaces a file
}
std::string describe(const std::string& command, const Value& args) {
    std::string s = command;
    const editor::CommandInfo* c = editor::find_command(command);
    if (c) s += " (" + c->help + ")";
    for (const auto& [k, v] : args.object) {
        if (k == "confirm") continue;
        std::string text = v.type == Value::String ? v.text : mods::json::dump(v);
        if (text.size() > 300) text = text.substr(0, 300) + "...";
        s += "\n  " + k + ": " + text;
    }
    return s;
}

// ---------------------------------------------------------------- the handler
Handler::Handler(Backend& backend, std::string token, int port, std::string version, std::shared_ptr<SessionAllows> allows)
    : backend_(backend), allows_(allows ? std::move(allows) : std::make_shared<SessionAllows>()), token_(std::move(token)), version_(std::move(version)), port_(port) {}
void Handler::set_token(const std::string& token) {
    std::lock_guard<std::mutex> lk(token_mu_);
    token_ = token;
}

std::chrono::steady_clock::time_point Handler::last_seen() const {
    std::lock_guard<std::mutex> lk(mu_);
    return last_seen_;
}

Response Handler::handle(const Request& r) {
    // DNS rebinding first: a web page that resolved its own name to this address sends its own Host / Origin
    if (!host_allowed(r.header("host"), port_.load())) return error_response(403, "forbidden: Host must be 127.0.0.1, localhost or [::1] with the port");
    if (r.has_header("origin") && !origin_allowed(r.header("origin"))) return error_response(403, "forbidden: Origin is not local");
    std::string token;
    {
        std::lock_guard<std::mutex> lk(token_mu_);
        token = token_;
    }
    if (!token_matches(r.header("authorization"), token)) {
        Response x = error_response(401, "unauthorized: send \"Authorization: Bearer <token>\" (Settings -> Editor shows it)");
        x.headers.push_back({"WWW-Authenticate", "Bearer realm=\"wwhd-editor\""});
        return x;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        last_seen_ = std::chrono::steady_clock::now();
    }
    const std::string path = r.target.substr(0, r.target.find('?'));
    if (path != "/mcp") return error_response(404, "not found: the endpoint is /mcp");
    if (r.method != "POST") {
        Response x = error_response(405, "method not allowed: POST JSON-RPC messages to /mcp (no event stream)");
        x.headers.push_back({"Allow", "POST"});
        return x;
    }
    const std::string type = lower(r.header("content-type"));
    if (type.rfind("application/json", 0) != 0) return error_response(415, "unsupported media type: send application/json");
    if (r.has_header("mcp-protocol-version") && !supported(trim(r.header("mcp-protocol-version"))))
        return error_response(400, "unsupported MCP-Protocol-Version");
    Value in;
    try {
        in = mods::json::parse(r.body);
    } catch (const std::exception& e) {
        Response x;
        x.status = 400;
        x.body = mods::json::dump(rpc_error(Value(), -32700, std::string("parse error: ") + e.what()));
        return x;
    }
    Response out;
    if (in.type == Value::Array) {
        if (in.array.empty()) {
            out.status = 400;
            out.body = mods::json::dump(rpc_error(Value(), -32600, "empty batch"));
            return out;
        }
        Value replies;
        replies.type = Value::Array;
        for (const auto& m : in.array) {
            bool reply = false;
            Value v = message(m, reply);
            if (reply) replies.array.push_back(v);
        }
        if (replies.array.empty()) {
            out.status = 202;
            return out;
        }
        out.body = mods::json::dump(replies);
        return out;
    }
    bool reply = false;
    Value v = message(in, reply);
    if (!reply) {
        out.status = 202;
        return out;
    }
    out.body = mods::json::dump(v);
    return out;
}

Value Handler::message(const Value& m, bool& reply) {
    reply = false;
    const bool has_id = m.type == Value::Object && m.object.count("id");
    const Value id = has_id ? m.get("id") : Value();
    if (m.type != Value::Object || m.get("jsonrpc").string() != "2.0" ||
        (has_id && id.type != Value::String && id.type != Value::Number)) {
        reply = true;
        return rpc_error(id.type == Value::String || id.type == Value::Number ? id : Value(), -32600, "invalid request: a JSON-RPC 2.0 message");
    }
    if (m.get("method").type != Value::String) return Value();  // a response from the client: nothing to answer
    backend_.request(m.get("method").text.substr(0, 64));
    if (!has_id) return Value();                                  // a notification (initialized, cancelled...)
    reply = true;
    const std::string method = m.get("method").text;
    const Value& params = m.get("params");
    if (params.type != Value::Null && params.type != Value::Object) return rpc_error(id, -32602, "params must be an object");
    try {
        if (method == "initialize") {
            const std::string asked = params.get("protocolVersion").string();
            Value r;
            r["protocolVersion"] = supported(asked) ? asked : std::string(kProtocolVersions[0]);
            r["capabilities"]["tools"]["listChanged"] = false;
            r["serverInfo"]["name"] = "wwhd-editor";
            r["serverInfo"]["title"] = "Wind Waker HD world editor";
            r["serverInfo"]["version"] = version_;
            r["instructions"] = kInstructions;
            return rpc_result(id, r);
        }
        if (method == "ping") {
            Value r;
            r.type = Value::Object;
            return rpc_result(id, r);
        }
        if (method == "tools/list") {
            Value r;
            r["tools"] = tool_list();
            return rpc_result(id, r);
        }
        if (method == "tools/call") {
            if (params.get("name").type != Value::String) return rpc_error(id, -32602, "tools/call needs a tool name");
            const std::string name = params.get("name").text;
            if (name != "screenshot" && !editor::find_command(name)) return rpc_error(id, -32602, "unknown tool \"" + name + "\"");
            const Value& args = params.get("arguments");
            if (args.type != Value::Null && args.type != Value::Object) return rpc_error(id, -32602, "arguments must be an object");
            return rpc_result(id, call_tool(name, args));
        }
        return rpc_error(id, -32601, "method not found: " + method);
    } catch (const std::exception& e) {
        return rpc_error(id, -32603, std::string("internal error: ") + e.what());
    }
}

// a command that needs the player's Allow: asked, unless allowed for this session (by its name)
bool Handler::allowed(const std::string& name, const Value& args) {
    if (allows_->has(name)) return true;
    const Answer a = backend_.ask_choice("Claude wants to: " + describe(name, args), true);
    if (a == Answer::AllowSession) allows_->add(name);
    return a != Answer::Deny;
}

bool Handler::check_paths(const std::string& command, Value& args, std::string& why) {
    for (const auto& pp : path_params()) {
        if (command != pp.command || args.get(pp.param).type != Value::String) continue;
        const std::string given = args.get(pp.param).text;
        if (given.empty() || !fs::path(given).is_absolute()) return why = std::string("\"") + pp.param + "\" must be a full path", false;
        const std::string full = canonical(given);
        if (pp.write) {
            bool ok = false;
            const auto roots = backend_.export_roots();
            for (const auto& r : roots) ok = ok || inside(full, r);
            if (!ok) {
                why = "over MCP, packages are exported only into the editor's export folder";
                if (!roots.empty()) why += " (" + roots.front() + ")";
                why += " or a folder the player picked in the Export area";
                return false;
            }
        } else {
            bool ok = inside(full, backend_.package_dir());
            for (const auto& p : backend_.picked()) ok = ok || inside(full, p);
            // asked every time, per path: Allow / Deny only (no "Allow for this session")
            if (!ok && backend_.ask_choice("Claude wants to read a file outside the open package:\n" + full, false) == Answer::Deny)
                return why = "the player did not allow reading " + full, false;
        }
        args[pp.param] = full;  // the command uses the path that was checked
    }
    return true;
}

Value Handler::call_tool(const std::string& name, const Value& args_in) {
    std::lock_guard<std::mutex> lk(call_mu_);
    ++calls_;
    Value args = args_in;
    if (args.type == Value::Null) args.type = Value::Object;
    if (name == "screenshot") {
        std::string screen = "tv";
        for (const auto& [k, v] : args.object)
            if (k != "screen") {
                const Value r = failed("unknown parameter \"" + k + "\" for screenshot");
                backend_.note(name, args, r);
                return tool_result(text_content(r.get("error").text), true);
            }
        if (args.object.count("screen")) screen = args.get("screen").string();
        if (screen != "tv" && screen != "gamepad") {
            const Value r = failed("screen is tv or gamepad");
            backend_.note(name, args, r);
            return tool_result(text_content(r.get("error").text), true);
        }
        std::string png, why;
        uint32_t w = 0, h = 0;
        if (!backend_.screenshot(screen == "gamepad", png, w, h, why)) {
            const Value r = failed(why.empty() ? "no picture" : why);
            backend_.note(name, args, r);
            return tool_result(text_content(r.get("error").text), true);
        }
        Value logged;
        logged["ok"] = true;
        logged["result"]["screen"] = screen;
        logged["result"]["width"] = int(w);
        logged["result"]["height"] = int(h);
        logged["result"]["bytes"] = double(png.size());
        backend_.note(name, args, logged);
        Value content;
        content.type = Value::Array;
        Value img;
        img["type"] = "image";
        img["data"] = base64(png);
        img["mimeType"] = "image/png";
        content.array.push_back(img);
        content.array.push_back(text_content(screen + " picture, " + std::to_string(w) + " x " + std::to_string(h) + " pixels"));
        return tool_result(content, false);
    }
    const editor::CommandInfo* info = editor::find_command(name);
    if (!info) return tool_result(text_content("unknown tool \"" + name + "\""), true);
    const bool confirmed = args.get("confirm").type == Value::Bool && args.get("confirm").boolean;
    std::string why;
    if (info->destructive && !confirmed) {
        // the command layer refuses it (and logs the refusal) without asking anybody
    } else if (!check_paths(name, args, why) || (needs_allow(name, args) && !allowed(name, args))) {
        if (why.empty()) why = "the player chose Deny (or did not answer in time)";
        const Value r = failed(why);
        backend_.note(name, args, r);
        return tool_result(text_content(why), true);
    }
    const Value r = backend_.run(name, args);
    const bool ok = r.get("ok").type == Value::Bool && r.get("ok").boolean;
    std::string text = ok ? mods::json::dump(r.get("result")) : r.get("error").string("failed");
    return tool_result(text_content(text), !ok);
}

}  // namespace editor_mcp
