// World editor MCP interface (runtime/src/editor/mcp.h): HTTP parsing, Host / Origin, the token, JSON-RPC
// (initialize, tools/list with every command, tools/call ok and error), the confirm and Allow rule, the
// path rule, the size limit and the loopback listener (real sockets on 127.0.0.1).
#include "editor/commands.h"
#include "editor/mcp.h"
#include "editor/mcp_server.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using editor::Value;
using namespace editor_mcp;

namespace {
int g_failures = 0;
#define CHECK(x)                                                                    \
    do {                                                                            \
        if (!(x)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

Value json(const char* text) { return mods::json::parse(text); }

struct FakeHost : editor::Host {
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string& note,
                      std::string&) override {
        note = "";
        return true;
    }
    bool reload_room(std::string&) override { return true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"sea", {11}}}; }
    bool warp(const editor::Location&, std::string&) override { return true; }
    editor::Place link() override {
        editor::Place p;
        p.valid = true;
        p.stage = "sea";
        p.room = 11;
        p.position = {0, 1040, -201500};
        return p;
    }
    bool busy(std::string&) override { return false; }
    bool fly(bool, bool, std::string&) override { return true; }
    Value fly_state() override { return Value(); }
    bool set_test_mode(bool on, std::string&) override { return testing = on, true; }
    bool test_mode() override { return testing; }
    bool reset_records(const std::string&, const std::vector<editor::Record>&, const std::vector<editor::Record>&, unsigned& erased,
                       std::string&) override {
        erased = 0;
        return true;
    }
    bool test_state(bool, int, const std::string&, const std::vector<editor::Record>&, std::string&) override { return true; }
    bool read_records(const std::vector<editor::Record>& records, std::vector<int>& values, std::string&) override {
        values.assign(records.size(), -1);
        if (!values.empty()) values.back() = 0;  // the last quest's flags
        for (size_t i = 0; i < records.size(); i++)
            if (records[i].kind == 4) values[i] = 2;  // quest step 2
        return true;
    }
    bool testing = false;
};

const char* kManifest = R"({
  "format_version": 1, "id": "editor-test", "name": "Editor test", "version": "1.0.0", "kind": "content",
  "actors": [
    {"id": "sign-a", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 10, "y": 1040, "z": -201500},
     "flag": "sign-a-read", "lines": ["Hello."]},
    {"id": "sign-b", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 300, "y": 1040, "z": -201500}, "flag": "sign-b-read", "lines": ["Hi."]}
  ]
})";

// a backend over a real session (the command layer), counting prompts
struct FakeBackend : Backend {
    FakeHost host;
    editor::Session session{&host};
    std::string dir;
    std::vector<std::string> picked_list, roots;
    int asked = 0, runs = 0, notes = 0;
    bool allow = false;
    std::string last_question;
    Value run(const std::string& command, const Value& params) override {
        ++runs;
        return session.run(command, params, true, "MCP");
    }
    bool ask(const std::string& text) override {
        ++asked;
        last_question = text;
        return allow;
    }
    bool screenshot(bool gamepad, std::string& png, uint32_t& w, uint32_t& h, std::string& why) override {
        if (gamepad) return why = "the GamePad picture is not shown", false;
        png = std::string("\x89PNG\r\n\x1a\n", 8) + "xyz";
        w = 1280, h = 720;
        return true;
    }
    std::string package_dir() override { return dir; }
    std::vector<std::string> picked() override { return picked_list; }
    std::vector<std::string> export_roots() override { return roots; }
    void note(const std::string& tool, const Value& args, const Value& result) override {
        ++notes;
        session.note(tool, args, result, "MCP");
    }
};

const std::string kToken = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

Request post(const std::string& body, const std::string& token = kToken, int port = 47615) {
    Request r;
    r.method = "POST";
    r.target = "/mcp";
    r.version = "HTTP/1.1";
    r.body = body;
    r.headers = {{"Host", "127.0.0.1:" + std::to_string(port)}, {"Content-Type", "application/json"}, {"Accept", "application/json, text/event-stream"}};
    if (!token.empty()) r.headers.push_back({"Authorization", "Bearer " + token});
    return r;
}
Value call(Handler& h, const std::string& tool, const Value& args) {
    Value m;
    m["jsonrpc"] = "2.0";
    m["id"] = 7;
    m["method"] = "tools/call";
    m["params"]["name"] = tool;
    m["params"]["arguments"] = args;
    bool reply = false;
    Value r = h.message(m, reply);
    CHECK(reply);
    return r;
}
bool is_error(const Value& r) { return r.get("result").get("isError").boolean; }
std::string text_of(const Value& r) {
    for (const auto& c : r.get("result").get("content").array)
        if (c.get("type").string() == "text") return c.get("text").text;
    return {};
}

