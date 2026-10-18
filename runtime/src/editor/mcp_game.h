// World editor MCP interface: the game's side (mcp.h has the design). The switch in Settings -> Editor,
// the token, the in-game Allow prompt, screenshots and the command runs.
#pragma once
#include <cstdint>
#include <string>

namespace editor_mcp {

// Built in (WWHD_EDITOR_MCP), not Android, and the game runs in the editor launch mode.
bool available();
// The switch (off at every start). Turning it on makes a new token and starts listening.
bool set_on(bool on, std::string& why);

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
std::string export_folder();  // where export_package may write over MCP (besides folders the player picked)

// The Allow prompt (render thread): the question waiting for the player, if any.
bool prompt(uint64_t& id, std::string& text);
void answer(uint64_t id, bool allow);
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
