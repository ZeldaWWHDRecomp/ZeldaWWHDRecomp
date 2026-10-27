// World editor: the MCP interface for Claude and other tool clients (docs/mod-editor.md, "Using Claude or
// another MCP client").
//
// Only in the editor launch mode (--editor), off until the switch in Settings -> Editor turns it on (or the game
// was started with --editor --mcp), and left out of builds configured with -DWWHD_EDITOR_MCP=OFF. Never on
// Android.
//
//   - Transport: MCP "Streamable HTTP", JSON-RPC 2.0 over POST /mcp with plain application/json replies
//     (no event streams: every tool answers in one reply). A small HTTP/1.1 server of its own
//     (mcp_server.cpp), listening on 127.0.0.1 (and ::1 when the system has it) only.
//   - Every request needs "Authorization: Bearer <token>": 32 random bytes from the operating system's
//     generator, compared in constant time. One token is kept across restarts in the player's settings
//     (made on first use; Settings -> Editor -> New token replaces it, and the old one is refused at once),
//     so a client's configuration stays valid. It is never logged and never in a crash report.
//   - DNS rebinding: a Host other than 127.0.0.1 / localhost / [::1] with the server's port, or a present
//     Origin that is not a local address, is refused before anything else.
//   - Limits: 1 MB per request body, 16 KB of headers, a few connections at once, timeouts.
//   - Tools are the command layer's commands (editor/commands.h command_list), each with a JSON schema
//     built from its parameters, plus "screenshot". A tool call runs the command exactly as the editor's
//     own panels do (in order, on the editor's command worker; never game code on the server's thread).
//   - Destructive commands are refused without "confirm": true; with it, the game asks the player
//     ("Claude wants to: ... Allow / Allow for this session / Deny") and runs the command only on Allow.
//     "Allow for this session" lets that command (by its name) run without asking again until the game
//     closes (SessionAllows: never saved). Files outside the open mod are asked for per path every time.
//   - Files: a path a tool reads must be inside the open package or one the player picked in the editor;
//     anything else needs the player's Allow, which shows the full path. Exports go only into the editor's
//     export folder or a folder the player picked.
//   - Every call shows in the editor's Log, marked MCP, with its parameters and result.
//
// This file and mcp.cpp have no game dependencies (unit-tested: runtime/tools/editor_mcp_test.cpp); the
// game's side (the switch, the prompt, the screenshot) is mcp_game.cpp.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "commands.h"

namespace editor_mcp {
using editor::Value;

// ---------------------------------------------------------------- HTTP
inline constexpr size_t kMaxBody = 1 << 20;     // bytes per request body
inline constexpr size_t kMaxHeaders = 16 << 10;  // request line and headers
inline constexpr int kDefaultPort = 47615;      // then the next nine, then any free port
inline constexpr int kMaxConnections = 4;

struct Request {
    std::string method, target, version, body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string header(const std::string& name) const;  // case-insensitive; "" when absent
    bool has_header(const std::string& name) const;
    bool keep_alive() const;
};
enum class Parsed { Incomplete, Complete, Bad, TooLarge };
// One request from the front of `data`; `used` is its length when Complete. Content-Length bodies only.
Parsed parse_request(const std::string& data, Request& out, size_t& used, std::string& why);

struct Response {
    int status = 200;
    std::string type = "application/json", body;
    std::vector<std::pair<std::string, std::string>> headers;
};
std::string response_text(const Response& r, bool keep_alive);
Response error_response(int status, const std::string& message);

// ---------------------------------------------------------------- checks
bool host_allowed(const std::string& host, int port);  // 127.0.0.1:<port>, localhost:<port>, [::1]:<port>
bool origin_allowed(const std::string& origin);        // absent, or http(s) on 127.0.0.1 / localhost / [::1]
bool token_matches(const std::string& authorization, const std::string& token);  // "Bearer <token>", constant time
std::string new_token();  // 64 hex digits (256 bits) from the OS generator; "" if it failed
bool valid_token(const std::string& token);  // 64 lower-case hex digits, as new_token makes them
// The token kept across restarts: the saved one when it is valid, else a new one, saved through `save`
// ("" when none could be made). `load` and `save` reach the player's settings (mcp_game.cpp).
std::string saved_or_new_token(const std::function<bool(std::string&)>& load, const std::function<void(const std::string&)>& save);

// ---------------------------------------------------------------- start-up
// The start options: --editor --mcp turns the interface on at start; --editor alone leaves it off; --mcp alone
// only says it needs --editor; never on Android or in builds without the interface.
enum class LaunchMcp { Off, On, NeedsEditor, Unavailable };
LaunchMcp launch_choice(bool editor, bool mcp, bool available_here);

// ---------------------------------------------------------------- client configuration
// Claude Code: the command that adds the server (claude mcp add --transport http ... --header ...)
std::string claude_code_command(int port, const std::string& token);
// Claude Desktop (claude_desktop_config.json, "mcpServers"): the desktop app starts local servers as programs,
// so the mcp-remote bridge (npx, Node.js 18 or newer) connects it to this Streamable HTTP address. The header
// is "Authorization:${WWHD_EDITOR_AUTH}" with the value in "env": Claude Desktop on Windows does not quote
// arguments with spaces, and the token stays out of the process list.
std::string claude_desktop_config(int port, const std::string& token);
// `path` is `root` or inside it, after resolving "..", "." and symbolic links (component-wise)
bool inside(const std::string& path, const std::string& root);
std::string canonical(const std::string& path);  // as inside() compares it ("" for an empty path)

// ---------------------------------------------------------------- tools
inline constexpr const char* kProtocolVersions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};  // newest first
Value input_schema(const editor::CommandInfo& c);
Value tool_list();  // every command of command_list() and screenshot
// Parameters that name files: `write` ones are a destination folder.
struct PathParam {
    const char* command;
    const char* param;
    bool write;
};
const std::vector<PathParam>& path_params();
// Runs only after the player's Allow: destructive commands (which need "confirm": true anyway) and an
// import that replaces a file.
enum class Answer { Deny, Allow, AllowSession };
// The commands the player allowed for this session ("Allow for this session"): by command name, until the game
// closes; never saved. Shared by the handlers of one game run (a new token keeps them).
class SessionAllows {
public:
    bool has(const std::string& command) const;
    void add(const std::string& command);
    void clear();

private:
    mutable std::mutex mu_;
    std::vector<std::string> commands_;
};
bool needs_allow(const std::string& command, const Value& args);
std::string describe(const std::string& command, const Value& args);  // for the Allow prompt and the log