// ---------------------------------------------------------------- HTTP
void http_parsing() {
    Request r;
    size_t used = 0;
    std::string why;
    const std::string one = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:1\r\nContent-Length: 2\r\n\r\n{}";
    CHECK(parse_request(one.substr(0, 20), r, used, why) == Parsed::Incomplete);
    CHECK(parse_request(one.substr(0, one.size() - 1), r, used, why) == Parsed::Incomplete);
    CHECK(parse_request(one, r, used, why) == Parsed::Complete && used == one.size() && r.body == "{}" && r.method == "POST" && r.target == "/mcp");
    CHECK(r.header("HOST") == "127.0.0.1:1" && r.keep_alive());
    // two requests in one read (pipelining): the first, then the rest
    CHECK(parse_request(one + one, r, used, why) == Parsed::Complete && used == one.size());
    CHECK(parse_request("GARBAGE\r\n\r\n", r, used, why) == Parsed::Bad);
    CHECK(parse_request("post /mcp HTTP/1.1\r\n\r\n", r, used, why) == Parsed::Bad);
    CHECK(parse_request("POST /mcp HTTP/2\r\n\r\n", r, used, why) == Parsed::Bad);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nNoColon\r\n\r\n", r, used, why) == Parsed::Bad);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", r, used, why) == Parsed::Bad);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nContent-Length: -1\r\n\r\n", r, used, why) == Parsed::Bad);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\n{}", r, used, why) == Parsed::Bad);
    // the size limits: a body over 1 MB is refused from its Content-Length, before it arrives
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nContent-Length: 1048577\r\n\r\n", r, used, why) == Parsed::TooLarge);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nContent-Length: 1048576\r\n\r\n", r, used, why) == Parsed::Incomplete);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nX: " + std::string(20000, 'a'), r, used, why) == Parsed::TooLarge);
    CHECK(parse_request("POST /mcp HTTP/1.1\r\nConnection: close\r\n\r\n", r, used, why) == Parsed::Complete && !r.keep_alive());
    Response x;
    x.body = "{}";
    const std::string t = response_text(x, true);
    CHECK(t.rfind("HTTP/1.1 200 OK\r\n", 0) == 0 && t.find("Content-Length: 2\r\n") != std::string::npos && t.size() >= 2 &&
          t.substr(t.size() - 2) == "{}");
}

