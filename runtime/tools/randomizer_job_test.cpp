// SPDX-License-Identifier: MPL-2.0
// Settings > Randomizer's background job (mods/randomizer_job.cpp, desktop): the installed setup as guest-sdk.json
// names it, run with the request's options; its status file followed while it works; its end (ready with the seed,
// or the reason it stopped); one job at a time; cancel; whether this install can make randomized games at all.
// A stand-in setup.py checks the command line and answers like setup.py run_randomizer.
#include "mods/code_mods.h"
#include "mods/mod_json.h"
#include "mods/randomizer_job.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {
std::mutex settings_mutex;
std::map<std::string, std::string> settings;
void env(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}
void clear_env(const char* key) {
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}
}  // namespace
namespace hostui {
bool get(const char* key, std::string& value) {
    std::lock_guard lock(settings_mutex);
    value = settings[key];
    return !value.empty();
}
void set(const char* key, const std::string& value) {
    std::lock_guard lock(settings_mutex);
    settings[key] = value;
}
void post(std::function<void()> fn) { fn(); }
}  // namespace hostui
namespace mods::packages {
bool enable_after_code_rebuild(const std::string&, std::string&) { return true; }
}  // namespace mods::packages

namespace fs = std::filesystem;
using randomizer_job::Request;
using randomizer_job::Status;

static std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

template <class F> static bool wait_for(F done, int seconds = 20) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

