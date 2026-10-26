// World editor MCP interface: the game's side (mcp_game.h).
#include "mcp_game.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "../runtime.h"
#include "../savestate.h"

#ifndef WWHD_EDITOR_MCP
#define WWHD_EDITOR_MCP 1
#endif
#if WWHD_EDITOR_MCP && !defined(__ANDROID__)
#define WWHD_MCP_LISTENS 1
#include "../build_info.h"
#include "../overlay/hostui.h"
#include "../screenshot.h"
#include "editor_game.h"
#include "mcp.h"
#include "mcp_server.h"
#else
#define WWHD_MCP_LISTENS 0
#endif

namespace editor_mcp {
std::string export_folder() {
    return (std::filesystem::path(ss::states_dir()).parent_path() / "editor-exports").string();
}
#if WWHD_MCP_LISTENS
namespace {
namespace fs = std::filesystem;
constexpr auto kCommandWait = std::chrono::seconds(120);  // a warp or a model preparation may take a while
constexpr auto kPromptWait = std::chrono::seconds(120);   // the player's answer
constexpr auto kPictureWait = std::chrono::seconds(5);

std::mutex g_mu;  // everything below
std::string g_token;  // while listening
std::shared_ptr<SessionAllows> g_allows = std::make_shared<SessionAllows>();  // until the game closes, never saved
// the token kept in the player's settings (made on first use; never logged)
constexpr const char* kTokenSetting = "editorMcpToken";
void save_token(const std::string& t) {
    hostui::post([t] { hostui::set(kTokenSetting, t); });  // settings change on the main thread
}
std::string g_kept;  // the kept token once read or made (the save reaches the settings file a moment later)
std::string kept_token() {
    if (!valid_token(g_kept)) g_kept = saved_or_new_token([](std::string& t) { return hostui::get(kTokenSetting, t); }, save_token);
    return g_kept;
}
std::string g_error;
std::vector<std::string> g_picked;
int g_test_answer = 0;  // test runs: 1 allow, -1 deny

// the Allow prompt: one at a time (tool calls run one after another)
struct Prompt {
    uint64_t id = 0;
    std::string text;
    bool session = false;  // "Allow for this session" is offered
    int answer = 0;        // 1 allow, 2 allow for this session, -1 deny
};
std::condition_variable g_prompt_cv;
std::deque<std::shared_ptr<Prompt>> g_prompts;
uint64_t g_prompt_ids = 0;

class GameBackend final : public Backend {
public:
    Value run(const std::string& command, const Value& params) override {
        struct Wait {
            std::mutex mu;
            std::condition_variable cv;
            bool done = false;
            Value result;
        };
        auto w = std::make_shared<Wait>();
        // the same worker and order as the editor's panels: the command waits for the game there, never here
        editor::run_async(
            command, params,
            [w](const Value& r) {
                std::lock_guard<std::mutex> lk(w->mu);
                w->result = r;
                w->done = true;
                w->cv.notify_all();
            },
            "MCP");
        std::unique_lock<std::mutex> lk(w->mu);
        if (!w->cv.wait_for(lk, kCommandWait, [&] { return w->done; })) {
            Value v;
            v["ok"] = false;
            v["error"] = "the game did not finish the command in time (it may still run: read_state shows the outcome)";
            return v;
        }
        return w->result;
    }
    bool ask(const std::string& text) override { return ask_choice(text, false) != Answer::Deny; }
    Answer ask_choice(const std::string& text, bool session) override {
        auto p = std::make_shared<Prompt>();
        p->text = text;
        p->session = session;
        std::unique_lock<std::mutex> lk(g_mu);
        if (g_test_answer) {
            const int a = g_test_answer == 2 && !session ? 1 : g_test_answer;
            LOG("[editor] MCP prompt answered by the test run: %s", a == 2 ? "allow for this session" : a > 0 ? "allow" : "deny");
            return a == 2 ? Answer::AllowSession : a > 0 ? Answer::Allow : Answer::Deny;
        }
        p->id = ++g_prompt_ids;
        g_prompts.push_back(p);
        const bool answered = g_prompt_cv.wait_for(lk, kPromptWait, [&] { return p->answer != 0; });
        for (auto it = g_prompts.begin(); it != g_prompts.end(); ++it)
            if (*it == p) {
                g_prompts.erase(it);
                break;
            }
        if (!answered || p->answer < 0) return Answer::Deny;
        return p->answer == 2 && p->session ? Answer::AllowSession : Answer::Allow;
    }
    bool screenshot(bool gamepad, std::string& png, uint32_t& width, uint32_t& height, std::string& why) override {
        struct Wait {
            std::mutex mu;
            std::condition_variable cv;
            bool done = false;
            std::string png, why;
            uint32_t w = 0, h = 0;
        };
        auto w = std::make_shared<Wait>();
        if (!screenshot::capture(gamepad, 1280, [w](const std::string& data, uint32_t pw, uint32_t ph, const std::string& reason) {
                std::lock_guard<std::mutex> lk(w->mu);
                w->png = data;
                w->w = pw, w->h = ph;
                w->why = reason;
                w->done = true;
                w->cv.notify_all();
            }))
            return why = "another picture is being taken", false;
        std::unique_lock<std::mutex> lk(w->mu);
        if (!w->cv.wait_for(lk, kPictureWait, [&] { return w->done; })) {
            lk.unlock();
            screenshot::cancel_capture();
            return why = "no frame was shown in time (is the game window minimised?)", false;
        }
        if (w->png.empty()) return why = w->why.empty() ? "no picture" : w->why, false;
        png = std::move(w->png);
        width = w->w, height = w->h;
        return true;
    }
    std::string package_dir() override {
        std::string dir;
        editor::with_session([&](editor::Session& s) { dir = s.package_dir(s.open_mod()); });
        return dir;
    }
    std::vector<std::string> picked() override {
        std::lock_guard<std::mutex> lk(g_mu);
        return g_picked;
    }
    std::vector<std::string> export_roots() override {
        std::vector<std::string> v{export_folder()};
        std::lock_guard<std::mutex> lk(g_mu);
        for (const auto& p : g_picked)
            if (fs::is_directory(p)) v.push_back(p);
        return v;
    }
    void note(const std::string& tool, const Value& args, const Value& result) override { editor::note(tool, args, result, "MCP"); }
    void request(const std::string& method) override {
        if (method != "tools/call" && method != "ping") LOG("[editor] MCP %s", method.c_str());  // (tool calls are in the editor's Log)
    }
};
GameBackend g_backend;
std::shared_ptr<Handler> g_handler;  // under g_mu
Server g_server;                     // started / stopped under g_mu

bool start(const std::string& token, int port, std::string& why) {
    if (token.size() < 32) return why = "no random token could be made", false;
    auto h = std::make_shared<Handler>(g_backend, token, port, build::version(), g_allows);
    if (!g_server.start(h, port, why)) return false;
    g_handler = h;
    g_token = token;
    LOG("[editor] MCP interface on: 127.0.0.1 port %d", g_server.port());  // (never the token)
    return true;
}
}  // namespace

bool available() { return editor::launch_mode(); }

bool set_on(bool on, std::string& why) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!on) {
        if (g_server.running()) LOG("[editor] MCP interface off");
        g_server.stop();
        g_handler.reset();
        g_token.clear();
        g_error.clear();
        for (auto& p : g_prompts) p->answer = -1;  // a waiting question is answered "no"
        g_prompt_cv.notify_all();
        return true;
    }
    if (!available()) return why = "the MCP interface exists only in the editor launch mode (--editor) of desktop builds", false;
    if (g_server.running()) return true;
    if (!start(kept_token(), kDefaultPort, why)) {
        g_error = why;
        return false;
    }
    g_error.clear();
    return true;
}