// ---------------------------------------------------------------- checks
void checks() {
    CHECK(host_allowed("127.0.0.1:5000", 5000));
    CHECK(host_allowed("localhost:5000", 5000));
    CHECK(host_allowed("LocalHost:5000", 5000));
    CHECK(host_allowed("[::1]:5000", 5000));
    CHECK(!host_allowed("127.0.0.1:5001", 5000));
    CHECK(!host_allowed("127.0.0.1", 5000));
    CHECK(!host_allowed("", 5000));
    CHECK(!host_allowed("evil.example:5000", 5000));
    CHECK(!host_allowed("localhost.evil.example:5000", 5000));
    CHECK(!host_allowed("127.0.0.2:5000", 5000));
    CHECK(!host_allowed("::1:5000", 5000));
    CHECK(!host_allowed("[::1]", 5000));
    CHECK(origin_allowed(""));
    CHECK(origin_allowed("http://localhost:3000"));
    CHECK(origin_allowed("http://127.0.0.1"));
    CHECK(origin_allowed("https://[::1]:8080"));
    CHECK(!origin_allowed("null"));
    CHECK(!origin_allowed("https://evil.example"));
    CHECK(!origin_allowed("http://localhost.evil.example"));
    CHECK(!origin_allowed("http://localhost@evil.example"));
    CHECK(!origin_allowed("file://"));
    CHECK(!origin_allowed("http://127.0.0.1.nip.io"));

    CHECK(token_matches("Bearer " + kToken, kToken));
    CHECK(token_matches("bearer " + kToken, kToken));
    CHECK(!token_matches("Bearer " + kToken.substr(1), kToken));
    CHECK(!token_matches("Bearer " + kToken + "0", kToken));
    CHECK(!token_matches("Bearer " + std::string(kToken.size(), '0'), kToken));
    CHECK(!token_matches(kToken, kToken));
    CHECK(!token_matches("Basic " + kToken, kToken));
    CHECK(!token_matches("Bearer ", ""));  // no token: nothing passes
    const std::string a = new_token(), b = new_token();
    CHECK(a.size() == 64 && a.find_first_not_of("0123456789abcdef") == std::string::npos && a != b);

    const fs::path root = fs::temp_directory_path() / ("wwhd-mcp-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "pkg" / "models");
    fs::create_directories(root / "pkgx");
    CHECK(inside((root / "pkg" / "models" / "a.glb").string(), (root / "pkg").string()));
    CHECK(inside((root / "pkg").string(), (root / "pkg").string()));
    CHECK(inside((root / "pkg" / "new" / "deeper").string(), (root / "pkg").string()));
    CHECK(!inside((root / "pkgx" / "a.glb").string(), (root / "pkg").string()));  // a common prefix is not inside
    CHECK(!inside((root / "pkg" / ".." / "pkgx" / "a.glb").string(), (root / "pkg").string()));
    CHECK(!inside((root / "pkg").string(), ""));
#if !defined(_WIN32)
    fs::create_directory_symlink(root / "pkgx", root / "pkg" / "link");
    CHECK(!inside((root / "pkg" / "link" / "a.glb").string(), (root / "pkg").string()));  // a link out of the package
#endif
    fs::remove_all(root);
}

// ---------------------------------------------------------------- JSON-RPC and tools
void json_rpc() {
    FakeBackend be;
    be.session.add_package("editor-test", "Editor test", "", json(kManifest));
    Handler h(be, kToken, 47615, "test");

    // initialize: a version the server has is kept, another one gets the newest
    Response r = h.handle(post(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})"));
    CHECK(r.status == 200);
    Value v = json(r.body.c_str());
    CHECK(v.get("id").number == 1 && v.get("result").get("protocolVersion").string() == "2025-03-26");
    CHECK(v.get("result").get("capabilities").get("tools").type == Value::Object);
    CHECK(v.get("result").get("serverInfo").get("name").string() == "wwhd-editor");
    r = h.handle(post(R"({"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"1999-01-01"}})"));
    v = json(r.body.c_str());
    CHECK(v.get("id").string() == "a" && v.get("result").get("protocolVersion").string() == kProtocolVersions[0]);
    // notifications: 202, no body
    r = h.handle(post(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
    CHECK(r.status == 202 && r.body.empty());
    // ping
    r = h.handle(post(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
    CHECK(r.status == 200 && json(r.body.c_str()).get("result").type == Value::Object);

    // tools/list: every command, with its schema, and screenshot
    r = h.handle(post(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})"));
    v = json(r.body.c_str());
    std::set<std::string> names;
    for (const auto& t : v.get("result").get("tools").array) {
        names.insert(t.get("name").string());
        CHECK(!t.get("description").string().empty());
        CHECK(t.get("inputSchema").get("type").string() == "object");
    }
    for (const auto& c : editor::command_list()) CHECK(names.count(c.name) == 1);
    CHECK(names.count("screenshot") == 1 && names.size() == editor::command_list().size() + 1);
    CHECK(!editor::find_command("screenshot"));  // the extra tool never hides a command
    for (const auto& t : v.get("result").get("tools").array) {
        if (t.get("name").string() == "move_actor") {
            const Value& s = t.get("inputSchema");
            CHECK(s.get("properties").get("position").get("type").string() == "object");
            CHECK(s.get("properties").get("position").get("required").array.size() == 3);
            CHECK(s.get("required").array.size() == 2 && s.get("additionalProperties").type == Value::Bool);
        }
        if (t.get("name").string() == "remove_actor") CHECK(t.get("annotations").get("destructiveHint").boolean);
    }
    // every parameter that names a file is covered by the path rule
    for (const auto& c : editor::command_list())
        for (const auto& p : c.params)
            if (p.name == "file" || p.name == "path" || p.name == "folder" || p.name == "dir" || p.name == "glb_path") {
                bool covered = false;
                for (const auto& pp : path_params()) covered = covered || (c.name == pp.command && p.name == pp.param);
                CHECK(covered);
            }

    // tools/call: a command that works, one that fails, an unknown tool, bad arguments
    v = call(h, "list_actors", Value());
    CHECK(!is_error(v) && text_of(v).find("sign-a") != std::string::npos);
    v = call(h, "move_actor", json(R"({"id":"sign-a","position":{"x":20,"y":1040,"z":-201500}})"));
    if (is_error(v)) std::fprintf(stderr, "move_actor: %s\n", text_of(v).c_str());
    CHECK(!is_error(v));
    v = call(h, "move_actor", json(R"({"id":"nobody","position":{"x":20,"y":1040,"z":-201500}})"));
    CHECK(is_error(v) && !text_of(v).empty());
    v = call(h, "move_actor", json(R"({"id":"sign-a","position":{"x":20}})"));
    CHECK(is_error(v));
    v = call(h, "set_dialogue", json(R"({"id":"sign-a","lines":["One.","Two."]})"));
    CHECK(!is_error(v));
    v = call(h, "read_state", Value());
    CHECK(!is_error(v));
    const Value state = json(text_of(v).c_str());
    CHECK(state.get("progress").get("known").boolean && state.get("progress").get("flags").get("sign-a-read").type == Value::Bool);
    {
        Value m = json(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"no_such_tool","arguments":{}}})");
        bool reply = false;
        Value e = h.message(m, reply);
        CHECK(reply && e.get("error").get("code").number == -32602);
        m = json(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"list_actors","arguments":[1]}})");
        e = h.message(m, reply);
        CHECK(e.get("error").get("code").number == -32602);
        m = json(R"({"jsonrpc":"2.0","id":9,"method":"resources/list"})");
        e = h.message(m, reply);
        CHECK(e.get("error").get("code").number == -32601);
        m = json(R"({"id":9,"method":"ping"})");
        e = h.message(m, reply);
        CHECK(reply && e.get("error").get("code").number == -32600);
        m = json(R"({"jsonrpc":"2.0","id":{"x":1},"method":"ping"})");
        e = h.message(m, reply);
        CHECK(reply && e.get("error").get("code").number == -32600 && e.get("id").type == Value::Null);
    }
    // malformed bodies: JSON-RPC errors, never a crash
    r = h.handle(post("{not json"));
    CHECK(r.status == 400 && json(r.body.c_str()).get("error").get("code").number == -32700);
    r = h.handle(post("[]"));
    CHECK(r.status == 400);
    r = h.handle(post(std::string(64, '[')));
    CHECK(r.status == 400);
    r = h.handle(post(R"([{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/initialized"}])"));
    CHECK(r.status == 200 && json(r.body.c_str()).array.size() == 1);
    // the screenshot tool: an image content
    v = call(h, "screenshot", Value());
    CHECK(!is_error(v) && v.get("result").get("content").array[0].get("type").string() == "image" &&
          v.get("result").get("content").array[0].get("mimeType").string() == "image/png" &&
          v.get("result").get("content").array[0].get("data").string() == "iVBORw0KGgp4eXo=");
    v = call(h, "screenshot", json(R"({"screen":"gamepad"})"));
    CHECK(is_error(v));
    v = call(h, "screenshot", json(R"({"screen":"tv","extra":1})"));
    CHECK(is_error(v));

    // every call is in the command log, marked MCP
    bool marked = false;
    for (const auto& e : be.session.log()) marked = marked || (e.command == "move_actor" && e.source == "MCP");
    CHECK(marked);
    bool shot_logged = false;
    for (const auto& e : be.session.log()) shot_logged = shot_logged || (e.command == "screenshot" && e.source == "MCP");
    CHECK(shot_logged);
}

