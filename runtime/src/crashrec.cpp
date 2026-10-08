// Crash recovery (see crashrec.h).
//
// Automatic states are ordinary save states in <states>/auto/auto<n>.bin (save state slots 101..103,
// rotating). Right after one is captured, <states>/auto/auto<n>.input starts: one record per pad read
// on a logic pass (the input the game actually used), written unbuffered so a crash loses nothing.
//
// Replay (WWHD_REPLAY=<n>, optional WWHD_REPLAY_AT=<TV frame>, default 1500): once the game is
// running, automatic state n is loaded and the pad reads after the load return the recorded input in
// order; when the recording ends, live input takes over. Crash recovery saves nothing during a replay.
//
// On/off: Save States menu, or WWHD_CRASH_RECOVERY=0|1 for one start; the menu choice is kept in
// <states>/crash_recovery.cfg. WWHD_CRASH_RECOVERY_INTERVAL=<seconds> (default 120).
// Test aid: WWHD_TEST_CRASH_AT=<TV frame> crashes on purpose.
#include "crashrec.h"
#include "crash_context.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>

#include "gfx/renderer.h"
#include "runtime.h"
#include "savestate.h"

namespace crashrec {
namespace {

constexpr char kMagic[8] = {'W', 'W', 'H', 'D', 'I', 'N', 'P', '1'};
struct Record {
    uint32_t seq;
    uint8_t pad, touch, unused[2];
    uint32_t buttons;
    float lx, ly, rx, ry, tx, ty;
};
static_assert(sizeof(Record) == 36, "record layout");

std::string auto_dir() {
    std::string d = ss::states_dir() + "/auto";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
}
std::string state_path(int n) { return auto_dir() + "/auto" + std::to_string(n) + ".bin"; }
std::string input_path(int n) { return auto_dir() + "/auto" + std::to_string(n) + ".input"; }
std::string cfg_path() { return ss::states_dir() + "/crash_recovery.cfg"; }

std::mutex g_mu;
std::atomic<int> g_enabled{-1};  // -1: not read yet
int g_next = 1;                  // next automatic state to write
FILE* g_rec = nullptr;           // recording for the latest automatic state
uint32_t g_rec_seq = 0;
std::chrono::steady_clock::time_point g_last_save;
bool g_save_pending = false;

// replay
int g_replay = 0;                // automatic state being replayed (0: none)
FILE* g_play = nullptr;
bool g_play_started = false;
uint64_t g_replay_at = 1500;

// preformatted for crash handlers
char g_note[512] = "crash recovery: off\n";

void update_note(int latest, const char* when) {
    if (!enabled()) {
        snprintf(g_note, sizeof g_note, "crash recovery: off (Save States > Crash Recovery to turn it on)\n");
        return;
    }
    if (!latest) {
        snprintf(g_note, sizeof g_note, "crash recovery: on, no automatic state saved yet in this session\n");
        return;
    }
    snprintf(g_note, sizeof g_note,
             "crash recovery: latest automatic state %s (saved %s), input since then in %s\n"
             "  reproduce with: WWHD_REPLAY=%d ./build/cmake/wwhd   (loads it and replays the input)\n",
             state_path(latest).c_str(), when, input_path(latest).c_str(), latest);
}

bool read_cfg() {
    if (const char* e = getenv("WWHD_CRASH_RECOVERY")) return atoi(e) != 0;
    if (FILE* f = fopen(cfg_path().c_str(), "r")) {
        int v = 0;
        bool ok = fscanf(f, "%d", &v) == 1;
        fclose(f);
        if (ok) return v != 0;
    }
    return false;  // off unless chosen
}

void start_recording(int n) {
    if (g_rec) fclose(g_rec);
    g_rec = fopen(input_path(n).c_str(), "wb");
    g_rec_seq = 0;
    if (!g_rec) { LOG("[crashrec] can't write %s", input_path(n).c_str()); return; }
    setvbuf(g_rec, nullptr, _IONBF, 0);
    fwrite(kMagic, 1, 8, g_rec);
}

}  // namespace

bool enabled() {
    int v = g_enabled.load();
    if (v < 0) {
        v = read_cfg() ? 1 : 0;
        g_enabled = v;
        if (const char* r = getenv("WWHD_REPLAY")) g_replay = atoi(r);
        if (const char* a = getenv("WWHD_REPLAY_AT")) g_replay_at = strtoull(a, nullptr, 10);
        if (g_replay < 1 || g_replay > kAutoSlots) g_replay = 0;
        if (g_replay) LOG("[crashrec] replay of automatic state %d from frame %llu", g_replay, (unsigned long long)g_replay_at);
        if (v && !g_replay) LOG("[crashrec] on: automatic state every %d s", interval_seconds());
        update_note(0, "");
    }
    return v != 0 && !g_replay;
}

void set_enabled(bool on) {
    enabled();  // make sure the replay settings are read
    g_enabled = on ? 1 : 0;
    if (FILE* f = fopen(cfg_path().c_str(), "w")) { fprintf(f, "%d\n", on ? 1 : 0); fclose(f); }
    std::lock_guard<std::mutex> lk(g_mu);
    g_last_save = std::chrono::steady_clock::now();
    if (!on && g_rec) { fclose(g_rec); g_rec = nullptr; }
    update_note(0, "");
    LOG("[crashrec] %s", on ? "on" : "off");
}

int interval_seconds() {
    static const int s = [] {
        const char* e = getenv("WWHD_CRASH_RECOVERY_INTERVAL");
        int v = e ? atoi(e) : 120;
        return v < 10 ? 10 : v;
    }();
    return s;
}

void service() {
    static auto next_context = std::chrono::steady_clock::time_point{};
    auto context_now = std::chrono::steady_clock::now();
    if (context_now >= next_context) {
        crash_context::refresh();
        next_context = context_now + std::chrono::seconds(1);
    }
    bool on = enabled();
    // test aid: WWHD_TEST_CRASH_AT=<TV frame> crashes on purpose (checks the crash log and replay hints)
    static const uint64_t crash_at = getenv("WWHD_TEST_CRASH_AT") ? strtoull(getenv("WWHD_TEST_CRASH_AT"), nullptr, 10) : 0;
    if (crash_at && render::frame_count() >= crash_at) {
        LOG("[crashrec] WWHD_TEST_CRASH_AT: crashing on purpose");
        *(volatile uint32_t*)(uintptr_t)0x10 = 0;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_replay && !g_play_started && render::frame_count() >= g_replay_at) {
        g_play_started = true;
        g_play = fopen(input_path(g_replay).c_str(), "rb");
        char m[8] = {};
        if (!g_play || fread(m, 1, 8, g_play) != 8 || memcmp(m, kMagic, 8) != 0) {
            LOG("[crashrec] no input recording %s; loading the state only", input_path(g_replay).c_str());
            if (g_play) fclose(g_play);
            g_play = nullptr;
        }
        ss::request_load(kAutoBase + g_replay);
    }
    if (!on) return;
    auto now = std::chrono::steady_clock::now();
    if (g_last_save.time_since_epoch().count() == 0) g_last_save = now;
    if (g_save_pending && now - g_last_save >= std::chrono::seconds(interval_seconds() + 60)) {
        g_save_pending = false;  // the game stayed busy (ss gives up after 30 tries): try again next interval
        g_last_save = now;
    }
    if (!g_save_pending && now - g_last_save >= std::chrono::seconds(interval_seconds())) {
        g_save_pending = true;
        ss::request_save(kAutoBase + g_next);
    }
}

void on_auto_saved(int n) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_save_pending = false;
    g_last_save = std::chrono::steady_clock::now();
    g_next = n % kAutoSlots + 1;
    start_recording(n);
    time_t t = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char when[32];
    strftime(when, sizeof when, "%H:%M:%S", &tmv);
    update_note(n, when);
}