// ---------------------------------------------------------------- what the server needs from the game
class Backend {
public:
    virtual ~Backend() = default;
    // runs a command as the editor's panels do; {"ok", "result"} or {"ok": false, "error"}; logged as MCP
    virtual Value run(const std::string& command, const Value& params) = 0;
    // the in-game prompt: true only when the player chose Allow (false on Deny or no answer in time)
    virtual bool ask(const std::string& text) = 0;
    // the same with "Allow for this session" offered when `session` (a command, not a file outside the mod)
    virtual Answer ask_choice(const std::string& text, bool session) {
        (void)session;
        return ask(text) ? Answer::Allow : Answer::Deny;
    }
    // the next frame's TV (or GamePad) picture as PNG, at most 1280 pixels wide
    virtual bool screenshot(bool gamepad, std::string& png, uint32_t& width, uint32_t& height, std::string& why) = 0;
    virtual std::string package_dir() = 0;                // the open mod's package folder ("" none)
    virtual std::vector<std::string> picked() = 0;        // files and folders the player picked in the editor
    virtual std::vector<std::string> export_roots() = 0;  // where export_package may write
    // a log entry that is not a command run (a screenshot, a refusal before running)
    virtual void note(const std::string& tool, const Value& args, const Value& result) = 0;
    // a JSON-RPC request or notification arrived (its method only: for the game's log)
    virtual void request(const std::string& method) {}
};

class Handler {
public:
    // `allows`: the session's "Allow for this session" answers (a handler of its own when none is given)
    Handler(Backend& backend, std::string token, int port, std::string version = "1", std::shared_ptr<SessionAllows> allows = nullptr);
    // the whole HTTP exchange: checks, then the JSON-RPC message(s)
    Response handle(const Request& r);
    // one JSON-RPC message: `reply` false for notifications and responses
    Value message(const Value& m, bool& reply);
    // tools/call's result: {"content": [...], "isError": bool}
    Value call_tool(const std::string& name, const Value& args);
    void set_port(int port) { port_ = port; }
    // a new token: from now on only it passes (New token in Settings -> Editor)
    void set_token(const std::string& token);
    // the last time a request passed the token check (for "MCP: connected")
    std::chrono::steady_clock::time_point last_seen() const;
    uint64_t calls() const { return calls_.load(); }

private:
    Backend& backend_;
    std::shared_ptr<SessionAllows> allows_;
    mutable std::mutex token_mu_;  // token_
    std::string token_, version_;
    std::atomic<int> port_;
    std::atomic<uint64_t> calls_{0};
    mutable std::mutex mu_;  // last_seen_
    std::chrono::steady_clock::time_point last_seen_{};
    std::mutex call_mu_;  // one tool call at a time, as the editor's panels run them
    bool check_paths(const std::string& command, Value& args, std::string& why);
    bool allowed(const std::string& name, const Value& args);
};

}  // namespace editor_mcp
