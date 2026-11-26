// SPDX-License-Identifier: MPL-2.0
#include "randomizer_job.h"

#include "code_mods.h"
#include "code_mods_io.h"
#include "mod_json.h"
#include "../platform/process.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#ifdef __ANDROID__
#include "../platform/android_code_mods.h"
#endif

namespace randomizer_job {
namespace {
namespace fs = std::filesystem;
namespace json = mods::json;

std::mutex mutex;
Status state;
fs::path status_path, cancel_path;
std::atomic<bool> stopping{false};
struct Worker {
    std::thread thread;
    ~Worker() {
        if (!thread.joinable()) return;
        stopping = true;
        if (!cancel_path.empty()) std::ofstream(cancel_path) << "cancel";
        thread.join();
    }
} worker;

// the end of a setup run, from its status file (setup.py run_randomizer: code_mods.rebuild's format plus the seed)
void finish(const json::Value& report, const std::string& fallback) {
    state.running = false;
    if (report.get("state").string() == "ready") {
        state.ready = true, state.error.clear();
        state.seed = report.get("seed").string(), state.hash = report.get("hash").string();
        state.imported = report.get("imported").boolean;
        state.stage.clear();
        return;
    }
    state.error = report.get("message").string(report.get("error").string(fallback));
    if (state.error.empty()) state.error = "Setup stopped; see setup.log in the install folder";
}

// the last lines of setup's output, when it ended without saying why in its status file
std::string tail(const std::string& output) {
    std::string out = output;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    const auto stopped = out.rfind("Setup stopped: ");
    if (stopped != std::string::npos) {
        out = out.substr(stopped + 15);
        const auto end = out.find('\n');
        return end == std::string::npos ? out : out.substr(0, end);
    }
    const auto line = out.rfind('\n');
    return line == std::string::npos ? out : out.substr(line + 1);
}
}  // namespace

#ifndef __ANDROID__
std::string unavailable() {
    if (mods::code::fixed_build()) return "The decomp version can't make randomized games: start the normal game for them.";
    try {
        fs::path data, script;
        mods::code::setup_command(data, &script);
        // setup.py is <release>/tools/installer/setup.py; the release's randomizer is <release>/tools/bin/wwhd-rando
        const fs::path release = script.parent_path().parent_path().parent_path();
        std::error_code ec;
#ifdef _WIN32
        const fs::path rando = release / "tools" / "bin" / "wwhd-rando.exe";
#else
        const fs::path rando = release / "tools" / "bin" / "wwhd-rando";
#endif
        if (!std::getenv("WWHD_RANDO_EXE") && !fs::is_regular_file(rando, ec))
            return "This release has no randomizer.";
    } catch (const std::exception& e) {
        return std::string("Setup is not available from the game here (") + e.what() + ").";
    }
    return {};
}

bool start(const Request& request, std::string& why) {
    std::lock_guard guard(mutex);
    if (state.running) return why = "A randomized game is being made already.", false;
    if (mods::code::status().building) return why = "The game code is being rebuilt for code mods: wait until that is done.", false;
    if (request.import && request.folder.empty()) return why = "Choose the official randomizer's output folder.", false;
    std::vector<std::string> command;
    try {
        fs::path data;
        command = mods::code::setup_command(data);
        status_path = data / "randomizer-status.json", cancel_path = data / "randomizer-cancel";
    } catch (const std::exception& e) {
        return why = e.what(), false;
    }
    std::error_code ec;
    fs::remove(status_path, ec), fs::remove(cancel_path, ec);
    if (request.import) {
        command.insert(command.end(), {"--import-seed", request.folder});
    } else {
        command.push_back("--randomize");
        if (!request.permalink.empty()) command.insert(command.end(), {"--rando-permalink", request.permalink});
        else {
            command.insert(command.end(), {"--rando-preset", request.preset.empty() ? "standard" : request.preset});
            if (request.seed.empty()) command.push_back("--rando-new-seed");
            else command.insert(command.end(), {"--rando-seed", request.seed});
        }
    }
    // the build with this game's code-mod choice, as the game plays now
    command.insert(command.end(), {"--code-mods", mods::code::enabled() ? "1" : "0", "--yes", "--code-mods-status",
                                   status_path.string(), "--code-mods-cancel", cancel_path.string(), "--jobs", "4"});
    if (worker.thread.joinable()) worker.thread.join();
    state = Status{};
    state.running = true;
    state.stage = request.import ? "Importing the seed" : "Making the randomized game";
    fprintf(stderr, "[randomizer] %s from the game\n", request.import ? "importing a seed" : "making a randomized game");
    worker.thread = std::thread([command] {
        auto result = host::run_process(command);
        std::lock_guard lock(mutex);
        json::Value report;
        try {
            report = mods::code::read_build_json(status_path);
        } catch (...) {
        }
        std::string fallback = result.error.empty() ? tail(result.output) : result.error;
        if (result.code != 0 && report.get("state").string() == "ready") report = json::Value{};  // (cannot be: setup failed after)
        finish(report, fallback);
        fprintf(stderr, "[randomizer] %s%s\n", state.ready ? "ready: " : "failed: ",
                state.ready ? (state.seed.empty() ? "?" : state.seed.c_str()) : state.error.c_str());
    });
    return true;
}

Status status() {
    std::lock_guard guard(mutex);
    // setup publishes its progress atomically; before the first publication a poll finds nothing
    if (state.running && !status_path.empty())
        if (auto v = mods::code::poll_build_json(status_path); v && v->get("state").string() == "building") {
            state.stage = v->get("stage").string(state.stage);
            state.done = unsigned(v->get("done").number), state.total = unsigned(v->get("total").number);
        }
    return state;
}

void cancel() {
    std::lock_guard guard(mutex);
    if (state.running && !cancel_path.empty()) std::ofstream(cancel_path) << "cancel";
}

#else  // __ANDROID__: the phone's setup service makes it (WwhdActivity.requestRandomizer / randomizerStatus)

std::string unavailable() { return host::android::randomizer_unavailable(); }

bool start(const Request& request, std::string& why) {
    std::lock_guard guard(mutex);
    if (state.running) return why = "A randomized game is being made already.", false;
    json::Value r;
    r["import"].type = json::Value::Bool, r["import"].boolean = request.import;
    r["preset"] = request.preset.empty() ? std::string("standard") : request.preset;
    r["seed"] = request.seed, r["permalink"] = request.permalink;
    if (const std::string error = host::android::request_randomizer(json::dump(r)); !error.empty()) return why = error, false;
    if (worker.thread.joinable()) worker.thread.join();
    state = Status{};
    state.running = true;
    state.stage = request.import ? "Choose the official randomizer's output folder" : "Starting setup";
    fprintf(stderr, "[randomizer] setup job requested from the game (%s)\n", request.import ? "import" : "new game");
    worker.thread = std::thread([] {
        while (!stopping) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            json::Value report;
            try {
                report = json::parse(host::android::randomizer_status());
            } catch (...) {
                continue;
            }
            std::lock_guard lock(mutex);
            if (!state.running) return;  // dismissed
            const std::string phase = report.get("state").string();
            if (phase == "cancelled") {  // the folder picker was closed
                state = Status{};
                return;
            }
            if (phase == "ready" || phase == "failed") {
                if (phase == "failed") report["message"] = report.get("error").string("Setup failed; open setup for details");
                finish(report, "Setup failed; open setup for details");
                fprintf(stderr, "[randomizer] %s %s\n", state.ready ? "ready" : "failed:", state.ready ? state.seed.c_str() : state.error.c_str());
                return;
            }
            state.stage = report.get("stage").string(state.stage);
            state.done = unsigned(report.get("done").number), state.total = unsigned(report.get("total").number);
        }
    });
    return true;
}

Status status() {
    std::lock_guard guard(mutex);
    return state;
}

void cancel() {}  // the setup screen and the setup notification pause the job
#endif

void dismiss() {
    std::lock_guard guard(mutex);
    if (state.running) {
#ifdef __ANDROID__
        state = Status{};  // stop following: the setup itself goes on
#endif
        return;
    }
    state = Status{};
}

}  // namespace randomizer_job