// ---------------------------------------------------------------- HTTP-level checks: auth, Host, Origin
void http_checks() {
    FakeBackend be;
    be.session.add_package("editor-test", "Editor test", "", json(kManifest));
    Handler h(be, kToken, 47615);
    const std::string init = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})";
    CHECK(h.handle(post(init)).status == 200);
    CHECK(h.handle(post(init, "")).status == 401);
    CHECK(h.handle(post(init, kToken.substr(0, 63) + "0")).status == 401);
    Response r = h.handle(post(init, ""));
    bool www = false;
    for (const auto& [k, v] : r.headers) www = www || k == "WWW-Authenticate";
    CHECK(www);
    Request q = post(init);
    q.headers[0].second = "evil.example:47615";
    CHECK(h.handle(q).status == 403);
    q = post(init);
    q.headers[0].second = "127.0.0.1:80";
    CHECK(h.handle(q).status == 403);
    q = post(init);
    q.headers.push_back({"Origin", "https://evil.example"});
    CHECK(h.handle(q).status == 403);
    q = post(init);
    q.headers.push_back({"Origin", "http://localhost:6274"});  // a local inspector page
    CHECK(h.handle(q).status == 200);
    // Host is checked before the token: a rebinding page never learns anything else
    q = post(init, "");
    q.headers[0].second = "evil.example:47615";
    CHECK(h.handle(q).status == 403);
    q = post(init);
    q.target = "/other";
    CHECK(h.handle(q).status == 404);
    q = post(init);
    q.method = "GET";
    CHECK(h.handle(q).status == 405);
    q = post(init);
    q.headers[1].second = "text/plain";
    CHECK(h.handle(q).status == 415);
    q = post(init);
    q.headers.push_back({"MCP-Protocol-Version", "1999-01-01"});
    CHECK(h.handle(q).status == 400);
    q = post(init);
    q.headers.push_back({"MCP-Protocol-Version", "2025-06-18"});
    CHECK(h.handle(q).status == 200);
    CHECK(h.last_seen().time_since_epoch().count() != 0);
}