Status status() {
    std::lock_guard<std::mutex> lk(g_mu);
    Status s;
    s.on = g_server.running();
    s.error = g_error;
    if (!s.on) return s;
    s.port = g_server.port();
    s.token = g_token;
    if (g_handler) {
        s.calls = g_handler->calls();
        const auto seen = g_handler->last_seen();
        s.connected = seen.time_since_epoch().count() != 0 && std::chrono::steady_clock::now() - seen < std::chrono::seconds(30);
    }
    return s;
}
std::string status_line() {
    const Status s = status();
    if (!s.on) return "MCP: off";
    if (s.connected) return "MCP: connected";
    return "MCP: on, port " + std::to_string(s.port);
}
std::string client_command(const Status& s) { return claude_code_command(s.port, s.token); }
std::string desktop_config(const Status& s) { return claude_desktop_config(s.port, s.token); }
bool renew_token(std::string& why) {
    const std::string t = new_token();
    if (!valid_token(t)) return why = "no random token could be made", false;
    save_token(t);
    std::lock_guard<std::mutex> lk(g_mu);
    g_kept = t;
    if (g_handler) g_handler->set_token(t);  // the old one is refused from the next request on
    if (g_server.running()) g_token = t;
    LOG("[editor] MCP token replaced");  // (never the token)
    return true;
}
void launch(bool mcp) {
    switch (launch_choice(editor::launch_mode(), mcp, true)) {
    case LaunchMcp::Off:
        return;
    case LaunchMcp::NeedsEditor:
        LOG("[editor] --mcp needs --editor (start with --editor --mcp): Claude / MCP access stays off");
        return;
    case LaunchMcp::Unavailable:
        return;
    case LaunchMcp::On: {
        std::string why;
        if (set_on(true, why))
            LOG("[editor] Claude / MCP access on at start (--mcp)");
        else
            LOG("[editor] --mcp: Claude / MCP access could not start: %s", why.c_str());
        return;
    }
    }
}
bool prompt(uint64_t& id, std::string& text, bool* session) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& p : g_prompts)
        if (p->answer == 0) {
            id = p->id;
            text = p->text;
            if (session) *session = p->session;
            return true;
        }
    return false;
}
void answer(uint64_t id, bool allow, bool session) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& p : g_prompts)
        if (p->id == id) p->answer = !allow ? -1 : session && p->session ? 2 : 1;
    g_prompt_cv.notify_all();
}
bool prompt_waiting() {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& p : g_prompts)
        if (p->answer == 0) return true;
    return false;
}