static Status finished() {
    assert(wait_for([] { return !randomizer_job::status().running; }));
    return randomizer_job::status();
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto root = fs::temp_directory_path() /
                      ("wwhd-randomizer-job-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path release = root / "release", data = root / "data";
    fs::create_directories(release / "tools" / "installer");
    fs::create_directories(data);
    const fs::path tool = release / "tools" / "installer" / "setup.py";
    // the stand-in setup: the game's options (randomizer_job.cpp start), the status file setup.py run_randomizer writes
    std::ofstream(tool) << R"(import argparse, json, os, sys, time
p = argparse.ArgumentParser()
p.add_argument('--data-dir'); p.add_argument('--randomize', action='store_true'); p.add_argument('--import-seed')
p.add_argument('--rando-preset'); p.add_argument('--rando-seed'); p.add_argument('--rando-new-seed', action='store_true')
p.add_argument('--rando-permalink'); p.add_argument('--code-mods'); p.add_argument('--yes', action='store_true')
p.add_argument('--code-mods-status'); p.add_argument('--code-mods-cancel'); p.add_argument('--jobs')
a = p.parse_args()
open(os.path.join(a.data_dir, 'args.json'), 'w').write(json.dumps(sys.argv[1:]))
assert a.yes and a.jobs == '4' and a.code_mods in ('0', '1') and (a.randomize != bool(a.import_seed))
def status(v):
    open(a.code_mods_status + '.tmp', 'w').write(json.dumps(v)); os.replace(a.code_mods_status + '.tmp', a.code_mods_status)
if a.rando_seed == 'crash':
    print('Making a randomized game'); print('Setup stopped: the randomizer stopped (exit code 3)'); sys.exit(1)
if a.import_seed:
    status({'state': 'error', 'message': 'This is not a randomized game.'}); print('Setup stopped: x'); sys.exit(1)
status({'state': 'building', 'stage': 'Compiling game code', 'done': 3, 'total': 10})
go = os.path.join(a.data_dir, 'go')
while not os.path.exists(go):
    if os.path.exists(a.code_mods_cancel):
        status({'state': 'error', 'message': 'Code-mod rebuild cancelled; previous build retained'}); sys.exit(1)
    time.sleep(0.01)
os.remove(go)
status({'state': 'ready', 'fingerprint': 'f' * 64, 'exe': '/x', 'seed': '0123456789abcdef', 'hash': 'Hash ' + (a.rando_seed or a.rando_permalink or 'new'), 'imported': False})
)";
    mods::json::Value config;
    config["format_version"] = 2;
    config["python"].type = mods::json::Value::Array;
    config["python"].array.emplace_back(argv[1]);
    config["setup"] = tool.string();
    config["data_dir"] = data.string();
    const auto path = root / "guest-sdk.json";
    std::ofstream(path) << mods::json::dump(config);
    env("WWHD_GUEST_BUILD_CONFIG", path.string());
    clear_env("WWHD_RANDO_EXE");
    env("WWHD_CODE_MODS", "0");
    auto args = [&] { return mods::json::parse(read(data / "args.json")); };
    auto has = [&](const mods::json::Value& a, const std::string& x) {
        for (const auto& v : a.array)
            if (v.string() == x) return true;
        return false;
    };
    auto after = [&](const mods::json::Value& a, const std::string& x) {
        for (size_t i = 0; i + 1 < a.array.size(); i++)
            if (a.array[i].string() == x) return a.array[i + 1].string();
        return std::string();
    };

    // a release without the randomizer can't make randomized games; with it, it can
    assert(randomizer_job::unavailable().find("no randomizer") != std::string::npos);
    fs::create_directories(release / "tools" / "bin");
#ifdef _WIN32
    std::ofstream(release / "tools" / "bin" / "wwhd-rando.exe") << "x";
#else
    std::ofstream(release / "tools" / "bin" / "wwhd-rando") << "x";
#endif
    assert(randomizer_job::unavailable().empty());

    // a new game from a preset with a new random seed: progress from the status file, one job at a time, then ready
    Request r;
    r.preset = "beginner";
    std::string why;
    assert(randomizer_job::start(r, why) && why.empty());
    assert(wait_for([] { return randomizer_job::status().total == 10; }));
    Status s = randomizer_job::status();
    assert(s.running && !s.ready && s.stage == "Compiling game code" && s.done == 3);
    assert(!randomizer_job::start(r, why) && why.find("already") != std::string::npos);
    auto a = args();
    assert(has(a, "--randomize") && after(a, "--rando-preset") == "beginner" && has(a, "--rando-new-seed"));
    assert(after(a, "--data-dir") == data.string() && after(a, "--code-mods") == "0");
    assert(after(a, "--code-mods-status") == (data / "randomizer-status.json").string());
    assert(after(a, "--code-mods-cancel") == (data / "randomizer-cancel").string());
    std::ofstream(data / "go") << "go";
    s = finished();
    assert(s.ready && s.error.empty() && s.seed == "0123456789abcdef" && s.hash == "Hash new" && !s.imported);
    randomizer_job::dismiss();
    assert(!randomizer_job::status().ready);

    // a seed name; the game's code-mod choice goes with it
    env("WWHD_CODE_MODS", "1");
    r.seed = "Ganon Seed";
    assert(randomizer_job::start(r, why));
    assert(wait_for([] { return randomizer_job::status().total == 10; }));
    a = args();
    assert(after(a, "--rando-seed") == "Ganon Seed" && !has(a, "--rando-new-seed") && after(a, "--code-mods") == "1");
    std::ofstream(data / "go") << "go";
    assert(finished().hash == "Hash Ganon Seed");

    // a permalink: its settings and seed, no preset
    r = Request{};
    r.permalink = "LINK-everything-Omega";
    assert(randomizer_job::start(r, why));
    assert(wait_for([] { return randomizer_job::status().total == 10; }));
    a = args();
    assert(after(a, "--rando-permalink") == "LINK-everything-Omega" && !has(a, "--rando-preset"));
    std::ofstream(data / "go") << "go";
    assert(finished().ready);

    // cancel: setup stops at its next step and says so
    r = Request{};
    r.preset = "standard";
    assert(randomizer_job::start(r, why));
    assert(wait_for([] { return randomizer_job::status().total == 10; }));
    randomizer_job::cancel();
    s = finished();
    assert(!s.ready && s.error.find("cancelled") != std::string::npos);
    // a start clears the last result
    assert(fs::exists(data / "randomizer-cancel"));

    // an import: the folder; setup's refusal is the message
    r = Request{};
    r.import = true;
    assert(!randomizer_job::start(r, why) && why.find("output folder") != std::string::npos);
    r.folder = (root / "official output").string();
    assert(randomizer_job::start(r, why));
    assert(!fs::exists(data / "randomizer-cancel"));
    s = finished();
    assert(!s.ready && s.error == "This is not a randomized game.");
    assert(after(args(), "--import-seed") == r.folder && !has(args(), "--randomize"));

    // setup ended without a status: the line where it said why it stopped
    r = Request{};
    r.seed = "crash";
    assert(randomizer_job::start(r, why));
    s = finished();
    assert(!s.ready && s.error == "the randomizer stopped (exit code 3)");
    randomizer_job::dismiss();
    s = randomizer_job::status();
    assert(!s.running && !s.ready && s.error.empty());

    // without setup's tools the page says why
    env("WWHD_GUEST_BUILD_CONFIG", (root / "missing.json").string());
    assert(!randomizer_job::unavailable().empty());
    assert(!randomizer_job::start(r, why) && !why.empty());
    clear_env("WWHD_GUEST_BUILD_CONFIG");
    clear_env("WWHD_CODE_MODS");
    fs::remove_all(root);
    std::puts("randomizer job: ok");
}