// ---------------------------------------------------------------- confirm, Allow and paths
void safety() {
    FakeBackend be;
    const fs::path root = fs::temp_directory_path() / ("wwhd-mcp-safety-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "pkg" / "models");
    fs::create_directories(root / "elsewhere");
    fs::create_directories(root / "exports");
    std::ofstream(root / "pkg" / "models" / "a.glb") << "not a model";
    std::ofstream(root / "elsewhere" / "b.glb") << "not a model";
    be.dir = (root / "pkg").string();
    be.roots = {(root / "exports").string()};
    be.session.add_package("editor-test", "Editor test", be.dir, json(kManifest));
    Handler h(be, kToken, 47615);

    // destructive without confirm: refused by the command layer, nobody is asked, the actor stays
    Value v = call(h, "remove_actor", json(R"({"id":"sign-b"})"));
    CHECK(is_error(v) && text_of(v).find("confirm") != std::string::npos && be.asked == 0);
    CHECK(be.session.manifest("editor-test")->get("actors").array.size() == 2);
    // with confirm: the player is asked; Deny keeps it
    be.allow = false;
    v = call(h, "remove_actor", json(R"({"id":"sign-b","confirm":true})"));
    CHECK(is_error(v) && be.asked == 1 && be.last_question.find("remove_actor") != std::string::npos && be.last_question.find("sign-b") != std::string::npos);
    CHECK(be.session.manifest("editor-test")->get("actors").array.size() == 2);
    // Allow runs it, and undo brings it back as in the editor
    be.allow = true;
    v = call(h, "remove_actor", json(R"({"id":"sign-b","confirm":true})"));
    CHECK(!is_error(v) && be.asked == 2 && be.session.manifest("editor-test")->get("actors").array.size() == 1);
    v = call(h, "undo", Value());
    CHECK(!is_error(v) && be.session.manifest("editor-test")->get("actors").array.size() == 2);
    // the other destructive commands too
    for (const char* c : {"revert_package", "save_package", "reset_quest", "export_package", "load_state"}) CHECK(needs_allow(c, Value()));
    CHECK(!needs_allow("move_actor", Value()) && !needs_allow("import_model", Value()) && needs_allow("import_model", json(R"({"confirm":true})")));
    be.allow = false;
    const int before = be.asked;
    v = call(h, "load_state", json(R"({"slot":1})"));
    CHECK(is_error(v) && be.asked == before);
    v = call(h, "load_state", json(R"({"slot":1,"confirm":true})"));
    CHECK(is_error(v) && be.asked == before + 1);

    // files: inside the package, no question
    be.asked = 0;
    Value a;
    a["file"] = (root / "pkg" / "models" / "a.glb").string();
    v = call(h, "check_model", a);
    CHECK(be.asked == 0);
    // outside: the player is asked with the full path; Deny refuses before the command runs
    const int runs = be.runs;
    a["file"] = (root / "elsewhere" / "b.glb").string();
    v = call(h, "check_model", a);
    CHECK(is_error(v) && be.asked == 1 && be.last_question.find(canonical((root / "elsewhere" / "b.glb").string())) != std::string::npos && be.runs == runs);
    // a way out of the package through ".." is outside too
    a["file"] = (root / "pkg" / ".." / "elsewhere" / "b.glb").string();
    v = call(h, "check_model", a);
    CHECK(is_error(v) && be.asked == 2);
    // a file the player picked: no question
    be.picked_list = {canonical((root / "elsewhere" / "b.glb").string())};
    a["file"] = (root / "elsewhere" / "b.glb").string();
    v = call(h, "check_model", a);
    CHECK(be.asked == 2 && be.runs == runs + 1);
    // relative paths are refused
    a["file"] = "models/a.glb";
    v = call(h, "check_model", a);
    CHECK(is_error(v) && text_of(v).find("full path") != std::string::npos);
    // exports: only into the export folder (or a picked folder); elsewhere refused without asking
    be.allow = true;
    be.asked = 0;
    Value e;
    e["path"] = (root / "elsewhere" / "out").string();
    e["confirm"] = true;
    v = call(h, "export_package", e);
    CHECK(is_error(v) && be.asked == 0 && !fs::exists(root / "elsewhere" / "out"));
    e["path"] = (root / "exports" / "editor-test").string();
    v = call(h, "export_package", e);
    if (is_error(v)) std::fprintf(stderr, "export_package: %s\n", text_of(v).c_str());
    CHECK(be.asked == 1 && !is_error(v) && fs::exists(root / "exports" / "editor-test" / "manifest.json"));
    // refusals are in the log, marked MCP
    int refused = 0;
    for (const auto& l : be.session.log())
        if (l.source == "MCP" && !l.result.get("ok").boolean) ++refused;
    CHECK(refused >= 4);
    fs::remove_all(root);
}

// ---------------------------------------------------------------- the listener (real sockets)
#if defined(_WIN32)
using sock = SOCKET;
void close_sock(sock s) { closesocket(s); }
#else
using sock = int;
void close_sock(sock s) { close(s); }
#endif
sock connect_to(int port) {
    sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(uint16_t(port));
    if (connect(s, (const sockaddr*)&a, sizeof a) != 0) {
        close_sock(s);
        return sock(-1);
    }
    return s;
}
std::string roundtrip(int port, const std::string& request) {
    sock s = connect_to(port);
    if (s == sock(-1)) return {};
    send(s, request.data(), int(request.size()), 0);
    std::string out;
    char buf[8192];
    for (;;) {
        const int n = int(recv(s, buf, sizeof buf, 0));
        if (n <= 0) break;
        out.append(buf, size_t(n));
    }
    close_sock(s);
    return out;
}
int status_of(const std::string& response) { return response.size() > 12 ? std::atoi(response.c_str() + 9) : 0; }

void listener() {
#if defined(_WIN32)
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
#endif
    FakeBackend be;
    be.session.add_package("editor-test", "Editor test", "", json(kManifest));
    auto h = std::make_shared<Handler>(be, kToken, 0);
    Server server;
    std::string why;
    CHECK(server.start(h, 0, why));  // any free port
    const int port = server.port();
    CHECK(port > 0);
    const std::string host = "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
    auto request = [&](const std::string& extra, const std::string& b) {
        return "POST /mcp HTTP/1.1\r\n" + host + "Content-Type: application/json\r\nConnection: close\r\n" + extra +
               "Content-Length: " + std::to_string(b.size()) + "\r\n\r\n" + b;
    };
    std::string r = roundtrip(port, request("Authorization: Bearer " + kToken + "\r\n", body));
    CHECK(status_of(r) == 200 && r.find("\"remove_actor\"") != std::string::npos);
    CHECK(status_of(roundtrip(port, request("", body))) == 401);
    CHECK(status_of(roundtrip(port, request("Authorization: Bearer " + kToken + "\r\nOrigin: http://evil.example\r\n", body))) == 403);
    // over the size limit: refused from the header, the body is never read
    CHECK(status_of(roundtrip(port, "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 5000000\r\n\r\n")) == 413);
    CHECK(status_of(roundtrip(port, "NOT HTTP\r\n\r\n")) == 400);
    // keep-alive: two requests on one connection
    {
        sock s = connect_to(port);
        const std::string one = "POST /mcp HTTP/1.1\r\n" + host + "Content-Type: application/json\r\nAuthorization: Bearer " + kToken +
                                "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        const std::string last = "POST /mcp HTTP/1.1\r\n" + host + "Content-Type: application/json\r\nConnection: close\r\nAuthorization: Bearer " +
                                 kToken + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        const std::string both = one + last;
        send(s, both.data(), int(both.size()), 0);
        std::string out;
        char buf[65536];
        for (;;) {
            const int n = int(recv(s, buf, sizeof buf, 0));
            if (n <= 0) break;
            out.append(buf, size_t(n));
        }
        size_t seen = 0;
        for (size_t at = out.find("HTTP/1.1 200"); at != std::string::npos; at = out.find("HTTP/1.1 200", at + 1)) seen++;
        CHECK(seen == 2);
        close_sock(s);
    }
    // a few connections at once: the next one is turned away
    std::vector<sock> idle;
    for (int i = 0; i < kMaxConnections; i++) idle.push_back(connect_to(port));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(server.connections() == kMaxConnections);
    CHECK(status_of(roundtrip(port, request("Authorization: Bearer " + kToken + "\r\n", body))) == 503);
    for (sock s : idle) close_sock(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    CHECK(server.connections() == 0);
    // only loopback: nothing listens on another address of this machine (checked by the bind address)
    server.stop();
    CHECK(!server.running());
    CHECK(connect_to(port) == sock(-1));
}
}  // namespace

// The quest editor's commands reach tool clients by themselves (the tool list is the command list), and
// the ones that delete something need confirm and the player's Allow like remove_actor.
void quest_tools() {
    FakeBackend be;
    be.session.add_package("editor-test", "Editor test", "", json(kManifest));
    Handler h(be, kToken, 47615);
    bool reply = false;
    Value r = h.message(json(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})"), reply);
    std::set<std::string> names;
    for (const auto& t : r.get("result").get("tools").array) {
        names.insert(t.get("name").string());
        const std::string n = t.get("name").string();
        if (n == "remove_quest" || n == "remove_quest_step" || n == "remove_actors") CHECK(t.get("annotations").get("destructiveHint").boolean);
        if (n == "add_quest_step" || n == "start_at_step" || n == "set_flag") CHECK(!t.get("annotations").get("destructiveHint").boolean);
    }
    for (const char* c : {"quest_graph", "add_quest", "add_quest_step", "connect_quest_steps", "start_at_step", "list_flags", "set_flag", "search", "copy", "paste"})
        CHECK(names.count(c) == 1);
    for (const char* c : {"remove_quest", "remove_quest_step", "remove_actors"}) CHECK(needs_allow(c, Value()));
    for (const char* c : {"add_quest", "edit_quest_step", "move_quest_step", "connect_quest_steps", "set_quest_layout", "paste", "set_flag"}) CHECK(!needs_allow(c, Value()));
    // building a quest needs no question; removing a step does
    Value v = call(h, "add_quest", json(R"({"id":"tale","title":"A tale","text":"Read the sign."})"));
    CHECK(!is_error(v) && be.asked == 0);
    v = call(h, "add_quest_step", json(R"({"quest":"tale","text":"Come back."})"));
    CHECK(!is_error(v) && be.asked == 0);
    v = call(h, "remove_quest_step", json(R"({"quest":"tale","step":2})"));
    CHECK(is_error(v) && text_of(v).find("confirm") != std::string::npos && be.asked == 0);
    be.allow = false;
    v = call(h, "remove_quest_step", json(R"({"quest":"tale","step":2,"confirm":true})"));
    CHECK(is_error(v) && be.asked == 1 && be.last_question.find("remove_quest_step") != std::string::npos);
    CHECK(be.session.manifest("editor-test")->get("quests").array[0].get("steps").array.size() == 2);
    be.allow = true;
    v = call(h, "remove_quest_step", json(R"({"quest":"tale","step":2,"confirm":true})"));
    CHECK(!is_error(v) && be.asked == 2 && be.session.manifest("editor-test")->get("quests").array[0].get("steps").array.size() == 1);
    v = call(h, "remove_actors", json(R"({"ids":["sign-b"],"confirm":true})"));
    CHECK(!is_error(v) && be.asked == 3);
    v = call(h, "remove_quest", json(R"({"quest":"tale","confirm":true})"));
    CHECK(!is_error(v) && be.asked == 4 && be.session.manifest("editor-test")->get("quests").type == Value::Null);
    // flags: test mode only, never asked about
    v = call(h, "set_flag", json(R"({"id":"sign-a-read","value":true})"));
    CHECK(is_error(v) && text_of(v).find("test mode") != std::string::npos && be.asked == 4);
}

// A small quest of the later forms built through tools/call: a quest with an interior place, a
// resident with ordered talks (a flag condition, quest_done), a sign gated on two quests.
void quest_format_tools() {
    FakeBackend be;
    be.session.add_package("editor-test", "Editor test", "", json(kManifest));
    Handler h(be, kToken, 47615);
    // the tool descriptions teach the forms
    bool reply = false;
    Value r = h.message(json(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})"), reply);
    std::string all;
    const Value tools = r.get("result").get("tools");
    for (const auto& t : tools.array) all += t.get("description").string() + "\n";
    for (const char* form : {"quest_done", "not_flag", "quests: [{quest, step}]", "talks", "sea: {x, z}"}) CHECK(all.find(form) != std::string::npos);
    CHECK(!is_error(call(h, "add_quest", json(R"({"id":"tale","title":"A tale","text":"Talk to the keeper."})"))));
    CHECK(!is_error(call(h, "add_quest_step", json(R"({"quest":"tale","text":"Look inside."})"))));
    CHECK(!is_error(call(h, "add_quest", json(R"({"id":"bell","title":"The bell"})"))));
    {
        Value e = call(h, "edit_quest_step", json(R"({"quest":"tale","step":2,"fields":{"place":{"stage":"Pnezumi","room":0,"sea":{"x":0,"z":-200000},"point":0},"available":{"flag":["sign-a-read","sign-b-read"],"text":"Read both signs first."}}})"));
        if (is_error(e)) std::fprintf(stderr, "  %s\n", text_of(e).c_str());
        CHECK(!is_error(e));
    }
    CHECK(!is_error(call(h, "add_actor", json(R"({"type":"resident","id":"keeper","at":"position","position":{"x":100,"y":1040,"z":-201500},"stage":"sea","room":11,"snap":false,"options":{"resident":"uo2"}})"))));
    CHECK(!is_error(call(h, "set_actor", json(R"({"id":"keeper","property":"lines","value":null})"))));
    Value v = call(h, "set_actor", json(R"({"id":"keeper","property":"talks","value":[
        {"when":{"quest":"tale","step":1},"lines":["Look inside."],"then":[{"action":"quest_step","id":"tale","step":2}]},
        {"when":{"quest":"tale","step":2,"flag":"sign-a-read"},"lines":["Thank you."],"then":[{"action":"quest_done","id":"tale"}]},
        {"lines":["Hello."]}]})"));
    CHECK(!is_error(v));
    CHECK(!is_error(call(h, "set_actor", json(R"({"id":"sign-b","property":"when","value":{"quests":[{"quest":"tale","step":2},{"quest":"bell","step":1}],"not_flag":"sign-a-read"}})"))));
    CHECK(!is_error(call(h, "set_then", json(R"({"id":"sign-b","actions":[{"action":"quest_done","id":"bell"}]})"))));
    // a bad value comes back as a tool error with the validator's reason, nothing changes
    v = call(h, "set_then", json(R"({"id":"sign-b","actions":[{"action":"quest_done","id":"nope"}]})"));
    CHECK(is_error(v) && !text_of(v).empty());
    v = call(h, "quest_graph", json(R"({"quest":"tale"})"));
    CHECK(!is_error(v) && text_of(v).find("\"to\":\"end\"") != std::string::npos);
    v = call(h, "check_package", Value());
    CHECK(!is_error(v) && text_of(v).find("\"valid\":true") != std::string::npos);
    CHECK(be.asked == 0);
}

int main() {
    http_parsing();
    checks();
    json_rpc();
    http_checks();
    safety();
    quest_tools();
    quest_format_tools();
    listener();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("editor MCP tests passed\n");
    return 0;
}