void user_picked(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    const std::string c = canonical(path);
    for (const auto& p : g_picked)
        if (p == c) return;
    g_picked.push_back(c);
    if (g_picked.size() > 32) g_picked.erase(g_picked.begin());
}

void start_for_tests() {
    const char* port = std::getenv("WWHD_TEST_EDITOR_MCP");
    const char* token = std::getenv("WWHD_TEST_EDITOR_MCP_TOKEN");
    if (!port || !*port || !available()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_server.running()) return;
    const std::string t = token ? token : "";
    if (t.size() != 64 || t.find_first_not_of("0123456789abcdef") != std::string::npos) {
        LOG("[editor] WWHD_TEST_EDITOR_MCP: WWHD_TEST_EDITOR_MCP_TOKEN must be 64 hex digits; the interface stays off");
        return;
    }
    if (const char* a = std::getenv("WWHD_TEST_EDITOR_MCP_ANSWER"))
        g_test_answer = std::string(a) == "allow" ? 1 : std::string(a) == "session" ? 2 : std::string(a) == "deny" ? -1 : 0;
    std::string why;
    if (!start(t, std::atoi(port), why)) {
        g_error = why;
        LOG("[editor] MCP interface (test): %s", why.c_str());
    } else
        LOG("[editor] MCP interface (test run) on port %d", g_server.port());
}

#else
// built without the interface (-DWWHD_EDITOR_MCP=OFF) or Android: nothing listens, the switch is not shown
bool available() { return false; }
bool set_on(bool on, std::string& why) { return on ? (why = "this build has no MCP interface", false) : true; }
Status status() { return {}; }
std::string status_line() { return "MCP: off"; }
std::string client_command(const Status&) { return {}; }
std::string desktop_config(const Status&) { return {}; }
bool renew_token(std::string& why) { return why = "this build has no MCP interface", false; }
// --mcp where nothing can listen (Android, or built with -DWWHD_EDITOR_MCP=OFF): it only says so
void launch(bool mcp) {
    if (mcp) LOG("[editor] --mcp: this build has no Claude / MCP access (not on Android, or built without it)");
}
bool prompt(uint64_t&, std::string&, bool*) { return false; }
void answer(uint64_t, bool, bool) {}
bool prompt_waiting() { return false; }
void user_picked(const std::string&) {}
void start_for_tests() {}
#endif

}  // namespace editor_mcp
