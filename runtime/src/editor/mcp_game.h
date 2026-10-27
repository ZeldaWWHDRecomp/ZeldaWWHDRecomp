// World editor MCP interface: the game's side (mcp.h has the design). The switch in Settings -> Editor,
// the token, the in-game Allow prompt, screenshots and the command runs.
#pragma once
#include <cstdint>
#include <string>

namespace editor_mcp {

// Built in (WWHD_EDITOR_MCP), not Android, and the game runs in the editor launch mode.
bool available();
// The switch (off at every start, unless started with --editor --mcp). Turning it on starts listening with the
// token kept in the player's settings (made and saved on first use).
bool set_on(bool on, std::string& why);
// Start-up (main thread, after the arguments): --mcp turns the switch on with --editor; without --editor it only
// logs that it needs it (and where nothing can listen, that there is none).
void launch(bool mcp);
// New token (Settings -> Editor): a new one is saved and from now on only it passes; the old one is refused at once.
bool renew_token(std::string& why);

struct Status {
    bool on = false;
    int port = 0;
    bool connected = false;  // a client passed the token check in the last 30 seconds
    uint64_t calls = 0;
    std::string token;       // only for Settings -> Editor (shown and copied there; never logged)
    std::string error;       // why it could not start
};
Status status();
std::string status_line();  // "MCP: off" / "MCP: on, port N" / "MCP: connected" (the status bar)
// the ready-to-paste command for Claude Code
std::string client_command(const Status& s);
// the ready-to-paste claude_desktop_config.json for Claude Desktop (mcp.h claude_desktop_config)
std::string desktop_config(const Status& s);
std::string export_folder();  // where export_package may write over MCP (besides folders the player picked)

// The Allow prompt (render thread): the question waiting for the player, if any; `session`: "Allow for this
// session" is offered (a command; not a file outside the mod, which is asked for per path)
bool prompt(uint64_t& id, std::string& text, bool* session = nullptr);
void answer(uint64_t id, bool allow, bool session = false);
bool prompt_waiting();

// A file or folder the player picked in the editor (import: the file, export: the folder): tool calls may
// read it (and export into a picked folder) without asking again.
void user_picked(const std::string& path);

// Start-up (main thread, editor launch mode): the test-only switch. WWHD_TEST_EDITOR_MCP=<port> together
// with WWHD_TEST_EDITOR_MCP_TOKEN=<64 hex digits> turns the interface on with that token at start, for
// scripted end-to-end runs (tools/gametest/editor_mcp_smoke.py); WWHD_TEST_EDITOR_MCP_ANSWER=allow|deny
// then answers the Allow prompts. Never set these outside test runs.
void start_for_tests();

}  // namespace editor_mcp