input::PadState read(int pad) {
    if (g_replay) {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_play && ss::last_load_frame() != 0) {
            Record r;
            if (fread(&r, sizeof r, 1, g_play) == 1) {
                if (r.pad == pad) {
                    input::PadState p;
                    p.buttons = r.buttons;
                    p.lx = r.lx; p.ly = r.ly; p.rx = r.rx; p.ry = r.ry;
                    p.touch = r.touch != 0;
                    p.tx = r.tx; p.ty = r.ty;
                    return p;
                }
                fseek(g_play, -(long)sizeof r, SEEK_CUR);  // the other pad's read comes first: not ours yet
            } else {
                LOG("[crashrec] replay: recorded input ended, live input from now on");
                fclose(g_play);
                g_play = nullptr;
            }
        }
        if (g_play) return input::PadState{};  // replay pending: no live input until it is used up
        return input::read();
    }
    input::PadState p = input::read();
    if (g_rec && enabled()) {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_rec) {
            Record r{};
            r.seq = g_rec_seq++;
            r.pad = (uint8_t)pad;
            r.touch = p.touch;
            r.buttons = p.buttons;
            r.lx = p.lx; r.ly = p.ly; r.rx = p.rx; r.ry = p.ry;
            r.tx = p.tx; r.ty = p.ty;
            fwrite(&r, sizeof r, 1, g_rec);
        }
    }
    return p;
}

AutoInfo auto_info(int n) {
    AutoInfo a;
    ss::SlotInfo s = ss::slot_info(kAutoBase + n);
    a.used = s.used && s.compatible;
    a.when = s.when;
    a.area = s.area;
    return a;
}

void request_load(int n) { ss::request_load(kAutoBase + n); }

void crash_note(int fd, void (*out)(int, const char*, size_t)) {
    out(fd, g_note, strnlen(g_note, sizeof g_note));
}

}  // namespace crashrec
